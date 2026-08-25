#include "ActorUtils.h"
#include <algorithm>
#include <cmath>

namespace Util
{
	bool GetDetailedShapeBound(RE::bhkNiCollisionObject* collisionObj, DetailedShapeBound& outBound)
	{
		outBound = {};
		if (!collisionObj)
			return false;

		auto* bhkRigid = collisionObj->body.get() ? collisionObj->body.get()->AsBhkRigidBody() : nullptr;
		auto* hkpRigid = bhkRigid ? skyrim_cast<RE::hkpRigidBody*>(bhkRigid->referencedObject.get()) : nullptr;
		if (!hkpRigid)
			return false;

		const RE::hkpShape* shape = hkpRigid->collidable.GetShape();
		auto* motionState = hkpRigid->GetMotionState();
		if (!shape || !motionState)
			return false;

		RE::hkAabb aabb{};
		shape->GetAabbImpl(motionState->transform, 0.0f, aabb);

		float minV[4]{};
		float maxV[4]{};
		_mm_storeu_ps(minV, aabb.min.quad);
		_mm_storeu_ps(maxV, aabb.max.quad);

		const float invScale = RE::bhkWorld::GetWorldScaleInverse();
		const RE::NiPoint3 minP{ minV[0] * invScale, minV[1] * invScale, minV[2] * invScale };
		const RE::NiPoint3 maxP{ maxV[0] * invScale, maxV[1] * invScale, maxV[2] * invScale };

		const bool finite =
			std::isfinite(minP.x) && std::isfinite(minP.y) && std::isfinite(minP.z) &&
			std::isfinite(maxP.x) && std::isfinite(maxP.y) && std::isfinite(maxP.z);
		if (!finite || maxP.x < minP.x || maxP.y < minP.y || maxP.z < minP.z)
			return false;

		outBound.center = RE::NiPoint3{
			(minP.x + maxP.x) * 0.5f,
			(minP.y + maxP.y) * 0.5f,
			(minP.z + maxP.z) * 0.5f
		};
		outBound.halfExtents = RE::NiPoint3{
			(maxP.x - minP.x) * 0.5f,
			(maxP.y - minP.y) * 0.5f,
			(maxP.z - minP.z) * 0.5f
		};

		const float hx = std::max(outBound.halfExtents.x, 0.0f);
		const float hy = std::max(outBound.halfExtents.y, 0.0f);
		const float hz = std::max(outBound.halfExtents.z, 0.0f);
		outBound.boundingRadius = std::sqrt(hx * hx + hy * hy + hz * hz);
		outBound.horizontalRadius = std::sqrt(hx * hx + hy * hy);
		outBound.identity = reinterpret_cast<std::uintptr_t>(hkpRigid);
		outBound.shapeType = shape->type;

		return
			std::isfinite(outBound.boundingRadius) &&
			std::isfinite(outBound.horizontalRadius) &&
			outBound.boundingRadius > 0.0f;
	}

	bool GetShapeBound(RE::bhkNiCollisionObject* collisionObj, RE::NiPoint3& centerPos, float& radius)
	{
		if (!collisionObj)
			return false;

		RE::bhkRigidBody* bhkRigid = collisionObj->body.get() ? collisionObj->body.get()->AsBhkRigidBody() : nullptr;
		RE::hkpRigidBody* hkpRigid = bhkRigid ? skyrim_cast<RE::hkpRigidBody*>(bhkRigid->referencedObject.get()) : nullptr;
		if (bhkRigid && hkpRigid && !skyrim_cast<RE::hkpListShape*>(hkpRigid)) {  // Ignore hkpListShape, unsupported
			RE::hkVector4 massCenter;
			bhkRigid->GetCenterOfMassWorld(massCenter);
			float massTrans[4];
			// Use unaligned store to avoid UB from potential stack misalignment
			_mm_storeu_ps(massTrans, massCenter.quad);
			centerPos = RE::NiPoint3(massTrans[0], massTrans[1], massTrans[2]) * RE::bhkWorld::GetWorldScaleInverse();
			return Util::ExtractShapeBound(hkpRigid->collidable.GetShape(), radius);
		}
		return false;
	}

	bool ExtractShapeBound(const RE::hkpShape* shape, float& radius)
	{
		using ShapeType = RE::hkpShapeType;
		if (!shape)
			return false;

		// Helpers to avoid repeating projection math and ensure offset-invariant half-extents
		auto project = [shape](float x, float y, float z) {
			return shape->GetMaximumProjection(RE::hkVector4{ x, y, z, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
		};
		auto symmetricHalfExtents = [&project](float& hx, float& hy, float& hz) {
			float x_pos = project(1.0f, 0.0f, 0.0f);
			float x_neg = project(-1.0f, 0.0f, 0.0f);
			float y_pos = project(0.0f, 1.0f, 0.0f);
			float y_neg = project(0.0f, -1.0f, 0.0f);
			float z_pos = project(0.0f, 0.0f, 1.0f);
			float z_neg = project(0.0f, 0.0f, -1.0f);
			hx = 0.5f * (x_pos - x_neg);
			hy = 0.5f * (y_pos - y_neg);
			hz = 0.5f * (z_pos - z_neg);
		};
		auto halfDiagonal = [](float hx, float hy, float hz) {
			return sqrtf(hx * hx + hy * hy + hz * hz);
		};
		if (shape->type == ShapeType::kCapsule) {
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			// For capsules, use the maximum half-extent (typically hz for vertical orientation)
			// as the farthest point lies along the capsule's main axis, not at the diagonal
			radius = std::max(hx, std::max(hy, hz));
			return true;
		} else if (shape->type == ShapeType::kSphere) {
			// For spheres, any axis should yield the same half-extent; use symmetric X
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			radius = hx;
			return true;
		} else if (shape->type == ShapeType::kBox) {
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			radius = halfDiagonal(hx, hy, hz);
			return true;
		} else if (shape->type == ShapeType::kCylinder) {
			// Use symmetric half-extents; cylinder radius is max of X/Y half-extents
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			float hr = std::max(hx, hy);
			radius = sqrtf(hr * hr + hz * hz);
			return true;
		} else if (shape->type == ShapeType::kConvexVertices || shape->type == ShapeType::kTriangle) {
			// Offset-invariant estimate: take symmetric half-extents per axis and use the max
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			radius = std::max(hx, std::max(hy, hz));
			return true;
		} else {
			// Fallback: mirror the convex/triangle approach for consistency
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			radius = std::max(hx, std::max(hy, hz));
			return true;
		}
	}
}
