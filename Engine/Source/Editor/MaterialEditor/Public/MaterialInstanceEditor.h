// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "MaterialEditor.h"
class FMaterialInstanceEditor
{
  public:
	static TSharedPtr<SWindow> Open(const FMaterialEditorArgs&, FString& Error);
};
