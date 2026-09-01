#ifndef PIXL_HAIR_RECONSTRUCTION_HLSLI
#define PIXL_HAIR_RECONSTRUCTION_HLSLI

// PIXL Hair Reconstruction deliberately operates on Skyrim's existing cards.
// It does not require a groom, topology rewrite or persistent per-actor texture.
// All procedural terms are object/UV anchored and evaluated for current and
// previous positions so temporal reconstruction receives coherent geometry.
namespace HairReconstruction
{
	static const float2 WindAxis = float2(0.8192319f, 0.5734624f);

	float Hash12(float2 p)
	{
		float3 p3 = frac(float3(p.xyx) * 0.1031f);
		p3 += dot(p3, p3.yzx + 33.33f);
		return frac((p3.x + p3.y) * p3.z);
	}

	float Confidence()
	{
#if defined(HAIR)
		return 1.0f;
#elif defined(AUTO_HAIR)
		return 0.90f;
#elif defined(HAIR_CANDIDATE)
		return 0.50f;
#else
		return 0.0f;
#endif
	}

	float RootTip(float2 uv)
	{
		// Skyrim card textures conventionally run root-to-tip along V.  The smooth
		// dead zone leaves the scalp attachment rigid and prevents one-row UV noise
		// from moving an entire short hairstyle.
		return smoothstep(0.14f, 0.94f, saturate(uv.y));
	}

	float Flexibility(float2 uv, float3 modelPosition)
	{
		float rootTip = RootTip(uv);
		// Card-edge weighting allocates motion to loose locks while the centre mass
		// remains restrained.  The model-position hash is static and only breaks up
		// perfectly synchronized adjacent cards.
		float edge = smoothstep(0.18f, 0.48f, abs(uv.x - 0.5f));
		float variation = lerp(0.86f, 1.0f, Hash12(modelPosition.xy * 0.03125f));
		return saturate(rootTip * lerp(0.58f, 1.0f, edge) * variation);
	}

	float Wetness(float3 worldPosition, float3 worldNormal)
	{
		if (SharedData::hairReconstructionSettings.WetHair == 0u)
			return 0.0f;
		float exposed = saturate(worldNormal.z * 0.55f + 0.45f);
		float rain = saturate(SharedData::rainResponseSettings.Raining) *
			saturate(SharedData::rainResponseSettings.Wetness) * exposed;
		float4 waterData = SharedData::GetWaterData(worldPosition);
		float submerged = waterData.x > 0.0f && worldPosition.z <= waterData.x ? 1.0f : 0.0f;
		return max(rain, submerged);
	}

	float3 SampleMotion(
		float3 absoluteWorldPosition,
		float3 modelPosition,
		float2 uv,
		float time,
		float wetness)
	{
		float flexibility = Flexibility(uv, modelPosition);
		float seed = Hash12(floor(modelPosition.xy * 0.0625f) + modelPosition.zz);
		float spatialScale = max(SharedData::foliageDynamicsSettings.WindSpatialScale, 0.25f);
		float gustSpeed = max(SharedData::foliageDynamicsSettings.GustSpeed, 0.1f);
		float flutterSpeed = max(SharedData::foliageDynamicsSettings.FlutterSpeed, 0.1f);
		float phase =
			dot(absoluteWorldPosition.xy, WindAxis) * (0.0018f * spatialScale) -
			time * (0.72f * gustSpeed) + seed * 6.28318531f;
		float gust = 0.5f + 0.5f * sin(phase);
		gust = smoothstep(0.18f, 0.88f, gust);
		float flutter = sin(
			time * (2.2f + 1.8f * flutterSpeed) +
			dot(absoluteWorldPosition.xy, float2(-WindAxis.y, WindAxis.x)) * 0.008f +
			seed * 19.0f);

		float weatherWind =
			max(SharedData::foliageDynamicsSettings.WindStrength, 0.0f) *
			SharedData::hairReconstructionSettings.WindResponse;
		float wetWeight = 1.0f - wetness *
			saturate(SharedData::hairReconstructionSettings.WetWeight) * 0.55f;
		float amplitude =
			flexibility * weatherWind * wetWeight *
			SharedData::hairReconstructionSettings.MotionStrength;
		float2 lateral =
			WindAxis * ((gust - 0.5f) * 2.0f) +
			float2(-WindAxis.y, WindAxis.x) * (flutter * 0.18f);
		float damping = saturate(SharedData::hairReconstructionSettings.Damping);
		return float3(lateral * amplitude * lerp(3.2f, 1.2f, damping), -abs(gust - 0.5f) * amplitude * 0.28f);
	}

	void ApplyMotion(
		float3 modelPosition,
		float2 uv,
		inout float4 worldPosition,
		inout float4 previousWorldPosition)
	{
		if (SharedData::hairReconstructionSettings.Enabled == 0u ||
			SharedData::hairReconstructionSettings.SecondaryMotion == 0u ||
			SharedData::hairReconstructionSettings.Quality == 0u ||
			Confidence() < SharedData::hairReconstructionSettings.ReconstructionThreshold)
			return;

		float distanceToCamera = length(worldPosition.xyz);
		float distanceFade = 1.0f - smoothstep(
			SharedData::hairReconstructionSettings.SimulationDistance * 0.72f,
			SharedData::hairReconstructionSettings.SimulationDistance,
			distanceToCamera);
		if (distanceFade <= 0.0f)
			return;

		float wetness = Wetness(worldPosition.xyz, float3(0.0f, 0.0f, 1.0f));
		float currentTime = SharedData::Timer;
		float previousTime = currentTime - max(SharedData::hairReconstructionSettings.FrameDelta, 0.0f);
		float3 absoluteCurrent = worldPosition.xyz + SharedData::CameraPosAdjust;
		float3 absolutePrevious = previousWorldPosition.xyz + SharedData::CameraPosAdjust;
		float3 currentMotion = SampleMotion(absoluteCurrent, modelPosition, uv, currentTime, wetness);
		float3 previousMotion = SampleMotion(absolutePrevious, modelPosition, uv, previousTime, wetness);

		// Small inertial lag follows the skinned head/body delta.  It is bounded to
		// avoid teleports, ragdolls and camera rebases turning into a hair explosion.
		float3 actorDelta = worldPosition.xyz - previousWorldPosition.xyz;
		float actorDeltaLength = length(actorDelta);
		if (actorDeltaLength > 1.0e-4f && actorDeltaLength < 24.0f) {
			float flexibility = Flexibility(uv, modelPosition);
			float3 inertia = -actorDelta *
				(0.018f * SharedData::hairReconstructionSettings.MotionStrength * flexibility);
			currentMotion += inertia;
			previousMotion += inertia * saturate(SharedData::hairReconstructionSettings.Damping);
		}

		worldPosition.xyz += currentMotion * distanceFade;
		previousWorldPosition.xyz += previousMotion * distanceFade;
	}

#if defined(PSHADER)
	float3 ResolveDirection(
		float3 authoredDirection,
		float3 worldNormal,
		float3 worldPosition,
		float2 uv,
		bool authoredFlowMap)
	{
		float3 authored = authoredDirection - worldNormal * dot(authoredDirection, worldNormal);
		float authoredLengthSq = dot(authored, authored);
		if (authoredLengthSq < 1.0e-8f) {
			float3 fallbackAxis = abs(worldNormal.z) < 0.92f
				? float3(0.0f, 0.0f, 1.0f)
				: float3(0.0f, 1.0f, 0.0f);
			authored = cross(fallbackAxis, worldNormal);
			authoredLengthSq = max(dot(authored, authored), 1.0e-8f);
		}
		authored *= rsqrt(authoredLengthSq);
		if (SharedData::hairReconstructionSettings.Enabled == 0u ||
			SharedData::hairReconstructionSettings.AnisotropicLighting == 0u || authoredFlowMap)
			return authored;

		float3 dpdx = ddx_coarse(worldPosition);
		float3 dpdy = ddy_coarse(worldPosition);
		float2 duvdx = ddx_coarse(uv);
		float2 duvdy = ddy_coarse(uv);
		float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
		float3 inferred = -dpdx * duvdy.x + dpdy * duvdx.x; // increasing texture V
		float inferredLengthSq = dot(inferred, inferred);
		if (abs(determinant) < 1.0e-7f || inferredLengthSq < 1.0e-8f)
			return authored;
		inferred *= rsqrt(inferredLengthSq);
		inferred -= worldNormal * dot(inferred, worldNormal);
		inferredLengthSq = dot(inferred, inferred);
		if (inferredLengthSq < 1.0e-8f)
			return authored;
		inferred *= rsqrt(inferredLengthSq);
		if (dot(inferred, authored) < 0.0f)
			inferred = -inferred;
		float3 blended = lerp(authored, inferred,
			saturate(SharedData::hairReconstructionSettings.DirectionBlend));
		return blended * rsqrt(max(dot(blended, blended), 1.0e-8f));
	}

	float VirtualFibre(float2 uv)
	{
		float density = saturate(SharedData::hairReconstructionSettings.StrandDensity);
		float frequency = lerp(26.0f, 92.0f, density);
		float phase = uv.x * frequency + sin(uv.y * 17.0f) * 0.42f;
		float footprint = max(fwidth(phase), 1.0e-4f);
		float visibility = saturate(1.0f - footprint * 0.65f);
		return lerp(0.5f, 0.5f + 0.5f * sin(phase * 6.28318531f), visibility);
	}

	void ApplyCardAppearance(inout float4 baseColor, float3 worldPosition, float3 worldNormal, float2 uv)
	{
		if (SharedData::hairReconstructionSettings.Enabled == 0u)
			return;
		float fibre = VirtualFibre(uv);
		float detail = SharedData::hairReconstructionSettings.StrandDetail;
		baseColor.rgb *= 1.0f + (fibre - 0.5f) * (0.11f * detail);

		float wetness = Wetness(worldPosition, worldNormal);
		baseColor.rgb *= 1.0f - wetness * SharedData::hairReconstructionSettings.WetDarkening;

		if (SharedData::hairReconstructionSettings.ProceduralStrands != 0u &&
			SharedData::hairReconstructionSettings.Quality >= 2u) {
			float edgeBand = saturate(1.0f - abs(baseColor.a * 2.0f - 1.0f));
			float breakup = (fibre - 0.5f) *
				SharedData::hairReconstructionSettings.SilhouetteDetail * edgeBand;
			baseColor.a = saturate(baseColor.a + breakup);
		}
	}

	void ApplyMaterial(
		inout float roughness,
		inout float metallic,
		float3 worldPosition,
		float3 worldNormal)
	{
		if (SharedData::hairReconstructionSettings.Enabled == 0u)
			return;
		float wetness = Wetness(worldPosition, worldNormal);
		roughness = lerp(
			roughness,
			max(0.04f, SharedData::hairReconstructionSettings.WetRoughness),
			wetness * 0.72f);
		metallic = 0.0f;
	}

	float3 DebugColor(
		float3 direction,
		float flexibility,
		float2 motionVector,
		float2 uv)
	{
		uint mode = SharedData::hairReconstructionSettings.DebugMode;
		if (mode == 1u) {
#if defined(HAIR_CANDIDATE) && !defined(AUTO_HAIR) && !defined(HAIR)
			return float3(1.0f, 0.05f, 0.03f);
#elif defined(AUTO_HAIR) && !defined(HAIR)
			return float3(1.0f, 0.78f, 0.02f);
#else
			return float3(0.04f, 1.0f, 0.12f);
#endif
		}
		if (mode == 2u)
			return direction * 0.5f + 0.5f;
		if (mode == 3u)
			return lerp(float3(0.05f, 0.20f, 1.0f), float3(1.0f, 0.08f, 0.02f), RootTip(uv));
		if (mode == 4u)
			return lerp(float3(0.06f, 0.12f, 0.55f), float3(1.0f, 0.75f, 0.05f), flexibility);
		if (mode == 5u)
			return float3(saturate(abs(motionVector) * 24.0f), 0.0f);
		if (mode == 6u)
			return float3(Confidence(), 1.0f - Confidence(), saturate(length(motionVector) * 16.0f));
		if (mode == 7u)
			return VirtualFibre(uv).xxx;
		return 0.0f.xxx;
	}
#endif
}

#endif
