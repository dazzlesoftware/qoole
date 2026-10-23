// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
class FMapDocument;
class Object;
class Landscape;

// After a landscape's heights changed (from Before): foliage standing on it
// moves up or down with the ground, as in Unreal. Inside the change's
// transaction, if one is open.
void SnapFoliageToLandscape(FMapDocument& Document, const Object& Land, const Landscape& Before);

