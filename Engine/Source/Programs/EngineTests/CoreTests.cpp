// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "TestHelpers.h"
#include "Templates/SharedPointer.h"
#include "HAL/PlatformMisc.h"
#include <cstdlib>
#include <filesystem>

TEST_SUITE("Core")
{
	TEST_CASE("FString UTF-8 and UTF-16 round trip")
	{
		const FString Text = "Hello नमस्ते 😀";
		CHECK(FUtf8::CountCodePoints(Text.View()) == 14);
		const std::wstring Wide = Text.ToWide();
		CHECK(Wide.size() == 15); // the emoji is a surrogate pair
		CHECK(FString::FromWide(Wide) == Text);
	}

	TEST_CASE("FString helpers")
	{
		const FString S = "  alpha,beta,,gamma ";
		CHECK(S.TrimStartAndEnd() == "alpha,beta,,gamma");
		const TArray<FString> Parts = S.TrimStartAndEnd().ParseIntoArray(",");
		REQUIRE(Parts.size() == 3);
		CHECK(Parts[2] == "gamma");
		CHECK(S.TrimStartAndEnd().ParseIntoArray(",", false).size() == 4);
		CHECK(FString("abc").Mid(1) == "bc");
		CHECK(FString("abc").Mid(5).IsEmpty());
		CHECK(FString("Abc").ToUpper() == "ABC");
		CHECK(FString::Format("{}-{}", 1, FString("x")) == "1-x");
		CHECK(FString("abcabc").Find("c", 3) == 5);
	}

	TEST_CASE("FName interning")
	{
		const FName A("Panel");
		const FName B(FString("Panel"));
		const FName C("panel");
		CHECK(A == B);
		CHECK(A != C); // case-sensitive
		CHECK(A.View() == "Panel");
		CHECK(FName().IsNone());
		CHECK(FName("").IsNone());
	}

	TEST_CASE("Delegates")
	{
		struct FCounter
		{
			int32 Value = 0;
			void Add(int32 N)
			{
				Value += N;
			}
		};

		TSharedPtr<FCounter> Counter = std::make_shared<FCounter>();
		TMulticastDelegate<void(int32)> OnValue;
		int32 LambdaTotal = 0;
		const FDelegateHandle LambdaHandle = OnValue.AddLambda([&LambdaTotal](int32 N) { LambdaTotal += N; });
		OnValue.AddSP(Counter, &FCounter::Add);
		OnValue.Broadcast(3);
		CHECK(LambdaTotal == 3);
		CHECK(Counter->Value == 3);

		OnValue.Remove(LambdaHandle);
		Counter.reset(); // the SP binding unbinds itself
		OnValue.Broadcast(4);
		CHECK(LambdaTotal == 3);

		TDelegate<int32(int32)> Twice = TDelegate<int32(int32)>::CreateLambda([](int32 N) { return N * 2; });
		CHECK(Twice.Execute(21) == 42);
	}

	TEST_CASE("FTransform2D")
	{
		const FTransform2D Scale = FTransform2D::FromScale(2.0f);
		const FTransform2D Move = FTransform2D::FromTranslation({10.0f, 5.0f});
		const FTransform2D Both = Scale.Concatenate(Move); // scale, then move
		CHECK(Both.TransformPoint({1.0f, 1.0f}) == FVector2(12.0f, 7.0f));
		const FVector2 Back = Both.Inverse().TransformPoint({12.0f, 7.0f});
		CHECK(Back.X == doctest::Approx(1.0f));
		CHECK(Back.Y == doctest::Approx(1.0f));
		CHECK(Both.IsAxisAligned());
		CHECK_FALSE(FTransform2D::FromRotation(0.3f).IsAxisAligned());
	}

	TEST_CASE("FRect intersection")
	{
		const FRect A(0, 0, 10, 10);
		const FRect B(5, 5, 20, 20);
		CHECK(A.IntersectionWith(B) == FRect(5, 5, 10, 10));
		CHECK(A.IntersectionWith(FRect(20, 20, 30, 30)).IsEmpty());
		CHECK(A.Contains({0.0f, 0.0f}));
		CHECK_FALSE(A.Contains({10.0f, 5.0f}));
	}

	TEST_CASE("Colors")
	{
		const FColor C(128, 64, 255, 200);
		const FColor RoundTrip = C.ToLinear().ToFColorSRGB();
		CHECK(RoundTrip == C);
		CHECK(FColor::FromHex(0x336699) == FColor(0x33, 0x66, 0x99));
	}
}

TEST_SUITE("PlatformProcess")
{
	TEST_CASE("The running executable is found and programs start without waiting")
	{
		const FString Self = FPlatformProcess::ExecutablePath();
		CHECK(Self.EndsWith("EngineTests.exe", ESearchCase::IgnoreCase));
		CHECK(std::filesystem::exists(Self.ToWide()));
		FString Error;
		CHECK_FALSE(FPlatformProcess::CreateProc(Self + ".missing", {}, Error));
		CHECK_FALSE(Error.IsEmpty());
		const char* const SystemRoot = std::getenv("SystemRoot");
		REQUIRE(SystemRoot);
		const FString Shell = FString::Format("{}\\System32\\cmd.exe", SystemRoot);
		CHECK(FPlatformProcess::CreateProc(Shell, {"/c", "exit", "0"}, Error));
		CHECK(Error.IsEmpty());
	}
}
