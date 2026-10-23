// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Internationalization/TextLocalization.h"

// Shared status for a bounded decode/validation operation. Private readers may
// return an empty value after failure; callers must check status before using
// a decoded value or committing output. The first diagnostic is preserved.
// Pass localized display text, or forward an already localized error.
class FValidationContext final
{
public:
	explicit FValidationContext(FString& InError) : Error(InError) {}
	bool Check(bool Condition, const FString& Message)
	{
		if (!Condition && bValid)
		{
			Error = Message;
			bValid = false;
		}
		return bValid;
	}
	bool Fail(const FString& Message)
	{
		return Check(false, Message);
	}
	bool IsValid() const
	{
		return bValid;
	}

private:
	FString& Error;
	bool bValid = true;
};
