// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "GenericPlatform/PlatformDialogs.h"

#include "Misc/App.h"

#include <Windows.h>
#include <ShObjIdl.h>
#include <wrl/client.h>

#include <filesystem>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
	HWND GetOwner(void* ParentWindow)
	{
		return ParentWindow ? static_cast<HWND>(ParentWindow) : GetActiveWindow();
	}

	class FWindowsMessageDialog final : public IMessageDialog
	{
	public:
		EMessageResult Show(EMessageType Type, EMessageButtons Buttons, const FString& Text, const FString& Caption) override
		{
			UINT Flags = MB_TASKMODAL;
			switch (Type)
			{
			case EMessageType::Info: Flags |= MB_ICONINFORMATION; break;
			case EMessageType::Warning: Flags |= MB_ICONWARNING; break;
			case EMessageType::Error: Flags |= MB_ICONERROR; break;
			case EMessageType::Question: Flags |= MB_ICONQUESTION; break;
			}
			switch (Buttons)
			{
			case EMessageButtons::Ok: Flags |= MB_OK; break;
			case EMessageButtons::OkCancel: Flags |= MB_OKCANCEL; break;
			case EMessageButtons::YesNo: Flags |= MB_YESNO; break;
			case EMessageButtons::YesNoCancel: Flags |= MB_YESNOCANCEL; break;
			}
			const FString Title = Caption.IsEmpty() ? FApp::GetName() : Caption;
			switch (MessageBoxW(GetOwner(nullptr), Text.ToWide().c_str(), Title.ToWide().c_str(), Flags))
			{
			case IDOK: return EMessageResult::Ok;
			case IDYES: return EMessageResult::Yes;
			case IDNO: return EMessageResult::No;
			default: break;
			}
			return Buttons == EMessageButtons::Ok ? EMessageResult::Ok : EMessageResult::Cancel;
		}
	};

	// COM for the duration of a dialog (apartment-threaded, as the shell wants).
	struct FComScope
	{
		HRESULT Result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		~FComScope()
		{
			if (SUCCEEDED(Result))
			{
				CoUninitialize();
			}
		}
	};

	bool SetFolder(IFileDialog& Dialog, const std::wstring& Folder)
	{
		ComPtr<IShellItem> Item;
		if (Folder.empty() || FAILED(SHCreateItemFromParsingName(Folder.c_str(), nullptr, IID_PPV_ARGS(&Item))))
		{
			return false;
		}
		return SUCCEEDED(Dialog.SetFolder(Item.Get()));
	}

	bool RunFileDialog(bool bSave, const FFileDialogOptions& Options, FString& OutPath)
	{
		FComScope Com;
		ComPtr<IFileDialog> Dialog;
		if (FAILED(CoCreateInstance(bSave ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog))))
		{
			return false;
		}

		DWORD Flags = 0;
		Dialog->GetOptions(&Flags);
		Flags |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | (bSave ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
		Dialog->SetOptions(Flags);
		if (!Options.Title.IsEmpty())
		{
			Dialog->SetTitle(Options.Title.ToWide().c_str());
		}

		TArray<std::wstring> Strings;
		Strings.reserve(Options.Filters.size() * 2);
		TArray<COMDLG_FILTERSPEC> Specs;
		for (const FFileTypeFilter& Filter : Options.Filters)
		{
			Strings.push_back(Filter.Description.ToWide());
			Strings.push_back(Filter.Patterns.ToWide());
		}
		for (SIZE_T Index = 0; Index < Options.Filters.size(); ++Index)
		{
			Specs.push_back({ Strings[Index * 2].c_str(), Strings[Index * 2 + 1].c_str() });
		}
		if (!Specs.empty())
		{
			Dialog->SetFileTypes(static_cast<UINT>(Specs.size()), Specs.data());
			Dialog->SetFileTypeIndex(1);
		}
		if (!Options.DefaultExtension.IsEmpty())
		{
			Dialog->SetDefaultExtension(Options.DefaultExtension.ToWide().c_str());
		}

		if (!Options.InitialPath.IsEmpty())
		{
			const std::filesystem::path Initial(Options.InitialPath.ToWide());
			std::error_code Error;
			if (std::filesystem::is_directory(Initial, Error))
			{
				SetFolder(*Dialog.Get(), Initial.wstring());
			}
			else
			{
				SetFolder(*Dialog.Get(), Initial.parent_path().wstring());
				Dialog->SetFileName(Initial.filename().wstring().c_str());
			}
		}

		if (FAILED(Dialog->Show(GetOwner(Options.ParentWindow))))
		{
			return false; // cancelled
		}
		ComPtr<IShellItem> Result;
		PWSTR Path = nullptr;
		if (FAILED(Dialog->GetResult(&Result)) || FAILED(Result->GetDisplayName(SIGDN_FILESYSPATH, &Path)))
		{
			return false;
		}
		OutPath = FString::FromWide(Path);
		CoTaskMemFree(Path);
		return true;
	}

	class FWindowsFileDialogs final : public IFileDialogs
	{
	public:
		bool BrowseForFolder(const FString& Title, FString& OutPath) override
		{
			FComScope Com;
			ComPtr<IFileOpenDialog> Dialog;
			if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog))))
			{
				return false;
			}
			DWORD Flags = 0;
			Dialog->GetOptions(&Flags);
			Dialog->SetOptions(Flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
			Dialog->SetTitle(Title.ToWide().c_str());
			if (!OutPath.IsEmpty())
			{
				SetFolder(*Dialog.Get(), OutPath.ToWide());
			}
			if (FAILED(Dialog->Show(GetOwner(nullptr))))
			{
				return false;
			}
			ComPtr<IShellItem> Result;
			PWSTR Path = nullptr;
			if (FAILED(Dialog->GetResult(&Result)) || FAILED(Result->GetDisplayName(SIGDN_FILESYSPATH, &Path)))
			{
				return false;
			}
			OutPath = FString::FromWide(Path);
			CoTaskMemFree(Path);
			return true;
		}

		bool OpenFile(const FFileDialogOptions& Options, FString& OutPath) override
		{
			return RunFileDialog(false, Options, OutPath);
		}

		bool SaveFile(const FFileDialogOptions& Options, FString& OutPath) override
		{
			return RunFileDialog(true, Options, OutPath);
		}
	};
}

TUniquePtr<IMessageDialog> FPlatformDialogs::CreateMessageDialog()
{
	return std::make_unique<FWindowsMessageDialog>();
}

TUniquePtr<IFileDialogs> FPlatformDialogs::CreateFileDialogs()
{
	return std::make_unique<FWindowsFileDialogs>();
}
