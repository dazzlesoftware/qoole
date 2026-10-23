// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MaterialEditingLibrary.h"

#include "HAL/File.h"

#include "HAL/PlatformMisc.h"

#include <Windows.h>

#include <filesystem>

#include <cmath>

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

	const auto Compiler = FindDXC();

	if (!CompileTarget(Compiler, Source, DXIL, Log, false, Error) ||

		!CompileTarget(Compiler, Source, SPIRV, Log, true, Error) || !FFile::ReadAllBytes(DXIL, D) ||

		!FFile::ReadAllBytes(SPIRV, S))

		return false;

	M.GraphDXIL = std::move(D);

	M.GraphSPIRV = std::move(S);

	M.CookedGraphSource = Program.PixelShaderSource;

	Error.Empty();

	return true;
}
