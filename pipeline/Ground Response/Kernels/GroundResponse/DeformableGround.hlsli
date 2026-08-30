#ifndef PIXL_DEFORMABLE_GROUND_HLSLI
#define PIXL_DEFORMABLE_GROUND_HLSLI

// PIXL Ground Response 3.0
//
// t101 in terrain stages is a normalized, persistent WORLD-XY compaction map.
// It deliberately contains no collider height and no camera-relative Z state.
// The same map drives thick snow and thin mud; material classification decides
// how much raised shell is available to compress.

#ifndef USE_PIXL_DEFORMABLE_GROUND
#define USE_PIXL_DEFORMABLE_GROUND 1
#endif

namespace DeformableGround
{
	Texture2D<float4> InteractionField : register(t101);

	static const int TextureSize = 1024;
	static const int TextureMask = TextureSize - 1;
	static const float WorldSize = 4096.0f;
	static const float CellSize = WorldSize / float(TextureSize);

	// PIXL_GR_SLOPE_LIMITED_COLLAPSE_V2
	//
	// Compression walls are limited by a physical depth-vs-distance slope rather
	// than by a capacity-derived blur radius. 1.35 means a 13.5-unit depression
	// naturally relaxes over roughly 10 horizontal units; a 40-unit depression
	// reaches roughly 30 units before returning to pristine height.
	static const float CollapseWallSlope = 1.35f;     // vertical units / horizontal unit
	static const float CollapseInnerRadius = 8.0f;
	static const float CollapseMiddleRadius = 16.0f;
	static const float CollapseOuterRadius = 30.0f;

	struct SurfaceCompression
	{
		float current;
		float previous;
		float freshness;
		float previousFreshness;
	};

	struct SampleData
	{
		float amount;
		float depth;
		float freshness;
		float edge;
		float3 normal;
	};

	int2 WrapTexel(int2 logicalTexel)
	{
		return
			(logicalTexel +
				int2(GroundRuntimeSurfaceArrayOrigin)) &
			int2(TextureMask, TextureMask);
	}

	float4 LoadLogical(int2 logicalTexel)
	{
		logicalTexel =
			clamp(
				logicalTexel,
				int2(0, 0),
				int2(TextureSize - 1, TextureSize - 1));
		return
			saturate(
				InteractionField.Load(
					int3(WrapTexel(logicalTexel), 0)));
	}

	float4 SampleRawAbsolute(float2 absWorldXY)
	{
		float2 localPosition =
			absWorldXY -
			GroundRuntimeSurfaceOriginAbsolute;

		// Logical coverage is finite even though physical storage is toroidal.
		const float safeHalfExtent =
			WorldSize * 0.5f - CellSize;
		if (any(abs(localPosition) >= safeHalfExtent.xx))
			return 0.0f.xxxx;

		float2 texelPosition =
			(localPosition / WorldSize + 0.5f) *
				float(TextureSize) -
			0.5f;

		texelPosition =
			clamp(
				texelPosition,
				0.0f.xx,
				(float(TextureSize) - 1.0001f).xx);

		int2 baseTexel =
			min(
				(int2)floor(texelPosition),
				int2(TextureSize - 2, TextureSize - 2));
		float2 f =
			saturate(
				texelPosition -
				float2(baseTexel));

		float4 s00 = LoadLogical(baseTexel);
		float4 s10 = LoadLogical(baseTexel + int2(1, 0));
		float4 s01 = LoadLogical(baseTexel + int2(0, 1));
		float4 s11 = LoadLogical(baseTexel + int2(1, 1));

		return
			lerp(
				lerp(s00, s10, f.x),
				lerp(s01, s11, f.x),
				f.y);
	}

	// PIXL_GR_13V_FXC_HELPER_ORDER_FIX
	// FXC requires these helpers to be declared before the slope-limited
	// compression routine calls them. Logic is byte-for-byte unchanged.
	float2 AbsoluteXY(float3 cameraRelativeWorldPosition)
	{
		return
			cameraRelativeWorldPosition.xy +
			FrameBuffer::CameraPosAdjust.xy;
	}

	SurfaceCompression GetSurfaceCompression(float3 worldPosition)
	{
		SurfaceCompression result;
		result.current = 0.0f;
		result.previous = 0.0f;
		result.freshness = 0.0f;
		result.previousFreshness = 0.0f;

#if USE_PIXL_DEFORMABLE_GROUND
		if (!GroundResponseRuntime::IsRuntimeValid() ||
			SharedData::deformableGroundSettings.EnableDeformableGround == 0u)
			return result;

		float4 field =
			SampleRawAbsolute(
				AbsoluteXY(worldPosition));

		// GroundResponseStrength is applied once when capsule stamps are written.
		// Sampling stays linear so changing the slider cannot square the response
		// or retroactively reinterpret already-persistent world-space tracks.
		result.current = saturate(field.x);
		result.previous = saturate(field.z);
		result.freshness = saturate(field.y);
		result.previousFreshness = saturate(field.w);
#endif
		return result;
	}

	float SlopeLimitedCandidateDepth(
		float normalizedCompression,
		float maximumDepth,
		float sampleDistance)
	{
		float sourceDepth =
			saturate(normalizedCompression) *
			max(maximumDepth, 0.0f);

		return
			max(
				sourceDepth -
					sampleDistance * CollapseWallSlope,
				0.0f);
	}

	void AccumulateSlopeLimitedSample(
		float2 absWorldXY,
		float2 offset,
		float maximumDepth,
		inout float currentDepth,
		inout float previousDepth)
	{
		float4 sample =
			SampleRawAbsolute(
				absWorldXY + offset);

		float sampleDistance =
			length(offset);

		currentDepth =
			max(
				currentDepth,
				SlopeLimitedCandidateDepth(
					sample.x,
					maximumDepth,
					sampleDistance));

		previousDepth =
			max(
				previousDepth,
				SlopeLimitedCandidateDepth(
					sample.z,
					maximumDepth,
					sampleDistance));
	}

	SurfaceCompression GetSlopeLimitedSurfaceCompression(
		float3 worldPosition,
		float maximumDepth)
	{
		SurfaceCompression result;
		result.current = 0.0f;
		result.previous = 0.0f;
		result.freshness = 0.0f;
		result.previousFreshness = 0.0f;

#if USE_PIXL_DEFORMABLE_GROUND
		if (!GroundResponseRuntime::IsRuntimeValid() ||
			SharedData::deformableGroundSettings.EnableDeformableGround == 0u)
			return result;

		float safeDepth =
			max(maximumDepth, 0.0f);
		if (safeDepth <= 1e-5f)
			return GetSurfaceCompression(worldPosition);

		float2 absXY =
			AbsoluteXY(worldPosition);
		float4 centre =
			SampleRawAbsolute(absXY);

		float currentDepth =
			saturate(centre.x) * safeDepth;
		float previousDepth =
			saturate(centre.z) * safeDepth;

		// Ring 1: cardinals. Keeps foot-scale detail continuous at the 4-unit t101
		// texel resolution without widening small/shallow contacts excessively.
		AccumulateSlopeLimitedSample(
			absXY, float2(CollapseInnerRadius, 0.0f),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(-CollapseInnerRadius, 0.0f),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(0.0f, CollapseInnerRadius),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(0.0f, -CollapseInnerRadius),
			safeDepth, currentDepth, previousDepth);

		// Ring 2: diagonals. Alternating orientation prevents the old visible
		// concentric/cardinal stepping while filling the shoulder between rings.
		const float middleDiag =
			CollapseMiddleRadius * 0.70710678118f;
		AccumulateSlopeLimitedSample(
			absXY, float2(middleDiag, middleDiag),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(-middleDiag, middleDiag),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(middleDiag, -middleDiag),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(-middleDiag, -middleDiag),
			safeDepth, currentDepth, previousDepth);

		// Ring 3: eight-direction outer support. It only contributes if the
		// neighbouring depression is physically deep enough to survive the
		// distance penalty, so raising Interaction Strength deepens the mark
		// without turning the whole area into a broad weighted plateau.
		const float outerDiag =
			CollapseOuterRadius * 0.70710678118f;
		AccumulateSlopeLimitedSample(
			absXY, float2(CollapseOuterRadius, 0.0f),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(-CollapseOuterRadius, 0.0f),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(0.0f, CollapseOuterRadius),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(0.0f, -CollapseOuterRadius),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(outerDiag, outerDiag),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(-outerDiag, outerDiag),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(outerDiag, -outerDiag),
			safeDepth, currentDepth, previousDepth);
		AccumulateSlopeLimitedSample(
			absXY, float2(-outerDiag, -outerDiag),
			safeDepth, currentDepth, previousDepth);

		result.current =
			saturate(
				currentDepth /
				max(safeDepth, 1e-5f));
		result.previous =
			saturate(
				previousDepth /
				max(safeDepth, 1e-5f));

		// Freshness/hold remains local. Lateral collapse is a geometric response
		// to a nearby compressed pocket, not a second persistent track stamp.
		result.freshness = saturate(centre.y);
		result.previousFreshness = saturate(centre.w);
#endif
		return result;
	}


	float EvaluatePreviousDepth(
		float3 worldPosition,
		float maximumDepth)
	{
#if USE_PIXL_DEFORMABLE_GROUND
		if (maximumDepth <= 0.0f)
			return 0.0f;

		SurfaceCompression surface =
			GetSurfaceCompression(worldPosition);
		return
			max(maximumDepth, 0.0f) *
			surface.previous;
#else
		return 0.0f;
#endif
	}

	SampleData Evaluate(
		float3 worldPosition,
		float3 receiverNormal,
		float maximumDepth)
	{
		SampleData result;
		result.amount = 0.0f;
		result.depth = 0.0f;
		result.freshness = 0.0f;
		result.edge = 0.0f;
		result.normal = receiverNormal;

#if USE_PIXL_DEFORMABLE_GROUND
		if (SharedData::deformableGroundSettings.EnableDeformableGround == 0u ||
			maximumDepth <= 0.0f ||
			!GroundResponseRuntime::IsRuntimeValid())
			return result;

		const float2 absXY = AbsoluteXY(worldPosition);
		const float4 centre = SampleRawAbsolute(absXY);

		float amount = saturate(centre.x);
		if (amount <= 1e-5f)
			return result;

		float sampleStep = CellSize;
		float left =
			SampleRawAbsolute(
				absXY - float2(sampleStep, 0.0f)).x;
		float right =
			SampleRawAbsolute(
				absXY + float2(sampleStep, 0.0f)).x;
		float down =
			SampleRawAbsolute(
				absXY - float2(0.0f, sampleStep)).x;
		float up =
			SampleRawAbsolute(
				absXY + float2(0.0f, sampleStep)).x;

		float2 compactionGradient =
			float2(
				right - left,
				up - down) /
			max(2.0f * sampleStep, 1e-3f);

		float safeDepth =
			max(maximumDepth, 0.0f);
		float normalStrength =
			max(
				SharedData::deformableGroundSettings.GroundNormalStrength,
				0.0f);

		// Surface height decreases as compaction increases. Convert the
		// compaction derivative into the corresponding trench-wall normal.
		float2 heightNormal =
			compactionGradient *
			safeDepth *
			normalStrength;

		float3 deformationVector =
			receiverNormal +
				float3(
					heightNormal.x,
					heightNormal.y,
					0.0f);
		float deformationLengthSq = dot(deformationVector, deformationVector);
		float3 deformationNormal =
			deformationLengthSq > 1.0e-8f
				? deformationVector * rsqrt(deformationLengthSq)
				: float3(0.0f, 0.0f, 1.0f);

		float gradientMagnitude =
			length(compactionGradient) *
			CellSize;

		result.amount = amount;
		result.depth = safeDepth * amount;
		result.freshness = saturate(centre.y);
		result.edge =
			saturate(
				gradientMagnitude * 2.0f) *
			amount;
		result.normal = deformationNormal;
#endif
		return result;
	}
}

#endif
