// FidelityFX Super Resolution - Robust Contrast Adaptive Sharpening (RCAS)
// Based on https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))

cbuffer RCASConfig : register(b0)
{
	float sharpness;
	uint useConfidence;
	float2 inputDimensions;
};

Texture2D<float4> Source : register(t0);
Texture2D<float> ReactiveMask : register(t1);
Texture2D<float> TransparencyMask : register(t2);
Texture2D<float2> MotionVectors : register(t3);
RWTexture2D<float4> Dest : register(u0);

// RCAS contains several reciprocal limiters whose mathematical inputs can be
// exactly zero for flat black/white regions.  The reference implementation
// relies on implementation-specific reciprocal behaviour; explicit guards
// keep a single non-finite value from contaminating HDR output.
float SafeRcpPositive(float value)
{
	return rcp(max(value, 1.0e-6));
}

float SafeRcpSigned(float value)
{
	return rcp(abs(value) >= 1.0e-6 ? value : (value < 0.0 ? -1.0e-6 : 1.0e-6));
}

float GetSharpenConfidence(uint2 outputPixel, uint2 outputDimensions)
{
	if (useConfidence == 0)
		return 1.0;

	uint2 validInputDimensions = max(uint2(inputDimensions), 1u.xx);
	float2 inputPosition =
		(float2(outputPixel) + 0.5) * (inputDimensions / float2(outputDimensions));
	uint2 inputPixel = min(uint2(inputPosition), validInputDimensions - 1u.xx);

	float reactive = saturate(ReactiveMask.Load(int3(inputPixel, 0)) * 1.25);
	float transparency = saturate(TransparencyMask.Load(int3(inputPixel, 0)) * 1.5);
	float unstableSurface = max(reactive, transparency);

	// Motion vectors are normalized screen-space deltas. Evaluate them in output
	// pixels so quality modes share one confidence response.
	float2 motion = MotionVectors.Load(int3(inputPixel, 0));
	float motionPixels = length(motion * float2(outputDimensions));
	float motionConfidence = exp2(-motionPixels * 0.10);

	return saturate((1.0 - unstableSurface) * motionConfidence);
}

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	uint2 texDim;
	Dest.GetDimensions(texDim.x, texDim.y);

	if (DTid.x >= texDim.x || DTid.y >= texDim.y)
		return;

	// Algorithm uses minimal 3x3 pixel neighborhood. Clamp explicitly because
	// Texture2D.Load returns zero for out-of-range coordinates, which otherwise
	// creates a dark sharpening seam along the display edge.
	//    b
	//  d e f
	//    h
	int2 sp = int2(DTid.xy);
	uint sourceWidth;
	uint sourceHeight;
	Source.GetDimensions(sourceWidth, sourceHeight);
	int2 sourceMax = int2(max(uint2(sourceWidth, sourceHeight), 1u.xx) - 1u.xx);
	float3 b = Source.Load(int3(clamp(sp + int2(0, -1), int2(0, 0), sourceMax), 0)).rgb;
	float3 d = Source.Load(int3(clamp(sp + int2(-1, 0), int2(0, 0), sourceMax), 0)).rgb;
	float3 e = Source.Load(int3(sp, 0)).rgb;
	float3 f = Source.Load(int3(clamp(sp + int2(1, 0), int2(0, 0), sourceMax), 0)).rgb;
	float3 h = Source.Load(int3(clamp(sp + int2(0, 1), int2(0, 0), sourceMax), 0)).rgb;

	// Rename (32-bit) or regroup (16-bit).
	float bR = b.r;
	float bG = b.g;
	float bB = b.b;
	float dR = d.r;
	float dG = d.g;
	float dB = d.b;
	float eR = e.r;
	float eG = e.g;
	float eB = e.b;
	float fR = f.r;
	float fG = f.g;
	float fB = f.b;
	float hR = h.r;
	float hG = h.g;
	float hB = h.b;

	// Luma times 2.
	float bL = bB * 0.5 + (bR * 0.5 + bG);
	float dL = dB * 0.5 + (dR * 0.5 + dG);
	float eL = eB * 0.5 + (eR * 0.5 + eG);
	float fL = fB * 0.5 + (fR * 0.5 + fG);
	float hL = hB * 0.5 + (hR * 0.5 + hG);

	// Noise detection.
	float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
	float lumaRange = max(max(max(bL, dL), max(eL, fL)), hL) - min(min(min(bL, dL), min(eL, fL)), hL);
	nz = saturate(abs(nz) * SafeRcpPositive(lumaRange));
	nz = -0.5 * nz + 1.0;

	// Min and max of ring.
	float mn4R = min(min(min(bR, dR), fR), hR);
	float mn4G = min(min(min(bG, dG), fG), hG);
	float mn4B = min(min(min(bB, dB), fB), hB);
	float mx4R = max(max(max(bR, dR), fR), hR);
	float mx4G = max(max(max(bG, dG), fG), hG);
	float mx4B = max(max(max(bB, dB), fB), hB);

	// Immediate constants for peak range.
	float2 peakC = float2(1.0, -1.0 * 4.0);

	// Limiters, these need to be high precision RCPs.
	float hitMinR = min(mn4R, eR) * SafeRcpPositive(4.0 * mx4R);
	float hitMinG = min(mn4G, eG) * SafeRcpPositive(4.0 * mx4G);
	float hitMinB = min(mn4B, eB) * SafeRcpPositive(4.0 * mx4B);
	float hitMaxR = (peakC.x - max(mx4R, eR)) * SafeRcpSigned(4.0 * mn4R + peakC.y);
	float hitMaxG = (peakC.x - max(mx4G, eG)) * SafeRcpSigned(4.0 * mn4G + peakC.y);
	float hitMaxB = (peakC.x - max(mx4B, eB)) * SafeRcpSigned(4.0 * mn4B + peakC.y);
	float lobeR = max(-hitMinR, hitMaxR);
	float lobeG = max(-hitMinG, hitMaxG);
	float lobeB = max(-hitMinB, hitMaxB);
	float lobe = max(-FSR_RCAS_LIMIT, min(max(lobeR, max(lobeG, lobeB)), 0.0)) * sharpness;

	// Apply noise removal.
	lobe *= nz * GetSharpenConfidence(DTid.xy, texDim);

	// Resolve, which needs the medium precision rcp approximation to avoid visible tonality changes.
	float rcpL = rcp(4.0 * lobe + 1.0);
	float pixR = (lobe * bR + lobe * dR + lobe * hR + lobe * fR + eR) * rcpL;
	float pixG = (lobe * bG + lobe * dG + lobe * hG + lobe * fG + eG) * rcpL;
	float pixB = (lobe * bB + lobe * dB + lobe * hB + lobe * fB + eB) * rcpL;

	Dest[DTid.xy] = float4(pixR, pixG, pixB, 1.0);
}
