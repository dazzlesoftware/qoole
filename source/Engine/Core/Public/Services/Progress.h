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

// Progress reporting for long operations, replacing CProgressWnd.
//
//   TUniquePtr<IProgressTask> Task = FServices::GetProgress().Begin({ "Extracting" });
//   for (...) {
//       Task->SetPercent(Done * 100 / Total);
//       if (Task->IsCancelled())
//           break;
//   }
//   // The progress UI closes when Task is destroyed.

#include "CoreTypes.h"
#include "Containers/String.h"
#include "Templates/SharedPointer.h"

struct FProgressOptions
{
	FString Text;					// what is being done
	FString Title = "Progress";
	bool bCancellable = true;
	bool bBusyCursor = false;		// show the wait cursor while running
};

class IProgressTask
{
public:
	virtual ~IProgressTask() = default;

	virtual void SetText(const FString& Text) = 0;

	// Moves the bar (0 to 100) and processes pending UI events, so the
	// progress window repaints and can see a cancel request.
	virtual void SetPercent(int32 Percent) = 0;

	// Whether the user asked to cancel.
	virtual bool IsCancelled() = 0;
};

class IProgress
{
public:
	virtual ~IProgress() = default;
	virtual TUniquePtr<IProgressTask> Begin(const FProgressOptions& Options) = 0;
};

// Default without a UI: reports nothing and never cancels.
class FNullProgress : public IProgress
{
public:
	TUniquePtr<IProgressTask> Begin(const FProgressOptions& Options) override;
};
