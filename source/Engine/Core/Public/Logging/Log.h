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

#pragma once

// Logging, replacing MFC's TRACE.
//
//   GLog->Logf(ELogVerbosity::Display, "Loading %s... ", Name);
//
// Text is UTF-8 and is passed to the sinks as written: no newline is added,
// so a message can be built from several calls. Every message also goes to
// the debugger output window (what TRACE did); applications add sinks to show
// it elsewhere, e.g. the editor's output pane.

#include "CoreTypes.h"

#include <mutex>
#include <vector>

enum class ELogVerbosity : uint8
{
	Fatal,
	Error,
	Warning,
	Display,	// Shown to the user, e.g. in the editor's output pane.
	Log,
	Verbose,	// Debugging detail (what TRACE was used for).
};

class ILogSink
{
public:
	virtual ~ILogSink() = default;
	virtual void Serialize(ELogVerbosity Verbosity, const char* Utf8Text) = 0;
};

class FLog
{
public:
	constexpr FLog() = default;
	FLog(const FLog&) = delete;
	FLog& operator=(const FLog&) = delete;

	// Sinks are not owned. Remove a sink before destroying it.
	void AddSink(ILogSink* Sink);
	void RemoveSink(ILogSink* Sink);

	void Log(ELogVerbosity Verbosity, const char* Utf8Text);
	void Logf(ELogVerbosity Verbosity, const char* Format, ...);

private:
	std::mutex Mutex;
	std::vector<ILogSink*> Sinks;
};

extern FLog* GLog;

const char* LexToString(ELogVerbosity Verbosity);
