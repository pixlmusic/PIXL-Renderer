cbuffer MicroclimateFieldCB : register(b0)
{
	float4 FieldOriginExtent;       // xy = output origin, z = extent, w = dt
	float4 PreviousOriginWind;      // xy = previous origin, zw = wind velocity
	float4 BaseWeather;             // humidity, fog, precipitation, cloud potential
	float4 EmitterPosition;          // xy = absolute position, z = radius, w = enabled
	float4 EmitterWeather;           // humidity, fog, precipitation, storm potential
	float4 FieldControls;            // enabled, debug view, previous-field decay, reserved
	float4 ExtendedControls;         // approximate lowland fog influence
};

Texture2D<float4> PreviousField : register(t0);
Texture2D<float2> PreviousStormField : register(t1);
SamplerState FieldSampler : register(s0);
RWTexture2D<float4> NextField : register(u0);
RWTexture2D<float2> NextStormField : register(u1);

float HashCell(float2 cell)
{
	float3 p = frac(float3(cell.x, cell.y, cell.x) * 0.1031f);
	p += dot(p, p.yzx + 33.33f);
	return frac((p.x + p.y) * p.z);
}

float FrontNoise(float2 worldXY)
{
	// World positions are Skyrim game units. 420k units is about six
	// kilometres, which yields regional cells instead of checkerboard weather.
	float2 grid = worldXY / 420000.0f;
	float2 cell = floor(grid);
	float2 f = frac(grid);
	f = f * f * (3.0f - 2.0f * f);
	float a = HashCell(cell);
	float b = HashCell(cell + float2(1.0f, 0.0f));
	float c = HashCell(cell + float2(0.0f, 1.0f));
	float d = HashCell(cell + 1.0f);
	return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float RegionalNoise(float2 worldXY)
{
	float broad = FrontNoise(worldXY);
	float detail = FrontNoise(worldXY + float2(17321.0f, -29117.0f)) ;
	float fine = FrontNoise(worldXY * 2.7f + float2(-3811.0f, 9127.0f));
	return saturate(broad * 0.62f + detail * 0.26f + fine * 0.12f);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint width, height;
	NextField.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	float2 uv = (float2(dispatchID.xy) + 0.5f) / float2(width, height);
	float2 worldXY = FieldOriginExtent.xy + uv * FieldOriginExtent.z;
	float2 previousWorldXY = worldXY - PreviousOriginWind.zw * max(FieldOriginExtent.w, 0.0f);
	float2 previousUV = (previousWorldXY - PreviousOriginWind.xy) / max(FieldOriginExtent.z, 1.0f);
	float4 oldValue = 0.0f.xxxx;
	float2 oldStorm = 0.0f.xx;
	if (all(previousUV >= 0.0f) && all(previousUV <= 1.0f))
	{
		oldValue = PreviousField.SampleLevel(FieldSampler, previousUV, 0.0f);
		oldStorm = PreviousStormField.SampleLevel(FieldSampler, previousUV, 0.0f);
	}

	float2 emitterDelta = worldXY - EmitterPosition.xy;
	float frontNoise = RegionalNoise(worldXY);
	float frontScale = lerp(0.72f, 1.28f, frontNoise);
	float radius = max(EmitterPosition.z * frontScale, 1.0f);
	float radialDistance = length(emitterDelta) / radius;
	float emitterEdge = 1.0f - smoothstep(0.68f, 1.18f, radialDistance);
	float emitterShape = emitterEdge * saturate(1.0f - radialDistance * 0.22f);
	float emitterMask = saturate(EmitterPosition.w) * emitterShape;
	float persistence = saturate(FieldControls.z);
	float weatherStrength = max(BaseWeather.y, max(BaseWeather.z, BaseWeather.w * 0.85f));
	float weatherActive = smoothstep(0.12f, 0.42f, weatherStrength);
	float regionalMask = smoothstep(0.38f, 0.73f, frontNoise);
	float baseInfluence = weatherActive * regionalMask;
	float4 forcing = saturate(BaseWeather * (regionalMask * weatherActive) + EmitterWeather * emitterMask);
	float stormForcing = saturate(EmitterWeather.w * emitterMask + BaseWeather.w * regionalMask * weatherActive * 0.25f);
	float influenceForcing = max(baseInfluence, emitterMask);
	// Global TESWeather is converted to broad world-anchored cells. Outside a
	// cell, consumers retain Skyrim's base weather; a clear transition decays the
	// previous regional field instead of leaving stale clouds behind.
	float4 value = max(oldValue * persistence, forcing);
	value = lerp(value, forcing, saturate(FieldOriginExtent.w * 0.22f));
	NextField[dispatchID.xy] = saturate(value);
	float2 stormValue = max(oldStorm * persistence, float2(stormForcing, influenceForcing));
	NextStormField[dispatchID.xy] = saturate(lerp(stormValue, float2(stormForcing, influenceForcing), saturate(FieldOriginExtent.w * 0.22f)));
}
