// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Widgets/SCompoundWidget.h"
#include "MapFeaturePanelHost.h"
#include "MapLandscapeMode.h"
class SBox;

class SLandscapeEditorPanel final : public SCompoundWidget
{
  public:
	SLATE_BEGIN_ARGS(SLandscapeEditorPanel)
	SLATE_END_ARGS()
	void Construct(const FArguments&, FMapLandscapeMode* InMode, FMapFeaturePanelHost InHost);
	void RefreshLandscapeLayers();
	void EndInteraction()
	{
		Interaction.End(GetDocument());
	}

  private:
	TSharedRef<SWidget> MakePanel();
	bool PickLandscapeFile(bool bSave, const FString& Title, const FString& Name, FString& OutPath);
	bool PickLandscapeTexture(FString& OutPath);
	void RefreshLandscapeLayersNow();
	void ReportLandscapeFile(bool bOk, const FString& Done, const FString& Error);
	TSharedRef<SWidget> MakeLandscapeSplineSettings();
	TSharedRef<SWidget> MakeLandscapeLayerList();
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
	FMapLandscapeMode* Mode = nullptr;
	FMapFeaturePanelHost Host;
	FMapPanelTransaction Interaction;
	TWeakPtr<SBox> LandscapeLayerHost, LandscapeSplineHost;
	FString LandscapeLayerSignature;
	bool bLandscapeLayersQueued = false;
};
