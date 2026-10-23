// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Widgets/SCompoundWidget.h"
#include "MapFeaturePanelHost.h"
#include "MapFoliageMode.h"
class SBox;

class SFoliageEditorPanel final : public SCompoundWidget
{
  public:
	SLATE_BEGIN_ARGS(SFoliageEditorPanel)
	SLATE_END_ARGS()
	void Construct(const FArguments&, FMapFoliageMode* InMode, FMapFeaturePanelHost InHost);
	void RefreshFoliageTypes();
	void EndInteraction()
	{
		Interaction.End(GetDocument());
	}

  private:
	TSharedRef<SWidget> MakePanel();
	TSharedRef<SWidget> MakeFoliageTypeList();
	TSharedRef<SWidget> MakeFoliageTypeSettings();
	FMapDocument* GetDocument() const
	{
		return Host.GetDocument ? Host.GetDocument() : nullptr;
	}
	void BeginInteraction(const FString& Description)
	{
		Interaction.Begin(GetDocument(), Description);
	}
	void InvalidateViews()
	{
		if (Host.InvalidateViews)
			Host.InvalidateViews();
	}
	void Log(const FString& Message)
	{
		if (Host.Log)
			Host.Log(Message);
	}
	FString DefaultFolder() const
	{
		return Host.GetDefaultFolder ? Host.GetDefaultFolder() : FString();
	}
	FMapFoliageMode* Mode = nullptr;
	FMapFeaturePanelHost Host;
	FMapPanelTransaction Interaction;
	int32 SelectedFoliageType = 0;
	TWeakPtr<SBox> FoliageTypeHost, FoliageSettingsHost;
	FString FoliageSignature;
	bool bFoliageQueued = false;
};
