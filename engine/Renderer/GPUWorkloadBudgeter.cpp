// PIXL Renderer - hysteretic adaptive GPU workload controller.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
#include "GPUWorkloadBudgeter.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
namespace PIXL::Renderer
{
	namespace { constexpr std::array<float,4> kScales{.50f,.67f,.84f,1.0f}; constexpr std::uint32_t kHold=120; }
	GPUWorkloadBudgeter& GPUWorkloadBudgeter::Get(){static GPUWorkloadBudgeter value;return value;}
	void GPUWorkloadBudgeter::SetTargetFrameMs(float value) noexcept { if(std::isfinite(value))targetFrameMs=std::clamp(value,8.0f,50.0f); }
	void GPUWorkloadBudgeter::SetMaximumLevel(WorkloadDomain domain,std::uint8_t level) noexcept {auto& s=states[static_cast<std::size_t>(domain)];s.maximumLevel=std::min<std::uint8_t>(level,3);s.level=std::min(s.level,s.maximumLevel);}
	float GPUWorkloadBudgeter::GetScale(WorkloadDomain domain) const noexcept {return enabled?kScales[states[static_cast<std::size_t>(domain)].level]:1.0f;}
	void GPUWorkloadBudgeter::Update(const Profiler& profiler,float deltaSeconds)
	{
		std::array<float,static_cast<std::size_t>(WorkloadDomain::Count)> samples{};
		std::array<bool,static_cast<std::size_t>(WorkloadDomain::Count)> sampled{};
		for(const auto& timer:profiler.GetResults())if(timer.valid&&std::isfinite(timer.gpuTimeMs)&&timer.gpuTimeMs>=0.0f){
			// Hybrid reflections share the GI module's timer prefix but have a separate ray budget.
			if(timer.name.starts_with("HybridGI::HybridReflection")){
				const auto index=static_cast<std::size_t>(WorkloadDomain::Reflections);
				samples[index]+=timer.gpuTimeMs;sampled[index]=true;
				continue;
			}
			for(std::size_t i=0;i<samples.size();++i){if(timer.name.starts_with(ToString(static_cast<WorkloadDomain>(i)))){samples[i]+=timer.gpuTimeMs;sampled[i]=true;break;}}
		}
		const float safeDelta=std::isfinite(deltaSeconds)?std::clamp(deltaSeconds,0.0f,.1f):0.0f;
		const float response=1.0f-std::exp(-safeDelta*2.0f), domainTarget=targetFrameMs*.28f;
		for(std::size_t i=0;i<states.size();++i){auto& s=states[i];if(!sampled[i]){s.lastMs=0.0f;s.reason="no GPU timing sample";continue;}s.lastMs=samples[i];s.averageMs=s.averageMs==0?samples[i]:std::lerp(s.averageMs,samples[i],response);if(s.holdFrames)--s.holdFrames;if(!enabled||s.holdFrames)continue;if(s.averageMs>domainTarget*1.10f&&s.level>0){--s.level;s.holdFrames=kHold;s.reason="sustained over budget";}else if(s.averageMs<domainTarget*.72f&&s.level<s.maximumLevel){++s.level;s.holdFrames=kHold;s.reason="sustained headroom";}}
	}
	std::string_view GPUWorkloadBudgeter::ToString(WorkloadDomain d) noexcept {switch(d){case WorkloadDomain::HybridGI:return "HybridGI";case WorkloadDomain::SkyBounce:return "SkyBounce";case WorkloadDomain::Atmosphere:return "Atmosphere";case WorkloadDomain::Reflections:return "Reflection";case WorkloadDomain::GroundResponse:return "GroundResponse";case WorkloadDomain::Water:return "Water";case WorkloadDomain::CameraSuite:return "CameraSuite";case WorkloadDomain::Reconstruction:return "ImageReconstruction";default:return "Unknown";}}
}
