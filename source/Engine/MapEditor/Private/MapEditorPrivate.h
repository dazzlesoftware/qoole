// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Map core headers and small conversions shared by the MapEditor module.

#pragma once

#include "CoreMinimal.h"

#include "MapCoreHeaders.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMapEditor, Log);

namespace MapEditorPrivate
{
	inline TArray<Object*> ToArray(const LinkList<ObjectPtr>& List)
	{
		TArray<Object*> Result;
		Result.reserve(static_cast<SIZE_T>(List.NumOfElm()));
		for (int Index = 0; Index < List.NumOfElm(); ++Index)
		{
			Result.push_back(List[Index].GetPtr());
		}
		return Result;
	}

	// The caller owns the list (its destructor frees the nodes).
	inline void ToList(const TArray<Object*>& Objects, LinkList<ObjectPtr>& OutList)
	{
		OutList.DeleteAllNodes();
		for (Object* Obj : Objects)
		{
			OutList.AppendNode(*(new ObjectPtr(Obj)));
		}
	}

	// Bounds of an object in its parent's space (the corners of its own
	// bounding box, transformed).
	void GetObjectBounds(Object& Obj, Vector3d& OutMin, Vector3d& OutMax);
}
