// PIXL Renderer - shared hybrid reflection publication and diagnostics.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReflectionContext.h"
namespace PIXL::Renderer
{
	ReflectionContext& ReflectionContext::Get(){static ReflectionContext value;return value;}
	void ReflectionContext::Publish(ReflectionFrame next){std::scoped_lock lock(mutex);frame=std::move(next);}
	void ReflectionContext::Invalidate(){std::scoped_lock lock(mutex);frame={};}
	ReflectionFrame ReflectionContext::Acquire() const {std::scoped_lock lock(mutex);return frame;}
}
