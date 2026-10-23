#ifndef PIXL_MATERIAL_SURFACE_HLSLI
#define PIXL_MATERIAL_SURFACE_HLSLI

namespace MaterialSurface
{
	float HashCell(int3 cell)
	{
		uint3 p = asuint(cell);
		uint h = p.x * 1597334677u ^ p.y * 3812015801u ^ p.z * 2798796415u;
		h ^= h >> 16; h *= 2246822519u; h ^= h >> 13;
		return float(h & 0x00ffffffu) / 16777215.0f;
	}

	// Continuous 3D value noise with its analytic gradient. No UV projection,
	// finite-difference texture fetches, frame seed or camera-relative phase.
	float4 NoiseGradient(float3 position)
	{
		int3 cell = int3(floor(position));
		float3 f = frac(position);
		float3 u = f * f * (3 - 2 * f);
		float3 du = 6 * f * (1 - f);
		float a = HashCell(cell), b = HashCell(cell + int3(1,0,0));
		float c = HashCell(cell + int3(0,1,0)), d = HashCell(cell + int3(1,1,0));
		float e = HashCell(cell + int3(0,0,1)), g = HashCell(cell + int3(1,0,1));
		float h = HashCell(cell + int3(0,1,1)), k = HashCell(cell + int3(1,1,1));
		float4 x = lerp(float4(a,c,e,h), float4(b,d,g,k), u.x);
		float2 y = lerp(x.xz, x.yw, u.y);
		float gx = lerp(lerp(b-a,d-c,u.y),lerp(g-e,k-h,u.y),u.z);
		float gy = lerp(x.y-x.x,x.w-x.z,u.z);
		return float4(float3(gx,gy,y.y-y.x) * du, lerp(y.x,y.y,u.z));
	}

	float SnowMask(float accumulation, float slope, float shelter, float noise, float footprint)
	{
		float amount = saturate(accumulation) * saturate(slope) * saturate(shelter);
		float width = clamp(0.16f + footprint, 0.16f, 0.42f);
		// Accumulation transitions from sparse windblown deposits to a continuous
		// blanket, so fully settled roofs and ground don't remain speckled forever.
		float threshold = lerp(1.08f, -0.18f, amount);
		return smoothstep(threshold - width, threshold + width, noise) * saturate(amount * 8);
	}

	float DepositPattern(float3 position, float footprint)
	{
		// World-anchored macro deposits break up broad cell-like patches. Fade
		// sub-pixel variation toward its mean to avoid crawling during camera motion.
		float macro = NoiseGradient(position * 0.58f + float3(13.7f, 31.1f, 7.3f)).w;
		float fine = NoiseGradient(position * 1.73f + float3(41.3f, 5.9f, 23.1f)).w;
		float visibility = 1.0f - smoothstep(0.12f, 0.85f, max(footprint, 0.0f) * 1.73f);
		return saturate(lerp(macro, 0.5f, 0.24f) + (fine - 0.5f) * (0.24f * visibility));
	}

	float TopFacingDeposit(float normalZ, float minimum, float feather)
	{
		return smoothstep(minimum, min(minimum + max(feather, 0.02f), 1.0f), saturate(normalZ));
	}

	// CPU contract: MaterialSurfaceProfile.h; no added texture or buffer bindings.
	float4 DecodePackedMap(float4 value, uint profile)
	{
		if ((profile & 8u) != 0u)
			return float4(1, 0, 1, 1); // Missing properties: rough dielectric, unoccluded.
		uint layout = profile & 7u;
		if (layout == 1u) return float4(value.g, value.b, value.r, 1); // ORM
		if (layout == 2u) return float4(value.g, value.r, value.b, 1); // MRA
		if (layout == 3u) return float4(value.rgb, 1);               // RMA
		if (layout == 4u) return float4(1 - value.a, value.r, value.g, 1); // MaskMap
		return value; // Existing RMAOS, including authored dielectric reflectance.
	}

	float AutomaticPorosity(float roughness, float metallic)
	{
		// A bounded prior, not a material identity classifier. Explicit profiles win.
		return lerp(0.18f, 0.85f, smoothstep(0.15f, 0.85f, roughness)) * (1 - saturate(metallic));
	}

	float2 WetProperties(uint profile, float roughness, float metallic)
	{
		float pore = AutomaticPorosity(roughness, metallic);
		if ((profile & 0x4000u) == 0u) return float2(1, pore);
		uint poreCode = (profile >> 9) & 31u;
		return float2(float((profile >> 4) & 31u) / 15.0f, poreCode == 31u ? pore : float(poreCode) / 30.0f);
	}

	float FilmCoverage(float waterAmount, float porosity)
	{
		// Pores absorb light rainfall before a continuous film forms; heavy rain
		// and pooled water still converge to full coverage on rough surfaces.
		float water = saturate(waterAmount);
		return water * lerp(1.0f, smoothstep(0.0f, 0.75f, water), saturate(porosity));
	}
}
#endif
