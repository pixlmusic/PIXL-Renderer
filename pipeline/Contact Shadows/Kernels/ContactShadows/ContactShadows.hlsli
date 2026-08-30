
namespace ContactShadows
{
	Texture2D<unorm float> ContactShadowsTexture : register(t45);

	float GetScreenSpaceShadow(float3 screenPosition, float2 uv, float noise)
	{
		int2 p = int2(screenPosition.xy + 0.5f);
		uint width, height;
		ContactShadowsTexture.GetDimensions(width, height);
		int2 maxP = int2(max(width, 1u), max(height, 1u)) - 1;
		p = clamp(p, 0, maxP);

		float c = ContactShadowsTexture.Load(int3(p, 0)).x;
		float l = ContactShadowsTexture.Load(int3(clamp(p + int2(-1, 0), 0, maxP), 0)).x;
		float r = ContactShadowsTexture.Load(int3(clamp(p + int2( 1, 0), 0, maxP), 0)).x;
		float u = ContactShadowsTexture.Load(int3(clamp(p + int2( 0,-1), 0, maxP), 0)).x;
		float d = ContactShadowsTexture.Load(int3(clamp(p + int2( 0, 1), 0, maxP), 0)).x;
		float2 texelSize = rcp(float2(max(width, 1u), max(height, 1u)));
		float viewDepth = abs(SharedData::GetScreenDepth(screenPosition.z));
		float depthL = abs(SharedData::GetScreenDepth(saturate(uv + float2(-texelSize.x, 0.0f))));
		float depthR = abs(SharedData::GetScreenDepth(saturate(uv + float2( texelSize.x, 0.0f))));
		float depthU = abs(SharedData::GetScreenDepth(saturate(uv + float2(0.0f, -texelSize.y))));
		float depthD = abs(SharedData::GetScreenDepth(saturate(uv + float2(0.0f,  texelSize.y))));
		float depthSigma = max(viewDepth * 0.0035f, 5.0f);
		float4 bilateralWeight = exp2(-abs(float4(depthL, depthR, depthU, depthD) - viewDepth) / depthSigma * 2.25f);

		// Small depth-bilateral cross filter, applied only where the shadow field
		// has a gradient. Unlike the former unweighted cross, this cannot pull a
		// shadow across an actor, roofline or distant depth discontinuity.
		float filtered =
			(c * 4.0f + dot(float4(l, r, u, d), bilateralWeight)) /
			max(4.0f + dot(bilateralWeight, 1.0f.xxxx), 1.0e-4f);
		float edge = saturate(abs(filtered - c) * 5.0f);
		float depthFootprint =
			max(abs(ddx(viewDepth)), abs(ddy(viewDepth))) /
			max(viewDepth, 1.0f);
		float neighborhoodDepthSpan =
			max(max(abs(depthL - viewDepth), abs(depthR - viewDepth)),
				max(abs(depthU - viewDepth), abs(depthD - viewDepth))) /
			max(viewDepth, 1.0f);
		float discontinuity = smoothstep(
			0.0025f, 0.022f, max(depthFootprint, neighborhoodDepthSpan));
		float blur = edge * edge * 0.72f * (1.0f - discontinuity);
		// Screen-space shadows refine the authoritative shadow map; they must not
		// erase every photon when the ray field becomes temporarily under-resolved.
		float shadow = max(saturate(lerp(c, filtered, blur)), 0.12f);

		// Contact shadows are a near-field detail technique. At long range even a
		// continuous one-pixel receiver has insufficient depth precision to keep
		// receiver/caster ordering stable while the camera moves. Retire the complete
		// screen-space term before that range, with additional early rejection at
		// discontinuities. The ordinary shadow maps remain authoritative there.
		float rangeConfidence = 1.0f - smoothstep(2048.0f, 5120.0f, viewDepth);
		float receiverConfidence =
			rangeConfidence * (1.0f - discontinuity * (1.0f - rangeConfidence));
		return lerp(1.0f, shadow, receiverConfidence);
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
		float strength = saturate(SharedData::materialForgeSettings.LocalContactShadowStrength);
		float configuredRayLength = max(SharedData::materialForgeSettings.LocalContactShadowLength, 0.0f);
		float rayLength = min(lightDistance, max(configuredRayLength, minimumRayLength));
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
			1.0f - smoothstep(2048.0f, 5120.0f, viewDistance);
		strength *= distanceConfidence;
		if (strength <= 1e-3f)
			return 1.0f;
		float3 rayDirectionVS = normalize(FrameBuffer::WorldToView(lightDirectionWS, false));
		uint stepCount = rayLength < 48.0f ? 4u : 6u;
		float invStepCount = rcp((float)stepCount);
		float stepStride = rayLength * invStepCount;
		float3 rayStepVS = rayDirectionVS * stepStride;
		float3 rayPositionVS = originVS + rayStepVS * 0.45f;

		float maxOcclusion = 0.0f;
		float accumOcclusion = 0.0f;
		float accumWeight = 0.0f;
		float nearestHit = 1.0f;

		[unroll] for (uint stepIndex = 0; stepIndex < 6; ++stepIndex)
		{
			if (stepIndex >= stepCount)
				break;

			float2 rayUV = FrameBuffer::ViewToUV(rayPositionVS);
			if (!FrameBuffer::IsOutsideFrame(rayUV)) {
				float sceneDepth = SharedData::GetScreenDepth(rayUV);
				float depthDelta = rayPositionVS.z - sceneDepth;

				float rayT = ((float)stepIndex + 0.45f) * invStepCount;
				float baseThickness = max(3.0f, abs(rayPositionVS.z) * 0.0025f);
				float thickness = baseThickness * lerp(0.90f, 1.20f, rayT);

				float enter = smoothstep(1.0f, 2.0f, depthDelta);
				float leave = 1.0f - smoothstep(thickness * 0.60f, thickness * 0.90f, depthDelta);
				float hit = saturate(enter * leave);

				float contact = 1.0f - smoothstep(0.10f, 0.50f, rayT);
				hit *= lerp(0.80f, 1.0f, contact);

				float weight = lerp(1.0f, 0.45f, rayT);
				accumOcclusion += hit * weight;
				accumWeight += weight;
				maxOcclusion = max(maxOcclusion, hit);
				if (hit > 0.05f)
					nearestHit = min(nearestHit, rayT);

				if (maxOcclusion > 0.995f && stepIndex > 1u)
					break;
			}

			rayPositionVS += rayStepVS;
		}

		float avgOcclusion = accumWeight > 0.0f ? accumOcclusion / accumWeight : 0.0f;
		float support = smoothstep(0.10f, 0.40f, avgOcclusion);
		float contact = 1.0f - smoothstep(0.08f, 0.35f, nearestHit);
		float occlusion = lerp(avgOcclusion, maxOcclusion, support * contact);

		// Contact-hardening falloff: only receiver-adjacent blockers reach full
		// strength. Separated blockers become progressively lighter/softer instead
		// of leaving a uniformly dark projected silhouette.
		float separationFade = lerp(0.42f, 1.0f, contact);
		occlusion *= separationFade * smoothstep(0.02f, 0.12f, NdotL);
		occlusion = occlusion * occlusion * (3.0f - 2.0f * occlusion);
		return 1.0f - strength * saturate(occlusion);
	}
}
