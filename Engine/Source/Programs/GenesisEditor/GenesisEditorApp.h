// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Framework/Application/SlateApplication.h"
#include "CoreMinimal.h"
#include "ProjectDescriptor.h"
#include "AssetRegistry.h"
#include "MaterialEditor.h"
#include "EditorScriptExecution.h"
#include "HAL/File.h"
#include "MaterialEditingLibrary.h"
#include "MapAssetLibrary.h"
#include "GenericPlatform/PlatformDialogs.h"
#include "MapCoreHeaders.h"
#include "MapEditorShell.h"
#include "RHI.h"
#include "Misc/App.h"
#include "Services/ConfigStore.h"
#include "Services/Services.h"
#include "SOutputLog.h"
#include "Styling/StarshipCoreStyle.h"
#include "SlateRHIRenderingPolicy.h"
#include "SlateRHIRenderer.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <Windows.h>
#include <objbase.h>
#include <crtdbg.h>

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>

DECLARE_LOG_CATEGORY_EXTERN(LogGenesisEditor, Log);

class FMapEditorApp
{
public:
	virtual ~FMapEditorApp() = default;
	int32 Run(const FString& CommandLine);

protected:
	bool Initialize(const TArray<FString>& Args);
	void SetAppScale(float Scale);
	void Shutdown();

	virtual void OnInitialize(const TArray<FString>&) {}
	virtual void OnReady() {}
	virtual bool OnTick()
	{
		return false;
	}

	TSharedPtr<IPlatformApplication> Platform;
	TUniquePtr<FRHIDevice> Device;
	TSharedPtr<FSlateFontCache> FontCache;
	TUniquePtr<FSlateRHIRenderingPolicy> Renderer;
	TUniquePtr<FSlateRHIRenderer> WindowRenderer;
	TSharedPtr<FSlateApplication> App;
	TSharedPtr<SWindow> MainWindow;
	TSharedPtr<FMapEditorShell> Shell;
	TUniquePtr<FOutputDevice> FileLog;
	TUniquePtr<IConfigStore> Config;
	TUniquePtr<IMessageDialog> MessageDialog;
	TUniquePtr<IFileDialogs> FileDialogs;
	bool bEnvironmentStarted = false;

	ERHIBackend Backend = ERHIBackend::D3D12;
	float AppScale = 1.0f;
	FString SettingsDir;
	bool bInteractive = true;
};
