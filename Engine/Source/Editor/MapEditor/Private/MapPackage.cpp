// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapPackage.h"
#include "Misc/ValidationContext.h"
#include "Misc/ScopeExit.h"
#include "Containers/ContainerAliases.h"
#include "Internationalization/TextLocalization.h"
#include "World/WorldGeometry.h"
#include "MapCoreHeaders.h"
#include "MapEditorEnvironment.h"
#include "MapWorldAdapter.h"
#include "MapAssetLibrary.h"
#include "AssetPackage.h"
#include <bit>
#include <cmath>

namespace
{
	using J = FJsonValue;

	float Number(FValidationContext& Validation, const J& Value)
	{
		if (!Validation.Check(Value.GetType() == EJsonType::Number, GENESIS_TEXT("Expected a number.")))
		{
			return {};
		}

		const double N = Value.AsNumber();
		if (!Validation.Check(std::isfinite(N) && std::abs(N) <= 1.0e9, GENESIS_TEXT("Number is out of range.")))
		{
			return {};
		}
		return static_cast<float>(N);
	}
	int Integer(FValidationContext& Validation, const J& Value, int Min, int Max)
	{
		if (!Validation.Check(Value.GetType() == EJsonType::Number, GENESIS_TEXT("Expected an integer.")))
		{
			return {};
		}

		const double N = Value.AsNumber();
		if (!Validation.Check(std::isfinite(N) && N >= Min && N <= Max && N == std::floor(N),
		        GENESIS_TEXT("Integer is out of range.")))
		{
			return {};
		}
		return static_cast<int>(N);
	}
	std::string Text(FValidationContext& Validation, const J& Value)
	{
		if (!Validation.Check(Value.GetType() == EJsonType::String && Value.AsString().Len() <= 65536,
		        GENESIS_TEXT("Expected a bounded UTF-8 string.")))
		{
			return {};
		}

		if (!Validation.Check(
		        Value.AsString().View().find('\0') == std::string_view::npos, GENESIS_TEXT("Embedded NUL in string.")))
		{
			return {};
		}
		return Value.AsString().ToAnsi();
	}
	bool Boolean(FValidationContext& Validation, const J& Value)
	{
		if (!Validation.Check(Value.GetType() == EJsonType::Bool, GENESIS_TEXT("Expected a boolean.")))
		{
			return false;
		}
		return Value.AsBool();
	}
	TArrayView<const J> Array(FValidationContext& Validation, const J& Value, int Max = 1000000)
	{
		if (!Validation.Check(Value.IsArray() && Value.Num() <= Max, GENESIS_TEXT("Expected a bounded array.")))
		{
			return {};
		}
		return Value.GetItems();
	}
	J Vec(FValidationContext& Validation, const Vector3d& V)
	{
		FJsonValue Out = J::MakeArray();
		for (float N : {V.GetX(), V.GetY(), V.GetZ()})
		{
			if (!Validation.Check(std::isfinite(N), GENESIS_TEXT("Cannot save nonfinite vectors.")))
			{
				return {};
			}

			Out.Push(N);
		}
		return Out;
	}
	Vector3d ReadVec(FValidationContext& Validation, const J& Value)
	{
		if (!Validation.Check(
		        Array(Validation, Value, 3).size() == 3, GENESIS_TEXT("Expected three vector coordinates.")))
		{
			return {};
		}
		return Vector3d(Number(Validation, Value[0]), Number(Validation, Value[1]), Number(Validation, Value[2]));
	}
	J Ansi(const std::string& Value)
	{
		return FString::FromAnsi(Value);
	}
	J Ansi(const char* Value)
	{
		return FString::FromAnsi(Value ? Value : "");
	}
	J WriteObject(FValidationContext& Validation, Object& Obj, FAssetPackage& Package, int Depth)
	{
		if (!Validation.Check(Depth < 128, GENESIS_TEXT("World hierarchy is too deep.")))
		{
			return {};
		}

		FJsonValue Out = J::MakeObject();
		Out["Name"] = Ansi(Obj.GetObjName());
		Out["Position"] = Vec(Validation, Obj.GetPosition());
		if (!Validation.IsValid())
		{
			return {};
		}

		Out["Scale"] = Vec(Validation, Obj.GetScale());
		if (!Validation.IsValid())
		{
			return {};
		}

		Out["Identity"] = Obj.GetIdentity().ToJson();
		const SphrVector& R = Obj.GetOrientation();
		Out["Rotation"] = Vec(Validation, Vector3d(R.GetYaw(), R.GetPitch(), R.GetRoll()));
		if (!Validation.IsValid())
		{
			return {};
		}

		if (Obj.HasEntity())
		{
			Entity& E = *Obj.GetEntityPtr();
			FJsonValue EntityData = J::MakeObject();
			EntityData["Class"] = Ansi(E.GetClassname());
			EntityData["Keys"] = J::MakeObject();
			for (int I = 0; I < E.GetNumKeys(); ++I)
				EntityData["Keys"][FString::FromAnsi(E.GetKeyNum(I)).View()] = Ansi(E.GetKey(E.GetKeyNum(I)));
			EntityData["Origin"] = Vec(Validation, E.GetOrigin());
			if (!Validation.IsValid())
			{
				return {};
			}

			EntityData["Angle"] = E.GetAngle();
			EntityData["MAngle"] = Vec(Validation, E.GetMAngle());
			if (!Validation.IsValid())
			{
				return {};
			}

			FJsonValue Flags = J::MakeArray(), Hidden = J::MakeArray();
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
			FJsonValue Brush = J::MakeArray();
			Geometry& G = Obj.GetBrush();
			for (int F = 0; F < G.GetNumFaces(); ++F)
			{
				FJsonValue Face = J::MakeObject();
				FJsonValue Vertices = J::MakeArray();
				GPolygon& Poly = G.GetFace(F);
				for (int I = 0; I < Poly.GetNumSides(); ++I)
				{
					Vertices.Push(Vec(Validation, G.GetVertex(Poly.GetSide(I).GetStartPoint())));
					if (!Validation.IsValid())
					{
						return {};
					}
				}
				if (!Validation.IsValid())
				{
					return {};
				}

				Face["Vertices"] = std::move(Vertices);
				FaceTex* Tex = Poly.GetTexturePtr();
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
			{
				Validation.Fail(Error);
				return {};
			}
			Out["Landscape"] = std::move(Data);
		}
		else if (Obj.HasMesh())
		{
			FStaticMeshComponentData Definition;
			if (const std::shared_ptr<const FLoadedMeshAsset> Asset = Obj.GetMeshAsset())
			{
				Definition.Asset = Asset->Source;
				Definition.MaterialOverride = Asset->MaterialOverride;
				Definition.AdditionalOverrides = Asset->AdditionalOverrides;
			}
			else
			{
				if (!Validation.Check(StaticMesh::Find(Obj.GetMeshName()).get() == Obj.GetMeshPtr().get(),
				        GENESIS_TEXT("Custom meshes need an asset serializer before saving.")))
				{
					return {};
				}

				Definition.BuiltIn = FString::FromAnsi(Obj.GetMeshName());
			}
			FJsonValue Data;
			FString Error;
			if (!FWorldGeometryCodec::EncodeMesh(Definition, Data, Error))
			{
				Validation.Fail(Error);
				return {};
			}
			TArray<FAssetReference> References;
			if (!FWorldGeometryCodec::GetAssetReferences("Genesis.StaticMesh", Data, References, Error))
			{
				Validation.Fail(Error);
				return {};
			}
			for (const FAssetReference& Ref : References)
				if (!Ref.Path.IsEmpty())
					Package.Dependencies.push_back(Ref.Path);
			Out["Mesh"] = std::move(Data);
		}

		if (Obj.HasFoliage())
		{
			FString Error;
			FJsonValue Data;
			if (!FWorldGeometryCodec::EncodeFoliage(Obj.GetFoliage(), Package, Data, Error))
			{
				Validation.Fail(Error);
				return {};
			}
			Out["Foliage"] = std::move(Data);
		}
		Out["Children"] = J::MakeArray();
		for (int I = 0; I < Obj.GetNumChildren(); ++I)
		{
			Out["Children"].Push(WriteObject(Validation, Obj.GetChild(I), Package, Depth + 1));
			if (!Validation.IsValid())
			{
				return {};
			}
		}
		if (!Validation.IsValid())
		{
			return {};
		}
		return Out;
	}
	TUniquePtr<Object> ReadObject(
	    FValidationContext& Validation, const J& In, const FAssetPackage& Package, int Depth, int& Total)
	{
		if (!Validation.Check(In.IsObject() && Depth < 128 && ++Total <= 100000,
		        GENESIS_TEXT("Invalid or excessive world hierarchy.")))
		{
			return {};
		}

		TUniquePtr<Object> Obj = MakeUnique<Object>();
		if (In.Contains("Identity"))
		{
			FString Error;
			const TOptional<FActorIdentity> Identity = FActorIdentity::FromJson(In.Get("Identity"), Error);
			if (!Validation.Check(Identity.has_value(), GENESIS_TEXT("Invalid actor/component identity.")))
			{
				return {};
			}

			Obj->SetIdentity(*Identity);
		}
		Obj->SetObjName(Text(Validation, In.Get("Name")).c_str());
		if (!Validation.IsValid())
		{
			return {};
		}

		const Vector3d Position = ReadVec(Validation, In.Get("Position")), Scale = ReadVec(Validation, In.Get("Scale")),
		               Rotation = ReadVec(Validation, In.Get("Rotation"));
		if (!Validation.IsValid())
		{
			return {};
		}

		if (!Validation.Check(std::abs(Scale.GetX()) >= 0.000001f && std::abs(Scale.GetY()) >= 0.000001f &&
		            std::abs(Scale.GetZ()) >= 0.000001f,
		        GENESIS_TEXT("Zero object scale.")))
		{
			return {};
		}

		const int Payloads =
		    In.Contains("Brush") + In.Contains("Mesh") + In.Contains("Landscape") + In.Contains("Foliage");
		if (!Validation.Check(Payloads <= 1, GENESIS_TEXT("Conflicting object payloads.")))
		{
			return {};
		}

		if (In.Contains("Entity"))
		{
			const FJsonValue& E = In.Get("Entity");
			if (!Validation.Check(E.Get("Keys").IsObject() && E.Get("Keys").Num() <= MAX_ENTITY_KEYS,
			        GENESIS_TEXT("Invalid entity keys.")))
			{
				return {};
			}

			TUniquePtr<Entity> EntityPtr = MakeUnique<Entity>(Text(Validation, E.Get("Class")).c_str());
			if (!Validation.IsValid())
			{
				return {};
			}

			EntityPtr->ClearKeys();
			for (const FString& Key : E.Get("Keys").GetKeys())
			{
				EntityPtr->SetKey(Key.ToAnsi().c_str(), Text(Validation, E.Get("Keys").Get(Key.View())).c_str());
				if (!Validation.IsValid())
				{
					return {};
				}
			}
			if (!Validation.IsValid())
			{
				return {};
			}

			EntityPtr->SetOrigin(ReadVec(Validation, E.Get("Origin")));
			if (!Validation.IsValid())
			{
				return {};
			}

			EntityPtr->SetAngle(Integer(Validation, E.Get("Angle"), INT32_MIN, INT32_MAX));
			if (!Validation.IsValid())
			{
				return {};
			}

			EntityPtr->SetMAngle(ReadVec(Validation, E.Get("MAngle")));
			if (!Validation.IsValid())
			{
				return {};
			}

			if (!Validation.Check(Array(Validation, E.Get("Flags"), ENTITY_FLAGS).size() == ENTITY_FLAGS &&
			            Array(Validation, E.Get("Hidden"), ENTITY_FLAGS).size() == ENTITY_FLAGS,
			        GENESIS_TEXT("Invalid entity flags.")))
			{
				return {};
			}

			for (int I = 0; I < ENTITY_FLAGS; ++I)
			{
				EntityPtr->SetAttribState(
				    I, Boolean(Validation, E.Get("Flags")[I]), Boolean(Validation, E.Get("Hidden")[I]));
				if (!Validation.IsValid())
				{
					return {};
				}
			}
			Obj->SetEntity(EntityPtr.release(), false);
		}
		if (!Validation.Check(!Obj->HasEntity() || Payloads == 0 || (In.Contains("Brush") && Obj->IsModelNode()),
		        GENESIS_TEXT("Entity conflicts with object payload.")))
		{
			return {};
		}

		if (In.Contains("Brush"))
		{
			const TArrayView<const J> Faces = Array(Validation, In.Get("Brush"), 256);
			if (!Validation.IsValid())
			{
				return {};
			}

			std::vector<std::vector<Vector3d>> Polygons;
			for (const FJsonValue& Face : Faces)
			{
				std::vector<Vector3d> Points;
				for (const FJsonValue& V : Array(Validation, Face.Get("Vertices"), 256))
				{
					Points.push_back(ReadVec(Validation, V));
					if (!Validation.IsValid())
					{
						return {};
					}
				}
				if (!Validation.IsValid())
				{
					return {};
				}

				Polygons.push_back(std::move(Points));
			}
			TUniquePtr<Geometry> G = TUniquePtr<Geometry>(Geometry::FromPolygons(Polygons));
			if (!Validation.Check(G != nullptr, GENESIS_TEXT("Invalid brush polygons.")))
			{
				return {};
			}

			for (int I = 0; I < static_cast<int>(Faces.size()); ++I)
			{
				FJsonValue const& F = Faces[I];
				TUniquePtr<FaceTex> Tex = MakeUnique<FaceTex>(Text(Validation, F.Get("Texture")).c_str());
				if (!Validation.IsValid())
				{
					return {};
				}

				Tex->SetTInfo(Integer(Validation, F.Get("OffsetX"), INT32_MIN, INT32_MAX),
				    Integer(Validation, F.Get("OffsetY"), INT32_MIN, INT32_MAX), Number(Validation, F.Get("Rotation")),
				    Number(Validation, F.Get("ScaleX")), Number(Validation, F.Get("ScaleY")));
				if (!Validation.IsValid())
				{
					return {};
				}

				const TFunction<unsigned int(const J&)> Attribute = [&Validation](const J& V) -> unsigned int
				{
					const double N = V.AsNumber(-1);
					if (!Validation.Check(N >= 0 && N <= UINT32_MAX && N == std::floor(N),
					        GENESIS_TEXT("Invalid texture attributes.")))
					{
						return {};
					}
					return static_cast<unsigned int>(N);
				};
				Tex->SetTAttribs(Attribute(F.Get("Attrib1")), Attribute(F.Get("Attrib2")), Attribute(F.Get("Attrib3")));
				if (!Validation.IsValid())
				{
					return {};
				}

				Tex->SetTexLock(Boolean(Validation, F.Get("Locked")));
				if (!Validation.IsValid())
				{
					return {};
				}

				G->SetFaceTexture(I, Tex.release());
			}
			Obj->SetBrush(G.release());
		}
		if (In.Contains("Mesh"))
		{
			FStaticMeshComponentData Definition;
			FString Error;
			if (!FWorldGeometryCodec::DecodeMesh(In.Get("Mesh"), Definition, Error))
			{
				Validation.Fail(Error);
				return {};
			}
			if (Definition.IsNative())
			{
				const std::shared_ptr<const FLoadedMeshAsset> Asset = FMapAssetLibrary::LoadMesh(
				    Definition.Asset, Definition.MaterialOverride, Error, Definition.AdditionalOverrides);
				if (!Asset)
				{
					Validation.Fail(Error);
					return {};
				}
				Obj->SetMeshAsset(Asset);
			}
			else
				Obj->SetMesh(StaticMesh::Find(Definition.BuiltIn.ToAnsi().c_str()));
		}

		if (In.Contains("Landscape"))
		{
			FString Error;
			std::shared_ptr<const Landscape> Land;
			if (!FWorldGeometryCodec::DecodeLandscape(
			        Package, In.Get("Landscape"), Land, Error, FMapAssetLibrary::GetRegistry().get()))
			{
				Validation.Fail(Error);
				return {};
			}
			Obj->SetLandscape(Land);
		}
		if (In.Contains("Foliage"))
		{
			FString Error;
			std::shared_ptr<const Foliage> Plants;
			if (!FWorldGeometryCodec::DecodeFoliage(Package, In.Get("Foliage"), Plants, Error))
			{
				Validation.Fail(Error);
				return {};
			}
			Obj->SetFoliage(Plants);
		}

		const TArrayView<const J> Children = Array(Validation, In.Get("Children"), 100000);
		if (!Validation.IsValid())
		{
			return {};
		}

		if (!Validation.Check(Payloads == 0 || Children.empty(), GENESIS_TEXT("Leaf payload has child objects.")))
		{
			return {};
		}

		if (!Validation.Check(!Obj->IsItemNode() || Children.empty(), GENESIS_TEXT("Point entity has child objects.")))
		{
			return {};
		}

		for (const FJsonValue& Child : Children)
		{
			TUniquePtr<Object> Node = ReadObject(Validation, Child, Package, Depth + 1, Total);
			if (!Validation.IsValid())
			{
				return {};
			}

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
	FAssetPackage Package;
	if (!Build(Root, Id, Package, Error, bPrefab) || !FAssetPackageStore::Save(Path, Package, Error))
		return false;
	Id = Package.Id;
	return true;
}

bool FMapPackage::Build(Object& Root, const FString& Id, FAssetPackage& Package, FString& Error, bool bPrefab)
{
	FValidationContext Validation(Error);

	Package = {};
	Package.Id = Id;
	Package.Kind = bPrefab ? EAssetPackageKind::Asset : EAssetPackageKind::Map;
	Package.Schema = bPrefab ? "Genesis.Prefab" : "Genesis.World";
	Package.SchemaVersion = 3;
	if (const FMapGameSet* Set = FMapEditorEnvironment::GetActiveGameSet())
	{
		Package.Exports["GameSet"]["Game"] = Set->Game;
		Package.Exports["GameSet"]["Entities"] = Set->EntFile;
	}
	FWorld World;
	const J Legacy = WriteObject(Validation, Root, Package, 0);
	if (!Validation.IsValid())
	{
		return false;
	}
	if (!FMapWorldAdapter::ToRuntime(Legacy, World, Error))
		return false;
	Package.Exports["Scene"] = World.ToJson();
	TSet<std::string> Seen;
	std::erase_if(Package.Dependencies, [&](const FString& Path) { return !Seen.insert(Path.ToStdString()).second; });
	return true;
}

TUniquePtr<Object> FMapPackage::Load(
    const FString& Path, FString& Id, FString& Error, bool bPrefab, const FString& ExpectedId, bool* bUpgraded)
{
	FAssetPackage Package;
	if (!FAssetPackageStore::Load(Path, Package, Error))
		return {};
	if (!ExpectedId.IsEmpty() && Package.Id != ExpectedId)
	{
		Error = GENESIS_TEXT("The prefab identity changed since discovery; refresh content before placing it.");
		return {};
	}
	if (Package.Kind != (bPrefab ? EAssetPackageKind::Asset : EAssetPackageKind::Map) ||
	    Package.Schema != (bPrefab ? "Genesis.Prefab" : "Genesis.World") ||
	    (Package.SchemaVersion < 1 || Package.SchemaVersion > 3))
	{
		Error = GENESIS_TEXT("Unsupported Genesis world schema.");
		return {};
	}
	const FMapGameSet* PreviousSet = FMapEditorEnvironment::GetActiveGameSet();
	FValidationContext Validation(Error);
	FScopeExit Rollback([&]
	{
		if (PreviousSet && FMapEditorEnvironment::GetActiveGameSet() != PreviousSet)
			if (!FMapEditorEnvironment::Activate(*PreviousSet))
				Error += GENESIS_TEXT(" The original entity definition set could not be restored.");
	});

	FJsonValue LegacyRoot;
	if (Package.SchemaVersion >= 2)
	{
		const TOptional<FWorld> World = FWorld::FromJson(Package.Exports.Get("Scene"), Error);
		if (!World || !FMapWorldAdapter::ToLegacy(*World, LegacyRoot, Error))
			return {};
	}
	else
		LegacyRoot = Package.Exports.Get("Root");
	const FJsonValue& SetData = Package.Exports.Get("GameSet");
	const FMapGameSet* Set =
	    FMapEditorEnvironment::FindGameSet(SetData.Get("Game").AsString(), SetData.Get("Entities").AsString());
	if (!Validation.Check(Set != nullptr, GENESIS_TEXT("This world's entity definition set is unavailable.")))
	{
		return {};
	}

	if (!Validation.Check(!bPrefab || PreviousSet == Set,
	        GENESIS_TEXT("Convert the prefab to this world's entity definition set before placing it.")))
	{
		return {};
	}

	if (FMapEditorEnvironment::GetActiveGameSet() != Set && !FMapEditorEnvironment::Activate(*Set, &Error))
		return {};
	int Total = 0;
	TUniquePtr<Object> Root = ReadObject(Validation, LegacyRoot, Package, 0, Total);
	if (!Validation.IsValid())
	{
		return {};
	}

	if (bUpgraded)
		*bUpgraded = Package.SchemaVersion < 3;
	Id = Package.Id;
	Rollback.Release();
	return Root;
}
