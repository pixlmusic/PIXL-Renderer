#if defined(HORIZON_BLEND) && !defined(INTERIOR)
namespace HorizonBlend
{
	// Depth (z/w) that water folded back from beyond the far clip plane lands at: eight
	// depth quanta inside the far plane, exactly representable in both D24 and D32F
	// buffers - behind everything the scene rendered, in front of the depth clear.
	static const float FoldedDepth = 1.0 - 8.0 / 16777216.0;

	// Scene depth at or beyond this counts as "nothing rendered behind the water": the
	// clear value, folded far water (FoldedDepth), or an external plugin's depth-clamped
	// far-water backdrop a few quanta inside that. The margin is a few world units at the
	// far plane, where no real surface can render.
	static const float EmptyDepthThreshold = 1.0 - 64.0 / 16777216.0;

	// HorizonFix supplies a far-water skirt folded just inside the far plane.
	// Fade only that folded, near-horizontal water into the scene atmosphere;
	// ordinary close and mid-range water retains its authored colour.
	// Restrict the skirt fade to the genuinely grazing, far-distance edge.  A
	// broad range makes ordinary first-person water become a visible horizontal
	// band instead of merely hiding the far-plane seam.
	static const float FadeStartElevation = 0.05;
	static const float FadeEndElevation = 0.008;
	static const float FadeStartDistance = 0.65;
	static const float FadeEndDistance = 0.995;

	float HorizonSmootherStep(float edge0, float edge1, float value)
	{
		float t = saturate((value - edge0) / max(edge1 - edge0, 1.0e-6f));
		return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
	}
}
#endif

#if defined(UNDERWATERMASK)

struct VS_INPUT
{
	float4 Position: POSITION0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
};

#	ifdef VSHADER

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float z = min(1, 1e-4 * max(0, input.Position.z - 70000)) * 0.5 + input.Position.z;
	vsout.Position = float4(input.Position.xy, z, 1);

	return vsout;
}
#	endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#	ifdef PSHADER
PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	psout.Color = 1;

	return psout;
}
#	endif

#else

#	include "Common/FrameBuffer.hlsli"
#	include "Common/Game.hlsli"
#	include "Common/MotionBlur.hlsli"
#	include "Common/Permutation.hlsli"
#	include "Common/Random.hlsli"
#	include "Common/Shading.hlsli"
#	include "Common/BRDF.hlsli"
#	include "Common/Color.hlsli"

#	define WATER

#	include "Common/SharedData.hlsli"

#	ifndef USE_PIXL_WATER_OPTICS
#		define USE_PIXL_WATER_OPTICS 1
#	endif

// Public builds never replace real water radiance with RainResponse debug colors.
// An explicit development permutation can still opt into diagnostics.
#	ifndef PIXL_WATER_RELEASE_DIAGNOSTICS
#		define PIXL_WATER_RELEASE_DIAGNOSTICS 0
#	endif

#include "OceanDynamics/OceanWaveBands.hlsli"

struct VS_INPUT
{
#	if defined(SPECULAR) || defined(UNDERWATER) || defined(STENCIL) || defined(SIMPLE)
	float4 Position: POSITION0;
#		if defined(NORMAL_TEXCOORD)
	float2 TexCoord0: TEXCOORD0;
#		endif
#		if defined(VC)
	float4 Color: COLOR0;
#		endif
#	endif

#	if defined(LOD)
	float4 Position: POSITION0;
#		if defined(VC)
	float4 Color: COLOR0;
#		endif
#	endif
};

struct VS_OUTPUT
{
#	if defined(SPECULAR) || defined(UNDERWATER)
	float4 HPosition: SV_POSITION0;
#		if !defined(WATERBODY)
	float4 FogParam: COLOR0;
#		endif
	float4 WPosition: TEXCOORD0;
	float4 TexCoord1: TEXCOORD1;
	float4 TexCoord2: TEXCOORD2;
#		if defined(WADING) || (defined(FLOWMAP) && (defined(REFRACTIONS) || defined(BLEND_NORMALS))) || (defined(VERTEX_ALPHA_DEPTH) && defined(VC)) || ((defined(SPECULAR) && NUM_SPECULAR_LIGHTS == 0) && defined(FLOWMAP) /*!defined(NORMAL_TEXCOORD) && !defined(BLEND_NORMALS) && !defined(VC)*/)
	float4 TexCoord3: TEXCOORD3;
#		endif
#		if defined(FLOWMAP)
	nointerpolation float2 TexCoord4: TEXCOORD4;
#		endif
#		if NUM_SPECULAR_LIGHTS == 0
	float4 MPosition: TEXCOORD5;
#		endif
#	endif

#	if defined(SIMPLE)
	float4 HPosition: SV_POSITION0;
	float4 FogParam: COLOR0;
	float4 WPosition: TEXCOORD0;
	float4 TexCoord1: TEXCOORD1;
	float4 TexCoord2: TEXCOORD2;
	float4 MPosition: TEXCOORD5;
#	endif

#	if defined(LOD)
	float4 HPosition: SV_POSITION0;
	float4 FogParam: COLOR0;
	float4 WPosition: TEXCOORD0;
	float4 TexCoord1: TEXCOORD1;
#	endif

#	if defined(STENCIL)
	float4 HPosition: SV_POSITION0;
	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
#	endif

	float4 NormalsScale: TEXCOORD8;
#	if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(LOD)
	float4 OceanBasePosition: TEXCOORD9;
#		if defined(STENCIL)
	float4 OceanPreviousBasePosition: TEXCOORD10;
#		endif
#	endif
};

#	ifdef VSHADER

#		if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(LOD)
Texture2DArray<float4> OceanDisplacementTex : register(t120);
Texture2DArray<float4> OceanPreviousDisplacementTex : register(t121);
Texture2D<float> OceanTerrainHeightmap : register(t122);
Texture2D<float> OceanCellTerrainHeightmap : register(t124);
Texture2D<float4> OceanWaveCageTexture : register(t123);
SamplerState OceanDisplacementSampler : register(s15);

cbuffer OceanBathymetry : register(b7)
{
	float4 OceanHeightmapWorldBounds : packoffset(c0); // origin XY and opposite-corner XY
	float4 OceanHeightmapEncoding : packoffset(c1);   // source height encoding and metadata range
	float4 OceanShoreSettings : packoffset(c2);       // heightmap valid, shore depth, foam strength, active
	float4 OceanGeometrySettings : packoffset(c3);    // quality tier, crest transmission, reserved
	float4 OceanPhaseSettings : packoffset(c4);       // swell direction, finite-depth phase warp, breaker profile
	float4 OceanTimingSettings : packoffset(c5);      // current/previous simulation times
	float4 OceanSurfBoardPosition : packoffset(c6);   // absolute XYZ, active flag
	float4 OceanSurfBoardVelocity : packoffset(c7);   // XY velocity, speed
	float4 OceanLocalTerrainPatch : packoffset(c8);   // cell origin XY, spacing, valid
};

cbuffer OceanWaveCageConstants : register(b13)
{
	float4 OceanWaveCageSettings; // texture resolution, valid, blend, maximum radius
};

bool SampleOceanTerrainHeight(float2 worldXY, out float terrainHeight, out float2 sampleStep)
{
	terrainHeight = -32768.0f;
	sampleStep = 1.0f.xx;
	if (!all(isfinite(worldXY)))
		return false;
	float localEdgeDistance = 0.0f;
	bool found = false;
	if (OceanLocalTerrainPatch.w > 0.5f && OceanLocalTerrainPatch.z > 1.0f) {
		uint localWidth,localHeight;
		OceanCellTerrainHeightmap.GetDimensions(localWidth,localHeight);
		float2 lastSample=max(float2(localWidth,localHeight)-1.0f,1.0f.xx);
		float2 grid = (worldXY - OceanLocalTerrainPatch.xy) / OceanLocalTerrainPatch.z;
		if (all(grid >= 0.0f) && all(grid <= lastSample)) {
			uint2 cell = min((uint2)floor(grid), (uint2)(lastSample-1.0f));
			float2 f = saturate(grid - cell);
			float h00 = OceanCellTerrainHeightmap.Load(int3(cell, 0));
			float h10 = OceanCellTerrainHeightmap.Load(int3(cell + uint2(1, 0), 0));
			float h01 = OceanCellTerrainHeightmap.Load(int3(cell + uint2(0, 1), 0));
			float h11 = OceanCellTerrainHeightmap.Load(int3(cell + uint2(1, 1), 0));
			if (all(isfinite(float4(h00, h10, h01, h11))) && min(min(h00, h10), min(h01, h11)) > -30000.0f) {
				terrainHeight = lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
				localEdgeDistance = min(min(grid.x, grid.y), min(lastSample.x - grid.x, lastSample.y - grid.y));
				found = true;
				sampleStep = OceanLocalTerrainPatch.z.xx;
				// Use exact cell terrain in the interior. Only evaluate the coarse map
				// in the four-cell feather where the two sources meet.
			}
		}
	}
	float2 extent = OceanHeightmapWorldBounds.zw - OceanHeightmapWorldBounds.xy;
	if ((!found || localEdgeDistance < 4.0f) && OceanShoreSettings.x > 0.5f && all(abs(extent) > 1.0f)) {
		float2 uv = (worldXY - OceanHeightmapWorldBounds.xy) / extent;
		if (all(uv >= 0.0f) && all(uv <= 1.0f)) {
			uint width, height;
			OceanTerrainHeightmap.GetDimensions(width, height);
			float2 texel = rcp(max(float2(width, height), 1.0f.xx));
			uv = clamp(uv, 0.5f * texel, 1.0f - 0.5f * texel);
			float encoded = OceanTerrainHeightmap.SampleLevel(OceanDisplacementSampler, uv, 0.0f);
			if (!isfinite(encoded) || !all(isfinite(OceanHeightmapEncoding)))
				return found;
			float globalHeight = clamp(lerp(OceanHeightmapEncoding.x, OceanHeightmapEncoding.y, saturate(encoded)),
				OceanHeightmapEncoding.z, OceanHeightmapEncoding.w);
			float2 globalStep = abs(extent / max(float2(width, height), 1.0f.xx));
			if (found) {
				float localWeight = smoothstep(0.0f, 4.0f, localEdgeDistance);
				terrainHeight = lerp(globalHeight, terrainHeight, localWeight);
				sampleStep = OceanLocalTerrainPatch.z.xx;
			} else {
				terrainHeight = globalHeight;
				sampleStep = globalStep;
			}
			found = true;
		}
	}
	return found;
}

float3 SampleOceanDisplacement(float2 absoluteWorldXY, float viewDistance)
{
	static const float extents[4] = { 256.0f, 1024.0f, 4096.0f, 32768.0f };
	float3 result = 0.0f;
	if (!all(isfinite(absoluteWorldXY)))
		return result;
	[unroll]
	for (uint cascade = 0; cascade < 4; ++cascade) {
		// Keep displacement on Skyrim's authored mesh to the long swell. Shorter
		// FFT bands are shaded as WaterOptics normals per pixel; applying them to
		// coarse native vertices created the visibly faceted repeating surface.
		if (cascade != 3)
			continue;
		float2 uv = absoluteWorldXY / extents[cascade] + 0.5f;
		float4 wave = OceanDisplacementTex.SampleLevel(OceanDisplacementSampler, float3(uv, cascade), 0.0f);
		if (!all(isfinite(wave)))
			continue;
		result = float3(clamp(wave.x, -35.0f, 35.0f), clamp(wave.y, -35.0f, 35.0f),
			clamp(wave.z, -100.0f, 100.0f));
	}
	return result;
}

float3 SamplePreviousOceanDisplacement(float2 absoluteWorldXY, float viewDistance)
{
	static const float extents[4] = { 256.0f, 1024.0f, 4096.0f, 32768.0f };
	float3 result = 0.0f;
	if (!all(isfinite(absoluteWorldXY)))
		return result;
	[unroll]
	for (uint cascade = 0; cascade < 4; ++cascade) {
		if (cascade != 3)
			continue;
		float2 uv = absoluteWorldXY / extents[cascade] + 0.5f;
		float4 wave = OceanPreviousDisplacementTex.SampleLevel(OceanDisplacementSampler, float3(uv, cascade), 0.0f);
		if (!all(isfinite(wave)))
			continue;
		result = float3(clamp(wave.x, -35.0f, 35.0f), clamp(wave.y, -35.0f, 35.0f),
			clamp(wave.z, -100.0f, 100.0f));
	}
	return result;
}

float OceanDepthSpeedRatio(float depth)
{
	float period = clamp(OceanGeometrySettings.z, 2.0f, 24.0f);
	float omega = 6.28318530718f / period;
	float k0 = omega * omega / (9.81f * 70.0f);
	return sqrt(clamp(tanh(k0 * max(depth, 0.0f)), 0.001225f, 1.0f));
}

float OceanShoreBlend(float depth)
{
	return 1.0f - smoothstep(200.0f, 1600.0f, max(depth, 0.0f));
}

float OceanPhaseDelay(float depth)
{
	float period = clamp(OceanGeometrySettings.z, 2.0f, 24.0f);
	float wavelength = (9.81f * 70.0f) * period * period / 6.28318530718f;
	return clamp((1.0f - OceanDepthSpeedRatio(depth)) * wavelength * 0.055f *
		clamp(OceanPhaseSettings.z, 0.0f, 1.5f), 0.0f, 420.0f);
}

float3 ApplyOceanShoreTransform(float3 wave, float2 absoluteWorldXY, float waterHeight, float simulationTime)
{
	float terrain;
	float2 sampleStep;
	if (!SampleOceanTerrainHeight(absoluteWorldXY, terrain, sampleStep))
		return float3(0.0f, 0.0f, clamp(wave.z, -100.0f, 100.0f));
	float depth = waterHeight - terrain;
	float shallow = saturate(OceanShoreBlend(depth) * clamp(OceanPhaseSettings.z, 0.0f, 1.5f));
	float ratio = OceanDepthSpeedRatio(depth);
	float shoaling = lerp(1.0f, clamp(rsqrt(max(ratio, 0.05f)), 1.0f, 1.25f), shallow);
	float3 shoaledWave = wave * shoaling;
	float wet = smoothstep(-24.0f, 48.0f, depth);
	float3 verticalFallback = float3(0.0f, 0.0f,
		OceanThreeBandHeight(shoaledWave.z, shallow, depth, wet));
	if (OceanWaveCageSettings.y < 0.5f || OceanWaveCageSettings.z <= 0.0f)
		return verticalFallback;

	float2 extent = OceanHeightmapWorldBounds.zw - OceanHeightmapWorldBounds.xy;
	if (any(abs(extent) <= 1.0f))
		return verticalFallback;
	float2 cageUV = (absoluteWorldXY - OceanHeightmapWorldBounds.xy) / extent;
	if (any(cageUV < 0.0f) || any(cageUV > 1.0f))
		return verticalFallback;
	float2 cageTexel = rcp(max(OceanWaveCageSettings.xx, 1.0f.xx));
	cageUV = clamp(cageUV, 0.5f * cageTexel, 1.0f - 0.5f * cageTexel);
	float4 cage = OceanWaveCageTexture.SampleLevel(OceanDisplacementSampler, cageUV, 0.0f);
	if (cage.w < 0.5f)
		return verticalFallback;

	// Match the CPU-fitted cage frame: its terrain tangent is measured over two
	// map texels, with a 64-unit minimum to avoid noisy xLODGen normals.
	float2 frameStep = max(2.0f * sampleStep, 64.0f.xx);
	float terrainLeft, terrainRight, terrainDown, terrainUp;
	float2 ignoredStep;
	bool hasGradient = SampleOceanTerrainHeight(absoluteWorldXY - float2(frameStep.x, 0.0f), terrainLeft, ignoredStep) &&
		SampleOceanTerrainHeight(absoluteWorldXY + float2(frameStep.x, 0.0f), terrainRight, ignoredStep) &&
		SampleOceanTerrainHeight(absoluteWorldXY - float2(0.0f, frameStep.y), terrainDown, ignoredStep) &&
		SampleOceanTerrainHeight(absoluteWorldXY + float2(0.0f, frameStep.y), terrainUp, ignoredStep);
	float2 terrainSlope = hasGradient
		? float2(terrainRight - terrainLeft, terrainUp - terrainDown) / (2.0f * frameStep)
		: 0.0f.xx;
	float3 terrainNormal = normalize(float3(-terrainSlope, 1.0f));
	float shoreSign = depth >= 0.0f ? 1.0f : -1.0f;
	float3 shoreCandidate = float3(terrainSlope * shoreSign,
		dot(terrainSlope, terrainSlope) * shoreSign);
	float shoreLengthSq = dot(shoreCandidate, shoreCandidate);
	float3 shoreAxis = shoreLengthSq > 1.0e-8f
		? shoreCandidate * rsqrt(shoreLengthSq) : float3(1.0f, 0.0f, 0.0f);
	float3 alongAxis = normalize(cross(terrainNormal, shoreAxis));
	shoreAxis = normalize(cross(alongAxis, terrainNormal));
	float referenceRadius = max(cage.x, max(cage.y, cage.z));
	float waveLength = length(shoaledWave);
	if (referenceRadius <= 1.0e-3f || waveLength <= 1.0e-6f)
		return verticalFallback;
	float3 radialClip = shoaledWave * (tanh(waveLength / referenceRadius) / waveLength);
	float3 cagedWave = shoreAxis * (cage.x * dot(shoreAxis, radialClip)) +
		alongAxis * (cage.y * dot(alongAxis, radialClip)) +
		terrainNormal * (cage.z * dot(terrainNormal, radialClip));
	return OceanBeachCoverage(lerp(verticalFallback, cagedWave * wet,
		saturate(OceanWaveCageSettings.z)), depth);
}

float GetOceanShoreDisplacementWeight(float2 absoluteWorldXY, float waterHeight)
{
	float terrainHeight;
	float2 sampleStep;
	if (!SampleOceanTerrainHeight(absoluteWorldXY, terrainHeight, sampleStep))
		return 1.0f; // no terrain data: preserve offshore water
	float depth = waterHeight - terrainHeight;
	// Attenuate orbital displacement as the terrain reaches the water plane.
	// This is a conservative shoreline fade, not a substitute for shallow-water
	// dispersion or a phase-related breaker simulation.
    // Preserve wave motion through the wet surf zone and taper only across the
    // actual water/land crossing. The native water mesh already defines shore
    // coverage; a broad fade here removed the incoming wave too far offshore.
    return smoothstep(-24.0f, 48.0f, depth);
}

float2 GetOceanShoreSamplePosition(float2 absoluteWorldXY)
{
	// Keep the FFT phase world-anchored. Terrain-derived phase warping made
	// crests jump where approximate heightmap gradients changed between tiles.
	return absoluteWorldXY;
}
#		endif

cbuffer PerTechnique : register(b0)
{
	float4 QPosAdjust : packoffset(c0);
};

cbuffer PerMaterial : register(b1)
{
	float4 VSFogParam : packoffset(c0);
	float4 VSFogNearColor : packoffset(c1);
	float4 VSFogFarColor : packoffset(c2);
	float4 NormalsScroll0 : packoffset(c3);
	float4 NormalsScroll1 : packoffset(c4);
	float4 NormalsScale : packoffset(c5);
};

cbuffer PerGeometry : register(b2)
{
	row_major float4x4 World : packoffset(c0);
	row_major float4x4 PreviousWorld : packoffset(c4);
	row_major float4x4 WorldViewProj : packoffset(c8);
	float3 ObjectUV : packoffset(c12);
	float4 CellTexCoordOffset : packoffset(c13);
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout = (VS_OUTPUT)0;

	vsout.NormalsScale = NormalsScale;

	float4 inputPosition = float4(input.Position.xyz, 1.0);
	float4 worldPos = mul(World, inputPosition);
	float4 worldViewPos = mul(WorldViewProj, inputPosition);

#	if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(LOD)
	if (OceanShoreSettings.w > 0.5f)
	{
		float2 absoluteXY = worldPos.xy + FrameBuffer::CameraPosAdjust.xy;
		float waterHeight = worldPos.z + FrameBuffer::CameraPosAdjust.z;
		vsout.OceanBasePosition = worldPos;
		float2 waveXY = GetOceanShoreSamplePosition(absoluteXY);
		float3 oceanOffset = ApplyOceanShoreTransform(
			SampleOceanDisplacement(waveXY, length(worldViewPos.xyz)), absoluteXY, waterHeight,
			OceanTimingSettings.x);
		worldPos.xyz += oceanOffset;
		// Reproject from camera-relative world coordinates so the water vertex
		// path uses the same transform as OceanDomain and remains consistent with
		// camera-origin shifts.
		worldViewPos = mul(FrameBuffer::CameraViewProj, float4(worldPos.xyz, 1.0f));
	}
#	endif

	float heightMult = min((1.0 / 10000.0) * max(worldViewPos.z - 70000, 0), 1);

	vsout.HPosition.xy = worldViewPos.xy;
	vsout.HPosition.z = heightMult * 0.5 + worldViewPos.z;
	vsout.HPosition.w = worldViewPos.w;

#	if defined(HORIZON_BLEND) && !defined(INTERIOR)
	vsout.HPosition.z = min(vsout.HPosition.z, vsout.HPosition.w * HorizonBlend::FoldedDepth);
#	endif

#		if defined(STENCIL)
	vsout.WorldPosition = worldPos;
	vsout.PreviousWorldPosition = mul(PreviousWorld, inputPosition);
#if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(UNDERWATER) && !defined(LOD)
	vsout.OceanPreviousBasePosition = vsout.PreviousWorldPosition;
#endif
#if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(UNDERWATER) && !defined(LOD)
	if (OceanShoreSettings.w > 0.5f) {
		float2 previousXY = vsout.PreviousWorldPosition.xy + FrameBuffer::CameraPreviousPosAdjust.xy;
		float previousHeight = vsout.PreviousWorldPosition.z + FrameBuffer::CameraPreviousPosAdjust.z;
		float2 previousWaveXY = GetOceanShoreSamplePosition(previousXY);
		vsout.PreviousWorldPosition.xyz += ApplyOceanShoreTransform(
			SamplePreviousOceanDisplacement(previousWaveXY, 0.0f), previousXY, previousHeight,
			OceanTimingSettings.y);
	}
#endif
#		else

#			if !defined(WATERBODY)
	float fogDistanceFactor = min(VSFogFarColor.w, pow(saturate(length(worldViewPos.xyz) * VSFogParam.y - VSFogParam.x), NormalsScale.w));
	vsout.FogParam.xyz = lerp(VSFogNearColor.xyz, VSFogFarColor.xyz, fogDistanceFactor);
	vsout.FogParam.w = fogDistanceFactor;
#			endif

	vsout.WPosition.xyz = worldPos.xyz;
	vsout.WPosition.w = length(worldPos.xyz);

#			if defined(LOD)
	float4 posAdjust =
		ObjectUV.x ? 0.0 : (QPosAdjust.xyxy + worldPos.xyxy) / NormalsScale.xxyy;

	vsout.TexCoord1.xyzw = NormalsScroll0 + posAdjust;
#			else
#				if !defined(SPECULAR) || (NUM_SPECULAR_LIGHTS == 0)
	vsout.MPosition.xyzw = inputPosition.xyzw;
#				endif

	float2 posAdjust = worldPos.xy + QPosAdjust.xy;

	float2 scrollAdjust1 = posAdjust / NormalsScale.xx;
	float2 scrollAdjust2 = posAdjust / NormalsScale.yy;
	float2 scrollAdjust3 = posAdjust / NormalsScale.zz;

#				if defined(WATERBODY) && defined(NORMAL_TEXCOORD)
	float2 cellShift = float2(floor(ObjectUV.z * 0.5), floor((ObjectUV.z - 1.0) * 0.5));
	float2 scaledUV = input.TexCoord0.xy * ObjectUV.z - cellShift;
#				endif

#				if !(defined(FLOWMAP) && (defined(REFRACTIONS) || defined(BLEND_NORMALS) || defined(DEPTH) || NUM_SPECULAR_LIGHTS == 0))
#					if defined(NORMAL_TEXCOORD)
	float3 normalsScale = 0.001 * NormalsScale.xyz;
	if (ObjectUV.x) {
		scrollAdjust1 = input.TexCoord0.xy / normalsScale.xx;
		scrollAdjust2 = input.TexCoord0.xy / normalsScale.yy;
		scrollAdjust3 = input.TexCoord0.xy / normalsScale.zz;
	}
#					else
	if (ObjectUV.x) {
		scrollAdjust1 = 0.0;
		scrollAdjust2 = 0.0;
		scrollAdjust3 = 0.0;
	}
#					endif
#				endif

	vsout.TexCoord1 = 0.0;
	vsout.TexCoord2 = 0.0;
#				if defined(FLOWMAP)
#					if !(((defined(SPECULAR) || NUM_SPECULAR_LIGHTS == 0) || (defined(UNDERWATER) && defined(REFRACTIONS))) && !defined(NORMAL_TEXCOORD))
#						if defined(BLEND_NORMALS)
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = NormalsScroll0.zw + scrollAdjust2;
	vsout.TexCoord2.xy = NormalsScroll1.xy + scrollAdjust3;
#						else
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = 0.0;
	vsout.TexCoord2.xy = 0.0;
#						endif
#					endif
#					if !defined(NORMAL_TEXCOORD)
	vsout.TexCoord3 = 0.0;
#					elif defined(WADING)
#						if defined(WATERBODY)
	float2 wadingUV = (input.TexCoord0.xy - 0.5f) * 0.5f;
	vsout.TexCoord2.zw = (CellTexCoordOffset.xy + wadingUV) / ObjectUV.xy;
	vsout.TexCoord3.xy = CellTexCoordOffset.zw + wadingUV;
#						else
	vsout.TexCoord2.zw = ((-0.5 + input.TexCoord0.xy) * 0.1 + CellTexCoordOffset.xy) +
	                     float2(CellTexCoordOffset.z, -CellTexCoordOffset.w + ObjectUV.x) / ObjectUV.xx;
	vsout.TexCoord3.xy = -0.25 + (input.TexCoord0.xy * 0.5 + ObjectUV.yz);
#						endif
	vsout.TexCoord3.zw = input.TexCoord0.xy;
#					elif (defined(REFRACTIONS) || NUM_SPECULAR_LIGHTS == 0 || defined(BLEND_NORMALS))
#						if defined(WATERBODY)
	float2 dims = float2(ObjectUV.x, ObjectUV.y);
	vsout.TexCoord2.zw = (CellTexCoordOffset.xy + scaledUV) / dims;
	vsout.TexCoord3.xy = CellTexCoordOffset.zw + scaledUV;
	vsout.TexCoord3.zw = scaledUV;
#						else
	vsout.TexCoord2.zw = (CellTexCoordOffset.xy + input.TexCoord0.xy) / ObjectUV.xx;
	vsout.TexCoord3.xy = CellTexCoordOffset.zw + input.TexCoord0.xy;
	vsout.TexCoord3.zw = input.TexCoord0.xy;
#						endif
#					endif
	vsout.TexCoord4 = ObjectUV.xy;
#				else
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = NormalsScroll0.zw + scrollAdjust2;
	vsout.TexCoord2.xy = NormalsScroll1.xy + scrollAdjust3;
	vsout.TexCoord2.z = worldViewPos.w;
	vsout.TexCoord2.w = 0;
#					if (defined(WADING) || (defined(VERTEX_ALPHA_DEPTH) && defined(VC)))
	vsout.TexCoord3 = 0.0;
#						if (defined(NORMAL_TEXCOORD) && ((!defined(BLEND_NORMALS) && !defined(VERTEX_ALPHA_DEPTH)) || defined(WADING)))
	vsout.TexCoord3.xy = input.TexCoord0;
#						endif
#						if defined(VERTEX_ALPHA_DEPTH) && defined(VC)
	vsout.TexCoord3.z = input.Color.w;
#						endif
#					endif
#				endif
#			endif
#		endif

	return vsout;
}

#	endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
#	if defined(UNDERWATER) || defined(SIMPLE) || defined(LOD) || defined(SPECULAR)
	float4 Lighting: SV_Target0;
#	endif

#	if defined(STENCIL)
	float4 WaterMask: SV_Target0;
	float2 MotionVector: SV_Target1;
#	endif
};

#	ifdef PSHADER

#		if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(UNDERWATER) && !defined(LOD) && !defined(STENCIL)
Texture2DArray<float4> OceanDisplacementPS : register(t120);
Texture2DArray<float4> OceanPreviousDisplacementPS : register(t121);
Texture2D<float> OceanTerrainHeightmapPS : register(t122);
Texture2D<float> OceanCellTerrainHeightmapPS : register(t124);
SamplerState OceanTerrainSamplerPS : register(s15);
// b3 is owned by Radiant Grid in water pixel permutations; reserve b7 for the
// ocean heightmap constants (currently unused by the water shader pipeline).
cbuffer OceanBathymetryPS : register(b7)
{
	float4 OceanHeightmapBoundsPS : packoffset(c0);
	float4 OceanHeightEncodingPS : packoffset(c1);
	float4 OceanShoreSettingsPS : packoffset(c2);
	float4 OceanGeometrySettingsPS : packoffset(c3);
	float4 OceanPhaseSettingsPS : packoffset(c4);
	float4 OceanTimingSettingsPS : packoffset(c5);
	float4 OceanSurfBoardPositionPS : packoffset(c6);
	float4 OceanSurfBoardVelocityPS : packoffset(c7);
	float4 OceanLocalTerrainPatchPS : packoffset(c8);
};

bool SampleOceanTerrainHeightPS(float2 worldXY, out float terrainHeight, out float2 sampleStep)
{
	terrainHeight = -32768.0f;
	sampleStep = 1.0f.xx;
	if (!all(isfinite(worldXY)))
		return false;
	float localEdgeDistance = 0.0f;
	bool found = false;
	if (OceanLocalTerrainPatchPS.w > 0.5f && OceanLocalTerrainPatchPS.z > 1.0f) {
		float2 grid = (worldXY - OceanLocalTerrainPatchPS.xy) / OceanLocalTerrainPatchPS.z;
		if (all(grid >= 0.0f) && all(grid <= 32.0f)) {
			uint2 cell = min((uint2)floor(grid), 31u.xx);
			float2 f = saturate(grid - cell);
			float h00 = OceanCellTerrainHeightmapPS.Load(int3(cell, 0));
			float h10 = OceanCellTerrainHeightmapPS.Load(int3(cell + uint2(1, 0), 0));
			float h01 = OceanCellTerrainHeightmapPS.Load(int3(cell + uint2(0, 1), 0));
			float h11 = OceanCellTerrainHeightmapPS.Load(int3(cell + uint2(1, 1), 0));
			if (all(isfinite(float4(h00, h10, h01, h11))) && min(min(h00, h10), min(h01, h11)) > -30000.0f) {
				terrainHeight = lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
				localEdgeDistance = min(min(grid.x, grid.y), min(32.0f - grid.x, 32.0f - grid.y));
				found = true;
				sampleStep = OceanLocalTerrainPatchPS.z.xx;
			}
		}
	}
	float2 extent = OceanHeightmapBoundsPS.zw - OceanHeightmapBoundsPS.xy;
	if ((!found || localEdgeDistance < 4.0f) && OceanShoreSettingsPS.x > 0.5f && all(abs(extent) > 1.0f)) {
		float2 uv = (worldXY - OceanHeightmapBoundsPS.xy) / extent;
		if (all(uv >= 0.0f) && all(uv <= 1.0f)) {
			uint width, height;
			OceanTerrainHeightmapPS.GetDimensions(width, height);
			float2 texel = rcp(max(float2(width, height), 1.0f.xx));
			uv = clamp(uv, 0.5f * texel, 1.0f - 0.5f * texel);
			float encoded = OceanTerrainHeightmapPS.SampleLevel(OceanTerrainSamplerPS, uv, 0.0f);
			if (!isfinite(encoded) || !all(isfinite(OceanHeightEncodingPS)))
				return found;
			float globalHeight = clamp(lerp(OceanHeightEncodingPS.x, OceanHeightEncodingPS.y, saturate(encoded)),
				OceanHeightEncodingPS.z, OceanHeightEncodingPS.w);
			float2 globalStep = abs(extent / max(float2(width, height), 1.0f.xx));
			if (found) {
				float localWeight = smoothstep(0.0f, 4.0f, localEdgeDistance);
				terrainHeight = lerp(globalHeight, terrainHeight, localWeight);
				sampleStep = OceanLocalTerrainPatchPS.z.xx;
			} else {
				terrainHeight = globalHeight;
				sampleStep = globalStep;
			}
			found = true;
		}
	}
	return found;
}

float2 GetOceanSurfaceSlope(PS_INPUT input)
{
	// The long band is already represented by the displaced/tessellated surface.
	// Sample only the three shorter bands here to avoid double-counting its slope.
	float2 absoluteXY = input.OceanBasePosition.xy + FrameBuffer::CameraPosAdjust.xy;
	static const float extents[4] = { 256.0f, 1024.0f, 4096.0f, 32768.0f };
	float weights[3] = { 0.24f, 0.54f, 0.72f };
	float viewDistance = length(input.WPosition.xyz);
	weights[0] *= 1.0f - smoothstep(1800.0f, 4200.0f, viewDistance);
	weights[1] *= 1.0f - smoothstep(14000.0f, 32000.0f, viewDistance);
	weights[2] *= 1.0f - smoothstep(65000.0f, 120000.0f, viewDistance);
	float height = 0.0f;
	[unroll]
	for (uint cascade = 0; cascade < 3; ++cascade) {
		if (weights[cascade] <= 1.0e-4f)
			continue;
		float2 uv = absoluteXY / extents[cascade] + 0.5f;
		float sampledHeight = OceanDisplacementPS.SampleLevel(OceanTerrainSamplerPS,
			float3(uv, cascade), 0.0f).z;
		if (isfinite(sampledHeight))
			height += sampledHeight * weights[cascade];
	}
	float2 screenDx = ddx_coarse(absoluteXY);
	float2 screenDy = ddy_coarse(absoluteXY);
	float determinant = screenDx.x * screenDy.y - screenDx.y * screenDy.x;
	if (!isfinite(determinant) || abs(determinant) < 1.0e-5f)
		return 0.0f.xx;
	float heightDx = ddx_coarse(height);
	float heightDy = ddy_coarse(height);
	if (!isfinite(heightDx) || !isfinite(heightDy))
		return 0.0f.xx;
	float2 slope = float2(
		(heightDx * screenDy.y - heightDy * screenDx.y) / determinant,
		(-heightDx * screenDy.x + heightDy * screenDx.x) / determinant);
	return clamp(slope * max(OceanGeometrySettingsPS.x, 0.25f), -0.85f.xx, 0.85f.xx);
}

float2 SampleOceanFoamFilamentsGrad(float2 worldXY, float2 orbitalOffset,
	float2 worldDx, float2 worldDy);

float2 GetOceanShoreFoam(PS_INPUT input)
{
	if ((OceanShoreSettingsPS.x < 0.5f && OceanLocalTerrainPatchPS.w < 0.5f) || OceanShoreSettingsPS.w < 0.5f)
		return 0.0f.xx;
	float3 absolutePosition = input.WPosition.xyz + FrameBuffer::CameraPosAdjust.xyz;
	// Capture derivatives before the bathymetry/foam early-outs below.
	float2 foamWorldDx = ddx_coarse(input.WPosition.xy);
	float2 foamWorldDy = ddy_coarse(input.WPosition.xy);
	float2 phasePosition = input.OceanBasePosition.xy + FrameBuffer::CameraPosAdjust.xy;
	float terrainHeight;
	float2 terrainStep;
	if (!SampleOceanTerrainHeightPS(absolutePosition.xy, terrainHeight, terrainStep))
		return 0.0f.xx;
	// Use displaced surface height for contact, while the wave transform keeps
	// stable still-water depth. This lets a real crest wash onto the beach
	// without phase-warping its own input coordinates.
	float waterDepth = absolutePosition.z - terrainHeight;
	float stillWaterDepth = OceanGeometrySettingsPS.w - terrainHeight;
	// All shoreline and crest masks below are zero beyond this depth. Avoid
	// terrain-gradient and foam-detail work over the open ocean.
	if (stillWaterDepth > 1200.0f)
		return 0.0f.xx;
	// Heightmap resolution and terrain interpolation are coarse, so use a
	// softly broken band rather than a one-pixel binary shoreline edge.
	float inWaterMask = smoothstep(-12.0f, 28.0f, waterDepth);
	float shallowMask = 1.0f - smoothstep(24.0f, max(OceanShoreSettingsPS.y, 32.0f), stillWaterDepth);
	float contact = inWaterMask * shallowMask;
	contact *= 1.0f - smoothstep(180.0f, 620.0f, max(stillWaterDepth, 0.0f));
	float coastSlope = 0.0f;
	float ratio = 1.0f;
	float shoreBlend = 0.0f;
	float2 shoreGradient = 0.0f.xx;
	float left, right, down, up;
	float2 ignoredStep;
	bool hasGradient = SampleOceanTerrainHeightPS(absolutePosition.xy - float2(terrainStep.x, 0), left, ignoredStep) &&
		SampleOceanTerrainHeightPS(absolutePosition.xy + float2(terrainStep.x, 0), right, ignoredStep) &&
		SampleOceanTerrainHeightPS(absolutePosition.xy - float2(0, terrainStep.y), down, ignoredStep) &&
		SampleOceanTerrainHeightPS(absolutePosition.xy + float2(0, terrainStep.y), up, ignoredStep);
	if (hasGradient) {
		shoreGradient = float2(right - left, up - down) / max(2.0f * terrainStep, 1.0f.xx);
		coastSlope = length(shoreGradient);
		float period = clamp(OceanGeometrySettingsPS.z, 2.0f, 24.0f);
		float omega = 6.28318530718f / period;
		ratio = sqrt(clamp(tanh(omega * omega * max(stillWaterDepth, 0.0f) /
			(9.81f * 70.0f)), 0.001225f, 1.0f));
		shoreBlend = 1.0f - smoothstep(200.0f, 1600.0f, max(stillWaterDepth, 0.0f));
	}
	float2 swellUV = frac(phasePosition / 32768.0f + 0.5f);
	float4 currentWave = OceanDisplacementPS.SampleLevel(OceanTerrainSamplerPS,
		float3(swellUV, 3.0f), 0);
	float4 previousWave = OceanPreviousDisplacementPS.SampleLevel(OceanTerrainSamplerPS,
		float3(swellUV, 3.0f), 0);
	float swellHeight = currentWave.z;
	float shoreAmount = saturate(shoreBlend * clamp(OceanPhaseSettingsPS.z, 0.0f, 1.5f));
	float shoalingScale = lerp(1.0f, clamp(rsqrt(max(ratio, 0.05f)), 1.0f, 1.25f), shoreAmount);
	float shoaledHeight = swellHeight * shoalingScale;
	float wet = smoothstep(-24.0f, 48.0f, stillWaterDepth);
	swellHeight = OceanThreeBandHeight(shoaledHeight, shoreAmount, stillWaterDepth, wet);
	// Apply the same depth-limited transfer to neighboring samples so the crest
	// detector follows the visible profile and cannot paint broad flat patches.
	// The broad geometry cascade has a 256-unit texel. A half-texel difference
	// measures its actual crest slope without amplifying bilinear sub-texel noise.
	float2 sampleOffset = float2(128.0f, 0.0f);
	float heightLeft = OceanThreeBandHeight(OceanDisplacementPS.SampleLevel(OceanTerrainSamplerPS,
		float3(frac((phasePosition - sampleOffset) / 32768.0f + 0.5f), 3.0f), 0).z * shoalingScale,
		shoreAmount, stillWaterDepth, wet);
	float heightRight = OceanThreeBandHeight(OceanDisplacementPS.SampleLevel(OceanTerrainSamplerPS,
		float3(frac((phasePosition + sampleOffset) / 32768.0f + 0.5f), 3.0f), 0).z * shoalingScale,
		shoreAmount, stillWaterDepth, wet);
	sampleOffset = float2(0.0f, 128.0f);
	float heightDown = OceanThreeBandHeight(OceanDisplacementPS.SampleLevel(OceanTerrainSamplerPS,
		float3(frac((phasePosition - sampleOffset) / 32768.0f + 0.5f), 3.0f), 0).z * shoalingScale,
		shoreAmount, stillWaterDepth, wet);
	float heightUp = OceanThreeBandHeight(OceanDisplacementPS.SampleLevel(OceanTerrainSamplerPS,
		float3(frac((phasePosition + sampleOffset) / 32768.0f + 0.5f), 3.0f), 0).z * shoalingScale,
		shoreAmount, stillWaterDepth, wet);
	float2 surfaceGradient = float2(heightRight - heightLeft, heightUp - heightDown) / 256.0f;
	float swellSteepness = length(surfaceGradient);
	// The broad 32 km cascade is deliberately low frequency and the shoreline
	// cage limits its displacement. Use a lower slope threshold for its foam cue;
	// the old cutoff only activated in steep offshore chop, not on these visible
	// rolling beach crests.
	// The 32 km cascade describes long swell, not local whitewater. Requiring a
	// distinctly steep face prevents its broad, shallow slope from becoming a
	// continuous white sheet across the offshore surface.
	float steepFace = smoothstep(0.008f, 0.034f, swellSteepness);
	float deepFactor = smoothstep(900.0f, 2400.0f, stillWaterDepth);
	// The contact film stays geometrically anchored, but the wash still tracks
	// the incoming FFT phase rather than freezing when displacement reaches zero.
	float washHeight = lerp(clamp(currentWave.z * shoalingScale, -16.0f, 16.0f),
		swellHeight, smoothstep(96.0f, 384.0f, max(stillWaterDepth, 0.0f)));
	float movingCrest = lerp(smoothstep(0.5f, 6.0f, washHeight),
		smoothstep(4.0f, 24.0f, washHeight), deepFactor);
	float sampleDelta = clamp(OceanTimingSettingsPS.x - OceanTimingSettingsPS.y,
		1.0f / 120.0f, 0.1f);
	float2 crestVelocity = (currentWave.xy - previousWave.xy) / sampleDelta;
	float velocityLength = length(crestVelocity);
	float frontFacing = velocityLength > 1.0e-3f && swellSteepness > 1.0e-5f
		? saturate(-dot(surfaceGradient, crestVelocity) /
			(max(swellSteepness, 1.0e-4f) * velocityLength))
		: 0.0f;
	// Thurey et al.'s opposing-flow / steep-front test supplies the strongest
	// breaker cue. Retain a restrained steep-crest floor because the game uses a
	// coarse long-swell cascade whose frame-to-frame velocity is intentionally
	// filtered for temporal stability.
	float opposingFront = smoothstep(0.20f, 0.72f, frontFacing);
	// If the asynchronous previous sample is momentarily unavailable, retain a
	// small geometry-only breaker cue, but only for a genuinely steep front.
	float breakerCue = steepFace * max(opposingFront, 0.18f);
	float coastActivation = smoothstep(0.00035f, 0.018f, coastSlope);
	float breakerStrength = clamp(OceanPhaseSettingsPS.w, 0.0f, 2.0f);
	float shallowBreaker = 1.0f - smoothstep(300.0f, 900.0f, max(stillWaterDepth, 0.0f));
	float crestFoam = movingCrest * breakerCue * shallowBreaker * breakerStrength * 0.82f;
	// The shore wash follows the same FFT crest and its displaced contact edge.
	// This keeps the incoming crest and retreat in one phase instead of layering a
	// second, independently timed shoreline oscillator over the water.
	// Let the animated leading lace gather in slightly deeper water before the
	// contact film reaches the terrain edge. This makes the run-up read as a
	// rolling shore wave while the separate wetEdge remains anchored at the beach.
	float frontDepth = clamp(64.0f + washHeight * 0.78f, 34.0f, 138.0f);
	float frontWidth = clamp(lerp(34.0f, 58.0f, shoreBlend), 30.0f, 58.0f);
	float frontBand = 1.0f - smoothstep(frontWidth * 0.55f, frontWidth * 1.45f,
		abs(stillWaterDepth - frontDepth));
	// If neither a crest nor shoreline wash can contribute, avoid reading
	// the high-frequency whitewater stencil at all on this pixel.
	float possibleWetEdge = 1.0f - smoothstep(18.0f, 156.0f, max(waterDepth, 0.0f));
	float possibleFoam = max(crestFoam, max(frontBand, possibleWetEdge) *
		coastActivation * inWaterMask * breakerStrength);
	if (possibleFoam <= 1.0e-4f)
		return saturate(float2(contact * 0.025f, 0.0f));
	float2 foamDetail = SampleOceanFoamFilamentsGrad(absolutePosition.xy,
		currentWave.xy, foamWorldDx, foamWorldDy);
	float foamBreakup = lerp(0.22f, 1.0f, foamDetail.x);
	float movingSwash = frontBand * coastActivation * inWaterMask * foamBreakup *
		breakerStrength * 0.62f;
	// Keep the wash on the wet side of the native terrain contact, with sparse
	// residual lace on the falling phase instead of a uniform opaque white band.
	float wetEdge = 1.0f - smoothstep(18.0f, 156.0f, max(waterDepth, 0.0f));
	float phaseWash = saturate(movingCrest *
		(0.42f + 0.58f * smoothstep(0.18f, 0.72f, frontFacing)));
	movingSwash = max(movingSwash, wetEdge * coastActivation * foamBreakup *
		breakerStrength * lerp(0.10f, 0.54f, phaseWash));
	float retreat = smoothstep(0.5f, 12.0f, (previousWave.z - currentWave.z) / sampleDelta);
	float retreatLace = wetEdge * coastActivation * inWaterMask *
		smoothstep(1.0f, 72.0f, max(stillWaterDepth, 0.0f)) *
		(1.0f - smoothstep(110.0f, 190.0f, max(stillWaterDepth, 0.0f))) * retreat *
		foamDetail.y * breakerStrength * 0.30f;
	movingSwash = max(movingSwash, retreatLace);
	// The long swell cascade is broad by design. Keep its whitewater in broken
	// crest filaments so low-frequency mask coverage cannot read as tiled cloudy
	// slabs on otherwise dark open water.
	crestFoam *= smoothstep(0.38f, 0.80f, foamDetail.x) *
		lerp(0.42f, 1.0f, foamDetail.y);
	return saturate(float2(movingSwash + contact * 0.025f, crestFoam));
}

float GetSurfBoardWakeFoam(float2 absoluteWorldXY)
{
	if (OceanShoreSettingsPS.w < 0.5f || OceanSurfBoardPositionPS.w < 0.5f ||
		OceanSurfBoardVelocityPS.z < 35.0f)
		return 0.0f;
	float speed = OceanSurfBoardVelocityPS.z;
	float2 direction = OceanSurfBoardVelocityPS.xy / max(speed, 1.0e-4f);
	float2 lateralDirection = float2(-direction.y, direction.x);
	float2 relative = absoluteWorldXY - OceanSurfBoardPositionPS.xy;
	float behind = -dot(relative, direction);
	float lateral = abs(dot(relative, lateralDirection));
	float wakeLength = smoothstep(8.0f, 38.0f, behind) *
		(1.0f - smoothstep(330.0f, 520.0f, behind));
	float wakeWidth = 12.0f + saturate(behind / 360.0f) * 34.0f;
	float rails = 1.0f - smoothstep(wakeWidth * 0.52f, wakeWidth, lateral);
	float streak = 0.58f + 0.42f * saturate(0.5f + 0.5f *
		sin(behind * 0.071f + sin(lateral * 0.12f) * 2.1f));
	float speedWeight = smoothstep(45.0f, 190.0f, speed);
	return saturate(wakeLength * rails * streak * speedWeight * 0.72f);
}
#		endif

SamplerState ReflectionSampler : register(s0);
SamplerState RefractionSampler : register(s1);
SamplerState DisplacementSampler : register(s2);
SamplerState CubeMapSampler : register(s3);
SamplerState Normals01Sampler : register(s4);
SamplerState Normals02Sampler : register(s5);
SamplerState Normals03Sampler : register(s6);
SamplerState DepthSampler : register(s7);
SamplerState FlowMapSampler : register(s8);
SamplerState FlowMapNormalsSampler : register(s9);
SamplerState SSRReflectionSampler : register(s10);
SamplerState RawSSRReflectionSampler : register(s11);

Texture2D<float4> ReflectionTex : register(t0);
Texture2D<float4> RefractionTex : register(t1);
Texture2D<float4> DisplacementTex : register(t2);
TextureCube<float4> CubeMapTex : register(t3);
Texture2D<float4> Normals01Tex : register(t4);
Texture2D<float4> Normals02Tex : register(t5);
Texture2D<float4> Normals03Tex : register(t6);
Texture2D<float4> DepthTex : register(t7);
Texture2D<float4> FlowMapTex : register(t8);
Texture2D<float4> FlowMapNormalsTex : register(t9);
Texture2D<float4> SSRReflectionTex : register(t10);
Texture2D<float4> RawSSRReflectionTex : register(t11);
// WaterOptics reserves t65 for receiver caustics, t66 for the linear,
// high-resolution foam-coverage mask, and t67 for the game's authored rapid-water
// artwork. The rapid layer is projected in world space and advected by flow so
// it follows tessellated/displaced water rather than a legacy flat overlay.
Texture2D<float> WaterFoamStencil : register(t66);
Texture2D<float4> AuthoredRapidWater : register(t67);
// Volumetric clouds are composited after opaque water, so environment cubemaps
// cannot contain them. Use the resolved camera sky for reflected-sky fallback.
Texture2D<float4> VolumetricCloudReflection : register(t68);
Texture2D<float> VolumetricCloudDepth : register(t69);

cbuffer PerTechnique : register(b0)
{
	float4 VPOSOffset : packoffset(c0);  // inverse main render target width and height in xy, 0 in zw
	float4 PosAdjust : packoffset(c1);   // inverse framebuffer range in w
	float4 CameraDataWater : packoffset(c2);
	float4 SunDir : packoffset(c3);
	float4 SunColor : packoffset(c4);
}

cbuffer PerMaterial : register(b1)
{
	float4 ShallowColor : packoffset(c0);
	float4 DeepColor : packoffset(c1);
	float4 ReflectionColor : packoffset(c2);
	float4 FresnelRI : packoffset(c3);    // Fresnel amount in x, specular power in z
	float4 BlendRadius : packoffset(c4);  // flowmap scale in y, specular radius in z
	float4 VarAmounts : packoffset(c5);   // Sun specular power in x, reflection amount in y, alpha in z, refraction magnitude in w
	float4 NormalsAmplitude : packoffset(c6);
	float4 WaterParams : packoffset(c7);   // noise falloff in x, reflection magnitude in y, sun sparkle power in z, framebuffer range in w
	float4 FogNearColor : packoffset(c8);  // above water fog amount in w
	float4 FogFarColor : packoffset(c9);
	float4 FogParam : packoffset(c10);      // above water fog distance far in z, above water fog range in w
	float4 DepthControl : packoffset(c11);  // depth reflections factor in x, depth refractions factor in y, depth normals factor in z, depth specular lighting factor in w
	float4 SSRParams : packoffset(c12);     // fWaterSSRIntensity in x, fWaterSSRBlurAmount in y, inverse main render target width and height in zw
	float4 SSRParams2 : packoffset(c13);    // fWaterSSRNormalPerturbationScale in x
}

cbuffer PerGeometry : register(b2)
{
	float4x4 TextureProj : packoffset(c0);
	float4 ReflectPlane[1] : packoffset(c4);
	float4 ProjData : packoffset(c5);
	float4 LightPos[8] : packoffset(c6);
	float4 LightColor[8] : packoffset(c14);
}

#		define SampColorSampler Normals01Sampler
#		define LinearSampler Normals01Sampler

#		if defined(SKY_BOUNCE)
#			include "SkyBounce/SkyBounce.hlsli"
#		endif

#		if defined(ATMOSPHERE_PIPELINE)
#			include "Atmosphere/Atmosphere.hlsli"
#		endif

#		include "Common/ShadowSampling.hlsli"

#		if defined(SIMPLE) || defined(UNDERWATER) || defined(LOD) || defined(SPECULAR)
float GetWaterFogFade()
{
#			if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled) {
		return Atmosphere::GetVanillaFogFade(PosAdjust.w);
	}
#			endif
	return PosAdjust.w;
}

#			if defined(FLOWMAP)

/**
 * Structure containing complete flowmap information
 */
struct FlowmapData
{
	float4 color;       // Raw flowmap color (R=flow_x, G=flow_y, B=flow_strength, A=flow_mask)
	float2 flowVector;  // Flow vector (coordinate space depends on source function)
};

/**
 * Gets raw flowmap data before UV-space coordinate transformation
 *
 * @param input Pixel shader input containing texture coordinates
 * @param uvShift UV offset for sampling the flowmap texture
 * @return FlowmapData with raw components:
 *         - color: Raw flowmap texture sample (RG=rotation, B=strength, A=mask)
 *         - flowVector: Base flow vector before any coordinate transformation
 *                      Ready for direct application of rotation matrix for world positioning
 *
 * @details This function provides flowmap data in its original coordinate space, suitable
 *          for world-space positioning effects (like ripple movement). The flowVector has
 *          NOT been transformed for UV-space normal sampling - that transformation is only
 *          applied in GetFlowmapDataUV() which uses transpose for UV coordinate perturbation.
 *
 *          Use this function when you need to apply the rotation matrix directly for
 *          world-space effects without needing to reverse any existing transformations.
 *
 * @see GetFlowmapDataUV() for UV-space normal sampling (applies transpose transformation)
 */
FlowmapData GetFlowmapDataTextureSpace(PS_INPUT input, float2 uvShift)
{
	FlowmapData data;
	data.color = FlowMapTex.SampleLevel(FlowMapSampler, input.TexCoord2.zw + uvShift, 0);
	data.flowVector = (64 * input.TexCoord3.xy) * sqrt(max(1.01 - data.color.z, 0.0f));
	// NOTE: flowVector is NOT transformed yet - this is the raw vector before rotation matrix
	return data;
}
/**
 * Samples flowmap texture and calculates UV-space flow data for texture sampling
 *
 * @param input Pixel shader input containing texture coordinates and world position data
 * @param uvShift UV offset for sampling the flowmap texture (used for animation/variation)
 * @return FlowmapData Complete flowmap information with UV-space flow vector
 *
 * @details This function:
 *          - Samples the flowmap texture at the specified UV coordinates
 *          - Decodes flow direction from RG channels (remapped from [0,1] to [-1,1])
 *          - Calculates flow strength using the blue channel with sqrt falloff
 *          - Applies transpose rotation matrix to transform flow direction to UV space
 *          - Scales flow vector by world position and strength factors
 *
 * @note Flowmap format:
 *       - Red channel: Flow direction X component (0.5 = no flow, 0/1 = negative/positive flow)
 *       - Green channel: Flow direction Y component (0.5 = no flow, 0/1 = negative/positive flow)
 *       - Blue channel: Flow strength (0 = no flow, 1 = maximum flow)
 *       - Alpha channel: Flow mask/intensity multiplier
 */
FlowmapData GetFlowmapDataUV(PS_INPUT input, float2 uvShift)
{
	FlowmapData data = GetFlowmapDataTextureSpace(input, uvShift);
	float2 flowSinCos = data.color.xy * 2 - 1;
	float2x2 flowRotationMatrix = float2x2(flowSinCos.x, flowSinCos.y, -flowSinCos.y, flowSinCos.x);
	data.flowVector = mul(transpose(flowRotationMatrix), data.flowVector);
	return data;
}
// ----------------------------------------------------------------
// Flowmap Parallax Functions
// ----------------------------------------------------------------

/**
 * Samples height from flowmap texture using the same 4-sample blend as flowmap normals
 * This ensures height transitions match the normal transitions exactly
 *
 * @param input PS_INPUT for flowmap coordinate access
 * @param normalMul The blend weights from the flowmap system (same as used for normals)
 * @param uvShift The UV shift value (1 / (128 * flowmapDimensions))
 * @param mipLevel Mip level for texture sampling
 */
float GetFlowmapHeightBlended(PS_INPUT input, float2 normalMul, float2 uvShift, float mipLevel)
{
	// Sample height using the EXACT same UV computation as GetFlowmapNormal
	// This ensures the height blending matches the normal blending perfectly

	// Sample 0: uvShift, multiplier=9.92, offset=0
	FlowmapData flowData0 = GetFlowmapDataUV(input, uvShift);
	float2 uv0 = 0 + (flowData0.flowVector - float2(9.92 * ((0.001 * ReflectionColor.w) * flowData0.color.w), 0));
	float height0 = FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, uv0, mipLevel).w;

	// Sample 1: float2(0, uvShift.y), multiplier=10.64, offset=0.27
	FlowmapData flowData1 = GetFlowmapDataUV(input, float2(0, uvShift.y));
	float2 uv1 = 0.27 + (flowData1.flowVector - float2(10.64 * ((0.001 * ReflectionColor.w) * flowData1.color.w), 0));
	float height1 = FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, uv1, mipLevel).w;

	// Sample 2: 0.0.xx, multiplier=8, offset=0
	FlowmapData flowData2 = GetFlowmapDataUV(input, 0.0.xx);
	float2 uv2 = 0 + (flowData2.flowVector - float2(8 * ((0.001 * ReflectionColor.w) * flowData2.color.w), 0));
	float height2 = FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, uv2, mipLevel).w;

	// Sample 3: float2(uvShift.x, 0), multiplier=8.48, offset=0.62
	FlowmapData flowData3 = GetFlowmapDataUV(input, float2(uvShift.x, 0));
	float2 uv3 = 0.62 + (flowData3.flowVector - float2(8.48 * ((0.001 * ReflectionColor.w) * flowData3.color.w), 0));
	float height3 = FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, uv3, mipLevel).w;

	// Use the EXACT same blending formula as flowmap normals
	float blendedHeight =
		normalMul.y * (normalMul.x * height2 + (1 - normalMul.x) * height3) +
		(1 - normalMul.y) * (normalMul.x * height1 + (1 - normalMul.x) * height0);

	return blendedHeight;
}

// Keep this for compatibility - just forwards to the proper function
float GetFlowmapHeightBarycentric(PS_INPUT input, float2 flowmapDimensions, float2 baseUV, float mipLevel)
{
	// This is now unused - we use GetFlowmapHeightBlended directly
	return FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, baseUV, mipLevel).w;
}

/**
 * Computes mip level for flowmap texture sampling
 */
float GetFlowmapMipLevel(float2 flowmapUV)
{
	float2 textureDims;
	FlowMapNormalsTex.GetDimensions(textureDims.x, textureDims.y);

	textureDims /= 8.0;

	float2 texCoordsPerSize = flowmapUV * textureDims;
	float2 dxSize = ddx(texCoordsPerSize);
	float2 dySize = ddy(texCoordsPerSize);
	float2 dTexCoords = dxSize * dxSize + dySize * dySize;
	float minTexCoordDelta = max(dTexCoords.x, dTexCoords.y);
	return max(0.5 * log2(max(minTexCoordDelta, 1e-8f)), 0);
}

/**
 * Samples height from flowmap texture (riverflow.dds alpha channel)
 * Uses the same UV calculation as GetFlowmapNormal for consistency
 */

/**
 * Generates flowmap-based normal (no parallax - flowmap normals are not parallax-shifted)
 * Uses mip clamping to preserve detail at distance and prevent over-blurring
 */
float3 GetFlowmapNormal(PS_INPUT input, float2 uvShift, float multiplier, float offset)
{
	FlowmapData flowData = GetFlowmapDataUV(input, uvShift);
	float2 uv = offset + (flowData.flowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowData.color.w), 0));

	float2 dx = ddx(uv);
	float2 dy = ddy(uv);
	float mipLevel = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1e-8f));
	mipLevel = clamp(mipLevel + SharedData::MipBias, 0, 5);

	float mipScale = exp2(-mipLevel);
	float2 scaledFlowVector = flowData.flowVector * mipScale;
	float2 scaledUv = offset + (scaledFlowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowData.color.w), 0));

	return float3(FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, scaledUv, mipLevel).xy, flowData.color.z);
}

/**
 * Gets flowmap data with world-space flow vector for positioning effects
 *
 * @param input Pixel shader input containing texture coordinates
 * @param uvShift UV offset for flowmap sampling (used for animation phases)
 * @return FlowmapData Complete flowmap information with world-space flow vector
 *
 * @details This function:
 *          - Samples raw flowmap data (before UV-space transformations)
 *          - Decodes flow direction from flowmap RG channels
 *          - Applies component-wise directional transformation
 *          - Returns complete flowmap data with world-space flow vector
 *
 * @note Use this for effects that need to move with water current (ripples, debris, foam, etc.)
 *       For UV-space normal sampling, use GetFlowmapDataUV() instead
 */
FlowmapData GetFlowmapDataWorldSpace(PS_INPUT input, float2 uvShift)
{
	FlowmapData data = GetFlowmapDataTextureSpace(input, uvShift);
	float2 flowDirection = -(data.color.xy * 2 - 1);    // Decode direction with 180° correction
	data.flowVector = data.flowVector * flowDirection;  // Transform to world space
	return data;
}

/**
 * Converts existing texture-space flowmap data to world-space (avoids duplicate sampling)
 *
 * @param textureSpaceData FlowmapData from GetFlowmapDataTextureSpace()
 * @return FlowmapData Complete flowmap data with world-space flow vector
 *
 * @note Use this overload when you already have texture-space flowmap data to avoid duplicate texture sampling
 */
FlowmapData GetFlowmapDataWorldSpace(FlowmapData textureSpaceData)
{
	FlowmapData data = textureSpaceData;
	float2 flowDirection = -(data.color.xy * 2 - 1);    // Decode direction with 180° correction
	data.flowVector = data.flowVector * flowDirection;  // Transform to world space
	return data;
}
#			endif

#			if defined(LOD)
#				undef WATER_OPTICS
#				undef RAIN_RESPONSE
#			endif

#			if defined(WATER_OPTICS) && !defined(VC)
#				define WATER_PARALLAX
#				include "WaterOptics/WaterParallax.hlsli"
#			endif

#			if defined(WORLD_PROBES)
#				include "WorldProbes/WorldProbes.hlsli"
#			endif

#			if defined(RAIN_RESPONSE)
#				include "RainResponse/RainResponse.hlsli"
#			endif

// Structure to return both normal and ripple/splash color information
struct WaterNormalData
{
	float3 normal;
	float4 rippleInfo;  // xyz = scaled ripple normal (normalized normal * intensity), w = splash effect intensity
};

WaterNormalData GetWaterNormal(PS_INPUT input, float distanceFactor, float normalsDepthFactor, float3 viewDirection, float depth, float wetnessOcclusion)
{
	WaterNormalData result;
	result.rippleInfo = float4(0, 0, 0, 0);

	float3 normalScalesRcp = rcp(max(abs(input.NormalsScale.xyz), 1e-5f.xxx));

#			if defined(WATER_PARALLAX)
	// Surface microrelief is UV-only: use it only where the normal field is
	// visible. Far geometry and flat normal maps should not raymarch.
	float cameraWaterDistance = length(input.WPosition.xyz);
	float waterParallaxSceneWeight = saturate(normalsDepthFactor * 4.0f) *
		(1.0f - smoothstep(2300.0f, 6200.0f, cameraWaterDistance)) *
		smoothstep(0.005f, 0.05f, max(NormalsAmplitude.x,
			max(NormalsAmplitude.y, NormalsAmplitude.z)));
	float2 parallaxOffset = 0.0f.xx;
	// Flowmap shading has its own POM intersection; non-Flowmap normals and
	// optional rain ripples are the only consumers of the general UV offset.
#				if !defined(FLOWMAP) || defined(RAIN_RESPONSE)
	parallaxOffset = WaterOptics::GetParallaxOffsetScene(
		input, normalScalesRcp, waterParallaxSceneWeight);
#				endif
#			endif

#			if defined(FLOWMAP)
#				if defined(WATERBODY)
	float2 flowmapDimensions = input.TexCoord4.xy;
#				else
	float2 flowmapDimensions = input.TexCoord4.xx;
#				endif
	flowmapDimensions = max(abs(flowmapDimensions), 1.0f.xx);
	float2 uvShift = 1 / (128 * flowmapDimensions);

	// Compute flowmap parallax and create parallaxed input for normal sampling
	PS_INPUT flowmapInput = input;
	float2 flowmapParallaxOffset = float2(0, 0);
#				if defined(WATER_PARALLAX) && !defined(LOD)
	// v2: one Flowmap POM trace drives both the flow coordinate and base
	// normal UV, replacing the prior independent second 16-step search.
	float parallaxAmount = WaterOptics::GetFlowmapParallaxAmountScene(
		input, flowmapDimensions, viewDirection, waterParallaxSceneWeight);
	// Preserve the existing flowmap coordinate orientation (opposite the height ray).
	float2 parallaxDir = -WaterOptics::SafeParallaxDirection(viewDirection);
	parallaxDir.y = -parallaxDir.y;
	parallaxDir *= 0.008f * saturate(-viewDirection.z * 2.0f);
	flowmapInput.TexCoord3.xy += parallaxAmount * parallaxDir;
	flowmapParallaxOffset = WaterOptics::GetFlowmapNormalUVOffsetFromAmount(
		viewDirection, parallaxAmount);
#				endif

	// Calculate cell blend weights using parallaxed input
	float2 normalMul = 0.5 + -(-0.5 + abs(frac(flowmapInput.TexCoord2.zw * (64 * flowmapDimensions)) * 2 - 1));

	// Sample flowmap normals with parallax applied
	float3 flowmapNormal0 = GetFlowmapNormal(flowmapInput, uvShift, 9.92, 0);
	float3 flowmapNormal1 = GetFlowmapNormal(flowmapInput, float2(0, uvShift.y), 10.64, 0.27);
	float3 flowmapNormal2 = GetFlowmapNormal(flowmapInput, 0.0.xx, 8, 0);
	float3 flowmapNormal3 = GetFlowmapNormal(flowmapInput, float2(uvShift.x, 0), 8.48, 0.62);

	float2 flowmapNormalWeighted =
		normalMul.y * (normalMul.x * flowmapNormal2.xy + (1 - normalMul.x) * flowmapNormal3.xy) +
		(1 - normalMul.y) *
			(normalMul.x * flowmapNormal1.xy + (1 - normalMul.x) * flowmapNormal0.xy);
	float2 flowmapDenominator = sqrt(normalMul * normalMul + (1 - normalMul) * (1 - normalMul));
	float3 flowmapNormal =
		float3(((-0.5 + flowmapNormalWeighted) / (flowmapDenominator.x * flowmapDenominator.y)) *
				   max(0.4, normalsDepthFactor),
			0);
	flowmapNormal.z =
		sqrt(saturate(1 - flowmapNormal.x * flowmapNormal.x - flowmapNormal.y * flowmapNormal.y));
	float2 baseNormalUv = input.TexCoord1.xy;
#				if defined(WATER_PARALLAX)
	// Use flowmap-derived parallax offset for base normals
	baseNormalUv += flowmapParallaxOffset.xy * normalScalesRcp.x;
#				endif
	float3 normals1 = Normals01Tex.SampleBias(Normals01Sampler, baseNormalUv, SharedData::MipBias).xyz * 2.0 + float3(-1, -1, -2);
#			endif  // End of FLOWMAP block

#			if !defined(FLOWMAP)
#				if defined(WATER_PARALLAX)
	float3 normals1 = Normals01Tex.SampleBias(Normals01Sampler, input.TexCoord1.xy + parallaxOffset.xy * normalScalesRcp.x, SharedData::MipBias).xyz * 2.0 + float3(-1, -1, -2);
#				else
	float3 normals1 = Normals01Tex.SampleBias(Normals01Sampler, input.TexCoord1.xy, SharedData::MipBias).xyz * 2.0 + float3(-1, -1, -2);
#				endif
#			endif  // End of !FLOWMAP block
#			if defined(FLOWMAP) && !defined(BLEND_NORMALS)
#				ifdef DISABLE_FLOWMAP_NORMALS
	// FLOWMAP NORMALS DISABLED: Using only base normals (flow system still active for ripples/splashes)
	float3 finalNormal = normalize(normals1 + float3(0, 0, 1));
#				else
	// FLOWMAP NORMALS ENABLED: Blending flow-based normals with base normals
	float3 finalNormal = normalize(lerp(normals1 + float3(0, 0, 1), flowmapNormal, distanceFactor));
#				endif
#			elif !defined(LOD)

#				if defined(WATER_PARALLAX)
	float3 normals2 = Normals02Tex.SampleBias(Normals02Sampler, input.TexCoord1.zw + parallaxOffset.xy * normalScalesRcp.y, SharedData::MipBias).xyz * 2.0 - 1.0;
	float3 normals3 = Normals03Tex.SampleBias(Normals03Sampler, input.TexCoord2.xy + parallaxOffset.xy * normalScalesRcp.z, SharedData::MipBias).xyz * 2.0 - 1.0;
#				else
	float3 normals2 = Normals02Tex.SampleBias(Normals02Sampler, input.TexCoord1.zw, SharedData::MipBias).xyz * 2.0 - 1.0;
	float3 normals3 = Normals03Tex.SampleBias(Normals03Sampler, input.TexCoord2.xy, SharedData::MipBias).xyz * 2.0 - 1.0;
#				endif

	float3 blendedNormal = normalize(float3(0, 0, 1) + NormalsAmplitude.x * normals1 +
									 NormalsAmplitude.y * normals2 + NormalsAmplitude.z * normals3);
#				if defined(UNDERWATER)
	float3 finalNormal = blendedNormal;
#				else
	float3 finalNormal = normalize(lerp(float3(0, 0, 1), blendedNormal, normalsDepthFactor));
#				endif

#				if defined(FLOWMAP)
	float normalBlendFactor =
		normalMul.y * ((1 - normalMul.x) * flowmapNormal3.z + normalMul.x * flowmapNormal2.z) +
		(1 - normalMul.y) * (normalMul.x * flowmapNormal1.z + (1 - normalMul.x) * flowmapNormal0.z);
	finalNormal = normalize(lerp(normals1 + float3(0, 0, 1), normalize(lerp(finalNormal, flowmapNormal, normalBlendFactor)), distanceFactor));
#				endif
#			else
	float3 finalNormal =
		normalize(float3(0, 0, 1) + NormalsAmplitude.xxx * normals1);
#			endif

#			if defined(WADING)
#				if defined(FLOWMAP)
	float2 displacementUv = input.TexCoord3.zw;
#				else
	float2 displacementUv = input.TexCoord3.xy;
#				endif
	float3 displacement = normalize(float3(NormalsAmplitude.w * (-0.5 + DisplacementTex.Sample(DisplacementSampler, displacementUv).zw),
		0.04));
	finalNormal = lerp(displacement, finalNormal, displacement.z);
#			endif

#			if defined(RAIN_RESPONSE)
	// Rain Response Debug System:
	// DEBUG_RAIN_RESPONSE Color Legend:
	// - BRIGHT MAGENTA: Ripples, BRIGHT GREEN: Splashes, CYAN: Both effects
	float4 raindropInfo = float4(0, 0, 1, 0);
	float maxRainDropDistance = SharedData::rainResponseSettings.RaindropFxRange * SharedData::rainResponseSettings.RaindropFxRange * 3;
	float rainDropDistance = dot(input.WPosition.xyz, input.WPosition.xyz);
	float distanceFadeout = saturate((1 - saturate(rainDropDistance / maxRainDropDistance)) * 3);
	if (finalNormal.z > 0 && SharedData::rainResponseSettings.Raining > 0.0f && SharedData::rainResponseSettings.EnableRaindropFx &&
		(rainDropDistance < maxRainDropDistance) && wetnessOcclusion > 0.05) {
		float rippleStrengthModifier = (wetnessOcclusion * wetnessOcclusion) * distanceFadeout;
		float3 rippleWPosition = input.WPosition.xyz + finalNormal * 16;
#				if defined(WATER_PARALLAX)
		rippleWPosition.xy += parallaxOffset;
#				endif
#				if defined(FLOWMAP)
		// Flow-following ripple enhancement: Makes raindrops follow water current
		FlowmapData worldFlowData = GetFlowmapDataWorldSpace(input, float2(0, 0));

		// Calculate flow-aware ripple offset using centralized timing logic
		// Parameters: avgFlowmapMultiplier=9.26 (average of GetWaterNormal flowmap normal multipliers: 9.92, 10.64, 8, 8.48)
		// uvToWorldScale=0.125 (1/8 - relates to 64× texture coordinate scaling factor)
		float2 flowOffset = RainResponse::GetFlowAwareRippleOffset(
			worldFlowData.flowVector,
			worldFlowData.color.w,      // Flow strength from flowmap alpha
			0.001 * ReflectionColor.w,  // Reflection timing scale (matches GetFlowmapNormal)
			9.26,                       // Average flowmap normal multiplier
			0.125                       // UV-to-world scale factor (1/8)
		);

		rippleWPosition.xy += flowOffset;
#				endif
		raindropInfo = RainResponse::GetRainDrops(rippleWPosition + FrameBuffer::CameraPosAdjust.xyz, SharedData::rainResponseSettings.Time, finalNormal, rippleStrengthModifier);

		// Calculate ripple and splash color intensities
		float rippleIntensity = length(raindropInfo.xy) * rippleStrengthModifier;
		float splashIntensity = raindropInfo.w * distanceFadeout;

		// Store ripple and splash information for color effects
		result.rippleInfo.xyz = raindropInfo.xyz * rippleIntensity;
		result.rippleInfo.w = splashIntensity;
	}
	float3 rippleNormal = normalize(raindropInfo.xyz);
	finalNormal = ReorientNormal(rippleNormal, finalNormal);
#			endif

	result.normal = finalNormal;
	return result;
}

float3 GetWaterSpecularColor(PS_INPUT input, float3 normal, float3 viewDirection, float distanceFactor, float skyBounceSpecular)
{
	if (!(Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Reflections))
		return ReflectionColor.xyz * VarAmounts.y * clamp(SharedData::waterOpticsSettings.ReflectionBrightness, 0.5f, 1.15f);

	float3 R = reflect(viewDirection, WaterParams.y * normal + float3(0, 0, 1 - WaterParams.y));
	float waterRoughness = 0.0f;
#			if USE_PIXL_WATER_OPTICS
	// Reflection law requires a unit normal. Blending wave normals with the
	// planar normal without renormalizing distorts the reflected direction.
	float3 reflectionNormal = WaterParams.y * normal + float3(0, 0, 1 - WaterParams.y);
	reflectionNormal *= rsqrt(max(dot(reflectionNormal, reflectionNormal), 1e-6f));
	R = reflect(viewDirection, reflectionNormal);
	float normalVariance = max(dot(ddx_coarse(normal), ddx_coarse(normal)), dot(ddy_coarse(normal), ddy_coarse(normal)));
	waterRoughness = clamp(0.045f + sqrt(saturate(normalVariance)) * 0.35f, 0.045f, 0.65f);
#			endif
	float3 reflectionColor = CubeMapTex.SampleLevel(CubeMapSampler, R, waterRoughness * 7.0f).xyz;

#			if defined(WORLD_PROBES)
	float3 dynamicCubemap;
	if (SharedData::InInterior) {
		dynamicCubemap = WorldProbes::EnvTexture.SampleLevel(CubeMapSampler, R, waterRoughness * 7.0f).xyz;
	} else {
		float3 specularIrradiance = 1.0;
		if (skyBounceSpecular < 1.0)
			specularIrradiance = Color::IrradianceToLinear(WorldProbes::EnvTexture.SampleLevel(CubeMapSampler, R, waterRoughness * 7.0f).xyz);

		float3 specularIrradianceReflections = 1.0;
		if (skyBounceSpecular > 0.0)
			specularIrradianceReflections = Color::IrradianceToLinear(WorldProbes::EnvReflectionsTexture.SampleLevel(CubeMapSampler, R, waterRoughness * 7.0f).xyz);

		dynamicCubemap = Color::IrradianceToGamma(lerp(specularIrradiance, specularIrradianceReflections, skyBounceSpecular));
	}

	float reflectionAmount = saturate(length(input.WPosition.xyz) / 1024.0);

	if (SharedData::HideSky)
		reflectionAmount = 0.0;
				reflectionColor = lerp(dynamicCubemap, reflectionColor, reflectionAmount);
#			endif

#			if USE_PIXL_WATER_OPTICS
	// The volumetric pass is screen-space, while this water pass falls back to
	// Skyrim's preintegrated environment cubemap at the horizon. Project the
	// reflected view ray back into the current sky image and composite its cloud
	// radiance/transmittance over that cubemap sample. Clear-depth sky rays have
	// no screen-space hit, so this specifically fills the distant reflection gap.
	// R is a world-space reflection direction (the same basis used by the
	// environment cubemaps). The cloud texture is projected in camera view
	// space, so rotate the direction before applying the projection matrix.
	float3 reflectedViewDirection = mul((float3x3)FrameBuffer::CameraView, R);
	float4 reflectedClip = mul(FrameBuffer::CameraProj, float4(reflectedViewDirection, 0.0f));
	uint cloudReflectionWidth, cloudReflectionHeight;
	VolumetricCloudReflection.GetDimensions(cloudReflectionWidth, cloudReflectionHeight);
	if (cloudReflectionWidth > 0u && cloudReflectionHeight > 0u &&
		all(isfinite(reflectedClip)) && reflectedClip.w > 1e-4f) {
		float2 reflectedSkyUV = reflectedClip.xy / reflectedClip.w * float2(0.5f, -0.5f) + 0.5f;
		if (all(reflectedSkyUV >= 0.0f) && all(reflectedSkyUV <= 1.0f)) {
			float4 reflectedCloud = VolumetricCloudReflection.SampleLevel(CubeMapSampler, reflectedSkyUV, 0.0f);
			float cloudOpacity = saturate(1.0f - reflectedCloud.a);
			reflectionColor = max(reflectedCloud.rgb, 0.0f.xxx) + reflectionColor * (1.0f - cloudOpacity);
		}
	}
#			endif

#			if USE_PIXL_WATER_OPTICS
	// At a grazing view angle Fresnel makes the reflection dominant.  The
	// cubemap can still contain a bright sky texel even when the water is in a
	// dark cave or under a heavy atmosphere, which produces a white, detached
	// sheen.  Limit only that extreme grazing contribution against the local
	// resolved fog luminance; ordinary water reflections and authored colour
	// remain untouched.  This is a luminance guard, not a blanket HDR clamp.
	float waterNdotV = saturate(dot(normal, -viewDirection));
	float grazingReflectionWeight = smoothstep(0.78f, 0.97f, 1.0f - waterNdotV);
	float localAtmosphereLuminance = max(
		Color::RGBToLuminance(Color::Fog(FogFarColor.xyz)),
		0.015f);
	float reflectionLuminance = Color::RGBToLuminance(max(reflectionColor, 0.0f.xxx));
	float grazingLuminanceLimit = max(localAtmosphereLuminance * 4.0f, 0.12f);
	float grazingLuminanceScale = min(
		1.0f,
		grazingLuminanceLimit / max(reflectionLuminance, 1e-4f));
	reflectionColor *= lerp(1.0f, grazingLuminanceScale, grazingReflectionWeight);
#			endif

#			if !defined(LOD) && NUM_SPECULAR_LIGHTS == 0
	float pointingDirection = dot(viewDirection, R) * 0.5 + 0.5;
	float pointingAlignment = dot(reflect(viewDirection, float3(0, 0, 1)), R) * 0.5 + 0.5;
	float ssrAmount = sqrt(saturate(min(pointingAlignment, pointingDirection)));
	float2 ssrReflectionUv = ((FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy) * SSRParams.zw) + 0.05 * normal.xy;
	float2 ssrReflectionUvDR = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(ssrReflectionUv);
	float4 ssrReflectionColorBlurred = SSRReflectionTex.Sample(SSRReflectionSampler, ssrReflectionUvDR);
	float4 ssrReflectionColorRaw = RawSSRReflectionTex.Sample(RawSSRReflectionSampler, ssrReflectionUvDR);
	float4 ssrReflectionColor = lerp(ssrReflectionColorBlurred, ssrReflectionColorRaw, ssrAmount * 0.7);
#			if USE_PIXL_WATER_OPTICS
	ssrReflectionColor = lerp(ssrReflectionColorRaw, ssrReflectionColorBlurred, waterRoughness);
#			endif
	float3 finalSsrReflectionColor = max(0, ssrReflectionColor.xyz);
	float ssrFraction = saturate(ssrReflectionColor.w * distanceFactor * ssrAmount);
#			if USE_PIXL_WATER_OPTICS
	// Confidence belongs to active-view UV, not backing texture UV. At reduced
	// render scale the right/bottom edges otherwise retain invalid SSR history.
	float2 edgeDistance = min(ssrReflectionUv, 1.0f.xx - ssrReflectionUv);
	float edgeConfidence = smoothstep(0.0f, 0.06f, min(edgeDistance.x, edgeDistance.y));
	float luminanceReference = max(Color::RGBToLuminance(reflectionColor), 0.05f);
	float luminanceSSR = Color::RGBToLuminance(finalSsrReflectionColor);
	float surfaceSsrStrength = SharedData::waterOpticsSettings.EnableEnhancedSSR != 0 ?
		clamp(SharedData::waterOpticsSettings.SurfaceSSRStrength, 0.0f, 2.5f) : 1.0f;
	// Keep night reflections tied to the fallback environment instead of allowing
	// a bright screen-space source to exceed it by the former fixed 8x ceiling.
	// A bright off-screen/sky hit against a dark enclosed-water fallback reads as
	// a rectangular patch when its confidence changes at an occluder. Keep the
	// fallback's luminance relationship while still allowing brighter real hits.
	float ssrLuminanceCeiling = lerp(1.60f, 3.00f, saturate((surfaceSsrStrength - 1.0f) / 1.5f));
	finalSsrReflectionColor *= min(1.0f, (luminanceReference * ssrLuminanceCeiling) / max(luminanceSSR, 1e-4f));
	// Boost confidence rather than raw radiance, preserving reflected colour and
	// the cubemap fallback while making valid on-screen detail more authoritative.
	// The old exponential capped even a perfect hit at ~58% for strength=1.
	// Trust valid screen detail while retaining smooth cube fallback at misses.
	ssrFraction = saturate(ssrFraction * surfaceSsrStrength) * edgeConfidence * saturate(1.0f - waterRoughness * 0.50f);
#			endif
	reflectionColor = lerp(reflectionColor, finalSsrReflectionColor, ssrFraction);
#			endif

	// The cubemap remains useful at distance, but an unattenuated HDR sample can
	// read as a bright strip through atmospheric fog. Fade only the far-water
	// reflection contribution; refraction and the fog composite remain unchanged.
	float farWaterReflectionFade = lerp(
		1.0f,
		0.72f,
		smoothstep(1800.0f, 7000.0f, length(input.WPosition.xyz)));
	return reflectionColor * farWaterReflectionFade *
		clamp(SharedData::waterOpticsSettings.ReflectionBrightness, 0.5f, 1.15f);
}

float GetScreenDepthWater(float2 screenPosition)
{
	float depth = DepthTex.Load(float3(screenPosition, 0)).x;
	float denominator = -depth * CameraDataWater.z + CameraDataWater.x;
	denominator = denominator >= 0.0f ? max(denominator, 1e-6f) : min(denominator, -1e-6f);
	return CameraDataWater.w / denominator;
}

float3 GetLdotN(float3 normal)
{
#			if defined(UNDERWATER)
	return 1;
#			else
	if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior)
		return 1;
	return saturate(dot(SunDir.xyz, normal));
#			endif
}

float GetFresnelValue(float3 normal, float3 viewDirection)
{
#			if defined(UNDERWATER)
	float3 actualNormal = -normal;
#			else
	float3 actualNormal = normal;
#			endif
	float viewAngle = 1 - saturate(dot(-viewDirection, actualNormal));
#			if USE_PIXL_WATER_OPTICS
	float dielectricF0 = clamp(FresnelRI.x, 0.0204f, 0.08f);
	return dielectricF0 + (1.0f - dielectricF0) * BRDF::Pow5(viewAngle);
#			else
	return (1 - FresnelRI.x) * pow(viewAngle, 5) + FresnelRI.x;
#			endif
}

struct DiffuseOutput
{
	float3 refractionColor;
	float3 refractionDiffuseColor;
	float depth;
	float refractionMul;
	float3 refractedViewDirection;
	float receiverDistance;
};

DiffuseOutput GetWaterDiffuseColor(PS_INPUT input, float3 normal, float3 viewDirection, inout float4 distanceMul, float refractionsDepthFactor, float fresnel, float3 viewPosition, float depth)
{
#			if defined(REFRACTIONS)
	float4 refractionNormal = mul(transpose(TextureProj), float4((VarAmounts.w * refractionsDepthFactor * normal.xy) + input.MPosition.xy, input.MPosition.z, 1));

	float refractionW = refractionNormal.w >= 0.0f ? max(refractionNormal.w, 1e-6f) : min(refractionNormal.w, -1e-6f);
	float2 refractionUvRaw = float2(refractionNormal.x, refractionNormal.w - refractionNormal.y) / refractionW;
	float2 screenPosition = FrameBuffer::DynamicResolutionParams1.xy * (FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy);

	float2 refractionScreenPosition = FrameBuffer::DynamicResolutionParams1.xy * (refractionUvRaw / VPOSOffset.xy);
	float viewZ = viewPosition.z >= 0.0f ? max(viewPosition.z, 1e-6f) : min(viewPosition.z, -1e-6f);
	float4 refractionWorldPosition = float4(input.WPosition.xyz * depth / viewZ, 0);

#				if defined(DEPTH) && !defined(VERTEX_ALPHA_DEPTH)
	float refractionDepth = GetScreenDepthWater(refractionScreenPosition);
	depth = refractionDepth;
	float2 safeProj = max(abs(ProjData.xy), 1e-6f.xx);
	float refractionDepthMul = length(float3((((VPOSOffset.zw + refractionUvRaw) * 2 - 1)) * refractionDepth / safeProj, refractionDepth));

	float3 refractionDepthAdjustedViewDirection = -viewDirection * refractionDepthMul;
	float refractionViewSurfaceAngle = dot(refractionDepthAdjustedViewDirection, ReflectPlane[0].xyz);

	float safeRefractionAngle = refractionViewSurfaceAngle >= 0.0f ? max(refractionViewSurfaceAngle, 1e-6f) : min(refractionViewSurfaceAngle, -1e-6f);
	float refractionPlaneMul = (1 - ReflectPlane[0].w / safeRefractionAngle);

	if (refractionPlaneMul < 0.0) {
		refractionUvRaw = FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy * VPOSOffset.xy + VPOSOffset.zw;
	} else {
		distanceMul = saturate(refractionPlaneMul * float4(length(refractionDepthAdjustedViewDirection).xx, abs(refractionViewSurfaceAngle).xx) / max(abs(FogParam.z), 1e-5f));

		refractionWorldPosition = mul(FrameBuffer::CameraViewProjInverse, float4((refractionUvRaw * 2 - 1) * float2(1, -1), DepthTex.Load(float3(refractionScreenPosition, 0)).x, 1));
		float worldW = refractionWorldPosition.w >= 0.0f ? max(refractionWorldPosition.w, 1e-6f) : min(refractionWorldPosition.w, -1e-6f);
		refractionWorldPosition.xyz /= worldW;
	}

#					if defined(HORIZON_BLEND) && !defined(INTERIOR)
	if (DepthTex.Load(float3(refractionScreenPosition, 0)).x >= HorizonBlend::EmptyDepthThreshold)
		distanceMul = 1.0.xxxx;
#					endif
#				endif

	float2 refractionUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(refractionUvRaw);
	float3 refractionColor = RefractionTex.Sample(RefractionSampler, refractionUV).xyz;
	float3 refractionDiffuseColor = lerp(Color::Water(ShallowColor.xyz), Color::Water(DeepColor.xyz), distanceMul.y);
#				if USE_PIXL_WATER_OPTICS
	// Preserve Skyrim's refracted scene detail and use the authored deep colour
	// only as a relative absorption tint. Treating the gamma-space deep colour
	// itself as Beer-Lambert transmittance collapsed canals into a flat green fill.
	float3 deepTint = max(Color::Water(DeepColor.xyz), 0.001f.xxx);
	float maxTint = max(max(deepTint.r, deepTint.g), max(deepTint.b, 0.02f));
	float3 relativeTint = saturate(deepTint / maxTint);
	float3 absorption = 1.0f.xxx - relativeTint;
	float tintStrength = saturate(SharedData::waterOpticsSettings.WaterTintStrength);
	float opticalDepth = saturate(distanceMul.y) * lerp(0.20f, 1.10f, tintStrength);
	float3 transmittance = exp2(-absorption * opticalDepth);
	float inScatter = (1.0f - exp2(-opticalDepth * 0.50f)) * tintStrength * 0.45f;
	refractionColor = lerp(refractionColor * transmittance, refractionDiffuseColor, inScatter);
#				endif

#				if defined(UNDERWATER)
	float refractionMul = 0;
#				else
	float refractionMul = 1 - pow(saturate((-distanceMul.x * FogParam.z + FogParam.z) / max(abs(FogParam.w), 1e-5f)), max(FogNearColor.w, 0.0f));
#				endif

	DiffuseOutput output;
	output.refractionColor = refractionColor;
	output.refractionDiffuseColor = refractionDiffuseColor;
	output.depth = depth;
	output.refractionMul = refractionMul;
	output.refractedViewDirection = normalize(refractionWorldPosition.xyz - input.WPosition.xyz);
	output.receiverDistance = length(refractionWorldPosition.xyz - input.WPosition.xyz);
	return output;
#			else
	DiffuseOutput output;
	output.refractionColor = lerp(Color::Water(ShallowColor.xyz), Color::Water(DeepColor.xyz), fresnel) * GetLdotN(normal);
	output.refractionDiffuseColor = output.refractionColor;
	output.depth = 1;
	output.refractionMul = 1;
	output.refractedViewDirection = viewDirection;
	output.receiverDistance = 0;
	return output;
#			endif
}

float GetDynamicSurfaceCausticFocus(float3 normal, float3 worldPosition, float receiverDistance)
{
	// The opaque Lighting pass cannot see Skyrim's animated water-normal SRVs:
	// those resources are material-local t4-t6 bindings in this Water pass. Build
	// a bounded first-order refractive footprint here instead, after the complete
	// wave, flowmap and rain-ripple normal has been composed by GetWaterNormal().
	// On 1.7.104 the per-water sun constants can lag the active scene light.
	// State marks that runtime in DirLightDirection.w after resolving the live sun.
	float useActiveSun = SharedData::DirLightDirection.w > 0.5f ? 1.0f : 0.0f;
	float3 sunVector = lerp(SharedData::SunDirection.xyz, SharedData::DirLightDirection.xyz, useActiveSun);
	float sunLengthSq = dot(sunVector, sunVector);
	if (sunLengthSq < 1.0e-6f) {
		sunVector = SunDir.xyz;
		sunLengthSq = dot(sunVector, sunVector);
	}
	float3 sunDirection = sunVector * rsqrt(max(sunLengthSq, 1.0e-8f));
	float3 transmittedRay = refract(-sunDirection, normal, 0.750187f); // air -> water
	float safeRayDepth = max(-transmittedRay.z, 0.15f);
	float2 raySlope = transmittedRay.xy / safeRayDepth;

	// Convert screen-space ray-slope derivatives to the local horizontal water
	// basis. The determinant guard keeps edge-on/degenerate quads finite.
	float2 dPdx = ddx_coarse(worldPosition.xy);
	float2 dPdy = ddy_coarse(worldPosition.xy);
	float2 dSdx = ddx_coarse(raySlope);
	float2 dSdy = ddy_coarse(raySlope);
	// Coherent-screen and finite-basis gates prevent a single subpixel normal
	// from injecting an isolated caustic hot spot at shoreline silhouettes.
	if (!all(isfinite(float4(dPdx, dPdy))) ||
		!all(isfinite(float4(dSdx, dSdy))))
		return 1.0f;
	float pixelFootprint = max(length(dPdx), length(dPdy));
	float basisDet = dPdx.x * dPdy.y - dPdx.y * dPdy.x;
	float basisConditioning = saturate(abs(basisDet) /
		max(length(dPdx) * length(dPdy), 1.0e-5f));
	float safeBasisDet = abs(basisDet) > 1e-5f ? basisDet : (basisDet < 0.0f ? -1e-5f : 1e-5f);
	float invBasisDet = rcp(safeBasisDet);

	float2 slopeGradientX = float2(
		(dSdx.x * dPdy.y - dSdy.x * dPdx.y) * invBasisDet,
		(dSdy.x * dPdx.x - dSdx.x * dPdy.x) * invBasisDet);
	float2 slopeGradientY = float2(
		(dSdx.y * dPdy.y - dSdy.y * dPdx.y) * invBasisDet,
		(dSdy.y * dPdx.x - dSdx.y * dPdy.x) * invBasisDet);

	// The Jacobian measures the area change of the refracted solar footprint at
	// the visible receiver. A reciprocal area above one is focusing; below one is
	// spreading. Clamp both depth and response to avoid singular highlights.
	float focusDepth = min(max(receiverDistance, 0.0f), 768.0f);
	float j00 = 1.0f + focusDepth * slopeGradientX.x;
	float j01 = focusDepth * slopeGradientX.y;
	float j10 = focusDepth * slopeGradientY.x;
	float j11 = 1.0f + focusDepth * slopeGradientY.y;
	float footprintArea = abs(j00 * j11 - j01 * j10);
	float focusedLight = clamp(rcp(max(footprintArea, 0.55f)), 0.72f, 1.55f);

	float validBasis = abs(basisDet) > 1e-5f ? 1.0f : 0.0f;
	float daylight = smoothstep(0.04f, 0.28f, sunDirection.z) * (sunLengthSq > 1e-8f ? 1.0f : 0.0f);
	float validTransmission = smoothstep(0.02f, 0.17f, -transmittedRay.z);
	float depthFade = smoothstep(6.0f, 48.0f, focusDepth) * (1.0f - smoothstep(512.0f, 768.0f, focusDepth));
	float focusStrength = saturate(SharedData::waterOpticsSettings.CausticsFocus * 0.42f) *
		saturate(SharedData::waterOpticsSettings.CausticsStrength * 0.65f) *
		saturate(SharedData::waterOpticsSettings.CausticsVisibility * 0.80f) *
		validBasis * validTransmission * daylight * depthFade *
		basisConditioning * (1.0f - smoothstep(32.0f, 140.0f, pixelFootprint));
	return lerp(1.0f, focusedLight, focusStrength);
}

#if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(LOD) && !defined(UNDERWATER) && !defined(STENCIL)
float OceanFoamHash(float2 cell)
{
	float3 p = frac(float3(cell.xyx) * 0.1031f);
	p += dot(p, p.yzx + 33.33f);
	return frac((p.x + p.y) * p.z);
}

float2 SampleOceanFoamFilamentsGrad(float2 worldXY, float2 orbitalOffset,
	float2 worldDx, float2 worldDy)
{
	// t66 is WaterOptics' existing linear foam artwork, with generated mipmaps.
	// Bounded orbital advection shares the swell phase; it never scrolls with
	// the camera or uses an independent shoreline clock.
	float2 position = worldXY - clamp(orbitalOffset, -64.0f.xx, 64.0f.xx) * 0.65f;
	float scale = clamp(SharedData::waterOpticsSettings.FoamScale, 0.5f, 4.0f);
	float2 uv0 = position * (0.0045f * scale);
	float2 uv1 = float2(position.x * 0.819152f - position.y * 0.573576f,
		position.x * 0.573576f + position.y * 0.819152f) * (0.009f * scale) + 0.37f;
	float2 dx0 = worldDx * (0.0045f * scale);
	float2 dy0 = worldDy * (0.0045f * scale);
	float2 dx1 = float2(worldDx.x * 0.819152f - worldDx.y * 0.573576f,
		worldDx.x * 0.573576f + worldDx.y * 0.819152f) * (0.009f * scale);
	float2 dy1 = float2(worldDy.x * 0.819152f - worldDy.y * 0.573576f,
		worldDy.x * 0.573576f + worldDy.y * 0.819152f) * (0.009f * scale);
	uint width, height;
	WaterFoamStencil.GetDimensions(width, height);
	float coarsePattern, finePattern;
	[branch] if (width > 1u && height > 1u) {
		coarsePattern = WaterFoamStencil.SampleGrad(Normals01Sampler, uv0, dx0, dy0);
		finePattern = WaterFoamStencil.SampleGrad(Normals01Sampler, uv1, dx1, dy1);
	} else {
		// Graceful, spatially smooth fallback for missing optional artwork.
		float2 p = position * 0.009f;
		float2 cell = floor(p), f = frac(p);
		f = f * f * (3.0f - 2.0f * f);
		coarsePattern = lerp(lerp(OceanFoamHash(cell), OceanFoamHash(cell + float2(1,0)), f.x),
			lerp(OceanFoamHash(cell + float2(0,1)), OceanFoamHash(cell + 1.0f.xx), f.x), f.y);
		finePattern = 1.0f - coarsePattern;
	}
	float footprint = max(length(dx1), length(dy1));
	float resolve = 1.0f - smoothstep(0.04f, 0.20f, footprint);
	float filaments = smoothstep(0.18f, 0.72f, coarsePattern * 0.70f + finePattern * 0.30f);
	float bubbles = smoothstep(0.25f, 0.74f, finePattern * 0.72f + coarsePattern * 0.28f);
	// Unresolved small cells converge to coverage rather than sparkling.
	return lerp(0.55f.xx, float2(filaments, bubbles), resolve);
}

float2 SampleOceanFoamFilaments(float2 worldXY, float2 orbitalOffset)
{
	return SampleOceanFoamFilamentsGrad(worldXY, orbitalOffset,
		ddx_coarse(worldXY), ddy_coarse(worldXY));
}
#endif

float GetDynamicWaterFoam(PS_INPUT input, float3 surfaceNormal)
{
#if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(LOD) && !defined(SIMPLE) && !defined(UNDERWATER)
	// Open ocean has its own world-anchored crest, shoreline, and surfing foam.
	// The generic contact mask is based on the camera depth buffer and can paint
	// a moving rectangular patch across the large native ocean meshes.
	return 0.0f;
#else
#			if !defined(REFRACTIONS) || defined(LOD) || defined(SIMPLE) || defined(UNDERWATER) || (defined(SPECULAR) && NUM_SPECULAR_LIGHTS != 0)
	return 0.0f;
#			else
	if (SharedData::waterOpticsSettings.EnableDynamicFoam == 0)
		return 0.0f;

	float3 absolutePosition = input.WPosition.xyz + FrameBuffer::CameraPosAdjust.xyz;
	float2 foamDx = ddx_coarse(input.WPosition.xy);
	float2 foamDy = ddy_coarse(input.WPosition.xy);
	float foamScale = max(SharedData::waterOpticsSettings.FoamScale, 0.5f);
	float2 flowDirection = 0.0f.xx;
	float flowStrength = 0.0f;
	float flowChange = 0.0f;

#			if defined(FLOWMAP)
	float4 flowSample = FlowMapTex.SampleLevel(FlowMapSampler, input.TexCoord2.zw, 0.0f);
	float2 decodedFlow = -(flowSample.xy * 2.0f - 1.0f);
	float decodedLengthSq = dot(decodedFlow, decodedFlow);
	if (decodedLengthSq > 1e-6f)
		flowDirection = decodedFlow * rsqrt(decodedLengthSq);
	flowStrength = saturate(flowSample.w * sqrt(max(1.01f - flowSample.z, 0.0f)));

	// Fixed flowmap-texel differences identify bends, convergence and colliding
	// currents without introducing a screen-size or camera-orientation dependency.
	uint flowWidth;
	uint flowHeight;
	FlowMapTex.GetDimensions(flowWidth, flowHeight);
	float2 flowTexel = rcp(max(float2(flowWidth, flowHeight), 1.0f.xx));
	float2 flowX = -(FlowMapTex.SampleLevel(FlowMapSampler, input.TexCoord2.zw + float2(flowTexel.x, 0.0f), 0.0f).xy * 2.0f - 1.0f);
	float2 flowY = -(FlowMapTex.SampleLevel(FlowMapSampler, input.TexCoord2.zw + float2(0.0f, flowTexel.y), 0.0f).xy * 2.0f - 1.0f);
	flowChange = saturate((length(flowX - decodedFlow) + length(flowY - decodedFlow)) * 1.8f) * flowStrength;
#			endif

	// Measure the opaque receiver immediately behind this water pixel. The former
	// refracted-ray distance could become nearly constant over a coarse Skyrim water
	// quad, exposing the mesh as large rectangular foam tiles. Unrefracted per-pixel
	// scene depth creates a continuous contact band on the water surface instead.
	float2 screenPosition =
		FrameBuffer::DynamicResolutionParams1.xy *
		(FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy);
	float sceneViewDepth = abs(GetScreenDepthWater(screenPosition));
	float waterViewDepth = abs(mul(FrameBuffer::CameraView, float4(input.WPosition.xyz, 1.0f)).z);
	float receiverSeparation = max(sceneViewDepth - waterViewDepth, 0.0f);
	float validReceiver = (sceneViewDepth < 1000000.0f && sceneViewDepth >= waterViewDepth) ? 1.0f : 0.0f;
	float shallowContact =
		(1.0f - smoothstep(5.0f, 92.0f, receiverSeparation)) * validReceiver;

	// No contact, no flow convergence, no rapid-water source: skip all four
	// foam/rapid texture samples. This is local and cannot hide shore activity.
	if (shallowContact < 1.0e-4f && flowChange < 1.0e-4f && flowStrength < 0.015f)
		return 0.0f;

	// Sample two differently oriented, flow-advected layers. Derivative-aware
	// sampling selects the generated mip chain and keeps thin stencil filaments
	// stable under TAA/DLSS rather than crawling at distance.
	float2 flowAdvection =
		flowDirection * SharedData::Timer * lerp(0.012f, 0.052f, flowStrength);
	float2 foamUv0 = absolutePosition.xy * (0.0045f * foamScale) - flowAdvection;
	float2 rotatedPosition = float2(
		absolutePosition.x * 0.819152f - absolutePosition.y * 0.573576f,
		absolutePosition.x * 0.573576f + absolutePosition.y * 0.819152f);
	float2 foamUv1 = rotatedPosition * (0.0078f * foamScale) - flowAdvection.yx * float2(-0.73f, 0.73f);
	float2 foamGradDx0 = foamDx * (0.0045f * foamScale);
	float2 foamGradDy0 = foamDy * (0.0045f * foamScale);
	float2 foamGradDx1 = float2(foamDx.x * 0.819152f - foamDx.y * 0.573576f,
		foamDx.x * 0.573576f + foamDx.y * 0.819152f) * (0.0078f * foamScale);
	float2 foamGradDy1 = float2(foamDy.x * 0.819152f - foamDy.y * 0.573576f,
		foamDy.x * 0.573576f + foamDy.y * 0.819152f) * (0.0078f * foamScale);
	float stencil0 = WaterFoamStencil.SampleGrad(
		SampColorSampler, foamUv0, foamGradDx0, foamGradDy0);
	float stencil1 = WaterFoamStencil.SampleGrad(
		SampColorSampler, foamUv1, foamGradDx1, foamGradDy1);
	float foamStencil = smoothstep(0.18f, 0.70f, stencil0 * 0.68f + stencil1 * 0.32f);

	float surfaceActivity = saturate(
		length(surfaceNormal.xy) * 4.25f +
		flowStrength * 0.45f +
		flowChange * 1.70f);
	float contactActivity = smoothstep(0.06f, 0.55f, surfaceActivity);
	float contactFoam = shallowContact * contactActivity;
	float convergenceFoam = flowChange * smoothstep(0.22f, 0.75f, foamStencil);

	// Re-project the vanilla whitewater artwork onto the current water surface.
	// Two phase-shifted samples preserve the authored breakup while avoiding a
	// rigid, camera-relative decal. The flow mask suppresses it on still lakes;
	// strong flow and convergence make rapids visible without requiring users to
	// disable the game's water effects.
	float2 rapidAdvection = flowDirection * SharedData::Timer * lerp(0.018f, 0.082f, flowStrength);
	float2 rapidUv0 = absolutePosition.xy * (0.0105f * foamScale) - rapidAdvection;
	float2 rapidUv1 = absolutePosition.xy * (0.0165f * foamScale) + rapidAdvection.yx * float2(-0.62f, 0.62f) + 0.37f;
	float rapid0 = dot(AuthoredRapidWater.SampleGrad(
		SampColorSampler, rapidUv0, foamDx * (0.0105f * foamScale),
		foamDy * (0.0105f * foamScale)).rgb, 0.333333f.xxx);
	float rapid1 = dot(AuthoredRapidWater.SampleGrad(
		SampColorSampler, rapidUv1, foamDx * (0.0165f * foamScale),
		foamDy * (0.0165f * foamScale)).rgb, 0.333333f.xxx);
	float rapidPattern = smoothstep(0.26f, 0.72f, rapid0 * 0.62f + rapid1 * 0.38f);
	float rapidActivity = smoothstep(0.16f, 0.62f, flowStrength) *
		saturate(0.38f + flowChange * 1.45f);
	float authoredRapidFoam = rapidPattern * rapidActivity;

	// Foam is now entirely water-owned: no player-centred projected wake and no
	// camera-relative component. Flow/contact coverage is applied as a pixel layer.
	float foam = max(contactFoam * 0.82f + convergenceFoam * 0.58f, authoredRapidFoam * 0.92f);
	return saturate(
		foam * lerp(0.22f, 1.0f, foamStencil) *
		SharedData::waterOpticsSettings.FoamStrength);
#			endif
#endif
}

float3 GetSunColor(float3 normal, float3 viewDirection, float3 worldPosition)
{
#			if defined(UNDERWATER)
	return 0.0.xxx;
#			else
	if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior)
		return 0.0.xxx;

	float3 reflectionDirection = reflect(viewDirection, normal);
	// SharedData carries the resolved 1.7.104 directional source; water b2 can
	// still hold a stale sun color or local-space direction on that runtime.
	float useActiveSun = SharedData::DirLightDirection.w > 0.5f ? 1.0f : 0.0f;
	float3 sunVector = lerp(SharedData::SunDirection.xyz, SharedData::DirLightDirection.xyz, useActiveSun);
	float sunLengthSq = dot(sunVector, sunVector);
	if (sunLengthSq < 1.0e-6f) {
		sunVector = SunDir.xyz;
		sunLengthSq = dot(sunVector, sunVector);
	}
	float3 sunDirection = sunVector * rsqrt(max(sunLengthSq, 1.0e-6f));
	float sunAlignment = saturate(dot(reflectionDirection, sunDirection));
	float reflectionMul = exp2(VarAmounts.x * log2(max(sunAlignment, 1e-6f)));
#			if USE_PIXL_WATER_OPTICS
	// Flat water produces a compact directional sun glint, while choppy water
	// widens and softens it. Use screen-space normal variance as a cheap analytic
	// footprint so the new lobe gains definition without turning high-frequency
	// ripples into temporal fireflies. The authored Skyrim lobe remains the broad
	// base and the combined response is energy bounded.
	float normalVariance = max(
		dot(ddx_coarse(normal), ddx_coarse(normal)),
		dot(ddy_coarse(normal), ddy_coarse(normal)));
	float surfaceRoughness = clamp(
		0.035f + sqrt(saturate(normalVariance)) * 0.20f,
		0.035f,
		0.42f);
	float roughnessFactor = saturate((surfaceRoughness - 0.035f) / 0.385f);
	float tightExponent = lerp(720.0f, 72.0f, roughnessFactor);
	float tightGlint = exp2(tightExponent * log2(max(sunAlignment, 1e-6f)));
	float NdotV = saturate(dot(-viewDirection, normal));
	float dielectricF0 = clamp(FresnelRI.x, 0.0204f, 0.08f);
	float fresnel = dielectricF0 + (1.0f - dielectricF0) * BRDF::Pow5(1.0f - NdotV);
	float authoredLobe = reflectionMul * lerp(1.0f, 0.78f, roughnessFactor);
	float directionalGlint = tightGlint * lerp(0.55f, 1.05f, fresnel);
	reflectionMul = min(max(authoredLobe, directionalGlint), 1.05f);
	// Keep a near-horizon directional glint tied to the same local surface
	// response as the reflection guard above.  This prevents a sharp white bar
	// when the sun lobe and Fresnel peak overlap at a very shallow view angle.
	float grazingGlintWeight = smoothstep(0.82f, 0.98f, 1.0f - NdotV);
	reflectionMul *= lerp(1.0f, 0.72f, grazingGlintWeight);
#			endif

	float llDirLightMult = (SharedData::linearLightCoreSettings.enableLinearLightCore && !SharedData::linearLightCoreSettings.isDirLightLinear) ? SharedData::linearLightCoreSettings.dirLightMult : 1.0f;
	float3 waterSunRadiance = SunColor.xyz * SunDir.w;
	if (useActiveSun > 0.5f)
		waterSunRadiance = max(SharedData::DirLightColor.rgb, 0.0f.xxx);
	float3 sunColor = Color::DirectionalLight(waterSunRadiance / max(llDirLightMult, 1e-5), SharedData::linearLightCoreSettings.isDirLightLinear) * (1.0 - exp(-DeepColor.w)) * llDirLightMult;
	// Skyrim's active directional key light is refreshed in SharedData every
	// frame. Add its lobe only when it is not the same source as the water
	// technique's sun, avoiding a doubled daytime highlight while preserving a
	// lighting transition when the engine swaps/rotates the key light at night.
	float3 keyDirection = SharedData::DirLightDirection.xyz;
	float keyLengthSq = dot(keyDirection, keyDirection);
	float3 keyRadiance = max(SharedData::DirLightColor.rgb, 0.0f.xxx);
	float distinctKey = step(1.0e-4f, keyLengthSq) * step(1.0e-4f, dot(keyRadiance, 1.0f.xxx)) *
		(1.0f - smoothstep(0.985f, 0.9995f, dot(keyDirection * rsqrt(max(keyLengthSq, 1.0e-6f)), sunDirection)));
	float keyAlignment = saturate(dot(reflectionDirection,
		keyDirection * rsqrt(max(keyLengthSq, 1.0e-6f))));
	float keyLobe = exp2(clamp(VarAmounts.x, 4.0f, 512.0f) * log2(max(keyAlignment, 1.0e-6f)));
	float3 activeKeyColor = Color::DirectionalLight(keyRadiance,
		SharedData::linearLightCoreSettings.isDirLightLinear) * (1.0f - exp(-DeepColor.w));
	float3 secondaryKey = activeKeyColor * keyLobe * distinctKey * (1.0f - useActiveSun) * 0.65f;

	// Masser and Secunda are independent world-space directional emitters in
	// SharedData. Their phase-adjusted colors and directions are updated by the
	// sky each frame, so moonlit specular glints follow both moon motion and
	// phase instead of leaving the ocean tied to the daytime sun vector.
	float3 lunarSpecular = 0.0f.xxx;
	float moonExponent = clamp(VarAmounts.x, 12.0f, 512.0f);
	float masserLengthSq = dot(SharedData::MasserDirection.xyz, SharedData::MasserDirection.xyz);
	if (masserLengthSq > 1.0e-6f) {
		float alignment = saturate(dot(reflectionDirection,
			SharedData::MasserDirection.xyz * rsqrt(masserLengthSq)));
		lunarSpecular += max(SharedData::MasserColor.rgb, 0.0f.xxx) *
			exp2(moonExponent * log2(max(alignment, 1.0e-6f)));
	}
	float secundaLengthSq = dot(SharedData::SecundaDirection.xyz, SharedData::SecundaDirection.xyz);
	if (secundaLengthSq > 1.0e-6f) {
		float alignment = saturate(dot(reflectionDirection,
			SharedData::SecundaDirection.xyz * rsqrt(secundaLengthSq)));
		lunarSpecular += max(SharedData::SecundaColor.rgb, 0.0f.xxx) *
			exp2(moonExponent * log2(max(alignment, 1.0e-6f)));
	}
#				if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled) {
		sunColor *= Atmosphere::GetSunlightFogAttenuation(worldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#				endif
	return reflectionMul * (sunColor + secondaryKey) + lunarSpecular *
		(1.0f - exp(-DeepColor.w)) * 0.45f;
#			endif
}
#		endif

#		if defined(RADIANT_GRID)
#			include "RadiantGrid/RadiantGrid.hlsli"
#		endif

#		if defined(NATURAL_LIGHTING) && defined(RADIANT_GRID)
#			include "NaturalLighting/NaturalLighting.hlsli"
#		endif

#		if defined(AMBIENT_PROBE)
#			include "AmbientProbe/AmbientProbe.hlsli"
#		endif

float3 GetOceanCrestTransmission(PS_INPUT input, float3 normal, float3 viewDirection)
{
#		if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(UNDERWATER) && !defined(LOD) && !defined(STENCIL)
		float strength = saturate(OceanGeometrySettingsPS.y);
		if (strength <= 1.0e-4f || OceanShoreSettingsPS.w < 0.5f)
			return 0.0f.xxx;
		float3 sunVector = SharedData::SunDirection.xyz;
		float sunLengthSq = dot(sunVector, sunVector);
		if (sunLengthSq < 1.0e-6f) {
			sunVector = SunDir.xyz;
			sunLengthSq = dot(sunVector, sunVector);
		}
		float3 keyVector = SharedData::DirLightDirection.xyz;
		float keyLengthSq = dot(keyVector, keyVector);
		float sunIntensity = sunLengthSq > 1.0e-8f ? saturate(SunDir.w) : 0.0f;
		float keyIntensity = keyLengthSq > 1.0e-8f
			? saturate(length(max(SharedData::DirLightColor.rgb, 0.0f.xxx)) * 0.35f) : 0.0f;
		bool useKeyLight = sunIntensity <= 1.0e-4f && keyIntensity > 1.0e-4f;
		float lightLengthSq = useKeyLight ? keyLengthSq : sunLengthSq;
		if (lightLengthSq <= 1.0e-8f || (!useKeyLight && sunIntensity <= 1.0e-4f))
			return 0.0f.xxx;
		float3 lightDirection = (useKeyLight ? keyVector : sunVector) * rsqrt(lightLengthSq);
		float2 slope = GetOceanSurfaceSlope(input);
		float slopeLength = length(slope);
		// A narrow response around steep shoulders approximates the thin optical
		// path at a backlit crest. Beer-Lambert attenuation keeps thicker parts
		// dark; the term is limited to a small fraction of direct sunlight.
		float crestMask = smoothstep(0.008f, 0.060f, slopeLength) *
			(1.0f - smoothstep(0.48f, 0.82f, slopeLength));
		float thicknessMeters = lerp(1.35f, 0.08f, crestMask);
		float3 transmittance = exp(-float3(0.34f, 0.13f, 0.065f) * thicknessMeters);
		// lightDirection is water-to-light and viewDirection is camera-to-surface.
		// For backlighting, light travels along -L and exits toward the camera along
		// -V; their alignment is therefore dot(L, V).
		float backlight = pow(saturate(dot(lightDirection, viewDirection)), 0.65f);
		float crestFacing = lerp(0.22f, 1.0f, saturate(dot(normal, lightDirection)));
		float3 directLight = useKeyLight
			? Color::DirectionalLight(max(SharedData::DirLightColor.rgb, 0.0f.xxx),
				SharedData::linearLightCoreSettings.isDirLightLinear) * keyIntensity * 0.55f
			: Color::DirectionalLight(SunColor.xyz,
				SharedData::linearLightCoreSettings.isDirLightLinear) * sunIntensity;
		return directLight * transmittance * (crestMask * backlight * crestFacing * strength * 0.72f);
#		else
		return 0.0f.xxx;
#		endif
}

float OceanShoreOpticalDepth(PS_INPUT input)
{
#		if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(UNDERWATER) && !defined(LOD) && !defined(STENCIL)
		float3 absolutePosition = input.WPosition.xyz + FrameBuffer::CameraPosAdjust.xyz;
		float terrainHeight;
		float2 sampleStep;
		if (!SampleOceanTerrainHeightPS(absolutePosition.xy, terrainHeight, sampleStep))
			return 1.0e6f;
		// Optical coverage belongs to the still-water footprint. Using displaced
		// height here made a trough erase the contact film while foam swept over it.
		return input.OceanBasePosition.z + FrameBuffer::CameraPosAdjust.z - terrainHeight;
#		else
		return 1.0e6f;
#		endif
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 screenPosition = FrameBuffer::DynamicResolutionParams1.xy * (FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy);

#		if defined(SIMPLE) || defined(UNDERWATER) || defined(LOD) || defined(SPECULAR)
	float3 viewDirection = normalize(input.WPosition.xyz);

	float waterFadeRange = WaterParams.x - 8192.0f;
	waterFadeRange = waterFadeRange >= 0.0f ? max(waterFadeRange, 1e-5f) : min(waterFadeRange, -1e-5f);
	float distanceFactor = saturate(lerp(FrameBuffer::FrameParams.w, 1, (length(input.WPosition.xyz) - 8192) / waterFadeRange));
	float4 distanceMul = saturate(lerp(VarAmounts.z, 1, -(distanceFactor - 1))).xxxx;
	float distanceBlendFactor = distanceFactor;
#			if defined(WATERBODY)
	distanceBlendFactor = 1.0f;
#			endif

	bool isSpecular = false;

	float depth = 0;

#			if defined(DEPTH)
#				if defined(VERTEX_ALPHA_DEPTH)
#					if defined(VC)
	distanceMul = saturate(input.TexCoord3.z);
#					endif
#				else
	distanceMul = 0;

	depth = GetScreenDepthWater(screenPosition);
	float2 depthOffset =
		FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy * VPOSOffset.xy + VPOSOffset.zw;
	float depthMul = length(float3((depthOffset * 2 - 1) * depth / max(abs(ProjData.xy), 1e-6f.xx), depth));
	float3 depthAdjustedViewDirection = -viewDirection * depthMul;
	float viewSurfaceAngle = dot(depthAdjustedViewDirection, ReflectPlane[0].xyz);

	float safeViewSurfaceAngle = viewSurfaceAngle >= 0.0f ? max(viewSurfaceAngle, 1e-6f) : min(viewSurfaceAngle, -1e-6f);
	float planeMul = (1 - ReflectPlane[0].w / safeViewSurfaceAngle);
	distanceMul = saturate(
		planeMul * float4(length(depthAdjustedViewDirection).xx, abs(viewSurfaceAngle).xx) /
		max(abs(FogParam.z), 1e-5f));

#					if defined(HORIZON_BLEND) && !defined(INTERIOR)
	if (DepthTex.Load(float3(screenPosition, 0)).x >= HorizonBlend::EmptyDepthThreshold)
		distanceMul = 1.0.xxxx;
#					endif
#				endif
#			endif

#			if defined(UNDERWATER)
	float4 depthControl = float4(0, 1, 1, 0);
#			elif defined(LOD)
	float4 depthControl = float4(1, 0, 0, 1);
#			elif defined(SPECULAR) && (NUM_SPECULAR_LIGHTS != 0)
	float4 depthControl = float4(0, 0, 1, 0);
#			else
	float4 depthControl = DepthControl * (distanceMul - 1) + 1;
#			endif
	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);

#			if defined(SKY_BOUNCE)
	float wetnessOcclusion = 1.0;

	float3 positionMSSkyBounce = input.WPosition.xyz;

	sh2 skyBounceSH = SkyBounce::SampleNoBias(positionMSSkyBounce);
	float skyBounce = SphericalHarmonics::Unproject(skyBounceSH, float3(0, 0, 1));

	float skyBounceDiffuse = SkyBounce::EvaluateDiffuse(skyBounceSH, float3(0, 0, 1), SkyBounce::GetFadeOutFactor(input.WPosition.xyz));

	wetnessOcclusion = inWorld ? pow(saturate(skyBounce), 2) : 0;
#			endif

#			if defined(SKY_BOUNCE)
	WaterNormalData waterData = GetWaterNormal(input, distanceBlendFactor, depthControl.z, viewDirection, depth, wetnessOcclusion);
#			else
	WaterNormalData waterData = GetWaterNormal(input, distanceBlendFactor, depthControl.z, viewDirection, depth, inWorld);
#			endif

	float3 normal = waterData.normal;
    // Recover the large-scale surface orientation from actual rasterized
    // geometry. Flat water is unchanged; displaced water tilts the existing
    // PIXL ripple normal so Fresnel and reflections follow the wave surface.
    float3 surfaceCross = cross(ddx(input.WPosition.xyz), ddy(input.WPosition.xyz));
    float surfaceLengthSq = dot(surfaceCross, surfaceCross);
    float3 surfaceNormal = surfaceLengthSq > 1e-12 ? surfaceCross * rsqrt(surfaceLengthSq) : float3(0, 0, 1);
    surfaceNormal *= surfaceNormal.z < 0 ? -1 : 1;
    float3 tilt = float3(-surfaceNormal.y, surfaceNormal.x, 0);
    normal = normalize(normal + cross(tilt, normal) + cross(tilt, cross(tilt, normal)) / max(1 + surfaceNormal.z, 1e-4));
    #if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(UNDERWATER) && !defined(LOD) && !defined(STENCIL)
    if (OceanShoreSettingsPS.w > 0.5f) {
        float2 oceanSlope = GetOceanSurfaceSlope(input);
        normal = normalize(normal + float3(-oceanSlope, 0.0f));
    }
    #endif
#			if USE_PIXL_WATER_OPTICS
	float mainNormalVariance = max(dot(ddx_coarse(normal), ddx_coarse(normal)), dot(ddy_coarse(normal), ddy_coarse(normal)));
	// Preserve readable night reflections while still widening highlights where
	// the normal field genuinely becomes turbulent. The earlier 0.65 ceiling
	// blurred calm canals into a flat refraction colour.
	float pixlWaterRoughness = clamp(0.035f + sqrt(saturate(mainNormalVariance)) * 0.20f, 0.035f, 0.42f);
#			endif

#			if defined(SKY_BOUNCE)
	sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(normal, -viewDirection, 0.0);
	float skyBounceSpecular = SkyBounce::EvaluateSpecular(skyBounceSH, specularLobe, SkyBounce::GetFadeOutFactor(input.WPosition.xyz));
#			endif

	float fresnel = GetFresnelValue(normal, viewDirection);

#			if defined(SPECULAR) && (NUM_SPECULAR_LIGHTS != 0)
	float3 finalColor = 0.0.xxx;

#				if !defined(RADIANT_GRID)
	[unroll] for (int lightIndex = 0; lightIndex < NUM_SPECULAR_LIGHTS; ++lightIndex)
	{
		float3 lightVector = LightPos[lightIndex].xyz - (PosAdjust.xyz + input.WPosition.xyz);
		float3 lightDirection = normalize(normalize(lightVector) - viewDirection);
		float lightFade = saturate(length(lightVector) / max(abs(LightPos[lightIndex].w), 1e-5f));
		float lightColorMul = (1 - lightFade * lightFade);
		float LdotN = saturate(dot(lightDirection, normal));
		float3 lightColor = (Color::PointLight(LightColor[lightIndex].xyz) * pow(LdotN, FresnelRI.z)) * lightColorMul;
		finalColor += lightColor;
	}
#				endif

	finalColor *= fresnel;
#				if PIXL_WATER_RELEASE_DIAGNOSTICS && defined(RAIN_RESPONSE) && defined(DEBUG_RAIN_RESPONSE)
	// DEBUG MODE: Override specular color with debug visualization
	float3 debugColor = RainResponse::GetDebugWetnessColorSpecular(waterData.rippleInfo, 2.5, 4.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#				endif

	isSpecular = true;
#			else

#				if defined(SKY_BOUNCE)
	float3 specularColor = GetWaterSpecularColor(input, normal, viewDirection, distanceFactor, skyBounceSpecular);
#				else
	float3 specularColor = GetWaterSpecularColor(input, normal, viewDirection, distanceFactor, 1.0);
#				endif

	DiffuseOutput diffuseOutput = GetWaterDiffuseColor(input, normal, viewDirection, distanceMul, depthControl.y, fresnel, viewPosition, depth);

#				if USE_PIXL_WATER_OPTICS && defined(REFRACTIONS) && !defined(UNDERWATER)
	float dynamicCausticFocus = GetDynamicSurfaceCausticFocus(normal, input.WPosition.xyz, diffuseOutput.receiverDistance);
	float dynamicCausticsEnabled =
		(SharedData::waterOpticsSettings.EnableEnhancedCaustics != 0 &&
		 !(Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior)) ? 1.0f : 0.0f;
	// Modulate only the visible refracted receiver. Reflections, fog, water tint,
	// authored receiver caustics and the alpha/fresnel contract remain unchanged.
	diffuseOutput.refractionColor *= lerp(1.0f, dynamicCausticFocus, dynamicCausticsEnabled);
#				endif

	float surfaceShadow;
	float dirShadow = ShadowSampling::Get3DFilteredShadow(input.WPosition.xyz, diffuseOutput.refractedViewDirection, input.HPosition.xy, surfaceShadow);

	float3 dirColor;
	float3 ambientColor;
	ShadowSampling::ExtractLighting(diffuseOutput.refractionDiffuseColor, dirColor, ambientColor);

	dirColor *= dirShadow;

#				if defined(SKY_BOUNCE)
	ambientColor = Color::IrradianceToLinear(ambientColor);
	ambientColor *= skyBounceDiffuse;
	ambientColor = Color::IrradianceToGamma(ambientColor);
#				endif

	diffuseOutput.refractionDiffuseColor = dirColor + ambientColor;

	float3 diffuseColor = lerp(diffuseOutput.refractionColor, diffuseOutput.refractionDiffuseColor, diffuseOutput.refractionMul);

	depthControl = DepthControl * (distanceMul - 1) + 1;

	float3 specularLighting = 0;

#				if defined(RADIANT_GRID)
	uint lightCount = 0;

	uint clusterIndex = 0;
	if (RadiantGrid::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		lightCount = RadiantGrid::lightGrid[clusterIndex].lightCount;
		uint lightOffset = RadiantGrid::lightGrid[clusterIndex].offset;
		[loop] for (uint i = 0; i < lightCount; i++)
		{
			uint clusteredLightIndex = RadiantGrid::lightList[lightOffset + i];
			RadiantGrid::Light light = RadiantGrid::lights[clusteredLightIndex];
			if (RadiantGrid::IsLightIgnored(light) || light.lightFlags & RadiantGrid::LightFlags::Shadow) {
				continue;
			}

			float3 lightDirection = light.positionWS.xyz - input.WPosition.xyz;
			float lightDist = length(lightDirection);

#					if defined(NATURAL_LIGHTING)
			float intensityMultiplier = NaturalLighting::GetAttenuation(lightDist, light);
#					else
			float intensityFactor = saturate(lightDist / max(light.radius, 1e-5f));
			float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#					endif

			float3 normalizedLightDirection = normalize(lightDirection);

			float3 H = normalize(normalizedLightDirection - viewDirection);
			float HdotN = saturate(dot(H, normal));

			const bool isPointLightLinear = light.lightFlags & RadiantGrid::LightFlags::Linear;
			float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * pow(HdotN, FresnelRI.z) * light.fade;
#			if USE_PIXL_WATER_OPTICS
			float NdotL = saturate(dot(normal, normalizedLightDirection));
			float NdotV = saturate(dot(normal, -viewDirection));
			float VdotH = saturate(dot(-viewDirection, H));
			float3 F = BRDF::F_Schlick(clamp(FresnelRI.x, 0.0204f, 0.08f).xxx, VdotH);
			float D = BRDF::D_GGX(pixlWaterRoughness, HdotN);
			float Vis = BRDF::Vis_SmithJointApprox(pixlWaterRoughness, NdotV, NdotL);
			lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * D * Vis * F * NdotL * light.fade;
#			endif
			specularLighting += lightColor * intensityMultiplier;
		}
	}
	specularColor += specularLighting * 3;
#				endif

#				if defined(UNDERWATER)
	float3 finalSpecularColor = lerp(Color::Water(ShallowColor.xyz), specularColor, 0.5);
#				if USE_PIXL_WATER_OPTICS
	float underwaterDistance = length(input.WPosition.xyz) * GAME_UNIT_TO_M;
	float3 underwaterScatter = max(Color::Water(DeepColor.xyz), 0.001f.xxx);
	float3 underwaterAbsorption = -log(max(underwaterScatter, 0.02f.xxx));
	float3 underwaterTransmittance = exp(-underwaterAbsorption * underwaterDistance * 0.035f);
	float3 underwaterSurface = (1.0f - fresnel) * diffuseColor + fresnel * finalSpecularColor;
	float3 finalColor = underwaterSurface * underwaterTransmittance + underwaterScatter * (1.0f.xxx - underwaterTransmittance);
#				else
	float3 finalColor = saturate(1 - length(input.WPosition.xyz) * 0.002) * ((1 - fresnel) * (diffuseColor - finalSpecularColor)) + finalSpecularColor;
#				endif
	// Add ripple and splash color effects for underwater
#					if PIXL_WATER_RELEASE_DIAGNOSTICS && defined(RAIN_RESPONSE) && defined(DEBUG_RAIN_RESPONSE)
	// DEBUG MODE: Override water color with debug visualization (darker for underwater)
	float3 debugColor = RainResponse::GetDebugWetnessColorUnderwater(waterData.rippleInfo, 1.5, 2.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#					endif
#				else

	float3 sunColor = GetSunColor(normal, viewDirection, input.WPosition.xyz) * surfaceShadow;
	float dynamicFoam = GetDynamicWaterFoam(input, normal);
#if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(LOD)
	// Whitewater comes from a narrow, crest-driven surf band and the board wake.
	// The accumulated FFT foam field previously produced broad flat patches when
	// it outlived or disagreed with the terrain-derived shoreline mask.
	float2 oceanFoam = GetOceanShoreFoam(input);
	float boardWakeFoam = GetSurfBoardWakeFoam(input.WPosition.xy + FrameBuffer::CameraPosAdjust.xy);
	float shorelineFoam = max(oceanFoam.x, boardWakeFoam) * 0.82f;
	float crestFoam = oceanFoam.y * 0.76f;
	float oceanFoamCoverage = saturate(max(shorelineFoam, crestFoam) *
		OceanShoreSettingsPS.z);
	// Keep low-valued mask skirts from becoming a broad pale tint. Only coherent
	// swash and crest cores should approach the authored whitewater colour.
	dynamicFoam = max(dynamicFoam, pow(oceanFoamCoverage, 1.18f));
#endif
	float3 dynamicFoamColor = Color::Water(lerp(float3(0.62f, 0.68f, 0.69f), float3(0.88f, 0.91f, 0.90f), saturate(SunColor.w)));
#if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(LOD) && !defined(UNDERWATER)
	// Foam is a rough diffuse scatterer, not self-lit white paint. Match the
	// current sky/key lighting and suppress mirror glints beneath dense bubbles.
	float3 foamAmbient = Color::Ambient(max(SharedData::GetAmbient(float3(0,0,1)), 0.0f.xxx));
	float3 foamKeyDirection = SharedData::DirLightDirection.xyz;
	float foamKeyLengthSq = dot(foamKeyDirection, foamKeyDirection);
	float foamNdotL = foamKeyLengthSq > 1.0e-8f
		? saturate(dot(normal, foamKeyDirection * rsqrt(foamKeyLengthSq))) : 0.0f;
	float3 foamKey = Color::DirectionalLight(max(SharedData::DirLightColor.rgb, 0.0f.xxx),
		SharedData::linearLightCoreSettings.isDirLightLinear);
	float3 foamLighting = foamAmbient + foamKey * (0.22f + 0.78f * foamNdotL) * surfaceShadow;
	dynamicFoamColor = Color::Water(float3(0.82f, 0.85f, 0.84f)) * max(foamLighting, 0.012f.xxx);
	sunColor *= 1.0f - saturate(dynamicFoam) * 0.85f;
#endif
	float3 crestTransmission = GetOceanCrestTransmission(input, normal, viewDirection);
	float shoreOpticalCoverage = 1.0f;
#if defined(OCEAN_DYNAMICS) && !defined(INTERIOR) && !defined(LOD) && !defined(UNDERWATER)
	if (OceanShoreSettingsPS.w > 0.5f) {
		float shoreDepth = OceanShoreOpticalDepth(input);
		if (shoreDepth < 100000.0f)
			shoreOpticalCoverage = smoothstep(0.0f,
				max(96.0f, min(fwidth(shoreDepth) * 2.0f, 192.0f)), max(shoreDepth, 0.0f));
	}
#endif
	// At the water/terrain intersection, let the scene refracted through the
	// surface take over progressively. This softens the retreat edge without
	// expanding the displaced water mesh over the dry beach. The foam wash remains
	// separate so it can still define the incoming swash.
	sunColor *= shoreOpticalCoverage;
	crestTransmission *= shoreOpticalCoverage;

#					if defined(VC)
	float specularFraction = lerp(1, fresnel * diffuseOutput.refractionMul, distanceBlendFactor);
	specularFraction = lerp(0.0f, specularFraction, shoreOpticalCoverage);
	float3 finalColorPreFog = lerp(diffuseColor, specularColor, specularFraction) + sunColor * depthControl.w + crestTransmission;
	finalColorPreFog = lerp(finalColorPreFog, dynamicFoamColor, dynamicFoam * (1.0f - fresnel * 0.45f));

#						if !defined(WATERBODY)
	float fogDistanceFactor = input.FogParam.w;
	float3 fogColor = Color::Fog(input.FogParam.xyz);
#						else
	float fogDistanceFactor = min(FogFarColor.w, pow(saturate(length(input.WPosition.xyz) * FogParam.y - FogParam.x), FresnelRI.y));
	float3 fogColor = Color::Fog(lerp(FogNearColor.xyz, FogFarColor.xyz, fogDistanceFactor));
#						endif

	fogDistanceFactor = Color::FogAlpha(fogDistanceFactor);

#						if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		fogColor = AmbientProbe::GetFogAmbientColor(fogColor);
	}
#						endif
#						if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled) {
		float4 atmosphere = Atmosphere::GetAtmosphere(input.WPosition.xyz, FrameBuffer::CameraPosAdjust.xyz, fogColor, float4(input.HPosition.xy * FrameBuffer::DynamicResolutionParams2.xy, input.HPosition.z, 1));
		if (Atmosphere::ShouldDisableVanillaFog()) {
			fogColor = atmosphere.xyz;
			fogColor *= GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, fogColor, atmosphere.w);
		} else {
			fogColor *= GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, fogColor, fogDistanceFactor);
			float3 expFogColor = atmosphere.xyz * GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, expFogColor, atmosphere.w);
		}
	} else {
		fogColor *= GetWaterFogFade();
		finalColorPreFog = lerp(finalColorPreFog, fogColor, fogDistanceFactor);
	}
#						else
	fogColor *= GetWaterFogFade();
	finalColorPreFog = lerp(finalColorPreFog, fogColor, fogDistanceFactor);
#						endif

	float3 finalColor = finalColorPreFog;
#if defined(OCEAN_DYNAMICS) && defined(REFRACTIONS) && !defined(INTERIOR) && !defined(LOD) && !defined(UNDERWATER)
	[branch] if (shoreOpticalCoverage < 0.999f) {
		float2 beachUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(
			FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy * VPOSOffset.xy + VPOSOffset.zw);
		float3 beachScene = RefractionTex.SampleLevel(RefractionSampler, beachUV, 0).rgb;
		finalColor = lerp(beachScene, finalColor, max(shoreOpticalCoverage, saturate(dynamicFoam) * 0.9f));
	}
#endif

#						if PIXL_WATER_RELEASE_DIAGNOSTICS && defined(RAIN_RESPONSE) && defined(DEBUG_RAIN_RESPONSE)
	// DEBUG MODE: Override water color with debug visualization
	float3 debugColor = RainResponse::GetDebugWetnessColorStandard(waterData.rippleInfo, 2.0, 3.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#						endif

#					else
	float specularFraction = lerp(1, fresnel, distanceBlendFactor);
	specularFraction = lerp(0.0f, specularFraction, shoreOpticalCoverage);
	float3 finalColorPreFog = lerp(diffuseOutput.refractionDiffuseColor, specularColor, specularFraction) + sunColor * depthControl.w + crestTransmission;
	finalColorPreFog = lerp(finalColorPreFog, dynamicFoamColor, dynamicFoam * (1.0f - fresnel * 0.45f));

#						if !defined(WATERBODY)
	float fogDistanceFactor = input.FogParam.w;
	float3 preFogColor = Color::Fog(input.FogParam.xyz);
#						else
	float fogDistanceFactor = min(FogFarColor.w, pow(saturate(length(input.WPosition.xyz) * FogParam.y - FogParam.x), FresnelRI.y));
	float3 preFogColor = Color::Fog(lerp(FogNearColor.xyz, FogFarColor.xyz, fogDistanceFactor));
#						endif

	fogDistanceFactor = Color::FogAlpha(fogDistanceFactor);

#						if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		preFogColor = AmbientProbe::GetFogAmbientColor(preFogColor);
	}
#						endif
#						if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled) {
		float4 atmosphere = Atmosphere::GetAtmosphere(input.WPosition.xyz, FrameBuffer::CameraPosAdjust.xyz, preFogColor, float4(input.HPosition.xy * FrameBuffer::DynamicResolutionParams2.xy, input.HPosition.z, 1));
		if (Atmosphere::ShouldDisableVanillaFog()) {
			preFogColor = atmosphere.xyz;
			preFogColor *= GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, preFogColor, atmosphere.w);
		} else {
			preFogColor *= GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, preFogColor, fogDistanceFactor);
			float3 expFogColor = atmosphere.xyz * GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, expFogColor, atmosphere.w);
		}
	} else {
		preFogColor *= GetWaterFogFade();
		finalColorPreFog = lerp(finalColorPreFog, preFogColor, fogDistanceFactor);
	}
#						else
	preFogColor *= GetWaterFogFade();

	finalColorPreFog = lerp(finalColorPreFog, preFogColor, fogDistanceFactor);
#						endif

	float3 refractionColor = diffuseOutput.refractionColor;

	float fogFactor = min(FogParam.w, pow(saturate(-diffuseOutput.depth * FogParam.y - FogParam.x), FogParam.z));
	float3 fogColor = Color::Fog(lerp(FogNearColor.xyz, FogFarColor.xyz, fogFactor));
#						if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled && Atmosphere::ShouldDisableVanillaFog()) {
		fogFactor = 0;
	}
#						endif
#						if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		fogColor = AmbientProbe::GetFogAmbientColor(fogColor);
	}
#						endif
	refractionColor = lerp(refractionColor, fogColor, Color::FogAlpha(fogFactor));

	float3 finalColor = lerp(refractionColor, finalColorPreFog, diffuseOutput.refractionMul);
#if defined(OCEAN_DYNAMICS) && defined(REFRACTIONS) && !defined(INTERIOR) && !defined(LOD) && !defined(UNDERWATER)
	[branch] if (shoreOpticalCoverage < 0.999f) {
		float2 beachUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(
			FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy * VPOSOffset.xy + VPOSOffset.zw);
		float3 beachScene = RefractionTex.SampleLevel(RefractionSampler, beachUV, 0).rgb;
		finalColor = lerp(beachScene, finalColor, max(shoreOpticalCoverage, saturate(dynamicFoam) * 0.9f));
	}
#endif
	// Skyrim's native water tiles already own shoreline coverage. Their mesh
	// edge and the terrain heightfield are not guaranteed to share topology or
	// resolution, so never clip/fade native water from bathymetry: doing so made
	// large holes and bright refraction slabs wherever the two approximations
	// disagreed. Bathymetry remains authoritative for shoaling and crest foam.
#						if PIXL_WATER_RELEASE_DIAGNOSTICS && defined(RAIN_RESPONSE) && defined(DEBUG_RAIN_RESPONSE)
	// DEBUG MODE: Override water color with debug visualization
	float3 debugColor = RainResponse::GetDebugWetnessColorStandard(waterData.rippleInfo, 2.0, 3.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#						endif
#					endif

#				endif
#			endif
#		if defined(HORIZON_BLEND) && !defined(INTERIOR)
	// The folded far-water skirt is a geometry extension, not a real receiver.
	// At a grazing view angle it previously remained fully opaque and formed a
	// bright horizontal line against the sky. Blend it toward the already
	// computed atmospheric fog colour only for distant, near-horizontal rays.
	// WPosition is world-space (and may be camera-relative after render-origin
	// adjustment), so its absolute Z is not the viewing elevation.  Use the ray
	// from the camera to the water surface; this keeps ordinary nearby water
	// from being classified as a horizon just because the world happens to be
	// high above the origin.
	// WPosition is already relative to the current camera/render origin.
	float3 cameraToWater = input.WPosition.xyz;
	float cameraToWaterLengthSq = dot(cameraToWater, cameraToWater);
	cameraToWaterLengthSq = isfinite(cameraToWaterLengthSq) ? max(cameraToWaterLengthSq, 0.0f) : 0.0f;
	float viewElevation = cameraToWaterLengthSq > 1e-8f
		? abs(cameraToWater.z * rsqrt(cameraToWaterLengthSq))
		: 1.0f;
	float horizonAngleFade = 1.0f - HorizonBlend::HorizonSmootherStep(
		HorizonBlend::FadeEndElevation,
		HorizonBlend::FadeStartElevation,
		viewElevation);
	float horizonDistanceFade = HorizonBlend::HorizonSmootherStep(
		HorizonBlend::FadeStartDistance,
		HorizonBlend::FadeEndDistance,
		saturate(distanceBlendFactor));
	// HORIZON_BLEND folds only the beyond-far-plane skirt to FoldedDepth.  The
	// previous implementation applied the atmospheric blend to any distant,
	// grazing water pixel, creating a pale rectangular sheen on real surfaces.
	// Gate the effect on the folded depth itself so regular water keeps its
	// normal reflection/refraction composition.
	// Pixel-shader SV_Position.z is device depth; .w is reciprocal clip W.
	// Dividing again classified ordinary distant water as the folded skirt.
	float waterDepth = input.HPosition.z;
	float depthTransition = max(fwidth(waterDepth) * 1.5f, 8.0f / 16777216.0f);
	float foldedSkirtMask = HorizonBlend::HorizonSmootherStep(
		HorizonBlend::FoldedDepth - depthTransition,
		HorizonBlend::FoldedDepth,
		waterDepth);
	float horizonSkirtFade = horizonAngleFade * horizonDistanceFade * foldedSkirtMask;
	// The folded skirt is composited after the normal water fog path.  The raw
	// far-fog value is slightly brighter than PIXL's resolved atmospheric result,
	// so attenuate it to avoid a visible pale seam at the transition.
	float3 horizonFogColor = max(Color::Fog(FogFarColor.xyz), 0.0f) * lerp(0.78f, 0.84f, horizonAngleFade);
	// Keep ordinary water fully shaded; only the folded far-plane skirt fades into
	// the atmosphere.  Reversing these arguments would turn all non-horizon water
	// into fog because horizonSkirtFade is zero for normal water surfaces.
	finalColor = lerp(finalColor, horizonFogColor, horizonSkirtFade);
#		endif

#		if USE_PIXL_WATER_OPTICS && !defined(UNDERWATER)
	// Clouds composite before Skyrim draws water. Restore only cloud segments
	// that are nearer than this water surface; cloud depth keeps farther layers
	// correctly refracted/reflected through the water instead of painting over it.
	uint cloudWidth, cloudHeight;
	VolumetricCloudReflection.GetDimensions(cloudWidth, cloudHeight);
	if (cloudWidth > 0u && cloudHeight > 0u && all(isfinite(screenUV)) &&
		all(screenUV >= 0.0f) && all(screenUV <= 1.0f)) {
		float4 foregroundCloud = VolumetricCloudReflection.SampleLevel(CubeMapSampler, screenUV, 0.0f);
		float foregroundCloudDepth = VolumetricCloudDepth.SampleLevel(CubeMapSampler, screenUV, 0.0f);
		float waterDistance = length(input.WPosition.xyz);
		float depthTolerance = max(64.0f, waterDistance * 0.002f);
		if (all(isfinite(foregroundCloud)) && isfinite(foregroundCloudDepth) && foregroundCloudDepth > 0.0f &&
			isfinite(waterDistance) && waterDistance > 0.0f &&
			foregroundCloudDepth + depthTolerance < waterDistance && foregroundCloud.a < 0.999f) {
			finalColor = max(foregroundCloud.rgb, 0.0f.xxx) + finalColor * saturate(foregroundCloud.a);
		}
	}
#		endif
	psout.Lighting = float4(finalColor, isSpecular);
#		endif

#		if defined(STENCIL)
	float3 viewDirection = normalize(input.WorldPosition.xyz);
	float3 geometricNormal = cross(ddx_coarse(input.WorldPosition.xyz), ddy_coarse(input.WorldPosition.xyz));
	float geometricNormalLengthSq = dot(geometricNormal, geometricNormal);
	float3 normal = geometricNormalLengthSq > 1e-10f ? geometricNormal * rsqrt(geometricNormalLengthSq) : float3(0, 0, 1);
	float VdotN = dot(viewDirection, normal);
	psout.WaterMask = float4(0, 0, VdotN, 0);

	psout.MotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);
#		endif

	return psout;
}

#	endif

#	if defined(OCEAN_TESSELLATION) && (defined(HULLSHADER) || defined(DOMAINSHADER) || defined(GEOMETRYSHADER))
// The water VS and PS keep their original permutation and interface. These
// stages subdivide only classified exterior ocean draws between them.
Texture2DArray<float4> OceanSurfaceCurrent : register(t120);
Texture2DArray<float4> OceanSurfacePrevious : register(t121);
Texture2D<float> OceanSeaFloor : register(t122);
Texture2D<float4> OceanWaveCageTexture : register(t123);
Texture2D<float> OceanCellSeaFloor : register(t124);
SamplerState OceanSurfaceSampler : register(s15);
cbuffer OceanSurfaceConstants : register(b7)
{
	float4 OceanMapBounds;
	float4 OceanHeightEncoding;
	float4 OceanSurfaceSettings;
	float4 OceanGeometrySettings;
	float4 OceanPhaseSettings;
	float4 OceanTimingSettings;
	float4 OceanSurfBoardPosition;
	float4 OceanSurfBoardVelocity;
	float4 OceanLocalTerrainPatch;
};
cbuffer OceanWaveCageConstants : register(b13)
{
	float4 OceanWaveCageSettings;
};

bool SampleOceanFloor(float2 worldXY, out float floorHeight, out float2 sampleStep)
{
	floorHeight = -32768.0f;
	sampleStep = 1.0f.xx;
	float localEdgeDistance = 0.0f;
	bool found = false;
	if (OceanLocalTerrainPatch.w > 0.5f && OceanLocalTerrainPatch.z > 1.0f) {
		uint localWidth,localHeight;
		OceanCellSeaFloor.GetDimensions(localWidth,localHeight);
		float2 lastSample=max(float2(localWidth,localHeight)-1.0f,1.0f.xx);
		float2 grid = (worldXY - OceanLocalTerrainPatch.xy) / OceanLocalTerrainPatch.z;
		if (all(grid >= 0.0f) && all(grid <= lastSample)) {
			uint2 cell = min((uint2)floor(grid), (uint2)(lastSample-1.0f));
			float2 f = saturate(grid - cell);
			float h00 = OceanCellSeaFloor.Load(int3(cell, 0));
			float h10 = OceanCellSeaFloor.Load(int3(cell + uint2(1, 0), 0));
			float h01 = OceanCellSeaFloor.Load(int3(cell + uint2(0, 1), 0));
			float h11 = OceanCellSeaFloor.Load(int3(cell + uint2(1, 1), 0));
			if (min(min(h00, h10), min(h01, h11)) > -30000.0f) {
				floorHeight = lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
				localEdgeDistance = min(min(grid.x, grid.y), min(lastSample.x - grid.x, lastSample.y - grid.y));
				found = true;
				sampleStep = OceanLocalTerrainPatch.z.xx;
			}
		}
	}
	float2 extent = OceanMapBounds.zw - OceanMapBounds.xy;
	if ((!found || localEdgeDistance < 4.0f) && OceanSurfaceSettings.x > 0.5f && all(abs(extent) > 1.0f)) {
		float2 uv = (worldXY - OceanMapBounds.xy) / extent;
		if (all(uv >= 0.0f) && all(uv <= 1.0f)) {
			uint width, height;
			OceanSeaFloor.GetDimensions(width, height);
			float2 texel = rcp(max(float2(width, height), 1.0f.xx));
			uv = clamp(uv, 0.5f * texel, 1.0f - 0.5f * texel);
			float encoded = OceanSeaFloor.SampleLevel(OceanSurfaceSampler, uv, 0.0f);
			float globalHeight = clamp(lerp(OceanHeightEncoding.x, OceanHeightEncoding.y, encoded),
				OceanHeightEncoding.z, OceanHeightEncoding.w);
			float2 globalStep = abs(extent / max(float2(width, height), 1.0f.xx));
			if (found) {
				float localWeight = smoothstep(0.0f, 4.0f, localEdgeDistance);
				floorHeight = lerp(globalHeight, floorHeight, localWeight);
				sampleStep = OceanLocalTerrainPatch.z.xx;
			} else {
				floorHeight = globalHeight;
				sampleStep = globalStep;
			}
			found = true;
		}
	}
	return found;
}

float3 OceanPatchWorld(VS_OUTPUT vertex)
{
#		if defined(STENCIL)
	return vertex.WorldPosition.xyz;
#		else
	return vertex.WPosition.xyz;
#		endif
}

struct OCEAN_PATCH_CONSTANTS
{
	float edge[3] : SV_TessFactor;
	float inside : SV_InsideTessFactor;
};

float OceanEdgeTessellation(float3 a, float3 b)
{
	// Input positions use PIXL's camera-relative frame. Projected edge demand
	// allocates work by visible pixel size. Symmetric endpoint evaluation makes
	// adjacent triangles agree on their shared edge.
	float4 clipA = mul(FrameBuffer::CameraViewProj, float4(a, 1.0f));
	float4 clipB = mul(FrameBuffer::CameraViewProj, float4(b, 1.0f));
	float2 ndcA = clipA.xy / max(abs(clipA.w), 1.0e-4f);
	float2 ndcB = clipB.xy / max(abs(clipB.w), 1.0e-4f);
	float2 pixelEdge = (ndcA - ndcB) * (0.5f * SharedData::BufferDim.xy);
	float projectedPixels = length(pixelEdge);
	// Keep this linear in projected edge length and use integer partitioning for
	// stable shared-edge subdivision.
	float projectedDemand = max(projectedPixels, 0.0f) / 64.0f;
	// CPU sends the normalized detail tiers 0.42 / 0.60 / 0.82 / 1.0.
	// Map those tiers explicitly so Detailed and Strong do not collapse to the
	// same tessellation cap as Balanced.
	float qualityTier = saturate(OceanGeometrySettings.x);
	float qualityScale = lerp(0.72f, 1.55f, qualityTier);
	// Integer power-of-two factors bound work across adjacent water triangles.
	float maxFactor = qualityTier < 0.5f ? 16.0f : qualityTier < 0.75f ? 32.0f : 64.0f;
	float2 midpointXY = 0.5f * (a.xy + b.xy) + FrameBuffer::CameraPosAdjust.xy;
	float floorHeight;
	float2 sampleStep;
	float shoreBoost = 1.0f;
	if (SampleOceanFloor(midpointXY, floorHeight, sampleStep)) {
		float edgeDepth = 0.5f * (a.z + b.z) + FrameBuffer::CameraPosAdjust.z - floorHeight;
		float shoreProximity = 1.0f - smoothstep(96.0f, 1152.0f, max(edgeDepth, 0.0f));
		shoreBoost = lerp(1.0f, 2.5f, shoreProximity);
	}
	float demand = projectedDemand * qualityScale * shoreBoost;
	if (demand < 1.5f) return 2.0f;
	if (demand < 3.0f) return min(4.0f, maxFactor);
	if (demand < 6.0f) return min(8.0f, maxFactor);
	if (demand < 12.0f) return min(16.0f, maxFactor);
	if (demand < 24.0f) return min(32.0f, maxFactor);
	return maxFactor;
}

OCEAN_PATCH_CONSTANTS OceanPatchConstants(InputPatch<VS_OUTPUT, 3> patch,
	uint patchID : SV_PrimitiveID)
{
	OCEAN_PATCH_CONSTANTS result;
	float3 p0 = OceanPatchWorld(patch[0]);
	float3 p1 = OceanPatchWorld(patch[1]);
	float3 p2 = OceanPatchWorld(patch[2]);
	// Every edge depends on its own endpoints only, so adjacent triangles request
	// matching tessellation and do not open cracks at their borders.
	result.edge[0] = OceanEdgeTessellation(p1, p2);
	result.edge[1] = OceanEdgeTessellation(p2, p0);
	result.edge[2] = OceanEdgeTessellation(p0, p1);
	result.inside = max(result.edge[0], max(result.edge[1], result.edge[2]));
	return result;
}

#		if defined(HULLSHADER)
[domain("tri")]
[partitioning("integer")]
#			if defined(OCEAN_OUTPUT_CCW)
[outputtopology("triangle_ccw")]
#			else
[outputtopology("triangle_cw")]
#			endif
[outputcontrolpoints(3)]
[patchconstantfunc("OceanPatchConstants")]
VS_OUTPUT OceanHull(InputPatch<VS_OUTPUT, 3> patch,
	uint controlPointID : SV_OutputControlPointID,
	uint patchID : SV_PrimitiveID)
{
	return patch[controlPointID];
}
#		endif

#		if defined(DOMAINSHADER)
float3 OceanWave(float2 absoluteXY, bool previous)
{
	// Geometry uses the same broad-swell band as the native Water VS and the
	// PIXL Surfing query. Shorter cascades remain shading detail in WaterOptics;
	// putting them on coarse control points caused repeated, faceted silhouettes.
	float2 uv = absoluteXY / 32768.0f + 0.5f;
	float4 wave = previous
		? OceanSurfacePrevious.SampleLevel(OceanSurfaceSampler, float3(uv, 3u), 0)
		: OceanSurfaceCurrent.SampleLevel(OceanSurfaceSampler, float3(uv, 3u), 0);
	return float3(clamp(wave.x, -35.0f, 35.0f), clamp(wave.y, -35.0f, 35.0f),
		clamp(wave.z, -100.0f, 100.0f));
}

float2 OceanPhaseSamplePosition(float2 absoluteXY)
{
	// Keep the FFT phase continuous in world space across terrain tiles. Depth
	// response changes crest height without translating crests using coarse data.
	return absoluteXY;
}

float OceanShoreWeight(float2 absoluteXY, float waterHeight)
{
	float floorHeight;
	float2 sampleStep;
	if (!SampleOceanFloor(absoluteXY, floorHeight, sampleStep))
		return 1.0f;
	float depth = waterHeight - floorHeight;
	// This is the same narrow wet/land crossing fade as the native vertex path
	// and CPU query. The source is approximate bathymetry, so taper the surface
	// smoothly across the wet/land boundary without allowing beach run-up to
	// inherit offshore swell amplitude.
	return smoothstep(-24.0f, 48.0f, depth);
}

float3 OceanShoreTransform(float3 wave, float2 absoluteXY, float waterHeight)
{
	float terrain;
	float2 sampleStep;
	if (!SampleOceanFloor(absoluteXY, terrain, sampleStep))
		return float3(0.0f, 0.0f, clamp(wave.z, -100.0f, 100.0f));
	float depth = waterHeight - terrain;
	float period = clamp(OceanGeometrySettings.z, 2.0f, 24.0f);
	float omega = 6.28318530718f / period;
	float ratio = sqrt(clamp(tanh(omega * omega * max(depth, 0.0f) /
		(9.81f * 70.0f)), 0.001225f, 1.0f));
	float shallow = saturate((1.0f - smoothstep(200.0f, 1600.0f, max(depth, 0.0f))) *
		clamp(OceanPhaseSettings.z, 0.0f, 1.5f));
	float shoaling = lerp(1.0f, clamp(rsqrt(max(ratio, 0.05f)), 1.0f, 1.25f), shallow);
	float3 shoaledWave = wave * shoaling;
	float wet = smoothstep(-24.0f, 48.0f, depth);
	float3 verticalFallback = float3(0.0f, 0.0f,
		OceanThreeBandHeight(shoaledWave.z, shallow, depth, wet));
	if (OceanWaveCageSettings.y < 0.5f || OceanWaveCageSettings.z <= 0.0f)
		return verticalFallback;
	float2 extent = OceanMapBounds.zw - OceanMapBounds.xy;
	if (any(abs(extent) <= 1.0f))
		return verticalFallback;
	float2 cageUV = (absoluteXY - OceanMapBounds.xy) / extent;
	if (any(cageUV < 0.0f) || any(cageUV > 1.0f))
		return verticalFallback;
	float2 texel = rcp(max(OceanWaveCageSettings.xx, 1.0f.xx));
	cageUV = clamp(cageUV, 0.5f * texel, 1.0f - 0.5f * texel);
	float4 cage = OceanWaveCageTexture.SampleLevel(OceanSurfaceSampler, cageUV, 0.0f);
	if (cage.w < 0.5f)
		return verticalFallback;
	float2 frameStep = max(2.0f * sampleStep, 64.0f.xx);
	float left, right, down, up;
	float2 ignoredStep;
	bool hasGradient = SampleOceanFloor(absoluteXY - float2(frameStep.x, 0.0f), left, ignoredStep) &&
		SampleOceanFloor(absoluteXY + float2(frameStep.x, 0.0f), right, ignoredStep) &&
		SampleOceanFloor(absoluteXY - float2(0.0f, frameStep.y), down, ignoredStep) &&
		SampleOceanFloor(absoluteXY + float2(0.0f, frameStep.y), up, ignoredStep);
	float2 terrainSlope = hasGradient
		? float2(right - left, up - down) / (2.0f * frameStep)
		: 0.0f.xx;
	float3 terrainNormal = normalize(float3(-terrainSlope, 1.0f));
	float shoreSign = depth >= 0.0f ? 1.0f : -1.0f;
	float3 shoreCandidate = float3(terrainSlope * shoreSign,
		dot(terrainSlope, terrainSlope) * shoreSign);
	float shoreLengthSq = dot(shoreCandidate, shoreCandidate);
	float3 shoreAxis = shoreLengthSq > 1.0e-8f
		? shoreCandidate * rsqrt(shoreLengthSq) : float3(1.0f, 0.0f, 0.0f);
	float3 alongAxis = normalize(cross(terrainNormal, shoreAxis));
	shoreAxis = normalize(cross(alongAxis, terrainNormal));
	float referenceRadius = max(cage.x, max(cage.y, cage.z));
	float waveLength = length(shoaledWave);
	if (referenceRadius <= 1.0e-3f || waveLength <= 1.0e-6f)
		return verticalFallback;
	float3 radialClip = shoaledWave * (tanh(waveLength / referenceRadius) / waveLength);
	float3 cagedWave = shoreAxis * (cage.x * dot(shoreAxis, radialClip)) +
		alongAxis * (cage.y * dot(alongAxis, radialClip)) +
		terrainNormal * (cage.z * dot(terrainNormal, radialClip));
	return OceanBeachCoverage(lerp(verticalFallback, cagedWave * wet,
		saturate(OceanWaveCageSettings.z)), depth);
}

float3 OceanVertexBaseline(float2 absoluteXY, float waterHeight, bool previous)
{
	float2 phaseXY = OceanPhaseSamplePosition(absoluteXY);
	return OceanShoreTransform(OceanWave(phaseXY, previous), absoluteXY, waterHeight);
}

float4 OceanBary4(float4 a, float4 b, float4 c, float3 bary)
{
	return a * bary.x + b * bary.y + c * bary.z;
}

[domain("tri")]
VS_OUTPUT OceanDomain(OCEAN_PATCH_CONSTANTS constants,
	float3 bary : SV_DomainLocation, const OutputPatch<VS_OUTPUT, 3> patch)
{
	VS_OUTPUT output = (VS_OUTPUT)0;
	float4 basePosition = OceanBary4(patch[0].OceanBasePosition,
		patch[1].OceanBasePosition, patch[2].OceanBasePosition, bary);
	float3 baseWorld = basePosition.xyz;
	output.OceanBasePosition = basePosition;
	output.HPosition = OceanBary4(patch[0].HPosition, patch[1].HPosition,
		patch[2].HPosition, bary);
	output.NormalsScale = OceanBary4(patch[0].NormalsScale,
		patch[1].NormalsScale, patch[2].NormalsScale, bary);
#			if defined(STENCIL)
	output.WorldPosition = OceanBary4(patch[0].WorldPosition,
		patch[1].WorldPosition, patch[2].WorldPosition, bary);
	output.PreviousWorldPosition = OceanBary4(patch[0].PreviousWorldPosition,
		patch[1].PreviousWorldPosition, patch[2].PreviousWorldPosition, bary);
#			else
	output.WPosition = OceanBary4(patch[0].WPosition,
		patch[1].WPosition, patch[2].WPosition, bary);
	output.TexCoord1 = OceanBary4(patch[0].TexCoord1,
		patch[1].TexCoord1, patch[2].TexCoord1, bary);
	output.TexCoord2 = OceanBary4(patch[0].TexCoord2,
		patch[1].TexCoord2, patch[2].TexCoord2, bary);
#				if defined(WADING) || (defined(FLOWMAP) && (defined(REFRACTIONS) || defined(BLEND_NORMALS))) || (defined(VERTEX_ALPHA_DEPTH) && defined(VC)) || (defined(SPECULAR) && NUM_SPECULAR_LIGHTS == 0 && defined(FLOWMAP))
	output.TexCoord3 = OceanBary4(patch[0].TexCoord3,
		patch[1].TexCoord3, patch[2].TexCoord3, bary);
#				endif
#				if defined(FLOWMAP)
	output.TexCoord4 = patch[0].TexCoord4;
#				endif
#				if defined(SIMPLE) || defined(UNDERWATER) || (defined(SPECULAR) && NUM_SPECULAR_LIGHTS == 0)
	output.MPosition = OceanBary4(patch[0].MPosition,
		patch[1].MPosition, patch[2].MPosition, bary);
#				endif
#				if !defined(WATERBODY) || defined(SIMPLE)
	output.FogParam = OceanBary4(patch[0].FogParam,
		patch[1].FogParam, patch[2].FogParam, bary);
#				endif
#			endif
	float2 absoluteXY = baseWorld.xy + FrameBuffer::CameraPosAdjust.xy;
	float baseHeight = baseWorld.z + FrameBuffer::CameraPosAdjust.z;
	float3 displacement = OceanVertexBaseline(absoluteXY, baseHeight, false);
	float3 displacedWorld = baseWorld + displacement;
	output.HPosition = mul(FrameBuffer::CameraViewProj, float4(displacedWorld, 1.0f));
#			if defined(STENCIL)
	output.WorldPosition.xyz = displacedWorld;
	float4 previousBasePosition = OceanBary4(patch[0].OceanPreviousBasePosition,
		patch[1].OceanPreviousBasePosition, patch[2].OceanPreviousBasePosition, bary);
	output.OceanPreviousBasePosition = previousBasePosition;
	output.PreviousWorldPosition = previousBasePosition;
	float2 previousXY = previousBasePosition.xy +
		FrameBuffer::CameraPreviousPosAdjust.xy;
	output.PreviousWorldPosition.xyz += OceanVertexBaseline(previousXY,
		previousBasePosition.z + FrameBuffer::CameraPreviousPosAdjust.z, true);
#			else
	output.WPosition.xyz = displacedWorld;
	output.WPosition.w = length(displacedWorld);
#			endif
	return output;
}
#		endif

#		if defined(GEOMETRYSHADER)
// Native coverage is authoritative. Bathymetry is an approximation: clipping
// triangles against it made the water retreat in polygonal chunks and hid the
// swash underneath. Shallow displacement is bounded before rasterization; the
// normal terrain depth buffer supplies the real intersection without a second
// synthetic coast contour.
[maxvertexcount(3)]
void OceanShoreline(triangle VS_OUTPUT input[3], inout TriangleStream<VS_OUTPUT> stream)
{
    stream.Append(input[0]);
    stream.Append(input[1]);
    stream.Append(input[2]);
    stream.RestartStrip();
}
#		endif
#	endif

#endif
