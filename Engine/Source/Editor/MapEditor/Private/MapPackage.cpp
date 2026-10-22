// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapPackage.h"
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
	void Require(bool Value, const char* Error) { if (!Value) throw std::runtime_error(Error); }
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
		Require(Value.GetType() == EJsonType::String && Value.AsString().Len() <= 65536, "Expected a bounded UTF-8 string.");
		Require(Value.AsString().View().find('\0') == std::string_view::npos, "Embedded NUL in string.");
		return Value.AsString().ToAnsi();
	}
	bool Boolean(const J& Value) { Require(Value.GetType() == EJsonType::Bool, "Expected a boolean."); return Value.AsBool(); }
	const TArray<J>& Array(const J& Value, int Max = 1000000)
	{
		Require(Value.IsArray() && Value.Num() <= Max, "Expected a bounded array.");
		return Value.GetItems();
	}
	J Vec(const Vector3d& V)
	{
		auto Out = J::MakeArray();
		for (float N : { V.GetX(), V.GetY(), V.GetZ() }) { Require(std::isfinite(N), "Cannot save nonfinite vectors."); Out.Push(N); }
		return Out;
	}
	Vector3d ReadVec(const J& Value)
	{
		Require(Array(Value, 3).size() == 3, "Expected three vector coordinates.");
		return Vector3d(Number(Value[0]), Number(Value[1]), Number(Value[2]));
	}
	J Ansi(const std::string& Value) { return FString::FromAnsi(Value); }
	J Ansi(const char* Value) { return FString::FromAnsi(Value ? Value : ""); }
	J AddFloats(FAssetPackage& Package, const std::vector<float>& Values)
	{
		auto Ref = J::MakeObject();
		Ref["Offset"] = static_cast<double>(Package.Bulk.size());
		Ref["Count"] = static_cast<double>(Values.size());
		for (const float Value : Values)
		{
			Require(std::isfinite(Value), "Cannot save nonfinite bulk values.");
			const uint32 Bits = std::bit_cast<uint32>(Value);
			for (int Shift = 0; Shift < 32; Shift += 8) Package.Bulk.push_back(static_cast<uint8>(Bits >> Shift));
		}
		return Ref;
	}
	std::vector<float> Floats(const FAssetPackage& Package, const J& Ref, int Count)
	{
		Require(Ref.IsObject(), "Missing bulk reference.");
		const int Offset = Integer(Ref.Get("Offset"), 0, INT32_MAX);
		Require(Integer(Ref.Get("Count"), 0, INT32_MAX) == Count &&
			static_cast<uint64>(Offset) + static_cast<uint64>(Count) * 4 <= Package.Bulk.size(), "Invalid bulk range.");
		std::vector<float> Values;
		Values.reserve(Count);
		for (int I = 0; I < Count; ++I)
		{
			uint32 Bits = 0;
			for (int B = 0; B < 4; ++B) Bits |= static_cast<uint32>(Package.Bulk[Offset + static_cast<size_t>(I) * 4 + B]) << (B * 8);
			const float Value = std::bit_cast<float>(Bits);
			Require(std::isfinite(Value) && std::abs(Value) <= 1.0e9f, "Invalid float in bulk data.");
			Values.push_back(Value);
		}
		return Values;
	}
	J WriteSpline(const LandscapeSpline& S)
	{
		auto Out = J::MakeObject();
		Out["Closed"] = S.closed; Out["Raise"] = S.raiseTerrain; Out["Lower"] = S.lowerTerrain;
		Out["Layer"] = Ansi(S.layer); Out["Mesh"] = Ansi(S.mesh); Out["MeshScale"] = S.meshScale;
		Out["FitWidth"] = S.meshFitWidth; Out["ZOffset"] = S.meshZOffset;
		Out["Points"] = J::MakeArray();
		for (const auto& P : S.points)
		{
			auto Point = J::MakeObject();
			Point["Position"] = Vec(P.position); Point["Width"] = P.width; Point["Falloff"] = P.falloff;
			Point["AutoTangent"] = P.autoTangent; Point["Tangent"] = Vec(P.tangent); Point["Junction"] = P.joint;
			Out["Points"].Push(std::move(Point));
		}
		return Out;
	}
	LandscapeSpline ReadSpline(const J& In)
	{
		LandscapeSpline S;
		S.closed = Boolean(In.Get("Closed")); S.raiseTerrain = Boolean(In.Get("Raise")); S.lowerTerrain = Boolean(In.Get("Lower"));
		S.layer = Text(In.Get("Layer")); S.mesh = Text(In.Get("Mesh")); S.meshScale = Number(In.Get("MeshScale"));
		S.meshFitWidth = Boolean(In.Get("FitWidth")); S.meshZOffset = Number(In.Get("ZOffset"));
		Require(S.meshScale > 0 && S.meshScale <= 1000, "Invalid spline mesh scale.");
		for (const J& Point : Array(In.Get("Points"), 10000))
		{
			LandscapeSplinePoint P;
			P.position = ReadVec(Point.Get("Position")); P.width = Number(Point.Get("Width")); P.falloff = Number(Point.Get("Falloff"));
			Require(P.width >= 0 && P.falloff >= 0, "Invalid spline width or falloff.");
			P.autoTangent = Boolean(Point.Get("AutoTangent")); P.tangent = ReadVec(Point.Get("Tangent"));
			P.joint = Integer(Point.Get("Junction"), -1, INT32_MAX); S.points.push_back(P);
		}
		return S;
	}
	J WriteObject(Object& Obj, FAssetPackage& Package, int Depth)
	{
		Require(Depth < 128, "World hierarchy is too deep.");
		auto Out = J::MakeObject();
		Out["Name"] = Ansi(Obj.GetObjName()); Out["Position"] = Vec(Obj.GetPosition()); Out["Scale"] = Vec(Obj.GetScale());
		Out["Identity"] = Obj.GetIdentity().ToJson();
		const auto& R = Obj.GetOrientation();
		Out["Rotation"] = Vec(Vector3d(R.GetYaw(), R.GetPitch(), R.GetRoll()));
		if (Obj.HasEntity())
		{
			auto& E = *Obj.GetEntityPtr();
			auto EntityData = J::MakeObject();
			EntityData["Class"] = Ansi(E.GetClassname()); EntityData["Keys"] = J::MakeObject();
			for (int I = 0; I < E.GetNumKeys(); ++I) EntityData["Keys"][FString::FromAnsi(E.GetKeyNum(I)).View()] = Ansi(E.GetKey(E.GetKeyNum(I)));
			EntityData["Origin"] = Vec(E.GetOrigin()); EntityData["Angle"] = E.GetAngle(); EntityData["MAngle"] = Vec(E.GetMAngle());
			auto Flags = J::MakeArray(), Hidden = J::MakeArray();
			for (int I = 0; I < ENTITY_FLAGS; ++I) { Flags.Push(E.IsAttribSet(I)); Hidden.Push(E.IsNotUserAttrib(I)); }
			EntityData["Flags"] = Flags; EntityData["Hidden"] = Hidden; Out["Entity"] = std::move(EntityData);
		}
		if (Obj.HasBrush())
		{
			auto Brush = J::MakeArray(); auto& G = Obj.GetBrush();
			for (int F = 0; F < G.GetNumFaces(); ++F)
			{
				auto Face = J::MakeObject(); auto Vertices = J::MakeArray(); auto& Poly = G.GetFace(F);
				for (int I = 0; I < Poly.GetNumSides(); ++I) Vertices.Push(Vec(G.GetVertex(Poly.GetSide(I).GetStartPoint())));
				Face["Vertices"] = std::move(Vertices); auto* Tex = Poly.GetTexturePtr();
				Face["Texture"] = Ansi(Tex->GetTName() ? Tex->GetTName() : "");
				int X, Y; float Rot, SX, SY; unsigned int A, B, C;
				Tex->GetTInfo(X, Y, Rot, SX, SY); Tex->GetTAttribs(A, B, C);
				Face["OffsetX"] = X; Face["OffsetY"] = Y; Face["Rotation"] = Rot; Face["ScaleX"] = SX; Face["ScaleY"] = SY;
				Face["Attrib1"] = static_cast<double>(A); Face["Attrib2"] = static_cast<double>(B); Face["Attrib3"] = static_cast<double>(C);
				Face["Locked"] = Tex->IsTexLocked(); Brush.Push(std::move(Face));
			}
			Out["Brush"] = std::move(Brush);
		}
		if (Obj.HasLandscape())
		{
			const auto& L = Obj.GetLandscape(); auto Land = J::MakeObject();
			Land["QuadsX"] = L.GetQuadsX(); Land["QuadsY"] = L.GetQuadsY(); Land["QuadSize"] = L.GetQuadSize();
			Land["Heights"] = AddFloats(Package, L.GetHeights()); Land["Layers"] = J::MakeArray(); Land["Splines"] = J::MakeArray();
			for (const auto& Layer : L.GetLayers())
			{
				auto Data = J::MakeObject(); Data["Name"] = Ansi(Layer.name); Data["Texture"] = Ansi(Layer.texture); Data["Tiling"] = Layer.tiling;
				if(Layer.texture.rfind("asset:",0)==0) {
					FAssetReference Ref; FString Error; Require(FMapAssetLibrary::ParseTextureToken(FString(Layer.texture),Ref,Error),"Invalid native texture reference.");
					Data["Texture"]=FString(Layer.texture); if(!Ref.Path.IsEmpty()) Package.Dependencies.push_back(Ref.Path);
				}
				Data["Color"] = Vec(Vector3d(Layer.r, Layer.g, Layer.b)); Data["Weights"] = AddFloats(Package, Layer.weights);
				Land["Layers"].Push(std::move(Data));
			}
			for (const auto& Spline : L.GetSplines()) Land["Splines"].Push(WriteSpline(Spline));
			Out["Landscape"] = std::move(Land);
		}
		else if (Obj.HasMesh())
		{
			if (const auto Asset=Obj.GetMeshAsset())
			{
				Out["Mesh"]["Asset"]=Asset->Source.ToJson(); Out["Mesh"]["MaterialOverride"]=Asset->MaterialOverride.ToJson();
				for(const auto& Ref : {Asset->Source,Asset->MaterialOverride}) if(!Ref.Path.IsEmpty()) Package.Dependencies.push_back(Ref.Path);
			}
			else {
			// Today's placed meshes come from the built-in library. Reject custom
			// geometry rather than silently discarding it during this transition.
			Require(StaticMesh::Find(Obj.GetMeshName()).get() == Obj.GetMeshPtr().get(), "Custom meshes need an asset serializer before saving.");
			Out["Mesh"] = Ansi(Obj.GetMeshName());
			}
		}
		if (Obj.HasFoliage())
		{
			auto Data = J::MakeObject(); Data["Types"] = J::MakeArray();
			for (const auto& Type : Obj.GetFoliage().GetTypes())
			{
				auto T = J::MakeObject(); T["Name"] = Ansi(Type.name); T["Mesh"] = Ansi(Type.mesh);
				T["Density"] = Type.density; T["Radius"] = Type.radius; T["ScaleMin"] = Type.scaleMin; T["ScaleMax"] = Type.scaleMax;
				T["Align"] = Type.alignToNormal; T["AlignAngle"] = Type.alignMaxAngle; T["RandomYaw"] = Type.randomYaw;
				T["SlopeMin"] = Type.slopeMin; T["SlopeMax"] = Type.slopeMax; T["HeightMin"] = Type.heightMin; T["HeightMax"] = Type.heightMax;
				T["ZOffset"] = Type.zOffset; T["CullDistance"] = Type.cullDistance; Data["Types"].Push(std::move(T));
			}
			std::vector<float> Values;
			for (const auto& I : Obj.GetFoliage().GetInstances())
				for (float V : { static_cast<float>(I.type), I.position.GetX(), I.position.GetY(), I.position.GetZ(), I.up.GetX(), I.up.GetY(), I.up.GetZ(), I.yaw, I.scale }) Values.push_back(V);
			Data["Count"] = Obj.GetFoliage().GetNumInstances(); Data["Instances"] = AddFloats(Package, Values); Out["Foliage"] = std::move(Data);
		}
		Out["Children"] = J::MakeArray();
		for (int I = 0; I < Obj.GetNumChildren(); ++I) Out["Children"].Push(WriteObject(Obj.GetChild(I), Package, Depth + 1));
		return Out;
	}
	TUniquePtr<Object> ReadObject(const J& In, const FAssetPackage& Package, int Depth, int& Total)
	{
		Require(In.IsObject() && Depth < 128 && ++Total <= 100000, "Invalid or excessive world hierarchy.");
		auto Obj = MakeUnique<Object>();
		if (In.Contains("Identity"))
		{
			FString Error; const auto Identity = FActorIdentity::FromJson(In.Get("Identity"), Error);
			Require(Identity.has_value(), "Invalid actor/component identity."); Obj->SetIdentity(*Identity);
		}
		Obj->SetObjName(Text(In.Get("Name")).c_str());
		const Vector3d Position = ReadVec(In.Get("Position")), Scale = ReadVec(In.Get("Scale")), Rotation = ReadVec(In.Get("Rotation"));
		Require(std::abs(Scale.GetX()) >= 0.000001f && std::abs(Scale.GetY()) >= 0.000001f && std::abs(Scale.GetZ()) >= 0.000001f, "Zero object scale.");
		const int Payloads = In.Contains("Brush") + In.Contains("Mesh") + In.Contains("Landscape") + In.Contains("Foliage");
		Require(Payloads <= 1, "Conflicting object payloads.");
		if (In.Contains("Entity"))
		{
			const auto& E = In.Get("Entity");
			Require(E.Get("Keys").IsObject() && E.Get("Keys").Num() <= MAX_ENTITY_KEYS, "Invalid entity keys.");
			auto EntityPtr = MakeUnique<Entity>(Text(E.Get("Class")).c_str());
			EntityPtr->ClearKeys();
			for (const auto& Key : E.Get("Keys").GetKeys()) EntityPtr->SetKey(Key.ToAnsi().c_str(), Text(E.Get("Keys").Get(Key.View())).c_str());
			EntityPtr->SetOrigin(ReadVec(E.Get("Origin"))); EntityPtr->SetAngle(Integer(E.Get("Angle"), INT32_MIN, INT32_MAX));
			EntityPtr->SetMAngle(ReadVec(E.Get("MAngle")));
			Require(Array(E.Get("Flags"), ENTITY_FLAGS).size() == ENTITY_FLAGS && Array(E.Get("Hidden"), ENTITY_FLAGS).size() == ENTITY_FLAGS, "Invalid entity flags.");
			for (int I = 0; I < ENTITY_FLAGS; ++I)
			{
				EntityPtr->SetAttribState(I, Boolean(E.Get("Flags")[I]), Boolean(E.Get("Hidden")[I]));
			}
			Obj->SetEntity(EntityPtr.release(), false);
		}
		Require(!Obj->HasEntity() || Payloads == 0 || (In.Contains("Brush") && Obj->IsModelNode()), "Entity conflicts with object payload.");
		if (In.Contains("Brush"))
		{
			const auto& Faces = Array(In.Get("Brush"), 256);
			std::vector<std::vector<Vector3d>> Polygons;
			for (const auto& Face : Faces)
			{
				std::vector<Vector3d> Points;
				for (const auto& V : Array(Face.Get("Vertices"), 256)) Points.push_back(ReadVec(V));
				Polygons.push_back(std::move(Points));
			}
			auto G = TUniquePtr<Geometry>(Geometry::FromPolygons(Polygons));
			Require(G != nullptr, "Invalid brush polygons.");
			for (int I = 0; I < static_cast<int>(Faces.size()); ++I)
			{
				const auto& F = Faces[I]; auto Tex = MakeUnique<FaceTex>(Text(F.Get("Texture")).c_str());
				Tex->SetTInfo(Integer(F.Get("OffsetX"), INT32_MIN, INT32_MAX), Integer(F.Get("OffsetY"), INT32_MIN, INT32_MAX),
					Number(F.Get("Rotation")), Number(F.Get("ScaleX")), Number(F.Get("ScaleY")));
				const auto Attribute = [](const J& V) { const double N = V.AsNumber(-1); Require(N >= 0 && N <= UINT32_MAX && N == std::floor(N), "Invalid texture attributes."); return static_cast<unsigned int>(N); };
				Tex->SetTAttribs(Attribute(F.Get("Attrib1")), Attribute(F.Get("Attrib2")), Attribute(F.Get("Attrib3")));
				Tex->SetTexLock(Boolean(F.Get("Locked"))); G->SetFaceTexture(I, Tex.release());
			}
			Obj->SetBrush(G.release());
		}
		if (In.Contains("Mesh"))
		{
			if(In.Get("Mesh").IsObject()) {
				FString Error; const auto Ref=FAssetReference::FromJson(In.Get("Mesh").Get("Asset"),Error), Override=FAssetReference::FromJson(In.Get("Mesh").Get("MaterialOverride"),Error);
				Require(Ref && !Ref->IsNull() && Override,"Invalid mesh asset reference.");
				const auto Asset=FMapAssetLibrary::LoadMesh(*Ref,*Override,Error); if(!Asset) throw std::runtime_error(Error.ToStdString()); Obj->SetMeshAsset(Asset);
			} else {
			auto Mesh = StaticMesh::Find(Text(In.Get("Mesh")).c_str()); Require(Mesh != nullptr, "Unknown static mesh."); Obj->SetMesh(Mesh);
			}
		}
		if (In.Contains("Landscape"))
		{
			const auto& L = In.Get("Landscape");
			const int QX = Integer(L.Get("QuadsX"), 1, Landscape::MaxQuads), QY = Integer(L.Get("QuadsY"), 1, Landscape::MaxQuads);
			const int Count = (QX + 1) * (QY + 1); const float Size = Number(L.Get("QuadSize")); Require(Size >= 1 && Size <= 1.0e6f, "Invalid landscape quad size.");
			std::vector<LandscapeLayer> Layers;
			for (const auto& Data : Array(L.Get("Layers"), 256))
			{
				LandscapeLayer Layer; Layer.name = Text(Data.Get("Name")); Layer.texture = Text(Data.Get("Texture")); Layer.tiling = Number(Data.Get("Tiling"));
				if(Data.Get("Texture").AsString().View().substr(0,6)=="asset:") {
					Layer.texture=Data.Get("Texture").AsString().ToStdString(); FTextureAsset Texture; FString Error;
					if(!FMapAssetLibrary::LoadTexture(FString(Layer.texture),Texture,Error)) throw std::runtime_error(Error.ToStdString());
				}
				Require(Layer.tiling > 0, "Invalid layer tiling.");
				const auto& Color = Data.Get("Color"); Require(Array(Color, 3).size() == 3, "Invalid layer color.");
				Layer.r = static_cast<unsigned char>(Integer(Color[0], 0, 255)); Layer.g = static_cast<unsigned char>(Integer(Color[1], 0, 255)); Layer.b = static_cast<unsigned char>(Integer(Color[2], 0, 255));
				Layer.weights = Floats(Package, Data.Get("Weights"), Count);
				for (float W : Layer.weights) Require(W >= 0 && W <= 1, "Invalid paint weight.");
				Layers.push_back(std::move(Layer));
			}
			std::vector<LandscapeSpline> Splines;
			for (const auto& S : Array(L.Get("Splines"), 10000)) Splines.push_back(ReadSpline(S));
			auto Land = Landscape::Create(QX, QY, Size, Floats(Package, L.Get("Heights"), Count), std::move(Layers), std::move(Splines));
			Require(Land != nullptr, "Cannot construct landscape."); Obj->SetLandscape(Land);
		}
		if (In.Contains("Foliage"))
		{
			const auto& F = In.Get("Foliage"); std::vector<FoliageType> Types;
			for (const auto& T : Array(F.Get("Types"), 1024))
			{
				FoliageType Type; Type.name = Text(T.Get("Name")); Type.mesh = Text(T.Get("Mesh"));
				Type.density = Number(T.Get("Density")); Type.radius = Number(T.Get("Radius")); Type.scaleMin = Number(T.Get("ScaleMin")); Type.scaleMax = Number(T.Get("ScaleMax"));
				Type.alignToNormal = Boolean(T.Get("Align")); Type.alignMaxAngle = Number(T.Get("AlignAngle")); Type.randomYaw = Boolean(T.Get("RandomYaw"));
				Type.slopeMin = Number(T.Get("SlopeMin")); Type.slopeMax = Number(T.Get("SlopeMax")); Type.heightMin = Number(T.Get("HeightMin")); Type.heightMax = Number(T.Get("HeightMax"));
				Type.zOffset = Number(T.Get("ZOffset")); Type.cullDistance = Number(T.Get("CullDistance")); Types.push_back(std::move(Type));
			}
			const int Count = Integer(F.Get("Count"), 0, 1000000); const auto Values = Floats(Package, F.Get("Instances"), Count * 9);
			std::vector<FoliageInstance> Instances;
			for (int I = 0; I < Count; ++I)
			{
				const size_t O = static_cast<size_t>(I) * 9;
				Require(Values[O] >= 0 && Values[O] < Types.size() && Values[O] == std::floor(Values[O]), "Invalid foliage type index.");
				FoliageInstance Instance; Instance.type = static_cast<int>(Values[O]); Instance.position = Vector3d(Values[O+1], Values[O+2], Values[O+3]);
				Instance.up = Vector3d(Values[O+4], Values[O+5], Values[O+6]); Instance.yaw = Values[O+7]; Instance.scale = Values[O+8];
				Require(Instance.scale > 0 && Instance.up.GetMagSqr() > 0.000001f, "Invalid foliage transform."); Instances.push_back(Instance);
			}
			Obj->SetFoliage(std::make_shared<Foliage>(std::move(Types), std::move(Instances)));
		}
		const auto& Children = Array(In.Get("Children"), 100000);
		Require(Payloads == 0 || Children.empty(), "Leaf payload has child objects.");
		Require(!Obj->IsItemNode() || Children.empty(), "Point entity has child objects.");
		for (const auto& Child : Children) { auto Node = ReadObject(Child, Package, Depth + 1, Total); Obj->AddChild(*Node.release(), false); }
		Obj->SetPosition(Position); Obj->SetScale(Scale); Obj->SetOrientation(SphrVector(Rotation.GetX(), Rotation.GetY(), Rotation.GetZ()));
		Obj->SetBoundRadius(false);
		return Obj;
	}
}

bool FMapPackage::Save(const FString& Path, Object& Root, FString& Id, FString& Error, bool bPrefab)
{
	try
	{
		FAssetPackage Package; Package.Id = Id;
		Package.Kind = bPrefab ? EAssetPackageKind::Asset : EAssetPackageKind::Map;
		Package.Schema = bPrefab ? "Genesis.Prefab" : "Genesis.World";
		Package.SchemaVersion = 2;
		if (const auto* Set = FMapEditorEnvironment::GetActiveGameSet())
		{
			Package.Exports["GameSet"]["Game"] = Set->Game;
			Package.Exports["GameSet"]["Entities"] = Set->EntFile;
		}
		FWorld World;
		if (!FMapWorldAdapter::ToRuntime(WriteObject(Root, Package, 0), World, Error)) return false;
		Package.Exports["Scene"] = World.ToJson();
		TSet<std::string> Seen; std::erase_if(Package.Dependencies,[&](const FString& Path) { return !Seen.insert(Path.ToStdString()).second; });
		if (!FAssetPackageStore::Save(Path, Package, Error)) return false;
		Id = Package.Id; return true;
	}
	catch (const std::exception& Exception) { Error = FString::Format("Cannot save world: {}", Exception.what()); return false; }
}

TUniquePtr<Object> FMapPackage::Load(const FString& Path, FString& Id, FString& Error, bool bPrefab, const FString& ExpectedId, bool* bUpgraded)
{
	FAssetPackage Package;
	if (!FAssetPackageStore::Load(Path, Package, Error)) return {};
	if (!ExpectedId.IsEmpty() && Package.Id != ExpectedId)
	{ Error = "The prefab identity changed since discovery; refresh content before placing it."; return {}; }
	if (Package.Kind != (bPrefab ? EAssetPackageKind::Asset : EAssetPackageKind::Map) ||
		Package.Schema != (bPrefab ? "Genesis.Prefab" : "Genesis.World") || (Package.SchemaVersion != 1 && Package.SchemaVersion != 2))
	{ Error = "Unsupported Genesis world schema."; return {}; }
	const auto* PreviousSet = FMapEditorEnvironment::GetActiveGameSet();
	try
	{
		FJsonValue LegacyRoot;
		if (Package.SchemaVersion == 2)
		{
			const auto World = FWorld::FromJson(Package.Exports.Get("Scene"), Error);
			if (!World || !FMapWorldAdapter::ToLegacy(*World, LegacyRoot, Error)) return {};
		}
		else LegacyRoot = Package.Exports.Get("Root");
		const auto& SetData = Package.Exports.Get("GameSet");
		const auto* Set = FMapEditorEnvironment::FindGameSet(SetData.Get("Game").AsString(), SetData.Get("Entities").AsString());
		Require(Set != nullptr, "This world's entity definition set is unavailable.");
		Require(!bPrefab || PreviousSet == Set, "Convert the prefab to this world's entity definition set before placing it.");
		if (FMapEditorEnvironment::GetActiveGameSet() != Set && !FMapEditorEnvironment::Activate(*Set, &Error)) return {};
		int Total = 0; auto Root = ReadObject(LegacyRoot, Package, 0, Total);
		if (bUpgraded) *bUpgraded = Package.SchemaVersion == 1;
		Id = Package.Id; return Root;
	}
	catch (const std::exception& Exception)
	{
		Error = FString::Format("Cannot load world: {}", Exception.what());
		if (PreviousSet && FMapEditorEnvironment::GetActiveGameSet() != PreviousSet)
			if (!FMapEditorEnvironment::Activate(*PreviousSet)) Error += " The original entity definition set could not be restored.";
		return {};
	}
}
