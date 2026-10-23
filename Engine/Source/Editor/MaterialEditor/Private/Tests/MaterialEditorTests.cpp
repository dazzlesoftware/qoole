// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "TestHelpers.h"

#include "UITestHelpers.h"

#include "MaterialEditingLibrary.h"

#include "SMaterialGraph.h"
#include "MaterialCompilationManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialEditor.h"
#include "SContentBrowser.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include <chrono>
#include <thread>

#include "Renderer/NativeMeshRenderer.h"

#include "Geometry/StaticMesh.h"

#include "SlateRHIRenderingPolicy.h"

#include "Widgets/SWindow.h"

#include <algorithm>

#include <limits>

#include <filesystem>

namespace

{

struct FTempFolder

{

	std::filesystem::path Path = std::filesystem::temp_directory_path() / FAssetId::New().ToWide();

	FTempFolder()

	{

		std::filesystem::create_directory(Path);
	}

	~FTempFolder()

	{

		std::error_code E;

		std::filesystem::remove_all(Path, E);
	}

	FString File(const char* N) const

	{

		return FString::FromWide((Path / N).wstring());
	}
};

FMaterialAsset ColorGraph(float R, float G, float B)

{

	FMaterialAsset M;

	M.Graph = FMaterialGraph{};

	auto N = M.Graph->Add(EMaterialExpression::Constant3Vector);

	auto* V = M.Graph->Find(N);

	V->Value[0] = R;

	V->Value[1] = G;

	V->Value[2] = B;

	M.Graph->Outputs[0] = N;

	return M;
}

TArray<uint8> DrawMaterial(FRHIDevice& D, FMaterialAsset M, bool Textured = false,
						   const FLoadedMaterialSlot* Resources = nullptr)

{

	FString Error;

	FNativeMeshRenderer Renderer(D);

	auto Asset = std::make_shared<FLoadedMeshAsset>();

	Asset->Data.Mesh = StaticMesh::Find("Cube");

	if (Resources)
		static_cast<FLoadedMaterialSlot&>(*Asset) = *Resources;
	Asset->Material = M;

	if (Textured)

	{

		FTextureAsset T;

		T.Width = T.Height = 1;

		T.bSRGB = false;

		T.Pixels = {0, 0, 255, 255};

		Asset->GraphTextures = {T};

		Asset->GraphTextureNormals = {false};
	}

	FNativeMeshDrawList List;

	Matrix44 T;

	T.SetTranslation(Vector3d(0, 220, 0));

	REQUIRE(List.Append(Asset, T, Error));

	FRHITextureDesc TD;

	TD.Width = TD.Height = 64;

	TD.bRenderTarget = true;

	auto Target = D.CreateTexture(TD);

	TD.Format = EPixelFormat::D32_FLOAT;

	auto Depth = D.CreateTexture(TD);

	REQUIRE(Target);

	REQUIRE(Depth);

	auto& Cmd = D.BeginFrame();

	auto Frame = Renderer.Prepare(List, Target->GetFormat(), Error);

	REQUIRE_MESSAGE(Frame, *Error);

	Cmd.BeginRenderPass(*Target, *Depth, FLinearColor(0, 0, 0, 1), 1.f);

	FNativeMeshView V;

	V.Width = V.Height = 64;

	V.ProjectionScale = 64;

	V.bLit = false;

	Renderer.Draw(Cmd, *Frame, V, false);

	Cmd.EndRenderPass();

	D.EndFrame();

	TArray<uint8> Pixels;

	REQUIRE(D.ReadbackTexture(*Target, Pixels));

	D.WaitIdle();

	return Pixels;
}

} // namespace

TEST_SUITE("MaterialGraph")

{

	TEST_CASE("Texture parameters and dynamic instances preserve cooks and render selected resources")
	{
		FTempFolder Temp;
		FString Error;
		FTextureAsset T;
		T.Width = T.Height = 1;
		T.bSRGB = false;
		T.Pixels = {255, 0, 0, 255};
		FAssetPackage Red, Green;
		REQUIRE(FRenderAssetCodec::EncodeTexture(T, Red, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Red.asset"), Red, Error));
		T.Pixels = {0, 255, 0, 255};
		REQUIRE(FRenderAssetCodec::EncodeTexture(T, Green, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Green.asset"), Green, Error));
		FMaterialAsset Parent;
		Parent.Graph = FMaterialGraph{};
		auto& G = *Parent.Graph;
		const auto Sample = G.Add(EMaterialExpression::TextureSampleParameter2D);
		G.Find(Sample)->ParameterName = "Albedo";
		G.Find(Sample)->Texture = {Red.Id, "/Game/Red"};
		const auto RGB = G.Add(EMaterialExpression::ComponentMask);
		G.Find(RGB)->Inputs[0] = Sample;
		G.Outputs[0] = RGB;
		const auto Roughness = G.Add(EMaterialExpression::ScalarParameter);
		G.Find(Roughness)->ParameterName = "Roughness";
		G.Outputs[1] = Roughness;
		const auto Vector = G.Add(EMaterialExpression::VectorParameter);
		G.Find(Vector)->ParameterName = "Color";
		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(Parent, Error), *Error);
		auto Dynamic = FMaterialInstanceDynamic::Create(Parent, Error);
		REQUIRE_MESSAGE(Dynamic, *Error);
		REQUIRE(Dynamic->SetTextureParameterValue("Albedo", {Green.Id, "/Game/Green"}, Error));
		REQUIRE(Dynamic->SetScalarParameterValue("Roughness", .5f, Error));
		REQUIRE(Dynamic->SetVectorParameterValue("Color", FLinearColor(.2f, .3f, .4f, 1), Error));
		CHECK_FALSE(Dynamic->SetScalarParameterValue("Color", 1, Error));
		CHECK_FALSE(
			Dynamic->SetScalarParameterValue("Roughness", std::numeric_limits<float>::quiet_NaN(), Error));
		CHECK_FALSE(Dynamic->SetTextureParameterValue("Albedo", {}, Error));
		CHECK(Dynamic->GetMaterial().Graph->Find(Sample)->Texture.Id == Green.Id);
		CHECK(Parent.Graph->Find(Sample)->Texture.Id == Red.Id);
		CHECK(Dynamic->GetMaterial().CookedGraphSource == Parent.CookedGraphSource);
		CHECK(Dynamic->GetMaterial().GraphDXIL == Parent.GraphDXIL);
		FAssetPackage Base;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Parent, Base, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Base.asset"), Base, Error));
		FMaterialInstanceAsset Instance;
		Instance.Parent = {Base.Id, "/Game/Base"};
		Instance.Textures = {{"Albedo", {Green.Id, "/Game/Green"}}};
		FAssetPackage Package;
		REQUIRE(FRenderAssetCodec::EncodeMaterialInstance(Instance, Package, Error));
		REQUIRE(Package.Dependencies.size() == 2);
		REQUIRE(FAssetPackageStore::Save(Temp.File("Instance.asset"), Package, Error));
		FMaterialInstanceAsset Decoded;
		REQUIRE(FRenderAssetCodec::DecodeMaterialInstance(Package, Decoded, Error));
		CHECK(Decoded.Textures[0].Texture.Id == Green.Id);
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry.Scan());
		FRenderAssetLoader Loader(Registry);
		FMaterialAsset Loaded;
		REQUIRE(Loader.LoadMaterial({Package.Id, "/Game/Instance"}, Loaded, Error));
		FLoadedMaterialSlot R, V;
		REQUIRE(Loader.LoadMaterialResources(Parent, R, Error));
		REQUIRE(Loader.LoadMaterialResources(Loaded, V, Error));
		for (auto Backend : {ERHIBackend::D3D12, ERHIBackend::Vulkan})
		{
			FRHIDeviceDesc DD;
			DD.Backend = Backend;
			DD.bSoftwareAdapter = Backend == ERHIBackend::D3D12;
			auto D = FRHIDevice::Create(DD);
			REQUIRE(D);
			auto RP = DrawMaterial(*D, Parent, false, &R), GP = DrawMaterial(*D, Loaded, false, &V);
			const int Pixel = (32 * 64 + 32) * 4;
			CHECK(RP[Pixel] > 200);
			CHECK(RP[Pixel + 1] < 5);
			CHECK(GP[Pixel] < 5);
			CHECK(GP[Pixel + 1] > 200);
		}
		REQUIRE(Dynamic->ClearParameterValue("Albedo", Error));
		CHECK(Dynamic->GetMaterial().Graph->Find(Sample)->Texture.Id == Red.Id);
	}
	TEST_CASE("Content browser creates instances and instance editor overrides reset undo and save")
	{
		FTempFolder Temp;
		FString Error;
		FUITestApp App;
		FMaterialAsset Parent;
		Parent.Graph = FMaterialGraph{};
		const auto Id = Parent.Graph->Add(EMaterialExpression::ScalarParameter);
		Parent.Graph->Find(Id)->ParameterName = "Tint";
		Parent.Graph->Find(Id)->Value[0] = .5f;
		Parent.Graph->Outputs[0] = Id;
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Parent, Error));
		FAssetPackage Base;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Parent, Base, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Base.asset"), Base, Error));
		const auto Registry = MakeShared<FAssetRegistry>();
		REQUIRE(Registry->SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry->Scan());
		const auto Browser =
			SNew(SContentBrowser)
				.Registry(Registry)
				.Roots(TArray<FContentBrowserRoot>{{"Content", FString::FromWide(Temp.Path.wstring())}})
				.InitialPath(FString::FromWide(Temp.Path.wstring()));
		App.AddWindow(Browser);
		App.Tick();
		Browser->SetSelectedPath(Temp.File("Base.asset"));
		const auto File = Browser->CreateMaterialInstance();
		REQUIRE_FALSE(File.IsEmpty());
		CHECK(Browser->GetAssetType(File) == EContentAssetType::Material);
		const auto* Data = Registry->FindByFile(File);
		REQUIRE(Data);
		FMaterialEditorArgs Args;
		Args.Registry = Registry;
		Args.Material = Data->GetReference();
		Args.Name = "Instance QA";
		int Saved = 0;
		Args.OnSaved = FSimpleDelegate::CreateLambda([&] { ++Saved; });
		const auto Window = FMaterialEditor::Open(Args, Error);
		REQUIRE_MESSAGE(Window, *Error);
		App.Tick();
		const auto Click = [&](const FString& Text) {
			std::function<SWidget*(SWidget&)> Find = [&](SWidget& W) -> SWidget* {
				if (auto* T = dynamic_cast<STextBlock*>(&W); T && T->GetText() == Text)
					return T;
				for (const auto& Child : W.GetArrangedChildren())
					if (auto* Result = Find(Child.Widget.Get()))
						return Result;
				return nullptr;
			};
			auto* Widget = Find(*Window);
			REQUIRE_MESSAGE(Widget, *Text);
			App.ClickWidget(*Widget);
			App.Tick();
		};
		Click("Override");
		SSpinBox<float>* Scalar = nullptr;
		std::function<void(SWidget&)> FindSpin = [&](SWidget& W) {
			if (auto* V = dynamic_cast<SSpinBox<float>*>(&W))
				Scalar = V;
			for (const auto& C : W.GetArrangedChildren())
				FindSpin(C.Widget.Get());
		};
		FindSpin(*Window);
		REQUIRE(Scalar);
		App.ClickWidget(Scalar->GetEditableText().Get());
		App.Key(EKey::A, false, true);
		App.Type("0.25");
		App.Key(EKey::Enter);
		App.Tick();
		Click("Save");
		CHECK(Saved == 1);
		FAssetPackage Package;
		FMaterialInstanceAsset Instance;
		REQUIRE(FAssetPackageStore::Load(File, Package, Error));
		REQUIRE(FRenderAssetCodec::DecodeMaterialInstance(Package, Instance, Error));
		REQUIRE(Instance.Parameters.size() == 1);
		CHECK(Instance.Parameters[0].Value[0] == .25f);
		Click("Reset to Parent");
		Click("Undo");
		Click("Save");
		CHECK(Saved == 2);
		REQUIRE(FAssetPackageStore::Load(File, Package, Error));
		REQUIRE(FRenderAssetCodec::DecodeMaterialInstance(Package, Instance, Error));
		CHECK(Instance.Parameters.size() == 1);
		Click("Redo");
		Click("Save");
		CHECK(Saved == 3);
		REQUIRE(FAssetPackageStore::Load(File, Package, Error));
		REQUIRE(FRenderAssetCodec::DecodeMaterialInstance(Package, Instance, Error));
		CHECK(Instance.Parameters.empty());
	}

	TEST_CASE("Material instance packages inherit overrides and reject cycles atomically")
	{
		FTempFolder Temp;
		FString Error;
		FMaterialAsset Parent;
		Parent.Graph = FMaterialGraph{};
		auto P = Parent.Graph->Add(EMaterialExpression::ScalarParameter);
		Parent.Graph->Find(P)->ParameterName = "Tint";
		Parent.Graph->Outputs[0] = P;
		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(Parent, Error), *Error);
		FAssetPackage Base;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Parent, Base, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Base.asset"), Base, Error));
		FMaterialInstanceAsset Instance;
		Instance.Parent = {Base.Id, "/Game/Base"};
		Instance.Parameters = {{"Tint", 1, {.25f, 0, 0, 0}}};
		FAssetPackage First;
		REQUIRE(FRenderAssetCodec::EncodeMaterialInstance(Instance, First, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("First.asset"), First, Error));
		Instance.Parent = {First.Id, "/Game/First"};
		Instance.Parameters[0].Value[0] = .75f;
		FAssetPackage Second;
		REQUIRE(FRenderAssetCodec::EncodeMaterialInstance(Instance, Second, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Second.asset"), Second, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry.Scan());
		FMaterialAsset Loaded;
		REQUIRE_MESSAGE(FRenderAssetLoader(Registry).LoadMaterial({Second.Id, "/Game/Second"}, Loaded, Error),
						*Error);
		CHECK(Loaded.Graph->Find(P)->Value[0] == .75f);
		CHECK(Loaded.GraphDXIL == Parent.GraphDXIL);
		CHECK(Loaded.CookedGraphSource == Parent.CookedGraphSource);
		auto Previous = Loaded;
		Instance.Parameters[0].Name = "Missing";
		CHECK_FALSE(FMaterialInstance::Resolve(Parent, Instance, Loaded, Error));
		CHECK(Loaded.Graph->Find(P)->Value[0] == .75f);
		Instance.Parameters[0].Name = "Tint";
		Instance.Parent = {Second.Id, "/Game/Second"};
		REQUIRE(FRenderAssetCodec::EncodeMaterialInstance(Instance, First, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("First.asset"), First, Error));
		REQUIRE(Registry.Scan());
		CHECK_FALSE(FRenderAssetLoader(Registry).LoadMaterial({Second.Id, "/Game/Second"}, Loaded, Error));
		CHECK(Error.Contains("cycle"));
		CHECK(Loaded.CookedGraphSource == Previous.CookedGraphSource);
	}

	TEST_CASE("Material parameters update pixels without recompiling cooked shaders")
	{
		FMaterialAsset M;
		M.Graph = FMaterialGraph{};
		auto& G = *M.Graph;
		auto Parameter = G.Add(EMaterialExpression::VectorParameter);
		G.Find(Parameter)->Value[0] = 1;
		auto RGB = G.Add(EMaterialExpression::ComponentMask);
		G.Find(RGB)->Inputs[0] = Parameter;
		G.Outputs[0] = RGB;
		auto Scalar = G.Add(EMaterialExpression::ScalarParameter);
		G.Find(Scalar)->Value[0] = .6f;
		G.Outputs[1] = Scalar;
		FString Error;
		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(M, Error), *Error);
		auto Green = M;
		Green.Graph->Find(Parameter)->Value[0] = 0;
		Green.Graph->Find(Parameter)->Value[1] = 1;
		FMaterialGraphProgram Program;
		REQUIRE(FMaterialGraphCompiler::Compile(*Green.Graph, Program, Error));
		CHECK(Program.PixelShaderSource == M.CookedGraphSource);
		CHECK(Program.Parameters.size() == 2);
		for (auto Backend : {ERHIBackend::D3D12, ERHIBackend::Vulkan})
		{
			FRHIDeviceDesc DD;
			DD.Backend = Backend;
			DD.bSoftwareAdapter = Backend == ERHIBackend::D3D12;
			auto D = FRHIDevice::Create(DD);
			REQUIRE(D);
			auto R = DrawMaterial(*D, M), V = DrawMaterial(*D, Green);
			const int P = (32 * 64 + 32) * 4;
			CHECK(R[P] > 200);
			CHECK(R[P + 1] < 5);
			CHECK(V[P] < 5);
			CHECK(V[P + 1] > 200);
		}
	}

	TEST_CASE("Expanded expression library cooks on both backends and annotations preserve shaders")
	{
		for (int I = int(EMaterialExpression::Reroute); I < int(EMaterialExpression::Count); ++I)
		{
			auto Type = static_cast<EMaterialExpression>(I);
			if (Type == EMaterialExpression::Comment || Type == EMaterialExpression::ScalarParameter ||
				Type == EMaterialExpression::VectorParameter ||
				Type == EMaterialExpression::TextureSampleParameter2D)
				continue;
			FMaterialAsset M;
			M.Graph = FMaterialGraph{};
			auto& G = *M.Graph;
			auto N = G.Add(Type);
			int Width = 1;
			if (Type == EMaterialExpression::Rotator)
			{
				auto UV = G.Add(EMaterialExpression::TextureCoordinate);
				G.Find(N)->Inputs[0] = UV;
				Width = 2;
			}
			if (Type == EMaterialExpression::CrossProduct)
			{
				auto V = G.Add(EMaterialExpression::Constant3Vector);
				G.Find(N)->Inputs[0] = V;
				G.Find(N)->Inputs[1] = V;
				Width = 3;
			}
			if (Type == EMaterialExpression::AppendVector)
				Width = 2;
			if (Width == 2)
			{
				auto Append = G.Add(EMaterialExpression::AppendVector);
				G.Find(Append)->Inputs[0] = N;
				G.Outputs[0] = Append;
			}
			else
				G.Outputs[Width == 3 ? 0 : 1] = N;
			FString Error;
			REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(M, Error), MaterialExpressionName(Type),
							": ", *Error);
			CHECK_FALSE(M.GraphDXIL.empty());
			CHECK_FALSE(M.GraphSPIRV.empty());
			FMaterialGraphProgram Before, After;
			REQUIRE(FMaterialGraphCompiler::Compile(G, Before, Error));
			auto Comment = G.Add(EMaterialExpression::Comment);
			G.Find(Comment)->Description = "Editor-only annotation";
			G.Find(N)->Description = "Node annotation";
			REQUIRE(FMaterialGraphCompiler::Compile(G, After, Error));
			CHECK(Before.PixelShaderSource == After.PixelShaderSource);
			FMaterialGraph RoundTrip;
			REQUIRE(FMaterialGraphCompiler::Decode(FMaterialGraphCompiler::Encode(G), RoundTrip, Error));
			CHECK(RoundTrip.Find(Comment)->Description == "Editor-only annotation");
		}
	}
	TEST_CASE("Clipboard preserves internal wires and atomically rejects invalid data")
	{
		FUITestApp App;
		auto M = std::make_shared<FMaterialAsset>();
		M->Graph = FMaterialGraph{};
		auto A = M->Graph->Add(EMaterialExpression::Constant);
		auto B = M->Graph->Add(EMaterialExpression::Multiply);
		M->Graph->Find(B)->Inputs[0] = A;
		M->Graph->Outputs[1] = B;
		const auto Canvas = SNew(SMaterialGraph).Material(M);
		App.AddWindow(Canvas, FVector2(1000, 700));
		App.Tick();
		Canvas->SetNodeSelection({A, B});
		const auto Clipboard = Canvas->CopySelection();
		FString Error;
		REQUIRE_MESSAGE(Canvas->PasteNodes(Clipboard, FVector2(600, 300), Error), *Error);
		REQUIRE(Canvas->GetSelectedNodes().size() == 2);
		auto NewA = Canvas->GetSelectedNodes()[0], NewB = Canvas->GetSelectedNodes()[1];
		CHECK(NewA != A);
		CHECK(NewB != B);
		CHECK(M->Graph->Find(NewB)->Inputs[0] == NewA);
		CHECK(M->Graph->Outputs[1] == B);
		CHECK_FALSE(Canvas->PasteNodes("{}", FVector2(), Error));
		CHECK(M->Graph->Nodes.size() == 4);
		Canvas->Undo();
		CHECK(M->Graph->Nodes.size() == 2);
		Canvas->Redo();
		CHECK(M->Graph->Nodes.size() == 4);
	}
	TEST_CASE("Background compiler publishes only the latest requested revision")
	{
		FMaterialCompilationManager Compiler;
		auto M = ColorGraph(1, 0, 0);
		Compiler.Submit(M, 1);
		Compiler.Submit(ColorGraph(0, 1, 0), 2);
		Compiler.Submit(M, 3);
		TOptional<FMaterialCompilationResult> Result;
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
		while (std::chrono::steady_clock::now() < Deadline)
		{
			Result = Compiler.TakeCompleted();
			if (Result)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		REQUIRE(Result.has_value());
		CHECK(Result->Revision == 3);
		REQUIRE_MESSAGE(Result->bSuccess, *Result->Error);
		CHECK_FALSE(Result->Material.GraphDXIL.empty());
		CHECK_FALSE(Compiler.IsBusy());
	}

	TEST_CASE("Uploads and idle waits cannot release resources used by an open frame")

	{

		for (auto Backend : {ERHIBackend::D3D12, ERHIBackend::Vulkan})
		{

			FRHIDeviceDesc DD;
			DD.Backend = Backend;
			DD.bSoftwareAdapter = Backend == ERHIBackend::D3D12;
			auto Device = FRHIDevice::Create(DD);

			if (!Device && Backend == ERHIBackend::Vulkan)
				continue;
			REQUIRE(Device);

			auto Resource = std::make_shared<int>(42);
			std::weak_ptr<int> Weak = Resource;

			Device->BeginFrame();
			Device->DeferRelease(Resource);
			Resource.reset();

			Device->WaitIdle();
			CHECK_FALSE(Weak.expired());

			const uint32 White = 0xffffffff;
			FRHITextureDesc TD;
			TD.Width = TD.Height = 1;
			TD.InitialData = &White;
			auto Texture = Device->CreateTexture(TD);
			REQUIRE(Texture);

			Device->WaitIdle();
			CHECK_FALSE(Weak.expired());

			Device->EndFrame();
			Device->WaitIdle();
			CHECK(Weak.expired());
		}
	}

	TEST_CASE("Arithmetic and UV expressions lower to valid programs on both desktop targets")

	{

		FString Error;

		auto M = ColorGraph(.1f, .2f, .3f);
		auto& G = *M.Graph;

		auto A = G.Add(EMaterialExpression::Constant);
		G.Find(A)->Value[0] = .4f;

		for (auto E : {EMaterialExpression::Add, EMaterialExpression::Subtract, EMaterialExpression::Multiply,
					   EMaterialExpression::Divide, EMaterialExpression::Lerp, EMaterialExpression::OneMinus,
					   EMaterialExpression::Clamp})
		{

			auto B = G.Add(E);
			G.Find(B)->Inputs[0] = A;
			A = B;
		}

		G.Outputs[1] = A;

		auto UV = G.Add(EMaterialExpression::TextureCoordinate),
			 Scale = G.Add(EMaterialExpression::Constant2Vector);
		G.Find(Scale)->Value[0] = 2;
		G.Find(Scale)->Value[1] = 3;

		auto Multiply = G.Add(EMaterialExpression::Multiply);
		G.Find(Multiply)->Inputs[0] = UV;
		G.Find(Multiply)->Inputs[1] = Scale;

		auto Texture = G.Add(EMaterialExpression::TextureSample);
		G.Find(Texture)->Texture = {FAssetId::New(), "/Game/Texture"};
		G.Find(Texture)->Inputs[0] = Multiply;

		auto Mask = G.Add(EMaterialExpression::ComponentMask);
		G.Find(Mask)->Inputs[0] = Texture;
		G.Outputs[0] = Mask;

		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(M, Error), *Error);

		CHECK_FALSE(M.GraphDXIL.empty());
		CHECK_FALSE(M.GraphSPIRV.empty());
	}

	TEST_CASE("Typed graph validation is atomic and layout independent")

	{

		auto M = ColorGraph(.1f, .2f, .3f);

		auto& G = *M.Graph;

		FString Error;

		FMaterialGraphProgram P;

		REQUIRE(FMaterialGraphCompiler::Compile(G, P, Error));

		auto Source = P.PixelShaderSource;

		G.Nodes[0].X = 250;

		G.Nodes[0].Y = -30;

		REQUIRE(FMaterialGraphCompiler::Compile(G, P, Error));

		CHECK(P.PixelShaderSource == Source);

		auto A = G.Add(EMaterialExpression::Multiply);

		G.Find(A)->Inputs[0] = G.Outputs[0];

		G.Find(A)->Value[1] = .5f;

		G.Outputs[0] = A;

		REQUIRE(FMaterialGraphCompiler::Compile(G, P, Error));

		CHECK(P.PixelShaderSource != Source);

		Source = P.PixelShaderSource;

		G.Find(A)->Inputs[1] = A;

		CHECK_FALSE(FMaterialGraphCompiler::Compile(G, P, Error));

		CHECK(Error.Contains("cycle"));

		CHECK(P.PixelShaderSource == Source);

		G.Find(A)->Inputs[1] = 0;

		G.Outputs[1] = G.Outputs[0];

		CHECK_FALSE(FMaterialGraphCompiler::Compile(G, P, Error));

		CHECK(Error.Contains("type mismatch"));

		G.Outputs[1] = 0;

		G.Find(A)->Inputs[0] = 999;

		CHECK_FALSE(FMaterialGraphCompiler::Compile(G, P, Error));

		G.Find(A)->Inputs[0] = 1;

		G.Nodes.push_back(G.Nodes[0]);

		CHECK_FALSE(FMaterialGraphCompiler::Compile(G, P, Error));
	}

	TEST_CASE("Graph packages cook DXIL and SPIRV preserve identity and reject stale cooks")

	{

		FString Error;

		auto M = ColorGraph(.1f, .8f, .2f);

		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(M, Error), *Error);

		CHECK(M.GraphDXIL.size() > 4);

		CHECK(M.GraphSPIRV.size() > 4);

		FAssetPackage P;

		P.Id = FAssetId::New();

		const auto Id = P.Id;

		REQUIRE(FRenderAssetCodec::EncodeMaterial(M, P, Error));

		CHECK(P.SchemaVersion == 4);

		CHECK(P.Id == Id);

		FMaterialAsset Back;

		REQUIRE(FRenderAssetCodec::DecodeMaterial(P, Back, Error));

		REQUIRE(Back.Graph);

		CHECK(Back.Graph->Nodes[0].Value[1] == .8f);

		CHECK(Back.GraphDXIL == M.GraphDXIL);

		const auto Before = P;

		M.Graph->Nodes[0].Value[0] = .9f;

		CHECK_FALSE(FRenderAssetCodec::EncodeMaterial(M, P, Error));

		CHECK(P.Bulk == Before.Bulk);

		auto Bad = P;

		Bad.Exports["Graph"]["Outputs"][0] = 999;

		CHECK_FALSE(FRenderAssetCodec::DecodeMaterial(Bad, Back, Error));

		CHECK(Back.GraphDXIL == M.GraphDXIL);

		Bad = P;

		Bad.Bulk.pop_back();

		CHECK_FALSE(FRenderAssetCodec::DecodeMaterial(Bad, Back, Error));

		FTempFolder Temp;

		REQUIRE(FAssetPackageStore::Save(Temp.File("Graph.asset"), P, Error));

		FAssetPackage Disk;

		REQUIRE(FAssetPackageStore::Load(Temp.File("Graph.asset"), Disk, Error));

		REQUIRE(FRenderAssetCodec::DecodeMaterial(Disk, Back, Error));

		CHECK(Back.GraphSPIRV == M.GraphSPIRV);
	}

	TEST_CASE("Legacy material upgrade preserves linear color alpha and normal references")

	{

		FMaterialAsset M;

		M.BaseColor[0] = .5f;

		M.Roughness = .23f;

		M.Metallic = .7f;

		M.BaseColorTexture = {FAssetId::New(), "/Game/Color"};

		M.NormalTexture = {FAssetId::New(), "/Game/Normal"};

		M.Graph = FMaterialEditingLibrary::CreateGraph(M);

		FMaterialGraphProgram P;

		FString Error;

		REQUIRE(FMaterialGraphCompiler::Compile(*M.Graph, P, Error));

		REQUIRE(P.Textures.size() == 2);

		CHECK(P.Textures[0].Reference.Id == M.BaseColorTexture.Id);

		CHECK(P.Textures[1].bNormal);

		CHECK(M.Graph->Nodes[0].Value[0] == doctest::Approx(.214041f));

		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(M, Error), *Error);

		FAssetPackage Package;

		REQUIRE(FRenderAssetCodec::EncodeMaterial(M, Package, Error));

		CHECK(Package.Dependencies.size() == 2);

		// Exercise the real registry/mesh loader, including graph-only resources.

		FTempFolder Temp;

		FTextureAsset Image;
		Image.Width = Image.Height = 1;
		Image.Pixels = {128, 128, 255, 255};

		FAssetPackage Color, Normal;
		Color.Id = M.BaseColorTexture.Id;
		Normal.Id = M.NormalTexture.Id;

		REQUIRE(FRenderAssetCodec::EncodeTexture(Image, Color, Error));
		REQUIRE(FRenderAssetCodec::EncodeTexture(Image, Normal, Error));

		REQUIRE(FAssetPackageStore::Save(Temp.File("Color.asset"), Color, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Normal.asset"), Normal, Error));

		REQUIRE(FAssetPackageStore::Save(Temp.File("Material.asset"), Package, Error));

		FStaticMeshAsset Mesh{StaticMesh::Find("Cube"), {Package.Id, "/Game/Material"}};
		FAssetPackage MeshPackage;

		REQUIRE(FRenderAssetCodec::EncodeMesh(Mesh, MeshPackage, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Mesh.asset"), MeshPackage, Error));

		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry.Scan());

		FLoadedMeshAsset Loaded;
		REQUIRE_MESSAGE(FRenderAssetLoader(Registry).LoadMesh({MeshPackage.Id, "/Game/Mesh"}, Loaded, Error),
						*Error);

		REQUIRE(Loaded.Material);
		REQUIRE(Loaded.Material->Graph);
		CHECK(Loaded.GraphTextures.size() == 2);
		CHECK(Loaded.GraphTextureNormals.size() == 2);
		CHECK_FALSE(Loaded.Texture);
		CHECK_FALSE(Loaded.NormalTexture);
	}

	TEST_CASE("Canvas pin drag edits undo redo and scaled input")

	{

		FUITestApp App(1.5f);

		auto M = std::make_shared<FMaterialAsset>(ColorGraph(.2f, .3f, .4f));

		const auto Canvas = SNew(SMaterialGraph).Material(M);

		const auto W = App.AddWindow(Canvas, FVector2(900, 500));

		App.Tick();

		auto Scalar = Canvas->AddNode(EMaterialExpression::Constant, FVector2(300, 150));

		const auto From = Canvas->GetPinPosition(Scalar, 0, true), To = Canvas->GetPinPosition(0, 1, false);

		App.MouseDown(W, From);

		App.Tick();

		App.MouseMove(W, To);

		App.Tick();

		App.MouseUp(W, To);

		App.Tick();

		CHECK(M->Graph->Outputs[1] == Scalar);

		Canvas->Undo();

		CHECK(M->Graph->Outputs[1] == 0);

		Canvas->Redo();

		CHECK(M->Graph->Outputs[1] == Scalar);

		App.Click(W, Canvas->GetPinPosition(Scalar, 0, true) - FVector2(50, 25));

		App.Key(EKey::Delete);

		App.Tick();

		CHECK(M->Graph->Outputs[1] == 0);

		CHECK(M->Graph->Find(Scalar) == nullptr);

		Canvas->Undo();

		CHECK(M->Graph->Find(Scalar) != nullptr);
	}

	TEST_CASE("Cooked graph changes GPU pixels on D3D12 and Vulkan")

	{

		FString Error;

		auto Red = ColorGraph(1, 0, 0), Green = ColorGraph(0, 1, 0);

		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(Red, Error), *Error);

		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(Green, Error), *Error);

		auto Blue = ColorGraph(0, 0, 0);

		auto T = Blue.Graph->Add(EMaterialExpression::TextureSample);

		Blue.Graph->Find(T)->Texture = {FAssetId::New(), "/Game/Texture"};

		auto RGB = Blue.Graph->Add(EMaterialExpression::ComponentMask);

		Blue.Graph->Find(RGB)->Inputs[0] = T;

		Blue.Graph->Outputs[0] = RGB;

		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(Blue, Error), *Error);

		for (auto Backend : {ERHIBackend::D3D12, ERHIBackend::Vulkan})

		{

			FRHIDeviceDesc DD;

			DD.Backend = Backend;

			DD.bSoftwareAdapter = Backend == ERHIBackend::D3D12;

			auto D = FRHIDevice::Create(DD);

			if (!D && Backend == ERHIBackend::Vulkan)

			{

				MESSAGE("Vulkan unavailable");

				continue;
			}

			REQUIRE(D);

			auto R = DrawMaterial(*D, Red), G = DrawMaterial(*D, Green);

			const int P = (32 * 64 + 32) * 4;

			CHECK(R[P] > 200);

			CHECK(R[P + 1] < 5);

			CHECK(G[P] < 5);

			CHECK(G[P + 1] > 200);

			auto B = DrawMaterial(*D, Blue, true);

			CHECK(B[P] < 5);

			CHECK(B[P + 1] < 5);

			CHECK(B[P + 2] > 200);
		}
	}

	TEST_CASE("Canvas draws a readable graph into the actual Slate renderer")

	{

		FTestLogCapture Log;

		FUITestApp App;

		auto M = std::make_shared<FMaterialAsset>();

		M->Graph = FMaterialEditingLibrary::CreateGraph(*M);

		const auto Canvas = SNew(SMaterialGraph).Material(M);

		const auto W = App.AddWindow(Canvas, FVector2(1280, 720));

		App.Tick();

		FRHIDeviceDesc DD;

		DD.Backend = ERHIBackend::D3D12;

		DD.bSoftwareAdapter = true;

		auto D = FRHIDevice::Create(DD);

		REQUIRE(D);

		FSlateRHIRenderingPolicy Renderer(*D, App.GetApp().GetFontCache());

		REQUIRE(Renderer.Initialize());

		auto Target = Renderer.CreateRenderTarget(1280, 720);

		auto& Cmd = D->BeginFrame();

		Renderer.BeginFrame();

		Renderer.Render(Cmd, Target->GetTexture(), W->GetDrawElements(), FLinearColor(0, 0, 0, 1));

		D->EndFrame();

		TArray<uint8> Pixels;

		REQUIRE(D->ReadbackTexture(Target->GetTexture(), Pixels));

		CHECK(Pixels.size() == 1280 * 720 * 4);

		auto Dir = std::filesystem::path(FPaths::ExecutableDir().ToWide()) / "../../Saved/MaterialGraph";

		std::filesystem::create_directories(Dir);

		REQUIRE(FFileHelper::SaveArrayToFile(Pixels, FString::FromWide((Dir / "Canvas.rgba").wstring())));

		D->WaitIdle();

		CHECK(Log.Problems.empty());
	}
}
