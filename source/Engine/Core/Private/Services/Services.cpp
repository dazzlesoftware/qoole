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

#include "Services/Services.h"
#include "Logging/Log.h"
#include "Misc/App.h"

// ========== Defaults ==========

namespace
{
	class FNullProgressTask : public IProgressTask
	{
	public:
		void SetText(const FString&) override {}
		void SetPercent(int32) override {}
		bool IsCancelled() override { return false; }
	};

	template<typename TInterface, typename TDefault>
	class TServiceSlot
	{
	public:
		TInterface& Get() { return Current ? *Current : Default; }
		void Set(TInterface* Service) { Current = Service; }

	private:
		TDefault Default;
		TInterface* Current = nullptr;
	};

	TServiceSlot<IConfigStore, FMemoryConfigStore>& ConfigSlot()
	{
		static TServiceSlot<IConfigStore, FMemoryConfigStore> Slot;
		return Slot;
	}

	TServiceSlot<IMessageDialog, FLogMessageDialog>& MessageDialogSlot()
	{
		static TServiceSlot<IMessageDialog, FLogMessageDialog> Slot;
		return Slot;
	}

	TServiceSlot<IProgress, FNullProgress>& ProgressSlot()
	{
		static TServiceSlot<IProgress, FNullProgress> Slot;
		return Slot;
	}

	TServiceSlot<IFileDialogs, FNullFileDialogs>& FileDialogsSlot()
	{
		static TServiceSlot<IFileDialogs, FNullFileDialogs> Slot;
		return Slot;
	}
}

EMessageResult FLogMessageDialog::Show(EMessageType Type, EMessageButtons Buttons,
									   const FString& Text, const FString& Caption)
{
	const ELogVerbosity Verbosity =
		Type == EMessageType::Error ? ELogVerbosity::Error :
		Type == EMessageType::Warning ? ELogVerbosity::Warning : ELogVerbosity::Display;
	GLog->Logf(Verbosity, "%s: %s\n", Caption.IsEmpty() ? *FApp::GetName() : *Caption, *Text);

	switch (Buttons)
	{
	case EMessageButtons::Ok:		return EMessageResult::Ok;
	case EMessageButtons::YesNo:	return EMessageResult::No;
	default:						return EMessageResult::Cancel;
	}
}

TUniquePtr<IProgressTask> FNullProgress::Begin(const FProgressOptions&)
{
	return MakeUnique<FNullProgressTask>();
}

bool FNullFileDialogs::BrowseForFolder(const FString&, FString&)
{
	return false;
}

// ========== FServices ==========

IConfigStore& FServices::GetConfig() { return ConfigSlot().Get(); }
void FServices::SetConfig(IConfigStore* Store) { ConfigSlot().Set(Store); }

IMessageDialog& FServices::GetMessageDialog() { return MessageDialogSlot().Get(); }
void FServices::SetMessageDialog(IMessageDialog* Dialog) { MessageDialogSlot().Set(Dialog); }

IProgress& FServices::GetProgress() { return ProgressSlot().Get(); }
void FServices::SetProgress(IProgress* Progress) { ProgressSlot().Set(Progress); }

IFileDialogs& FServices::GetFileDialogs() { return FileDialogsSlot().Get(); }
void FServices::SetFileDialogs(IFileDialogs* Dialogs) { FileDialogsSlot().Set(Dialogs); }
