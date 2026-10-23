// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapPackage.h"
#include "World/WorldGeometry.h"
#include "MapCoreHeaders.h"
#include "MapEditorEnvironment.h"
#include "MapWorldAdapter.h"
#include "MapAssetLibrary.h"
#include "AssetPackage.h"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace
{
using J = FJsonValue;
void Require(bool Value, const char* Error)
{
	if (!Value)
		throw std::runtime_error(Error);
}
float Number(const J& Value)
{
	Require(Value.GetType() == EJsonType::Number, "Expected a number.");
	const double N = Value.AsNumber();
	Require(std::isfinite(N) && std::abs(N) <= 1.0e9, "Number is out of range.");
	return static_cast<float>(N);
}
int Integer(const J& Value, int Min, int Max)
{
	Require(Value.GetType() == EJsonType::Number, "Expected an integer.");
	const double N = Value.AsNumber();
	Require(std::isfinite(N) && N >= Min && N <= Max && N == std::floor(N), "Integer is out of range.");
	return static_cast<int>(N);
}
std::string Text(const J& Value)
{
	Require(Value.GetType() == EJsonType::String && Value.AsString().Len() <= 65536,
			"Expected a bounded UTF-8 string.");
	Require(Value.AsString().View().find('\0') == std::string_view::npos, "Embedded NUL in string.");
	return Value.AsString().ToAnsi();
}
bool Boolean(const J& Value)
{
	Require(Value.GetType() == EJsonType::Bool, "Expected a boolean.");
	return Value.AsBool();
}
const TArray<J>& Array(const J& Value, int Max = 1000000)
{
	Require(Value.IsArray() && Value.Num() <= Max, "Expected a bounded array.");
	return Value.GetItems();
}
J Vec(const Vector3d& V)
{
	auto Out = J::MakeArray();
	for (float N : {V.GetX(), V.GetY(), V.GetZ()})
	{
		Require(std::isfinite(N), "Cannot save nonfinite vectors.");
		Out.Push(N);
	}
	return Out;
}
Vector3d ReadVec(const J& Value)
{
	Require(Array(Value, 3).size() == 3, "Expected three vector coordinates.");
	return Vector3d(Number(Value[0]), Number(Value[1]), Number(Value[2]));
}
J Ansi(const std::string& Value)
{
	return FString::FromAnsi(Value);
}
J Ansi(const char* Value)
{
	return FString::FromAnsi(Value ? Value : "");
}
J WriteObject(Object& Obj, FAssetPackage& Package, int Depth)
{
	Require(Depth < 128, "World hierarchy is too deep.");
	auto Out = J::MakeObject();
	Out["Name"] = Ansi(Obj.GetObjName());
	Out["Position"] = Vec(Obj.GetPosition());
	Out["Scale"] = Vec(Obj.GetScale());
	Out["Identity"] = Obj.GetIdentity().ToJson();
	const auto& R = Obj.GetOrientation();
	Out["Rotation"] = Vec(Vector3d(R.GetYaw(), R.GetPitch(), R.GetRoll()));
	if (Obj.HasEntity())
	{
		auto& E = *Obj.GetEntityPtr();
		auto EntityData = J::MakeObject();
		EntityData["Class"] = Ansi(E.GetClassname());
		EntityData["Keys"] = J::MakeObject();
		for (int I = 0; I < E.GetNumKeys(); ++I)
			EntityData["Keys"][FString::FromAnsi(E.GetKeyNum(I)).View()] = Ansi(E.GetKey(E.GetKeyNum(I)));
		EntityData["Origin"] = Vec(E.GetOrigin());
		EntityData["Angle"] = E.GetAngle();
		EntityData["MAngle"] = Vec(E.GetMAngle());
		auto Flags = J::MakeArray(), Hidden = J::MakeArray();
		for (int I = 0; I < ENTITY_FLAGS; ++I)
		{
			Flags.Push(E.IsAttribSet(I));
			Hidden.Push(E.IsNotUserAttrib(I));
		}
		EntityData["Flags"] = Flags;
		EntityData["Hidden"] = Hidden;
		Out["Entity"] = std::move(EntityData);
	}
	if (Obj.HasBrush())
	{
		auto Brush = J::MakeArray();
		auto& G = Obj.GetBrush();
		for (int F = 0; F < G.GetNumFaces(); ++F)
		{
			auto Face = J::MakeObject();
			auto Vertices = J::MakeArray();
			auto& Poly = G.GetFace(F);
			for (int I = 0; I < Poly.GetNumSides(); ++I)
				Vertices.Push(Vec(G.GetVertex(Poly.GetSide(I).GetStartPoint())));
			Face["Vertices"] = std::move(Vertices);
			auto* Tex = Poly.GetTexturePtr();
			Face["Texture"] = Ansi(Tex->GetTName() ? Tex->GetTName() : "");
			int X, Y;
			float Rot, SX, SY;
			unsigned int A, B, C;
			Tex->GetTInfo(X, Y, Rot, SX, SY);
			Tex->GetTAttribs(A, B, C);
			Face["OffsetX"] = X;
			Face["OffsetY"] = Y;
			Face["Rotation"] = Rot;
			Face["ScaleX"] = SX;
			Face["ScaleY"] = SY;
			Face["Attrib1"] = static_cast<double>(A);
			Face["Attrib2"] = static_cast<double>(B);
			Face["Attrib3"] = static_cast<double>(C);
			Face["Locked"] = Tex->IsTexLocked();
			Brush.Push(std::move(Face));
		}
		Out["Brush"] = std::move(Brush);
	}
	if (Obj.HasLandscape())
	{
		FString Error;
		FJsonValue Data;
		if (!FWorldGeometryCodec::EncodeLandscape(Obj.GetLandscape(), Package, Data, Error))
			throw std::runtime_error(Error.ToStdString());
		Out["Landscape"] = std::move(Data);
	}
	else if (Obj.HasMesh())
	{
		FStaticMeshComponentData Definition;
		if (const auto Asset = Obj.GetMeshAsset())
		{
			Definition.Asset = Asset->Source;
			Definition.MaterialOverride = Asset->MaterialOverride;
			Definition.AdditionalOverrides = Asset->AdditionalOverrides;
		}
		else
		{
			Require(StaticMesh::Find(Obj.GetMeshName()).get() == Obj.GetMeshPtr().get(),
					"Custom meshes need an asset serializer before saving.");
			Definition.BuiltIn = FString::FromAnsi(Obj.GetMeshName());
		}
		FJsonValue Data;
		FString Error;
		if (!FWorldGeometryCodec::EncodeMesh(Definition, Data, Error))
			throw std::runtime_error(Error.ToStdString());
		TArray<FAssetReference> References;
		if (!FWorldGeometryCodec::GetAssetReferences("Genesis.StaticMesh", Data, References, Error))
			throw std::runtime_error(Error.ToStdString());
		for (const auto& Ref : References)
			if (!Ref.Path.IsEmpty())
				Package.Dependencies.push_back(Ref.Path);
		Out["Mesh"] = std::move(Data);
	}

	if (Obj.HasFoliage())
	{
		FString Error;
		FJsonValue Data;
		if (!FWorldGeometryCodec::EncodeFoliage(Obj.GetFoliage(), Package, Data, Error))
			throw std::runtime_error(Error.ToStdString());
		Out["Foliage"] = std::move(Data);
	}
	Out["Children"] = J::MakeArray();
	for (int I = 0; I < Obj.GetNumChildren(); ++I)
		Out["Children"].Push(WriteObject(Obj.GetChild(I), Package, Depth + 1));
	return Out;
}
TUniquePtr<Object> ReadObject(const J& In, const FAssetPackage& Package, int Depth, int& Total)
{
	Require(In.IsObject() && Depth < 128 && ++Total <= 100000, "Invalid or excessive world hierarchy.");
	auto Obj = MakeUnique<Object>();
	if (In.Contains("Identity"))
	{
		FString Error;
		const auto Identity = FActorIdentity::FromJson(In.Get("Identity"), Error);
		Require(Identity.has_value(), "Invalid actor/component identity.");
		Obj->SetIdentity(*Identity);
	}
	Obj->SetObjName(Text(In.Get("Name")).c_str());
	const Vector3d Position = ReadVec(In.Get("Position")), Scale = ReadVec(In.Get("Scale")),
				   Rotation = ReadVec(In.Get("Rotation"));
	Require(std::abs(Scale.GetX()) >= 0.000001f && std::abs(Scale.GetY()) >= 0.000001f &&
				std::abs(Scale.GetZ()) >= 0.000001f,
			"Zero object scale.");
	const int Payloads = In.Contains("Brush") + In.Contains("Mesh") + In.Contains("Landscape") + In.Contains("Foliage");
	Require(Payloads <= 1, "Conflicting object payloads.");
	if (In.Contains("Entity"))
	{
		const auto& E = In.Get("Entity");
		Require(E.Get("Keys").IsObject() && E.Get("Keys").Num() <= MAX_ENTITY_KEYS, "Invalid entity keys.");
		auto EntityPtr = MakeUnique<Entity>(Text(E.Get("Class")).c_str());
		EntityPtr->ClearKeys();
		for (const auto& Key : E.Get("Keys").GetKeys())
			EntityPtr->SetKey(Key.ToAnsi().c_str(), Text(E.Get("Keys").Get(Key.View())).c_str());
		EntityPtr->SetOrigin(ReadVec(E.Get("Origin")));
		EntityPtr->SetAngle(Integer(E.Get("Angle"), INT32_MIN, INT32_MAX));
		EntityPtr->SetMAngle(ReadVec(E.Get("MAngle")));
		Require(Array(E.Get("Flags"), ENTITY_FLAGS).size() == ENTITY_FLAGS &&
					Array(E.Get("Hidden"), ENTITY_FLAGS).size() == ENTITY_FLAGS,
				"Invalid entity flags.");
		for (int I = 0; I < ENTITY_FLAGS; ++I)
		{
			EntityPtr->SetAttribState(I, Boolean(E.Get("Flags")[I]), Boolean(E.Get("Hidden")[I]));
		}
		Obj->SetEntity(EntityPtr.release(), false);
	}
	Require(!Obj->HasEntity() || Payloads == 0 || (In.Contains("Brush") && Obj->IsModelNode()),
			"Entity conflicts with object payload.");
	if (In.Contains("Brush"))
	{
		const auto& Faces = Array(In.Get("Brush"), 256);
		std::vector<std::vector<Vector3d>> Polygons;
		for (const auto& Face : Faces)
		{
			std::vector<Vector3d> Points;
			for (const auto& V : Array(Face.Get("Vertices"), 256))
				Points.push_back(ReadVec(V));
			Polygons.push_back(std::move(Points));
		}
		auto G = TUniquePtr<Geometry>(Geometry::FromPolygons(Polygons));
		Require(G != nullptr, "Invalid brush polygons.");
		for (int I = 0; I < static_cast<int>(Faces.size()); ++I)
		{
			const auto& F = Faces[I];
			auto Tex = MakeUnique<FaceTex>(Text(F.Get("Texture")).c_str());
			Tex->SetTInfo(Integer(F.Get("OffsetX"), INT32_MIN, INT32_MAX),
						  Integer(F.Get("OffsetY"), INT32_MIN, INT32_MAX), Number(F.Get("Rotation")),
						  Number(F.Get("ScaleX")), Number(F.Get("ScaleY")));
			const auto Attribute = [](const J& V) {
				const double N = V.AsNumber(-1);
				Require(N >= 0 && N <= UINT32_MAX && N == std::floor(N), "Invalid texture attributes.");
				return static_cast<unsigned int>(N);
			};
			Tex->SetTAttribs(Attribute(F.Get("Attrib1")), Attribute(F.Get("Attrib2")), Attribute(F.Get("Attrib3")));
			Tex->SetTexLock(Boolean(F.Get("Locked")));
			G->SetFaceTexture(I, Tex.release());
		}
		Obj->SetBrush(G.release());
	}
	if (In.Contains("Mesh"))
	{
		FStaticMeshComponentData Definition;
		FString Error;
		if (!FWorldGeometryCodec::DecodeMesh(In.Get("Mesh"), Definition, Error))
			throw std::runtime_error(Error.ToStdString());
		if (Definition.IsNative())
		{
			const auto Asset = FMapAssetLibrary::LoadMesh(Definition.Asset, Definition.MaterialOverride, Error,
														  Definition.AdditionalOverrides);
			if (!Asset)
				throw std::runtime_error(Error.ToStdString());
			Obj->SetMeshAsset(Asset);
		}
		else
			Obj->SetMesh(StaticMesh::Find(Definition.BuiltIn.ToAnsi().c_str()));
	}

	if (In.Contains("Landscape"))
	{
		FString Error;
		std::shared_ptr<const Landscape> Land;
		if (!FWorldGeometryCodec::DecodeLandscape(Package, In.Get("Landscape"), Land, Error,
												  FMapAssetLibrary::GetRegistry().get()))
			throw std::runtime_error(Error.ToStdString());
		Obj->SetLandscape(Land);
	}
	if (In.Contains("Foliage"))
	{
		FString Error;
		std::shared_ptr<const Foliage> Plants;
		if (!FWorldGeometryCodec::DecodeFoliage(Package, In.Get("Foliage"), Plants, Error))
			throw std::runtime_error(Error.ToStdString());
		Obj->SetFoliage(Plants);
	}

	const auto& Children = Array(In.Get("Children"), 100000);
	Require(Payloads == 0 || Children.empty(), "Leaf payload has child objects.");
	Require(!Obj->IsItemNode() || Children.empty(), "Point entity has child objects.");
	for (const auto& Child : Children)
	{
		auto Node = ReadObject(Child, Package, Depth + 1, Total);
		Obj->AddChild(*Node.release(), false);
	}
	Obj->SetPosition(Position);
	Obj->SetScale(Scale);
	Obj->SetOrientation(SphrVector(Rotation.GetX(), Rotation.GetY(), Rotation.GetZ()));
	Obj->SetBoundRadius(false);
	return Obj;
}
} // namespace

bool FMapPackage::Save(const FString& Path, Object& Root, FString& Id, FString& Error, bool bPrefab)
{
	try
	{
		FAssetPackage Package;
		Package.Id = Id;
		Package.Kind = bPrefab ? EAssetPackageKind::Asset : EAssetPackageKind::Map;
		Package.Schema = bPrefab ? "Genesis.Prefab" : "Genesis.World";
		Package.SchemaVersion = 3;
		if (const auto* Set = FMapEditorEnvironment::GetActiveGameSet())
		{
			Package.Exports["GameSet"]["Game"] = Set->Game;
			Package.Exports["GameSet"]["Entities"] = Set->EntFile;
		}
		FWorld World;
		if (!FMapWorldAdapter::ToRuntime(WriteObject(Root, Package, 0), World, Error))
			return false;
		Package.Exports["Scene"] = World.ToJson();
		TSet<std::string> Seen;
		std::erase_if(Package.Dependencies,
					  [&](const FString& Path) { return !Seen.insert(Path.ToStdString()).second; });
		if (!FAssetPackageStore::Save(Path, Package, Error))
			return false;
		Id = Package.Id;
		return true;
	}
	catch (const std::exception& Exception)
	{
		Error = FString::Format("Cannot save world: {}", Exception.what());
		return false;
	}
}

TUniquePtr<Object> FMapPackage::Load(const FString& Path, FString& Id, FString& Error, bool bPrefab,
									 const FString& ExpectedId, bool* bUpgraded)
{
	FAssetPackage Package;
	if (!FAssetPackageStore::Load(Path, Package, Error))
		return {};
	if (!ExpectedId.IsEmpty() && Package.Id != ExpectedId)
	{
		Error = "The prefab identity changed since discovery; refresh content before placing it.";
		return {};
	}
	if (Package.Kind != (bPrefab ? EAssetPackageKind::Asset : EAssetPackageKind::Map) ||
		Package.Schema != (bPrefab ? "Genesis.Prefab" : "Genesis.World") ||
		(Package.SchemaVersion < 1 || Package.SchemaVersion > 3))
	{
		Error = "Unsupported Genesis world schema.";
		return {};
	}
	const auto* PreviousSet = FMapEditorEnvironment::GetActiveGameSet();
	try
	{
		FJsonValue LegacyRoot;
		if (Package.SchemaVersion >= 2)
		{
			const auto World = FWorld::FromJson(Package.Exports.Get("Scene"), Error);
			if (!World || !FMapWorldAdapter::ToLegacy(*World, LegacyRoot, Error))
				return {};
		}
		else
			LegacyRoot = Package.Exports.Get("Root");
		const auto& SetData = Package.Exports.Get("GameSet");
		const auto* Set =
			FMapEditorEnvironment::FindGameSet(SetData.Get("Game").AsString(), SetData.Get("Entities").AsString());
		Require(Set != nullptr, "This world's entity definition set is unavailable.");
		Require(!bPrefab || PreviousSet == Set,
				"Convert the prefab to this world's entity definition set before placing it.");
		if (FMapEditorEnvironment::GetActiveGameSet() != Set && !FMapEditorEnvironment::Activate(*Set, &Error))
			return {};
		int Total = 0;
		auto Root = ReadObject(LegacyRoot, Package, 0, Total);
		if (bUpgraded)
			*bUpgraded = Package.SchemaVersion < 3;
		Id = Package.Id;
		return Root;
	}
	catch (const std::exception& Exception)
	{
		Error = FString::Format("Cannot load world: {}", Exception.what());
		if (PreviousSet && FMapEditorEnvironment::GetActiveGameSet() != PreviousSet)
			if (!FMapEditorEnvironment::Activate(*PreviousSet))
				Error += " The original entity definition set could not be restored.";
		return {};
	}
}
