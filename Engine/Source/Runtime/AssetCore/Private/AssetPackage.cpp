// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "AssetPackage.h"
#include "HAL/File.h"
#include <Windows.h>
#include <objbase.h>
#include <filesystem>
#include <cmath>
#include <cstdio>
#pragma comment(lib, "ole32.lib")

namespace
{
	constexpr uint64 MaxPayload = 512ull * 1024 * 1024;
	constexpr uint64 MaxManifest = 1024 * 1024;
	FString ToPath(const std::filesystem::path& Path) { return FString::FromWide(Path.wstring()); }
	FString NewId()
	{
		GUID Id{};
		if (FAILED(CoCreateGuid(&Id))) return {};
		wchar_t Text[40]{};
		StringFromGUID2(Id, Text, 40);
		return FString::FromWide(Text).Mid(1, 36).ToLower();
	}
	bool IsId(const FString& Id)
	{
		if (Id.Len() != 36) return false;
		for (int I = 0; I < 36; ++I)
		{
			const char C = Id[I];
			if (I == 8 || I == 13 || I == 18 || I == 23) { if (C != '-') return false; }
			else if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'))) return false;
		}
		return true;
	}
	// Detect accidental corruption. This is not an authenticity signature.
	FString Checksum(const uint8* Data, size_t Size)
	{
		uint64 Hash = 14695981039346656037ull;
		for (size_t I = 0; I < Size; ++I) { Hash ^= Data[I]; Hash *= 1099511628211ull; }
		return FString::Format("{:016x}", Hash);
	}
	bool WriteNew(const std::filesystem::path& Path, const void* Data, size_t Size)
	{
		FILE* File = _wfopen(Path.c_str(), L"wbx");
		if (!File) return false;
		const bool Written = Size == 0 || std::fwrite(Data, 1, Size, File) == Size;
		const bool Closed = std::fclose(File) == 0;
		return Written && Closed;
	}
	bool ReadLimited(const std::filesystem::path& Path, uint64 Limit, TArray<uint8>& Data)
	{
		FFile File;
		if (!File.Open(ToPath(Path), EFileOpenMode::Read)) return false;
		const int64 Size = File.Size();
		if (Size < 0 || static_cast<uint64>(Size) > Limit) return false;
		Data.resize(static_cast<size_t>(Size));
		return Size == 0 || File.Read(Data.data(), Size) == Size;
	}
	bool ValidPath(const std::filesystem::path& Path, EAssetPackageKind Kind)
	{
		return ToPath(Path.extension()).Equals(Kind == EAssetPackageKind::Map ? ".gmap" : ".asset", ESearchCase::IgnoreCase);
	}
	FJsonValue Sidecar(const FString& Name, const uint8* Data, size_t Size)
	{
		auto Json = FJsonValue::MakeObject();
		Json["File"] = Name;
		Json["Size"] = static_cast<double>(Size);
		Json["Checksum"] = Checksum(Data, Size);
		return Json;
	}
	bool ReadSidecar(const std::filesystem::path& Directory, const FJsonValue& Json,
		const FString& ExpectedName, TArray<uint8>& Bytes)
	{
		// Derive names instead of following paths supplied by a manifest.
		if (!Json.IsObject() || Json.Get("File").AsString() != ExpectedName ||
			Json.Get("Size").GetType() != EJsonType::Number) return false;
		const double Size = Json.Get("Size").AsNumber();
		if (!std::isfinite(Size) || Size < 0 || Size > MaxPayload || std::floor(Size) != Size) return false;
		if (!ReadLimited(Directory / ExpectedName.ToWide(), MaxPayload, Bytes) || Bytes.size() != static_cast<size_t>(Size)) return false;
		return Json.Get("Checksum").AsString() == Checksum(Bytes.data(), Bytes.size());
	}
}

bool FAssetPackageStore::Save(const FString& Path, FAssetPackage& Package, FString& Error)
{
	Error.Empty();
	const std::filesystem::path File(Path.ToWide());
	if (!ValidPath(File, Package.Kind)) { Error = "Package kind and filename extension do not match."; return false; }
	if (Package.Schema.IsEmpty() || Package.SchemaVersion < 1 || !Package.Exports.IsObject())
	{ Error = "A package needs an object schema, version and export object."; return false; }
	if (!Package.Id.IsEmpty() && !IsId(Package.Id)) { Error = "Invalid package UUID."; return false; }
	const FString Id = Package.Id.IsEmpty() ? NewId() : Package.Id;
	const FString Generation = NewId();
	if (Id.IsEmpty() || Generation.IsEmpty()) { Error = "Cannot allocate a package UUID."; return false; }
	const FString ExportText = Package.Exports.ToString(false);
	if (static_cast<uint64>(ExportText.Len()) > MaxPayload || Package.Bulk.size() > MaxPayload)
	{ Error = "Package exceeds the version 1 payload limit (512 MiB per sidecar)."; return false; }
	const FString Stem = ToPath(File.stem()) + "." + Generation;
	const FString ExportName = Stem + ".gexp", BulkName = Stem + ".gbulk";
	auto Header = FJsonValue::MakeObject();
	Header["Magic"] = "GenesisPackage";
	Header["FormatVersion"] = FormatVersion;
	Header["Id"] = Id;
	Header["Generation"] = Generation;
	Header["Kind"] = Package.Kind == EAssetPackageKind::Map ? "Map" : "Asset";
	Header["Schema"] = Package.Schema;
	Header["SchemaVersion"] = Package.SchemaVersion;
	Header["Dependencies"] = FJsonValue::MakeArray();
	for (const FString& Dependency : Package.Dependencies) Header["Dependencies"].Push(Dependency);
	Header["Exports"] = Sidecar(ExportName, reinterpret_cast<const uint8*>(*ExportText), ExportText.Len());
	Header["Bulk"] = Sidecar(BulkName, Package.Bulk.data(), Package.Bulk.size());
	const FString Manifest = Header.ToString();
	if (static_cast<uint64>(Manifest.Len()) > MaxManifest) { Error = "Package manifest exceeds 1 MiB."; return false; }
	std::error_code EC;
	const auto Directory = File.parent_path().empty() ? std::filesystem::path(L".") : File.parent_path();
	std::filesystem::create_directories(Directory, EC);
	if (EC) { Error = "Cannot create package directory."; return false; }
	const auto Temp = Directory / (Stem + ".tmp").ToWide();
	if (!WriteNew(Directory / ExportName.ToWide(), *ExportText, ExportText.Len()) ||
		!WriteNew(Directory / BulkName.ToWide(), Package.Bulk.data(), Package.Bulk.size()) ||
		!WriteNew(Temp, *Manifest, Manifest.Len()))
	{ Error = "Cannot write package generation; the previous manifest was preserved."; return false; }
	if (!MoveFileExW(Temp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{ Error = "Cannot commit package manifest; the previous package was preserved."; return false; }
	Package.Id = Id;
	return true;
}

FString FAssetId::New() { return NewId(); }
bool FAssetId::IsValid(const FString& Id) { return IsId(Id); }

namespace
{
	struct FManifest
	{
		FAssetPackageMetadata Metadata;
		FJsonValue Json;
		FString Stem;
	};

	bool ReadManifest(const FString& Path, FManifest& Manifest, FString& Error)
	{
		Error.Empty();
		const auto Fail = [&Error](const char* Text) { Error = Text; return false; };
		const std::filesystem::path File(Path.ToWide());
		TArray<uint8> Bytes;
		if (!ReadLimited(File, MaxManifest, Bytes)) return Fail("Cannot read package manifest (limit 1 MiB).");
		const auto Json = FJsonValue::Parse(std::string_view(reinterpret_cast<const char*>(Bytes.data()), Bytes.size()), &Error);
		if (!Json) return false;
		if (!Json->IsObject() || Json->Get("Magic").AsString() != "GenesisPackage" ||
			Json->Get("FormatVersion").GetType() != EJsonType::Number || Json->Get("FormatVersion").AsNumber() != FAssetPackageStore::FormatVersion)
			return Fail("Unsupported Genesis package format.");
		FAssetPackageMetadata Result;
		const FString Kind = Json->Get("Kind").AsString();
		if (Kind != "Asset" && Kind != "Map") return Fail("Unknown package kind.");
		Result.Kind = Kind == "Map" ? EAssetPackageKind::Map : EAssetPackageKind::Asset;
		if (!ValidPath(File, Result.Kind)) return Fail("Package kind and extension do not match.");
		Result.Id = Json->Get("Id").AsString();
		const FString Generation = Json->Get("Generation").AsString();
		Result.Schema = Json->Get("Schema").AsString();
		const double Version = Json->Get("SchemaVersion").AsNumber();
		if (!IsId(Result.Id) || !IsId(Generation) || Result.Schema.IsEmpty() ||
			!std::isfinite(Version) || Version < 1 || Version > INT32_MAX || std::floor(Version) != Version)
			return Fail("Invalid package identity or schema.");
		Result.SchemaVersion = static_cast<int32>(Version);
		if (!Json->Get("Dependencies").IsArray()) return Fail("Invalid package dependencies.");
		for (const auto& Dependency : Json->Get("Dependencies").GetItems())
		{
			if (Dependency.GetType() != EJsonType::String) return Fail("Invalid package dependency.");
			Result.Dependencies.push_back(Dependency.AsString());
		}
		const FString Stem = ToPath(File.stem()) + "." + Generation;
		const auto ValidateSidecar = [&](const char* Key, const char* Extension, uint64& Size)
		{
			const auto& SidecarJson = Json->Get(Key);
			if (!SidecarJson.IsObject() || SidecarJson.Get("File").AsString() != Stem + Extension ||
				SidecarJson.Get("Size").GetType() != EJsonType::Number) return false;
			const double Number = SidecarJson.Get("Size").AsNumber();
			if (!std::isfinite(Number) || Number < 0 || Number > MaxPayload || std::floor(Number) != Number) return false;
			const FString Hash = SidecarJson.Get("Checksum").AsString();
			if (Hash.Len() != 16) return false;
			for (const char C : Hash.View()) if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'))) return false;
			Size = static_cast<uint64>(Number);
			return true;
		};
		if (!ValidateSidecar("Exports", ".gexp", Result.ExportBytes) || !ValidateSidecar("Bulk", ".gbulk", Result.BulkBytes))
			return Fail("Invalid package sidecar declaration.");
		Manifest.Metadata = std::move(Result);
		Manifest.Json = *Json;
		Manifest.Stem = Stem;
		return true;
	}
}

bool FAssetPackageStore::ReadMetadata(const FString& Path, FAssetPackageMetadata& Metadata, FString& Error)
{
	FManifest Manifest;
	if (!ReadManifest(Path, Manifest, Error)) return false;
	Metadata = std::move(Manifest.Metadata);
	return true;
}

bool FAssetPackageStore::Load(const FString& Path, FAssetPackage& Package, FString& Error)
{
	FManifest Manifest;
	if (!ReadManifest(Path, Manifest, Error)) return false;
	const std::filesystem::path File(Path.ToWide());
	TArray<uint8> Bytes;
	FAssetPackage Result;
	if (!ReadSidecar(File.parent_path(), Manifest.Json.Get("Exports"), Manifest.Stem + ".gexp", Bytes) ||
		!ReadSidecar(File.parent_path(), Manifest.Json.Get("Bulk"), Manifest.Stem + ".gbulk", Result.Bulk))
	{ Error = "Package sidecar is missing, corrupt or has an invalid size."; return false; }
	const auto Exports = FJsonValue::Parse(std::string_view(reinterpret_cast<const char*>(Bytes.data()), Bytes.size()), &Error);
	if (!Exports || !Exports->IsObject()) { if (Error.IsEmpty()) Error = "Invalid package exports."; return false; }
	Result.Id = std::move(Manifest.Metadata.Id);
	Result.Kind = Manifest.Metadata.Kind;
	Result.Schema = std::move(Manifest.Metadata.Schema);
	Result.SchemaVersion = Manifest.Metadata.SchemaVersion;
	Result.Dependencies = std::move(Manifest.Metadata.Dependencies);
	Result.Exports = *Exports;
	Package = std::move(Result);
	return true;
}
