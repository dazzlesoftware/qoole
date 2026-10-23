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

// Modal message boxes, replacing direct MessageBox calls.

#include "CoreTypes.h"
#include "Containers/String.h"

enum class EMessageType : uint8
{
	Info,
	Warning,
	Error,
	Question,
};

enum class EMessageButtons : uint8
{
	Ok,
	OkCancel,
	YesNo,
	YesNoCancel,
};

enum class EMessageResult : uint8
{
	Ok,
	Cancel,
	Yes,
	No,
};

class IMessageDialog
{
public:
	virtual ~IMessageDialog() = default;

	// An empty caption means the application name (FApp::GetName()).
	virtual EMessageResult Show(
	    EMessageType Type, EMessageButtons Buttons, const FString& Text, const FString& Caption = FString()) = 0;
};

// Default without a UI: logs the message and answers with the choice that
// does nothing (Ok for Ok, otherwise Cancel or No).
class FLogMessageDialog : public IMessageDialog
{
public:
	EMessageResult Show(
	    EMessageType Type, EMessageButtons Buttons, const FString& Text, const FString& Caption = FString()) override;
};
