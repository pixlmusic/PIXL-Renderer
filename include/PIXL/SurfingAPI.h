// Versioned plain-data diagnostics contract between PIXL Surfing and PIXL Renderer.
// No C++ objects, engine pointers, or renderer resources cross this boundary.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PIXL_SurfingTelemetryV1
{
	uint32_t structSize;
	uint32_t version;
	uint32_t state;       // PIXL Surfing's stable V1 state ordinal.
	uint32_t flags;       // bit 0 ocean connected, 1 surface, 2 depth, 3 board, 4 owns board, 5 movement ownership, 6 debug logging.
	uint32_t worldspaceFormID;
	float playerPosition[3];
	float boardPosition[3];
	float boardVelocity[3];
	float waterHeight;
	float waterDepth;
	float waterNormal[3];
	float waveDirection[2];
	float breakerStrength;
	float catchConfidence;
	float balance;
	float sampleAgeSeconds;
	// V1 remains frozen at its original 100-byte layout.
} PIXL_SurfingTelemetryV1;

typedef struct PIXL_SurfingTelemetryV2
{
	PIXL_SurfingTelemetryV1 base;
	float playerVelocity[3];
	float paddleInput;
	float steerInput;
	float windVelocity[2];
} PIXL_SurfingTelemetryV2;

typedef uint32_t (__cdecl *PIXL_SurfingGetTelemetryV1)(PIXL_SurfingTelemetryV1* telemetry);
typedef uint32_t (__cdecl *PIXL_SurfingGetTelemetryV2)(PIXL_SurfingTelemetryV2* telemetry);
typedef void (__cdecl *PIXL_SurfingSetDebugV1)(uint32_t enabled);

// PIXLSurfing.dll exports functions with these names for GetProcAddress:
// PIXL_Surfing_GetTelemetry_V1/V2 and PIXL_Surfing_SetDebug_V1.

#ifdef __cplusplus
}

static_assert(sizeof(PIXL_SurfingTelemetryV1) == 100,
	"PIXL Surfing telemetry V1 layout changed; preserve its ABI");
static_assert(sizeof(PIXL_SurfingTelemetryV2) == 128,
	"PIXL Surfing telemetry V2 layout changed; introduce a new API version instead");
#endif
