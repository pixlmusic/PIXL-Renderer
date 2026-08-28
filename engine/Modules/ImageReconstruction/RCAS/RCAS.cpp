#include "RCAS.h"

#include "../../../Deferred.h"
#include "../../../State.h"
#include "../../../Util.h"

struct RCASConfig
{
	float sharpness;
	uint32_t useConfidence;
	float2 inputDimensions;
};

static_assert(sizeof(RCASConfig) == 16, "RCASConfig ABI mismatch");

RCAS::~RCAS()
{
	delete rcasConfigCB;
	rcasConfigCB = nullptr;
}

void RCAS::Initialize()
{
	if (rcasConfigCB)
		return;

	logger::info("[RCAS] Creating resources");
	CreateComputeShader();
	rcasConfigCB = new ConstantBuffer(ConstantBufferDesc<RCASConfig>());
}

void RCAS::CreateComputeShader()
{
	std::vector<std::pair<const char*, const char*>> defines;
	rcasComputeShader.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\ImageReconstruction\\RCAS\\RCAS.hlsl", defines, "cs_5_0"));
}

void RCAS::ApplySharpen(
	ID3D11ShaderResourceView* inputSRV,
	ID3D11UnorderedAccessView* outputUAV,
	float sharpness,
	ID3D11ShaderResourceView* reactiveMask,
	ID3D11ShaderResourceView* transparencyMask,
	ID3D11ShaderResourceView* motionVectors,
	float2 inputDimensions)
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "RCAS Sharpening");

	auto state = globals::state;
	auto context = globals::d3d::context;

	if (!rcasComputeShader) {
		logger::warn("[RCAS] Compute shader not compiled");
		return;
	}

	globals::profiler->BeginPass("ImageReconstruction::RCAS");
	state->BeginPerfEvent("RCAS Sharpening");

	uint32_t screenWidth = globals::game::graphicsState->screenWidth;
	uint32_t screenHeight = globals::game::graphicsState->screenHeight;

	RCASConfig config{};
	config.sharpness = sharpness;
	config.useConfidence = reactiveMask && transparencyMask && motionVectors && inputDimensions.x > 0.0f && inputDimensions.y > 0.0f;
	config.inputDimensions = config.useConfidence ? inputDimensions : float2{};

	rcasConfigCB->Update(config);
	auto bufferArray = rcasConfigCB->CB();

	context->CSSetShader(rcasComputeShader.get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &bufferArray);

	ID3D11ShaderResourceView* srvs[] = { inputSRV, reactiveMask, transparencyMask, motionVectors };
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

	ID3D11UnorderedAccessView* uavs[] = { outputUAV };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	uint32_t dispatchX = (screenWidth + 7) / 8;
	uint32_t dispatchY = (screenHeight + 7) / 8;
	context->Dispatch(dispatchX, dispatchY, 1);

	ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);

	ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

	context->CSSetShader(nullptr, nullptr, 0);

	globals::profiler->EndPass();
	state->EndPerfEvent();
}
