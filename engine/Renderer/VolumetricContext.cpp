// PIXL Renderer - shared publication for atmosphere froxel resources.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VolumetricContext.h"

#include <utility>

namespace PIXL::Renderer
{
	VolumetricContext& VolumetricContext::Get()
	{
		static VolumetricContext value;
		return value;
	}

	void VolumetricContext::Publish(VolumetricFrame next)
	{
		std::scoped_lock lock(mutex);
		frame = std::move(next);
	}

	void VolumetricContext::Invalidate()
	{
		std::scoped_lock lock(mutex);
		frame = {};
	}

	VolumetricFrame VolumetricContext::Acquire() const
	{
		std::scoped_lock lock(mutex);
		return frame;
	}
}
