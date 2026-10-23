// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Materials/MaterialGraph.h"
class FMaterialFunctionCodec
{
  public:
	static bool Encode(const FMaterialGraph&, FAssetPackage&, FString& Error);
	static bool Decode(const FAssetPackage&, FMaterialGraph&, FString& Error);
	// Resolves transitive sources atomically on the editor/tool thread. Runtime
	// compilation/rendering uses immutable embedded snapshots instead of IO.
	static bool Refresh(FMaterialGraph&, const FAssetRegistry&, bool& bChanged, FString& Error,
						const FString& OwnerId = {});
};
