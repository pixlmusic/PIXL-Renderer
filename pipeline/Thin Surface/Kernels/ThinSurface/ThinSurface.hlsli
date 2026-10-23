namespace ThinSurface
{
	#ifndef USE_PIXL_THIN_TRANSMISSION
	#	define USE_PIXL_THIN_TRANSMISSION 1
	#endif

	namespace MaterialModel
	{
		static const uint Default = 0;  // Use ThinSurfaceSettings
		static const uint RimLight = 1;
		static const uint IsotropicFabric = 2;
		static const uint AnisotropicFabric = 3;
		static const uint Disabled = 4;  // Any value >= 4
	}

	bool IsValidMaterial(uint Material)
	{
		return Material > 0 && Material < MaterialModel::Disabled;
	}

	float SafeAlpha(float alpha)
	{
		return (asuint(alpha) & 0x7f800000u) != 0x7f800000u ? saturate(alpha) : 0.0f;
	}

	float3 SafeDirection(float3 direction, float3 fallback)
	{
		float lengthSq = dot(direction, direction);
		if (any((asuint(direction) & 0x7f800000u) == 0x7f800000u) ||
			(asuint(lengthSq) & 0x7f800000u) == 0x7f800000u || lengthSq < 1.0e-8f)
			return fallback;
		return direction * rsqrt(lengthSq);
	}

	uint GetMaterialModelFromDescriptor(uint Descriptor)
	{
		// TerrainField : 6
		// ExtraFeatureDescriptor : 3
		return (Descriptor >> 6) & 7;
	}

	float GetViewDependentAlphaNaive(float alpha, float3 view, float3 normal)
	{
		alpha = SafeAlpha(alpha);
	#if USE_PIXL_THIN_TRANSMISSION
		view = SafeDirection(view, float3(0.0f, 0.0f, 1.0f));
		normal = SafeDirection(normal, float3(0.0f, 0.0f, 1.0f));
		float opticalDepth = -log(max(1.0f - alpha, 1e-4f));
		float pathLength = min(rcp(max(abs(dot(view, normal)), 0.08f)), 12.0f);
		return 1.0f - exp(-opticalDepth * pathLength);
	#else
		return 1.0 - (1.0 - alpha) * dot(view, normal);
	#endif
	}

	float GetViewDependentAlphaFabric1D(float alpha, float3 view, float3 normal)
	{
		alpha = SafeAlpha(alpha);
	#if USE_PIXL_THIN_TRANSMISSION
		view = SafeDirection(view, float3(0.0f, 0.0f, 1.0f));
		normal = SafeDirection(normal, float3(0.0f, 0.0f, 1.0f));
		float opticalDepth = -log(max(1.0f - alpha, 1e-4f));
		float pathLength = min(rcp(max(abs(dot(view, normal)), 0.08f)), 12.0f);
		return 1.0f - exp(-opticalDepth * pathLength);
	#else
		return alpha / min(1.0, (abs(dot(view, normal)) + 0.001));
	#endif
	}

	float GetViewDependentAlphaFabric2D(float alpha, float3 view, float3x3 tbnTr)
	{
		float3 t = SafeDirection(tbnTr[0], float3(1.0f, 0.0f, 0.0f));
		float3 b = SafeDirection(tbnTr[1], float3(0.0f, 1.0f, 0.0f));
		float3 n = SafeDirection(tbnTr[2], float3(0.0f, 0.0f, 1.0f));
		float3 v = SafeDirection(view, float3(0.0f, 0.0f, 1.0f));
		float a0 = 1.0f - sqrt(1.0f - SafeAlpha(alpha));
	#if USE_PIXL_THIN_TRANSMISSION
		float weaveCoverage = min(max(length(cross(v, t)) + length(cross(v, b)) - a0, 0.0f), 2.0f);
		float pathLength = min(weaveCoverage * rcp(max(abs(dot(v, n)), 0.08f)), 12.0f);
		float opticalDepth = -log(max(1.0f - saturate(a0), 1e-4f));
		return 1.0f - exp(-opticalDepth * pathLength);
	#else
		return a0 * (length(cross(v, t)) + length(cross(v, b))) / (abs(dot(v, n)) + 0.001) - a0 * a0;
	#endif
	}

	float SoftClamp(float alpha, float limit)
	{
		if ((asuint(alpha) & 0x7f800000u) == 0x7f800000u)
			return 0.0f;
		if ((asuint(limit) & 0x7f800000u) == 0x7f800000u)
			return saturate(alpha);
		// soft clamp [alpha,1] and remap the transparency
		limit = max(limit, 1.0e-4f);
		float exponent = clamp(-4.0f * (alpha - limit * 0.5f) / limit, -16.0f, 16.0f);
		alpha = min(alpha, limit / (1.0f + exp(exponent)));
		return saturate(alpha);
	}
}
