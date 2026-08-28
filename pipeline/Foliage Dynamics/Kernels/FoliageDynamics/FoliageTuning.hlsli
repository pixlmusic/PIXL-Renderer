#ifndef PIXL_FOLIAGE_TUNING_HLSLI
#define PIXL_FOLIAGE_TUNING_HLSLI

cbuffer PIXLFoliageTuningCB : register(b13)
{
	// c0
	uint PIXLFG_Magic;
	uint PIXLFG_Version;
	uint PIXLFG_EnableGrassAlphaControl;
	uint PIXLFG_GrassFlipNormalX;

	// c1
	uint PIXLFG_GrassFlipNormalY;
	float PIXLFG_GrassNormalStrength;
	float PIXLFG_GrassCardNormalBlend;
	float PIXLFG_GrassAlphaCoverage;

	// c2
	float PIXLFG_GrassCutoutBias;
	float PIXLFG_GrassAlphaPower;
	float PIXLFG_GrassEdgeDither;
	float PIXLFG_GrassSaturation;

	// c3
	float PIXLFG_GrassContrast;
	float PIXLFG_GrassWetSpecularBoost;
	float PIXLFG_GrassTransmissionBoost;
	float PIXLFG_GrassLocalLightBoost;

	// c4 -- dedicated b13 extension; FeatureData b6 remains unchanged.
	float PIXLFG_GrassDetailDistanceScale;
	float PIXLFG_GrassDetailTransitionSoftness;
	float PIXLFG_GrassSpecularNormalization;
	float PIXLFG_GrassComplexSpecularMapInfluence;

	// c5
	uint PIXLFG_GrassMirrorSpecularY;
	uint PIXLFG_GrassTuningPadding0;
	uint PIXLFG_GrassTuningPadding1;
	uint PIXLFG_GrassTuningPadding2;
};

namespace FoliageTuning
{
	static const uint Magic = 0x50464754u;
	static const uint Version = 4u;

	bool IsValid() { return PIXLFG_Magic == Magic && PIXLFG_Version == Version; }
	bool AlphaControlEnabled() { return IsValid() && PIXLFG_EnableGrassAlphaControl != 0u; }
	bool FlipNormalX() { return IsValid() && PIXLFG_GrassFlipNormalX != 0u; }
	bool FlipNormalY() { return IsValid() && PIXLFG_GrassFlipNormalY != 0u; }
	float NormalStrength() { return IsValid() ? clamp(PIXLFG_GrassNormalStrength, 0.0f, 2.0f) : 1.0f; }
	float CardNormalBlend() { return IsValid() ? saturate(PIXLFG_GrassCardNormalBlend) : 0.0f; }
	float AlphaCoverage() { return IsValid() ? clamp(PIXLFG_GrassAlphaCoverage, 0.1f, 3.0f) : 1.0f; }
	float CutoutBias() { return IsValid() ? clamp(PIXLFG_GrassCutoutBias, -0.5f, 0.5f) : 0.0f; }
	float AlphaPower() { return IsValid() ? clamp(PIXLFG_GrassAlphaPower, 0.2f, 5.0f) : 1.0f; }
	float EdgeDither() { return IsValid() ? saturate(PIXLFG_GrassEdgeDither) : 0.0f; }
	float Saturation() { return IsValid() ? clamp(PIXLFG_GrassSaturation, 0.0f, 2.0f) : 1.0f; }
	float Contrast() { return IsValid() ? clamp(PIXLFG_GrassContrast, 0.25f, 2.0f) : 1.0f; }
	float WetSpecularBoost() { return IsValid() ? clamp(PIXLFG_GrassWetSpecularBoost, 0.0f, 3.0f) : 1.0f; }
	float TransmissionBoost() { return IsValid() ? clamp(PIXLFG_GrassTransmissionBoost, 0.0f, 3.0f) : 1.0f; }
	float LocalLightBoost() { return IsValid() ? clamp(PIXLFG_GrassLocalLightBoost, 0.0f, 3.0f) : 1.0f; }
	float DetailDistanceScale() { return IsValid() ? clamp(PIXLFG_GrassDetailDistanceScale, 0.5f, 2.0f) : 1.0f; }
	float DetailTransitionSoftness() { return IsValid() ? clamp(PIXLFG_GrassDetailTransitionSoftness, 0.5f, 2.0f) : 1.0f; }
	float SpecularNormalization() { return IsValid() ? clamp(PIXLFG_GrassSpecularNormalization, 0.0f, 4.0f) : 1.0f; }
	float ComplexSpecularMapInfluence() { return IsValid() ? saturate(PIXLFG_GrassComplexSpecularMapInfluence) : 0.15f; }
	bool MirrorSpecularY() { return IsValid() && PIXLFG_GrassMirrorSpecularY != 0u; }
}

#endif
