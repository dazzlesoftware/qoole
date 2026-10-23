// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/WorldGeometry.h"
#include "Landscape/Landscape.h"
#include "Foliage/Foliage.h"
#include <bit>
#include <cmath>
#include <stdexcept>
namespace
{
using J = FJsonValue;
bool ParseTextureToken(const FString& Token, FAssetReference& Out, FString& Error)
{
	return FWorldGeometryCodec::ParseTextureToken(Token, Out, Error);
}
bool LoadTexture(const FString& Token, FTextureAsset& Out, FString& Error, const FAssetRegistry* Registry)
{
	FAssetReference Ref;
	if (!ParseTextureToken(Token, Ref, Error))
		return false;
	if (!Registry)
	{
		Error = "This landscape texture requires an asset registry.";
		return false;
	}
	return FRenderAssetLoader(*Registry).LoadTexture(Ref, Out, Error);
}
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
	Require(Value.size() <= 65536 && Value.find('\0') == std::string::npos, "Invalid bounded string.");
	return FString::FromAnsi(Value);
}
J Ansi(const char* Value)
{
	return FString::FromAnsi(Value ? Value : "");
}
J AddFloats(FAssetPackage& Package, const std::vector<float>& Values)
{
	auto Ref = J::MakeObject();
	Ref["Offset"] = static_cast<double>(Package.Bulk.size());
	Ref["Count"] = static_cast<double>(Values.size());
	for (const float Value : Values)
	{
		Require(std::isfinite(Value) && std::abs(Value) <= 1.0e9f, "Cannot save out-of-range bulk values.");
		const uint32 Bits = std::bit_cast<uint32>(Value);
		for (int Shift = 0; Shift < 32; Shift += 8)
			Package.Bulk.push_back(static_cast<uint8>(Bits >> Shift));
	}
	return Ref;
}
std::vector<float> Floats(const FAssetPackage& Package, const J& Ref, int Count)
{
	Require(Ref.IsObject(), "Missing bulk reference.");
	const int Offset = Integer(Ref.Get("Offset"), 0, INT32_MAX);
	Require(Integer(Ref.Get("Count"), 0, INT32_MAX) == Count &&
				static_cast<uint64>(Offset) + static_cast<uint64>(Count) * 4 <= Package.Bulk.size(),
			"Invalid bulk range.");
	std::vector<float> Values;
	Values.reserve(Count);
	for (int I = 0; I < Count; ++I)
	{
		uint32 Bits = 0;
		for (int B = 0; B < 4; ++B)
			Bits |= static_cast<uint32>(Package.Bulk[Offset + static_cast<size_t>(I) * 4 + B]) << (B * 8);
		const float Value = std::bit_cast<float>(Bits);
		Require(std::isfinite(Value) && std::abs(Value) <= 1.0e9f, "Invalid float in bulk data.");
		Values.push_back(Value);
	}
	return Values;
}
J WriteSpline(const LandscapeSpline& S)
{
	auto Out = J::MakeObject();
	Out["Closed"] = S.closed;
	Out["Raise"] = S.raiseTerrain;
	Out["Lower"] = S.lowerTerrain;
	Out["Layer"] = Ansi(S.layer);
	Out["Mesh"] = Ansi(S.mesh);
	Out["MeshScale"] = S.meshScale;
	Out["FitWidth"] = S.meshFitWidth;
	Out["ZOffset"] = S.meshZOffset;
	Out["Points"] = J::MakeArray();
	for (const auto& P : S.points)
	{
		auto Point = J::MakeObject();
		Point["Position"] = Vec(P.position);
		Point["Width"] = P.width;
		Point["Falloff"] = P.falloff;
		Point["AutoTangent"] = P.autoTangent;
		Point["Tangent"] = Vec(P.tangent);
		Point["Junction"] = P.joint;
		Out["Points"].Push(std::move(Point));
	}
	return Out;
}
LandscapeSpline ReadSpline(const J& In)
{
	LandscapeSpline S;
	S.closed = Boolean(In.Get("Closed"));
	S.raiseTerrain = Boolean(In.Get("Raise"));
	S.lowerTerrain = Boolean(In.Get("Lower"));
	S.layer = Text(In.Get("Layer"));
	S.mesh = Text(In.Get("Mesh"));
	S.meshScale = Number(In.Get("MeshScale"));
	S.meshFitWidth = Boolean(In.Get("FitWidth"));
	S.meshZOffset = Number(In.Get("ZOffset"));
	Require(S.meshScale > 0 && S.meshScale <= 1000, "Invalid spline mesh scale.");
	for (const J& Point : Array(In.Get("Points"), 10000))
	{
		LandscapeSplinePoint P;
		P.position = ReadVec(Point.Get("Position"));
		P.width = Number(Point.Get("Width"));
		P.falloff = Number(Point.Get("Falloff"));
		Require(P.width >= 0 && P.falloff >= 0, "Invalid spline width or falloff.");
		P.autoTangent = Boolean(Point.Get("AutoTangent"));
		P.tangent = ReadVec(Point.Get("Tangent"));
		P.joint = Integer(Point.Get("Junction"), -1, INT32_MAX);
		S.points.push_back(P);
	}
	return S;
}
} // namespace
bool FWorldGeometryCodec::EncodeLandscape(const Landscape& L, FAssetPackage& Package, FJsonValue& Result,
										  FString& Error)
{
	const auto BulkSize = Package.Bulk.size(), DepSize = Package.Dependencies.size();
	try
	{
		Require(std::isfinite(L.GetQuadSize()) && L.GetQuadSize() >= 1 && L.GetQuadSize() <= 1.0e6f,
				"Invalid landscape quad size.");
		Require(L.GetLayers().size() <= 256 && L.GetSplines().size() <= 10000, "Too many landscape layers or splines.");
		J Out;
		auto Land = J::MakeObject();
		Land["QuadsX"] = L.GetQuadsX();
		Land["QuadsY"] = L.GetQuadsY();
		Land["QuadSize"] = L.GetQuadSize();
		Land["Heights"] = AddFloats(Package, L.GetHeights());
		Land["Layers"] = J::MakeArray();
		Land["Splines"] = J::MakeArray();
		for (const auto& Layer : L.GetLayers())
		{
			Require(std::isfinite(Layer.tiling) && Layer.tiling > 0 && Layer.tiling <= 1.0e9f, "Invalid layer tiling.");
			Require(Layer.weights.size() == L.GetHeights().size(), "Invalid paint weight count.");
			for (float W : Layer.weights)
				Require(std::isfinite(W) && W >= 0 && W <= 1, "Invalid paint weight.");
			auto Data = J::MakeObject();
			Data["Name"] = Ansi(Layer.name);
			Data["Texture"] = Ansi(Layer.texture);
			Data["Tiling"] = Layer.tiling;
			if (Layer.texture.rfind("asset:", 0) == 0)
			{
				FAssetReference Ref;
				FString TokenError;
				Require(ParseTextureToken(FString(Layer.texture), Ref, TokenError),
						"Invalid native texture reference.");
				Data["Texture"] = FString(Layer.texture);
				if (!Ref.Path.IsEmpty())
					Package.Dependencies.push_back(Ref.Path);
			}
			Data["Color"] = Vec(Vector3d(Layer.r, Layer.g, Layer.b));
			Data["Weights"] = AddFloats(Package, Layer.weights);
			Land["Layers"].Push(std::move(Data));
		}
		for (const auto& Spline : L.GetSplines())
		{
			auto Data = WriteSpline(Spline);
			(void)ReadSpline(Data);
			Land["Splines"].Push(std::move(Data));
		}
		Out = std::move(Land);
		Result = std::move(Out);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Package.Bulk.resize(BulkSize);
		Package.Dependencies.resize(DepSize);
		Error = E.what();
		return false;
	}
}
bool FWorldGeometryCodec::EncodeFoliage(const Foliage& F, FAssetPackage& Package, FJsonValue& Result, FString& Error)
{
	const auto BulkSize = Package.Bulk.size(), DepSize = Package.Dependencies.size();
	try
	{
		Require(F.GetTypes().size() <= 1024 && F.GetInstances().size() <= 1000000, "Too much foliage data.");
		for (const auto& I : F.GetInstances())
			Require(I.type >= 0 && I.type < F.GetNumTypes() && I.scale > 0 && I.up.GetMagSqr() > .000001f,
					"Invalid foliage transform.");
		J Out;
		auto Data = J::MakeObject();
		Data["Types"] = J::MakeArray();
		for (const auto& Type : F.GetTypes())
		{
			auto T = J::MakeObject();
			T["Name"] = Ansi(Type.name);
			T["Mesh"] = Ansi(Type.mesh);
			T["Density"] = Type.density;
			T["Radius"] = Type.radius;
			T["ScaleMin"] = Type.scaleMin;
			T["ScaleMax"] = Type.scaleMax;
			T["Align"] = Type.alignToNormal;
			T["AlignAngle"] = Type.alignMaxAngle;
			T["RandomYaw"] = Type.randomYaw;
			T["SlopeMin"] = Type.slopeMin;
			T["SlopeMax"] = Type.slopeMax;
			T["HeightMin"] = Type.heightMin;
			T["HeightMax"] = Type.heightMax;
			T["ZOffset"] = Type.zOffset;
			T["CullDistance"] = Type.cullDistance;
			for (const char* Key : {"Density", "Radius", "ScaleMin", "ScaleMax", "AlignAngle", "SlopeMin", "SlopeMax",
									"HeightMin", "HeightMax", "ZOffset", "CullDistance"})
				(void)Number(T.Get(Key));
			Data["Types"].Push(std::move(T));
		}
		std::vector<float> Values;
		for (const auto& I : F.GetInstances())
			for (float V : {static_cast<float>(I.type), I.position.GetX(), I.position.GetY(), I.position.GetZ(),
							I.up.GetX(), I.up.GetY(), I.up.GetZ(), I.yaw, I.scale})
				Values.push_back(V);
		Data["Count"] = F.GetNumInstances();
		Data["Instances"] = AddFloats(Package, Values);
		Out = std::move(Data);
		Result = std::move(Out);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Package.Bulk.resize(BulkSize);
		Package.Dependencies.resize(DepSize);
		Error = E.what();
		return false;
	}
}
bool FWorldGeometryCodec::DecodeLandscape(const FAssetPackage& Package, const FJsonValue& L,
										  std::shared_ptr<const Landscape>& Result, FString& Error,
										  const FAssetRegistry* Registry)
{
	try
	{
		std::shared_ptr<const Landscape> Out;
		const int QX = Integer(L.Get("QuadsX"), 1, Landscape::MaxQuads),
				  QY = Integer(L.Get("QuadsY"), 1, Landscape::MaxQuads);
		const int Count = (QX + 1) * (QY + 1);
		const float Size = Number(L.Get("QuadSize"));
		Require(Size >= 1 && Size <= 1.0e6f, "Invalid landscape quad size.");
		std::vector<LandscapeLayer> Layers;
		for (const auto& Data : Array(L.Get("Layers"), 256))
		{
			LandscapeLayer Layer;
			Layer.name = Text(Data.Get("Name"));
			Layer.texture = Text(Data.Get("Texture"));
			Layer.tiling = Number(Data.Get("Tiling"));
			if (Data.Get("Texture").AsString().View().substr(0, 6) == "asset:")
			{
				Layer.texture = Data.Get("Texture").AsString().ToStdString();
				FTextureAsset Texture;
				FString TextureError;
				if (!LoadTexture(FString(Layer.texture), Texture, TextureError, Registry))
					throw std::runtime_error(TextureError.ToStdString());
			}
			Require(Layer.tiling > 0, "Invalid layer tiling.");
			const auto& Color = Data.Get("Color");
			Require(Array(Color, 3).size() == 3, "Invalid layer color.");
			Layer.r = static_cast<unsigned char>(Integer(Color[0], 0, 255));
			Layer.g = static_cast<unsigned char>(Integer(Color[1], 0, 255));
			Layer.b = static_cast<unsigned char>(Integer(Color[2], 0, 255));
			Layer.weights = Floats(Package, Data.Get("Weights"), Count);
			for (float W : Layer.weights)
				Require(W >= 0 && W <= 1, "Invalid paint weight.");
			Layers.push_back(std::move(Layer));
		}
		std::vector<LandscapeSpline> Splines;
		for (const auto& S : Array(L.Get("Splines"), 10000))
			Splines.push_back(ReadSpline(S));
		auto Land = Landscape::Create(QX, QY, Size, Floats(Package, L.Get("Heights"), Count), std::move(Layers),
									  std::move(Splines));
		Require(Land != nullptr, "Cannot construct landscape.");
		Out = Land;
		Result = std::move(Out);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
bool FWorldGeometryCodec::DecodeFoliage(const FAssetPackage& Package, const FJsonValue& F,
										std::shared_ptr<const Foliage>& Result, FString& Error, const FAssetRegistry*)
{
	try
	{
		std::shared_ptr<const Foliage> Out;
		std::vector<FoliageType> Types;
		for (const auto& T : Array(F.Get("Types"), 1024))
		{
			FoliageType Type;
			Type.name = Text(T.Get("Name"));
			Type.mesh = Text(T.Get("Mesh"));
			Type.density = Number(T.Get("Density"));
			Type.radius = Number(T.Get("Radius"));
			Type.scaleMin = Number(T.Get("ScaleMin"));
			Type.scaleMax = Number(T.Get("ScaleMax"));
			Type.alignToNormal = Boolean(T.Get("Align"));
			Type.alignMaxAngle = Number(T.Get("AlignAngle"));
			Type.randomYaw = Boolean(T.Get("RandomYaw"));
			Type.slopeMin = Number(T.Get("SlopeMin"));
			Type.slopeMax = Number(T.Get("SlopeMax"));
			Type.heightMin = Number(T.Get("HeightMin"));
			Type.heightMax = Number(T.Get("HeightMax"));
			Type.zOffset = Number(T.Get("ZOffset"));
			Type.cullDistance = Number(T.Get("CullDistance"));
			Types.push_back(std::move(Type));
		}
		const int Count = Integer(F.Get("Count"), 0, 1000000);
		const auto Values = Floats(Package, F.Get("Instances"), Count * 9);
		std::vector<FoliageInstance> Instances;
		for (int I = 0; I < Count; ++I)
		{
			const size_t O = static_cast<size_t>(I) * 9;
			Require(Values[O] >= 0 && Values[O] < Types.size() && Values[O] == std::floor(Values[O]),
					"Invalid foliage type index.");
			FoliageInstance Instance;
			Instance.type = static_cast<int>(Values[O]);
			Instance.position = Vector3d(Values[O + 1], Values[O + 2], Values[O + 3]);
			Instance.up = Vector3d(Values[O + 4], Values[O + 5], Values[O + 6]);
			Instance.yaw = Values[O + 7];
			Instance.scale = Values[O + 8];
			Require(Instance.scale > 0 && Instance.up.GetMagSqr() > 0.000001f, "Invalid foliage transform.");
			Instances.push_back(Instance);
		}
		Out = std::make_shared<Foliage>(std::move(Types), std::move(Instances));
		Result = std::move(Out);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
bool FWorldGeometryCodec::ParseTextureToken(const FString& Token, FAssetReference& Out, FString& Error)
{
	if (Token.View().substr(0, 6) != "asset:")
	{
		Error = "Expected a native texture reference.";
		return false;
	}
	const auto Json = FJsonValue::Parse(Token.View().substr(6), &Error);
	if (!Json)
		return false;
	const auto Ref = FAssetReference::FromJson(*Json, Error);
	if (!Ref || Ref->IsNull())
	{
		Error = "Invalid native texture reference.";
		return false;
	}
	Out = *Ref;
	Error.Empty();
	return true;
}

bool FWorldGeometryCodec::GetAssetReferences(const FString& Type, const FJsonValue& Payload,
											 TArray<FAssetReference>& Out, FString& Error)
{
	try
	{

		TArray<FAssetReference> Refs;
		if (Type == "Genesis.StaticMesh")
		{
			FStaticMeshComponentData Mesh;
			if (!DecodeMesh(Payload, Mesh, Error))
				return false;
			if (Mesh.IsNative())
			{
				Refs.push_back(Mesh.Asset);
				if (!Mesh.MaterialOverride.IsNull())
					Refs.push_back(Mesh.MaterialOverride);
				for (const auto& Ref : Mesh.AdditionalOverrides)
					if (!Ref.IsNull())
						Refs.push_back(Ref);
			}
		}
		if (Type == "Genesis.Landscape")
			for (const auto& Layer : Payload.Get("Layers").GetItems())
			{
				const auto Token = Layer.Get("Texture").AsString();
				if (Token.View().substr(0, 6) == "asset:")
				{
					FAssetReference Ref;
					FString RefError;
					Require(FWorldGeometryCodec::ParseTextureToken(Token, Ref, RefError),
							"Invalid landscape texture reference.");
					Refs.push_back(Ref);
				}
			}
		Out = std::move(Refs);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}

bool FWorldGeometryCodec::DecodeMesh(const FJsonValue& Data, FStaticMeshComponentData& Result, FString& Error)
{
	try
	{
		FStaticMeshComponentData Out;
		if (Data.GetType() == EJsonType::String)
		{
			Out.BuiltIn = Data.AsString();
			Require(Out.BuiltIn.Len() <= 65536 && Out.BuiltIn.View().find('\0') == std::string_view::npos,
					"Invalid built-in mesh name.");
			Require(StaticMesh::Find(Out.BuiltIn.ToAnsi().c_str()) != nullptr, "Unknown built-in static mesh.");
		}
		else
		{
			Require(Data.IsObject(), "Invalid static mesh component payload.");
			const auto Source = FAssetReference::FromJson(Data.Get("Asset"), Error),
					   Override = FAssetReference::FromJson(Data.Get("MaterialOverride"), Error);
			Require(Source && !Source->IsNull() && Override, "Invalid mesh asset reference.");
			Out.Asset = *Source;
			Out.MaterialOverride = *Override;
			const auto& Values = Data.Get("AdditionalOverrides");
			Require(Values.IsNull() || (Values.IsArray() && Values.Num() <= 63), "Invalid material overrides.");
			for (const auto& V : Values.GetItems())
			{
				const auto Ref = FAssetReference::FromJson(V, Error);
				Require(Ref.has_value(), "Invalid slot override.");
				Out.AdditionalOverrides.push_back(*Ref);
			}
		}
		Result = std::move(Out);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
bool FWorldGeometryCodec::EncodeMesh(const FStaticMeshComponentData& Data, FJsonValue& Result, FString& Error)
{
	try
	{
		FJsonValue Out;
		if (!Data.IsNative())
		{
			Require(Data.MaterialOverride.IsNull() && Data.AdditionalOverrides.empty(),
					"Built-in mesh components cannot carry native material overrides.");
			Out = Data.BuiltIn;
		}
		else
		{
			Require(Data.BuiltIn.IsEmpty(), "Mesh component cannot have both a built-in and an asset source.");
			Require(Data.AdditionalOverrides.size() <= 63, "Too many material overrides.");
			Out = FJsonValue::MakeObject();
			Out["Asset"] = Data.Asset.ToJson();
			Out["MaterialOverride"] = Data.MaterialOverride.ToJson();
			Out["AdditionalOverrides"] = FJsonValue::MakeArray();
			for (const auto& Ref : Data.AdditionalOverrides)
				Out["AdditionalOverrides"].Push(Ref.ToJson());
		}
		FStaticMeshComponentData Validated;
		if (!DecodeMesh(Out, Validated, Error))
			return false;
		Result = std::move(Out);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
