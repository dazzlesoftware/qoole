// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "CoreMinimal.h"
class FMapDocument;

// Application services used by feature panels. No GenesisEditor shell dependency.
// The host and mode must outlive their panel; resolve the document on demand.
struct FMapFeaturePanelHost
{
	TFunction<FMapDocument*()> GetDocument;
	TFunction<void()> InvalidateViews;
	TFunction<void(const FString&)> Log;
	TFunction<FString()> GetDefaultFolder;
};

// A slider/color interaction may join an existing transaction, but only closes
// the exact transaction it opened. Resolve current documents before dereferencing.
class FMapPanelTransaction
{
  public:
	void Begin(FMapDocument* Current, const FString& Description);
	void End(FMapDocument* Current);

  private:
	FMapDocument* Owner = nullptr;
	uint64 Id = 0;
};
