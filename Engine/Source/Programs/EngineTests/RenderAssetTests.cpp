// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "Templates/SharedPointer.h"
#include "Assets/RenderAssets.h"
#include "Geometry/StaticMesh.h"
#undef Min
#undef Max
#include <limits>

TEST_SUITE("RenderAssets")
{
	TEST_CASE("Native meshes round trip topology normals UVs colors and material references")
	{
		FString Error;
		std::vector<std::string> Names = StaticMesh::GetBasicShapeNames();
		for (const std::basic_string<char>& Name : StaticMesh::GetFoliageMeshNames())
			Names.push_back(Name);
		for (const std::basic_string<char>& Name : StaticMesh::GetSplineMeshNames())
			Names.push_back(Name);
		for (const std::basic_string<char>& Name : Names)
		{
			FStaticMeshAsset Source{StaticMesh::Find(Name.c_str()), {FAssetId::New(), "/Game/Materials/Surface"}};
			FAssetPackage P;
			P.Id = FAssetId::New();
			const FString Id = P.Id;
			REQUIRE_MESSAGE(FRenderAssetCodec::EncodeMesh(Source, P, Error), Name, " ", *Error);
			CHECK(P.Id == Id);
			CHECK(P.Dependencies.size() == 1);
			FStaticMeshAsset Back;
			REQUIRE_MESSAGE(FRenderAssetCodec::DecodeMesh(P, Back, Error), *Error);
			REQUIRE(Back.Mesh->GetNumVertices() == Source.Mesh->GetNumVertices());
			CHECK(Back.Mesh->GetTriangles().size() == Source.Mesh->GetTriangles().size());
			CHECK(Back.Mesh->GetEdgeList().size() == Source.Mesh->GetEdgeList().size());
			CHECK(Back.Material.Id == Source.Material.Id);
			for (int I = 0; I < Back.Mesh->GetNumVertices(); ++I)
				CHECK(Back.Mesh->GetVertex(I) == Source.Mesh->GetVertex(I));
			CHECK(Back.Mesh->GetVertexColors() == Source.Mesh->GetVertexColors());
		}
		TSharedPtr<StaticMesh> Custom = std::make_shared<StaticMesh>("Custom");
		Custom->AddVertex(Vector3d());
		Custom->AddVertex(Vector3d(1, 0, 0));
		Custom->AddVertex(Vector3d(0, 1, 0));
		Custom->AddFlatTriangle(0, 1, 2);
		const float UV[3][2] = {{0, 0}, {2, 0}, {0, 3}};
		Custom->SetTriangleUVs(0, UV);
		Custom->Finish();
		FAssetPackage P;
		REQUIRE(FRenderAssetCodec::EncodeMesh({Custom, {}}, P, Error));
		FStaticMeshAsset Back;
		REQUIRE(FRenderAssetCodec::DecodeMesh(P, Back, Error));
		CHECK(Back.Mesh->GetTriangles()[0].uv[2][1] == 3);
	}
	TEST_CASE("Texture and material codecs preserve typed data and fail without altering outputs")
	{
		FString Error;
		FTextureAsset Texture;
		Texture.Width = 2;
		Texture.Height = 1;
		Texture.bSRGB = false;
		Texture.Pixels = {1, 2, 3, 4, 5, 6, 7, 8};
		FAssetPackage P;
		REQUIRE(FRenderAssetCodec::EncodeTexture(Texture, P, Error));
		FTextureAsset Back;
		REQUIRE(FRenderAssetCodec::DecodeTexture(P, Back, Error));
		CHECK(Back.Pixels == Texture.Pixels);
		CHECK_FALSE(Back.bSRGB);
		FAssetPackage Broken = P;
		Broken.Exports["Width"] = 8193;
		CHECK_FALSE(FRenderAssetCodec::DecodeTexture(Broken, Back, Error));
		CHECK(Back.Pixels == Texture.Pixels);
		Broken = P;
		Broken.Bulk.pop_back();
		CHECK_FALSE(FRenderAssetCodec::DecodeTexture(Broken, Back, Error));
		const FAssetPackage Before = P;
		Texture.Width = 0;
		CHECK_FALSE(FRenderAssetCodec::EncodeTexture(Texture, P, Error));
		CHECK(P.Exports == Before.Exports);
		CHECK(P.Bulk == Before.Bulk);
		FMaterialAsset M;
		M.BaseColor[0] = 0.2f;
		M.Metallic = 0.7f;
		M.BaseColorTexture = {FAssetId::New(), "/Game/Textures/Stone"};
		M.NormalTexture = {FAssetId::New(), "/Game/Textures/StoneNormal"};
		REQUIRE(FRenderAssetCodec::EncodeMaterial(M, P, Error));
		FMaterialAsset Mat;
		REQUIRE(FRenderAssetCodec::DecodeMaterial(P, Mat, Error));
		CHECK(Mat.BaseColor[0] == doctest::Approx(0.2));
		CHECK(Mat.Metallic == doctest::Approx(0.7));
		CHECK(Mat.BaseColorTexture.Id == M.BaseColorTexture.Id);
		CHECK(P.SchemaVersion == 3);
		CHECK(Mat.NormalTexture.Id == M.NormalTexture.Id);
		FAssetPackage Old = P;
		Old.SchemaVersion = 1;
		FMaterialAsset Legacy;
		REQUIRE(FRenderAssetCodec::DecodeMaterial(Old, Legacy, Error));
		CHECK(Legacy.NormalTexture.IsNull());
		FAssetPackage InvalidNormal = P;
		InvalidNormal.Exports["NormalTexture"]["Id"] = "invalid";
		CHECK_FALSE(FRenderAssetCodec::DecodeMaterial(InvalidNormal, Mat, Error));
		M.Roughness = std::numeric_limits<float>::quiet_NaN();
		CHECK_FALSE(FRenderAssetCodec::EncodeMaterial(M, P, Error));
		Broken = P;
		Broken.Exports["Metallic"] = 2;
		CHECK_FALSE(FRenderAssetCodec::DecodeMaterial(Broken, Mat, Error));
	}
	TEST_CASE("Mesh decoders reject corrupt indices normals bulk sizes and future schemas")
	{
		FString Error;
		FAssetPackage P;
		REQUIRE(FRenderAssetCodec::EncodeMesh({StaticMesh::Find("Cube"), {}}, P, Error));
		FStaticMeshAsset Out;
		FAssetPackage Bad = P;
		Bad.Bulk.pop_back();
		CHECK_FALSE(FRenderAssetCodec::DecodeMesh(Bad, Out, Error));
		Bad = P;
		const size_t IndexOffset = 8 * 12;
		for (int I = 0; I < 4; ++I)
			Bad.Bulk[IndexOffset + I] = 255;
		CHECK_FALSE(FRenderAssetCodec::DecodeMesh(Bad, Out, Error));
		Bad = P;
		for (size_t I = IndexOffset + 12; I < IndexOffset + 24; ++I)
			Bad.Bulk[I] = 0;
		CHECK_FALSE(FRenderAssetCodec::DecodeMesh(Bad, Out, Error));
		Bad = P;
		Bad.SchemaVersion = 3;
		CHECK_FALSE(FRenderAssetCodec::DecodeMesh(Bad, Out, Error));
		CHECK_FALSE(Out.Mesh);
		Bad = P;
		Bad.Exports["Vertices"] = 1000001;
		CHECK_FALSE(FRenderAssetCodec::DecodeMesh(Bad, Out, Error));
	}
}

TEST_CASE("Material blend modes round trip and older packages remain opaque" * doctest::test_suite("RenderAssets"))
{
	FString Error;
	FAssetPackage Package;
	FMaterialAsset Source, Back;
	for (EMaterialBlendMode Mode :
	    {EMaterialBlendMode::Opaque, EMaterialBlendMode::Masked, EMaterialBlendMode::Translucent})
	{
		Source.BlendMode = Mode;
		Source.OpacityMaskClip = 0.61f;
		Source.BaseColor[3] = 0.4f;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Source, Package, Error));
		CHECK(Package.SchemaVersion == 3);
		REQUIRE(FRenderAssetCodec::DecodeMaterial(Package, Back, Error));
		CHECK(Back.BlendMode == Mode);
		CHECK(Back.OpacityMaskClip == doctest::Approx(0.61));
		CHECK(Back.BaseColor[3] == doctest::Approx(0.4));
	}
	for (int Version : {1, 2})
	{
		FAssetPackage Old = Package;
		Old.SchemaVersion = Version;
		REQUIRE(FRenderAssetCodec::DecodeMaterial(Old, Back, Error));
		CHECK(Back.BlendMode == EMaterialBlendMode::Opaque);
	}
	for (FJsonValue Bad : {FJsonValue(-1), FJsonValue(3), FJsonValue(0.5), FJsonValue("Masked"), FJsonValue()})
	{
		FAssetPackage Corrupt = Package;
		Corrupt.Exports["BlendMode"] = Bad;
		Back.Roughness = 0.77f;
		CHECK_FALSE(FRenderAssetCodec::DecodeMaterial(Corrupt, Back, Error));
		CHECK(Back.Roughness == doctest::Approx(0.77));
	}
	FAssetPackage Corrupt = Package;
	Corrupt.Exports["OpacityMaskClip"] = 1.1f;
	CHECK_FALSE(FRenderAssetCodec::DecodeMaterial(Corrupt, Back, Error));
	Source.BlendMode = static_cast<EMaterialBlendMode>(9);
	FString Original = Package.Exports.ToString();
	CHECK_FALSE(FRenderAssetCodec::EncodeMaterial(Source, Package, Error));
	CHECK(Package.Exports.ToString() == Original);
}

TEST_CASE("Mesh material slots validate and version one meshes retain slot zero" * doctest::test_suite("RenderAssets"))
{
	FString Error;
	TSharedPtr<StaticMesh> Mesh = std::make_shared<StaticMesh>("Two slots");
	Mesh->AddVertex(Vector3d());
	Mesh->AddVertex(Vector3d(1, 0, 0));
	Mesh->AddVertex(Vector3d(0, 1, 0));
	Mesh->AddFlatTriangle(0, 1, 2);
	Mesh->AddFlatTriangle(2, 1, 0);
	Mesh->SetTriangleMaterial(1, 1);
	Mesh->Finish();
	FStaticMeshAsset Source{Mesh, {FAssetId::New(), "/Game/Red"}, {{FAssetId::New(), "/Game/Blue"}}};
	FAssetPackage Package;
	REQUIRE(FRenderAssetCodec::EncodeMesh(Source, Package, Error));
	CHECK(Package.SchemaVersion == 2);
	CHECK(Package.Dependencies.size() == 2);
	FStaticMeshAsset Back;
	REQUIRE(FRenderAssetCodec::DecodeMesh(Package, Back, Error));
	CHECK(Back.GetNumMaterials() == 2);
	CHECK(Back.Mesh->GetTriangles()[1].MaterialSlot == 1);
	CHECK(Back.GetMaterial(1).Id == Source.GetMaterial(1).Id);
	FAssetPackage Broken = Package;
	Broken.Bulk[36 + 72] = 2;
	const std::shared_ptr<const StaticMesh> Original = Back.Mesh;
	CHECK_FALSE(FRenderAssetCodec::DecodeMesh(Broken, Back, Error));
	CHECK(Back.Mesh == Original);
	Broken = Package;
	Broken.Exports["Materials"] = FJsonValue::MakeArray();
	CHECK_FALSE(FRenderAssetCodec::DecodeMesh(Broken, Back, Error));
	Source.AdditionalMaterials.resize(64);
	CHECK_FALSE(FRenderAssetCodec::EncodeMesh(Source, Broken, Error));
	Source.AdditionalMaterials.clear();
	CHECK_FALSE(FRenderAssetCodec::EncodeMesh(Source, Broken, Error));
	// Build an actual legacy 72-byte triangle stream from the current geometry.
	FAssetPackage Legacy = Package;
	Legacy.SchemaVersion = 1;
	Legacy.Exports["Material"] = Source.Material.ToJson();
	Legacy.Exports["MaterialSlotNames"] = FJsonValue();
	Legacy.Bulk.clear();
	Legacy.Bulk.insert(Legacy.Bulk.end(), Package.Bulk.begin(), Package.Bulk.begin() + 36);
	for (int I = 0; I < 2; ++I)
		Legacy.Bulk.insert(
		    Legacy.Bulk.end(), Package.Bulk.begin() + 36 + I * 76, Package.Bulk.begin() + 36 + I * 76 + 72);
	REQUIRE(FRenderAssetCodec::DecodeMesh(Legacy, Back, Error));
	CHECK(Back.GetNumMaterials() == 1);
	for (const StaticMesh::Triangle& T : Back.Mesh->GetTriangles())
		CHECK(T.MaterialSlot == 0);
}

TEST_CASE("Nested asset reader failures preserve outputs and recover on the next valid operation" *
    doctest::test_suite("RenderAssets"))
{
	FString Error;
	FAssetPackage Valid;
	REQUIRE(FRenderAssetCodec::EncodeMesh({StaticMesh::Find("Cube"), {}}, Valid, Error));
	FStaticMeshAsset Output;
	REQUIRE(FRenderAssetCodec::DecodeMesh(Valid, Output, Error));
	const std::shared_ptr<const StaticMesh> Original = Output.Mesh;
	for (const char* Field : {"Vertices", "Triangles", "Edges"})
	{
		FAssetPackage Bad = Valid;
		Bad.Exports[Field] = "invalid";
		CHECK_FALSE(FRenderAssetCodec::DecodeMesh(Bad, Output, Error));
		CHECK_FALSE(Error.IsEmpty());
		CHECK(Output.Mesh == Original);
		REQUIRE(FRenderAssetCodec::DecodeMesh(Valid, Output, Error));
		CHECK(Error.IsEmpty());
		Output.Mesh = Original;
	}
	FMaterialAsset Material;
	Material.BaseColorTexture = {"invalid", "/Game/Textures/Test"};
	const FAssetPackage Previous = Valid;
	CHECK_FALSE(FRenderAssetCodec::EncodeMaterial(Material, Valid, Error));
	CHECK_FALSE(Error.IsEmpty());
	CHECK(Valid.Exports == Previous.Exports);
	CHECK(Valid.Bulk == Previous.Bulk);
	CHECK(Valid.Dependencies == Previous.Dependencies);
}
