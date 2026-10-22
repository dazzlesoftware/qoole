// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapAssetImport.h"
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
#include <stdexcept>

bool FMapAssetImport::ReadObj(const FString& File, FStaticMeshAsset& Out, FString& Error)
{
	int Line = 0;
	try
	{
		std::error_code EC;
		const std::filesystem::path Path(File.ToWide());
		if (std::filesystem::file_size(Path, EC) > 64 * 1024 * 1024 || EC)
			throw std::runtime_error("OBJ is unreadable or exceeds 64 MiB.");
		std::ifstream Stream(Path);
		if (!Stream)
			throw std::runtime_error("Cannot read OBJ.");
		auto Mesh = std::make_shared<StaticMesh>(FString::FromWide(Path.stem().wstring()).ToAnsi().c_str());
		std::vector<Vector3d> Normals;
		std::vector<std::pair<float, float>> UVs;
		std::set<std::pair<int, int>> Edges;
		std::string Text;
		std::vector<std::string> MaterialNames;
		unsigned int MaterialSlot = 0;
		const auto ReadNumber = [](std::istringstream& S) {
			float V;
			if (!(S >> V) || !std::isfinite(V) || std::abs(V) > 1e9f)
				throw std::runtime_error("Invalid OBJ number.");
			return V;
		};
		const auto Index = [](const std::string& Token, size_t Count) {
			int I = 0;
			const auto R = std::from_chars(Token.data(), Token.data() + Token.size(), I);
			if (R.ec != std::errc() || R.ptr != Token.data() + Token.size() || I == 0)
				throw std::runtime_error("Invalid OBJ index.");
			const auto N = I > 0 ? static_cast<int64>(I) - 1 : static_cast<int64>(Count) + I;
			if (N < 0 || N >= static_cast<int64>(Count))
				throw std::runtime_error("OBJ index outside its vertex/normal/UV list.");
			return static_cast<int>(N);
		};
		while (std::getline(Stream, Text))
		{
			++Line;
			if (Text.size() > 65536)
				throw std::runtime_error("OBJ line exceeds limit.");
			std::istringstream S(Text);
			std::string Kind;
			S >> Kind;
			if (Kind.empty() || Kind[0] == '#')
				continue;
			if (Kind == "v" || Kind == "vn")
			{
				const float X = ReadNumber(S), Y = ReadNumber(S), Z = ReadNumber(S);
				Vector3d V(X, Y, Z);
				if (Kind == "v")
				{
					if (Mesh->GetNumVertices() >= 1000000)
						throw std::runtime_error("Too many OBJ vertices.");
					Mesh->AddVertex(V);
				}
				else
				{
					const float Length = std::sqrt(X * X + Y * Y + Z * Z);
					if (!std::isfinite(Length) || Length < 1e-6f || Normals.size() >= 1000000)
						throw std::runtime_error("Invalid OBJ normal.");
					V.MultVector(1 / Length);
					Normals.push_back(V);
				}
			}
			else if (Kind == "vt")
			{
				const float U = ReadNumber(S), V = ReadNumber(S);
				if (UVs.size() >= 1000000)
					throw std::runtime_error("Too many OBJ UVs.");
				UVs.emplace_back(U, V);
			}
			else if (Kind == "usemtl")
			{
				std::string Name;
				if (!(S >> Name) || Name[0] == '#')
					throw std::runtime_error("Missing OBJ material name.");
				auto It = std::find(MaterialNames.begin(), MaterialNames.end(), Name);
				if (It == MaterialNames.end())
				{
					if (MaterialNames.size() >= 64)
						throw std::runtime_error("OBJ exceeds 64 material slots.");
					if (Name.size() > 256)
						throw std::runtime_error("OBJ material name exceeds 256 bytes.");
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
						throw std::runtime_error("OBJ face too large.");
					const auto A = Token.find('/'), B = A == std::string::npos ? A : Token.find('/', A + 1);
					Corner C{Index(Token.substr(0, A), Mesh->GetNumVertices())};
					if (A != std::string::npos && B != A + 1)
						C.UV = Index(Token.substr(A + 1, B == std::string::npos ? B : B - A - 1), UVs.size());
					if (B != std::string::npos)
						C.N = Index(Token.substr(B + 1), Normals.size());
					Face.push_back(C);
				}
				if (Face.size() < 3 || Mesh->GetTriangles().size() + Face.size() - 2 > 2000000)
					throw std::runtime_error("Invalid OBJ face or triangle limit exceeded.");
				// Reject nonconvex/nonplanar polygons; fan triangulation must not change the shape.
				const auto& P = Mesh->GetVertex(Face[0].V);
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
						throw std::runtime_error("Degenerate OBJ face.");
					N.MultVector(1 / Length);
					if (I == 1)
						First = N;
					else if (N.DotVector(First) < 0.999f)
						throw std::runtime_error("Triangulate nonconvex or nonplanar OBJ polygons before import.");
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
					BEdge.SubVector(Mesh->GetVertex(Face[(I + 2) % Face.size()].V),
									Mesh->GetVertex(Face[(I + 1) % Face.size()].V));
					Turn.CrossVector(AEdge, BEdge);
					if (Turn.DotVector(First) <= 0)
						throw std::runtime_error("Triangulate nonconvex OBJ polygons before import.");
					const auto A = Face[I].V, B = Face[(I + 1) % Face.size()].V;
					Edges.emplace(std::min(A, B), std::max(A, B));
					if (Edges.size() > 3000000)
						throw std::runtime_error("Too many OBJ wire edges.");
				}
			}
			else if (Kind != "o" && Kind != "g" && Kind != "s" && Kind != "mtllib" && Kind != "usemtl")
				throw std::runtime_error("Unsupported OBJ record.");
		}
		if (Stream.bad() || Mesh->GetTriangles().empty())
			throw std::runtime_error("OBJ contains no readable triangles.");
		for (const auto& E : Edges)
			Mesh->AddEdge(E.first, E.second);
		Mesh->Finish();
		FStaticMeshAsset Asset;
		Asset.Mesh = std::move(Mesh);
		Asset.AdditionalMaterials.resize(MaterialNames.size() - 1);
		for (const auto& Name : MaterialNames)
			Asset.MaterialSlotNames.push_back(FString(Name));
		Out = std::move(Asset);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = FString::Format("OBJ line {}: {}", Line, E.what());
		return false;
	}
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
	if(!FRenderAssetCodec::EncodeTexture(T,P,Error)) return false;
	SetImportSource(P,Target,File);
	return FAssetPackageStore::Save(Target,P,Error);
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
	const auto File = P.Exports.Get("ImportSource").Get("File").AsString();
	if (File.IsEmpty())
		return {};
	try
	{
		const auto Base = std::filesystem::absolute(std::filesystem::path(Target.ToWide())).parent_path();
		return FString::FromWide((Base / std::filesystem::path(File.ToWide())).lexically_normal().wstring());
	}
	catch (...)
	{
		return {};
	}
}
void FMapAssetImport::SetImportSource(FAssetPackage& P, const FString& Target, const FString& File)
{
	const auto Base = std::filesystem::absolute(std::filesystem::path(Target.ToWide())).parent_path();
	const auto Full = std::filesystem::absolute(std::filesystem::path(File.ToWide())).lexically_normal();
	const auto Relative = Full.lexically_relative(Base);
	P.Exports["ImportSource"]["File"] = FString::FromWide((Relative.empty() ? Full : Relative).generic_wstring());
}

FString FMapAssetImport::GetMeshImportSource(const FAssetPackage& P,const FString& Target) { return GetImportSource(P,Target); }
void FMapAssetImport::SetMeshImportSource(FAssetPackage& P,const FString& Target,const FString& File) { SetImportSource(P,Target,File); }
