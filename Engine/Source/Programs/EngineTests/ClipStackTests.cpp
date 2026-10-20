// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "TestHelpers.h"

#include "Rendering/DrawElements.h"

TEST_SUITE("ClipStack")
{
	TEST_CASE("Nested clips intersect and pop back")
	{
		FDrawElementList List;
		List.SetViewportSize({ 800, 600 });
		CHECK_FALSE(List.GetCurrentClip());

		List.PushClip(FPaintGeometry::Make({ 10.0f, 10.0f }, { 100.0f, 100.0f }, 1.0f));
		CHECK(*List.GetCurrentClip() == FIntRect(10, 10, 110, 110));
		List.AddSolidBox(0, FPaintGeometry::Make({ 20.0f, 20.0f }, { 10.0f, 10.0f }, 1.0f), FColor::Red());

		List.PushClip(FPaintGeometry::Make({ 50.0f, 0.0f }, { 200.0f, 80.0f }, 1.0f));
		CHECK(*List.GetCurrentClip() == FIntRect(50, 10, 110, 80));
		CHECK(List.GetClipDepth() == 2);
		List.AddSolidBox(0, FPaintGeometry::Make({ 60.0f, 20.0f }, { 10.0f, 10.0f }, 1.0f), FColor::Green());

		List.PopClip();
		CHECK(*List.GetCurrentClip() == FIntRect(10, 10, 110, 110));
		List.AddSolidBox(0, FPaintGeometry::Make({ 30.0f, 30.0f }, { 10.0f, 10.0f }, 1.0f), FColor::Blue());
		List.PopClip();
		CHECK_FALSE(List.GetCurrentClip());
		List.AddSolidBox(0, FPaintGeometry::Make({ 300.0f, 300.0f }, { 10.0f, 10.0f }, 1.0f), FColor::White());

		const TArray<FDrawElement>& Elements = List.GetElements();
		REQUIRE(Elements.size() == 4);
		CHECK(Elements[0].ClipIndex == 0);
		CHECK(Elements[1].ClipIndex == 1);
		CHECK(Elements[2].ClipIndex == 0);
		CHECK(Elements[3].ClipIndex == INDEX_NONE);
		CHECK(List.GetClipRects().size() == 2);
	}

	TEST_CASE("Clips are in pixels at the geometry's DPI scale")
	{
		FDrawElementList List;
		List.PushClip(FPaintGeometry::Make({ 10.0f, 10.0f }, { 100.0f, 50.0f }, 1.5f));
		CHECK(*List.GetCurrentClip() == FIntRect(15, 15, 165, 90));
	}

	TEST_CASE("Elements outside the clip are culled; disjoint clips cull everything")
	{
		FDrawElementList List;
		List.PushClip(FPaintGeometry::Make({ 0.0f, 0.0f }, { 50.0f, 50.0f }, 1.0f));
		List.AddSolidBox(0, FPaintGeometry::Make({ 100.0f, 100.0f }, { 10.0f, 10.0f }, 1.0f), FColor::Red());
		CHECK(List.GetElements().empty());
		CHECK(List.GetCulledCount() == 1);

		List.PushClip(FPaintGeometry::Make({ 200.0f, 200.0f }, { 50.0f, 50.0f }, 1.0f));
		CHECK(List.GetCurrentClip()->IsEmpty());
		List.AddSolidBox(0, FPaintGeometry::Make({ 10.0f, 10.0f }, { 10.0f, 10.0f }, 1.0f), FColor::Red());
		CHECK(List.GetElements().empty());
		List.PopClip();
		List.AddSolidBox(0, FPaintGeometry::Make({ 10.0f, 10.0f }, { 10.0f, 10.0f }, 1.0f), FColor::Red());
		CHECK(List.GetElements().size() == 1);
	}

	TEST_CASE("Batches carry the clip of their elements")
	{
		FDrawElementList List;
		const FPaintGeometry Box = FPaintGeometry::Make({ 10.0f, 10.0f }, { 10.0f, 10.0f }, 1.0f);
		List.AddSolidBox(0, Box, FColor::Red());
		List.PushClip(FPaintGeometry::Make({ 0.0f, 0.0f }, { 100.0f, 100.0f }, 1.0f));
		List.AddSolidBox(0, Box, FColor::Red());
		List.AddSolidBox(0, Box, FColor::Red());
		List.PopClip();
		List.AddSolidBox(0, Box, FColor::Red());

		FFakeResolver Resolver;
		FBatchedElements Out;
		FElementBatcher().Batch(List, nullptr, Resolver, Out);
		REQUIRE(Out.Batches.size() == 3);
		CHECK(Out.Batches[0].ClipIndex == INDEX_NONE);
		CHECK(Out.Batches[1].ClipIndex == 0);
		CHECK(Out.Batches[1].NumElements == 2);
		CHECK(Out.Batches[2].ClipIndex == INDEX_NONE);
		CHECK(Out.ClipRects[0] == FIntRect(0, 0, 100, 100));
	}
}
