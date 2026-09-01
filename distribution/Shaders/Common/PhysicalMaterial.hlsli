#ifndef PHYSICAL_MATERIAL_HLSLI
#define PHYSICAL_MATERIAL_HLSLI

#include "Common/MaterialForgeTuning.hlsli"

namespace PhysicalMaterial
{
	namespace Constants
	{
		static const float DielectricF0 = 0.04f;
		static const float DielectricF0Maximum = 0.08f;
		static const float MinPerceptualRoughness = 0.04f;
		static const float MaxPerceptualRoughness = 1.0f;
	}

	/** @brief Canonical physical surface shared by raster and future ray integrators. */
	struct Surface
	{
		float3 BaseColor;
		float Roughness;
		float3 F0;
		float Metallic;
	};

	/**
	 * Convert Skyrim's Blinn-Phong exponent to perceptual GGX roughness.
	 *
	 * The intermediate GGX alpha is sqrt(2 / (n + 2)); perceptual roughness is
	 * sqrt(alpha). Invalid negative exponents are treated as the broadest lobe.
	 */
	float LegacyShininessToPerceptualRoughness(float shininess)
	{
		const float safeShininess = max(shininess, 0.0f);
		const float baseline = clamp(
			sqrt(sqrt(2.0f / (safeShininess + 2.0f))),
			Constants::MinPerceptualRoughness,
			Constants::MaxPerceptualRoughness);

		if (!MaterialForgeTuning::IsValid())
			return baseline;

		float tunedShininess = safeShininess * MaterialForgeTuning::ShininessScale();
		float tuned = sqrt(sqrt(2.0f / (tunedShininess + 2.0f)));
		tuned = pow(max(tuned, 1e-4f), MaterialForgeTuning::RoughnessCurve());
		tuned = tuned * MaterialForgeTuning::RoughnessScale() + MaterialForgeTuning::RoughnessBias();
		tuned = clamp(tuned, MaterialForgeTuning::MinRoughness(), MaterialForgeTuning::MaxRoughness());

		return lerp(baseline, tuned, MaterialForgeTuning::Strength());
	}

	/**
	 * Resolve a Skyrim legacy material into bounded physical parameters.
	 *
	 * Skyrim's specular color and gloss map form a relative dielectric-strength
	 * control rather than literal Fresnel reflectance. Scaling them by the 4%
	 * dielectric reference prevents a white legacy specular color from becoming
	 * a perfect conductor in physical lighting.
	 */
	Surface FromLegacy(float3 baseColor, float shininess, float3 specularColor, float specularStrength)
	{
		Surface surface;
		const float3 baselineBase = saturate(baseColor);
		const float3 safeSpecular = saturate(max(specularColor, 0.0f));
		const float safeStrength = saturate(specularStrength);
		const float3 baselineF0 =
			saturate(safeSpecular * safeStrength * Constants::DielectricF0);

		float conversion = MaterialForgeTuning::Strength();
		float3 tunedBase = saturate(baselineBase * MaterialForgeTuning::BaseColorEnergy());

		float colorInfluence = MaterialForgeTuning::SpecularColorInfluence();
		float strengthInfluence = MaterialForgeTuning::SpecularStrengthInfluence();
		float3 tunedSpecularColor = 1.0f.xxx;
		float tunedSpecularStrength = 1.0f;
		[flatten] if (colorInfluence > 1e-4f)
		{
			const float epsilon = 1e-5f;
			tunedSpecularColor =
				pow(safeSpecular + epsilon.xxx, colorInfluence.xxx) -
				pow(epsilon.xxx, colorInfluence.xxx);
		}
		[flatten] if (strengthInfluence > 1e-4f)
		{
			const float epsilon = 1e-5f;
			tunedSpecularStrength =
				pow(safeStrength + epsilon, strengthInfluence) -
				pow(epsilon, strengthInfluence);
		}
		float3 tunedF0 = saturate(
			tunedSpecularColor *
			tunedSpecularStrength *
			MaterialForgeTuning::DielectricF0() *
			MaterialForgeTuning::F0Scale());

		surface.BaseColor = lerp(baselineBase, tunedBase, conversion);
		surface.Roughness = LegacyShininessToPerceptualRoughness(shininess);
		surface.F0 = lerp(baselineF0, tunedF0, conversion);
		surface.Metallic = 0.0f;
		return surface;
	}

	/**
	 * Estimate a bounded conductor weight when a legacy texture set has no
	 * metallic channel. This is intentionally conservative: an environment mask
	 * is the strongest single signal, while unmasked surfaces require corroborating
	 * smoothness, specular response, colour correlation, and opacity. No lighting or
	 * emission value participates, so candles cannot classify themselves as metal.
	 */
	float InferLegacyMetallic(float3 baseColor, float3 specularColor, float specularStrength,
		float roughness, float environmentResponse, float opacity, float confidenceThreshold,
		float maximumMetalness)
	{
		const float3 safeBase = saturate(baseColor);
		const float3 safeSpecular = saturate(max(specularColor, 0.0f));
		const float baseMaximum = max(max(safeBase.r, safeBase.g), safeBase.b);
		const float baseMinimum = min(min(safeBase.r, safeBase.g), safeBase.b);
		const float specularMaximum = max(max(safeSpecular.r, safeSpecular.g), safeSpecular.b);
		const float specularMinimum = min(min(safeSpecular.r, safeSpecular.g), safeSpecular.b);

		const float baseChroma = (baseMaximum - baseMinimum) / max(baseMaximum, 0.08f);
		const float specularChroma = (specularMaximum - specularMinimum) / max(specularMaximum, 0.08f);
		const float colourCorrelation = saturate(dot(
			normalize(safeBase + 0.02f),
			normalize(safeSpecular + 0.02f)));

		const float tintedEvidence =
			smoothstep(MaterialForgeTuning::TintChromaLow(), MaterialForgeTuning::TintChromaHigh(), max(baseChroma, specularChroma)) *
			smoothstep(MaterialForgeTuning::ColorCorrelationLow(), MaterialForgeTuning::ColorCorrelationHigh(), colourCorrelation);

		// Neutral iron/steel/silver cannot rely on chroma, so preserve a bounded
		// neutral conductor prior. Unlike the old product classifier, one mediocre
		// input no longer collapses every UI slider to an imperceptible zero.
		const float conductorColourEvidence = lerp(MaterialForgeTuning::NeutralPrior(), 1.0f, tintedEvidence);
		const float smoothEvidence = smoothstep(0.18f, 0.78f, 1.0f - saturate(roughness));
		const float specularEvidence =
			smoothstep(0.04f, 0.55f, saturate(specularStrength)) *
			lerp(0.72f, 1.0f, smoothstep(0.04f, 0.35f, specularMaximum));
		const float maskEvidence = smoothstep(0.04f, 0.70f, saturate(environmentResponse));
		const float opaqueEvidence = smoothstep(0.78f, 0.98f, saturate(opacity));

		// Weighted evidence makes the controls useful on ordinary legacy SPECULAR
		// materials while still giving an environment mask the strongest single vote.
		float4 evidenceWeights = float4(
			MaterialForgeTuning::ColorWeight(),
			MaterialForgeTuning::SmoothnessWeight(),
			MaterialForgeTuning::SpecularWeight(),
			MaterialForgeTuning::EnvironmentWeight());
		float evidenceWeightSum = max(dot(evidenceWeights, 1.0f.xxxx), 1e-4f);
		float evidence = dot(
			float4(conductorColourEvidence, smoothEvidence, specularEvidence, maskEvidence),
			evidenceWeights) / evidenceWeightSum;

		// No-mask surfaces remain eligible, but require stronger smooth/specular
		// support. Both confidence floors are tuneable while retaining the old
		// 0.72 / 0.55 defaults.
		evidence *= lerp(MaterialForgeTuning::NoMaskPenalty(), 1.0f, maskEvidence);
		evidence *= lerp(MaterialForgeTuning::CrossEvidenceFloor(), 1.0f, smoothEvidence * specularEvidence);
		evidence *= opaqueEvidence;

		const float threshold = clamp(confidenceThreshold, 0.0f, 0.95f);
		const float confidence = saturate((evidence - threshold) / max(1.0f - threshold, 1e-3f));
		return confidence * saturate(maximumMetalness);
	}

	/** Apply a conductor interpretation while preserving energy conservation. */
	void ApplyMetallic(inout Surface surface, float3 conductorF0, float metallic, float diffuseAttenuation)
	{
		const float safeMetallic = saturate(metallic);
		surface.F0 = lerp(surface.F0, saturate(conductorF0), safeMetallic);
		surface.BaseColor *= 1.0f - safeMetallic * saturate(diffuseAttenuation);
		surface.Metallic = max(surface.Metallic, safeMetallic);
	}

	/**
	 * @brief Apply a legacy specular-workflow override.
	 *
	 * Skyrim/Material Layers environment-map colors are not metallic masks. Keep
	 * their normal-incidence reflectance inside the dielectric range so colored
	 * albedo is not accidentally converted into a silver conductor. Authored True
	 * PBR materials bypass this adapter and retain their explicit metallic channel.
	 */
	void ApplySpecularOverride(inout Surface surface, float3 F0, float roughness, float weight)
	{
		const float safeWeight = saturate(weight);
		surface.F0 = lerp(surface.F0, clamp(F0, 0.0, Constants::DielectricF0Maximum), safeWeight);
		surface.Roughness = lerp(surface.Roughness, clamp(roughness, Constants::MinPerceptualRoughness, Constants::MaxPerceptualRoughness), safeWeight);
	}
}

#endif
