// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Logging. Messages go to every registered FOutputDevice (by default the
// debugger output and stderr). Usage:
//
//   DECLARE_LOG_CATEGORY_EXTERN(LogRHI, Log);   // in a header
//   DEFINE_LOG_CATEGORY(LogRHI, Log);           // in one .cpp
//   GENESIS_LOG(LogRHI, Warning, "Swapchain {}x{} lost", Width, Height);
//
// Categories filter by verbosity at runtime; Fatal always breaks/aborts.

#pragma once

#include "Containers/String.h"
#include "Internationalization/TextLocalization.h"
#include "Misc/AssertionMacros.h"

enum class ELogVerbosity : uint8
{
	Fatal,
	Error,
	Warning,
	Display,
	Log,
	Verbose,
	VeryVerbose,
};

const char* ToString(ELogVerbosity Verbosity);

struct FLogCategory
{
	const char* Name;
	ELogVerbosity Verbosity; // messages more verbose than this are dropped

	bool IsSuppressed(ELogVerbosity Level) const
	{
		return Level > Verbosity;
	}
};

class FOutputDevice
{
public:
	virtual ~FOutputDevice() = default;
	virtual void Serialize(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message) = 0;
};

class FLog
{
public:
	static void Write(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message);

	// Devices are not owned. The defaults (debugger, stderr) can be removed
	// with SetDefaultDevicesEnabled(false), e.g. for quiet tests.
	static void AddDevice(FOutputDevice* Device);
	static void RemoveDevice(FOutputDevice* Device);
	static void SetDefaultDevicesEnabled(bool bEnabled);
};

#define DECLARE_LOG_CATEGORY_EXTERN(CategoryName, DefaultVerbosity) extern FLogCategory CategoryName
#define DEFINE_LOG_CATEGORY(CategoryName, DefaultVerbosity)                                                            \
	FLogCategory CategoryName                                                                                          \
	{                                                                                                                  \
		#CategoryName, ELogVerbosity::DefaultVerbosity                                                                 \
	}

#define GENESIS_LOG(Category, VerbosityName, Fmt, ...)                                                                 \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(Category).IsSuppressed(ELogVerbosity::VerbosityName))                                                    \
		{                                                                                                              \
			::FLog::Write(Category, ELogVerbosity::VerbosityName,                                                      \
			    ::FTextLocalization::Format(Fmt __VA_OPT__(, ) __VA_ARGS__).ToStdString());                            \
		}                                                                                                              \
	} while (0)

DECLARE_LOG_CATEGORY_EXTERN(LogCore, Log);
