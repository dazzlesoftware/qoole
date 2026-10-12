// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The operating system's own message boxes and file pickers, as the Core
// services (FServices::SetMessageDialog / SetFileDialogs). Programs that
// want them register them at startup:
//
//   TUniquePtr<IMessageDialog> Messages = FPlatformDialogs::CreateMessageDialog();
//   FServices::SetMessageDialog(Messages.get());
//
// They are modal to the given or the active window and must be shown from
// the thread that owns the windows.

#pragma once

#include "CoreMinimal.h"
#include "Services/FileDialogs.h"
#include "Services/MessageDialog.h"

struct FPlatformDialogs
{
	// Null on platforms without native dialogs.
	static TUniquePtr<IMessageDialog> CreateMessageDialog();
	static TUniquePtr<IFileDialogs> CreateFileDialogs();
};
