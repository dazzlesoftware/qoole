// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Compatibility include for the editor's remaining legacy object operations.
// Runtime geometry uses value helpers rather than Min/Max macros, so including
// these headers no longer imposes an order on Core/RHI headers. The remaining
// legacy headers are read at warning level 3 during migration.

#pragma once

#pragma warning(push, 3)

#include "Math3D.h"

#include "DrawBase.h"
#include "EntList.h"
#include "Entity.h"
#include "Game.h"
#include "GenesisGame.h"
#include "Geometry.h"
#include "LCommon.h"
#include "Mapio.h"
#include "Objects.h"
#include "Selector.h"
#include "Texture.h"
#include "View.h"

#pragma warning(pop)
