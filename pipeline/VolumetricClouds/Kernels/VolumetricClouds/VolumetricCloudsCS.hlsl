// PIXL Renderer - independent DX11 volumetric cloud implementation.
// Enviro 3 informed the feature audit only; this shader contains no Enviro code or assets.

#include "Common/SharedData.hlsli"

cbuffer CloudSettings : register(b13)
{
	float4 RenderSize;          // low width, low height, inverse width, inverse height
	float4 FullSize;            // full width, full height, inverse width, inverse height
	float4 Layer;               // base, top, maximum distance, curvature radius
	float4 Weather;             // coverage, density, type, precipitation
	float4 WeatherSecondary;    // storm, fog, transition, sun visibility
	float4 WindTime;            // wind velocity xy, time, delta time
	float4 CloudMotion;         // wind velocity xy, frame delta, reserved
	float4 WeatherMapData;      // origin xy, extent, inverse extent
	float4 PreviousWeatherMap;  // previous origin xy, extent, history valid
	float4 Shape;               // base scale, detail scale, erosion, turbulence
	float4 Lighting;            // extinction, scattering, ambient, powder
	float4 Phase;               // forward g, backward g, silver, multiple scattering
	float4 Temporal;            // history weight, valid, frame index, depth rejection
	float4 Composite;           // shadow strength, shadow distance, vanilla visibility, debug
	uint4 Quality;              // primary steps, light steps, weather valid, update weather
}

SamplerState LinearClamp : register(s0);

static const float PI = 3.14159265359f;

float SaturateFinite(float value)
{
	return isfinite(value) ? saturate(value) : 0.0f;
}

uint HashUint(uint value)
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

float Hash31(int3 p)
{
	uint3 q = asuint(p);
	return (HashUint(q.x ^ HashUint(q.y ^ HashUint(q.z))) & 0x00ffffffu) / 16777215.0f;
}

float Hash21(int2 p)
{
	uint2 q = asuint(p);
	return (HashUint(q.x ^ HashUint(q.y)) & 0x00ffffffu) / 16777215.0f;
}

float Noise3(float3 p)
{
	int3 cell = (int3)floor(p);
	float3 f = frac(p);
	f = f * f * (3.0f - 2.0f * f);
	float n000 = Hash31(cell + int3(0, 0, 0));
	float n100 = Hash31(cell + int3(1, 0, 0));
	float n010 = Hash31(cell + int3(0, 1, 0));
	float n110 = Hash31(cell + int3(1, 1, 0));
	float n001 = Hash31(cell + int3(0, 0, 1));
	float n101 = Hash31(cell + int3(1, 0, 1));
	float n011 = Hash31(cell + int3(0, 1, 1));
	float n111 = Hash31(cell + int3(1, 1, 1));
	return lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y),
		lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
}

float Fbm3(float3 p)
{
	float value = Noise3(p) * 0.57f;
	value += Noise3(p * 2.03f + 17.13f) * 0.29f;
	value += Noise3(p * 4.11f - 9.37f) * 0.14f;
	return value;
}

float Noise2(float2 p)
{
	int2 cell = (int2)floor(p);
	float2 f = frac(p);
	f = f * f * (3.0f - 2.0f * f);
	return lerp(lerp(Hash21(cell), Hash21(cell + int2(1, 0)), f.x),
		lerp(Hash21(cell + int2(0, 1)), Hash21(cell + int2(1, 1)), f.x), f.y);
}

float Fbm2(float2 p)
{
	float value = Noise2(p) * 0.52f;
	value += Noise2(p * 2.07f + 31.0f) * 0.30f;
	value += Noise2(p * 4.19f - 17.0f) * 0.18f;
	return value;
}

float2 WeatherUV(float2 absoluteXY, float4 mapData)
{
	return (absoluteXY - mapData.xy) * mapData.w + 0.5f;
}

Texture2D<float4> PreviousWeather : register(t0);
RWTexture2D<float4> WeatherOutput : register(u0);

[numthreads(8, 8, 1)]
void WeatherMapCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint2 pixel = dispatchThreadID.xy;
	if (any(pixel >= uint2(512, 512)))
		return;
	float2 uv = (float2(pixel) + 0.5f) / 512.0f;
	float2 absoluteXY = WeatherMapData.xy + (uv - 0.5f) * WeatherMapData.z;
	float2 frontPosition = absoluteXY / max(WeatherMapData.z * 0.33f, 1.0f);
	float slowTime = WindTime.z * 0.0035f;
	float front = Fbm2(frontPosition + float2(slowTime, -slowTime * 0.61f));
	float cells = Fbm2(frontPosition * 2.73f + float2(-slowTime * 0.37f, slowTime * 0.53f));
	float stormCell = smoothstep(0.47f, 0.82f, Fbm2(frontPosition * 0.71f + 83.0f));
	float breakup = lerp(front, cells, 0.38f);
	float4 target;
	// Keep weather-map modulation proportional to authored global coverage. Without
	// this gate, zero-coverage clear weather still produced isolated cloud patches.
	target.x = Weather.x <= 1.0e-4f ? 0.0f :
		saturate(Weather.x + (breakup - 0.5f) * lerp(0.72f, 0.24f, Weather.x));
	target.y = saturate(Weather.z + (cells - 0.5f) * 0.42f + stormCell * WeatherSecondary.x * 0.30f);
	target.z = saturate(Weather.w * (0.52f + stormCell * 0.58f) + WeatherSecondary.x * stormCell * 0.46f);
	target.w = saturate(Shape.w * (0.30f + cells * 0.70f) + stormCell * WeatherSecondary.x * 0.35f);

	float4 result = target;
	if (Quality.z != 0u && PreviousWeatherMap.w > 0.5f) {
		float2 advectedXY = absoluteXY - WindTime.xy * max(WindTime.w, 0.0f);
		float2 previousUV = WeatherUV(advectedXY, PreviousWeatherMap);
		if (all(previousUV >= 0.0f) && all(previousUV <= 1.0f)) {
			float4 previous = PreviousWeather.SampleLevel(LinearClamp, previousUV, 0);
			// Weather fronts should retain coherent motion but respond on a useful
			// weather-transition timescale; the old calm-weather coefficient kept
			// stale cloud coverage for close to a minute after clearing.
			float evolution = 1.0f - exp2(-max(WindTime.w, 0.001f) * lerp(0.35f, 0.70f, WeatherSecondary.x));
			result = lerp(previous, target, evolution);
		}
	}
	WeatherOutput[pixel] = saturate(result);
}

Texture2D<float4> WeatherTexture : register(t0);
Texture2D<float> SceneDepth : register(t1);
RWTexture2D<float4> CloudOutput : register(u0);
RWTexture2D<float> CloudDepthOutput : register(u1);

float HeightProfile(float h, float type)
{
	float bottom = smoothstep(0.0f, lerp(0.12f, 0.055f, type), h);
	float top = 1.0f - smoothstep(lerp(0.68f, 0.84f, type), 1.0f, h);
	float anvil = lerp(1.0f, smoothstep(0.08f, 0.42f, h) * (1.0f - 0.18f * h), type * WeatherSecondary.x);
	return saturate(bottom * top * anvil);
}

float SampleDensity(float3 absolutePosition, bool detail)
{
	float2 engineCameraXY = float2(FrameBuffer::CameraViewInverse[0][3], FrameBuffer::CameraViewInverse[1][3]);
	float horizontalDistance = length(absolutePosition.xy - (FrameBuffer::CameraPosAdjust.xy + engineCameraXY));
	float curvedHeight = absolutePosition.z + horizontalDistance * horizontalDistance / max(2.0f * Layer.w, 1.0f);
	float h = (curvedHeight - Layer.x) / max(Layer.y - Layer.x, 1.0f);
	if (h <= 0.0f || h >= 1.0f)
		return 0.0f;
	float2 weatherUV = WeatherUV(absolutePosition.xy, WeatherMapData);
	float4 localWeather = WeatherTexture.SampleLevel(LinearClamp, saturate(weatherUV), 0);
	float coverage = saturate(localWeather.x);
	float cloudType = saturate(lerp(Weather.z, localWeather.y, 0.70f));
	float2 advectedWind = WindTime.xy * WindTime.z;
	float2 layerWind = advectedWind * lerp(0.72f, 1.28f, h) +
		float2(-advectedWind.y, advectedWind.x) * ((h - 0.5f) * 0.12f);
	float3 windOffset = float3(layerWind, WindTime.z * lerp(3.0f, 8.0f, h));
	// Both the weather map and density field advect formations along Skyrim's
	// wind vector. Opposite signs made coverage/type drift against cloud shapes.
	float3 basePosition = absolutePosition * (0.000020f * max(Shape.x, 0.05f)) - windOffset * 0.000020f;
	float warp = Noise3(basePosition * 0.58f + 41.0f) - 0.5f;
	basePosition.xy += warp * Shape.w * 1.65f;
	float baseNoise = Fbm3(basePosition);
	float threshold = lerp(0.77f, 0.30f, coverage);
	float density = saturate((baseNoise - threshold) / max(1.0f - threshold, 0.08f));
	// The world-space field extends almost to the configured cloud view distance.
	// Fading density at its texture border projected a curved alpha seam into the
	// sky. Clamped edge sampling is stable for the small uncovered margin.
	density *= HeightProfile(h, cloudType);
	if (detail && density > 0.005f) {
		float3 detailPosition = absolutePosition * (0.000105f * max(Shape.y, 0.05f));
		detailPosition.xy -= WindTime.xy * WindTime.z * 0.000045f;
		detailPosition.z += 12.0f * WindTime.z * 0.000045f;
		float erosionNoise = Fbm3(detailPosition + warp * 1.7f);
		float edge = saturate(1.0f - abs(density * 2.0f - 1.0f));
		density = saturate(density - (1.0f - erosionNoise) * Shape.z * (0.28f + edge * 0.72f));
	}
	return density * Weather.y;
}

float HG(float cosTheta, float g)
{
	g = clamp(g, -0.92f, 0.92f);
	float denominator = max(1.0f + g * g - 2.0f * g * cosTheta, 0.025f);
	return (1.0f - g * g) / (4.0f * PI * denominator * sqrt(denominator));
}

float LightTransmittance(float3 position, float3 sunDirection, float stepLength)
{
	float opticalDepth = 0.0f;
	float distance = stepLength;
	[loop]
	for (uint i = 0u; i < 8u; ++i) {
		if (i >= Quality.y)
			break;
		float cone = (float(i) + 0.5f) * 0.37f;
		float3 offset = float3(cone * 97.0f, -cone * 61.0f, cone * 43.0f);
		opticalDepth += SampleDensity(position + sunDirection * distance + offset, i < 2u) * stepLength;
		distance *= 1.55f;
	}
	return exp2(-opticalDepth * max(Lighting.x, 0.001f) * 0.00034f);
}

float3 ReconstructRay(float2 uv, out float3 engineCamera)
{
	float4 farPoint = mul(FrameBuffer::CameraViewProjInverse,
		float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f));
	farPoint.xyz /= max(abs(farPoint.w), 1.0e-6f);
	// Skyrim uploads a row-major matrix consumed as mul(matrix, vector), so the
	// inverse-view translation is the final column. This matches State's
	// transpose()._41/_42/_43 camera extraction on the CPU.
	engineCamera = float3(FrameBuffer::CameraViewInverse[0][3],
		FrameBuffer::CameraViewInverse[1][3], FrameBuffer::CameraViewInverse[2][3]);
	return normalize(farPoint.xyz - engineCamera);
}

bool IntersectLayer(float cameraZ, float3 ray, out float entryDistance, out float exitDistance)
{
	float directionZ = abs(ray.z) < 0.002f ? (ray.z < 0.0f ? -0.002f : 0.002f) : ray.z;
	float bottom = (Layer.x - cameraZ) / directionZ;
	float top = (Layer.y - cameraZ) / directionZ;
	entryDistance = max(min(bottom, top), 0.0f);
	exitDistance = min(max(bottom, top), Layer.z);
	// Near-horizontal rays meet the curved shell at a finite distance.
	if (ray.z <= 0.002f && cameraZ < Layer.x) {
		float horizonEntry = sqrt(max(2.0f * Layer.w * (Layer.x - cameraZ), 0.0f));
		float horizonExit = sqrt(max(2.0f * Layer.w * (Layer.y - cameraZ), 0.0f));
		entryDistance = max(min(horizonEntry, horizonExit), 0.0f);
		exitDistance = min(max(horizonEntry, horizonExit), Layer.z);
	}
	return exitDistance > entryDistance + 1.0f;
}

[numthreads(8, 8, 1)]
void RaymarchCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint2 pixel = dispatchThreadID.xy;
	if (any(pixel >= uint2(RenderSize.xy)))
		return;
	float2 uv = (float2(pixel) + 0.5f) * RenderSize.zw;
	float fullDepth = SceneDepth.SampleLevel(LinearClamp, FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(uv), 0);
	float3 engineCamera;
	float3 ray = ReconstructRay(uv, engineCamera);
	float3 cameraAbsolute = engineCamera + FrameBuffer::CameraPosAdjust.xyz;
	float entryDistance;
	float exitDistance;
	if (!IntersectLayer(cameraAbsolute.z, ray, entryDistance, exitDistance)) {
		CloudOutput[pixel] = float4(0.0f, 0.0f, 0.0f, 1.0f);
		CloudDepthOutput[pixel] = Layer.z;
		return;
	}
	// Geometry in front of the cloud interval prevents needless cloud work.
	if (fullDepth < 0.99999f) {
		float4 scenePoint = mul(FrameBuffer::CameraViewProjInverse,
			float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), fullDepth, 1.0f));
		float sceneDistance = length(scenePoint.xyz / max(abs(scenePoint.w), 1.0e-6f) - engineCamera);
		exitDistance = min(exitDistance, sceneDistance);
		if (exitDistance <= entryDistance + 1.0f) {
			CloudOutput[pixel] = float4(0.0f, 0.0f, 0.0f, 1.0f);
			CloudDepthOutput[pixel] = Layer.z;
			return;
		}
	}

	uint stepCount = max(Quality.x, 8u);
	float interval = exitDistance - entryDistance;
	float baseStep = interval / float(stepCount);
	float jitter = frac(Hash21(int2(pixel) + int2((uint)Temporal.z * 23u, (uint)Temporal.z * 47u)) + 0.6180339f * Temporal.z);
	float distance = entryDistance + baseStep * jitter;
	float transmittance = 1.0f;
	float3 radiance = 0.0f;
	float representativeDepth = exitDistance;
	float weightedDepth = 0.0f;
	float accumulatedWeight = 0.0f;
	uint emptyRun = 0u;
	float3 sunDirection = normalize(SharedData::DirLightDirection.xyz + float3(0.0f, 0.0f, 1.0e-5f));
	float cosTheta = dot(ray, sunDirection);
	float phaseForward = HG(cosTheta, Phase.x);
	float phaseBackward = HG(cosTheta, Phase.y);
	float phaseValue = phaseForward * 0.78f + phaseBackward * 0.22f;
	float silver = pow(saturate(cosTheta), lerp(8.0f, 28.0f, saturate(Phase.z))) * Phase.z;
	float3 directColor = max(SharedData::DirLightColor.rgb, 0.0f) * WeatherSecondary.w;
	float3 ambientColor = max(float3(SharedData::AmbientSHR.x, SharedData::AmbientSHG.x, SharedData::AmbientSHB.x), 0.015f.xxx);
	// Skyrim exposes the two moon directions and their phase-adjusted colors in
	// SharedData. Use them as a low-energy, directional night fill so moonlit
	// cloud forms retain shape and hue instead of collapsing to uniform ambient.
	float3 solarDirection = SharedData::SunDirection.xyz;
	solarDirection = dot(solarDirection, solarDirection) > 1.0e-6f ? normalize(solarDirection) : sunDirection;
	float nightFactor = 1.0f - smoothstep(-0.08f, 0.08f, solarDirection.z);
	float3 masserDirection = SharedData::MasserDirection.xyz;
	float3 secundaDirection = SharedData::SecundaDirection.xyz;
	masserDirection = dot(masserDirection, masserDirection) > 1.0e-6f ? normalize(masserDirection) : float3(0.0f, 0.0f, 1.0f);
	secundaDirection = dot(secundaDirection, secundaDirection) > 1.0e-6f ? normalize(secundaDirection) : float3(0.0f, 0.0f, 1.0f);
	float3 masserColor = max(SharedData::MasserColor.rgb, 0.0f);
	float3 secundaColor = max(SharedData::SecundaColor.rgb, 0.0f);
	float lunarPhase = 0.5f * (HG(dot(ray, masserDirection), 0.42f) + HG(dot(ray, secundaDirection), 0.42f));
	float3 lunarScattering = (masserColor + secundaColor) * (0.12f * nightFactor);

	[loop]
	for (uint stepIndex = 0u; stepIndex < 96u; ++stepIndex) {
		if (stepIndex >= stepCount || distance >= exitDistance || transmittance < 0.012f)
			break;
		float adaptiveStep = baseStep * (1.0f + saturate(distance / max(Layer.z, 1.0f)) * 0.85f);
		float3 position = cameraAbsolute + ray * distance;
		float density = SampleDensity(position, true);
		if (density <= 0.002f) {
			emptyRun++;
			distance += adaptiveStep * (emptyRun > 2u ? 2.2f : 1.0f);
			continue;
		}
		emptyRun = 0u;
		float extinction = density * adaptiveStep * max(Lighting.x, 0.001f) * 0.00042f;
		float segmentTransmittance = exp2(-extinction);
		float segmentWeight = transmittance * (1.0f - segmentTransmittance);
		float lightVisibility = LightTransmittance(position, sunDirection, max(adaptiveStep * 1.9f, 1800.0f));
		float powder = 1.0f - exp2(-density * max(Lighting.w, 0.0f) * 2.8f);
		// Three energy-reduced scattering orders broaden illumination in dense
		// regions without three additional shadow marches.
		float multipleScattering = 0.0f;
		float orderEnergy = 1.0f;
		float orderVisibility = lightVisibility;
		[unroll]
		for (uint order = 0u; order < 3u; ++order) {
			multipleScattering += orderEnergy * orderVisibility;
			orderEnergy *= saturate(Phase.w) * 0.38f;
			orderVisibility = lerp(orderVisibility, 1.0f, 0.48f);
		}
		float3 direct = directColor * (phaseValue * 5.4f + silver * 0.22f) * multipleScattering +
			lunarScattering * lunarPhase * 5.4f * lerp(0.72f, 1.0f, lightVisibility);
		float height = saturate((position.z - Layer.x) / max(Layer.y - Layer.x, 1.0f));
		float baseDarkening = lerp(0.40f, 1.0f, smoothstep(0.0f, 0.72f, height));
		float3 ambient = (ambientColor + lunarScattering * (0.65f + 0.35f * saturate(SharedData::MasserColor.w + SharedData::SecundaColor.w))) *
			Lighting.z * baseDarkening * (0.68f + powder * 0.32f);
		radiance += segmentWeight * (direct + ambient) * Lighting.y;
		weightedDepth += distance * segmentWeight;
		accumulatedWeight += segmentWeight;
		transmittance *= segmentTransmittance;
		distance += adaptiveStep;
	}
	if (accumulatedWeight > 1.0e-5f)
		representativeDepth = weightedDepth / accumulatedWeight;
	if (!all(isfinite(radiance)) || !isfinite(transmittance)) {
		radiance = 0.0f;
		transmittance = 1.0f;
		representativeDepth = exitDistance;
	}
	CloudOutput[pixel] = float4(max(radiance, 0.0f), saturate(transmittance));
	CloudDepthOutput[pixel] = clamp(representativeDepth, 0.0f, Layer.z);
}

Texture2D<float4> CurrentCloud : register(t0);
Texture2D<float> CurrentCloudDepth : register(t1);
Texture2D<float4> PreviousCloud : register(t2);
Texture2D<float> PreviousCloudDepth : register(t3);
RWTexture2D<float4> ResolvedCloud : register(u0);
RWTexture2D<float> ResolvedCloudDepth : register(u1);

[numthreads(8, 8, 1)]
void TemporalCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint2 pixel = dispatchThreadID.xy;
	if (any(pixel >= uint2(RenderSize.xy)))
		return;
	float2 uv = (float2(pixel) + 0.5f) * RenderSize.zw;
	float4 current = CurrentCloud.Load(int3(pixel, 0));
	float currentDepth = CurrentCloudDepth.Load(int3(pixel, 0));
	float3 engineCamera;
	float3 ray = ReconstructRay(uv, engineCamera);
	float3 currentEnginePosition = engineCamera + ray * currentDepth;
	float3 currentAbsolutePosition = currentEnginePosition + FrameBuffer::CameraPosAdjust.xyz;
	float height = saturate((currentAbsolutePosition.z - Layer.x) / max(Layer.y - Layer.x, 1.0f));
	float2 perpendicularWind = float2(-CloudMotion.y, CloudMotion.x);
	float2 layerWind = CloudMotion.xy * lerp(0.72f, 1.28f, height) +
		perpendicularWind * ((height - 0.5f) * 0.12f);
	float3 previousEnginePosition = currentEnginePosition + FrameBuffer::CameraPosAdjust.xyz - FrameBuffer::CameraPreviousPosAdjust.xyz;
	// The density uses worldPosition - wind * time, so a feature was upwind in
	// the previous frame. Include cloud advection in addition to camera/origin motion.
	previousEnginePosition.xy -= layerWind * max(CloudMotion.z, 0.0f);
	float4 previousClip = mul(FrameBuffer::CameraPreviousViewProjUnjittered, float4(previousEnginePosition, 1.0f));
	float2 previousUV = previousClip.xy / max(abs(previousClip.w), 1.0e-6f) * float2(0.5f, -0.5f) + 0.5f;
	bool historyUsable = Temporal.y > 0.5f && previousClip.w > 0.0f && all(previousUV > 0.0f) && all(previousUV < 1.0f);
	float4 result = current;
	if (historyUsable) {
		float4 history = PreviousCloud.SampleLevel(LinearClamp, previousUV, 0);
		float historyDepth = PreviousCloudDepth.SampleLevel(LinearClamp, previousUV, 0);
		float4 localMin = current;
		float4 localMax = current;
		[unroll]
		for (int y = -1; y <= 1; ++y) {
			[unroll]
			for (int x = -1; x <= 1; ++x) {
				int2 p = clamp(int2(pixel) + int2(x, y), int2(0, 0), int2(RenderSize.xy) - 1);
				float4 value = CurrentCloud.Load(int3(p, 0));
				localMin = min(localMin, value);
				localMax = max(localMax, value);
			}
		}
		history = clamp(history, localMin - 0.035f, localMax + 0.035f);
		float depthError = abs(historyDepth - currentDepth) / max(currentDepth, 2048.0f);
		float weatherMotion = saturate(length(WindTime.xy) * WindTime.w / 1200.0f + WeatherSecondary.z * 0.08f);
		float rejection = saturate(depthError / max(Temporal.w, 0.001f) + weatherMotion);
		float historyWeight = saturate(Temporal.x * (1.0f - rejection));
		result = lerp(current, history, historyWeight);
	}
	ResolvedCloud[pixel] = result;
	ResolvedCloudDepth[pixel] = currentDepth;
}

Texture2D<float4> CompositeCloud : register(t0);
Texture2D<float> CompositeCloudDepth : register(t1);
Texture2D<float> CompositeSceneDepth : register(t2);
Texture2D<float4> CompositeWeather : register(t3);
Texture3D<float4> CloudIntegratedFog : register(t19);
RWTexture2D<float4> SceneColor : register(u0);

float4 SampleIntegratedCloudFog(float3 positionWS)
{
	if (SharedData::atmosphereSettings.enabled == 0 ||
		SharedData::atmosphereSettings.volumetricFogEnabled == 0 ||
		SharedData::atmosphereSettings.fogDensity <= 0.0f ||
		SharedData::atmosphereSettings.volumetricFogExtinctionScale <= 0.0f ||
		SharedData::atmosphereSettings.volumetricFogDistance <=
			SharedData::atmosphereSettings.volumetricFogStartDistance + 1.0f)
		return float4(0.0f, 0.0f, 0.0f, 1.0f);

	uint volumeWidth, volumeHeight, volumeDepth;
	CloudIntegratedFog.GetDimensions(volumeWidth, volumeHeight, volumeDepth);
	if (volumeWidth == 0 || volumeHeight == 0 || volumeDepth == 0)
		return float4(0.0f, 0.0f, 0.0f, 1.0f);

	float4 clip = mul(FrameBuffer::CameraViewProj, float4(positionWS, 1.0f));
	if (clip.w <= 1.0e-4f)
		return float4(0.0f, 0.0f, 0.0f, 1.0f);
	float2 uv = saturate(clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f);
	float nearPlane = max(SharedData::CameraData.y,
		SharedData::atmosphereSettings.volumetricFogStartDistance);
	float farPlane = max(nearPlane + 1.0f,
		SharedData::atmosphereSettings.volumetricFogDistance);
	float depthScale = max(SharedData::atmosphereSettings.volumetricDepthDistributionScale,
		float(volumeDepth) / 120.0f);
	float nearWithOffset = nearPlane + 9.5f;
	float farExp = exp2(min(float(volumeDepth) / depthScale, 120.0f));
	float denominator = farPlane - nearWithOffset;
	denominator = abs(denominator) >= 1.0e-6f ? denominator : (denominator < 0.0f ? -1.0e-6f : 1.0e-6f);
	float gridOffset = (farPlane - nearWithOffset * farExp) / denominator;
	float gridScale = (1.0f - gridOffset) / max(nearWithOffset, 1.0e-4f);
	float volumeZ = log2(max(clip.w * gridScale + gridOffset, 1.0e-6f)) * depthScale / float(volumeDepth);
	if (clip.w < SharedData::atmosphereSettings.volumetricFogStartDistance ||
		clip.w > SharedData::atmosphereSettings.volumetricFogDistance)
		return float4(0.0f, 0.0f, 0.0f, 1.0f);
	return CloudIntegratedFog.SampleLevel(LinearClamp, float3(uv, saturate(volumeZ)), 0);
}

[numthreads(8, 8, 1)]
void CompositeCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint2 pixel = dispatchThreadID.xy;
	if (any(pixel >= uint2(FullSize.xy)))
		return;
	float2 uv = (float2(pixel) + 0.5f) * FullSize.zw;
	float depth = CompositeSceneDepth.Load(int3(pixel, 0));
	float4 scene = SceneColor[pixel];
	float cloudDepth = CompositeCloudDepth.SampleLevel(LinearClamp, uv, 0);
	float4 cloud = CompositeCloud.SampleLevel(LinearClamp, uv, 0);
	float3 engineCamera = float3(FrameBuffer::CameraViewInverse[0][3],
		FrameBuffer::CameraViewInverse[1][3], FrameBuffer::CameraViewInverse[2][3]);
	float3 ray = ReconstructRay(uv, engineCamera);
	bool cloudInFrontOfScene = true;
	if (depth < 0.99999f) {
		float4 scenePoint = mul(FrameBuffer::CameraViewProjInverse,
			float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth, 1.0f));
		float sceneDistance = length(scenePoint.xyz / max(abs(scenePoint.w), 1.0e-6f) - engineCamera);
		cloudInFrontOfScene = cloudDepth + max(64.0f, sceneDistance * 0.002f) < sceneDistance;
	}
	if (!cloudInFrontOfScene)
		cloud = float4(0.0f, 0.0f, 0.0f, 1.0f);
	if (Composite.w == 1.0f && depth >= 0.99999f)
		scene.rgb = cloud.rgb;
	else if (Composite.w == 2.0f && depth >= 0.99999f)
		scene.rgb = (1.0f - cloud.a).xxx;
	else {
		float3 cloudPosition = engineCamera + ray * cloudDepth;
		float4 fog = SampleIntegratedCloudFog(cloudPosition);
		cloud.rgb = cloud.rgb * fog.a + fog.rgb * (1.0f - cloud.a);
		scene.rgb = cloud.rgb + scene.rgb * cloud.a;
	}
	if (depth < 0.99999f && Composite.x > 0.0f) {
		float4 world = mul(FrameBuffer::CameraViewProjInverse,
			float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth, 1.0f));
		world.xyz /= max(abs(world.w), 1.0e-6f);
		float distanceToCamera = length(world.xyz - engineCamera);
		if (distanceToCamera < Composite.y) {
			float2 absoluteXY = world.xy + FrameBuffer::CameraPosAdjust.xy;
			float2 shadowOffset = normalize(SharedData::DirLightDirection.xy + float2(1.0e-4f, 0.0f)) * 18000.0f;
			float coverage = CompositeWeather.SampleLevel(LinearClamp, WeatherUV(absoluteXY + shadowOffset, WeatherMapData), 0).x;
			float shadow = saturate((coverage - 0.34f) * 1.35f) * Composite.x * lerp(0.45f, 1.0f, WeatherSecondary.w);
			scene.rgb *= 1.0f - shadow;
		}
	}
	SceneColor[pixel] = scene;
}

