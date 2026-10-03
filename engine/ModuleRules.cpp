#include "ModuleRules.h"
#include "RenderModule.h"
#include "Globals.h"
#include "State.h"

#include <mutex>
#include <unordered_map>

namespace ModuleRules
{
	namespace
	{
		std::mutex constraintCacheMutex;
		bool constraintCacheValid = false;
		std::vector<std::pair<SettingId, ConstraintResult>> constraintCacheEntries;
		std::unordered_map<std::string, std::size_t> constraintCacheIndex;

		std::string MakeKey(const SettingId& setting)
		{
			return setting.featureShortName + "|" + setting.settingPath;
		}

		void RebuildConstraintCache()
		{
			constraintCacheEntries.clear();
			constraintCacheIndex.clear();
			for (auto* feature : RenderModule::GetModuleList()) {
				if (!feature->loaded)
					continue;
				for (const auto& constraint : feature->GetActiveConstraints()) {
					const auto key = MakeKey(constraint.targetSetting);
					auto [it, inserted] = constraintCacheIndex.try_emplace(key, constraintCacheEntries.size());
					if (inserted)
						constraintCacheEntries.push_back({ constraint.targetSetting, {} });
					auto& result = constraintCacheEntries[it->second].second;
					if (!result.isConstrained) {
						result.isConstrained = true;
						result.forcedValue = constraint.forcedValue;
					} else if (constraint.forcedValue != result.forcedValue) {
						logger::warn("[ModuleRules] Conflict on {}.{}: {} wants {}, but {} already forced {}",
							constraint.targetSetting.featureShortName, constraint.targetSetting.settingPath,
							feature->GetName(), FormatConstraintValue(constraint.forcedValue),
							result.sources[0].featureName, FormatConstraintValue(result.forcedValue));
					}
					result.sources.push_back({ feature->GetName(), feature->GetShortName(),
						constraint.reason, constraint.recommendDisableAtBoot });
				}
			}
			constraintCacheValid = true;
		}
	}

	void InvalidateConstraintCache()
	{
		std::scoped_lock lock(constraintCacheMutex);
		constraintCacheValid = false;
	}

	ConstraintResult GetConstraints(const SettingId& setting)
	{
		if (globals::state && globals::state->IsDeveloperMode())
			return {};
		std::scoped_lock lock(constraintCacheMutex);
		if (!constraintCacheValid)
			RebuildConstraintCache();
		const auto it = constraintCacheIndex.find(MakeKey(setting));
		return it == constraintCacheIndex.end() ? ConstraintResult{} : constraintCacheEntries[it->second].second;
	}

	std::vector<std::pair<SettingId, ConstraintResult>> GetAllActiveConstraints()
	{
		if (globals::state && globals::state->IsDeveloperMode())
			return {};
		std::scoped_lock lock(constraintCacheMutex);
		if (!constraintCacheValid)
			RebuildConstraintCache();
		return constraintCacheEntries;
	}

	std::string BuildConstraintTooltip(const ConstraintResult& result)
	{
		if (!result.isConstrained || result.sources.empty())
			return "";

		std::string tooltip = "This setting is constrained by:\n";
		for (const auto& src : result.sources) {
			tooltip += "\n- " + src.featureName + ":\n  " + src.reason;
			if (src.recommendDisableAtBoot) {
				tooltip += "\n  (Consider disabling this feature at boot for best compatibility)";
			}
		}

		tooltip += "\n\nForced value: " + FormatConstraintValue(result.forcedValue);

		return tooltip;
	}

	std::string FormatConstraintValue(const std::variant<bool, int, float>& value)
	{
		if (std::holds_alternative<bool>(value)) {
			return std::get<bool>(value) ? "Enabled" : "Disabled";
		} else if (std::holds_alternative<int>(value)) {
			return std::to_string(std::get<int>(value));
		} else if (std::holds_alternative<float>(value)) {
			char buf[32];
			snprintf(buf, sizeof(buf), "%.2f", std::get<float>(value));
			return buf;
		}
		return "Unknown";
	}
}
