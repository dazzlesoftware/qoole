// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "GameFramework/SaveGame.h"
#include "World/GameWorld.h"
#include "HAL/File.h"
#include "Internationalization/TextLocalization.h"
#include "Misc/ValidationContext.h"
#include <cmath>
#include <filesystem>

namespace
{
	constexpr const char* SaveSchema = "Genesis.SaveGame";
	constexpr double MaxAbsoluteCoordinate = 1.0e12;

	bool ReadNumber(FValidationContext& Validation, const FJsonValue& In, double Limit, double& Out)
	{
		const double Value = In.AsNumber();
		if (!Validation.Check(In.GetType() == EJsonType::Number && std::isfinite(Value) && std::abs(Value) <= Limit,
		        GENESIS_TEXT("Invalid saved game number.")))
			return false;
		Out = Value;
		return true;
	}
} // namespace

FJsonValue FSaveGame::ToJson() const
{
	FJsonValue Json = FJsonValue::MakeObject();
	Json["Schema"] = SaveSchema;
	Json["Version"] = Version;
	Json["World"] = World.ToJson();
	Json["Player"] = Player.ToJson();
	Json["Absolute"] = FJsonValue::MakeArray();
	Json["Absolute"].Push(AbsoluteX).Push(AbsoluteY).Push(AbsoluteZ);
	Json["Yaw"] = Yaw;
	Json["Pitch"] = Pitch;
	Json["Regions"] = Regions.ToJson();
	Json["Game"] = Game;
	return Json;
}

TOptional<FSaveGame> FSaveGame::FromJson(const FJsonValue& Json, FString& Error)
{
	FValidationContext Validation(Error);
	if (!Validation.Check(
	        Json.IsObject() && Json.Get("Schema").AsString() == SaveSchema, GENESIS_TEXT("Not a Genesis saved game.")))
		return {};
	if (!Validation.Check(Json.Get("Version").AsNumber() == Version, GENESIS_TEXT("Unsupported saved game version.")))
		return {};
	FSaveGame Result;
	FString Detail;
	TOptional<FAssetReference> World = FAssetReference::FromJson(Json.Get("World"), Detail);
	if (!Validation.Check(World && !World->IsNull(), GENESIS_TEXT("The saved game does not name its world.")))
		return {};
	Result.World = *World;
	TOptional<FWorldLocation> Player = FWorldLocation::FromJson(Json.Get("Player"), Error);
	if (!Player)
		return {};
	Result.Player = *Player;
	const FJsonValue& Absolute = Json.Get("Absolute");
	double Yaw = 0, Pitch = 0;
	if (!Validation.Check(Absolute.IsArray() && Absolute.Num() == 3, GENESIS_TEXT("Invalid saved game number.")) ||
	    !ReadNumber(Validation, Absolute[0], MaxAbsoluteCoordinate, Result.AbsoluteX) ||
	    !ReadNumber(Validation, Absolute[1], MaxAbsoluteCoordinate, Result.AbsoluteY) ||
	    !ReadNumber(Validation, Absolute[2], MaxAbsoluteCoordinate, Result.AbsoluteZ) ||
	    !ReadNumber(Validation, Json.Get("Yaw"), 360000.0, Yaw) ||
	    !ReadNumber(Validation, Json.Get("Pitch"), 90.0, Pitch))
		return {};
	Result.Yaw = static_cast<float>(Yaw);
	Result.Pitch = static_cast<float>(Pitch);
	TOptional<FRegionStateStore> Regions = FRegionStateStore::FromJson(Json.Get("Regions"), Error);
	if (!Regions)
		return {};
	Result.Regions = std::move(*Regions);
	if (!Validation.Check(Json.Get("Game").IsObject(), GENESIS_TEXT("Saved game state must be an object.")))
		return {};
	Result.Game = Json.Get("Game");
	Error.Empty();
	return Result;
}

bool FSaveGame::Save(const FString& File, FString& Error) const
{
	const std::filesystem::path Target(File.ToWide());
	std::error_code FileError;
	std::filesystem::create_directories(Target.parent_path(), FileError);
	// Write beside the file, then replace it, so a failed save keeps the old one.
	const std::filesystem::path Temporary = Target.wstring() + L".tmp";
	if (!FFile::WriteAllText(FString::FromWide(Temporary.wstring()), ToJson().ToString()))
	{
		Error = FTextLocalization::Format("Could not write the saved game {}.", File);
		return false;
	}
	std::filesystem::rename(Temporary, Target, FileError);
	if (FileError)
	{
		std::filesystem::remove(Temporary, FileError);
		Error = FTextLocalization::Format("Could not replace the saved game {}.", File);
		return false;
	}
	Error.Empty();
	return true;
}

TOptional<FSaveGame> FSaveGame::Load(const FString& File, FString& Error)
{
	std::error_code FileError;
	const uintmax_t Size = std::filesystem::file_size(std::filesystem::path(File.ToWide()), FileError);
	FString Text;
	if (FileError || Size > static_cast<uintmax_t>(MaxBytes) || !FFile::ReadAllText(File, Text))
	{
		Error = FTextLocalization::Format("Could not read the saved game {}.", File);
		return {};
	}
	FString ParseError;
	const TOptional<FJsonValue> Json = FJsonValue::Parse(Text.View(), &ParseError);
	if (!Json)
	{
		Error = FTextLocalization::Format("The saved game {} is damaged: {}", File, ParseError);
		return {};
	}
	return FromJson(*Json, Error);
}

bool FSaveGame::ResolvePlayer(const FGameWorld& InWorld, FWorldLocation& Out) const
{
	if (InWorld.GetComposition().FindTile(Player.RegionId))
	{
		Out = Player;
		return true;
	}
	return InWorld.FromAbsolute(AbsoluteX, AbsoluteY, AbsoluteZ, Out);
}
