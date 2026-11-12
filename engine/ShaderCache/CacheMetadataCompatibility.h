#pragma once

#include <string_view>

namespace SIE
{
	struct CacheMetadataCompatibility
	{
		bool layoutAndABICompatible{};
		bool shaderRevisionMatches{};
	};

	// Individual disk artifacts are keyed by the full preprocessed source closure,
	// macro set, compiler identity, entry point, profile, and flags. A shared shader
	// revision mismatch is therefore provenance only; the artifact identity remains
	// the authoritative reuse check. Layout and shared-buffer ABI mismatches are not
	// covered by that artifact key and must still reject the entire pipeline cache.
	inline constexpr CacheMetadataCompatibility CheckCacheMetadataCompatibility(
		std::string_view cachedLayout, std::string_view expectedLayout,
		std::string_view cachedABI, std::string_view expectedABI,
		std::string_view cachedRevision, std::string_view expectedRevision) noexcept
	{
		return {
			!cachedLayout.empty() && cachedLayout == expectedLayout &&
			!cachedABI.empty() && cachedABI == expectedABI,
			!cachedRevision.empty() && cachedRevision == expectedRevision
		};
	}
}
