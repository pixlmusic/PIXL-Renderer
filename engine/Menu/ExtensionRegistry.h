#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace PIXLUI::Extensions
{
	using Handle = std::uint64_t;
	struct Availability
	{
		bool available = true;
		std::string reason;
	};

	// Internal C++ API, not a cross-DLL ABI. All calls belong to the UI thread.
	// Captures must own their data; unregister before the provider is destroyed.
	struct Panel
	{
		std::string identifier;
		std::string displayName;
		std::string category;
		int order = 0;
		std::function<void()> draw;
		std::function<Availability()> availability;
		// Optional short text mark; no borrowed GPU texture lifetime to manage.
		std::string icon;
	};

	struct Entry
	{
		Handle handle = 0;
		Panel panel;
		bool registered = true;
		bool faulted = false;
	};

	class Registry
	{
	public:
		using Entries = std::vector<std::shared_ptr<Entry>>;
		[[nodiscard]] Handle Register(Panel panel)
		{
			if (shutdown_ || panel.identifier.empty() || panel.displayName.empty() || !panel.draw ||
				panel.identifier.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string::npos)
				return 0;
			for (const auto& entry : *entries_)
				if (entry->panel.identifier == panel.identifier)
					return 0;
			auto next = std::make_shared<Entries>(*entries_);
			const auto handle = ++nextHandle_;
			next->push_back(std::make_shared<Entry>(Entry{ handle, std::move(panel) }));
			std::stable_sort(next->begin(), next->end(), [](const auto& a, const auto& b) {
				if (a->panel.category != b->panel.category)
					return a->panel.category < b->panel.category;
				return a->panel.order < b->panel.order;
			});
			entries_ = std::move(next);
			return handle;
		}

		bool Unregister(Handle handle)
		{
			auto next = std::make_shared<Entries>();
			bool removed = false;
			for (const auto& entry : *entries_) {
				if (entry->handle == handle) {
					entry->registered = false;
					removed = true;
				} else {
					next->push_back(entry);
				}
			}
			if (removed)
				entries_ = std::move(next);
			return removed;
		}

		// O(1) snapshot, rebuilt only on registration changes. A callback may
		// unregister itself/another panel without invalidating the current draw.
		[[nodiscard]] std::shared_ptr<const Entries> Snapshot() const { return entries_; }
		void Shutdown()
		{
			shutdown_ = true;
			for (const auto& entry : *entries_)
				entry->registered = false;
			entries_ = std::make_shared<const Entries>();
		}

	private:
		std::shared_ptr<const Entries> entries_ = std::make_shared<const Entries>();
		Handle nextHandle_ = 0;
		bool shutdown_ = false;
	};
}
