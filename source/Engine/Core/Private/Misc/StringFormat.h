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

#include <cstdarg>
#include <cstdio>
#include <string>

namespace Genesis
{
	// vsnprintf into a std::string of whatever size the text needs.
	inline std::string FormatV(const char* Format, va_list Args)
	{
		if (!Format)
			return std::string();

		va_list Copy;
		va_copy(Copy, Args);
		const int Length = std::vsnprintf(nullptr, 0, Format, Copy);
		va_end(Copy);
		if (Length <= 0)
			return std::string();

		std::string Result(static_cast<size_t>(Length), '\0');
		std::vsnprintf(Result.data(), Result.size() + 1, Format, Args);
		return Result;
	}
}
