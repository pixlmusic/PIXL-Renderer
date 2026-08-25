#ifndef PIXL_MATERIAL_FORGE_TUNING_HLSLI
#define PIXL_MATERIAL_FORGE_TUNING_HLSLI

cbuffer PIXLMaterialForgeLegacyTuningCB : register(b10)
{
	// c0
	uint PIXLMF_Magic;
	uint PIXLMF_Version;
	uint PIXLMF_EnableTuning;
	uint PIXLMF_pad0;

	// c1
	float PIXLMF_ConversionStrength;
	float PIXLMF_ShininessScale;
	float PIXLMF_RoughnessCurve;
	float PIXLMF_RoughnessScale;

	// c2
	float PIXLMF_RoughnessBias;
	float PIXLMF_MinRoughness;
	float PIXLMF_MaxRoughness;
	float PIXLMF_DielectricF0;

	// c3
	float PIXLMF_SpecularColorInfluence;
	float PIXLMF_SpecularStrengthInfluence;
	float PIXLMF_BaseColorEnergy;
	float PIXLMF_MetallicDiffuseSuppression;

	// c4
	float PIXLMF_MetalEnvironmentWeight;
	float PIXLMF_MetalSpecularWeight;
	float PIXLMF_MetalSmoothnessWeight;
	float PIXLMF_MetalColorWeight;

	// c5
	float PIXLMF_MetalNoMaskPenalty;
	float PIXLMF_MetalNeutralPrior;
	float PIXLMF_MetalCrossEvidenceFloor;
	float PIXLMF_F0Scale;

	// c6
	float PIXLMF_MetalTintChromaLow;
	float PIXLMF_MetalTintChromaHigh;
	float PIXLMF_MetalColorCorrelationLow;
	float PIXLMF_MetalColorCorrelationHigh;
};

namespace MaterialForgeTuning
{
	static const uint Magic = 0x504D4654u;
	static const uint Version = 1u;

	bool IsValid()
	{
		return PIXLMF_Magic == Magic && PIXLMF_Version == Version && PIXLMF_EnableTuning != 0u;
	}

	float Strength() { return IsValid() ? saturate(PIXLMF_ConversionStrength) : 0.0f; }
	float ShininessScale() { return IsValid() ? clamp(PIXLMF_ShininessScale, 0.1f, 8.0f) : 1.0f; }
	float RoughnessCurve() { return IsValid() ? clamp(PIXLMF_RoughnessCurve, 0.2f, 4.0f) : 1.0f; }
	float RoughnessScale() { return IsValid() ? clamp(PIXLMF_RoughnessScale, 0.25f, 2.0f) : 1.0f; }
	float RoughnessBias() { return IsValid() ? clamp(PIXLMF_RoughnessBias, -0.75f, 0.75f) : 0.0f; }
	float MinRoughness() { return IsValid() ? clamp(PIXLMF_MinRoughness, 0.01f, 0.95f) : 0.04f; }
	float MaxRoughness() { return IsValid() ? clamp(PIXLMF_MaxRoughness, MinRoughness(), 1.0f) : 1.0f; }
	float DielectricF0() { return IsValid() ? clamp(PIXLMF_DielectricF0, 0.005f, 0.12f) : 0.04f; }
	float SpecularColorInfluence() { return IsValid() ? clamp(PIXLMF_SpecularColorInfluence, 0.0f, 3.0f) : 1.0f; }
	float SpecularStrengthInfluence() { return IsValid() ? clamp(PIXLMF_SpecularStrengthInfluence, 0.0f, 3.0f) : 1.0f; }
	float BaseColorEnergy() { return IsValid() ? clamp(PIXLMF_BaseColorEnergy, 0.25f, 2.0f) : 1.0f; }
	float MetallicDiffuseSuppression() { return IsValid() ? clamp(PIXLMF_MetallicDiffuseSuppression, 0.0f, 1.5f) : 1.0f; }
	float EnvironmentWeight() { return IsValid() ? max(PIXLMF_MetalEnvironmentWeight, 0.0f) : 0.30f; }
	float SpecularWeight() { return IsValid() ? max(PIXLMF_MetalSpecularWeight, 0.0f) : 0.28f; }
	float SmoothnessWeight() { return IsValid() ? max(PIXLMF_MetalSmoothnessWeight, 0.0f) : 0.24f; }
	float ColorWeight() { return IsValid() ? max(PIXLMF_MetalColorWeight, 0.0f) : 0.18f; }
	float NoMaskPenalty() { return IsValid() ? saturate(PIXLMF_MetalNoMaskPenalty) : 0.72f; }
	float NeutralPrior() { return IsValid() ? saturate(PIXLMF_MetalNeutralPrior) : 0.58f; }
	float CrossEvidenceFloor() { return IsValid() ? saturate(PIXLMF_MetalCrossEvidenceFloor) : 0.55f; }
	float F0Scale() { return IsValid() ? clamp(PIXLMF_F0Scale, 0.1f, 3.0f) : 1.0f; }
	float TintChromaLow() { return IsValid() ? clamp(PIXLMF_MetalTintChromaLow, 0.0f, 0.95f) : 0.06f; }
	float TintChromaHigh() { return IsValid() ? max(PIXLMF_MetalTintChromaHigh, TintChromaLow() + 1e-3f) : 0.32f; }
	float ColorCorrelationLow() { return IsValid() ? clamp(PIXLMF_MetalColorCorrelationLow, 0.0f, 0.99f) : 0.76f; }
	float ColorCorrelationHigh() { return IsValid() ? max(PIXLMF_MetalColorCorrelationHigh, ColorCorrelationLow() + 1e-3f) : 0.97f; }
}

#endif
