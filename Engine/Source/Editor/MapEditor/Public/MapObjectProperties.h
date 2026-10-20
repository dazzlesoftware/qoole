// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FMapObjectProperties: a map object as an IPropertySource for the property
// grid (the Properties panel). It replaces the MFC editor's property pages.
//
//   Object    Name, Position (scope space)
//   Transform Location, Rotation (roll, pitch, yaw in degrees) and Scale,
//             and Static Mesh, for static mesh actors (Unreal's details)
//   Surface   Texture (brushes and groups of brushes: every face)
//   Entity    Class, and the keys the entity set defines for the class,
//             typed from its definition: numbers, choices (typedef and
//             strdef), one checkbox per flag (flagdef), text otherwise;
//             keys the definition does not list come last, as text.
//
// Changes go through FMapDocument, so they are undoable; the property grid
// groups them into transactions (OnBeginTransaction/OnEndTransaction).

#pragma once

#include "CoreMinimal.h"
#include "Framework/PropertyGrid/PropertySource.h"
#include "MapDocument.h"

class Object;

class FMapObjectProperties final : public IPropertySource
{
public:
	FMapObjectProperties(FMapDocument& InDocument, Object& InObject) : Document(InDocument), Obj(InObject) {}

	Object& GetObject() const { return Obj; }

	FString GetDisplayName() const override;
	void GetProperties(TArray<FPropertyDesc>& OutProperties) const override;
	FPropertyValue GetValue(FName Property) const override;
	void SetValue(FName Property, const FPropertyValue& Value) override;

private:
	FMapDocument& Document;
	Object& Obj;
};
