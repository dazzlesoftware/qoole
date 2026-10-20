// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The map core headers the MapEditor module uses. Include this instead of
// them directly: Math3D.h defines Min and Max as macros, which would break
// FMath::Min/Max everywhere after it, so they are removed here (the map
// core headers do not use them in inline code). The map core is older code
// that does not build cleanly at warning level 4; its headers are read at
// level 3.

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

#undef Min
#undef Max
