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
#include "Containers/ContainerAliases.h"

#include <atomic>

namespace
{
	FString& AppName()
	{
		static FString Name("Genesis Editor");
		return Name;
	}

	TFunction<void()>& ExitHandler()
	{
		static TFunction<void()> Handler;
		return Handler;
	}

	std::atomic<bool> bExitRequested = false;
} // namespace

const FString& FApp::GetName()
{
	return AppName();
}

void FApp::SetName(const FString& Name)
{
	AppName() = Name;
}

void FApp::RequestExit()
{
	bExitRequested = true;
	if (ExitHandler())
		ExitHandler()();
}

bool FApp::IsExitRequested()
{
	return bExitRequested;
}

void FApp::SetRequestExitHandler(TFunction<void()> Handler)
{
	ExitHandler() = std::move(Handler);
}
