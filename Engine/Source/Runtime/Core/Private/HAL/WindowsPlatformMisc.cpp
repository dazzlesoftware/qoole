// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "HAL/PlatformMisc.h"
#include "Internationalization/TextLocalization.h"
#include "Math/MathUtility.h"

#include <Windows.h>

#include <cstdio>

double FPlatformTime::Seconds()
{
	static const double SecondsPerCycle = []
	{
		LARGE_INTEGER Frequency;
		QueryPerformanceFrequency(&Frequency);
		return 1.0 / static_cast<double>(Frequency.QuadPart);
	}();
	LARGE_INTEGER Counter;
	QueryPerformanceCounter(&Counter);
	return static_cast<double>(Counter.QuadPart) * SecondsPerCycle;
}

bool FPlatformMisc::IsDebuggerPresent()
{
	return ::IsDebuggerPresent() != FALSE;
}

void FPlatformMisc::DebugBreak()
{
	__debugbreak();
}

void FPlatformMisc::LowLevelOutputDebugString(std::string_view Text)
{
	::OutputDebugStringW(FString(Text).ToWide().c_str());
}

void FPlatformMisc::Sleep(float Seconds)
{
	::Sleep(static_cast<DWORD>(FMath::Max(0.0f, Seconds) * 1000.0f));
}

bool FFileHelper::LoadFileToArray(TArray<uint8>& Out, const FString& Path)
{
	Out.clear();
	FILE* File = _wfopen(Path.ToWide().c_str(), L"rb");
	if (!File)
	{
		return false;
	}
	_fseeki64(File, 0, SEEK_END);
	const int64 Size = _ftelli64(File);
	_fseeki64(File, 0, SEEK_SET);
	bool bOk = Size >= 0;
	if (bOk)
	{
		Out.resize(static_cast<SIZE_T>(Size));
		bOk = Size == 0 || std::fread(Out.data(), 1, Out.size(), File) == Out.size();
	}
	std::fclose(File);
	if (!bOk)
	{
		Out.clear();
	}
	return bOk;
}

bool FFileHelper::LoadFileToString(FString& Out, const FString& Path)
{
	TArray<uint8> Bytes;
	if (!LoadFileToArray(Bytes, Path))
	{
		return false;
	}
	Out = FString(reinterpret_cast<const char*>(Bytes.data()), static_cast<int32>(Bytes.size()));
	return true;
}

bool FFileHelper::SaveArrayToFile(TArrayView<const uint8> Data, const FString& Path)
{
	FILE* File = _wfopen(Path.ToWide().c_str(), L"wb");
	if (!File)
	{
		return false;
	}
	const bool bOk = Data.empty() || std::fwrite(Data.data(), 1, Data.size(), File) == Data.size();
	return std::fclose(File) == 0 && bOk;
}

bool FFileHelper::FileExists(const FString& Path)
{
	const DWORD Attributes = GetFileAttributesW(Path.ToWide().c_str());
	return Attributes != INVALID_FILE_ATTRIBUTES && !(Attributes & FILE_ATTRIBUTE_DIRECTORY);
}

FString FPaths::ExecutableDir()
{
	wchar_t Buffer[MAX_PATH * 4];
	const DWORD Length = GetModuleFileNameW(nullptr, Buffer, static_cast<DWORD>(std::size(Buffer)));
	std::wstring Path(Buffer, Length);
	const SIZE_T Slash = Path.find_last_of(L"\\/");
	Path = Slash == std::wstring::npos ? std::wstring() : Path.substr(0, Slash + 1);
	for (wchar_t& C : Path)
	{
		if (C == L'\\') C = L'/';
	}
	return FString::FromWide(Path);
}

FString FPaths::Combine(const FString& A, const FString& B)
{
	if (A.IsEmpty()) return B;
	if (B.IsEmpty()) return A;
	const bool bASlash = A.EndsWith("/") || A.EndsWith("\\");
	const bool bBSlash = B.StartsWith("/") || B.StartsWith("\\");
	if (bASlash && bBSlash) return A + B.Mid(1);
	if (bASlash || bBSlash) return A + B;
	return A + "/" + B;
}

FString FPaths::GetCleanFilename(const FString& Path)
{
	const std::string_view View = Path.View();
	const SIZE_T Slash = View.find_last_of("\\/");
	return Slash == std::string_view::npos ? Path : FString(View.substr(Slash + 1));
}

FString FPaths::GetExtension(const FString& Path)
{
	const FString Name = GetCleanFilename(Path);
	const SIZE_T Dot = Name.View().find_last_of('.');
	return Dot == std::string_view::npos ? FString() : FString(Name.View().substr(Dot + 1));
}

bool FPaths::DirectoryExists(const FString& Path)
{
	const DWORD Attributes = GetFileAttributesW(Path.ToWide().c_str());
	return Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY);
}

FString FPaths::FindContentDir()
{
	FString Dir = ExecutableDir();
	for (int32 Level = 0; Level < 5 && !Dir.IsEmpty(); ++Level)
	{
		const FString Candidate = Combine(Dir, "Content/");
		if (DirectoryExists(Candidate))
		{
			return Candidate;
		}
		// Up one level: strip the trailing slash, then the last component.
		const std::string_view View = Dir.View().substr(0, Dir.View().size() - 1);
		const SIZE_T Slash = View.find_last_of("/\\");
		if (Slash == std::string_view::npos)
		{
			break;
		}
		Dir = FString(View.substr(0, Slash + 1));
	}
	return FString();
}

FString FPlatformProcess::ExecutablePath()
{
	std::wstring Buffer(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD Length = GetModuleFileNameW(nullptr, Buffer.data(), static_cast<DWORD>(Buffer.size()));
		if (Length == 0)
			return {};
		if (Length < Buffer.size())
		{
			Buffer.resize(Length);
			return FString::FromWide(Buffer);
		}
		Buffer.resize(Buffer.size() * 2);
	}
}

bool FPlatformProcess::CreateProc(const FString& Executable, const TArray<FString>& Arguments, FString& Error)
{
	std::wstring CommandLine = L"\"" + Executable.ToWide() + L"\"";
	for (const FString& Argument : Arguments)
	{
		const std::wstring Wide = Argument.ToWide();
		CommandLine += Wide.find_first_of(L" \t") == std::wstring::npos ? L" " + Wide : L" \"" + Wide + L"\"";
	}
	STARTUPINFOW Startup{};
	Startup.cb = sizeof(Startup);
	PROCESS_INFORMATION Process{};
	if (!CreateProcessW(Executable.ToWide().c_str(), CommandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
	        &Startup, &Process))
	{
		Error = FTextLocalization::Format(
		    "Could not start {} (error {}).", Executable, static_cast<uint32>(GetLastError()));
		return false;
	}
	CloseHandle(Process.hThread);
	CloseHandle(Process.hProcess);
	Error.Empty();
	return true;
}
