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

#pragma once

// Container aliases (docs/GenesisUI.md section 2). They are the standard
// containers under Unreal-style names, so the implementation can change later
// without touching call sites.

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

template<typename T>
using TArray = std::vector<T>;

template<typename K, typename V, typename Hash = std::hash<K>, typename Eq = std::equal_to<K>>
using TMap = std::unordered_map<K, V, Hash, Eq>;

template<typename T, typename Hash = std::hash<T>, typename Eq = std::equal_to<T>>
using TSet = std::unordered_set<T, Hash, Eq>;

template<typename Signature>
using TFunction = std::function<Signature>;
