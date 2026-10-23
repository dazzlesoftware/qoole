// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "MaterialEditor.h"
class FMaterialParameterCollectionEditor
{
  public:
	static TSharedPtr<SWindow> Open(const FMaterialEditorArgs&, FString& Error);
};
