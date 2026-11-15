#include "ShaderCache/CacheMetadataCompatibility.h"

#include <cassert>

int main()
{
	using SIE::CheckCacheMetadataCompatibility;
	constexpr auto oldShaderRevision = CheckCacheMetadataCompatibility(
		"PIXL.StageShard.v1", "PIXL.StageShard.v1",
		"PIXL.SharedBuffers.20260926.RenderOrigin1", "PIXL.SharedBuffers.20260926.RenderOrigin1",
		"PIXL.Shaders.20261023.MaterialSurfaceProfiles1", "PIXL.Shaders.20261102.SDFResidentDirectCoverage1");
	assert(oldShaderRevision.layoutAndABICompatible);
	assert(!oldShaderRevision.shaderRevisionMatches);

	constexpr auto changedABI = CheckCacheMetadataCompatibility(
		"PIXL.StageShard.v1", "PIXL.StageShard.v1",
		"PIXL.SharedBuffers.old", "PIXL.SharedBuffers.new",
		"same", "same");
	assert(!changedABI.layoutAndABICompatible);

	constexpr auto changedLayout = CheckCacheMetadataCompatibility(
		"PIXL.StageShard.old", "PIXL.StageShard.new",
		"same", "same",
		"same", "same");
	assert(!changedLayout.layoutAndABICompatible);

	constexpr auto missingABI = CheckCacheMetadataCompatibility(
		"PIXL.StageShard.v1", "PIXL.StageShard.v1",
		"", "PIXL.SharedBuffers.20260926.RenderOrigin1",
		"same", "same");
	assert(!missingABI.layoutAndABICompatible);

	constexpr auto exactMetadata = CheckCacheMetadataCompatibility(
		"PIXL.StageShard.v1", "PIXL.StageShard.v1",
		"PIXL.SharedBuffers.20260926.RenderOrigin1", "PIXL.SharedBuffers.20260926.RenderOrigin1",
		"revision", "revision");
	assert(exactMetadata.layoutAndABICompatible && exactMetadata.shaderRevisionMatches);
	return 0;
}
