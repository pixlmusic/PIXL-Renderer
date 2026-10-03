// PIXL Renderer - validated hook metadata and runtime compatibility reporting.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HookRegistry.h"

#include <algorithm>
#include <cstring>
#include <windows.h>

namespace PIXL::Renderer
{
	namespace
	{
		bool IsReadableRange(std::uintptr_t address, std::size_t size)
		{
			if (!address || !size || address > UINTPTR_MAX - size)
				return false;

			std::uintptr_t cursor = address;
			const auto end = address + size;
			while (cursor < end) {
				MEMORY_BASIC_INFORMATION info{};
				if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) ||
					info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
					return false;

				const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
				if (regionEnd <= cursor)
					return false;
				cursor = std::min(regionEnd, end);
			}
			return true;
		}
	}

	HookRegistry& HookRegistry::Get()
	{
		static HookRegistry instance;
		return instance;
	}

	void HookRegistry::Declare(HookDesc desc)
	{
		std::scoped_lock lock(mutex);
		auto [it, inserted] = records.try_emplace(desc.name);
		if (inserted) {
			it->second.desc = std::move(desc);
		} else {
			// Declaration is idempotent, but refreshed metadata lets a module add
			// detail before installation without erasing the current result.
			it->second.desc = std::move(desc);
		}
	}

	void HookRegistry::SetStatus(std::string_view name, HookStatus status, std::string detail, std::uintptr_t address)
	{
		std::scoped_lock lock(mutex);
		auto& record = records[std::string(name)];
		if (record.desc.name.empty())
			record.desc.name = name;
		record.status = status;
		record.detail = std::move(detail);
		record.resolvedAddress = address;
	}

	std::optional<HookRecord> HookRegistry::Find(std::string_view name) const
	{
		std::scoped_lock lock(mutex);
		const auto it = records.find(std::string(name));
		return it == records.end() ? std::nullopt : std::optional<HookRecord>(it->second);
	}

	std::vector<HookRecord> HookRegistry::Snapshot() const
	{
		std::scoped_lock lock(mutex);
		std::vector<HookRecord> result;
		result.reserve(records.size());
		for (const auto& [name, record] : records)
			result.push_back(record);
		std::ranges::sort(result, {}, [](const HookRecord& record) { return record.desc.name; });
		return result;
	}

	RuntimeCapabilities HookRegistry::GetRuntimeCapabilities() const
	{
		RuntimeCapabilities result;
		result.anniversaryEdition = REL::Module::IsAE();
		result.specialEdition = REL::Module::IsSE();
		result.virtualReality = REL::Module::IsVR();
		result.runtime = result.virtualReality ? "VR" : result.anniversaryEdition ? "AE" : result.specialEdition ? "SE" : "Unknown";
		result.version = REL::Module::get().version().string(".");
		return result;
	}

	bool HookRegistry::ValidateBytes(
		std::string_view name,
		std::uintptr_t address,
		std::span<const std::uint8_t> expected,
		std::span<const std::uint8_t> mask)
	{
		if (expected.empty() || (!mask.empty() && mask.size() != expected.size()) || !IsReadableRange(address, expected.size())) {
			SetStatus(name, HookStatus::RelocationMissing, "Address is unavailable, unreadable, or signature metadata is invalid", address);
			return false;
		}

		const auto* actual = reinterpret_cast<const std::uint8_t*>(address);
		for (std::size_t i = 0; i < expected.size(); ++i) {
			const auto compareMask = mask.empty() ? std::uint8_t{ 0xFF } : mask[i];
			if ((actual[i] & compareMask) != (expected[i] & compareMask)) {
				SetStatus(name, HookStatus::SignatureMismatch, "Executable bytes do not match the verified runtime signature", address);
				return false;
			}
		}

		SetStatus(name, HookStatus::Validated, "Verified executable signature", address);
		return true;
	}

	std::string_view HookRegistry::ToString(HookStatus status)
	{
		switch (status) {
		case HookStatus::Declared: return "DECLARED";
		case HookStatus::Validated: return "VALIDATED";
		case HookStatus::Installed: return "INSTALLED";
		case HookStatus::Disabled: return "DISABLED";
		case HookStatus::Unsupported: return "UNSUPPORTED";
		case HookStatus::SignatureMismatch: return "SIGNATURE_MISMATCH";
		case HookStatus::RelocationMissing: return "RELOCATION_MISSING";
		default: return "UNKNOWN";
		}
	}
}
