// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
// Geometry first intentionally: its former Min/Max macros broke headers below.
#include <ostream>
#include "doctest.h"
#include "Geometry/Math3D.h"
#include "Renderer/SceneViewVolume.h"
#include "CoreMinimal.h"
#include "Renderer/WorldSceneDrawList.h"
#include "TestHelpers.h"
#include <type_traits>
#if defined(Min) || defined(Max)
#error Runtime geometry must not export Min/Max macros.
#endif
static_assert(std::is_same_v<decltype(GeometryMath::Min(1, 2.0f)), float>);
static_assert(GeometryMath::Min(2, 3) == 2 && GeometryMath::Max(2, 3) == 3);
TEST_SUITE("Geometry")
{
	TEST_CASE("Runtime geometry headers compose with Core and renderer in either order")
	{
		int A = 1, B = 2;
		CHECK(GeometryMath::Min(A++, B++) == 1);
		CHECK(A == 2);
		CHECK(B == 3);
		CHECK(GeometryMath::Max(3.0f, 2) == 3.0f);
		CHECK(FMath::Min(1, 2) == 1);
		CHECK(FMath::Max(1, 2) == 2);
		FSceneViewVolume View{Vector3d(-1, 1, -1), Vector3d(1, 100, 1), true};
		CHECK(View.IsValid());
	}
}
