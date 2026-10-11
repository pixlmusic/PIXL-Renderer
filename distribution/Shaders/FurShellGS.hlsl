// Procedural normal-extruded shell layers for confidently classified fur draws.
// Input/output semantics mirror the SKINNED AUTO_FUR Lighting.hlsl permutation.

cbuffer VS_PerFrame : register(b12)
{
	row_major float3x3 ScreenProj : packoffset(c0);
	row_major float4x4 ViewProj : packoffset(c8);
};

struct FurShellInput
{
	float4 Position : SV_POSITION0;
	float2 TexCoord0 : TEXCOORD0;
#if !defined(MODELSPACENORMALS) || defined(SKINNED)
	float3 TBN0 : TEXCOORD1;
	float3 TBN1 : TEXCOORD2;
	float3 TBN2 : TEXCOORD3;
#endif
	float4 WorldPosition : POSITION1;
	float4 PreviousWorldPosition : POSITION2;
	float4 Color : COLOR0;
	float4 FogParam : COLOR1;
	float3 ModelPosition : TEXCOORD12;
	nointerpolation uint FurShellLayer : TEXCOORD14;
};

struct FurShellOutput
{
	float4 Position : SV_POSITION0;
	float2 TexCoord0 : TEXCOORD0;
#if !defined(MODELSPACENORMALS) || defined(SKINNED)
	float3 TBN0 : TEXCOORD1;
	float3 TBN1 : TEXCOORD2;
	float3 TBN2 : TEXCOORD3;
#endif
	float4 WorldPosition : POSITION1;
	float4 PreviousWorldPosition : POSITION2;
	float4 Color : COLOR0;
	float4 FogParam : COLOR1;
	float3 ModelPosition : TEXCOORD12;
	nointerpolation uint FurShellLayer : TEXCOORD14;
};

static const uint ShellLayerCount = 8;
cbuffer FurShellSettings : register(b13)
{
	float FurShellLength;
	float3 FurShellPadding;
};

FurShellOutput BuildShellVertex(FurShellInput input, uint layer, float3 shellNormal)
{
	FurShellOutput output;
	// Small deterministic length variation breaks the uniform extruded silhouette
	// into soft clumps. The seed is object-space so animated actors do not shimmer.
	float3 clump = floor(input.ModelPosition * 5.0f);
	float clumpNoise = frac(sin(dot(clump, float3(127.1f, 311.7f, 74.7f))) * 43758.5453f);
	float lengthVariation = lerp(0.82f, 1.18f, clumpNoise);
	float distance = FurShellLength * lengthVariation * (float(layer) / float(ShellLayerCount));
	float3 worldPosition = input.WorldPosition.xyz + shellNormal * distance;
	float3 previousWorldPosition = input.PreviousWorldPosition.xyz + shellNormal * distance;

	output.Position = mul(ViewProj, float4(worldPosition, 1.0f));
	output.TexCoord0 = input.TexCoord0;
#if !defined(MODELSPACENORMALS) || defined(SKINNED)
	output.TBN0 = input.TBN0;
	output.TBN1 = input.TBN1;
	output.TBN2 = input.TBN2;
#endif
	output.WorldPosition = float4(worldPosition, input.WorldPosition.w);
	output.PreviousWorldPosition = float4(previousWorldPosition, input.PreviousWorldPosition.w);
	output.Color = input.Color;
	output.FogParam = input.FogParam;
	output.ModelPosition = input.ModelPosition;
	output.FurShellLayer = layer;
	return output;
}

[maxvertexcount(27)]
void main(triangle FurShellInput input[3], inout TriangleStream<FurShellOutput> stream)
{
	// Static model-space-normal meshes omit TBN from the vertex output. Recover a
	// geometric normal for those fur assets (rugs, pelts and static trims); skinned
	// and tangent-space meshes retain their smooth interpolated normal.
#if defined(MODELSPACENORMALS) && !defined(SKINNED)
	float3 shellNormal = cross(
		input[1].WorldPosition.xyz - input[0].WorldPosition.xyz,
		input[2].WorldPosition.xyz - input[0].WorldPosition.xyz);
	float faceNormalLengthSq = dot(shellNormal, shellNormal);
	shellNormal *= rsqrt(max(faceNormalLengthSq, 1.0e-8f));
#endif

	// The original surface remains as an opaque undercoat. Eight extruded shells
	// use the full fur length; this stream stays within D3D11's 1024-component cap.
	[unroll]
	for (uint layer = 0; layer <= ShellLayerCount; ++layer) {
		[unroll]
		for (uint vertex = 0; vertex < 3; ++vertex) {
#if defined(MODELSPACENORMALS) && !defined(SKINNED)
			stream.Append(BuildShellVertex(input[vertex], layer, shellNormal));
#else
			float3 vertexNormal = float3(
				input[vertex].TBN0.z,
				input[vertex].TBN1.z,
				input[vertex].TBN2.z);
			float vertexNormalLengthSq = dot(vertexNormal, vertexNormal);
			vertexNormal *= rsqrt(max(vertexNormalLengthSq, 1.0e-8f));
			stream.Append(BuildShellVertex(input[vertex], layer, vertexNormal));
#endif
		}
		stream.RestartStrip();
	}
}
