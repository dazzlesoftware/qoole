// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/WorldGeometry.h"
#include "Misc/ValidationContext.h"
#include "Misc/ScopeExit.h"
#include "Internationalization/TextLocalization.h"
#include "Landscape/Landscape.h"
#include "Foliage/Foliage.h"
#include <bit>
#include <cmath>

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
			Error = GENESIS_TEXT("This landscape texture requires an asset registry.");
			return false;
		}
		return FRenderAssetLoader(*Registry).LoadTexture(Ref, Out, Error);
	}

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
	J Ansi(FValidationContext& Validation, const std::string& Value)
	{
		if (!Validation.Check(Value.size() <= 65536 && Value.find('\0') == std::string::npos,
		        GENESIS_TEXT("Invalid bounded string.")))
		{
			return {};
		}
		return FString::FromAnsi(Value);
	}
	J Ansi(FValidationContext&, const char* Value)
	{
		return FString::FromAnsi(Value ? Value : "");
	}
	J AddFloats(FValidationContext& Validation, FAssetPackage& Package, const std::vector<float>& Values)
	{
		FJsonValue Ref = J::MakeObject();
		Ref["Offset"] = static_cast<double>(Package.Bulk.size());
		Ref["Count"] = static_cast<double>(Values.size());
		for (const float Value : Values)
		{
			if (!Validation.Check(std::isfinite(Value) && std::abs(Value) <= 1.0e9f,
			        GENESIS_TEXT("Cannot save out-of-range bulk values.")))
			{
				return {};
			}

			const uint32 Bits = std::bit_cast<uint32>(Value);
			for (int Shift = 0; Shift < 32; Shift += 8)
				Package.Bulk.push_back(static_cast<uint8>(Bits >> Shift));
		}
		return Ref;
	}
	std::vector<float> Floats(FValidationContext& Validation, const FAssetPackage& Package, const J& Ref, int Count)
	{
		if (!Validation.Check(Ref.IsObject(), GENESIS_TEXT("Missing bulk reference.")))
		{
			return {};
		}

		const int Offset = Integer(Validation, Ref.Get("Offset"), 0, INT32_MAX);
		if (!Validation.IsValid())
		{
			return {};
		}

		if (!Validation.Check(Integer(Validation, Ref.Get("Count"), 0, INT32_MAX) == Count &&
		            static_cast<uint64>(Offset) + static_cast<uint64>(Count) * 4 <= Package.Bulk.size(),
		        GENESIS_TEXT("Invalid bulk range.")))
		{
			return {};
		}

		std::vector<float> Values;
		Values.reserve(Count);
		for (int I = 0; I < Count; ++I)
		{
			uint32 Bits = 0;
			for (int B = 0; B < 4; ++B)
				Bits |= static_cast<uint32>(Package.Bulk[Offset + static_cast<size_t>(I) * 4 + B]) << (B * 8);
			const float Value = std::bit_cast<float>(Bits);
			if (!Validation.Check(
			        std::isfinite(Value) && std::abs(Value) <= 1.0e9f, GENESIS_TEXT("Invalid float in bulk data.")))
			{
				return {};
			}

			Values.push_back(Value);
		}
		return Values;
	}
	J WriteSpline(FValidationContext& Validation, const LandscapeSpline& S)
	{
		FJsonValue Out = J::MakeObject();
		Out["Closed"] = S.closed;
		Out["Raise"] = S.raiseTerrain;
		Out["Lower"] = S.lowerTerrain;
		Out["Layer"] = Ansi(Validation, S.layer);
		if (!Validation.IsValid())
		{
			return {};
		}

		Out["Mesh"] = Ansi(Validation, S.mesh);
		if (!Validation.IsValid())
		{
			return {};
		}

		Out["MeshScale"] = S.meshScale;
		Out["FitWidth"] = S.meshFitWidth;
		Out["ZOffset"] = S.meshZOffset;
		Out["Points"] = J::MakeArray();
		for (const LandscapeSplinePoint& P : S.points)
		{
			FJsonValue Point = J::MakeObject();
			Point["Position"] = Vec(Validation, P.position);
			if (!Validation.IsValid())
			{
				return {};
			}

			Point["Width"] = P.width;
			Point["Falloff"] = P.falloff;
			Point["AutoTangent"] = P.autoTangent;
			Point["Tangent"] = Vec(Validation, P.tangent);
			if (!Validation.IsValid())
			{
				return {};
			}

			Point["Junction"] = P.joint;
			Out["Points"].Push(std::move(Point));
		}
		return Out;
	}
	LandscapeSpline ReadSpline(FValidationContext& Validation, const J& In)
	{
		LandscapeSpline S;
		S.closed = Boolean(Validation, In.Get("Closed"));
		if (!Validation.IsValid())
		{
			return {};
		}

		S.raiseTerrain = Boolean(Validation, In.Get("Raise"));
		if (!Validation.IsValid())
		{
			return {};
		}

		S.lowerTerrain = Boolean(Validation, In.Get("Lower"));
		if (!Validation.IsValid())
		{
			return {};
		}

		S.layer = Text(Validation, In.Get("Layer"));
		if (!Validation.IsValid())
		{
			return {};
		}

		S.mesh = Text(Validation, In.Get("Mesh"));
		if (!Validation.IsValid())
		{
			return {};
		}

		S.meshScale = Number(Validation, In.Get("MeshScale"));
		if (!Validation.IsValid())
		{
			return {};
		}

		S.meshFitWidth = Boolean(Validation, In.Get("FitWidth"));
		if (!Validation.IsValid())
		{
			return {};
		}

		S.meshZOffset = Number(Validation, In.Get("ZOffset"));
		if (!Validation.IsValid())
		{
			return {};
		}

		if (!Validation.Check(S.meshScale > 0 && S.meshScale <= 1000, GENESIS_TEXT("Invalid spline mesh scale.")))
		{
			return {};
		}

		for (const J& Point : Array(Validation, In.Get("Points"), 10000))
		{
			LandscapeSplinePoint P;
			P.position = ReadVec(Validation, Point.Get("Position"));
			if (!Validation.IsValid())
			{
				return {};
			}

			P.width = Number(Validation, Point.Get("Width"));
			if (!Validation.IsValid())
			{
				return {};
			}

			P.falloff = Number(Validation, Point.Get("Falloff"));
			if (!Validation.IsValid())
			{
				return {};
			}

			if (!Validation.Check(P.width >= 0 && P.falloff >= 0, GENESIS_TEXT("Invalid spline width or falloff.")))
			{
				return {};
			}

			P.autoTangent = Boolean(Validation, Point.Get("AutoTangent"));
			if (!Validation.IsValid())
			{
				return {};
			}

			P.tangent = ReadVec(Validation, Point.Get("Tangent"));
			if (!Validation.IsValid())
			{
				return {};
			}

			P.joint = Integer(Validation, Point.Get("Junction"), -1, INT32_MAX);
			if (!Validation.IsValid())
			{
				return {};
			}

			S.points.push_back(P);
		}
		if (!Validation.IsValid())
		{
			return {};
		}
		return S;
	}
} // namespace
bool FWorldGeometryCodec::EncodeLandscape(
    const Landscape& L, FAssetPackage& Package, FJsonValue& Result, FString& Error)
{
	const unsigned long long BulkSize = Package.Bulk.size(), DepSize = Package.Dependencies.size();
	FValidationContext Validation(Error);
	FScopeExit Rollback([&]
	{
		Package.Bulk.resize(BulkSize);
		Package.Dependencies.resize(DepSize);
	});

	if (!Validation.Check(std::isfinite(L.GetQuadSize()) && L.GetQuadSize() >= 1 && L.GetQuadSize() <= 1.0e6f,
	        GENESIS_TEXT("Invalid landscape quad size.")))
	{
		return false;
	}

	if (!Validation.Check(L.GetLayers().size() <= 256 && L.GetSplines().size() <= 10000,
	        GENESIS_TEXT("Too many landscape layers or splines.")))
	{
		return false;
	}

	J Out;
	FJsonValue Land = J::MakeObject();
	Land["QuadsX"] = L.GetQuadsX();
	Land["QuadsY"] = L.GetQuadsY();
	Land["QuadSize"] = L.GetQuadSize();
	Land["Heights"] = AddFloats(Validation, Package, L.GetHeights());
	if (!Validation.IsValid())
	{
		return false;
	}

	Land["Layers"] = J::MakeArray();
	Land["Splines"] = J::MakeArray();
	for (const LandscapeLayer& Layer : L.GetLayers())
	{
		if (!Validation.Check(std::isfinite(Layer.tiling) && Layer.tiling > 0 && Layer.tiling <= 1.0e9f,
		        GENESIS_TEXT("Invalid layer tiling.")))
		{
			return false;
		}

		if (!Validation.Check(
		        Layer.weights.size() == L.GetHeights().size(), GENESIS_TEXT("Invalid paint weight count.")))
		{
			return false;
		}

		for (float W : Layer.weights)
		{
			if (!Validation.Check(std::isfinite(W) && W >= 0 && W <= 1, GENESIS_TEXT("Invalid paint weight.")))
			{
				return false;
			}
		}
		FJsonValue Data = J::MakeObject();
		Data["Name"] = Ansi(Validation, Layer.name);
		if (!Validation.IsValid())
		{
			return false;
		}

		Data["Texture"] = Ansi(Validation, Layer.texture);
		if (!Validation.IsValid())
		{
			return false;
		}

		Data["Tiling"] = Layer.tiling;
		if (Layer.texture.rfind("asset:", 0) == 0)
		{
			FAssetReference Ref;
			FString TokenError;
			if (!Validation.Check(ParseTextureToken(FString(Layer.texture), Ref, TokenError),
			        GENESIS_TEXT("Invalid native texture reference.")))
			{
				return false;
			}

			Data["Texture"] = FString(Layer.texture);
			if (!Ref.Path.IsEmpty())
				Package.Dependencies.push_back(Ref.Path);
		}
		Data["Color"] = Vec(Validation, Vector3d(Layer.r, Layer.g, Layer.b));
		if (!Validation.IsValid())
		{
			return false;
		}

		Data["Weights"] = AddFloats(Validation, Package, Layer.weights);
		if (!Validation.IsValid())
		{
			return false;
		}

		Land["Layers"].Push(std::move(Data));
	}
	for (const LandscapeSpline& Spline : L.GetSplines())
	{
		J Data = WriteSpline(Validation, Spline);
		if (!Validation.IsValid())
		{
			return false;
		}

		(void)ReadSpline(Validation, Data);
		if (!Validation.IsValid())
		{
			return false;
		}

		Land["Splines"].Push(std::move(Data));
	}
	Out = std::move(Land);
	Result = std::move(Out);
	Error.Empty();
	Rollback.Release();
	return true;
}
bool FWorldGeometryCodec::EncodeFoliage(const Foliage& F, FAssetPackage& Package, FJsonValue& Result, FString& Error)
{
	const unsigned long long BulkSize = Package.Bulk.size(), DepSize = Package.Dependencies.size();
	FValidationContext Validation(Error);
	FScopeExit Rollback([&]
	{
		Package.Bulk.resize(BulkSize);
		Package.Dependencies.resize(DepSize);
	});

	if (!Validation.Check(
	        F.GetTypes().size() <= 1024 && F.GetInstances().size() <= 1000000, GENESIS_TEXT("Too much foliage data.")))
	{
		return false;
	}

	for (const FoliageInstance& I : F.GetInstances())
	{
		if (!Validation.Check(I.type >= 0 && I.type < F.GetNumTypes() && I.scale > 0 && I.up.GetMagSqr() > .000001f,
		        GENESIS_TEXT("Invalid foliage transform.")))
		{
			return false;
		}
	}
	J Out;
	FJsonValue Data = J::MakeObject();
	Data["Types"] = J::MakeArray();
	for (const FoliageType& Type : F.GetTypes())
	{
		FJsonValue T = J::MakeObject();
		T["Name"] = Ansi(Validation, Type.name);
		if (!Validation.IsValid())
		{
			return false;
		}

		T["Mesh"] = Ansi(Validation, Type.mesh);
		if (!Validation.IsValid())
		{
			return false;
		}

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
		{
			(void)Number(Validation, T.Get(Key));
			if (!Validation.IsValid())
			{
				return {};
			}
		}
		if (!Validation.IsValid())
		{
			return false;
		}

		Data["Types"].Push(std::move(T));
	}
	std::vector<float> Values;
	for (const FoliageInstance& I : F.GetInstances())
		for (float V : {static_cast<float>(I.type), I.position.GetX(), I.position.GetY(), I.position.GetZ(),
		         I.up.GetX(), I.up.GetY(), I.up.GetZ(), I.yaw, I.scale})
			Values.push_back(V);
	Data["Count"] = F.GetNumInstances();
	Data["Instances"] = AddFloats(Validation, Package, Values);
	if (!Validation.IsValid())
	{
		return false;
	}

	Out = std::move(Data);
	Result = std::move(Out);
	Error.Empty();
	Rollback.Release();
	return true;
}
bool FWorldGeometryCodec::DecodeLandscape(const FAssetPackage& Package, const FJsonValue& L,
    std::shared_ptr<const Landscape>& Result, FString& Error, const FAssetRegistry* Registry)
{
	FValidationContext Validation(Error);

	std::shared_ptr<const Landscape> Out;
	const int QX = Integer(Validation, L.Get("QuadsX"), 1, Landscape::MaxQuads),
	          QY = Integer(Validation, L.Get("QuadsY"), 1, Landscape::MaxQuads);
	if (!Validation.IsValid())
	{
		return false;
	}

	const int Count = (QX + 1) * (QY + 1);
	const float Size = Number(Validation, L.Get("QuadSize"));
	if (!Validation.IsValid())
	{
		return false;
	}

	if (!Validation.Check(Size >= 1 && Size <= 1.0e6f, GENESIS_TEXT("Invalid landscape quad size.")))
	{
		return false;
	}

	std::vector<LandscapeLayer> Layers;
	for (const FJsonValue& Data : Array(Validation, L.Get("Layers"), 256))
	{
		LandscapeLayer Layer;
		Layer.name = Text(Validation, Data.Get("Name"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Layer.texture = Text(Validation, Data.Get("Texture"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Layer.tiling = Number(Validation, Data.Get("Tiling"));
		if (!Validation.IsValid())
		{
			return false;
		}

		if (Data.Get("Texture").AsString().View().substr(0, 6) == "asset:")
		{
			Layer.texture = Data.Get("Texture").AsString().ToStdString();
			FTextureAsset Texture;
			FString TextureError;
			if (!LoadTexture(FString(Layer.texture), Texture, TextureError, Registry))
			{
				Validation.Fail(TextureError);
				return false;
			}
		}
		if (!Validation.Check(Layer.tiling > 0, GENESIS_TEXT("Invalid layer tiling.")))
		{
			return false;
		}

		const FJsonValue& Color = Data.Get("Color");
		if (!Validation.Check(Array(Validation, Color, 3).size() == 3, GENESIS_TEXT("Invalid layer color.")))
		{
			return false;
		}

		Layer.r = static_cast<unsigned char>(Integer(Validation, Color[0], 0, 255));
		if (!Validation.IsValid())
		{
			return false;
		}

		Layer.g = static_cast<unsigned char>(Integer(Validation, Color[1], 0, 255));
		if (!Validation.IsValid())
		{
			return false;
		}

		Layer.b = static_cast<unsigned char>(Integer(Validation, Color[2], 0, 255));
		if (!Validation.IsValid())
		{
			return false;
		}

		Layer.weights = Floats(Validation, Package, Data.Get("Weights"), Count);
		if (!Validation.IsValid())
		{
			return false;
		}

		for (float W : Layer.weights)
		{
			if (!Validation.Check(W >= 0 && W <= 1, GENESIS_TEXT("Invalid paint weight.")))
			{
				return false;
			}
		}
		Layers.push_back(std::move(Layer));
	}
	if (!Validation.IsValid())
	{
		return false;
	}

	std::vector<LandscapeSpline> Splines;
	for (const FJsonValue& S : Array(Validation, L.Get("Splines"), 10000))
	{
		Splines.push_back(ReadSpline(Validation, S));
		if (!Validation.IsValid())
		{
			return {};
		}
	}
	if (!Validation.IsValid())
	{
		return false;
	}

	std::vector<float> Heights = Floats(Validation, Package, L.Get("Heights"), Count);
	if (!Validation.IsValid())
	{
		return false;
	}
	std::shared_ptr<const Landscape> Land =
	    Landscape::Create(QX, QY, Size, std::move(Heights), std::move(Layers), std::move(Splines));
	if (!Validation.IsValid())
	{
		return false;
	}

	if (!Validation.Check(Land != nullptr, GENESIS_TEXT("Cannot construct landscape.")))
	{
		return false;
	}

	Out = Land;
	Result = std::move(Out);
	Error.Empty();
	return true;
}
bool FWorldGeometryCodec::DecodeFoliage(const FAssetPackage& Package, const FJsonValue& F,
    std::shared_ptr<const Foliage>& Result, FString& Error, const FAssetRegistry*)
{
	FValidationContext Validation(Error);

	std::shared_ptr<const Foliage> Out;
	std::vector<FoliageType> Types;
	for (const FJsonValue& T : Array(Validation, F.Get("Types"), 1024))
	{
		FoliageType Type;
		Type.name = Text(Validation, T.Get("Name"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.mesh = Text(Validation, T.Get("Mesh"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.density = Number(Validation, T.Get("Density"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.radius = Number(Validation, T.Get("Radius"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.scaleMin = Number(Validation, T.Get("ScaleMin"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.scaleMax = Number(Validation, T.Get("ScaleMax"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.alignToNormal = Boolean(Validation, T.Get("Align"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.alignMaxAngle = Number(Validation, T.Get("AlignAngle"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.randomYaw = Boolean(Validation, T.Get("RandomYaw"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.slopeMin = Number(Validation, T.Get("SlopeMin"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.slopeMax = Number(Validation, T.Get("SlopeMax"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.heightMin = Number(Validation, T.Get("HeightMin"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.heightMax = Number(Validation, T.Get("HeightMax"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.zOffset = Number(Validation, T.Get("ZOffset"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Type.cullDistance = Number(Validation, T.Get("CullDistance"));
		if (!Validation.IsValid())
		{
			return false;
		}

		Types.push_back(std::move(Type));
	}
	if (!Validation.IsValid())
	{
		return false;
	}

	const int Count = Integer(Validation, F.Get("Count"), 0, 1000000);
	if (!Validation.IsValid())
	{
		return false;
	}

	const std::vector<float> Values = Floats(Validation, Package, F.Get("Instances"), Count * 9);
	if (!Validation.IsValid())
	{
		return false;
	}

	std::vector<FoliageInstance> Instances;
	for (int I = 0; I < Count; ++I)
	{
		const size_t O = static_cast<size_t>(I) * 9;
		if (!Validation.Check(Values[O] >= 0 && Values[O] < Types.size() && Values[O] == std::floor(Values[O]),
		        GENESIS_TEXT("Invalid foliage type index.")))
		{
			return false;
		}

		FoliageInstance Instance;
		Instance.type = static_cast<int>(Values[O]);
		Instance.position = Vector3d(Values[O + 1], Values[O + 2], Values[O + 3]);
		Instance.up = Vector3d(Values[O + 4], Values[O + 5], Values[O + 6]);
		Instance.yaw = Values[O + 7];
		Instance.scale = Values[O + 8];
		if (!Validation.Check(
		        Instance.scale > 0 && Instance.up.GetMagSqr() > 0.000001f, GENESIS_TEXT("Invalid foliage transform.")))
		{
			return false;
		}

		Instances.push_back(Instance);
	}
	Out = std::make_shared<Foliage>(std::move(Types), std::move(Instances));
	Result = std::move(Out);
	Error.Empty();
	return true;
}
bool FWorldGeometryCodec::ParseTextureToken(const FString& Token, FAssetReference& Out, FString& Error)
{
	if (Token.View().substr(0, 6) != "asset:")
	{
		Error = GENESIS_TEXT("Expected a native texture reference.");
		return false;
	}
	const TOptional<FJsonValue> Json = FJsonValue::Parse(Token.View().substr(6), &Error);
	if (!Json)
		return false;
	const TOptional<FAssetReference> Ref = FAssetReference::FromJson(*Json, Error);
	if (!Ref || Ref->IsNull())
	{
		Error = GENESIS_TEXT("Invalid native texture reference.");
		return false;
	}
	Out = *Ref;
	Error.Empty();
	return true;
}

bool FWorldGeometryCodec::GetAssetReferences(
    const FString& Type, const FJsonValue& Payload, TArray<FAssetReference>& Out, FString& Error)
{
	FValidationContext Validation(Error);

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
			for (const FAssetReference& Ref : Mesh.AdditionalOverrides)
				if (!Ref.IsNull())
					Refs.push_back(Ref);
		}
	}
	if (Type == "Genesis.Landscape")
		for (const FJsonValue& Layer : Payload.Get("Layers").GetItems())
		{
			const FString Token = Layer.Get("Texture").AsString();
			if (Token.View().substr(0, 6) == "asset:")
			{
				FAssetReference Ref;
				FString RefError;
				if (!Validation.Check(FWorldGeometryCodec::ParseTextureToken(Token, Ref, RefError),
				        GENESIS_TEXT("Invalid landscape texture reference.")))
				{
					return false;
				}

				Refs.push_back(Ref);
			}
		}
	Out = std::move(Refs);
	Error.Empty();
	return true;
}

bool FWorldGeometryCodec::DecodeMesh(const FJsonValue& Data, FStaticMeshComponentData& Result, FString& Error)
{
	FValidationContext Validation(Error);

	FStaticMeshComponentData Out;
	if (Data.GetType() == EJsonType::String)
	{
		Out.BuiltIn = Data.AsString();
		if (!Validation.Check(Out.BuiltIn.Len() <= 65536 && Out.BuiltIn.View().find('\0') == std::string_view::npos,
		        GENESIS_TEXT("Invalid built-in mesh name.")))
		{
			return false;
		}

		if (!Validation.Check(StaticMesh::Find(Out.BuiltIn.ToAnsi().c_str()) != nullptr,
		        GENESIS_TEXT("Unknown built-in static mesh.")))
		{
			return false;
		}
	}
	else
	{
		if (!Validation.Check(Data.IsObject(), GENESIS_TEXT("Invalid static mesh component payload.")))
		{
			return false;
		}

		const TOptional<FAssetReference> Source = FAssetReference::FromJson(Data.Get("Asset"), Error),
		                                 Override = FAssetReference::FromJson(Data.Get("MaterialOverride"), Error);
		if (!Validation.Check(Source && !Source->IsNull() && Override, GENESIS_TEXT("Invalid mesh asset reference.")))
		{
			return false;
		}

		Out.Asset = *Source;
		Out.MaterialOverride = *Override;
		const FJsonValue& Values = Data.Get("AdditionalOverrides");
		if (!Validation.Check(Values.IsNull() || (Values.IsArray() && Values.Num() <= 63),
		        GENESIS_TEXT("Invalid material overrides.")))
		{
			return false;
		}

		for (const FJsonValue& V : Values.GetItems())
		{
			const TOptional<FAssetReference> Ref = FAssetReference::FromJson(V, Error);
			if (!Validation.Check(Ref.has_value(), GENESIS_TEXT("Invalid slot override.")))
			{
				return false;
			}

			Out.AdditionalOverrides.push_back(*Ref);
		}
	}
	Result = std::move(Out);
	Error.Empty();
	return true;
}
bool FWorldGeometryCodec::EncodeMesh(const FStaticMeshComponentData& Data, FJsonValue& Result, FString& Error)
{
	FValidationContext Validation(Error);

	FJsonValue Out;
	if (!Data.IsNative())
	{
		if (!Validation.Check(Data.MaterialOverride.IsNull() && Data.AdditionalOverrides.empty(),
		        GENESIS_TEXT("Built-in mesh components cannot carry native material overrides.")))
		{
			return false;
		}

		Out = Data.BuiltIn;
	}
	else
	{
		if (!Validation.Check(Data.BuiltIn.IsEmpty(),
		        GENESIS_TEXT("Mesh component cannot have both a built-in and an asset source.")))
		{
			return false;
		}

		if (!Validation.Check(Data.AdditionalOverrides.size() <= 63, GENESIS_TEXT("Too many material overrides.")))
		{
			return false;
		}

		Out = FJsonValue::MakeObject();
		Out["Asset"] = Data.Asset.ToJson();
		Out["MaterialOverride"] = Data.MaterialOverride.ToJson();
		Out["AdditionalOverrides"] = FJsonValue::MakeArray();
		for (const FAssetReference& Ref : Data.AdditionalOverrides)
			Out["AdditionalOverrides"].Push(Ref.ToJson());
	}
	FStaticMeshComponentData Validated;
	if (!DecodeMesh(Out, Validated, Error))
		return false;
	Result = std::move(Out);
	Error.Empty();
	return true;
}
