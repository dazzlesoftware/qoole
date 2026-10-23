// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Internationalization/TextLocalization.h"
#include "HAL/File.h"
#include "Serialization/Json.h"
#include <mutex>

namespace
{
	struct FLocalizationState
	{
		std::mutex Mutex;
		TMap<FString, FString> Messages;
	};

	FLocalizationState& GetLocalizationState()
	{
		static FLocalizationState State;
		return State;
	}
} // namespace

FString FTextLocalization::Get(std::string_view Source)
{
	FLocalizationState& State = GetLocalizationState();
	const std::lock_guard<std::mutex> Lock(State.Mutex);
	const TMap<FString, FString>::const_iterator Found = State.Messages.find(FString(Source));
	return Found == State.Messages.end() ? FString(Source) : Found->second;
}

bool FTextLocalization::LoadCatalog(const FString& Path, FString& Error)
{
	FString Contents;
	if (!FFile::ReadAllText(Path, Contents))
	{
		Error = GENESIS_TEXT("Cannot read the localization catalog.");
		return false;
	}

	Error.Empty();
	const TOptional<FJsonValue> Root = FJsonValue::Parse(Contents.View(), &Error);
	if (!Root || !Root->IsObject())
	{
		if (Error.IsEmpty())
		{
			Error = GENESIS_TEXT("The localization catalog must be a JSON object.");
		}
		return false;
	}

	TMap<FString, FString> Messages;
	for (const FString& Source : Root->GetKeys())
	{
		const FJsonValue& Value = Root->Get(Source.View());
		if (Value.GetType() != EJsonType::String || Source.IsEmpty())
		{
			Error = GENESIS_TEXT("Localization entries must have nonempty source strings and string translations.");
			return false;
		}
		Messages.emplace(Source, Value.AsString());
	}

	FLocalizationState& State = GetLocalizationState();
	const std::lock_guard<std::mutex> Lock(State.Mutex);
	State.Messages.swap(Messages);
	Error.Empty();
	return true;
}

void FTextLocalization::Reset()
{
	FLocalizationState& State = GetLocalizationState();
	const std::lock_guard<std::mutex> Lock(State.Mutex);
	State.Messages.clear();
}
