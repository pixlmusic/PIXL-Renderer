#pragma once

#include <d3d11.h>
#include <cstdint>
#include <memory>

namespace MaterialForgeTessellation
{
	enum class HeightSource : std::uint8_t
	{
		PBRRed,
		ComplexMaterialAlpha
	};

	/** Arms one compatible indexed Lighting draw. Zero scale clears any pending draw. */
	void ArmForNextDraw(
		float a_displacementScale,
		float a_maximumTessellationFactor,
		HeightSource a_heightSource = HeightSource::PBRRed,
		float a_fadeStart = 768.0f,
		float a_fadeEnd = 1792.0f) noexcept;

	/** Temporarily installs the MaterialForge hull/domain stages for one draw. */
	class DrawState
	{
	public:
		DrawState(ID3D11DeviceContext* a_context, bool a_allowTessellation);
		~DrawState();

		DrawState(const DrawState&) = delete;
		DrawState& operator=(const DrawState&) = delete;

		[[nodiscard]] bool IsActive() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
}
