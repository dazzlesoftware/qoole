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

#include "Misc/App.h"
#include "Windows/WindowsPlatform.h"

FString FApp::GetExecutableDir()
{
	std::wstring Path(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD Length = GetModuleFileNameW(nullptr, Path.data(), static_cast<DWORD>(Path.size()));
		if (Length == 0)
			return FString();
		if (Length < Path.size())
		{
			Path.resize(Length);
			break;
		}
		Path.resize(Path.size() * 2);
	}

	const size_t Slash = Path.find_last_of(L"\\/");
	if (Slash != std::wstring::npos)
		Path.resize(Slash);
	return FString::FromWide(Path);
}
