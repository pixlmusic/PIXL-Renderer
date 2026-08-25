#ifndef LIGHTING_COMMON_HLSLI
#define LIGHTING_COMMON_HLSLI

// The canonical legacy-to-physical adapters are also required by hair
// permutations compiled while the global MATERIAL_FORGE feature is enabled.
#include "Common/PhysicalMaterial.hlsli"

struct DirectContext
{
	float3 worldNormal;
	float3 vertexNormal;
	float3 viewDir;
	float3 lightDir;
	float3 halfVector;
	float3 lightColor;
	float detailedShadow;
	float softShadow;
#if defined(MATERIAL_FORGE)
	float3 coatWorldNormal;
	float3 coatViewDir;
	float3 coatLightDir;
	float3 coatHalfVector;
	float3 coatLightColor;
#endif
#if defined(HAIR) && defined(STRAND_SHADING)
	float hairShadow;
#endif
};

struct IndirectContext
{
	float3 worldNormal;
	float3 vertexNormal;
	float3 viewDir;
};

struct DirectLightingOutput
{
	float3 diffuse;
	float3 specular;
	float3 transmission;
#if defined(MATERIAL_FORGE)
	float3 coatDiffuse;
#endif
};

struct IndirectLobeWeights
{
	float3 diffuse;
	float3 specular;
};

#if defined(MATERIAL_FORGE)
#	if defined(GLINT)
#		include "Common/Glints/Glints2023.hlsli"
#	else
namespace Glints
{
	typedef float GlintCachedVars;
}
#	endif
#endif

struct MaterialProperties
{
	// Canonical physical surface shared by legacy, skin, hair, and Material Forge adapters.
	// BaseColor is diffuse albedo, Roughness is perceptual roughness, and F0 is
	// reflectance at normal incidence. Model-specific fields remain below.
	float3 BaseColor;
	float Roughness;
	float3 F0;
	// Canonical conductor weight. Authored for Material Forge and reconstructed
	// conservatively for eligible legacy materials.
	float Metallic;
	// Strand Shading is compiled alongside the global MATERIAL_FORGE feature define for
	// hair permutations. Keep its legacy lobe exponent in the shared adapter
	// section so that combination remains a valid shader permutation.
	float Shininess;
#if !defined(MATERIAL_FORGE)
	float Glossiness;
	float3 SpecularColor;
#	if (defined(RIM_LIGHTING) || defined(SOFT_LIGHTING))
	float3 rimSoftLightColor;
#	endif
#	if defined(BACK_LIGHTING)
	float3 backLightColor;
#	endif
#	if defined(PIXL_SKIN) && defined(SKIN)
	float RoughnessSecondary;
	float SecondarySpecIntensity;
	float Curvature;
	float Thickness;
	float3 SubsurfaceColor;
	float AO;
	float FuzzRoughness;
	float3 FuzzColor;
	float FuzzWeight;
#	endif
#else
	float AO;
	float3 SubsurfaceColor;
	float Thickness;
	float3 CoatColor;
	float CoatStrength;
	float CoatRoughness;
	float3 CoatF0;
	float3 FuzzColor;
	float FuzzWeight;
	float GlintScreenSpaceScale;
	float GlintLogMicrofacetDensity;
	float GlintMicrofacetRoughness;
	float GlintDensityRandomization;
	Glints::GlintCachedVars GlintCache;
	float Noise;
#endif
};

#endif
