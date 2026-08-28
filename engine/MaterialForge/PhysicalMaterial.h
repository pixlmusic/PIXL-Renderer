#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/**
 * @brief Backend-neutral physical-material contract used by raster and future ray-tracing backends.
 *
 * This namespace deliberately contains no Skyrim, Direct3D, or Vulkan types. Game materials are
 * resolved into Descriptor once, then a backend adapter binds the textures and constants it needs.
 * Descriptor is an authoring/runtime semantic contract, not a GPU constant-buffer layout.
 */
namespace PhysicalMaterial
{
	inline constexpr std::uint32_t SchemaVersion = 3;
	inline constexpr std::size_t MaxTextureBindings = 9;

	using MaterialID = std::uint64_t;
	using TextureID = std::uint64_t;

	/** @brief BRDF interpretation used by backend material evaluators. */
	enum class ShadingModel : std::uint32_t
	{
		LegacySpecular,
		MetallicRoughness,
	};

	/** @brief Expected transfer function for a sampled semantic texture. */
	enum class ColorSpace : std::uint32_t
	{
		Linear,
		SRGB,
	};

	/** @brief Physical lobes and optional material behavior. */
	enum class MaterialTrait : std::uint32_t
	{
		Subsurface = 1u << 0,
		TwoLayer = 1u << 1,
		ColoredCoat = 1u << 2,
		InterlayerParallax = 1u << 3,
		CoatNormal = 1u << 4,
		Fuzz = 1u << 5,
		HairMarschner = 1u << 6,
		Glint = 1u << 7,
		ProjectedGlint = 1u << 8,
		/** MaterialForge classified this surface as layered animal/fabric fur. */
		FurShell = 1u << 9,
	};

	/** @brief Semantic textures available to a physical material. */
	enum class Texture : std::uint32_t
	{
		BaseColor = 1u << 0,
		Normal = 1u << 1,
		Rmaos = 1u << 2,
		Emissive = 1u << 3,
		Displacement = 1u << 4,
		Features0 = 1u << 5,
		Features1 = 1u << 6,
		Environment = 1u << 7,
		EnvironmentMask = 1u << 8,
	};

	/** @brief Fixed channel assignment for the packed material-properties texture. */
	enum class RmaosChannel : std::uint32_t
	{
		Roughness = 0,
		Metallic = 1,
		AmbientOcclusion = 2,
		SpecularLevel = 3,
	};

	/**
	 * @brief Existing raster shader ABI flags.
	 *
	 * Values must remain synchronized with PBR::Flags in Common/PBRMath.hlsli. Future backends
	 * should consume Descriptor::featureMask and Descriptor::textureMask instead of this mixed mask.
	 */
	enum class RasterFlag : std::uint32_t
	{
		HasEmissive = 1u << 0,
		HasDisplacement = 1u << 1,
		HasFeaturesTexture0 = 1u << 2,
		HasFeaturesTexture1 = 1u << 3,
		Subsurface = 1u << 4,
		TwoLayer = 1u << 5,
		ColoredCoat = 1u << 6,
		InterlayerParallax = 1u << 7,
		CoatNormal = 1u << 8,
		Fuzz = 1u << 9,
		HairMarschner = 1u << 10,
		Glint = 1u << 11,
		ProjectedGlint = 1u << 12,
		// Reserved by the semantic contract. Raster shell emission is intentionally
		// gated until its dedicated geometry path is installed and live-tested.
		FurShell = 1u << 13,
	};

	/** @brief Parameters for stochastic microfacet glints. */
	struct Glint
	{
		float screenSpaceScale = 1.5f;
		float logMicrofacetDensity = 40.f;
		float microfacetRoughness = 0.015f;
		float densityRandomization = 2.f;
	};

	/**
	 * @brief Resolved physical material independent of the rendering API.
	 *
	 * Texture identities are supplied separately by the backend's texture resolver using Texture
	 * semantics. Keeping handles out of this record allows the same description to feed DX11,
	 * Vulkan, ray-query, RTGI, and path-tracing implementations.
	 */
	struct Descriptor
	{
		std::uint32_t schemaVersion = SchemaVersion;
		std::uint32_t shadingModel = static_cast<std::uint32_t>(ShadingModel::MetallicRoughness);
		std::uint32_t featureMask = 0;
		std::uint32_t textureMask = 0;

		std::array<float, 3> baseColorFactor = { 1.f, 1.f, 1.f };
		float opacity = 1.f;

		std::array<float, 3> emissiveFactor = { 0.f, 0.f, 0.f };
		float emissiveStrength = 0.f;

		std::array<float, 3> f0Factor = { 0.04f, 0.04f, 0.04f };
		/** Normalized authored shell length hint for a future shell-fur backend. */
		float furShellLength = 0.f;

		float roughnessScale = 1.f;
		float metallicScale = 1.f;
		float ambientOcclusionStrength = 1.f;
		float specularLevel = 0.04f;

		float normalScale = 1.f;
		float displacementScale = 1.f;
		float subsurfaceOpacity = 0.f;
		float coatStrength = 0.f;

		std::array<float, 3> subsurfaceColor = { 0.f, 0.f, 0.f };
		float coatRoughness = 1.f;

		std::array<float, 3> coatColor = { 1.f, 1.f, 1.f };
		float coatSpecularLevel = 0.04f;

		std::array<float, 3> fuzzColor = { 0.f, 0.f, 0.f };
		float fuzzWeight = 0.f;

		std::array<float, 3> projectedBaseColorScale = { 1.f, 1.f, 1.f };
		float projectedRoughness = 1.f;

		float projectedSpecularLevel = 0.04f;
		/** Confidence that the source material represents fur rather than cloth/hair. */
		float furConfidence = 0.f;
		/** Suggested strand occupancy used when a shell backend is available. */
		float furDensity = 0.f;
		/** Suggested fiber softness/broad forward scatter. */
		float furSoftness = 0.f;

		Glint glint;
		Glint projectedGlint;

		/** @brief Tests whether a physical feature is enabled. */
		[[nodiscard]] constexpr bool HasTrait(MaterialTrait a_feature) const
		{
			return (featureMask & static_cast<std::uint32_t>(a_feature)) != 0;
		}

		/** @brief Tests whether a semantic texture is available. */
		[[nodiscard]] constexpr bool HasTexture(Texture a_texture) const
		{
			return (textureMask & static_cast<std::uint32_t>(a_texture)) != 0;
		}
	};

	/** @brief Opaque texture reference associated with one material semantic. */
	struct TextureBinding
	{
		TextureID textureID = 0;
		std::uint32_t semantic = 0;
		std::uint32_t colorSpace = static_cast<std::uint32_t>(ColorSpace::Linear);
	};

	/**
	 * @brief Fixed-layout material-table row suitable for direct backend upload or repacking.
	 *
	 * Material and texture IDs are session-scoped opaque values. A backend resolves TextureID
	 * independently, so this record remains valid for DX11, Vulkan, and ray-tracing pipelines.
	 */
	struct TableEntry
	{
		MaterialID materialID = 0;
		std::uint32_t revision = 0;
		std::uint32_t bindingCount = 0;
		Descriptor descriptor;
		std::array<TextureBinding, MaxTextureBindings> bindings{};
	};

	static_assert(static_cast<std::uint32_t>(RasterFlag::HasEmissive) == (1u << 0));
	static_assert(static_cast<std::uint32_t>(RasterFlag::ProjectedGlint) == (1u << 12));
	static_assert(static_cast<std::uint32_t>(RasterFlag::FurShell) == (1u << 13));
	static_assert(sizeof(Descriptor) == 208, "Descriptor schema 3 layout changed without a schema-version update.");
	static_assert(sizeof(TextureBinding) == 16, "TextureBinding must retain its backend upload layout.");
	static_assert(std::is_standard_layout_v<Descriptor>);
	static_assert(std::is_trivially_copyable_v<Descriptor>);
	static_assert(std::is_standard_layout_v<TableEntry>);
	static_assert(std::is_trivially_copyable_v<TableEntry>);
}
