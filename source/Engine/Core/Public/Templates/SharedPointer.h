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

// Smart pointer aliases (docs/GenesisUI.md section 2). TSharedRef, the
// non-null wrapper, arrives with the code that first needs it.

#include <memory>
#include <utility>

template<typename T>
using TSharedPtr = std::shared_ptr<T>;

template<typename T>
using TWeakPtr = std::weak_ptr<T>;

template<typename T>
using TUniquePtr = std::unique_ptr<T>;

template<typename T, typename... Args>
TSharedPtr<T> MakeShared(Args&&... InArgs)
{
	return std::make_shared<T>(std::forward<Args>(InArgs)...);
}

template<typename T, typename... Args>
TUniquePtr<T> MakeUnique(Args&&... InArgs)
{
	return std::make_unique<T>(std::forward<Args>(InArgs)...);
}
