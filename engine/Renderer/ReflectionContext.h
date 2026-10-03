// PIXL Renderer - shared hybrid reflection publication and diagnostics.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <mutex>
#include <d3d11.h>
#include <winrt/base.h>
namespace PIXL::Renderer
{
	struct ReflectionFrame
	{
		winrt::com_ptr<ID3D11ShaderResourceView> radianceConfidence;
		std::uint32_t width{},height{},traceSteps{};
		float maxDistance{},maxRoughness{},thickness{};
		bool screenTrace{},worldFallback{},temporal{},spatial{},valid{};
	};
	class ReflectionContext
	{
	public:
		static ReflectionContext& Get();
		void Publish(ReflectionFrame frame);
		void Invalidate();
		[[nodiscard]] ReflectionFrame Acquire() const;
	private: mutable std::mutex mutex; ReflectionFrame frame;
	};
}
