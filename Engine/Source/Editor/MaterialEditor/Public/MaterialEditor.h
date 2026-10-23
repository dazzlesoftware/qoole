// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
#include "Widgets/WidgetDelegates.h"
class FRHIDevice;
class FSlateRHIRenderingPolicy;
class FSlateRHIRenderer;
class SWindow;

// The application supplies selection/transaction integration. MaterialEditor
// owns authoring, preview, compilation and saving, with no MapEditor dependency.
struct FMaterialEditorArgs
{
	std::shared_ptr<FAssetRegistry> Registry;
	FAssetReference Material;
	FString Name;
	FRHIDevice* Device = nullptr;
	FSlateRHIRenderingPolicy* Renderer = nullptr;
	FSlateRHIRenderer* WindowRenderer = nullptr;
	FSimpleDelegate OnSaved;
	TDelegate<void(int32)> OnApply;
	TDelegate<void(const FString&)> OnLog;
};
class FMaterialEditor
{
  public:
	static TSharedPtr<SWindow> Open(const FMaterialEditorArgs&, FString& Error);
};
