// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Small OS services Core itself needs: time, debugger, files, paths.

#pragma once

#include "Containers/String.h"

struct FPlatformTime
{
	// Seconds since an arbitrary fixed point, high resolution.
	static double Seconds();
};

struct FPlatformMisc
{
	static bool IsDebuggerPresent();
	static void DebugBreak();
	static void LowLevelOutputDebugString(std::string_view Text);
	static void Sleep(float Seconds);
};

struct FFileHelper
{
	static bool LoadFileToArray(TArray<uint8>& Out, const FString& Path);
	static bool LoadFileToString(FString& Out, const FString& Path);
	static bool SaveArrayToFile(TArrayView<const uint8> Data, const FString& Path);
	static bool FileExists(const FString& Path);
};

struct FPaths
{
	// Directory of the running executable, with a trailing slash.
	static FString ExecutableDir();
	// Joins with a single '/' between the parts.
	static FString Combine(const FString& A, const FString& B);
	static FString GetExtension(const FString& Path); // without the dot
	static FString GetCleanFilename(const FString& Path);
	static bool DirectoryExists(const FString& Path);
	// Finds the "Content" directory next to the executable or in one of its
	// parents (the source tree keeps it at Engine/Content, executables in
	// Engine/Binaries/Win64). Returns it with a trailing slash, or empty.
	static FString FindContentDir();
};
