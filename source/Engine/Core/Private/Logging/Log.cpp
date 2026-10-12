// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Logging/Log.h"

#include "HAL/PlatformMisc.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>

DEFINE_LOG_CATEGORY(LogCore, Log);

const char* ToString(ELogVerbosity Verbosity)
{
	switch (Verbosity)
	{
	case ELogVerbosity::Fatal: return "Fatal";
	case ELogVerbosity::Error: return "Error";
	case ELogVerbosity::Warning: return "Warning";
	case ELogVerbosity::Display: return "Display";
	case ELogVerbosity::Log: return "Log";
	case ELogVerbosity::Verbose: return "Verbose";
	case ELogVerbosity::VeryVerbose: return "VeryVerbose";
	}
	return "?";
}

namespace
{
	std::string FormatLine(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message)
	{
		// Messages that already end a line (e.g. the map core's progress
		// text) don't get a second line break.
		if (Message.ends_with('\n'))
		{
			Message.remove_suffix(1);
		}
		if (Verbosity == ELogVerbosity::Log || Verbosity == ELogVerbosity::Display)
		{
			return std::format("{}: {}\n", Category.Name, Message);
		}
		return std::format("{}: {}: {}\n", Category.Name, ToString(Verbosity), Message);
	}

	class FDebuggerOutputDevice final : public FOutputDevice
	{
	public:
		void Serialize(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message) override
		{
			FPlatformMisc::LowLevelOutputDebugString(FormatLine(Category, Verbosity, Message));
		}
	};

	class FStdErrOutputDevice final : public FOutputDevice
	{
	public:
		void Serialize(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message) override
		{
			const std::string Line = FormatLine(Category, Verbosity, Message);
			std::fwrite(Line.data(), 1, Line.size(), stderr);
			std::fflush(stderr);
		}
	};

	struct FLogState
	{
		std::recursive_mutex Mutex;
		FDebuggerOutputDevice Debugger;
		FStdErrOutputDevice StdErr;
		bool bDefaultDevices = true;
		TArray<FOutputDevice*> Devices;
	};

	FLogState& GetState()
	{
		static FLogState State;
		return State;
	}
}

void FLog::Write(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message)
{
	FLogState& State = GetState();
	{
		std::scoped_lock Lock(State.Mutex);
		if (State.bDefaultDevices || Verbosity == ELogVerbosity::Fatal)
		{
			State.Debugger.Serialize(Category, Verbosity, Message);
			State.StdErr.Serialize(Category, Verbosity, Message);
		}
		for (FOutputDevice* Device : State.Devices)
		{
			Device->Serialize(Category, Verbosity, Message);
		}
	}

	if (Verbosity == ELogVerbosity::Fatal)
	{
		if (FPlatformMisc::IsDebuggerPresent())
		{
			FPlatformMisc::DebugBreak();
		}
		std::abort();
	}
}

void FLog::AddDevice(FOutputDevice* Device)
{
	FLogState& State = GetState();
	std::scoped_lock Lock(State.Mutex);
	State.Devices.push_back(Device);
}

void FLog::RemoveDevice(FOutputDevice* Device)
{
	FLogState& State = GetState();
	std::scoped_lock Lock(State.Mutex);
	ArrayUtil::Remove(State.Devices, Device);
}

void FLog::SetDefaultDevicesEnabled(bool bEnabled)
{
	FLogState& State = GetState();
	std::scoped_lock Lock(State.Mutex);
	State.bDefaultDevices = bEnabled;
}
