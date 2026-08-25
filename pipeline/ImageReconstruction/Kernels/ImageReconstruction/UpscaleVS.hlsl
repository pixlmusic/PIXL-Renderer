struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD;
};

#if defined(VSHADER)

VS_OUTPUT main(uint vertexId : SV_VertexID)
{
	VS_OUTPUT vsout;

	// Generate the fullscreen triangle directly from the low two vertex-id bits.
	// The shifts are exactly equivalent to /2 and %2 for SV_VertexID, while
	// making the intended integer operations explicit to all HLSL compilers.
	const uint x = vertexId >> 1u;
	const uint y = vertexId & 1u;

	vsout.Position = float4((float)x * 4.0f - 1.0f, (float)y * 4.0f - 1.0f, 0.0f, 1.0f);
	vsout.TexCoord = float2((float)x * 2.0f, 1.0f - (float)y * 2.0f);

	return vsout;
}

#endif