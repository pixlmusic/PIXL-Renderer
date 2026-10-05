#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Renderer/TemporalContext.h"
#include "Renderer/PixelAnnotations.h"
#include "Renderer/ReconstructionContext.h"
#include "Renderer/LightTransportWorld.h"
#include "Renderer/D3D11BindingScope.h"
#include <cassert>

int main()
{
	using namespace PIXL::Renderer;
	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11DeviceContext> context;
	const auto created = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
		nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put());
	assert(SUCCEEDED(created));
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = 64; desc.Height = 32; desc.MipLevels = 1; desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8_UNORM; desc.SampleDesc.Count = 1;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	winrt::com_ptr<ID3D11Texture2D> texture;
	winrt::com_ptr<ID3D11ShaderResourceView> view;
	assert(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.put())));
	assert(SUCCEEDED(device->CreateShaderResourceView(texture.get(), nullptr, view.put())));

	auto& temporal = TemporalContext::Get();
	TemporalFrameInput input{};
	input.frameIndex = 1; input.renderWidth = 64; input.renderHeight = 32;
	input.outputWidth = 64; input.outputHeight = 32; input.motionVectors = view.get();
	input.extent = { 64, 32, { 0, 0, 64, 32 }, 64, 32, ResolutionDomain::ActiveRender };
	input.token = { input.frameIndex, { 1 }, ViewType::MainWorld, 1 };
	assert(input.extent.Valid());
	input.verticalFov = 1.0f;
	int resets = 0;
	const auto history = temporal.RegisterHistory({ .name = "handoff test", .owner = "test",
		.reset = [&](TemporalInvalidationReason) { ++resets; } });
	temporal.BeginFrame(input);
	assert(!temporal.GetFrameSnapshot().previousFrameValid);
	++input.frameIndex;
	input.token.frame = input.frameIndex;
	temporal.BeginFrame(input);
	assert(temporal.GetFrameSnapshot().previousFrameValid);
	assert(!temporal.GetFrameSnapshot().previous.motionVectors);
	temporal.Invalidate(TemporalInvalidationReason::SettingsChange);
	assert(resets == 1);
	++input.frameIndex;
	input.token.frame = input.frameIndex;
	temporal.BeginFrame(input);
	assert(!temporal.GetFrameSnapshot().previousFrameValid);
	assert(resets == 1); // Carried invalidation must not duplicate callbacks.
	++input.frameIndex;
	input.token.frame = input.frameIndex;
	temporal.BeginFrame(input);
	assert(temporal.GetFrameSnapshot().previousFrameValid);
	temporal.PublishDisocclusion(input.token, view.get(), 32, 16);
	assert(!temporal.GetFrameSnapshot().motion.disocclusion);
	assert(temporal.GetFrameSnapshot().motion.width == 64);
	temporal.PublishDisocclusion(input.token, view.get(), 64, 32);
	assert(temporal.GetFrameSnapshot().motion.disocclusion);
	input.frameIndex += 2;
	input.token.frame = input.frameIndex;
	temporal.BeginFrame(input);
	assert(!temporal.GetFrameSnapshot().previousFrameValid);
	auto auxiliary = input;
	auxiliary.frameIndex++;
	auxiliary.token.frame = auxiliary.frameIndex;
	auxiliary.token.view = ViewType::Cubemap;
	auxiliary.viewType = ViewType::Cubemap;
	temporal.BeginFrame(auxiliary);
	assert(temporal.GetFrameSnapshot().current.frameIndex == input.frameIndex);
	temporal.Invalidate(TemporalInvalidationReason::DeviceReset);
	assert(!temporal.GetFrameSnapshot().motion.motionVectors);
	assert(!temporal.GetFrameSnapshot().current.motionVectors);
	temporal.UnregisterHistory(history);

	D3D11_TEXTURE2D_DESC copyDesc = desc;
	copyDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	winrt::com_ptr<ID3D11Texture2D> copySource, copyDestination;
	winrt::com_ptr<ID3D11RenderTargetView> sourceRTV;
	winrt::com_ptr<ID3D11ShaderResourceView> destinationSRV;
	assert(SUCCEEDED(device->CreateTexture2D(&copyDesc, nullptr, copySource.put())));
	assert(SUCCEEDED(device->CreateTexture2D(&copyDesc, nullptr, copyDestination.put())));
	assert(SUCCEEDED(device->CreateRenderTargetView(copySource.get(), nullptr, sourceRTV.put())));
	assert(SUCCEEDED(device->CreateShaderResourceView(copyDestination.get(), nullptr, destinationSRV.put())));
	ID3D11RenderTargetView* rtv = sourceRTV.get();
	ID3D11ShaderResourceView* srv = destinationSRV.get();
	context->OMSetRenderTargets(1, &rtv, nullptr);
	context->PSSetShaderResources(17, 1, &srv);
	{
		ScopedD3D11BindingState bindingScope(context.get());
		assert(bindingScope.PrepareForCopy(copySource.get(), copyDestination.get()));
		winrt::com_ptr<ID3D11RenderTargetView> boundRTV;
		ID3D11RenderTargetView* rawRTV{};
		context->OMGetRenderTargets(1, &rawRTV, nullptr);
		boundRTV.attach(rawRTV);
		assert(!boundRTV);
		winrt::com_ptr<ID3D11ShaderResourceView> boundSRV;
		ID3D11ShaderResourceView* rawSRV{};
		context->PSGetShaderResources(17, 1, &rawSRV);
		boundSRV.attach(rawSRV);
		assert(!boundSRV);
		assert(bindingScope.ConflictCount() >= 2);
		context->CopyResource(copyDestination.get(), copySource.get());
	}
	ID3D11RenderTargetView* restoredRTV{};
	context->OMGetRenderTargets(1, &restoredRTV, nullptr);
	assert(restoredRTV == sourceRTV.get());
	if (restoredRTV) restoredRTV->Release();
	ID3D11ShaderResourceView* restoredSRV{};
	context->PSGetShaderResources(17, 1, &restoredSRV);
	assert(restoredSRV == destinationSRV.get());
	if (restoredSRV) restoredSRV->Release();
	ID3D11ShaderResourceView* nullSRV{};
	context->PSSetShaderResources(17, 1, &nullSRV);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto& annotations = PixelAnnotations::Get();
	annotations.BeginFrame(1);
	annotations.PublishBase(view.get(), view.get(), view.get(), 64, 32);
	annotations.PublishReconstruction(view.get(), view.get(), 32, 16);
	assert(!annotations.Acquire().reactiveMask);
	assert(annotations.Acquire().width == 64);
	annotations.PublishReconstruction(view.get(), view.get(), 64, 32);
	assert(annotations.Acquire().reactiveMask);
	annotations.PublishBase(view.get(), view.get(), view.get(), 64, 32);
	assert(!annotations.Acquire().reactiveMask);
	annotations.Invalidate();
	assert(!annotations.Acquire().valid && !annotations.Acquire().deferredMaterialMask);
	const FrameToken annotationToken{ 3, { 7 }, ViewType::MainWorld, 1 };
	annotations.BeginFrame(annotationToken, input.extent);
	auto staleAnnotationToken = annotationToken;
	staleAnnotationToken.resources.value = 6;
	annotations.PublishCompact(staleAnnotationToken, input.extent, view.get());
	assert(!annotations.Acquire().compactClassFlags);
	annotations.PublishCompact(annotationToken, input.extent, view.get());
	assert(annotations.Acquire().compactClassFlags);

	auto& lightTransport = LightTransportWorld::Get();
	lightTransport.BeginFrame(annotationToken.frame);
	lightTransport.PublishLocalLights(annotationToken, input.extent, view.get(), view.get(), view.get(), 0, 1, 1);
	assert(lightTransport.AcquireLocalLights(annotationToken).ValidFor(annotationToken));
	assert(!lightTransport.AcquireLocalLights(staleAnnotationToken).lights);
	lightTransport.PublishProbe(annotationToken, input.extent, ProbeKind::SkyVisibility,
		view.get(), 64, 32, 1, CoordinateSpace::CameraRelativeWorld, ResolutionDomain::Backing);
	assert(lightTransport.AcquireProbe(ProbeKind::SkyVisibility, annotationToken).ValidFor(annotationToken));
	assert(!lightTransport.AcquireProbe(ProbeKind::SkyVisibility, staleAnnotationToken).resource);
	lightTransport.Invalidate("test resource recreation");
	assert(!lightTransport.AcquireLocalLights(annotationToken).lights);

	auto& reconstruction = ReconstructionContext::Get();
	int calls = 0;
	const auto contributor = reconstruction.RegisterReactiveContributor("test",
		[&](ID3D11UnorderedAccessView*, std::uint32_t, std::uint32_t) { ++calls; });
	reconstruction.ApplyReactiveContributors(nullptr, 64, 32);
	reconstruction.ApplyReactiveContributors(FrameToken{}, RenderExtent{}, nullptr, 64, 32);
	assert(calls == 0);
	reconstruction.BeginFrame(1);
	ReconstructionFrame frame{};
	frame.frame = 1; frame.renderWidth = 64; frame.renderHeight = 32;
	frame.depth = view; frame.backend = ReconstructionBackend::DLSS;
	reconstruction.Publish(std::move(frame));
	reconstruction.BeginFrame(1);
	assert(reconstruction.Acquire().depth);
	reconstruction.BeginFrame(2);
	assert(!reconstruction.Acquire().depth);
	assert(reconstruction.GetDiagnostics().contributors == 1);
	reconstruction.BeginFrame(annotationToken, input.extent);
	ReconstructionFrame staleFrame{};
	staleFrame.frame = annotationToken.frame;
	staleFrame.token = staleAnnotationToken;
	staleFrame.extent = input.extent;
	staleFrame.depth = view;
	reconstruction.Publish(std::move(staleFrame));
	assert(!reconstruction.Acquire(annotationToken).depth);
	ReconstructionFrame currentFrame{};
	currentFrame.frame = annotationToken.frame;
	currentFrame.token = annotationToken;
	currentFrame.extent = input.extent;
	currentFrame.depth = view;
	reconstruction.Publish(std::move(currentFrame));
	assert(reconstruction.Acquire(annotationToken).depth);
	reconstruction.Invalidate();
	assert(!reconstruction.GetDiagnostics().valid);
	reconstruction.UnregisterReactiveContributor(contributor);
	return 0;
}
