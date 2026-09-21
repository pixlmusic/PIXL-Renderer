#include "Profiler.h"

#include <algorithm>
#include <format>
#include <unordered_map>

float Profiler::RollingHistory::GetAverage() const
{
	if (count == 0)
		return lastMs;
	float sum = 0.0f;
	for (uint32_t i = 0; i < count; i++)
		sum += history[i];
	return sum / static_cast<float>(count);
}

float Profiler::RollingHistory::GetPercentile(float p) const
{
	if (count == 0)
		return lastMs;
	p = std::clamp(p, 0.0f, 100.0f);

	thread_local std::vector<float> sorted;
	sorted.resize(count);
	for (uint32_t i = 0; i < count; i++)
		sorted[i] = history[i];
	float idx = (p / 100.0f) * static_cast<float>(count - 1);
	uint32_t lo = static_cast<uint32_t>(idx);
	uint32_t hi = std::min(lo + 1, count - 1);
	float frac = idx - static_cast<float>(lo);
	// Select the two order statistics instead of sorting all 300 samples four
	// times per pass per frame. This preserves percentile interpolation exactly.
	std::nth_element(sorted.begin(), sorted.begin() + hi, sorted.end());
	const float high = sorted[hi];
	const float low = lo == hi ? high : *std::max_element(sorted.begin(), sorted.begin() + hi);
	return low * (1.0f - frac) + high * frac;
}

void Profiler::Initialize(ID3D11Device* device, ID3D11DeviceContext* a_context)
{
	Release();

	if (!device || !a_context) {
		logger::warn("[PIXL Profiler] D3D11 device/context unavailable; GPU timing disabled");
		return;
	}

	context = a_context;

	LARGE_INTEGER freq{};
	if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) {
		logger::warn("[PIXL Profiler] High-resolution CPU timer unavailable; profiling disabled");
		Release();
		return;
	}
	cpuTicksToMs = 1000.0 / static_cast<double>(freq.QuadPart);

	for (auto& frame : frames) {
		D3D11_QUERY_DESC disjointDesc{};
		disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		if (FAILED(device->CreateQuery(&disjointDesc, frame.disjoint.put())) || !frame.disjoint) {
			logger::warn("[PIXL Profiler] Could not allocate a timestamp-disjoint query; profiling disabled");
			Release();
			return;
		}

		frame.timers.resize(kMaxTimers);
		for (auto& timer : frame.timers) {
			D3D11_QUERY_DESC tsDesc{};
			tsDesc.Query = D3D11_QUERY_TIMESTAMP;
			if (FAILED(device->CreateQuery(&tsDesc, timer.begin.put())) || !timer.begin ||
				FAILED(device->CreateQuery(&tsDesc, timer.end.put())) || !timer.end) {
				logger::warn("[PIXL Profiler] Could not allocate timestamp queries; profiling disabled");
				Release();
				return;
			}
		}
		frame.activeCount = 0;
		frame.inFlight = false;
	}

	writeFrame = 0;
	readFrame = 0;
	framesSinceInit = 0;
	collectedFrameCount = 0;
	initialized = true;
}

void Profiler::Release()
{
	for (auto& frame : frames) {
		frame.disjoint = nullptr;
		frame.timers.clear();
		frame.activeCount = 0;
		frame.inFlight = false;
	}
	results.clear();
	knownTimers.clear();
	knownTimerIndex.clear();
	totalTimeMs = 0.0f;
	cpuTotalTimeMs = 0.0f;
	writeFrame = 0;
	readFrame = 0;
	framesSinceInit = 0;
	collectedFrameCount = 0;
	frameActive = false;
	frameSkipped = false;
	cpuTicksToMs = 0.0;
	initialized = false;
	context = nullptr;
}

void Profiler::BeginFrame()
{
	if (!initialized || !context || frameActive || frameSkipped)
		return;

	CollectResults();

	auto& frame = frames[writeFrame];
	// A busy GPU may need more than three frames. Never overwrite pending
	// timestamps or stall rendering to wait for diagnostic data.
	if (frame.inFlight) {
		frameSkipped = true;
		return;
	}
	frame.activeCount = 0;
	frame.inFlight = true;
	frameActive = true;
	context->Begin(frame.disjoint.get());
}

void Profiler::BeginPass(const std::string& name)
{
	if (!initialized || !context)
		return;

	if (!frameActive)
		BeginFrame();
	if (!frameActive)
		return;

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers)
		return;

	auto& timer = frame.timers[frame.activeCount];
	timer.name = name;
	context->End(timer.begin.get());
	QueryPerformanceCounter(&timer.cpuBegin);

	if (beginPerfEvent)
		beginPerfEvent(name);
}

void Profiler::EndPass()
{
	if (!initialized || !context || !frameActive)
		return;

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers)
		return;

	auto& timer = frame.timers[frame.activeCount];

	LARGE_INTEGER cpuEnd;
	QueryPerformanceCounter(&cpuEnd);
	timer.cpuMs = static_cast<float>(static_cast<double>(cpuEnd.QuadPart - timer.cpuBegin.QuadPart) * cpuTicksToMs);

	context->End(timer.end.get());
	frame.activeCount++;

	if (endPerfEvent)
		endPerfEvent({});
}

void Profiler::EndFrame()
{
	if (frameSkipped) {
		frameSkipped = false;
		return;
	}
	if (!initialized || !context || !frameActive)
		return;

	frameActive = false;
	context->End(frames[writeFrame].disjoint.get());
	writeFrame = (writeFrame + 1) % kFrameLatency;
	framesSinceInit++;
}

void Profiler::CollectResults()
{
	if (!initialized || !context)
		return;

	readFrame = writeFrame;
	auto& frame = frames[readFrame];
	if (!frame.inFlight)
		return;

	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
	HRESULT hr = context->GetData(frame.disjoint.get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
	if (hr != S_OK)
		return;

	if (disjointData.Disjoint || disjointData.Frequency == 0) {
		frame.inFlight = false;
		return;
	}

	struct ActiveTimerData
	{
		float gpuMs = 0.0f;
		float cpuMs = 0.0f;
	};
	std::unordered_map<std::string, ActiveTimerData> activeTimers;
	float activeTotalMs = 0.0f;
	float activeCpuTotalMs = 0.0f;

	if (!disjointData.Disjoint && disjointData.Frequency > 0) {
		double ticksToMs = 1000.0 / static_cast<double>(disjointData.Frequency);

		for (uint32_t i = 0; i < frame.activeCount; i++) {
			auto& timer = frame.timers[i];
			UINT64 tsBegin = 0, tsEnd = 0;

			if (context->GetData(timer.begin.get(), &tsBegin, sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				return;
			if (context->GetData(timer.end.get(), &tsEnd, sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				return;
			if (tsEnd < tsBegin) {
				frame.inFlight = false;
				return;
			}

			float ms = static_cast<float>(static_cast<double>(tsEnd - tsBegin) * ticksToMs);
			auto& entry = activeTimers[timer.name];
			entry.gpuMs += ms;
			entry.cpuMs += timer.cpuMs;
			activeTotalMs += ms;
			activeCpuTotalMs += timer.cpuMs;

		}
	}
	frame.inFlight = false;
	// Commit only a complete frame, aggregating repeated calls before history.
	for (const auto& [name, sample] : activeTimers) {
		auto [it, inserted] = knownTimerIndex.try_emplace(name, knownTimers.size());
		if (inserted) {
			KnownTimer kt;
			kt.name = name;
			knownTimers.push_back(std::move(kt));
		}
		auto& known = knownTimers[it->second];
		known.gpu.PushSample(sample.gpuMs);
		known.cpu.PushSample(sample.cpuMs);
	}

	totalTimeMs = activeTotalMs;
	cpuTotalTimeMs = activeCpuTotalMs;

	results.clear();
	results.reserve(knownTimers.size());
	for (const auto& known : knownTimers) {
		TimerResult result;
		result.name = known.name;
		auto it = activeTimers.find(known.name);
		if (it != activeTimers.end()) {
			result.gpuTimeMs = it->second.gpuMs;
			result.cpuTimeMs = it->second.cpuMs;
		} else {
			result.gpuTimeMs = 0.0f;
			result.cpuTimeMs = 0.0f;
		}
		result.avgMs = known.gpu.GetAverage();
		result.p95Ms = known.gpu.GetPercentile(95.0f);
		result.p99Ms = known.gpu.GetPercentile(99.0f);
		result.cpuAvgMs = known.cpu.GetAverage();
		result.cpuP95Ms = known.cpu.GetPercentile(95.0f);
		result.cpuP99Ms = known.cpu.GetPercentile(99.0f);
		result.valid = it != activeTimers.end();
		result.historyBuffer = known.gpu.history;
		result.historyHead = known.gpu.head;
		result.historyCount = known.gpu.count;
		results.push_back(std::move(result));
	}

	// Emit a compact periodic trace that can be correlated with an in-game
	// profiler screenshot without logging every frame. This deliberately reports
	// the individual GPU passes rather than the shader-type buckets shown by the
	// overlay, making expensive modules such as Radiance Weave immediately clear.
	++collectedFrameCount;
	if (collectedFrameCount % 120u == 0u && !results.empty()) {
		std::vector<size_t> order;
		for (size_t i = 0; i < results.size(); ++i)
			if (results[i].valid)
				order.push_back(i);
		std::sort(order.begin(), order.end(), [this](size_t lhs, size_t rhs) {
			return results[lhs].gpuTimeMs > results[rhs].gpuTimeMs;
		});
		std::string topPasses;
		const size_t count = order.size();
		for (size_t i = 0; i < count; ++i) {
			if (i > 0)
				topPasses += " | ";
			const auto& result = results[order[i]];
			topPasses += std::format("{}={:.2f}ms (avg {:.2f}, p95 {:.2f}, CPU {:.2f})", result.name, result.gpuTimeMs, result.avgMs, result.p95Ms, result.cpuTimeMs);
		}
		logger::info(
			"[PIXL Perf] Profiled pass sums: GPU {:.2f}ms CPU submission {:.2f}ms | {}",
			totalTimeMs,
			cpuTotalTimeMs,
			topPasses);
	}
}
