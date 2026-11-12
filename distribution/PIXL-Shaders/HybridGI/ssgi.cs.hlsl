// PIXL screen-space GI ported from UnitySSGIURP (MIT, original authors
// credited in the accompanying HybridGI module documentation).
// The Unity render-pipeline wrapper is replaced by PIXL's DX11 depth,
// normal, radiance and temporal-history resources.
#include "Common/Color.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "HybridGI/common.hlsli"
#include "HybridGI/worldCache.hlsli"

Texture2D<float> srcDepth : register(t0);
Texture2D<float4> srcNormalRoughness : register(t1);
Texture2D<float3> srcRadiance : register(t2);
Texture2D<unorm float2> srcNoise : register(t3);
Texture2D<unorm float> srcAccumFrames : register(t4);
Texture2D<float4> srcPrevY : register(t5);
Texture2D<float2> srcPrevCoCg : register(t6);
Texture2D<float4> srcPrevSpecular : register(t7);
Texture2D<float2> srcNormal : register(t8);
Texture2D<uint> srcWorldMetadata : register(t9);
Texture2D<float4> srcWorldSH0 : register(t10);
Texture2D<float4> srcWorldSH1 : register(t11);
Texture2D<float4> srcWorldSH2 : register(t12);
Texture2D<uint> srcWorldNormal : register(t13);
Texture2D<float4> srcPrevBentVisibility : register(t15);
Texture2D<unorm float> srcPrevAO : register(t16);
Texture2D<uint> srcWorldIntelligence : register(t17);

RWTexture2D<unorm float> outAO : register(u0);
RWTexture2D<float4> outY : register(u1);
RWTexture2D<float2> outCoCg : register(u2);
RWTexture2D<uint> outPrevGeo : register(u4);
RWTexture2D<float4> outBentVisibility : register(u5);

// The screen trace owns visible hits. Use the persistent atlas only when a ray
// leaves the screen or finds no depth hit; reject stale, opposite-facing and
// low-confidence surfels before they can leak through an interior wall.
float3 ReadSSGIWorldCache(float3 queryWS, float3 receiverWS, float3 receiverNormalWS,
	float3 directionWS, float3 cameraWS, out float confidence)
{
	confidence = 0.0f;
	float cascadeBlend = WorldCacheCascadeBlend(queryWS, cameraWS);
	float3 cascadeRadiance[2] = { 0.0f.xxx, 0.0f.xxx };
	float cascadeConfidence[2] = { 0.0f, 0.0f };

	// Cache entries are surface surfels stored at cell resolution. Point lookup
	// made their boundaries visible as 3D blocks in indirect light. Reconstruct
	// from the eight surrounding cell centres, validating each surfel before it
	// enters the weighted average so interpolation cannot bridge a wall blindly.
	[unroll] for (uint cascade = 0u; cascade < 2u; ++cascade) {
		// Usually avoid the far-atlas work while well inside the near cascade.
		// If the near cache has no valid surfel, still query far as a fallback.
		if (cascade == 1u && cascadeBlend <= 1e-3f && cascadeConfidence[0] > (1.0f / 255.0f)) continue;
		float cellSize = WorldCacheCellSize(cascade);
		float3 gridPosition = queryWS / cellSize - 0.5f;
		int3 baseCell = int3(floor(gridPosition));
		float3 cellBlend = frac(gridPosition);
		float3 radianceSum = 0.0f;
		float confidenceSum = 0.0f;
		[unroll] for (uint z = 0u; z < 2u; ++z) {
			[unroll] for (uint y = 0u; y < 2u; ++y) {
				[unroll] for (uint x = 0u; x < 2u; ++x) {
					int3 cell = baseCell + int3(x, y, z);
					if (!WorldCacheCellInWindow(cell, cameraWS, cascade)) continue;
					float wx = x == 0u ? 1.0f - cellBlend.x : cellBlend.x;
					float wy = y == 0u ? 1.0f - cellBlend.y : cellBlend.y;
					float wz = z == 0u ? 1.0f - cellBlend.z : cellBlend.z;
					float spatialWeight = wx * wy * wz;
					if (spatialWeight <= 1e-4f) continue;
					uint2 coord = WorldCacheAtlasCoord(cell, cascade);
					uint metadata = srcWorldMetadata.Load(int3(coord, 0));
					if ((metadata & 0x00ffffffu) != WorldCacheHash(cell, cascade)) continue;
					uint age = ((WorldCacheClock & 255u) - (metadata >> 24)) & 255u;
					float ageFade = WorldCacheAgeFade(age, WorldCacheMaxAge);
					if (ageFade <= 0.0f) continue;
					uint packed = srcWorldNormal.Load(int3(coord, 0));
					float surfelConfidence = UnpackWorldConfidence(packed);
					float3 sourceNormalWS = UnpackWorldNormal(packed);
					float facingDot = dot(sourceNormalWS, -directionWS);
					float facing = saturate(facingDot * 4.0f);
					facing = lerp(facing, smoothstep(-0.05f, 0.25f, facingDot),
						saturate(WorldCacheLeakReduction));
					float plane = WorldReceiverPlaneConfidence(receiverWS, cell, sourceNormalWS,
						cellSize, surfelConfidence);
					float reliability = 1.0f;
#ifdef CACHE_INTELLIGENCE
					reliability = WorldIntelligenceReliability(srcWorldIntelligence.Load(int3(coord, 0)));
#endif
					float sampleConfidence = UnpackWorldOccupancy(packed) * surfelConfidence *
						ageFade * facing * plane * reliability;
					float weight = spatialWeight * sampleConfidence;
					if (weight <= 1e-5f) continue;
					float3 sampleRadiance = WorldCacheEvaluateRadiance(
						srcWorldSH0.Load(int3(coord, 0)), srcWorldSH1.Load(int3(coord, 0)),
						srcWorldSH2.Load(int3(coord, 0)), -directionWS);
					radianceSum += sampleRadiance * weight;
					confidenceSum += weight;
				}
			}
		}
		cascadeConfidence[cascade] = saturate(confidenceSum);
		cascadeRadiance[cascade] = radianceSum / max(confidenceSum, 1e-5f);
	}

	// Blend both cascades by their distance weights and valid surfel confidence.
	// Choosing only the near cascade until it became empty caused a visible energy
	// dip at the handoff even when the far cascade already held valid irradiance.
	float nearWeight = (1.0f - cascadeBlend) * cascadeConfidence[0];
	float farWeight = cascadeBlend * cascadeConfidence[1];
	// At either end of the transition, keep a valid opposite-cascade sample as
	// a fallback when the preferred cascade has not yet observed this location.
	if (nearWeight <= (1.0f / 255.0f) && cascadeConfidence[1] > (1.0f / 255.0f))
		farWeight = cascadeConfidence[1];
	if (farWeight <= (1.0f / 255.0f) && cascadeConfidence[0] > (1.0f / 255.0f))
		nearWeight = cascadeConfidence[0];
	confidence = saturate(nearWeight + farWeight);
	return (cascadeRadiance[0] * nearWeight + cascadeRadiance[1] * farWeight) /
		max(nearWeight + farWeight, 1e-5f);
}

// Inspect the resident surface cell directly. These modes must not reuse the
// ray-integrated diffuse result: doing so made cache diagnostics all look alike.
float3 InspectSSGIWorldCache(float3 receiverWS, float3 receiverNormalWS, float3 cameraWS)
{
    if (WorldCacheEnabled == 0u) return float3(1.0f, 0.0f, 1.0f);
    uint cascade = WorldCacheCascade(receiverWS, cameraWS);
    float size = WorldCacheCellSize(cascade);
    int3 cell = int3(floor(receiverWS / size));
    if (!WorldCacheCellInWindow(cell, cameraWS, cascade)) return 0.0f.xxx;
    uint2 coord = WorldCacheAtlasCoord(cell, cascade);
    uint meta = srcWorldMetadata.Load(int3(coord, 0));
    if ((meta & 0x00ffffffu) != WorldCacheHash(cell, cascade)) return 0.0f.xxx;
    uint age = ((WorldCacheClock & 255u) - (meta >> 24)) & 255u;
    uint surface = srcWorldNormal.Load(int3(coord, 0));
    float ageFade = WorldCacheAgeFade(age, WorldCacheMaxAge);
    float confidence = UnpackWorldOccupancy(surface) * UnpackWorldConfidence(surface) * ageFade;
#ifdef CACHE_INTELLIGENCE
    uint intelligence = srcWorldIntelligence.Load(int3(coord, 0));
    confidence *= WorldIntelligenceReliability(intelligence);
#else
    uint intelligence = 0u;
#endif
    if (DebugView == 5u)
        return lerp(float3(0.35f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.15f), saturate(confidence));
    if (DebugView == 6u)
        return (cascade == 0u ? float3(0.0f, 0.7f, 1.0f) : float3(1.0f, 0.1f, 0.7f)) *
            max(saturate(confidence), 0.15f);
    if (DebugView == 7u) {
        float3 irradiance = WorldCacheEvaluateRadiance(
            srcWorldSH0.Load(int3(coord, 0)), srcWorldSH1.Load(int3(coord, 0)),
            srcWorldSH2.Load(int3(coord, 0)), receiverNormalWS);
        return (1.0f.xxx - exp(-min(irradiance, 64.0f.xxx))) * saturate(confidence);
    }
    if (DebugView == 14u) return (float(intelligence & 65535u) / 65535.0f).xxx;
    if (DebugView == 15u) return saturate(float(age) / max(float(WorldCacheMaxAge), 1.0f)).xxx;
    if (DebugView == 16u) return saturate(confidence).xxx;
    if (DebugView == 17u) {
        uint state = intelligence >> 28;
        return state == WORLD_STATE_DARK ? float3(0.0f, 0.2f, 1.0f) :
            state == WORLD_STATE_RELIABLE ? float3(0.0f, 1.0f, 0.0f) :
            state == WORLD_STATE_UNCERTAIN ? float3(1.0f, 1.0f, 0.0f) :
            state == WORLD_STATE_STALE_LIGHT ? float3(1.0f, 0.0f, 0.0f) :
            state == WORLD_STATE_OCCLUDER ? float3(1.0f, 0.5f, 0.0f) :
            state == WORLD_STATE_EMPTY ? 0.2f.xxx : 0.5f.xxx;
    }
    if (DebugView == 18u) return (float((intelligence >> 16) & 255u) / 255.0f).xxx;
    return WorldReceiverPlaneConfidence(receiverWS, cell, UnpackWorldNormal(surface),
        size, confidence).xxx;
}

// Screen-space visibility is geometric. Fine normal-map ridges must not turn a
// flat depth plane into a field of self-occluders. Choose the nearer depth
// derivative on each axis so a silhouette does not pull in the background.
float3 ReceiverTraceNormal(uint2 tid, float z, float3 shadingNormal)
{
	uint2 limit = uint2(OUT_FRAME_DIM) - 1u;
	uint2 px = uint2(min(tid.x + 1u, limit.x), tid.y);
	uint2 nx = uint2(tid.x > 0u ? tid.x - 1u : tid.x, tid.y);
	uint2 py = uint2(tid.x, min(tid.y + 1u, limit.y));
	uint2 ny = uint2(tid.x, tid.y > 0u ? tid.y - 1u : tid.y);
	float zxP = READ_DEPTH(srcDepth, px), zxN = READ_DEPTH(srcDepth, nx);
	float zyP = READ_DEPTH(srcDepth, py), zyN = READ_DEPTH(srcDepth, ny);
	bool useXP = tid.x == 0u || (tid.x < limit.x && abs(zxP - z) < abs(zxN - z));
	bool useYP = tid.y == 0u || (tid.y < limit.y && abs(zyP - z) < abs(zyN - z));
	uint2 sx = useXP ? px : nx;
	uint2 sy = useYP ? py : ny;
	float dxDepth = useXP ? zxP : zxN;
	float dyDepth = useYP ? zyP : zyN;
	float samePlane = max(2.0f, z * 0.01f);
	if (!(dxDepth > FP_Z && dyDepth > FP_Z) ||
		!isfinite(dxDepth) || !isfinite(dyDepth) ||
		abs(dxDepth - z) > samePlane || abs(dyDepth - z) > samePlane)
		return shadingNormal;
	float3 center = ScreenToViewPosition((float2(tid) + 0.5f) * RCP_OUT_FRAME_DIM, z);
	float3 vx = ScreenToViewPosition((float2(sx) + 0.5f) * RCP_OUT_FRAME_DIM, dxDepth) - center;
	float3 vy = ScreenToViewPosition((float2(sy) + 0.5f) * RCP_OUT_FRAME_DIM, dyDepth) - center;
	float3 crossNormal = cross(vx, vy);
	float lengthSq = dot(crossNormal, crossNormal);
	if (lengthSq < 1e-8f || !isfinite(lengthSq)) return shadingNormal;
	float3 geometric = crossNormal * rsqrt(lengthSq);
	float orientation = dot(geometric, shadingNormal);
	// Extreme tangent-space normals can be nearly orthogonal to the real plane.
	if (abs(orientation) < 0.1f) orientation = dot(geometric, -center);
	return orientation < 0.0f ? -geometric : geometric;
}

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID)
{
	if (any(tid >= uint2(OUT_FRAME_DIM))) return;
	float2 uv = (float2(tid) + 0.5f) * RCP_OUT_FRAME_DIM;
	float z = READ_DEPTH(srcDepth, tid);
	float2 encN = srcNormal.Load(int3(tid, 0));
	float3 N = ToTraceView(GBuffer::DecodeNormal(encN));
	float3 worldN = TraceViewToWorldVector(N, FrameBuffer::CameraViewInverse);
	float2 encWorldN = GBuffer::EncodeNormal(worldN);
	outPrevGeo[tid] = PackPrevGeometry(z, encWorldN);
	if (!(z > FP_Z) || !isfinite(z) || z >= DepthFadeRange.y) {
		outAO[tid] = 0.0f; outY[tid] = 0.0f; outCoCg[tid] = 0.0f;
		outBentVisibility[tid] = float4(encWorldN, 1.0f, 1.0f); return;
	}
	float3 traceN = ReceiverTraceNormal(tid, z, N);

	// Begin above the receiver plane, in the traced hemisphere. A camera-depth
	// offset can remain below a sloped floor or textured wall and report the
	// receiver itself as an AO blocker at the first sample.
	float receiverBias = clamp(z * 0.002f, 1.5f, 4.0f);
	float3 P = ScreenToViewPosition(uv, z) + traceN * receiverBias;
	float2 noise = srcNoise.Load(int3((tid + uint2(FrameIndex * 17u, FrameIndex * 31u)) & 127u, 0));
	float3 axis = abs(traceN.z) < 0.85f ? float3(0,0,1) : float3(0,1,0);
	float3 T = normalize(cross(axis, traceN));
	float3 B = normalize(cross(traceN, T));
	uint rays = clamp(NumSlices, 2u, 8u);
	// The temporal pass only grows this count for geometry- and radiance-validated
	// history. Use it as a conservative convergence signal: freshly visible or
	// reactive pixels retain the full configured ray budget; only settled pixels
	// approach the user-selected work floor.
	float accumulated = srcAccumFrames.Load(int3(tid, 0)) * 255.0f;
#if defined(ADAPTIVE_RAY_ALLOCATION) && defined(TEMPORAL_DENOISER)
	float historyConfidence = saturate((accumulated - 2.0f) / 6.0f);
	float minimumRayFraction = clamp(AdaptiveRayMinimum, 0.35f, 1.0f);
	rays = max(2u, (uint)floor(lerp((float)rays,
		(float)rays * minimumRayFraction, historyConfidence) + 0.5f));
#endif
	uint steps = clamp(NumSteps, 4u, 24u);
	// AO and diffuse GI share rays, but AO must retain its configured reach
	// when GI is disabled or its radius is shorter than the AO radius.
	float maxDistance = clamp(max(GIRadius, AORadius) * EffectRadius, 48.0f, 1024.0f);
	float aoDistance = max(AORadius * EffectRadius, 1.0f);
	float3 indirect = 0.0f;
	float visible = 0.0f;
	float3 bent = traceN;
	float occlusionSum = 0.0f;
	float contactDepthAccum = 0.0f;
	float contactDepthWeight = 0.0f;
	float3 receiverWS = 0.0f;
	float3 cameraWS = 0.0f;
	float3 receiverNormalWS = 0.0f;
	if (WorldCacheEnabled != 0u) {
		receiverWS = TraceViewToWorldPosition(P, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
		cameraWS = ViewToWorldPosition(0.0f, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
		receiverNormalWS = WorldCacheSafeNormalize(worldN, float3(0.0f, 0.0f, 1.0f));
	}
	[loop] for (uint r = 0u; r < rays; ++r) {
		float angle = 6.2831853f * (float(r) + noise.x) / float(rays);
		float radial = sqrt(saturate((float(r) + noise.y) / float(rays)));
		float3 tangentDir = T * cos(angle) + B * sin(angle);
		float3 D = normalize(traceN * sqrt(max(1.0f - radial * radial, 0.0f)) + tangentDir * radial);
		bool hit = false;
		float3 hitUVZ = 0.0f;
		float previousDistance = 0.0f;
		float previousDepthDelta = 0.0f;
		bool hasPreviousDepth = false;
		[loop] for (uint s = 1u; s <= steps; ++s) {
			// Concentrate samples near the receiver without extending past the
			// requested radius. The old first step skipped narrow crevices.
			float fraction = (float(s) - 0.5f + 0.5f * noise.y) / float(steps);
			float distance = maxDistance * fraction * fraction;
			float3 Q = P + D * distance;
			if (Q.z <= FP_Z) break;
			float2 qUV = ViewToScreenPosition(Q);
			if (any(qUV <= 0.001f) || any(qUV >= 0.999f)) break;
			float sceneZ = srcDepth.SampleLevel(samplerPointClamp, FullFrameTextureUV(qUV), 0);
			if (!(sceneZ > FP_Z) || !isfinite(sceneZ)) continue;
			float delta = Q.z - sceneZ;
			float thickness = max(1.5f, min(Thickness, 32.0f) * 0.25f) + distance * 0.018f;
			// A coarse step can pass through a thin surface and land farther than
			// `thickness` behind it. Refine only a front-to-back depth crossing;
			// this is bounded work (four point samples) and keeps ordinary misses
			// at the configured ray-march cost. It also gives AO/GI the actual
			// visible surface location instead of a point buried behind it.
			bool crossedSurface = hasPreviousDepth && previousDepthDelta < 0.0f && delta >= 0.0f;
			if (crossedSurface || (delta >= 0.0f && delta <= thickness)) {
				float refinedDistance = distance;
				float refinedSceneZ = sceneZ;
				float2 refinedUV = qUV;
				if (crossedSurface) {
					float lo = previousDistance;
					float hi = distance;
					[unroll] for (uint refine = 0u; refine < 4u; ++refine) {
						float mid = (lo + hi) * 0.5f;
						float3 midPosition = P + D * mid;
						if (midPosition.z <= FP_Z) { lo = mid; continue; }
						float2 midUV = ViewToScreenPosition(midPosition);
						if (any(midUV <= 0.001f) || any(midUV >= 0.999f)) { lo = mid; continue; }
						float midSceneZ = srcDepth.SampleLevel(samplerPointClamp, FullFrameTextureUV(midUV), 0);
						if (!(midSceneZ > FP_Z) || !isfinite(midSceneZ)) { lo = mid; continue; }
						if (midPosition.z - midSceneZ >= 0.0f) {
							hi = mid;
							refinedSceneZ = midSceneZ;
							refinedUV = midUV;
						} else {
							lo = mid;
						}
					}
					refinedDistance = hi;
				}
				hit = true;
				hitUVZ = float3(refinedUV, refinedSceneZ);
				break;
			}
			previousDistance = distance;
			previousDepthDelta = delta;
			hasPreviousDepth = true;
		}
		if (hit) {
			// Visibility depends on the depth blocker, not whether its shading
			// normal makes it a valid diffuse emitter. Keep AO/contact depth when
			// the GI facing test below rejects thin or normal-mapped geometry.
			float3 hitDelta = ScreenToViewPosition(hitUVZ.xy, hitUVZ.z) - P;
			float hitDistanceSq = max(dot(hitDelta, hitDelta), 1e-6f);
			float hitDistance = sqrt(hitDistanceSq);
			float inverseHitDistance = rsqrt(hitDistanceSq);
			// A depth crossing on the receiver's own plane is not an ambient
			// blocker. Reject these grazing self-hits before they darken broad
			// walls and ceilings; real nearby blockers rise into the hemisphere.
			float receiverHorizon = saturate(dot(traceN, hitDelta * inverseHitDistance));
			float blockerConfidence = smoothstep(0.03f, 0.25f, receiverHorizon);
			occlusionSum += (1.0f - smoothstep(0.0f, aoDistance, hitDistance)) * blockerConfidence;
			// A grazing hit on the receiver's own plane is still visible to the
			// environment. Keep bent/specular visibility consistent with AO.
			visible += 1.0f - blockerConfidence;
			bent += D * (1.0f - blockerConfidence);
			if (ContactDepthEnabled != 0u && ContactDepthRadius > 0.0f) {
				float radialWeight = saturate(1.0f - hitDistance / max(ContactDepthRadius, 1.0f));
				radialWeight *= radialWeight;
				float horizon = saturate((receiverHorizon - ContactDepthBias) /
					max(1.0f - ContactDepthBias, 0.01f));
				contactDepthAccum += horizon * radialWeight;
				contactDepthWeight += radialWeight;
			}
			// Depth-only hits include back-facing and grazing surfaces, which are
			// poor irradiance emitters and can create bright colored flashes at
			// thin silhouettes. Require the sampled surface to face the receiver.
			float2 hitNormalEncoded = srcNormalRoughness.SampleLevel(
				samplerPointClamp, FullFrameTextureUV(hitUVZ.xy), 0).xy;
			float3 hitNormal = ToTraceView(GBuffer::DecodeNormal(hitNormalEncoded));
			float sourceFacing = saturate(dot(hitNormal, -D));
			if (sourceFacing <= 1e-3f)
				continue;

			float3 L = srcRadiance.SampleLevel(samplerLinearClamp, InternalFrameTextureUV(hitUVZ.xy), 0);
			L = filterInf(filterNaN(L));
			L = ClampFireflies(max(L, 0.0f), RadianceFireflyClamp);
			// A linear radius fade loses half the bounce halfway through the
			// trace. Keep nearby and mid-range transfers useful, while still
			// reaching zero at the configured screen-space limit.
			float radiusFraction = saturate(hitDistance / maxDistance);
			float falloff = 1.0f - radiusFraction * radiusFraction;
			indirect += L * falloff * sourceFacing;
		} else {
			visible += 1.0f;
			bent += D;
			if (WorldCacheEnabled != 0u && WorldCacheStrength > 0.0f &&
				r < min(rays, clamp(WorldCacheSampleCount, 1u, 8u))) {
				float3 worldD = WorldCacheSafeNormalize(
					TraceViewToWorldVector(D, FrameBuffer::CameraViewInverse), receiverNormalWS);
				float cellSize = WorldCacheTraceCellSize(receiverWS, cameraWS);
				float3 queryWS = receiverWS + receiverNormalWS * (cellSize * 0.55f);
				float confidence = 0.0f;
				float3 cacheRadiance = 0.0f;
				[loop] for (uint cacheStep = 0u; cacheStep < min(WorldCacheTraceSteps, 3u); ++cacheStep) {
					queryWS += worldD * (cellSize * 1.5f);
					if (length(queryWS - receiverWS) >= WorldCacheRadius) break;
					cacheRadiance = ReadSSGIWorldCache(queryWS, receiverWS, receiverNormalWS,
						worldD, cameraWS, confidence);
					if (confidence > 0.0f) break;
				}
				float distanceFade = saturate(1.0f - length(queryWS - receiverWS) /
					max(WorldCacheRadius, cellSize));
				// The cache is a secondary source for rays that left the visible
				// frame. Keep confidence, distance and firefly guards, but let that
				// stable world-space estimate contribute enough to fill large rooms.
				indirect += ClampFireflies(cacheRadiance, RadianceFireflyClamp) *
					confidence * distanceFade * WorldCacheStrength * 0.50f;
			}
		}
	}
	visible /= float(rays);
	float occlusion = occlusionSum / float(rays);
	// Cosine-weighted rays include misses in the estimator. Dividing by hits
	// instead makes a single surviving hit as bright as a fully occluded room.
	float3 rgb = indirect / float(rays) * max(GIStrength, 0.0f);
#ifndef GI
	rgb = 0.0f;
#endif
	rgb *= saturate((DepthFadeRange.y - z) * DepthFadeScaleConst);
	float3 yc = Color::RGBToYCoCg(rgb);
	float blend = rcp(max(accumulated + 1.0f, 1.0f));
	float4 oldY = srcPrevY.Load(int3(tid,0));
	float2 oldC = srcPrevCoCg.Load(int3(tid,0));
	float oldAO = srcPrevAO.Load(int3(tid,0));
	float oldLuminance = max(oldY.x * 0.8862269f, 0.0f);
	float newLuminance = max(yc.x, 0.0f);
	float relativeChange = abs(newLuminance - oldLuminance) /
		max(max(newLuminance, oldLuminance), 0.05f);
	float reactive = smoothstep(0.20f, 0.75f, relativeChange);
	blend = max(blend, lerp(0.08f, 0.35f, reactive));
#ifndef TEMPORAL_DENOISER
	blend = 1.0f;
#endif
	float3 oldRGB = Color::YCoCgToRGB(float3(oldY.x * 0.8862269f, oldC));
	oldRGB = filterInf(filterNaN(oldRGB));
	rgb = lerp(max(oldRGB,0.0f), rgb, blend);
	float depthFade = saturate((z - DepthFadeRange.x) * DepthFadeScaleConst);
	float targetAO = saturate(occlusion * max(AOPower, 0.0f));
	if (ContactDepthEnabled != 0u && contactDepthWeight > 1e-4f) {
		// Misses belong in the visibility estimator. Dividing by hit weight
		// made one isolated near hit as dark as a fully enclosed receiver.
		float contactDepth = saturate(contactDepthAccum / float(rays)) *
			saturate(ContactDepthStrength) * (1.0f - depthFade);
		// Combine independent occlusion estimates as visibility products, so
		// overlapping screen-space and near-field evidence cannot add past 1.
		targetAO = 1.0f - (1.0f - targetAO) * (1.0f - contactDepth);
	}
	// Filter the combined AO target together so the near-field term shares the
	// geometry-validated temporal history instead of flickering independently.
	float aoChange = smoothstep(0.10f, 0.40f, abs(targetAO - oldAO));
	float ao = lerp(oldAO, targetAO, max(max(blend, 0.12f), 0.40f * aoChange));
	// AO is a diffuse visibility cue. Retain a small amount of indirect visibility
	// even when high user strength and multiple occluders overlap; this prevents
	// broad floors and walls from collapsing to black.
	ao = min(saturate(ao), 0.72f);
	float3 stableBent = normalize(bent);
	float3 worldBent = normalize(TraceViewToWorldVector(stableBent, FrameBuffer::CameraViewInverse));
#if defined(TEMPORAL_DENOISER)
	// The composite uses this field to occlude environment specular. Publishing
	// raw 2-8 ray visibility while filtering only diffuse GI/AO makes reflections
	// flicker between lit and black. Reuse the geometry-validated remapped history.
	float4 oldBent = srcPrevBentVisibility.Load(int3(tid, 0));
	if (accumulated > 0.0f && all(isfinite(oldBent)) && oldBent.w > 0.0f) {
		float3 oldDirection = GBuffer::DecodeNormal(oldBent.xy);
		if (dot(oldDirection, worldN) > 0.0f) {
			float visibilityBlend = max(blend, 0.12f);
			float3 filteredBent = lerp(oldDirection, worldBent, visibilityBlend);
			worldBent = filteredBent * rsqrt(max(dot(filteredBent, filteredBent), 1e-6f));
			visible = lerp(saturate(oldBent.z), visible, visibilityBlend);
		}
	}
#endif
	float2 encodedBent = GBuffer::EncodeNormal(worldBent);
	float3 finalY = Color::RGBToYCoCg(max(filterInf(filterNaN(rgb)), 0.0f));
	// GISaturation was previously not consumed by this SSGI path. Keep luminance
	// and the established YCoCg/SH ABI unchanged while controlling only chroma.
	finalY.yz *= clamp(GISaturation, 0.0f, 2.0f);
	if ((DebugView >= 5u && DebugView <= 7u) || (DebugView >= 14u && DebugView <= 19u)) {
		float3 debugColor = InspectSSGIWorldCache(receiverWS, receiverNormalWS, cameraWS);
		finalY = Color::RGBToYCoCg(max(filterInf(filterNaN(debugColor)), 0.0f));
	}
	outY[tid] = float4(finalY.x / 0.88622692545f, 0, 0, 0);
	outCoCg[tid] = finalY.yz;
	outAO[tid] = saturate(ao);
#if defined(PIXL_TEST_ADAPTIVE_RAYS)
	outBentVisibility[tid] = float4(encodedBent.xy, visible, (float)rays / 8.0f);
#else
	outBentVisibility[tid] = float4(encodedBent.xy, visible, 1.0f);
#endif
}
