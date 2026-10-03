// PIXL Renderer - validated hook metadata and runtime compatibility reporting.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PIXL::Renderer
{
	enum class HookStatus : std::uint8_t
	{
		Declared,
		Validated,
		Installed,
		Disabled,
		Unsupported,
		SignatureMismatch,
		RelocationMissing
	};

	struct HookDesc
	{
		std::string name;
		std::string owner;
		std::string relocation;
		std::string featureImpact;
		std::uint32_t patchSize{ 0 };
		bool required{ false };
	};

	struct HookRecord
	{
		HookDesc desc;
		HookStatus status{ HookStatus::Declared };
		std::uintptr_t resolvedAddress{ 0 };
		std::string detail;
	};

	struct RuntimeCapabilities
	{
		std::string runtime;
		std::string version;
		bool anniversaryEdition{ false };
		bool specialEdition{ false };
		bool virtualReality{ false };
	};

	class HookRegistry
	{
	public:
		static HookRegistry& Get();

		void Declare(HookDesc desc);
		void SetStatus(std::string_view name, HookStatus status, std::string detail = {}, std::uintptr_t address = 0);
		[[nodiscard]] std::optional<HookRecord> Find(std::string_view name) const;
		[[nodiscard]] std::vector<HookRecord> Snapshot() const;
		[[nodiscard]] RuntimeCapabilities GetRuntimeCapabilities() const;

		// Signature checks are opt-in: callers must supply bytes verified for the
		// active executable. The registry never invents signatures from another
		// runtime and fails without dereferencing unreadable memory.
		[[nodiscard]] bool ValidateBytes(
			std::string_view name,
			std::uintptr_t address,
			std::span<const std::uint8_t> expected,
			std::span<const std::uint8_t> mask = {});

		static std::string_view ToString(HookStatus status);

	private:
		mutable std::mutex mutex;
		std::unordered_map<std::string, HookRecord> records;
	};
}
