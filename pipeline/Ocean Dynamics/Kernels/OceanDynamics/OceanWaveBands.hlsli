// Safe vertical fallback used when the terrain-fitted cage is unavailable.
// The full fitted ellipsoid is evaluated in Water.hlsl and OceanSpray.hlsl;
// this one-axis reduction keeps water moving gently while that field warms.
float OceanDepthCageRadius(float depth)
{
	// The paper's r3 is zero at the waterline. Let the radius grow with depth,
	// while retaining a conservative ceiling for imperfect world heightmaps.
	depth = isfinite(depth) ? max(depth, 0.0f) : 0.0f;
	return min(depth * 0.10f, 16.0f);
}

float OceanDepthCageBlend(float shoreAmount, float depth)
{
	float positiveDepth = isfinite(depth) ? max(depth, 0.0f) : 0.0f;
	// Keep the cage engaged across the coastal shelf, then restore full FFT height
	// over a broad transition instead of releasing it as soon as shoaling ends.
	float finiteDepth = 1.0f - smoothstep(1200.0f, 4800.0f, positiveDepth);
	finiteDepth = finiteDepth * finiteDepth * (3.0f - 2.0f * finiteDepth);
	return saturate(max(saturate(shoreAmount), finiteDepth));
}

// Preserve the authored beach footprint while wave energy reaches zero depth.
// Orbital XY motion must not drag the boundary away from the wash overlay.
float3 OceanBeachCoverage(float3 wave, float depth)
{
	float positiveDepth = max(depth, 0.0f);
	// A stable contact carrier prevents native triangle edges from retreating
	// through the swash. The thin contact film moves optically (foam/normals),
	// while genuine geometry waves resume progressively outside that band.
	float orbitalFade = smoothstep(128.0f, 512.0f, positiveDepth);
	orbitalFade *= orbitalFade * (3.0f - 2.0f * orbitalFade);
	wave.xy *= orbitalFade;
	float contactHeight = 0.16f * max(positiveDepth - 96.0f, 0.0f) *
		smoothstep(96.0f, 384.0f, positiveDepth);
	// This vertical projection is idempotent: fallback and fitted-cage paths can apply
	// it without multiplying attenuation or changing their transition shape.
	wave.z = clamp(wave.z, -contactHeight, contactHeight);
	return wave;
}

float OceanThreeBandHeight(float shoaledHeight, float shoreAmount, float depth, float wet)
{
	float radius = OceanDepthCageRadius(depth);
	// One-axis form of the radial tanh cage map. Small crests remain nearly linear;
	// both positive and negative displacement approach the local r3 bound.
	shoaledHeight = clamp(shoaledHeight, -65504.0f, 65504.0f);
	float cagedHeight = radius > 1.0e-3f
		? radius * tanh(clamp(shoaledHeight / radius, -8.0f, 8.0f))
		: 0.0f;
	float cageBlend = OceanDepthCageBlend(shoreAmount, depth);
	return OceanBeachCoverage(float3(0.0f, 0.0f,
		lerp(shoaledHeight, cagedHeight, cageBlend) * saturate(wet)), depth).z;
}
