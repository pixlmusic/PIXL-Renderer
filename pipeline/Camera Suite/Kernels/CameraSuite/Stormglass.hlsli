#ifndef PIXL_STORMGLASS_COMMON
#define PIXL_STORMGLASS_COMMON

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

// PIXL Stormglass
// ----------------
// Texture-free weather-driven optical surface. The helper is self-contained:
// it no longer depends on an external PixlHash/PixlLuminance definition.

struct PixlStormglassWarpData
{
	float2 uv;
	float coverage;
	float rim;
};

uint PixlStormglassHash32(uint value)
{
	value ^= value >> 16u;
	value *= 0x7feb352du;
	value ^= value >> 15u;
	value *= 0x846ca68bu;
	value ^= value >> 16u;
	return value;
}

uint PixlStormglassHashCombine(uint2 value, uint salt)
{
	uint h = PixlStormglassHash32(value.x ^ (value.y * 0x9e3779b9u) ^ salt);
	return PixlStormglassHash32(h + value.y + 0x85ebca6bu);
}

float2 PixlStormglassHash22(uint2 value, uint salt)
{
	uint h0 = PixlStormglassHashCombine(value + uint2(17u, 59u), salt + 11u);
	uint h1 = PixlStormglassHashCombine(value.yx + uint2(83u, 31u), salt + 97u);
	return float2(h0 & 0x00ffffffu, h1 & 0x00ffffffu) * (1.0f / 16777216.0f);
}

float PixlStormglassLuminance(float3 color)
{
	return dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
}

// Stormglass receives rain through two independent channels:
//  1) CameraSuite's dedicated presentation constant buffer.
//  2) PIXL SharedData's RainResponse per-frame Raining value.
// Either may activate the effect. This prevents a single CPU scheduling/cache
// path from silently suppressing otherwise-visible Skyrim rain.
float PixlStormglassLiveRain()
{
	return saturate(max(
		stormglassRainIntensity,
		SharedData::rainResponseSettings.Raining));
}

float PixlStormglassDropLayer(
	float2 uv,
	float aspect,
	float time,
	float laneCount,
	float speed,
	uint salt,
	float activity,
	float trailAmount)
{
	uint laneCountInt = max((uint)round(laneCount), 1u);
	float laneCoordinate = saturate(uv.x) * float(laneCountInt);
	uint lane = min((uint)floor(laneCoordinate), laneCountInt - 1u);

	// Stable lane phase prevents cycle-boundary teleporting.
	float lanePhase = PixlStormglassHash22(uint2(lane, salt), salt + 307u).y;
	float dropTime = max(time, 0.0f) * speed + lanePhase;
	uint cycle = (uint)floor(dropTime);
	float phase = frac(dropTime);

	float2 random = PixlStormglassHash22(uint2(lane, cycle), salt);
	float densityRandom = PixlStormglassHash22(uint2(cycle, lane), salt + 211u).x;
	float mobilityRandom = PixlStormglassHash22(uint2(lane + cycle * 13u, cycle + salt), salt + 733u).x;

	float centreXBase = (float(lane) + lerp(0.16f, 0.84f, random.x)) / float(laneCountInt);
	float centreY = lerp(-0.12f, 1.14f, phase);

	// Camera angular velocity supplies a signed inertial force. Each bead has a
	// slightly different mobility so the whole lens never translates as one rigid
	// screen-space layer. Gravity remains dominant; inertia only curves X travel.
	float inertia = clamp(stormglassLateralInertia, -1.0f, 1.0f);
	float mobility = lerp(0.55f, 1.18f, mobilityRandom);
	float inertialShape = sign(inertia) * pow(abs(inertia), 0.82f);
	float travelResponse = smoothstep(0.02f, 0.88f, phase);
	float lateralTravel = inertialShape * mobility * lerp(0.048f, 0.140f, random.x) * travelResponse;
	float centreX = centreXBase + lateralTravel;

	float radius = lerp(0.0085f, 0.0175f, random.y) * rsqrt(max(stormglassDropScale, 0.35f));
	float2 delta = float2((uv.x - centreX) * aspect, uv.y - centreY);

	float bodyDistance = length(float2(delta.x, delta.y * 1.18f));
	float body = 1.0f - smoothstep(radius * 0.20f, radius, bodyDistance);
	body = body * body * (3.0f - 2.0f * body);

	// The trail occupies earlier drop positions, so it lags against the current
	// lateral motion and naturally bends after a quick camera turn.
	float behind = -delta.y;
	float wakeLength = radius * lerp(4.5f, 9.0f, random.x);
	float wakeGate = step(0.0f, behind) * (1.0f - smoothstep(radius, wakeLength, behind));
	float wakeWidth = radius * lerp(0.10f, 0.24f, saturate(behind / max(wakeLength, 1e-5f)));
	float wakeLagUV = inertialShape * mobility * behind * 1.50f;
	float wakeDeltaX = delta.x + wakeLagUV * aspect;
	float wake = (1.0f - smoothstep(wakeWidth * 0.15f, max(wakeWidth, 1e-5f), abs(wakeDeltaX))) * wakeGate;

	float life = smoothstep(0.0f, 0.055f, phase) * (1.0f - smoothstep(0.90f, 1.0f, phase));
	float density = saturate((activity * 1.20f - densityRandom + 0.18f) * 4.0f) * activity;
	return saturate(body + wake * trailAmount * 0.34f) * life * density;
}

float PixlStormglassBeads(float2 uv, float aspect, float wetness)
{
	float scale = max(stormglassDropScale, 0.35f);
	float2 grid = float2(25.0f * scale, 17.0f * scale);
	float2 cellCoordinate = float2(uv.x * aspect, uv.y) * grid;
	int2 signedCell = (int2)floor(cellCoordinate);
	uint2 cell = uint2(max(signedCell, int2(0, 0)));
	float2 random = PixlStormglassHash22(cell, 401u);
	float densityRandom = PixlStormglassHash22(cell.yx, 557u).x;
	float2 local = frac(cellCoordinate) - lerp(0.18f.xx, 0.82f.xx, random);
	local.x /= max(aspect, 1e-3f);
	float radius = lerp(0.09f, 0.23f, random.y);
	float bead = 1.0f - smoothstep(radius * 0.25f, radius, length(local));
	float presence = saturate((wetness * 0.88f - densityRandom + 0.10f) * 9.0f);
	return bead * bead * presence * wetness;
}

float PixlStormglassDynamicHeightEx(float2 uv, float aspect, float rainExposure)
{
	// rainExposure is the camera's current precipitation visibility. It only
	// gates NEW weather-driven drops/trails. Residual wetness/static beads and
	// the water-emergence film remain independent, so walking under a roof does
	// not magically wipe an already-wet lens clean.
	float exposedRain = PixlStormglassLiveRain() * saturate(rainExposure);
	float wetness = saturate(max(stormglassWetness, PixlStormglassLiveRain() * 0.75f));
	float height = 0.0f;

	height += PixlStormglassDropLayer(
		uv, aspect, stormglassTime, max(7.0f, 11.0f * stormglassDropScale), 0.115f,
		71u, exposedRain, saturate(stormglassTrails));

	// The secondary moving layer is still conditioned on exposed rain. Wetness
	// changes its character/density while exposed but cannot spawn fresh drops
	// under an awning merely because the lens is already wet.
	float secondaryActivity = exposedRain * saturate(0.72f + wetness * 0.18f);
	height += PixlStormglassDropLayer(
		uv, aspect, stormglassTime + 13.7f, max(13.0f, 23.0f * stormglassDropScale), 0.071f,
		193u, secondaryActivity, saturate(stormglassTrails * 0.65f)) * 0.58f;

	// Premium surface-break response: an irregular draining sheet plus narrow
	// rivulets. This creates real X/Y gradients instead of an almost-flat film,
	// making the emerge-from-water transition visibly refract the scene.
	float film = saturate(surfaceBreakFilm);
	if (film > 0.001f)
	{
		float drainLine = (1.0f - film) * 1.14f;

		float columnCoord = uv.x * 18.0f;
		uint column = (uint)max(floor(columnCoord), 0.0f);
		float columnRandom = PixlStormglassHash22(uint2(column, 911u), 1297u).x;
		float columnWave =
			0.055f * sin(uv.x * 31.0f + stormglassTime * 0.83f) +
			0.035f * sin(uv.x * 67.0f - stormglassTime * 0.47f);
		float localFront = drainLine + (columnRandom - 0.5f) * 0.18f + columnWave;

		float sheet = smoothstep(localFront - 0.055f, localFront + 0.050f, uv.y);
		float sheetVariation = 0.72f + 0.28f *
			sin(uv.x * 39.0f + stormglassTime * 0.73f) *
			sin(uv.y * 27.0f - stormglassTime * 0.51f);

		float columnLocal = frac(columnCoord);
		float rivuletCentre = lerp(0.20f, 0.80f,
			PixlStormglassHash22(uint2(column, 1327u), 1481u).y);
		float rivuletWidth = lerp(0.055f, 0.12f, columnRandom);
		float rivulet = 1.0f - smoothstep(
			rivuletWidth * 0.35f,
			rivuletWidth,
			abs(columnLocal - rivuletCentre));
		float rivuletTail = smoothstep(localFront - 0.22f, localFront + 0.015f, uv.y) *
			(1.0f - smoothstep(localFront + 0.02f, 1.08f, uv.y));

		height += film * (
			sheet * sheetVariation * 1.05f +
			rivulet * rivuletTail * 0.75f);
	}

	return saturate(height);
}

// Compatibility path used by callers that do not have precipitation visibility.
float PixlStormglassDynamicHeight(float2 uv, float aspect)
{
	return PixlStormglassDynamicHeightEx(uv, aspect, 1.0f);
}

float PixlStormglassStaticBeadHeight(float2 uv, float aspect)
{
	float rain = PixlStormglassLiveRain();
	float wetness = saturate(max(stormglassWetness, rain * 0.75f));
	return PixlStormglassBeads(uv, aspect, wetness) * 0.42f;
}

// Preserve a combined height helper for compatibility/debug callers. Refraction
// below intentionally differentiates the moving and stationary components.
float PixlStormglassHeight(float2 uv, float aspect)
{
	return saturate(PixlStormglassDynamicHeight(uv, aspect) + PixlStormglassStaticBeadHeight(uv, aspect));
}

// General implementation used by both the full-resolution presentation path and
// the quarter-resolution field builder. heightSampleStepUV is the spacing at
// which the analytic height is evaluated; the slope is normalized by this spacing
// before being converted back to presentation pixels, so the refraction strength
// no longer changes when the field resolution changes.
PixlStormglassWarpData PixlApplyStormglassSampledExposure(
	float2 uv,
	uint2 dimensions,
	float2 heightSampleStepUV,
	float rainExposure)
{
	PixlStormglassWarpData result;
	result.uv = uv;
	result.coverage = 0.0f;
	result.rim = 0.0f;

	float exposedRain = PixlStormglassLiveRain() * saturate(rainExposure);
	float activeWetness = max(max(exposedRain, stormglassWetness), surfaceBreakFilm);
	float lensActivity = saturate(1.0f - submergedBlend);
	if (stormglassEnabled >= 0.5f && activeWetness > 0.002f && lensActivity > 0.001f)
	{
		float2 presentationPixel = rcp(max(float2(dimensions), 1.0f.xx));
		float2 sampleStep = max(heightSampleStepUV, presentationPixel);
		float aspect = float(dimensions.x) / max(float(dimensions.y), 1.0f);

		// Dynamic rain/film drives refraction. Static beads remain optically quiet
		// so fixed screen-space bead positions do not become obvious during motion.
		float centreDynamic = PixlStormglassDynamicHeightEx(uv, aspect, rainExposure);
		float rightDynamic = PixlStormglassDynamicHeightEx(
			saturate(uv + float2(sampleStep.x, 0.0f)), aspect, rainExposure);
		float downDynamic = PixlStormglassDynamicHeightEx(
			saturate(uv + float2(0.0f, sampleStep.y)), aspect, rainExposure);
		float staticBeads = PixlStormglassStaticBeadHeight(uv, aspect);
		// Under shelter, dynamic rain is already stopped by rainExposure. Keep only
		// a faint memory of existing moisture instead of showing a full-rain bead
		// field forever while the global weather remains rainy.
		float shelteredResidual = lerp(0.065f, 1.0f, saturate(rainExposure));
		staticBeads *= shelteredResidual;

		float2 slope = float2(
			centreDynamic - rightDynamic,
			centreDynamic - downDynamic) / sampleStep;

		float strength = saturate(stormglassStrength) * lensActivity;
		float refraction = max(stormglassRefraction, 0.0f);
		float2 offsetPixels = slope * (0.052f * refraction * strength);

		// The CPU/GPU diagnostic proved signed inertia reaches this shader. Add a
		// direct water-mass optical lag on top of the curved drop trajectory so a
		// quick camera turn is visibly readable even at quarter resolution.
		float inertia = clamp(stormglassLateralInertia, -1.0f, 1.0f);
		float inertiaShape = sign(inertia) * pow(abs(inertia), 0.78f);
		float movingWater = saturate(centreDynamic * 1.45f);
		float inertialPixels =
			inertiaShape *
			movingWater *
			lerp(8.0f, 15.0f, saturate(stormglassTrails)) *
			strength;
		offsetPixels.x += inertialPixels;

		float maxPixels = lerp(2.5f, 12.0f, strength);
		offsetPixels = clamp(offsetPixels, -maxPixels.xx, maxPixels.xx);
		float2 offset = offsetPixels * presentationPixel;

		result.uv = saturate(uv + offset);

		// Roof cover suppresses fresh rain but leaves a reduced residual bead
		// signature while the lens is still wet.
		float residualBeadWeight = lerp(0.08f, 0.18f, saturate(rainExposure));
		result.coverage =
			saturate(centreDynamic * 1.40f + staticBeads * residualBeadWeight) *
			strength;

		float2 perPixelGradient = slope * presentationPixel;
		result.rim = saturate(length(perPixelGradient) * 18.5f) * strength;
	}
	return result;
}

PixlStormglassWarpData PixlApplyStormglassSampled(
	float2 uv,
	uint2 dimensions,
	float2 heightSampleStepUV)
{
	return PixlApplyStormglassSampledExposure(
		uv, dimensions, heightSampleStepUV, 1.0f);
}

// Preserve the existing public API for any current HDROutput/presentation caller.
PixlStormglassWarpData PixlApplyStormglass(float2 uv, uint2 dimensions)
{
	float2 pixelSize = rcp(max(float2(dimensions), 1.0f.xx));
	return PixlApplyStormglassSampled(uv, dimensions, pixelSize);
}

float2 PixlApplySubmergedWarp(float2 uv, uint2 dimensions)
{
	float amount = saturate(submergedOpticsEnabled) * saturate(submergedBlend) *
		saturate(submergedStrength) * max(submergedRefraction, 0.0f);
	float2 result = uv;
	if (amount > 0.001f)
	{
		// The previous 1.35*amount displacement was sub-pixel at normal settings.
		// This remains controlled in presentation pixels but is now large enough
		// to survive Skyrim's water pass and TAA.
		float waveX =
			0.78f * sin(uv.y * 29.0f + stormglassTime * 1.07f) +
			0.30f * sin((uv.x + uv.y) * 67.0f - stormglassTime * 0.61f);
		float waveY =
			0.68f * sin(uv.x * 35.0f - stormglassTime * 0.91f) +
			0.24f * sin((uv.x - uv.y) * 83.0f + stormglassTime * 0.73f);
		float2 pixelSize = rcp(max(float2(dimensions), 1.0f.xx));
		float warpPixels = 7.5f * amount;
		result = saturate(uv + float2(waveX, waveY) * pixelSize * warpPixels);
	}
	return result;
}

float3 PixlApplyStormglassResponse(float3 scene, PixlStormglassWarpData lens)
{
	float sceneLuminance = PixlStormglassLuminance(scene);
	float edge = saturate(lens.rim * (0.35f + lens.coverage));
	float3 result = scene * (1.0f - lens.coverage * 0.025f);
	result += sceneLuminance * float3(0.92f, 0.97f, 1.0f) * edge * 0.018f;
	return max(result, 0.0f);
}

float3 PixlApplySubmergedGrade(float3 scene)
{
	float amount = saturate(submergedOpticsEnabled) * saturate(submergedBlend) * saturate(submergedStrength);
	float3 result = scene;
	if (amount > 0.001f)
	{
		float3 waterLinear = Color::SrgbToLinear(saturate(submergedWaterTint.rgb));
		float waterLuminance = max(PixlStormglassLuminance(waterLinear), 0.015f);
		float3 waterHue = clamp(waterLinear / waterLuminance, 0.68f.xxx, 1.35f.xxx);
		float sceneLuminance = PixlStormglassLuminance(scene);
		float fog = amount * saturate(submergedFogAmount);

		result = scene * lerp(1.0f.xxx, waterHue, amount * 0.23f);
		float3 colourPreservingVeil = lerp(result, sceneLuminance * waterHue, 0.32f);
		result = lerp(result, colourPreservingVeil, fog * 0.24f);
	}
	return max(result, 0.0f);
}

#endif
