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

#include "stdafx.h"
#include "MfcServices.h"
#include "MainFrm.h"
#include "ProcWnd.h"

#include "CoreMinimal.h"
#include "Misc/App.h"
#include "Services/Services.h"

// The editor builds with the multibyte character set, so MFC strings are in
// the ANSI code page while the services use UTF-8.

namespace {

// ========== Settings ==========

class FMfcConfigStore : public IConfigStore {
public:
	bool GetString(const FString &section, const FString &key, FString &outValue) const override {
		// GetProfileString can't tell a missing value from its default, so
		// ask with a default no real setting has.
		static const char *missing = "\x01<missing>\x01";
		CString value = AfxGetApp()->GetProfileString(section.ToAnsi().c_str(), key.ToAnsi().c_str(), missing);
		if(value == missing)
			return false;
		outValue = FString::FromAnsi((LPCTSTR)value);
		return true;
	}

	bool GetInt(const FString &section, const FString &key, int32 &outValue) const override {
		const int missing = INT_MIN;
		int value = (int)AfxGetApp()->GetProfileInt(section.ToAnsi().c_str(), key.ToAnsi().c_str(), missing);
		if(value == missing)
			return false;
		outValue = value;
		return true;
	}

	void SetString(const FString &section, const FString &key, const FString &value) override {
		AfxGetApp()->WriteProfileString(section.ToAnsi().c_str(), key.ToAnsi().c_str(), value.ToAnsi().c_str());
	}

	void SetInt(const FString &section, const FString &key, int32 value) override {
		AfxGetApp()->WriteProfileInt(section.ToAnsi().c_str(), key.ToAnsi().c_str(), value);
	}
};

// ========== Message boxes ==========

class FMfcMessageDialog : public IMessageDialog {
public:
	EMessageResult Show(EMessageType type, EMessageButtons buttons,
						const FString &text, const FString &caption) override {
		UINT style = 0;
		switch(buttons) {
		case EMessageButtons::Ok:			style |= MB_OK; break;
		case EMessageButtons::OkCancel:		style |= MB_OKCANCEL; break;
		case EMessageButtons::YesNo:		style |= MB_YESNO; break;
		case EMessageButtons::YesNoCancel:	style |= MB_YESNOCANCEL; break;
		}

		// Errors have no owner and stay on top, so they show even before the
		// main window does (as LError/LFatal always did).
		HWND owner = NULL;
		switch(type) {
		case EMessageType::Error:
			style |= MB_ICONERROR | MB_TOPMOST;
			break;
		case EMessageType::Warning:
			style |= MB_ICONWARNING;
			break;
		case EMessageType::Info:
		case EMessageType::Question:
			if(AfxGetMainWnd())
				owner = AfxGetMainWnd()->GetSafeHwnd();
			break;
		}

		const FString &title = caption.IsEmpty() ? FApp::GetName() : caption;
		switch(::MessageBoxW(owner, text.ToWide().c_str(), title.ToWide().c_str(), style)) {
		case IDOK:		return EMessageResult::Ok;
		case IDYES:		return EMessageResult::Yes;
		case IDNO:		return EMessageResult::No;
		default:		return EMessageResult::Cancel;
		}
	}
};

// ========== Progress ==========

class FMfcProgressTask : public IProgressTask {
public:
	FMfcProgressTask(const FProgressOptions &options)
		: progressWnd(AfxGetMainWnd(), options.Title.ToAnsi().c_str()),
		  busyCursor(options.bBusyCursor) {
		if(!options.Text.IsEmpty())
			progressWnd.SetText("%s", options.Text.ToAnsi().c_str());
		if(!options.bCancellable)
			progressWnd.NoCancelButton();
		if(busyCursor)
			SetCursor(LoadCursor(NULL, IDC_WAIT));
	}

	~FMfcProgressTask() override {
		progressWnd.Clear();
		if(busyCursor)
			SetCursor(LoadCursor(NULL, IDC_ARROW));
	}

	void SetText(const FString &text) override {
		progressWnd.SetText("%s", text.ToAnsi().c_str());
	}

	void SetPercent(int32 percent) override {
		progressWnd.SetPos(percent);
		progressWnd.PeekAndPump();
	}

	bool IsCancelled() override {
		return progressWnd.Cancelled() != FALSE;
	}

private:
	CProgressWnd progressWnd;
	bool busyCursor;
};

class FMfcProgress : public IProgress {
public:
	TUniquePtr<IProgressTask> Begin(const FProgressOptions &options) override {
		return MakeUnique<FMfcProgressTask>(options);
	}
};

// ========== Folder picker ==========

class FMfcFileDialogs : public IFileDialogs {
public:
	bool BrowseForFolder(const FString &title, FString &outPath) override {
		CDirDialog dirDialog;
		dirDialog.m_strTitle = title.ToAnsi().c_str();
		if(!dirDialog.DoBrowse())
			return false;
		outPath = FString::FromAnsi((LPCTSTR)dirDialog.m_strPath);
		return true;
	}
};

// ========== Output pane ==========

class FProcessWndLogSink : public ILogSink {
public:
	void Serialize(ELogVerbosity verbosity, const char *utf8Text) override {
		if(verbosity > ELogVerbosity::Display)
			return;

		// Messages written before the main frame exists only reach the
		// debugger output.
		FProcessWnd *pProcWnd = GMainFrame ? GMainFrame->GetProcessWnd() : NULL;
		if(!pProcWnd || !pProcWnd->GetSafeHwnd())
			return;

		std::string text = FString(utf8Text).ToAnsi();
		pProcWnd->OutputText(text.data());
		pProcWnd->GetViewPos();
		pProcWnd->SetViewPos();
	}
};

FMfcConfigStore configStore;
FMfcMessageDialog messageDialog;
FMfcProgress progress;
FMfcFileDialogs fileDialogs;
FProcessWndLogSink logSink;

} // namespace

void RegisterMfcServices(void) {
	FApp::SetName(FString::FromAnsi(AfxGetAppName()));
	FApp::SetRequestExitHandler([] {
		CWnd *pMainWnd = AfxGetMainWnd();
		if(pMainWnd)
			pMainWnd->PostMessage(WM_QUIT);
		else
			PostQuitMessage(0);
	});

	FServices::SetConfig(&configStore);
	FServices::SetMessageDialog(&messageDialog);
	FServices::SetProgress(&progress);
	FServices::SetFileDialogs(&fileDialogs);
	GLog->AddSink(&logSink);
}

void UnregisterMfcServices(void) {
	GLog->RemoveSink(&logSink);
	FServices::SetFileDialogs(NULL);
	FServices::SetProgress(NULL);
	FServices::SetMessageDialog(NULL);
	FServices::SetConfig(NULL);
	FApp::SetRequestExitHandler(NULL);
}
