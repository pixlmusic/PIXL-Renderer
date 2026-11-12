
namespace ContactShadows
{
	float ContactSmootherStep(float edge0, float edge1, float value)
	{
		float t = saturate((value - edge0) / max(edge1 - edge0, 1.0e-5f));
		return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
	}

	Texture2D<unorm float> ContactShadowsTexture : register(t45);

	float GetScreenSpaceShadow(float3 screenPosition, float2 uv, float noise)
	{
		uint width, height;
		ContactShadowsTexture.GetDimensions(width, height);
		// A missing or failed shadow allocation must leave native directional
		// lighting intact. Loading a null SRV would otherwise return zero.
		if (width == 0u || height == 0u)
			return 1.0f;
		// The field is allocated at render resolution (not reconstructed output).
		// Reject invalid/reprojected UVs instead of clamping them onto edge pixels.
		if (!all(isfinite(uv)) || any(uv < 0.0f) || any(uv > 1.0f))
			return 1.0f;
		int2 p = int2(saturate(uv) * float2(width, height));
		int2 maxP = int2(max(width, 1u), max(height, 1u)) - 1;
		p = clamp(p, 0, maxP);

		float c = ContactShadowsTexture.Load(int3(p, 0)).x;
		// The SRV is unorm, so this sample is guaranteed finite by the format.
		c = saturate(c);
		// The producer already applies the user-configured distance fade;
		// the consumer must not apply another fixed range fade.
		const float viewDepth = abs(SharedData::GetScreenDepth(screenPosition.z));
		if (!isfinite(viewDepth) || viewDepth <= 0.0f)
			return 1.0f;
		const float2 texelSize = rcp(float2(width, height));
		const float l = ContactShadowsTexture.Load(int3(clamp(p + int2(-1, 0), 0, maxP), 0)).x;
		const float r = ContactShadowsTexture.Load(int3(clamp(p + int2( 1, 0), 0, maxP), 0)).x;
		const float u = ContactShadowsTexture.Load(int3(clamp(p + int2( 0,-1), 0, maxP), 0)).x;
		const float d = ContactShadowsTexture.Load(int3(clamp(p + int2( 0, 1), 0, maxP), 0)).x;
		const float4 neighbours = saturate(float4(l, r, u, d));
		const float contrast = max(max(abs(l - c), abs(r - c)), max(abs(u - c), abs(d - c)));
		if (contrast < 0.035f)
			return c;

		const float depthL = abs(SharedData::GetScreenDepth(saturate(uv + float2(-texelSize.x, 0.0f))));
		const float depthR = abs(SharedData::GetScreenDepth(saturate(uv + float2( texelSize.x, 0.0f))));
		const float depthU = abs(SharedData::GetScreenDepth(saturate(uv + float2(0.0f, -texelSize.y))));
		const float depthD = abs(SharedData::GetScreenDepth(saturate(uv + float2(0.0f,  texelSize.y))));
		if (!all(isfinite(float4(depthL, depthR, depthU, depthD))))
			return c;
		const float4 depthDelta = abs(float4(depthL, depthR, depthU, depthD) - viewDepth);
		const float depthSigma = max(viewDepth * 0.0035f, 5.0f);
		const float4 bilateralWeight = exp2(-depthDelta / depthSigma * 2.25f);
		const float filtered =
			(c * 6.0f + dot(neighbours, bilateralWeight)) /
			max(6.0f + dot(bilateralWeight, 1.0f.xxxx), 1.0e-4f);
		const float neighborhoodDepthSpan =
			max(max(depthDelta.x, depthDelta.y), max(depthDelta.z, depthDelta.w)) /
			max(viewDepth, 1.0f);
		// Depth-neighbourhood rejection avoids divergent ddx/ddy operations in
		// material-dependent lighting branches.
		const float discontinuity = ContactSmootherStep(0.0025f, 0.022f,
			neighborhoodDepthSpan);
		const float blur = saturate(contrast * 4.0f) * 0.55f * (1.0f - discontinuity);
		// Subtle edge-only smoothing, preserving the dark receiver contact core.
		return max(saturate(lerp(c, filtered, blur)), 0.045f);
	}

	/**
	 * Short, stable screen-space contact ray for a local light. This complements a
	 * shadow map only over its least reliable near-surface range; it is deliberately
	 * bounded to six taps and is called only for the most influential local lights.
	 */
	float GetLocalContactShadow(
		float3 positionWS,
		float3 normalWS,
		float3 lightDirectionWS,
		float lightDistance,
		float minimumRayLength)
	{
		// An invalid light or uninitialized setting should not manufacture
		// self-shadowing or non-finite UVs in the forward lighting pass.
		if (!all(isfinite(positionWS)) || !all(isfinite(normalWS)) ||
			!all(isfinite(lightDirectionWS)) || !isfinite(lightDistance) ||
			lightDistance <= 0.0f ||
			dot(normalWS, normalWS) < 1.0e-5f ||
			dot(lightDirectionWS, lightDirectionWS) < 1.0e-5f)
			return 1.0f;
		if (!isfinite(SharedData::materialForgeSettings.LocalContactShadowStrength) ||
			!isfinite(SharedData::materialForgeSettings.LocalContactShadowLength))
			return 1.0f;
		float strength = saturate(SharedData::materialForgeSettings.LocalContactShadowStrength);
		// This is a contact-detail pass, not a replacement for the local shadow
		// map. Long screen-space rays cross unrelated depth layers and change their
		// evidence as the camera turns, which is the source of broad indoor blotches
		// and lights that appear to switch off. Keep both the saved setting and the
		// held-emitter minimum inside a strict near-field envelope.
		float configuredRayLength = clamp(SharedData::materialForgeSettings.LocalContactShadowLength, 8.0f, 48.0f);
		float rayLength = min(lightDistance, clamp(max(configuredRayLength, minimumRayLength), 8.0f, 48.0f));
		float NdotL = saturate(dot(normalWS, lightDirectionWS));
		if (strength <= 0.0f || rayLength <= 4.0f || NdotL <= 0.02f)
			return 1.0f;

		// Broad flat-surface blotches happen when the very first hit is trusted too
		// much and the receiver ray begins almost inside the surface. Bias along both
		// the normal and the light direction, then require some multi-step coherence
		// before a hard contact is allowed to form.
		float normalBias = lerp(2.5f, 1.25f, NdotL);
		float lightBias = lerp(1.0f, 0.35f, NdotL);
		float3 originWS = positionWS + normalWS * normalBias + lightDirectionWS * lightBias;
		float3 originVS = FrameBuffer::WorldToView(originWS);
		float viewDistance = abs(originVS.z);
		float distanceConfidence =
			1.0f - ContactSmootherStep(2048.0f, 5120.0f, viewDistance);
		strength *= distanceConfidence;
		if (strength <= 1e-3f)
			return 1.0f;
		float3 rayDirectionVS = normalize(FrameBuffer::WorldToView(lightDirectionWS, false));
		uint stepCount = 6u;
		float invStepCount = rcp((float)stepCount);
		float stepStride = rayLength * invStepCount;
		float3 rayStepVS = rayDirectionVS * stepStride;
		float3 rayPositionVS = originVS + rayStepVS * 0.45f;

		float accumOcclusion = 0.0f;
		float accumWeight = 0.0f;
		float accumHitPosition = 0.0f;
		uint consecutiveDepthHits = 0u;
		uint longestDepthHitRun = 0u;

		[unroll] for (uint stepIndex = 0; stepIndex < 6; ++stepIndex)
		{
			if (stepIndex >= stepCount)
				break;

			float2 rayUV = FrameBuffer::ViewToUV(rayPositionVS);
			float rayT = ((float)stepIndex + 0.45f) * invStepCount;
			float weight = lerp(1.0f, 0.45f, rayT);
			// Keep off-screen taps in the confidence denominator as unoccluded.
			// Dropping them renormalized the remaining hits and made shadows flash
			// at screen edges and during camera motion as taps crossed the viewport.
			accumWeight += weight;
			if (all(isfinite(rayUV)) && rayPositionVS.z < -1.0f &&
				!FrameBuffer::IsOutsideFrame(rayUV)) {
				float sceneDepth = SharedData::GetScreenDepth(rayUV);
				// Skyrim's view space is right-handed (visible geometry has negative
				// Z), while GetScreenDepth returns a positive linear distance.  The
				// old comparison mixed those conventions, so every ray appeared to be
				// in front of the depth surface and local shadows stayed white.  Compare
				// positive distances on both sides; this also stabilizes near-clip rays.
				float rayDepth = -rayPositionVS.z;
				float depthDelta = rayDepth - sceneDepth;

				float baseThickness = max(3.0f, abs(rayPositionVS.z) * 0.0025f);
				float thickness = baseThickness * lerp(0.90f, 1.20f, rayT);

				float enter = ContactSmootherStep(1.0f, 2.0f, depthDelta);
				float leave = 1.0f - ContactSmootherStep(thickness * 0.60f, thickness * 0.90f, depthDelta);
				float hit = isfinite(sceneDepth) && sceneDepth > 0.0f ?
					saturate(enter * leave) : 0.0f;
				if (hit > 0.5f) {
					++consecutiveDepthHits;
					longestDepthHitRun = max(longestDepthHitRun, consecutiveDepthHits);
				} else {
					consecutiveDepthHits = 0u;
				}

				float contact = 1.0f - smoothstep(0.10f, 0.50f, rayT);
				hit *= lerp(0.80f, 1.0f, contact);

				accumOcclusion += hit * weight;
				accumHitPosition += hit * weight * rayT;
			} else
				consecutiveDepthHits = 0u;

			rayPositionVS += rayStepVS;
		}

		float avgOcclusion = accumWeight > 0.0f ? accumOcclusion / accumWeight : 0.0f;
		float hitPosition = accumHitPosition / max(accumOcclusion, 1.0e-4f);
		// A single depth sample is common at moving silhouettes and with TAA
		// jitter. Treat contact rays as evidence only after multiple adjacent
		// steps agree; native/cached shadow maps remain authoritative for mapped
		// lights, so rejecting an isolated screen-space hit avoids visible flashes
		// without removing their real shadow.
		float coverageCoherence = smoothstep(0.24f, 0.48f, avgOcclusion);
		float adjacentHitCoherence = smoothstep(1.0f, 2.0f, (float)longestDepthHitRun);
		float coherence = coverageCoherence * adjacentHitCoherence;
		float contact = 1.0f - smoothstep(0.12f, 0.60f, hitPosition);
		float occlusion = avgOcclusion * coherence;

		// Contact-hardening falloff: only receiver-adjacent blockers reach full
		// strength. Separated blockers become progressively lighter/softer instead
		// of leaving a uniformly dark projected silhouette.
		float separationFade = lerp(0.35f, 1.0f, contact);
		occlusion *= separationFade * smoothstep(0.02f, 0.12f, NdotL);
		occlusion = occlusion * occlusion * (3.0f - 2.0f * occlusion);
		// This screen-space estimate is only a local contact cue for lights with
		// no shadow map. Even coherent depth hits can belong to another surface as
		// the view changes, so never let this approximation erase the light.
		occlusion = min(occlusion, 0.75f);
		return 1.0f - strength * saturate(occlusion);
	}
}
