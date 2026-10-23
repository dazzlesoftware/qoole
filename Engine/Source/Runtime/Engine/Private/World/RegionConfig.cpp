// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/RegionConfig.h"
#include "World/WorldCompositionLimits.h"
#include "Misc/ValidationContext.h"
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <limits>

namespace
{
	constexpr int32 MaxPort = 65535;
	constexpr std::string_view KnownKeys[] = {
	    "RegionUUID", "Location", "SizeX", "SizeY", "Map", "ExternalHostName", "InternalPort"};

	bool ParseInt(std::string_view Text, int64 Min, int64 Max, int32& Out)
	{
		while (!Text.empty() && Text.front() == ' ')
			Text.remove_prefix(1);
		while (!Text.empty() && Text.back() == ' ')
			Text.remove_suffix(1);
		int64 Value = 0;
		const std::from_chars_result Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
		if (Result.ec != std::errc() || Result.ptr != Text.data() + Text.size() || Value < Min || Value > Max)
			return false;
		Out = static_cast<int32>(Value);
		return true;
	}
} // namespace

bool FRegionConfigReader::Read(const FConfigFile& File, TArray<FRegionConfig>& Out, FString& Error)
{
	FValidationContext Validation(Error);
	TArray<FRegionConfig> Result;
	for (const FConfigSection& Section : File.GetSections())
	{
		const TFunction<FString(const FConfigEntry*, const FString&)> At =
		    [&](const FConfigEntry* Entry, const FString& Message)
		{
			return FTextLocalization::Format(
			    "{}({}): [{}] {}", File.GetSourceName(), Entry ? Entry->Line : Section.Line, Section.Name, Message);
		};
		const TFunction<const FConfigEntry*(std::string_view)> Required = [&](std::string_view Key)
		{
			const FConfigEntry* Entry = Section.Find(Key);
			Validation.Check(
			    Entry && !Entry->Value.IsEmpty(), At(nullptr, FTextLocalization::Format("{} is required.", Key)));
			return Entry;
		};
		FRegionConfig Region;
		Region.SourceFile = File.GetSourceName();
		Region.Line = Section.Line;
		Region.Tile.Name = Section.Name;

		const FConfigEntry* Id = Required("RegionUUID");
		const FConfigEntry* Location = Required("Location");
		const FConfigEntry* Map = Required("Map");
		if (!Validation.IsValid())
			return false;
		Region.Tile.Id = Id->Value.ToLower();
		if (!Validation.Check(FAssetId::IsValid(Region.Tile.Id), At(Id, GENESIS_TEXT("RegionUUID is not a UUID."))))
			return false;
		const TArray<FString> Cells = Location->Value.ParseIntoArray(",", false);
		const int64 MinCell = std::numeric_limits<int32>::min(), MaxCell = std::numeric_limits<int32>::max();
		if (!Validation.Check(Cells.size() == 2 && ParseInt(Cells[0].View(), MinCell, MaxCell, Region.Tile.X) &&
		            ParseInt(Cells[1].View(), MinCell, MaxCell, Region.Tile.Y),
		        At(Location, GENESIS_TEXT("Location must be two whole numbers, as 1000,1000."))))
			return false;
		for (const TPair<const char*, int32*> Size : {TPair<const char*, int32*>("SizeX", &Region.Tile.SizeX),
		         TPair<const char*, int32*>("SizeY", &Region.Tile.SizeY)})
			if (const FConfigEntry* Entry = Section.Find(Size.first))
				if (!Validation.Check(
				        ParseInt(Entry->Value.View(), 1, WorldCompositionLimits::MaxTileCells, *Size.second),
				        At(Entry,
				            FTextLocalization::Format(
				                "{} must be 1 to {} cells.", Size.first, WorldCompositionLimits::MaxTileCells))))
					return false;
		if (!Validation.Check(FAssetRegistry::IsPackagePath(Map->Value),
		        At(Map, GENESIS_TEXT("Map must be a package path, as /Game/Regions/Hills."))))
			return false;
		Region.Tile.Map = {{}, Map->Value};

		const FConfigEntry* Host = Section.Find("ExternalHostName");
		const FConfigEntry* Port = Section.Find("InternalPort");
		const bool bHasHost = Host && !Host->Value.IsEmpty(), bHasPort = Port && !Port->Value.IsEmpty();
		if (!Validation.Check(bHasHost == bHasPort,
		        At(bHasHost ? Host : Port, GENESIS_TEXT("Set ExternalHostName and InternalPort together."))))
			return false;
		if (bHasHost)
		{
			int32 PortNumber = 0;
			if (!Validation.Check(ParseInt(Port->Value.View(), 1, MaxPort, PortNumber),
			        At(Port, GENESIS_TEXT("InternalPort must be 1 to 65535."))))
				return false;
			if (!Validation.Check(Host->Value.View().find_first_of(" :") == std::string_view::npos,
			        At(Host, GENESIS_TEXT("ExternalHostName must be a host name or address without a port."))))
				return false;
			Region.Tile.Host = FString::Format("{}:{}", Host->Value, PortNumber);
		}

		for (const FConfigEntry& Entry : Section.Entries)
			if (std::none_of(std::begin(KnownKeys), std::end(KnownKeys),
			        [&Entry](std::string_view Known) { return Entry.Key.Equals(Known, ESearchCase::IgnoreCase); }))
				Region.Warnings.push_back(
				    At(&Entry, FTextLocalization::Format("{} is not used yet; ignored.", Entry.Key)));
		Result.push_back(std::move(Region));
	}
	Out.insert(Out.end(), std::make_move_iterator(Result.begin()), std::make_move_iterator(Result.end()));
	Error.Empty();
	return true;
}

bool FRegionConfigReader::ReadDirectory(
    const FString& Directory, TArray<FRegionConfig>& Out, TArray<FRegionConfigIssue>& Issues)
{
	std::error_code FileError;
	TArray<std::filesystem::path> Files;
	for (std::filesystem::directory_iterator It(std::filesystem::path(Directory.ToWide()), FileError), End;
	    !FileError && It != End; It.increment(FileError))
	{
		std::wstring Extension = It->path().extension().wstring();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
		if (It->is_regular_file(FileError) && Extension == L".ini")
			Files.push_back(It->path());
	}
	if (FileError)
	{
		Issues.push_back(
		    {Directory, FTextLocalization::Format("Could not read the region folder: {}", FileError.message())});
		return false;
	}
	std::sort(Files.begin(), Files.end());
	bool bAll = true;
	for (const std::filesystem::path& Path : Files)
	{
		FConfigFile File;
		FString Error;
		const FString Name = FString::FromWide(Path.wstring());
		if (!FConfigFile::Load(Name, File, Error) || !Read(File, Out, Error))
		{
			Issues.push_back({Name, Error});
			bAll = false;
		}
	}
	return bAll;
}
