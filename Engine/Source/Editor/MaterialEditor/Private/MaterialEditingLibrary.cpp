// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MaterialEditingLibrary.h"

#include "HAL/File.h"

#include "HAL/PlatformMisc.h"

#include <Windows.h>

#include <filesystem>

#include <cmath>
#include <mutex>
#include <deque>
#include <set>
#include <map>
#include "Materials/MaterialFunction.h"

FMaterialGraph FMaterialEditingLibrary::CreateGraph(const FMaterialAsset& M)

{

	if (M.Graph)

		return *M.Graph;

	FMaterialGraph G;

	auto Color = G.Add(EMaterialExpression::Constant4Vector, 0, 0);

	for (int I = 0; I < 4; ++I)

		G.Find(Color)->Value[I] = I == 3 ? M.BaseColor[I]

								  : M.BaseColor[I] <= .04045f

									  ? M.BaseColor[I] / 12.92f

									  : std::pow((M.BaseColor[I] + .055f) / 1.055f, 2.4f);

	uint32 Product = Color;

	if (!M.BaseColorTexture.IsNull())

	{

		auto T = G.Add(EMaterialExpression::TextureSample, 0, 190);

		G.Find(T)->Texture = M.BaseColorTexture;

		auto P = G.Add(EMaterialExpression::Multiply, 240, 0);

		G.Find(P)->Inputs[0] = Product;

		G.Find(P)->Inputs[1] = T;

		Product = P;
	}

	auto VC = G.Add(EMaterialExpression::VertexColor, 240, 190);

	auto P = G.Add(EMaterialExpression::Multiply, 480, 0);

	G.Find(P)->Inputs[0] = Product;

	G.Find(P)->Inputs[1] = VC;

	auto RGB = G.Add(EMaterialExpression::ComponentMask, 720, 0);

	G.Find(RGB)->Inputs[0] = P;

	G.Find(RGB)->Mask = 7;

	G.Outputs[0] = RGB;

	auto A = G.Add(EMaterialExpression::ComponentMask, 720, 180);

	G.Find(A)->Inputs[0] = P;

	G.Find(A)->Mask = 8;

	G.Outputs[3] = A;

	auto R = G.Add(EMaterialExpression::Constant, 480, 230);

	G.Find(R)->Value[0] = M.Roughness;

	G.Outputs[1] = R;

	auto Met = G.Add(EMaterialExpression::Constant, 480, 380);

	G.Find(Met)->Value[0] = M.Metallic;

	G.Outputs[2] = Met;

	if (!M.NormalTexture.IsNull())

	{

		auto T = G.Add(EMaterialExpression::TextureSample, 0, 430);

		G.Find(T)->Texture = M.NormalTexture;

		G.Find(T)->bNormalTexture = true;

		auto N = G.Add(EMaterialExpression::NormalDecode, 240, 430);

		G.Find(N)->Inputs[0] = T;

		G.Outputs[4] = N;
	}

	return G;
}

namespace

{

FString FindDXC()

{

	wchar_t Buffer[32768];

	auto L = GetEnvironmentVariableW(L"GENESIS_DXC", Buffer, std::size(Buffer));

	if (L > 0 && L < std::size(Buffer))

		return FString::FromWide(Buffer);

	return FPaths::Combine(FPaths::ExecutableDir(), "../ThirdParty/DXC/bin/x64/dxc.exe");
}

bool CompileTarget(const FString& Compiler, const FString& Source, const FString& Output, const FString& Log,

				   bool Vulkan, FString& Error)

{

	std::wstring Args = L"\"" + Compiler.ToWide() + L"\" -nologo -O3 -T ps_6_0 -E PSMaterial ";

	if (Vulkan)

		Args += L"-spirv -fspv-target-env=vulkan1.1 -DMAP_SPIRV ";

	Args += L"-Fo \"" + Output.ToWide() + L"\" \"" + Source.ToWide() + L"\"";

	SECURITY_ATTRIBUTES SA{sizeof(SA), nullptr, TRUE};

	HANDLE H = CreateFileW(Log.ToWide().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &SA, CREATE_ALWAYS,

						   FILE_ATTRIBUTE_NORMAL, nullptr);

	if (H == INVALID_HANDLE_VALUE)

	{

		Error = "Cannot create shader diagnostic file.";

		return false;
	}

	STARTUPINFOW SI{};

	SI.cb = sizeof(SI);

	SI.dwFlags = STARTF_USESTDHANDLES;

	SI.hStdOutput = SI.hStdError = H;

	SI.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

	PROCESS_INFORMATION PI{};

	const bool Started = CreateProcessW(Compiler.ToWide().c_str(), Args.data(), nullptr, nullptr, TRUE,

										CREATE_NO_WINDOW, nullptr, nullptr, &SI, &PI) != FALSE;

	if (!Started)

	{

		CloseHandle(H);

		Error = "Material compiler unavailable. Run Engine/Build/SetupMaterialCompiler.ps1 or set "

				"GENESIS_DXC to a SPIR-V capable dxc.exe.";

		return false;
	}

	const auto Wait = WaitForSingleObject(PI.hProcess, 30000);

	if (Wait != WAIT_OBJECT_0)

	{

		TerminateProcess(PI.hProcess, 1);

		WaitForSingleObject(PI.hProcess, 5000);
	}

	DWORD Code = 1;

	GetExitCodeProcess(PI.hProcess, &Code);

	CloseHandle(PI.hThread);

	CloseHandle(PI.hProcess);

	CloseHandle(H);

	if (Code != 0 || Wait != WAIT_OBJECT_0)

	{

		FFile::ReadAllText(Log, Error);

		if (Error.IsEmpty())

			Error = "Shader compiler failed or timed out.";

		return false;
	}

	return true;
}

} // namespace

namespace
{
struct FCachedMaterialShader
{
	FString Key;
	TArray<uint8> DXIL, SPIRV;
};
std::mutex ShaderCacheMutex;
std::deque<FCachedMaterialShader> ShaderCache;
FMaterialShaderCacheStats ShaderCacheStats;
constexpr uint64 ShaderCacheLimit = 32 * 1024 * 1024;
FString ShaderCacheKey(const FString& Compiler, const FString& Source)
{
	std::error_code Error;
	const auto Stamp = std::filesystem::last_write_time(std::filesystem::path(Compiler.ToWide()), Error);
	if (Error)
		return {};
	return Compiler + FString::Format("\n{}\n", Stamp.time_since_epoch().count()) + Source;
}
bool CachedShader(const FString& Key, FMaterialAsset& M)
{
	std::lock_guard Lock(ShaderCacheMutex);
	for (const auto& Entry : ShaderCache)
		if (!Key.IsEmpty() && Entry.Key == Key)
		{
			M.GraphDXIL = Entry.DXIL;
			M.GraphSPIRV = Entry.SPIRV;
			++ShaderCacheStats.Hits;
			return true;
		}
	++ShaderCacheStats.Misses;
	return false;
}
void CacheShader(const FString& Key, const FMaterialAsset& M)
{
	const uint64 Bytes = M.GraphDXIL.size() + M.GraphSPIRV.size() + Key.Len();
	if (Key.IsEmpty() || Bytes > ShaderCacheLimit)
		return;
	std::lock_guard Lock(ShaderCacheMutex);
	for (const auto& Entry : ShaderCache)
		if (Entry.Key == Key)
			return;
	while (!ShaderCache.empty() && ShaderCacheStats.Bytes + Bytes > ShaderCacheLimit)
	{
		ShaderCacheStats.Bytes -= ShaderCache.front().DXIL.size() + ShaderCache.front().SPIRV.size() +
								  ShaderCache.front().Key.Len();
		ShaderCache.pop_front();
	}
	ShaderCache.push_back({Key, M.GraphDXIL, M.GraphSPIRV});
	ShaderCacheStats.Bytes += Bytes;
	ShaderCacheStats.Entries = static_cast<int32>(ShaderCache.size());
}
} // namespace
FMaterialShaderCacheStats FMaterialEditingLibrary::GetShaderCacheStats()
{
	std::lock_guard Lock(ShaderCacheMutex);
	return ShaderCacheStats;
}
void FMaterialEditingLibrary::ClearShaderCache()
{
	std::lock_guard Lock(ShaderCacheMutex);
	ShaderCache.clear();
	ShaderCacheStats = {};
}

bool FMaterialEditingLibrary::CompileMaterial(FMaterialAsset& M, FString& Error)

{

	if (!M.Graph)

	{

		Error = "Material has no graph.";

		return false;
	}

	FMaterialGraphProgram Program;

	if (!FMaterialGraphCompiler::Compile(*M.Graph, Program, Error))

		return false;

	if (M.CookedGraphSource == Program.PixelShaderSource && !M.GraphDXIL.empty() && !M.GraphSPIRV.empty())

	{

		Error.Empty();

		return true;
	}

	const auto Compiler = FindDXC();
	const auto CacheKey = ShaderCacheKey(Compiler, Program.PixelShaderSource);
	if (CachedShader(CacheKey, M))
	{
		M.CookedGraphSource = Program.PixelShaderSource;
		Error.Empty();
		return true;
	}

	wchar_t Temp[MAX_PATH];

	if (!GetTempPathW(MAX_PATH, Temp))

	{

		Error = "Cannot find shader temporary directory.";

		return false;
	}

	const auto Dir = std::filesystem::path(Temp) / (L"GenesisMaterial-" + FAssetId::New().ToWide());

	std::error_code EC;

	const bool Created = std::filesystem::create_directory(Dir, EC);

	if (!Created || EC)

	{

		Error = "Cannot create shader temporary directory.";

		return false;
	}

	struct FCleanup

	{

		std::filesystem::path Dir;

		~FCleanup()

		{

			std::error_code E;

			std::filesystem::remove_all(Dir, E);
		}

	} Cleanup{Dir};

	auto Path = [&](const wchar_t* N) { return FString::FromWide((Dir / N).wstring()); };

	const auto Source = Path(L"Material.hlsl"), DXIL = Path(L"Material.dxil"), SPIRV = Path(L"Material.spv"),

			   Log = Path(L"Compile.log");

	if (!FFile::WriteAllText(Source, Program.PixelShaderSource))

	{

		Error = "Cannot write shader source.";

		return false;
	}

	TArray<uint8> D, S;

	if (!CompileTarget(Compiler, Source, DXIL, Log, false, Error) ||

		!CompileTarget(Compiler, Source, SPIRV, Log, true, Error) || !FFile::ReadAllBytes(DXIL, D) ||

		!FFile::ReadAllBytes(SPIRV, S))

		return false;

	M.GraphDXIL = std::move(D);

	M.GraphSPIRV = std::move(S);

	M.CookedGraphSource = Program.PixelShaderSource;
	CacheShader(CacheKey, M);

	Error.Empty();

	return true;
}

bool FMaterialEditingLibrary::CompileMaterialInstance(const FMaterialAsset& Parent,
													  FMaterialInstanceAsset& Instance, FString& Error)
{
	if (Instance.StaticParameters.empty() && !Instance.bOverrideQuality)
	{
		Instance.StaticPermutation.reset();
		Error.Empty();
		return true;
	}
	FMaterialAsset Permutation;
	if (!FMaterialInstance::BuildStaticPermutation(Parent, Instance, Permutation, Error) ||
		!CompileMaterial(Permutation, Error))
		return false;
	auto Candidate = Instance;
	Candidate.StaticPermutation = std::move(Permutation);
	FMaterialAsset Check;
	if (!FMaterialInstance::Resolve(Parent, Candidate, Check, Error))
		return false;
	Instance = std::move(Candidate);
	Error.Empty();
	return true;
}

bool FMaterialEditingLibrary::RecompileDependents(FAssetRegistry& Registry, const FAssetReference& Changed,
												  int32& Updated, FString& Error)
{
	Updated = 0;
	Error.Empty();
	const auto* Source = Registry.Resolve(Changed, Error);
	if (!Source)
		return false;
	const auto Assets = Registry.GetAssets();
	std::set<FString> Dirty{Source->PackagePath};
	std::set<FString> Candidates;
	bool More = true;
	while (More)
	{
		More = false;
		for (const auto& A : Assets)
			if (A.PackagePath != Source->PackagePath && !Candidates.contains(A.PackagePath) &&
				(A.Metadata.Schema == "Genesis.Material" || A.Metadata.Schema == "Genesis.MaterialFunction" ||
				 A.Metadata.Schema == "Genesis.MaterialInstance"))
				for (const auto& D : A.Metadata.Dependencies)
					if (Dirty.contains(D))
					{
						Candidates.insert(A.PackagePath);
						Dirty.insert(A.PackagePath);
						More = true;
						break;
					}
	}
	std::map<FString, int> State;
	FString Failures;
	std::function<bool(const FAssetData&)> Visit = [&](const auto& A) {
		if (State[A.PackagePath] == 2)
			return true;
		if (State[A.PackagePath] == 3)
			return false;
		if (State[A.PackagePath] == 1)
		{
			Failures += "Dependency cycle at " + A.PackagePath + "\n";
			return false;
		}
		State[A.PackagePath] = 1;
		for (const auto& D : A.Metadata.Dependencies)
			if (Candidates.contains(D))
			{
				const auto* Parent = Registry.FindByPath(D);
				if (Parent && !Visit(*Parent))
				{
					State[A.PackagePath] = 3;
					return false;
				}
			}
		FString E;
		FAssetPackage P;
		bool Success = Registry.Load(A.GetReference(), P, E);
		if (Success && P.Schema == "Genesis.MaterialFunction")
		{
			FMaterialGraph G;
			bool Changed = false;
			Success = FMaterialFunctionCodec::Decode(P, G, E) &&
					  FMaterialFunctionCodec::Refresh(G, Registry, Changed, E, P.Id) &&
					  FMaterialFunctionCodec::Encode(G, P, E);
		}
		else if (Success && P.Schema == "Genesis.Material")
		{
			FMaterialAsset M;
			bool Changed = false;
			Success = FRenderAssetCodec::DecodeMaterial(P, M, E);
			if (Success && M.Graph)
				Success = FMaterialFunctionCodec::Refresh(*M.Graph, Registry, Changed, E, P.Id) &&
						  CompileMaterial(M, E) && FRenderAssetCodec::EncodeMaterial(M, P, E);
		}
		else if (Success && P.Schema == "Genesis.MaterialInstance")
		{
			FMaterialInstanceAsset M;
			FMaterialAsset Parent;
			Success = FRenderAssetCodec::DecodeMaterialInstance(P, M, E) &&
					  FRenderAssetLoader(Registry).LoadMaterial(M.Parent, Parent, E) &&
					  CompileMaterialInstance(Parent, M, E);
			FMaterialAsset Check;
			if (Success)
				Success = FMaterialInstance::Resolve(Parent, M, Check, E) &&
						  FRenderAssetCodec::EncodeMaterialInstance(M, P, E);
		}
		if (Success)
			Success = FAssetPackageStore::Save(A.FilePath, P, E);
		if (Success)
		{
			++Updated;
			State[A.PackagePath] = 2;
			return true;
		}
		State[A.PackagePath] = 3;
		Failures += A.PackagePath + ": " + E + "\n";
		return false;
	};
	for (const auto& A : Assets)
		if (Candidates.contains(A.PackagePath))
			Visit(A);
	if (Updated)
		Registry.Scan();
	Error = std::move(Failures);
	return Error.IsEmpty();
}
