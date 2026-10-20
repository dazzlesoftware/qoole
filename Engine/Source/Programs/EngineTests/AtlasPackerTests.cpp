// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "TestHelpers.h"

#include "Rendering/AtlasPacker.h"

#include <random>

namespace
{
	// Grows the rectangle by the packer padding on every side.
	FIntRect WithPadding(const FIntRect& R, int32 Padding)
	{
		return FIntRect(R.Left - Padding, R.Top - Padding, R.Right + Padding, R.Bottom + Padding);
	}
}

TEST_SUITE("AtlasPacker")
{
	TEST_CASE("Allocations stay inside the page and never overlap, padding included")
	{
		constexpr int32 Padding = 1;
		FAtlasPacker Packer(256, 256, Padding);
		std::mt19937 Random(1234);
		std::uniform_int_distribution<int32> Size(1, 24);

		TArray<FIntRect> Placed;
		for (int32 I = 0; I < 2000; ++I)
		{
			const int32 W = Size(Random);
			const int32 H = Size(Random);
			const TOptional<FIntRect> Slot = Packer.Allocate(W, H);
			if (!Slot)
			{
				continue;
			}
			CHECK(Slot->Width() == W);
			CHECK(Slot->Height() == H);
			const FIntRect Padded = WithPadding(*Slot, Padding);
			CHECK(Padded.Left >= 0);
			CHECK(Padded.Top >= 0);
			CHECK(Padded.Right <= 256);
			CHECK(Padded.Bottom <= 256);
			for (const FIntRect& Other : Placed)
			{
				REQUIRE_FALSE(Padded.Intersects(WithPadding(Other, Padding)));
			}
			Placed.push_back(*Slot);
		}
		// Skyline packing of small random rectangles fills most of the page.
		CHECK(Packer.GetOccupancy() > 0.7f);
		CHECK(Placed.size() > 100);
	}

	TEST_CASE("Full page and oversized requests fail; reset frees everything")
	{
		FAtlasPacker Packer(64, 64, 0);
		CHECK_FALSE(Packer.Allocate(65, 1));
		REQUIRE(Packer.Allocate(64, 32));
		REQUIRE(Packer.Allocate(64, 32));
		CHECK_FALSE(Packer.Allocate(1, 1));
		CHECK(Packer.GetOccupancy() == doctest::Approx(1.0f));
		Packer.Reset();
		CHECK(Packer.Allocate(64, 64));
	}

	TEST_CASE("Bottom-left placement fills rows before stacking")
	{
		FAtlasPacker Packer(100, 100, 0);
		const FIntRect A = *Packer.Allocate(40, 10);
		const FIntRect B = *Packer.Allocate(40, 10);
		const FIntRect C = *Packer.Allocate(40, 10); // does not fit the first row
		CHECK(A == FIntRect(0, 0, 40, 10));
		CHECK(B == FIntRect(40, 0, 80, 10));
		CHECK(C.Top == 10);
		const FIntRect D = *Packer.Allocate(20, 5); // fits the remaining strip of row one
		CHECK(D == FIntRect(80, 0, 100, 5));
	}
}
