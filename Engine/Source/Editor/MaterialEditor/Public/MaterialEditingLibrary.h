// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
class FMaterialEditingLibrary
{
  public:
	static FMaterialGraph CreateGraph(const FMaterialAsset&);
	static bool CompileMaterial(FMaterialAsset&, FString& Error);
};
