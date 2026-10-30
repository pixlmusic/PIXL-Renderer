#include "Renderer/TemporalMath.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using Matrix = std::array<float, 16>;

static void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

static Matrix MakeViewYaw(float radians)
{
	const float cosine = std::cos(radians);
	const float sine = std::sin(radians);
	return Matrix{ cosine, 0.0f, sine, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		-sine, 0.0f, cosine, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f };
}

int main()
{
	try {
		const Matrix identity = MakeViewYaw(0.0f);
		Require(!PIXL::Renderer::IsAbruptCameraRotation(identity, identity),
			"identical views must retain temporal history");
		Require(!PIXL::Renderer::IsAbruptCameraRotation(MakeViewYaw(0.20f), identity),
			"smooth pans must remain on per-pixel reprojection");
		Require(PIXL::Renderer::IsAbruptCameraRotation(MakeViewYaw(1.05f), identity),
			"large camera turns must invalidate stale history");

		Matrix invalid = identity;
		invalid[0] = std::numeric_limits<float>::quiet_NaN();
		Require(PIXL::Renderer::IsAbruptCameraRotation(invalid, identity),
			"non-finite view matrices must invalidate stale history");
		Require(PIXL::Renderer::IsAbruptCameraRotation(Matrix{}, identity),
			"uninitialized view matrices must invalidate stale history");

		std::cout << "Temporal rotation cut and invalid-matrix cases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
