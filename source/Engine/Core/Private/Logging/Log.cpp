/*
Copyright (C) 2026 Dazzle Software, LLC

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "Logging/Log.h"
#include "Misc/StringFormat.h"

#include <algorithm>

namespace Genesis::Private
{
	// Platform debugger output (OutputDebugString on Windows).
	void WriteToDebugger(const char* Utf8Text);
}

namespace
{
	FLog GLogInstance;
}

FLog* GLog = &GLogInstance;

void FLog::AddSink(ILogSink* Sink)
{
	if (!Sink)
		return;
	std::lock_guard Lock(Mutex);
	if (std::find(Sinks.begin(), Sinks.end(), Sink) == Sinks.end())
		Sinks.push_back(Sink);
}

void FLog::RemoveSink(ILogSink* Sink)
{
	std::lock_guard Lock(Mutex);
	Sinks.erase(std::remove(Sinks.begin(), Sinks.end(), Sink), Sinks.end());
}

void FLog::Log(ELogVerbosity Verbosity, const char* Utf8Text)
{
	if (!Utf8Text || !*Utf8Text)
		return;

	Genesis::Private::WriteToDebugger(Utf8Text);

	std::lock_guard Lock(Mutex);
	for (ILogSink* Sink : Sinks)
		Sink->Serialize(Verbosity, Utf8Text);
}

void FLog::Logf(ELogVerbosity Verbosity, const char* Format, ...)
{
	va_list Args;
	va_start(Args, Format);
	const std::string Text = Genesis::FormatV(Format, Args);
	va_end(Args);
	Log(Verbosity, Text.c_str());
}

const char* LexToString(ELogVerbosity Verbosity)
{
	switch (Verbosity)
	{
	case ELogVerbosity::Fatal:		return "Fatal";
	case ELogVerbosity::Error:		return "Error";
	case ELogVerbosity::Warning:	return "Warning";
	case ELogVerbosity::Display:	return "Display";
	case ELogVerbosity::Log:		return "Log";
	case ELogVerbosity::Verbose:	return "Verbose";
	}
	return "Unknown";
}
