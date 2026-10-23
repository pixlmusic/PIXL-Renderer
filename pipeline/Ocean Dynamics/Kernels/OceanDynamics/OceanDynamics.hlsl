// PIXL Renderer - Tessendorf-style directional spectrum and radix-2 inverse FFT.
// Independently authored for PIXL DX11; informed by published ocean-wave theory
// and the system architecture documented by Crest Water (MIT reference project).
#define OCEAN_CASCADE_COUNT 4
// Skyrim's world coordinates are approximately 70 units per metre. Spectral
// density and gravity are evaluated in SI units; the packed displacement is
// converted back to Skyrim world units for the vertex stage.
static const float OCEAN_WORLD_UNITS_PER_METER = 70.0f;

cbuffer OceanSimulation : register(b0)
{
	float TimeSeconds;
	float WindSpeed;
	float WindDirection;
	float SwellHeight;
	float SwellPeriod;
	float SwellDirection;
	float Choppiness;
	float Resolution;
	float InverseResolution;
	float DeltaSeconds;
	float FoamLifetime;
	float FoamStrength;
	float4 CascadeExtent;
	float WeatherEnergy;
	float CrossSwellDirectionRadians;
	float CrossSwellStrength;
	float DeepRogueWaveStrength;
};

cbuffer OceanFFT : register(b1)
{
	uint FFTResolution;
	uint FFTVertical;
	uint2 FFTPadding;
};

Texture2DArray<float4> Input0 : register(t0);
Texture2DArray<float4> Input1 : register(t1);
Texture2DArray<float4> Input2 : register(t2);
SamplerState OceanSampler : register(s0);
RWTexture2DArray<float4> Output0 : register(u0);
RWTexture2DArray<float4> Output1 : register(u1);

float2 ComplexMultiply(float2 a, float2 b)
{
	return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float Hash(uint2 p, uint slice)
{
	uint h = p.x * 0x8DA6B343u ^ p.y * 0xD8163841u ^ slice * 0xCB1AB31Fu;
	h ^= h >> 13;
	h *= 0x85EBCA6Bu;
	h ^= h >> 16;
	h *= 0xC2B2AE35u;
	h ^= h >> 16;
	return (h & 0x00FFFFFFu) / 16777216.0;
}

[numthreads(8, 8, 1)]
void SpectrumInit(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= (uint)Resolution || id.y >= (uint)Resolution || id.z >= OCEAN_CASCADE_COUNT)
		return;
	float u1 = max(Hash(id.xy, id.z), 1e-6);
	float u2 = Hash(id.yx + 31u, id.z + 11u);
	float radius = sqrt(-2.0 * log(u1));
	float angle = 6.28318530718 * u2;
	Output0[id] = float4(radius * cos(angle), radius * sin(angle), 0, 0);
}

float DirectionalDensity(float2 direction, float2 dominantDirection)
{
	float alignment = saturate(0.5 * (1.0 + dot(direction, dominantDirection)));
	// A normalized cos^4(half-angle) lobe keeps integrated spectral energy
	// constant while concentrating the directional sea state around its fetch.
	return alignment * alignment * (4.0 / (3.0 * 3.14159265359));
}

float OceanSmootherStep(float value)
{
	float t = saturate(value);
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float WindSpectrum(float kLength, float2 kDirection, float windSpeed, float2 windDirection)
{
	const float gravity = 9.81;
	float omega = sqrt(gravity * kLength);
	float peakOmega = 0.877 * gravity / max(windSpeed, 0.5);
	float ratio = peakOmega / max(omega, 1e-4);
	float pm = (0.0081 * gravity * gravity / pow(omega, 5.0)) * exp(-1.25 * pow(ratio, 4.0));
	// JONSWAP peak enhancement preserves the broad Pierson-Moskowitz body but
	// gives local wind waves a readable dominant band instead of uniform chop.
	float sigma = omega <= peakOmega ? 0.07f : 0.09f;
	float peakDistance = (omega - peakOmega) / max(sigma * peakOmega, 1.0e-4f);
	float peakEnhancement = exp(-0.5f * peakDistance * peakDistance);
	// The base PIXL wind energy is calibrated for Skyrim-world units already;
	// normalize the narrow peak enhancement so the added spectral shape does
	// not multiply total sea-state energy or exceed the displacement bounds.
	float jonswapPeak = min(pow(3.3f, 0.5f * peakEnhancement), 2.5f) / 1.4f;
	// Directional synthesis and the two-sided inverse transform otherwise put
	// too much PM energy into the long cascade at PIXL's default 8 m/s wind.
	float densityPerK = pm * 0.30f * gravity / max(2.0 * omega, 1e-4);
	return densityPerK * jonswapPeak * DirectionalDensity(kDirection, windDirection) / max(kLength, 1e-4);
}

float SingleSwellSpectrum(float kLength, float2 kDirection, float period, float height, float2 swellDirection)
{
	float peakOmega = 6.28318530718 / max(period, 2.0);
	float peakK = peakOmega * peakOmega / 9.81;
	float sigmaK = max(peakK * 0.22, 0.0001);
	float normalized = (kLength - peakK) / sigmaK;
	// Height is significant wave height (about four standard deviations), not
	// the variance of every spectral bin. The directional/mirror synthesis adds
	// both traveling components; calibrate energy so 1.15 m swell has an RMS
	// surface elevation near 0.29 m instead of multiple metres.
	float densityPerK = (height * height * 0.02f) * exp(-0.5 * normalized * normalized) /
		(max(2.50662827463 * sigmaK, 1e-5));
	return densityPerK * DirectionalDensity(kDirection, swellDirection) / max(kLength, 1e-4);
}

float SwellSpectrum(float kLength, float2 kDirection, float period, float height,
	float2 swellDirection, float2 crossSwellDirection, float crossSwellStrength,
	float rogueStrength, uint cascade)
{
	float crossWeight = OceanSmootherStep(crossSwellStrength);
	float primary = SingleSwellSpectrum(kLength, kDirection, period, height, swellDirection);
	float cross = SingleSwellSpectrum(kLength, kDirection, period, height, crossSwellDirection);
	float density = lerp(primary, cross, crossWeight);

	// A low-frequency, long-period component creates persistent deep-water wave
	// groups. It shares the same direction blend and FFT phase as the medium swell;
	// depth blending in Water.hlsl fades it out before the surf zone.
	if (cascade == OCEAN_CASCADE_COUNT - 1 && rogueStrength > 0.0f)
	{
		float roguePeriod = clamp(max(period * 1.65f, 14.0f), 14.0f, 24.0f);
		float rogueHeight = height * saturate(rogueStrength) * 0.9f;
		float roguePeakK = pow(6.28318530718f / roguePeriod, 2.0f) / 9.81f;
		float rogueSigmaK = max(roguePeakK * 0.24f, 1.0e-5f);
		float normalized = (kLength - roguePeakK) / rogueSigmaK;
		float rogueDensity = (rogueHeight * rogueHeight * 0.02f) *
			exp(-0.5f * normalized * normalized) /
			(max(2.50662827463f * rogueSigmaK, 1.0e-5f) * max(kLength, 1.0e-4f));
		float primaryLobe = DirectionalDensity(kDirection, swellDirection);
		float crossLobe = DirectionalDensity(kDirection, crossSwellDirection);
		density += rogueDensity * lerp(primaryLobe, crossLobe, crossWeight);
	}
	return density;
}

[numthreads(8, 8, 1)]
void SpectrumUpdate(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= (uint)Resolution || id.y >= (uint)Resolution || id.z >= OCEAN_CASCADE_COUNT)
		return;
	float cascadeExtentMeters = CascadeExtent[id.z] / OCEAN_WORLD_UNITS_PER_METER;
	float2 centered = (float2(id.xy) - Resolution * 0.5) * (6.28318530718 / cascadeExtentMeters);
	float kLength = length(centered);
	if (kLength < 1e-6)
	{
		Output0[id] = 0;
		Output1[id] = 0;
		return;
	}
	uint2 mirror = uint2((uint)Resolution - id.x, (uint)Resolution - id.y) % (uint)Resolution;
	float2 randomA = Input0.Load(int4(id.xy, id.z, 0)).xy;
	float2 randomB = Input0.Load(int4(mirror, id.z, 0)).xy;
	float2 direction = centered / kLength;
	float2 wind = float2(cos(WindDirection), sin(WindDirection));
	float2 swell = float2(cos(SwellDirection), sin(SwellDirection));
	float2 crossSwell = float2(cos(CrossSwellDirectionRadians), sin(CrossSwellDirectionRadians));
	float crossSwellStrength = saturate(CrossSwellStrength);
	float rogueStrength = saturate(DeepRogueWaveStrength);
	float windEnergy = WindSpectrum(kLength, direction, max(WindSpeed, 0.5), wind) * max(WeatherEnergy, 0.0);
	float windEnergyMirror = WindSpectrum(kLength, -direction, max(WindSpeed, 0.5), wind) * max(WeatherEnergy, 0.0);
	float swellEnergy = SwellSpectrum(kLength, direction, SwellPeriod, max(SwellHeight, 0.0),
		swell, crossSwell, crossSwellStrength, rogueStrength, id.z);
	float swellEnergyMirror = SwellSpectrum(kLength, -direction, SwellPeriod, max(SwellHeight, 0.0),
		swell, crossSwell, crossSwellStrength, rogueStrength, id.z);
	// Distribute frequencies into adjacent logarithmic wavelength bands. The
	// 20% overlap removes a hard spectral discontinuity at cascade boundaries.
	float bandMax = 3.14159265359 * Resolution / cascadeExtentMeters;
	float nextExtentMeters = id.z + 1 < OCEAN_CASCADE_COUNT
		? CascadeExtent[id.z + 1] / OCEAN_WORLD_UNITS_PER_METER : 0.0f;
	float bandMin = nextExtentMeters > 0.0f
		? 3.14159265359f * Resolution / nextExtentMeters : 0.0f;
	float lowBandFade = bandMin > 0.0
		? OceanSmootherStep((kLength - bandMin * 0.8f) / max(bandMin * 0.4f, 1.0e-5f))
		: 1.0f;
	float highBandFade = 1.0f - OceanSmootherStep(
		(kLength - bandMax * 0.8f) / max(bandMax * 0.2f, 1.0e-5f));
	float bandWeight = lowBandFade * highBandFade;
	float deltaK = 6.28318530718 / cascadeExtentMeters;
	// Convert the continuous 2D spectrum to the unnormalised inverse-FFT input.
	// Convert spectral density to per-bin amplitude (sqrt(S(k) * dK^2)),
	// then compensate for the unnormalised 2D inverse FFT. Keep the square
	// root around the density only: squaring dK here attenuates the resulting
	// wave height by another full frequency interval and made the ocean nearly
	// flat at Skyrim's world scales.
	float spectralScale = deltaK * Resolution * Resolution;
	float amplitude = sqrt(max((windEnergy + swellEnergy) * bandWeight * 0.5, 0.0)) * spectralScale;
	float amplitudeMirror = sqrt(max((windEnergyMirror + swellEnergyMirror) * bandWeight * 0.5, 0.0)) * spectralScale;
	float2 h0 = randomA * amplitude;
	float2 h0MirrorConjugate = float2(randomB.x, -randomB.y) * amplitudeMirror;
	float omega = sqrt(9.81 * kLength);
	float phase = omega * TimeSeconds;
	float2 positive = ComplexMultiply(h0, float2(cos(phase), sin(phase)));
	float2 negative = ComplexMultiply(h0MirrorConjugate, float2(cos(phase), -sin(phase)));
	float2 h = positive + negative;
	float2 minusIHeight = float2(h.y, -h.x);
	float2 dx = minusIHeight * direction.x * Choppiness;
	float2 dy = minusIHeight * direction.y * Choppiness;
	Output0[id] = float4(h, dx);
	Output1[id] = float4(dy, 0, 0);
}

float2 MultiplyComplex(float2 a, float2 b)
{
	return ComplexMultiply(a, b);
}

float4 ApplyTwiddle(float4 value, float2 twiddle)
{
	return float4(MultiplyComplex(value.xy, twiddle), MultiplyComplex(value.zw, twiddle));
}

groupshared float4 OceanSharedA[128];
groupshared float4 OceanSharedB[128];
groupshared float4 OceanScratchA[128];
groupshared float4 OceanScratchB[128];

[numthreads(128, 1, 1)]
void ComputeFFT(uint3 groupID : SV_GroupID, uint lane : SV_GroupThreadID)
{
	if (groupID.y >= FFTResolution || groupID.z >= OCEAN_CASCADE_COUNT)
		return;
	uint bitCount = firstbithigh(FFTResolution);
	uint reversedLane = reversebits(lane) >> (32u - bitCount);
	uint2 coord = FFTVertical != 0 ? uint2(groupID.y, reversedLane) : uint2(reversedLane, groupID.y);
	OceanSharedA[lane] = Input0.Load(int4(coord, groupID.z, 0));
	OceanSharedB[lane] = Input1.Load(int4(coord, groupID.z, 0));
	GroupMemoryBarrierWithGroupSync();

	[unroll]
	for (uint stage = 0; stage < 7; ++stage)
	{
		uint section = 1u << stage;
		uint span = section << 1;
		uint within = lane % span;
		uint lowerLane = (lane / span) * span + (within % section);
		uint upperLane = lowerLane + section;
		float angle = 6.28318530718 * (within % section) / span;
		float2 twiddle = float2(cos(angle), sin(angle));
		float4 lowerA = OceanSharedA[lowerLane];
		float4 upperA = ApplyTwiddle(OceanSharedA[upperLane], twiddle);
		float4 lowerB = OceanSharedB[lowerLane];
		float4 upperB = ApplyTwiddle(OceanSharedB[upperLane], twiddle);
		OceanScratchA[lane] = within < section ? lowerA + upperA : lowerA - upperA;
		OceanScratchB[lane] = within < section ? lowerB + upperB : lowerB - upperB;
		GroupMemoryBarrierWithGroupSync();
		OceanSharedA[lane] = OceanScratchA[lane];
		OceanSharedB[lane] = OceanScratchB[lane];
		GroupMemoryBarrierWithGroupSync();
	}

	uint2 outputCoord = FFTVertical != 0 ? uint2(groupID.y, lane) : uint2(lane, groupID.y);
	Output0[uint3(outputCoord, groupID.z)] = OceanSharedA[lane];
	Output1[uint3(outputCoord, groupID.z)] = OceanSharedB[lane];
}

[numthreads(8, 8, 1)]
void PackDisplacement(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= (uint)Resolution || id.y >= (uint)Resolution || id.z >= OCEAN_CASCADE_COUNT)
		return;
	float sign = ((id.x + id.y) & 1u) != 0 ? -1.0 : 1.0;
	float4 heightAndX = Input0.Load(int4(id.xy, id.z, 0));
	float2 displacementY = Input1.Load(int4(id.xy, id.z, 0)).xy;
	float scale = 1.0 / (Resolution * Resolution);
	Output0[id] = float4(heightAndX.z * sign, displacementY.x * sign, heightAndX.x * sign, 1.0) *
		float4(scale * OCEAN_WORLD_UNITS_PER_METER, scale * OCEAN_WORLD_UNITS_PER_METER,
			scale * OCEAN_WORLD_UNITS_PER_METER, 1.0);
}

[numthreads(8, 8, 1)]
void UpdateFoam(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= (uint)Resolution || id.y >= (uint)Resolution || id.z >= OCEAN_CASCADE_COUNT)
		return;
	float2 uv = (float2(id.xy) + 0.5f) * InverseResolution;
	float extent = max(CascadeExtent[id.z], 1.0f);
	float2 texel = InverseResolution.xx;
	float4 center = Input1.Load(int4(id.xy, id.z, 0));
	float4 left = Input1.SampleLevel(OceanSampler, float3(uv - float2(texel.x, 0.0f), id.z), 0.0f);
	float4 right = Input1.SampleLevel(OceanSampler, float3(uv + float2(texel.x, 0.0f), id.z), 0.0f);
	float4 down = Input1.SampleLevel(OceanSampler, float3(uv - float2(0.0f, texel.y), id.z), 0.0f);
	float4 up = Input1.SampleLevel(OceanSampler, float3(uv + float2(0.0f, texel.y), id.z), 0.0f);
	float scale = 0.5f * Resolution / extent;
	float dXdx = (right.x - left.x) * scale;
	float dYdx = (right.y - left.y) * scale;
	float dXdy = (up.x - down.x) * scale;
	float dYdy = (up.y - down.y) * scale;
	float jacobian = (1.0f + dXdx) * (1.0f + dYdy) - dXdy * dYdx;
	float compression = smoothstep(0.04f, 0.48f, 1.0f - jacobian);
	float2 slope = float2(right.z - left.z, up.z - down.z) * scale;
	float crestSteepness = smoothstep(0.22f, 0.68f, length(slope));
	float generated = saturate(max(compression, crestSteepness * 0.24f) * max(FoamStrength, 0.0f));

	float2 previousUV = uv;
	float safeDelta = clamp(DeltaSeconds, 0.0f, 0.1f);
	float2 oldDisplacement = Input2.Load(int4(id.xy, id.z, 0)).xy;
	float2 displacementDelta = center.xy - oldDisplacement;
	previousUV -= displacementDelta / extent;
	float history = Input0.SampleLevel(OceanSampler, float3(previousUV, id.z), 0.0f).r;
	float lifetime = max(FoamLifetime, 0.5f);
	history *= exp(-safeDelta / lifetime);
	// Interpret generation as a rate so changing update cadence does not
	// change whitewater coverage. Compression builds over a finite lifetime.
	float generatedThisStep = 1.0f - exp(-generated * safeDelta * 6.0f);
	float foam = saturate(generatedThisStep + history * (1.0f - generatedThisStep));
	Output0[id] = float4(foam, 0.0f, 0.0f, 1.0f);
}
