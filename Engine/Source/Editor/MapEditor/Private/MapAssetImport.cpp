// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapAssetImport.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "Misc/ValidationContext.h"
#include "Misc/ScopeExit.h"
#include "MapLandscapeTextures.h"
#include "Geometry/StaticMesh.h"
#undef Min
#undef Max
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

bool FMapAssetImport::ReadObj(const FString& File, FStaticMeshAsset& Out, FString& Error)
{
	int Line = 0;
	FValidationContext Validation(Error);
	FScopeExit Diagnostics([&]
	{
		if (!Validation.IsValid())
		{
			Error = FTextLocalization::Format("OBJ line {}: {}", Line, Error);
		}
	});

	std::error_code EC;
	const std::filesystem::path Path(File.ToWide());
	if (std::filesystem::file_size(Path, EC) > 64 * 1024 * 1024 || EC)
	{
		Validation.Fail(GENESIS_TEXT("OBJ is unreadable or exceeds 64 MiB."));
		return false;
	}
	std::ifstream Stream(Path);
	if (!Stream)
	{
		Validation.Fail(GENESIS_TEXT("Cannot read OBJ."));
		return false;
	}
	TSharedPtr<StaticMesh> Mesh =
	    std::make_shared<StaticMesh>(FString::FromWide(Path.stem().wstring()).ToAnsi().c_str());
	std::vector<Vector3d> Normals;
	std::vector<std::pair<float, float>> UVs;
	std::set<std::pair<int, int>> Edges;
	std::string Text;
	std::vector<std::string> MaterialNames;
	unsigned int MaterialSlot = 0;
	const TFunction<float(std::istringstream&)> ReadNumber = [&Validation](std::istringstream& S) -> float
	{
		float V;
		if (!(S >> V) || !std::isfinite(V) || std::abs(V) > 1e9f)
		{
			Validation.Fail(GENESIS_TEXT("Invalid OBJ number."));
			return {};
		}
		return V;
	};
	const TFunction<int(const std::string&, size_t)> Index = [&Validation](
	                                                             const std::string& Token, size_t Count) -> int
	{
		int I = 0;
		const std::from_chars_result R = std::from_chars(Token.data(), Token.data() + Token.size(), I);
		if (R.ec != std::errc() || R.ptr != Token.data() + Token.size() || I == 0)
		{
			Validation.Fail(GENESIS_TEXT("Invalid OBJ index."));
			return {};
		}
		const int64 N = I > 0 ? static_cast<int64>(I) - 1 : static_cast<int64>(Count) + I;
		if (N < 0 || N >= static_cast<int64>(Count))
		{
			Validation.Fail(GENESIS_TEXT("OBJ index outside its vertex/normal/UV list."));
			return {};
		}
		return static_cast<int>(N);
	};
	while (std::getline(Stream, Text))
	{
		++Line;
		if (Text.size() > 65536)
		{
			Validation.Fail(GENESIS_TEXT("OBJ line exceeds limit."));
			return false;
		}
		std::istringstream S(Text);
		std::string Kind;
		S >> Kind;
		if (Kind.empty() || Kind[0] == '#')
			continue;
		if (Kind == "v" || Kind == "vn")
		{
			const float X = ReadNumber(S), Y = ReadNumber(S), Z = ReadNumber(S);
			if (!Validation.IsValid())
			{
				return false;
			}
			Vector3d V(X, Y, Z);
			if (Kind == "v")
			{
				if (Mesh->GetNumVertices() >= 1000000)
				{
					Validation.Fail(GENESIS_TEXT("Too many OBJ vertices."));
					return false;
				}
				Mesh->AddVertex(V);
			}
			else
			{
				const float Length = std::sqrt(X * X + Y * Y + Z * Z);
				if (!std::isfinite(Length) || Length < 1e-6f || Normals.size() >= 1000000)
				{
					Validation.Fail(GENESIS_TEXT("Invalid OBJ normal."));
					return false;
				}
				V.MultVector(1 / Length);
				Normals.push_back(V);
			}
		}
		else if (Kind == "vt")
		{
			const float U = ReadNumber(S), V = ReadNumber(S);
			if (!Validation.IsValid())
			{
				return false;
			}
			if (UVs.size() >= 1000000)
			{
				Validation.Fail(GENESIS_TEXT("Too many OBJ UVs."));
				return false;
			}
			UVs.emplace_back(U, V);
		}
		else if (Kind == "usemtl")
		{
			std::string Name;
			if (!(S >> Name) || Name[0] == '#')
			{
				Validation.Fail(GENESIS_TEXT("Missing OBJ material name."));
				return false;
			}
			std::vector<std::string>::iterator It = std::find(MaterialNames.begin(), MaterialNames.end(), Name);
			if (It == MaterialNames.end())
			{
				if (MaterialNames.size() >= 64)
				{
					Validation.Fail(GENESIS_TEXT("OBJ exceeds 64 material slots."));
					return false;
				}
				if (Name.size() > 256)
				{
					Validation.Fail(GENESIS_TEXT("OBJ material name exceeds 256 bytes."));
					return false;
				}
				MaterialNames.push_back(Name);
				It = MaterialNames.end() - 1;
			}
			MaterialSlot = static_cast<unsigned int>(It - MaterialNames.begin());
		}
		else if (Kind == "f")
		{
			if (MaterialNames.empty())
			{
				MaterialNames.push_back("");
			}
			struct Corner
			{
				int V, UV = -1, N = -1;
			};
			std::vector<Corner> Face;
			std::string Token;
			while (S >> Token)
			{
				if (Token[0] == '#')
					break;
				if (Face.size() >= 1024)
				{
					Validation.Fail(GENESIS_TEXT("OBJ face too large."));
					return false;
				}
				const unsigned long long A = Token.find('/'), B = A == std::string::npos ? A : Token.find('/', A + 1);
				Corner C{Index(Token.substr(0, A), Mesh->GetNumVertices())};
				if (!Validation.IsValid())
				{
					return false;
				}
				if (A != std::string::npos && B != A + 1)
					C.UV = Index(Token.substr(A + 1, B == std::string::npos ? B : B - A - 1), UVs.size());
				if (!Validation.IsValid())
				{
					return false;
				}
				if (B != std::string::npos)
					C.N = Index(Token.substr(B + 1), Normals.size());
				if (!Validation.IsValid())
				{
					return false;
				}
				Face.push_back(C);
			}
			if (Face.size() < 3 || Mesh->GetTriangles().size() + Face.size() - 2 > 2000000)
			{
				Validation.Fail(GENESIS_TEXT("Invalid OBJ face or triangle limit exceeded."));
				return false;
			}
			// Reject nonconvex/nonplanar polygons; fan triangulation must not change the shape.
			const Vector3d& P = Mesh->GetVertex(Face[0].V);
			Vector3d First;
			for (size_t I = 1; I + 1 < Face.size(); ++I)
			{
				Vector3d AB = Mesh->GetVertex(Face[I].V), AC = Mesh->GetVertex(Face[I + 1].V);
				AB.SubVector(P);
				AC.SubVector(P);
				Vector3d N;
				N.CrossVector(AB, AC);
				const float Length = N.GetMag();
				if (!std::isfinite(Length) || Length < 1e-6f)
				{
					Validation.Fail(GENESIS_TEXT("Degenerate OBJ face."));
					return false;
				}
				N.MultVector(1 / Length);
				if (I == 1)
					First = N;
				else if (N.DotVector(First) < 0.999f)
				{
					Validation.Fail(GENESIS_TEXT("Triangulate nonconvex or nonplanar OBJ polygons before import."));
					return false;
				}
				const Corner C[3] = {Face[0], Face[I], Face[I + 1]};
				Vector3d Ns[3];
				float Coords[3][2]{};
				for (int J = 0; J < 3; ++J)
				{
					Ns[J] = C[J].N < 0 ? N : Normals[C[J].N];
					if (C[J].UV >= 0)
					{
						Coords[J][0] = UVs[C[J].UV].first;
						Coords[J][1] = UVs[C[J].UV].second;
					}
				}
				Mesh->AddTriangle(C[0].V, C[1].V, C[2].V, Ns[0], Ns[1], Ns[2]);
				Mesh->SetTriangleUVs(static_cast<int>(Mesh->GetTriangles().size()) - 1, Coords);
				Mesh->SetTriangleMaterial(static_cast<int>(Mesh->GetTriangles().size()) - 1, MaterialSlot);
			}
			for (size_t I = 0; I < Face.size(); ++I)
			{
				Vector3d AEdge, BEdge, Turn;
				AEdge.SubVector(Mesh->GetVertex(Face[(I + 1) % Face.size()].V), Mesh->GetVertex(Face[I].V));
				BEdge.SubVector(
				    Mesh->GetVertex(Face[(I + 2) % Face.size()].V), Mesh->GetVertex(Face[(I + 1) % Face.size()].V));
				Turn.CrossVector(AEdge, BEdge);
				if (Turn.DotVector(First) <= 0)
				{
					Validation.Fail(GENESIS_TEXT("Triangulate nonconvex OBJ polygons before import."));
					return false;
				}
				const int A = Face[I].V, B = Face[(I + 1) % Face.size()].V;
				Edges.emplace(std::min(A, B), std::max(A, B));
				if (Edges.size() > 3000000)
				{
					Validation.Fail(GENESIS_TEXT("Too many OBJ wire edges."));
					return false;
				}
			}
		}
		else if (Kind != "o" && Kind != "g" && Kind != "s" && Kind != "mtllib" && Kind != "usemtl")
		{
			Validation.Fail(GENESIS_TEXT("Unsupported OBJ record."));
			return false;
		}
	}
	if (Stream.bad() || Mesh->GetTriangles().empty())
	{
		Validation.Fail(GENESIS_TEXT("OBJ contains no readable triangles."));
		return false;
	}
	for (const std::pair<int, int>& E : Edges)
		Mesh->AddEdge(E.first, E.second);
	Mesh->Finish();
	FStaticMeshAsset Asset;
	Asset.Mesh = std::move(Mesh);
	Asset.AdditionalMaterials.resize(MaterialNames.size() - 1);
	for (const std::basic_string<char>& Name : MaterialNames)
		Asset.MaterialSlotNames.push_back(FString(Name));
	Out = std::move(Asset);
	Error.Empty();
	return true;
}
bool FMapAssetImport::ImportTexture(const FString& File, const FString& Target, FString& Error)
{
	FLandscapeImage Image;
	if (!FLandscapeTextureLibrary::LoadImageFile(File, Image, Error))
		return false;
	FTextureAsset T;
	T.Width = Image.Width;
	T.Height = Image.Height;
	T.Pixels = std::move(Image.Pixels);
	FAssetPackage P;
	if (!FRenderAssetCodec::EncodeTexture(T, P, Error))
		return false;
	SetImportSource(P, Target, File);
	return FAssetPackageStore::Save(Target, P, Error);
}
bool FMapAssetImport::ImportMesh(const FString& File, const FString& Target, FString& Error)
{
	FStaticMeshAsset M;
	FAssetPackage P;
	if (!ReadObj(File, M, Error) || !FRenderAssetCodec::EncodeMesh(M, P, Error))
		return false;
	SetMeshImportSource(P, Target, File);
	return FAssetPackageStore::Save(Target, P, Error);
}

FString FMapAssetImport::GetImportSource(const FAssetPackage& P, const FString& Target)
{
	const FString File = P.Exports.Get("ImportSource").Get("File").AsString();
	if (File.IsEmpty())
		return {};
	std::error_code PathError;
	const std::filesystem::path Base =
	    std::filesystem::absolute(std::filesystem::path(Target.ToWide()), PathError).parent_path();
	if (PathError)
	{
		return {};
	}
	return FString::FromWide((Base / std::filesystem::path(File.ToWide())).lexically_normal().wstring());
}

void FMapAssetImport::SetImportSource(FAssetPackage& P, const FString& Target, const FString& File)
{
	const std::filesystem::path Base = std::filesystem::absolute(std::filesystem::path(Target.ToWide())).parent_path();
	const std::filesystem::path Full =
	    std::filesystem::absolute(std::filesystem::path(File.ToWide())).lexically_normal();
	const std::filesystem::path Relative = Full.lexically_relative(Base);
	P.Exports["ImportSource"]["File"] = FString::FromWide((Relative.empty() ? Full : Relative).generic_wstring());
}

FString FMapAssetImport::GetMeshImportSource(const FAssetPackage& P, const FString& Target)
{
	return GetImportSource(P, Target);
}
void FMapAssetImport::SetMeshImportSource(FAssetPackage& P, const FString& Target, const FString& File)
{
	SetImportSource(P, Target, File);
}
