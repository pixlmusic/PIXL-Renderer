#include "SeasonIntegration.h"

#include "Utils/Game.h"

#include <RE/T/TESDataHandler.h>
#include <RE/T/TESLandTexture.h>

#include <TlHelp32.h>

#include <optional>

namespace
{
	constexpr auto kPollInterval = std::chrono::seconds(1);
	constexpr auto kDiscoveryRetryInterval = std::chrono::seconds(30);

	std::optional<PIXLSeason> DecodeSeason(std::uint32_t a_value)
	{
		switch (a_value) {
		case 0:
			return PIXLSeason::kNone;
		case 1:
			return PIXLSeason::kWinter;
		case 2:
			return PIXLSeason::kSpring;
		case 3:
			return PIXLSeason::kSummer;
		case 4:
			return PIXLSeason::kAutumn;
		default:
			return std::nullopt;
		}
	}
}

SeasonIntegration& SeasonIntegration::GetSingleton()
{
	static SeasonIntegration singleton;
	return singleton;
}

void SeasonIntegration::Initialize()
{
	std::scoped_lock lock(providerMutex);
	if (DiscoverProviderLocked()) {
		QueryProviderLocked(false);
	} else if (!unavailableLogged) {
		logger::info("[PIXL][Seasons] Seasons provider not detected; using material-driven fallback.");
		unavailableLogged = true;
	}
	const auto now = std::chrono::steady_clock::now();
	nextPoll = now + kPollInterval;
	nextDiscovery = now + kDiscoveryRetryInterval;
}

void SeasonIntegration::OnDataLoaded()
{
	RebuildLandTextureIndex();

	std::scoped_lock lock(providerMutex);
	if ((getCurrentSeason || DiscoverProviderLocked())) {
		QueryProviderLocked(false);
	}
	const auto now = std::chrono::steady_clock::now();
	nextPoll = now + kPollInterval;
	nextDiscovery = now + kDiscoveryRetryInterval;
}

void SeasonIntegration::RequestGameStateRefresh()
{
	refreshRequested.store(true, std::memory_order_release);
}

void SeasonIntegration::Poll()
{
	const bool forceGeneration = refreshRequested.exchange(false, std::memory_order_acq_rel);
	const auto now = std::chrono::steady_clock::now();
	if (!forceGeneration && now < nextPoll)
		return;

	std::scoped_lock lock(providerMutex);
	nextPoll = now + kPollInterval;
	if (!getCurrentSeason) {
		if (!forceGeneration && now < nextDiscovery)
			return;
		nextDiscovery = now + kDiscoveryRetryInterval;
		if (!DiscoverProviderLocked())
			return;
	}

	QueryProviderLocked(forceGeneration);
}

SeasonContext SeasonIntegration::GetContext() const noexcept
{
	SeasonContext result{};
	// The writer publishes season/status/availability before the release-store to
	// generation. Acquiring generation first makes the accompanying fields visible.
	result.generation = generation.load(std::memory_order_acquire);
	result.providerAvailable = providerAvailable.load(std::memory_order_relaxed);
	result.season = static_cast<PIXLSeason>(currentSeason.load(std::memory_order_relaxed));
	result.status = static_cast<SeasonProviderStatus>(providerStatus.load(std::memory_order_relaxed));
	return result;
}

ResolvedLandSurface SeasonIntegration::ResolveLandSurface(const RE::TESLandTexture* a_landTexture) const
{
	ResolvedLandSurface result{};
	result.originalLandTexture = a_landTexture;
	result.landTexture = a_landTexture;
	if (!a_landTexture || !a_landTexture->textureSet)
		return result;

	auto* originalTextureSet = a_landTexture->textureSet;
	auto* resolvedTextureSet = Util::GetSeasonalSwap(originalTextureSet);
	result.textureSet = resolvedTextureSet ? resolvedTextureSet : originalTextureSet;
	result.seasonalSwapActive = result.textureSet != originalTextureSet;
	if (!result.seasonalSwapActive)
		return result;

	// Never retain the pre-swap physical material as authoritative. If the owning
	// replacement LT cannot be found, callers can still classify the resolved TXST
	// path and renderer-observed snow flag without inheriting stale snow/soil data.
	result.landTexture = nullptr;
	if (result.textureSet->formID == 0)
		return result;

	const std::shared_lock lock(landTextureIndexMutex);
	if (const auto it = landTextureByTextureSet.find(result.textureSet->formID);
		it != landTextureByTextureSet.end()) {
		result.landTexture = it->second;
	}
	return result;
}

const char* SeasonIntegration::SeasonName(PIXLSeason a_season) noexcept
{
	switch (a_season) {
	case PIXLSeason::kWinter:
		return "Winter";
	case PIXLSeason::kSpring:
		return "Spring";
	case PIXLSeason::kSummer:
		return "Summer";
	case PIXLSeason::kAutumn:
		return "Autumn";
	default:
		return "None";
	}
}

const char* SeasonIntegration::StatusName(SeasonProviderStatus a_status) noexcept
{
	switch (a_status) {
	case SeasonProviderStatus::kActive:
		return "Native API active";
	case SeasonProviderStatus::kUnknownValue:
		return "Unknown season value";
	case SeasonProviderStatus::kQueryUnavailable:
		return "Query unavailable";
	default:
		return "Material fallback";
	}
}

bool SeasonIntegration::DiscoverProviderLocked()
{
	if (getCurrentSeason)
		return true;

	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if (snapshot == INVALID_HANDLE_VALUE)
		return false;

	MODULEENTRY32W moduleEntry{};
	moduleEntry.dwSize = sizeof(moduleEntry);
	bool found = false;
	if (Module32FirstW(snapshot, &moduleEntry)) {
		do {
			const auto current = reinterpret_cast<GetSeasonFn>(
				GetProcAddress(moduleEntry.hModule, "GetCurrentSeason"));
			if (!current)
				continue;

			getCurrentSeason = current;
			providerModule = moduleEntry.hModule;
			providerName = stl::utf16_to_utf8(moduleEntry.szModule).value_or("<unknown module>");
			found = true;
			break;
		} while (Module32NextW(snapshot, &moduleEntry));
	}
	CloseHandle(snapshot);

	if (found) {
		unavailableLogged = false;
		logger::info("[PIXL][Seasons] Seasons provider detected: {}. Native API available.", providerName);
	}
	return found;
}

void SeasonIntegration::QueryProviderLocked(bool a_forceGeneration)
{
	if (!getCurrentSeason) {
		providerAvailable.store(false, std::memory_order_relaxed);
		providerStatus.store(
			static_cast<std::uint32_t>(SeasonProviderStatus::kQueryUnavailable),
			std::memory_order_relaxed);
		return;
	}

	PublishSeasonLocked(getCurrentSeason(), a_forceGeneration);
}

void SeasonIntegration::PublishSeasonLocked(std::uint32_t a_rawSeason, bool a_forceGeneration)
{
	const auto decoded = DecodeSeason(a_rawSeason);
	const PIXLSeason nextSeason = decoded.value_or(PIXLSeason::kNone);
	const auto nextStatus = decoded ? SeasonProviderStatus::kActive : SeasonProviderStatus::kUnknownValue;
	const auto previousSeason = static_cast<PIXLSeason>(currentSeason.load(std::memory_order_relaxed));
	const bool changed = !providerPublished || previousSeason != nextSeason ||
		providerStatus.load(std::memory_order_relaxed) != static_cast<std::uint32_t>(nextStatus);

	currentSeason.store(static_cast<std::uint32_t>(nextSeason), std::memory_order_relaxed);
	providerStatus.store(static_cast<std::uint32_t>(nextStatus), std::memory_order_relaxed);
	providerAvailable.store(true, std::memory_order_relaxed);

	if (changed || a_forceGeneration) {
		std::uint32_t nextGeneration = generation.load(std::memory_order_relaxed) + 1u;
		if (nextGeneration == 0u)
			nextGeneration = 1u;
		generation.store(nextGeneration, std::memory_order_release);

		if (!decoded) {
			logger::warn("[PIXL][Seasons] Provider returned unknown season value {}; using neutral context (generation {}).",
				a_rawSeason, nextGeneration);
		} else if (!providerPublished) {
			logger::info("[PIXL][Seasons] Initial season: {} (generation {}).",
				SeasonName(nextSeason), nextGeneration);
		} else if (previousSeason != nextSeason) {
			logger::info("[PIXL][Seasons] Season changed: {} -> {} (generation {}).",
				SeasonName(previousSeason), SeasonName(nextSeason), nextGeneration);
		} else {
			logger::info("[PIXL][Seasons] Game state refreshed for {} (generation {}).",
				SeasonName(nextSeason), nextGeneration);
		}
	}

	providerPublished = true;
}

void SeasonIntegration::RebuildLandTextureIndex()
{
	std::unordered_map<std::uint32_t, const RE::TESLandTexture*> rebuilt;
	if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
		const auto& landTextures = dataHandler->GetFormArray<RE::TESLandTexture>();
		rebuilt.reserve(landTextures.size());
		for (auto* landTexture : landTextures) {
			if (!landTexture || !landTexture->textureSet || landTexture->textureSet->formID == 0)
				continue;
			rebuilt.try_emplace(landTexture->textureSet->formID, landTexture);
		}
	}

	const auto count = rebuilt.size();
	{
		const std::unique_lock lock(landTextureIndexMutex);
		landTextureByTextureSet = std::move(rebuilt);
	}
	logger::info("[PIXL][Seasons] Indexed {} landscape texture-set owners for resolved-material classification.", count);
}
