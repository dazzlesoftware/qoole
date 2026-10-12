/*
Copyright (C) 2026 Dazzle Software, LLC

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// Precompiled header of the MapCore library (Basic, Objects, Render, Game).
// It replaces res/StdAfx.h for these folders and must stay free of MFC and
// Windows headers: user interaction goes through Core's FServices.

#pragma once

#include <ctype.h>
#include <direct.h>
#include <float.h>
#include <io.h>
#include <limits.h>
#include <malloc.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "CoreMinimal.h"
#include "HAL/File.h"
#include "Misc/App.h"
#include "Services/Services.h"
