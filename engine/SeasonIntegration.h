#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace RE
{
	class BGSTextureSet;
	class TESLandTexture;
}

enum class PIXLSeason : std::uint32_t
{
	kNone = 0,
	kWinter = 1,
	kSpring = 2,
	kSummer = 3,
	kAutumn = 4
};

enum class SeasonProviderStatus : std::uint32_t
{
	kUnavailable = 0,
	kActive,
	kUnknownValue,
	kQueryUnavailable
};

struct SeasonContext
{
	bool providerAvailable = false;
	PIXLSeason season = PIXLSeason::kNone;
	SeasonProviderStatus status = SeasonProviderStatus::kUnavailable;
	std::uint32_t generation = 0;
};

struct ResolvedLandSurface
{
	const RE::TESLandTexture* originalLandTexture = nullptr;
	const RE::TESLandTexture* landTexture = nullptr;
	RE::BGSTextureSet* textureSet = nullptr;
	bool seasonalSwapActive = false;
};

/**
 * Optional, read-only bridge to a loaded Seasons of Skyrim provider.
 *
 * Provider calls are confined to the game/update side. Render code consumes the
 * atomic context snapshot and resolved material records only; PIXL never links
 * against or loads the third-party DLL.
 */
class SeasonIntegration final
{
public:
	static SeasonIntegration& GetSingleton();

	void Initialize();
	void OnDataLoaded();
	void RequestGameStateRefresh();
	void Poll();

	[[nodiscard]] SeasonContext GetContext() const noexcept;
	[[nodiscard]] ResolvedLandSurface ResolveLandSurface(const RE::TESLandTexture* a_landTexture) const;

	[[nodiscard]] static const char* SeasonName(PIXLSeason a_season) noexcept;
	[[nodiscard]] static const char* StatusName(SeasonProviderStatus a_status) noexcept;

private:
	using GetSeasonFn = std::uint32_t (*)();

	bool DiscoverProviderLocked();
	void QueryProviderLocked(bool a_forceGeneration);
	void PublishSeasonLocked(std::uint32_t a_rawSeason, bool a_forceGeneration);
	void RebuildLandTextureIndex();

	mutable std::mutex providerMutex;
	GetSeasonFn getCurrentSeason = nullptr;
	void* providerModule = nullptr;  // Borrowed HMODULE; PIXL never owns/unloads it.
	std::string providerName;
	bool unavailableLogged = false;
	bool providerPublished = false;
	std::chrono::steady_clock::time_point nextPoll{};
	std::chrono::steady_clock::time_point nextDiscovery{};
	std::atomic_bool refreshRequested{ false };

	std::atomic_bool providerAvailable{ false };
	std::atomic<std::uint32_t> currentSeason{ 0 };
	std::atomic<std::uint32_t> providerStatus{
		static_cast<std::uint32_t>(SeasonProviderStatus::kUnavailable)
	};
	std::atomic<std::uint32_t> generation{ 0 };

	mutable std::shared_mutex landTextureIndexMutex;
	std::unordered_map<std::uint32_t, const RE::TESLandTexture*> landTextureByTextureSet;
};
