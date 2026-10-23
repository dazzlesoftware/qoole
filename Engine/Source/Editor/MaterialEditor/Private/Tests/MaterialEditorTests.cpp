// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "TestHelpers.h"

#include "UITestHelpers.h"

#include "MaterialEditingLibrary.h"
#include "HAL/File.h"

#include "SMaterialGraph.h"
#include "MaterialCompilationManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialEditor.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialParameterCollection.h"
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
						   const FLoadedMaterialSlot* Resources = nullptr,
						   std::shared_ptr<FMaterialParameterCollectionInstance> Collection = nullptr)

{

	FString Error;

	FNativeMeshRenderer Renderer(D);
	if (Collection)
		Renderer.SetParameterCollectionInstance(Collection);

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
				Type == EMaterialExpression::TextureSampleParameter2D ||
				Type == EMaterialExpression::MaterialFunctionCall ||
				Type == EMaterialExpression::ScalarCollectionParameter ||
				Type == EMaterialExpression::VectorCollectionParameter ||
				Type == EMaterialExpression::MakeMaterialAttributes ||
				Type == EMaterialExpression::BreakMaterialAttributes ||
				Type == EMaterialExpression::BlendMaterialAttributes)
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

	TEST_CASE("Static quality and platform choices cook render and reuse cached permutations")
	{
		FMaterialEditingLibrary::ClearShaderCache();
		FMaterialAsset Material;
		Material.Graph = FMaterialGraph{};
		auto& G = *Material.Graph;
		const auto Red = G.Add(EMaterialExpression::Constant3Vector),
				   Green = G.Add(EMaterialExpression::Constant3Vector),
				   Blue = G.Add(EMaterialExpression::Constant3Vector);
		G.Find(Red)->Value[0] = 1;
		G.Find(Green)->Value[1] = 1;
		G.Find(Blue)->Value[2] = 1;
		const auto Static = G.Add(EMaterialExpression::StaticSwitchParameter);
		G.Find(Static)->ParameterName = "UseRed";
		G.Find(Static)->Inputs[0] = Red;
		G.Find(Static)->Inputs[1] = Green;
		G.Find(Static)->Value[2] = 1;
		G.Outputs[0] = Static;
		FString Error;
		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(Material, Error), *Error);
		const auto RedShader = Material.GraphDXIL;
		auto GreenMaterial = Material;
		GreenMaterial.Graph->Find(Static)->Value[2] = 0;
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(GreenMaterial, Error));
		CHECK(GreenMaterial.GraphDXIL != RedShader);
		auto Again = GreenMaterial;
		Again.Graph->Find(Static)->Value[2] = 1;
		const auto Hits = FMaterialEditingLibrary::GetShaderCacheStats().Hits;
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Again, Error));
		CHECK(Again.GraphDXIL == RedShader);
		CHECK(FMaterialEditingLibrary::GetShaderCacheStats().Hits == Hits + 1);
		const auto Quality = G.Add(EMaterialExpression::QualitySwitch);
		G.Find(Quality)->Inputs[0] = Red;
		G.Find(Quality)->Inputs[1] = Green;
		G.Find(Quality)->Inputs[2] = Blue;
		G.Outputs[0] = Quality;
		auto Low = Material;
		Low.Graph->QualityLevel = 0;
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Low, Error));
		auto High = Material;
		High.Graph->QualityLevel = 2;
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(High, Error));
		CHECK(Low.GraphDXIL != High.GraphDXIL);
		const auto Platform = G.Add(EMaterialExpression::PlatformSwitch);
		G.Find(Platform)->Inputs[0] = Red;
		G.Find(Platform)->Inputs[1] = Green;
		G.Outputs[0] = Platform;
		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(Material, Error), *Error);
		for (auto Backend : {ERHIBackend::D3D12, ERHIBackend::Vulkan})
		{
			FRHIDeviceDesc DD;
			DD.Backend = Backend;
			DD.bSoftwareAdapter = Backend == ERHIBackend::D3D12;
			auto Device = FRHIDevice::Create(DD);
			REQUIRE(Device);
			auto R = DrawMaterial(*Device, Again), V = DrawMaterial(*Device, GreenMaterial),
				 L = DrawMaterial(*Device, Low), H = DrawMaterial(*Device, High),
				 P = DrawMaterial(*Device, Material);
			const int Center = (32 * 64 + 32) * 4;
			CHECK(R[Center] > 200);
			CHECK(R[Center + 1] < 5);
			CHECK(V[Center] < 5);
			CHECK(V[Center + 1] > 200);
			CHECK(L[Center] > 200);
			CHECK(H[Center + 2] > 200);
			CHECK(P[Center + (Backend == ERHIBackend::D3D12 ? 0 : 1)] > 200);
			CHECK(P[Center + (Backend == ERHIBackend::D3D12 ? 1 : 0)] < 5);
		}
		FAssetPackage Package;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Material, Package, Error));
		FMaterialAsset Loaded;
		REQUIRE(FRenderAssetCodec::DecodeMaterial(Package, Loaded, Error));
		CHECK(Loaded.Graph->QualityLevel == 2);
		auto Canvas = SNew(SMaterialGraph).Material(std::make_shared<FMaterialAsset>(Loaded));
		Canvas->SetQualityLevel(0);
		Canvas->Undo();
		Canvas->Redo();
		auto Bad = *Material.Graph;
		Bad.Find(Static)->Value[2] = 0.5f;
		FMaterialGraphProgram Program;
		CHECK_FALSE(FMaterialGraphCompiler::Compile(Bad, Program, Error));
		CHECK(Error.Contains("Static switch"));
		Bad = *Material.Graph;
		Bad.QualityLevel = 3;
		CHECK_FALSE(FMaterialGraphCompiler::Compile(Bad, Program, Error));
	}

	TEST_CASE("Reusable material functions refresh atomically and cook self-contained snapshots")
	{
		FTempFolder Temp;
		FString Error;
		FMaterialGraph Function;
		Function.bFunction = true;
		const auto Input = Function.Add(EMaterialExpression::FunctionInput);
		const auto Multiplier = Function.Add(EMaterialExpression::Constant);
		Function.Find(Multiplier)->Value[0] = 2;
		const auto Product = Function.Add(EMaterialExpression::Multiply);
		Function.Find(Product)->Inputs[0] = Input;
		Function.Find(Product)->Inputs[1] = Multiplier;
		Function.Outputs[0] = Product;
		FAssetPackage Definition;
		REQUIRE(FMaterialFunctionCodec::Encode(Function, Definition, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Double.asset"), Definition, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry.Scan());
		FMaterialAsset Material;
		Material.Graph = FMaterialGraph{};
		auto& G = *Material.Graph;
		const auto Value = G.Add(EMaterialExpression::Constant);
		G.Find(Value)->Value[0] = .2f;
		const auto Call = G.Add(EMaterialExpression::MaterialFunctionCall);
		G.Find(Call)->Function = {"", "/Game/Double"};
		G.Find(Call)->Inputs[0] = Value;
		G.Outputs[0] = Call;
		bool Changed = false;
		REQUIRE_MESSAGE(FMaterialFunctionCodec::Refresh(G, Registry, Changed, Error), *Error);
		CHECK(Changed);
		CHECK(G.Find(Call)->Function.Id == Definition.Id);
		REQUIRE(G.Functions.size() == 1);
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Material, Error));
		FAssetPackage Cooked;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Material, Cooked, Error));
		CHECK(std::find(Cooked.Dependencies.begin(), Cooked.Dependencies.end(), "/Game/Double") !=
			  Cooked.Dependencies.end());
		FMaterialAsset Loaded;
		REQUIRE_MESSAGE(FRenderAssetCodec::DecodeMaterial(Cooked, Loaded, Error), *Error);
		CHECK(Loaded.Graph->Functions.size() == 1);
		FMaterialAsset Flat;
		Flat.Graph = FMaterialGraph{};
		const auto C = Flat.Graph->Add(EMaterialExpression::Constant);
		Flat.Graph->Find(C)->Value[0] = .4f;
		Flat.Graph->Outputs[0] = C;
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Flat, Error));
		for (auto Backend : {ERHIBackend::D3D12, ERHIBackend::Vulkan})
		{
			FRHIDeviceDesc DD;
			DD.Backend = Backend;
			DD.bSoftwareAdapter = Backend == ERHIBackend::D3D12;
			auto Device = FRHIDevice::Create(DD);
			REQUIRE(Device);
			const auto A = DrawMaterial(*Device, Loaded), B = DrawMaterial(*Device, Flat);
			CHECK(A == B);
		}
		REQUIRE(FMaterialFunctionCodec::Refresh(G, Registry, Changed, Error));
		CHECK_FALSE(Changed);
		Function.Find(Multiplier)->Value[0] = 3;
		REQUIRE(FMaterialFunctionCodec::Encode(Function, Definition, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Double.asset"), Definition, Error));
		REQUIRE(Registry.Scan());
		REQUIRE(FMaterialFunctionCodec::Refresh(G, Registry, Changed, Error));
		CHECK(Changed);
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Material, Error));
		CHECK(Material.GraphDXIL != Loaded.GraphDXIL);
		auto Before = FMaterialGraphCompiler::Encode(G);
		auto Bad = G;
		Bad.Find(Call)->Function.Id = FAssetId::New();
		CHECK_FALSE(FMaterialFunctionCodec::Refresh(Bad, Registry, Changed, Error));
		CHECK(Bad.Functions.size() == G.Functions.size());
		CHECK_FALSE(FMaterialFunctionCodec::Refresh(G, Registry, Changed, Error, Definition.Id));
		CHECK(Error.Contains("recursion"));
		CHECK(FMaterialGraphCompiler::Encode(G) == Before);
		auto Wrong = G;
		Wrong.Find(Value)->Expression = EMaterialExpression::Constant3Vector;
		FMaterialGraphProgram Program;
		CHECK_FALSE(FMaterialGraphCompiler::Compile(Wrong, Program, Error));
		CHECK(Error.Contains("Function input type"));
		auto Default = G;
		Default.Find(Call)->Inputs[0] = 0;
		REQUIRE(FMaterialGraphCompiler::Compile(Default, Program, Error));
	}
	TEST_CASE("Material function assets are created and reopened in the graph editor")
	{
		FTempFolder Temp;
		FString Error;
		FUITestApp App;
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
		const auto File = Browser->CreateMaterialFunction();
		REQUIRE(!File.IsEmpty());
		CHECK(Browser->GetAssetType(File) == EContentAssetType::MaterialFunction);
		const auto* Asset = Registry->FindByFile(File);
		REQUIRE(Asset);
		CHECK(Asset->Metadata.Schema == "Genesis.MaterialFunction");
		FMaterialEditorArgs Args;
		Args.Registry = Registry;
		Args.Material = Asset->GetReference();
		Args.Name = "Reusable";
		auto Window = FMaterialEditor::Open(Args, Error);
		REQUIRE_MESSAGE(Window, *Error);
		App.Tick();
		CHECK(Window->GetTitle().Contains("Material Function"));
		Window->RequestClose();
	}

	TEST_CASE("Shared parameter collections update uniform snapshots without recooking material shaders")
	{
		FTempFolder Temp;
		FString Error;
		FMaterialParameterCollection Collection;
		Collection.Parameters = {{"Color", 4, {1, 0, 0, 1}}, {"Roughness", 1, {.5f, 0, 0, 0}}};
		FAssetPackage Package;
		REQUIRE(FMaterialParameterCollectionCodec::Encode(Collection, Package, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Shared.asset"), Package, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry.Scan());
		FMaterialAsset Material;
		Material.Graph = FMaterialGraph{};
		auto& G = *Material.Graph;
		const auto Color = G.Add(EMaterialExpression::VectorCollectionParameter);
		G.Find(Color)->Collection = {Package.Id, "/Game/Shared"};
		G.Find(Color)->ParameterName = "Color";
		const auto Mask = G.Add(EMaterialExpression::ComponentMask);
		G.Find(Mask)->Inputs[0] = Color;
		G.Outputs[0] = Mask;
		const auto Roughness = G.Add(EMaterialExpression::ScalarCollectionParameter);
		G.Find(Roughness)->Collection = {Package.Id, "/Game/Shared"};
		G.Find(Roughness)->ParameterName = "Roughness";
		G.Outputs[1] = Roughness;
		bool Changed = false;
		REQUIRE_MESSAGE(FMaterialFunctionCodec::Refresh(G, Registry, Changed, Error), *Error);
		CHECK(Changed);
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Material, Error));
		FAssetPackage MaterialPackage;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Material, MaterialPackage, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Material.asset"), MaterialPackage, Error));
		CHECK(std::find(MaterialPackage.Dependencies.begin(), MaterialPackage.Dependencies.end(),
						"/Game/Shared") != MaterialPackage.Dependencies.end());
		auto Instance = std::make_shared<FMaterialParameterCollectionInstance>(
			FAssetReference{Package.Id, "/Game/Shared"}, Collection);
		const auto Shader = Material.GraphDXIL;
		const float Green[4] = {0, 1, 0, 1};
		const float Red[4] = {1, 0, 0, 1};
		for (auto Backend : {ERHIBackend::D3D12, ERHIBackend::Vulkan})
		{
			FRHIDeviceDesc DD;
			DD.Backend = Backend;
			DD.bSoftwareAdapter = Backend == ERHIBackend::D3D12;
			auto Device = FRHIDevice::Create(DD);
			REQUIRE(Device);
			REQUIRE(Instance->SetVector("Color", Red, Error));
			auto R = DrawMaterial(*Device, Material, false, nullptr, Instance);
			REQUIRE(Instance->SetVector("Color", Green, Error));
			auto V = DrawMaterial(*Device, Material, false, nullptr, Instance);
			const int Center = (32 * 64 + 32) * 4;
			CHECK(R[Center] > 200);
			CHECK(R[Center + 1] < 5);
			CHECK(V[Center] < 5);
			CHECK(V[Center + 1] > 200);
			CHECK(Material.GraphDXIL == Shader);
		}
		CHECK_FALSE(Instance->SetScalar("Color", 1, Error));
		CHECK_FALSE(Instance->SetScalar("Roughness", std::numeric_limits<float>::quiet_NaN(), Error));
		Collection.Parameters[0].Value[0] = 0;
		Collection.Parameters[0].Value[2] = 1;
		REQUIRE(FMaterialParameterCollectionCodec::Encode(Collection, Package, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Shared.asset"), Package, Error));
		REQUIRE(Registry.Scan());
		FMaterialAsset Reloaded;
		REQUIRE(FRenderAssetLoader(Registry).LoadMaterial({MaterialPackage.Id, "/Game/Material"}, Reloaded,
														  Error));
		CHECK(Reloaded.Graph->Find(Color)->Value[2] == 1);
		CHECK(Reloaded.GraphDXIL == Shader);
		CHECK(Reloaded.CookedGraphSource == Material.CookedGraphSource);
		auto Bad = Collection;
		Bad.Parameters.push_back(Bad.Parameters[0]);
		CHECK_FALSE(FMaterialParameterCollectionCodec::Encode(Bad, Package, Error));
		Collection.Parameters[0].Width = 1;
		REQUIRE(FMaterialParameterCollectionCodec::Encode(Collection, Package, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Shared.asset"), Package, Error));
		REQUIRE(Registry.Scan());
		CHECK_FALSE(FRenderAssetLoader(Registry).LoadMaterial({MaterialPackage.Id, "/Game/Material"},
															  Reloaded, Error));
		CHECK(Reloaded.Graph->Find(Color)->Value[2] == 1);
	}
	TEST_CASE("Collection editor creates shared assets and supports history and persistence")
	{
		FTempFolder Temp;
		FString Error;
		FUITestApp App;
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
		const auto File = Browser->CreateMaterialParameterCollection();
		REQUIRE(!File.IsEmpty());
		const auto* Asset = Registry->FindByFile(File);
		REQUIRE(Asset);
		FMaterialEditorArgs Args;
		Args.Registry = Registry;
		Args.Material = Asset->GetReference();
		Args.Name = "Shared";
		auto Window = FMaterialEditor::Open(Args, Error);
		REQUIRE(Window);
		App.Tick();
		const auto Click = [&](const FString& Text) {
			std::function<SWidget*(SWidget&)> Find = [&](SWidget& W) -> SWidget* {
				if (auto* T = dynamic_cast<STextBlock*>(&W); T && T->GetText() == Text)
					return T;
				for (const auto& C : W.GetArrangedChildren())
					if (auto* R = Find(C.Widget.Get()))
						return R;
				return nullptr;
			};
			auto* Widget = Find(*Window);
			REQUIRE_MESSAGE(Widget, *Text);
			App.ClickWidget(*Widget);
			App.Tick();
		};
		Click("Add Scalar");
		Click("Undo");
		Click("Redo");
		Click("Save");
		FAssetPackage P;
		FMaterialParameterCollection C;
		REQUIRE(FAssetPackageStore::Load(File, P, Error));
		REQUIRE(FMaterialParameterCollectionCodec::Decode(P, C, Error));
		CHECK(C.Parameters.size() == 3);
		Click("Undo");
		Click("Save");
		REQUIRE(FAssetPackageStore::Load(File, P, Error));
		REQUIRE(FMaterialParameterCollectionCodec::Decode(P, C, Error));
		CHECK(C.Parameters.size() == 2);
		Window->RequestClose();
	}

	TEST_CASE("Static and quality instance overrides are cooked portable and reject stale permutations")
	{
		FTempFolder Temp;
		FString Error;
		FMaterialAsset Parent;
		Parent.Graph = FMaterialGraph{};
		auto& G = *Parent.Graph;
		const auto Red = G.Add(EMaterialExpression::Constant3Vector),
				   Green = G.Add(EMaterialExpression::Constant3Vector);
		G.Find(Red)->Value[0] = 1;
		G.Find(Green)->Value[1] = 1;
		const auto Switch = G.Add(EMaterialExpression::StaticSwitchParameter);
		G.Find(Switch)->ParameterName = "UseRed";
		G.Find(Switch)->Value[2] = 1;
		G.Find(Switch)->Inputs[0] = Red;
		G.Find(Switch)->Inputs[1] = Green;
		const auto Quality = G.Add(EMaterialExpression::QualitySwitch);
		G.Find(Quality)->Inputs[0] = Red;
		G.Find(Quality)->Inputs[1] = Green;
		G.Find(Quality)->Inputs[2] = Switch;
		G.Outputs[0] = Quality;
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Parent, Error));
		FAssetPackage Base;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Parent, Base, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Base.asset"), Base, Error));
		FMaterialInstanceAsset Instance;
		Instance.Parent = {Base.Id, "/Game/Base"};
		Instance.StaticParameters = {{"UseRed", false}};
		FAssetPackage Package;
		CHECK_FALSE(FRenderAssetCodec::EncodeMaterialInstance(Instance, Package, Error));
		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterialInstance(Parent, Instance, Error), *Error);
		REQUIRE(FRenderAssetCodec::EncodeMaterialInstance(Instance, Package, Error));
		CHECK(Package.SchemaVersion == 3);
		CHECK(!Package.Bulk.empty());
		REQUIRE(FAssetPackageStore::Save(Temp.File("Instance.asset"), Package, Error));
		FMaterialInstanceAsset Decoded;
		REQUIRE(FRenderAssetCodec::DecodeMaterialInstance(Package, Decoded, Error));
		CHECK(Decoded.StaticParameters.size() == 1);
		FMaterialAsset Resolved;
		REQUIRE(FMaterialInstance::Resolve(Parent, Decoded, Resolved, Error));
		CHECK(Resolved.CookedGraphSource != Parent.CookedGraphSource);
		auto Low = Instance;
		Low.bOverrideQuality = true;
		Low.QualityLevel = 0;
		REQUIRE(FMaterialEditingLibrary::CompileMaterialInstance(Parent, Low, Error));
		FMaterialAsset LowMaterial;
		REQUIRE(FMaterialInstance::Resolve(Parent, Low, LowMaterial, Error));
		for (auto Backend : {ERHIBackend::D3D12, ERHIBackend::Vulkan})
		{
			FRHIDeviceDesc DD;
			DD.Backend = Backend;
			DD.bSoftwareAdapter = Backend == ERHIBackend::D3D12;
			auto Device = FRHIDevice::Create(DD);
			REQUIRE(Device);
			const auto R = DrawMaterial(*Device, Parent), V = DrawMaterial(*Device, Resolved),
					   L = DrawMaterial(*Device, LowMaterial);
			const int Center = (32 * 64 + 32) * 4;
			CHECK(R[Center] > 200);
			CHECK(V[Center + 1] > 200);
			CHECK(V[Center] < 5);
			CHECK(L[Center] > 200);
		}
		G.Find(Green)->Value[1] = .5f;
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Parent, Error));
		CHECK_FALSE(FMaterialInstance::Resolve(Parent, Decoded, Resolved, Error));
		CHECK(Error.Contains("stale"));
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Parent, Base, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Base.asset"), Base, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry.Scan());
		int32 Updated = 0;
		REQUIRE_MESSAGE(
			FMaterialEditingLibrary::RecompileDependents(Registry, {Base.Id, "/Game/Base"}, Updated, Error),
			*Error);
		CHECK(Updated == 1);
		REQUIRE(FRenderAssetLoader(Registry).LoadMaterial({Package.Id, "/Game/Instance"}, Resolved, Error));
		CHECK(Resolved.Graph->Find(Green)->Value[1] == .5f);
		auto Invalid = Instance;
		Invalid.StaticPermutation = Parent;
		CHECK_FALSE(FRenderAssetCodec::EncodeMaterialInstance(Invalid, Package, Error));
	}

	TEST_CASE("Material canvas input supports marquee multi-drag wire editing and history at multiple DPIs")
	{
		for (float DPI : {1.f, 1.5f})
		{
			FUITestApp App(DPI);
			auto Material = std::make_shared<FMaterialAsset>();
			Material->Graph = FMaterialGraph{};
			auto& G = *Material->Graph;
			const auto A = G.Add(EMaterialExpression::Constant, 0, 0),
					   B = G.Add(EMaterialExpression::Constant, 300, 0),
					   C = G.Add(EMaterialExpression::Multiply, 600, 0);
			G.Find(C)->Inputs[0] = A;
			G.Find(C)->Inputs[1] = B;
			G.Outputs[0] = C;
			const auto Canvas = SNew(SMaterialGraph).Material(Material);
			const auto Window = App.AddWindow(Canvas, FVector2(1400, 800));
			App.Tick();
			Canvas->FrameAll();
			App.Tick();
			auto Body = [&](uint32 Id) { return Canvas->GetPinPosition(Id, 0, true) - FVector2(60, 15); };
			App.Click(Window, Body(A));
			App.Tick();
			App.SetModifiers(false, true);
			App.Click(Window, Body(B));
			App.Tick();
			App.SetModifiers(false, false);
			REQUIRE(Canvas->GetSelectedNodes().size() == 2);
			const auto AX = G.Find(A)->X, BX = G.Find(B)->X;
			auto Start = Body(A);
			App.MouseDown(Window, Start);
			App.Tick();
			App.MouseMove(Window, Start + FVector2(48, 32));
			App.Tick();
			App.MouseUp(Window, Start + FVector2(48, 32));
			App.Tick();
			CHECK(G.Find(A)->X > AX);
			CHECK(G.Find(A)->X - AX == doctest::Approx(G.Find(B)->X - BX));
			App.Key(EKey::Z, false, true);
			App.Tick();
			CHECK(G.Find(A)->X == AX);
			CHECK(G.Find(B)->X == BX);
			App.Key(EKey::Y, false, true);
			App.Tick();
			CHECK(G.Find(A)->X > AX);
			App.SetModifiers(false, false, true);
			App.Click(Window, Canvas->GetPinPosition(C, 0, false));
			App.Tick();
			App.SetModifiers(false, false);
			CHECK(G.Find(C)->Inputs[0] == 0);
			const auto From = Canvas->GetPinPosition(A, 0, true), To = Canvas->GetPinPosition(C, 0, false);
			App.MouseDown(Window, From);
			App.Tick();
			App.MouseMove(Window, To);
			App.Tick();
			App.MouseUp(Window, To);
			App.Tick();
			CHECK(G.Find(C)->Inputs[0] == A);
			App.MouseDown(Window, FVector2(2, 2));
			App.Tick();
			App.MouseMove(Window, FVector2(1398, 798));
			App.Tick();
			App.MouseUp(Window, FVector2(1398, 798));
			App.Tick();
			CHECK(Canvas->GetSelectedNodes().size() == 3);
			App.Key(EKey::D, false, true);
			App.Tick();
			CHECK(G.Nodes.size() == 6);
			App.Key(EKey::Z, false, true);
			App.Tick();
			CHECK(G.Nodes.size() == 3);
		}
	}
	TEST_CASE("Function and collection drops preserve shared dependencies across clipboard and undo")
	{
		FTempFolder Temp;
		FString Error;
		FUITestApp App;
		const auto Registry = MakeShared<FAssetRegistry>();
		REQUIRE(Registry->SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		FMaterialGraph Function;
		Function.bFunction = true;
		Function.Outputs[0] = Function.Add(EMaterialExpression::FunctionInput);
		FAssetPackage P;
		REQUIRE(FMaterialFunctionCodec::Encode(Function, P, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Function.asset"), P, Error));
		FMaterialParameterCollection Collection;
		Collection.Parameters = {{"Global", 1, {.5f, 0, 0, 0}}};
		FAssetPackage CP;
		REQUIRE(FMaterialParameterCollectionCodec::Encode(Collection, CP, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Collection.asset"), CP, Error));
		REQUIRE(Registry->Scan());
		auto Material = std::make_shared<FMaterialAsset>();
		Material->Graph = FMaterialGraph{};
		const auto Canvas = SNew(SMaterialGraph).Material(Material).Registry(Registry);
		App.AddWindow(Canvas);
		App.Tick();
		REQUIRE_MESSAGE(
			Canvas->DropAsset({P.Id, "/Game/Function"}, "Genesis.MaterialFunction", FVector2(0, 0), Error),
			*Error);
		const auto Call = Canvas->GetSelectedNode();
		Canvas->SetNodeSelection({Call});
		const auto Clipboard = Canvas->CopySelection();
		auto Other = std::make_shared<FMaterialAsset>();
		Other->Graph = FMaterialGraph{};
		const auto Destination = SNew(SMaterialGraph).Material(Other).Registry(Registry);
		REQUIRE_MESSAGE(Destination->PasteNodes(Clipboard, FVector2(100, 100), Error), *Error);
		CHECK(Other->Graph->Functions.size() == 1);
		CHECK(Other->Graph->Nodes[0].Function.Id == P.Id);
		REQUIRE(Canvas->DropAsset({CP.Id, "/Game/Collection"}, "Genesis.MaterialParameterCollection",
								  FVector2(200, 0), Error));
		CHECK(Material->Graph->Nodes.size() == 2);
		Canvas->Undo();
		CHECK(Material->Graph->Nodes.size() == 1);
		Canvas->Redo();
		CHECK(Material->Graph->Nodes.size() == 2);
		const auto Before = FMaterialGraphCompiler::Encode(*Material->Graph);
		CHECK_FALSE(
			Canvas->DropAsset({FAssetId::New(), "/Game/Function"}, "Genesis.MaterialFunction", {}, Error));
		CHECK(FMaterialGraphCompiler::Encode(*Material->Graph) == Before);
	}

	TEST_CASE("Material editor saves only the latest valid revision while live compilation is on or off")
	{
		FTempFolder Temp;
		FString Error;
		FUITestApp App;
		auto Initial = ColorGraph(.1f, .2f, .3f);
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Initial, Error));
		FAssetPackage Package;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Initial, Package, Error));
		const auto File = Temp.File("Editing.asset");
		REQUIRE(FAssetPackageStore::Save(File, Package, Error));
		const auto Registry = MakeShared<FAssetRegistry>();
		REQUIRE(Registry->SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry->Scan());
		FMaterialEditorArgs Args;
		Args.Registry = Registry;
		Args.Material = {Package.Id, "/Game/Editing"};
		Args.Name = "Revision workflow";
		int Saves = 0;
		Args.OnSaved = FSimpleDelegate::CreateLambda([&] { ++Saves; });
		const auto Window = FMaterialEditor::Open(Args, Error);
		REQUIRE(Window);
		App.Tick();
		std::function<SWidget*(SWidget&, const FString&)> FindText = [&](SWidget& W,
																		 const FString& Text) -> SWidget* {
			if (auto* T = dynamic_cast<STextBlock*>(&W); T && T->GetText() == Text)
				return T;
			for (const auto& C : W.GetArrangedChildren())
				if (auto* R = FindText(C.Widget.Get(), Text))
					return R;
			return nullptr;
		};
		const auto Click = [&](const FString& Text) {
			auto* W = FindText(*Window, Text);
			REQUIRE_MESSAGE(W, *Text);
			App.ClickWidget(*W);
			App.Tick();
		};
		const auto Wait = [&](auto Predicate) {
			for (int I = 0; I < 300 && !Predicate(); ++I)
			{
				App.Tick();
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			REQUIRE(Predicate());
		};
		Wait([&] { return FindText(*Window, "Compiled for DirectX 12 and Vulkan") != nullptr; });
		SMaterialGraph* Canvas = nullptr;
		std::function<void(SWidget&)> FindCanvas = [&](SWidget& W) {
			if (auto* C = dynamic_cast<SMaterialGraph*>(&W))
				Canvas = C;
			for (const auto& C : W.GetArrangedChildren())
				FindCanvas(C.Widget.Get());
		};
		FindCanvas(*Window);
		REQUIRE(Canvas);
		Canvas->SetNodeSelection({1});
		App.Tick();
		Click("Preview: Material");
		Wait([&] { return FindText(*Window, "Node preview compiled for DirectX 12 and Vulkan") != nullptr; });
		Click("Save");
		Wait([&] { return Saves == 1; });
		Saves = 0;
		Canvas->EditNode([](auto& N) { N.Value[0] = std::numeric_limits<float>::quiet_NaN(); });
		Click("Save");
		CHECK(Saves == 0);
		FAssetPackage Read;
		FMaterialAsset Material;
		REQUIRE(FAssetPackageStore::Load(File, Read, Error));
		REQUIRE(FRenderAssetCodec::DecodeMaterial(Read, Material, Error));
		CHECK(Material.Graph->Find(1)->Value[0] == .1f);
		Canvas->EditNode([](auto& N) { N.Value[0] = .4f; });
		Click("Compile / Preview");
		Click("Save");
		Canvas->EditNode([](auto& N) { N.Value[0] = .6f; });
		Wait([&] { return Saves == 1; });
		REQUIRE(FAssetPackageStore::Load(File, Read, Error));
		REQUIRE(FRenderAssetCodec::DecodeMaterial(Read, Material, Error));
		CHECK(Material.Graph->Find(1)->Value[0] == .6f);
		Click("Live Preview: On");
		Canvas->EditNode([](auto& N) { N.Value[0] = .7f; });
		Click("Save");
		Canvas->EditNode([](auto& N) { N.Value[0] = .8f; });
		Wait([&] { return Saves == 2; });
		REQUIRE(FAssetPackageStore::Load(File, Read, Error));
		REQUIRE(FRenderAssetCodec::DecodeMaterial(Read, Material, Error));
		CHECK(Material.Graph->Find(1)->Value[0] == .8f);
		CHECK(Read.Id == Package.Id);
		Window->RequestClose();
	}
	TEST_CASE("Dependent recooking updates only affected assets and preserves files on function errors")
	{
		FTempFolder Temp;
		FString Error;
		FMaterialGraph Function;
		Function.bFunction = true;
		Function.Outputs[0] = Function.Add(EMaterialExpression::FunctionInput);
		Function.Find(Function.Outputs[0])->Value[0] = .25f;
		FAssetPackage Source;
		REQUIRE(FMaterialFunctionCodec::Encode(Function, Source, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Function.asset"), Source, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry.Scan());
		FMaterialAsset Material;
		Material.Graph = FMaterialGraph{};
		const auto C = Material.Graph->Add(EMaterialExpression::Constant);
		Material.Graph->Find(C)->Value[0] = .2f;
		const auto F = Material.Graph->Add(EMaterialExpression::MaterialFunctionCall);
		Material.Graph->Find(F)->Inputs[0] = C;
		Material.Graph->Find(F)->Function = {Source.Id, "/Game/Function"};
		Material.Graph->Outputs[0] = F;
		bool Changed = false;
		REQUIRE(FMaterialFunctionCodec::Refresh(*Material.Graph, Registry, Changed, Error));
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Material, Error));
		FAssetPackage Target;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Material, Target, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Target.asset"), Target, Error));
		auto Unrelated = ColorGraph(1, 0, 0);
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Unrelated, Error));
		FAssetPackage Other;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Unrelated, Other, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Unrelated.asset"), Other, Error));
		FString Original;
		REQUIRE(FFile::ReadAllText(Temp.File("Unrelated.asset"), Original));
		Function.Find(Function.Outputs[0])->Value[0] = .75f;
		REQUIRE(FMaterialFunctionCodec::Encode(Function, Source, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Function.asset"), Source, Error));
		REQUIRE(Registry.Scan());
		int32 Updated = 0;
		REQUIRE(FMaterialEditingLibrary::RecompileDependents(Registry, {Source.Id, "/Game/Function"}, Updated,
															 Error));
		CHECK(Updated == 1);
		FString Still;
		REQUIRE(FFile::ReadAllText(Temp.File("Unrelated.asset"), Still));
		CHECK(Still == Original);
		FString Before;
		REQUIRE(FFile::ReadAllText(Temp.File("Target.asset"), Before));
		Function.Find(Function.Outputs[0])->Mask = 3;
		REQUIRE(FMaterialFunctionCodec::Encode(Function, Source, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Function.asset"), Source, Error));
		REQUIRE(Registry.Scan());
		CHECK_FALSE(FMaterialEditingLibrary::RecompileDependents(Registry, {Source.Id, "/Game/Function"},
																 Updated, Error));
		CHECK(Updated == 0);
		CHECK(Error.Contains("type mismatch"));
		REQUIRE(FFile::ReadAllText(Temp.File("Target.asset"), Still));
		CHECK(Still == Before);
	}

	TEST_CASE("Nested material functions recook transitively and reject indirect recursion")
	{
		FTempFolder Temp;
		FString Error;
		FMaterialGraph Inner;
		Inner.bFunction = true;
		Inner.Outputs[0] = Inner.Add(EMaterialExpression::Constant);
		Inner.Find(Inner.Outputs[0])->Value[0] = .2f;
		FAssetPackage A;
		REQUIRE(FMaterialFunctionCodec::Encode(Inner, A, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Inner.asset"), A, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry.Scan());
		FMaterialGraph Outer;
		Outer.bFunction = true;
		Outer.Outputs[0] = Outer.Add(EMaterialExpression::MaterialFunctionCall);
		Outer.Find(Outer.Outputs[0])->Function = {A.Id, "/Game/Inner"};
		bool Changed = false;
		REQUIRE(FMaterialFunctionCodec::Refresh(Outer, Registry, Changed, Error));
		FAssetPackage B;
		REQUIRE(FMaterialFunctionCodec::Encode(Outer, B, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Outer.asset"), B, Error));
		REQUIRE(Registry.Scan());
		FMaterialAsset Material;
		Material.Graph = FMaterialGraph{};
		Material.Graph->Outputs[0] = Material.Graph->Add(EMaterialExpression::MaterialFunctionCall);
		Material.Graph->Find(Material.Graph->Outputs[0])->Function = {B.Id, "/Game/Outer"};
		REQUIRE(FMaterialFunctionCodec::Refresh(*Material.Graph, Registry, Changed, Error));
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Material, Error));
		const auto OldSource = Material.CookedGraphSource;
		FAssetPackage P;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Material, P, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Material.asset"), P, Error));
		Inner.Find(Inner.Outputs[0])->Value[0] = .8f;
		REQUIRE(FMaterialFunctionCodec::Encode(Inner, A, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Inner.asset"), A, Error));
		REQUIRE(Registry.Scan());
		int32 Count = 0;
		REQUIRE_MESSAGE(
			FMaterialEditingLibrary::RecompileDependents(Registry, {A.Id, "/Game/Inner"}, Count, Error),
			*Error);
		CHECK(Count == 2);
		REQUIRE(FRenderAssetLoader(Registry).LoadMaterial({P.Id, "/Game/Material"}, Material, Error));
		CHECK(Material.CookedGraphSource != OldSource);
		auto Recursive = Inner;
		Recursive.Nodes.clear();
		Recursive.Outputs[0] = Recursive.Add(EMaterialExpression::MaterialFunctionCall);
		Recursive.Find(Recursive.Outputs[0])->Function = {B.Id, "/Game/Outer"};
		const auto Before = FMaterialGraphCompiler::Encode(Recursive);
		CHECK_FALSE(FMaterialFunctionCodec::Refresh(Recursive, Registry, Changed, Error, A.Id));
		CHECK(Error.Contains("recursion"));
		CHECK(FMaterialGraphCompiler::Encode(Recursive) == Before);
	}
	TEST_CASE("Parameter grouping preserves shader compatibility and material windows preserve one draft")
	{
		FTempFolder Temp;
		FString Error;
		auto Material = ColorGraph(1, 1, 1);
		const auto P = Material.Graph->Add(EMaterialExpression::ScalarParameter);
		Material.Graph->Find(P)->ParameterName = "Roughness";
		Material.Graph->Find(P)->ParameterGroup = "Surface";
		Material.Graph->Outputs[1] = P;
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Material, Error));
		const auto Shader = Material.GraphDXIL;
		const auto Source = Material.CookedGraphSource;
		Material.Graph->Find(P)->ParameterGroup = "Surface Controls";
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Material, Error));
		CHECK(Material.GraphDXIL == Shader);
		CHECK(Material.CookedGraphSource == Source);
		FAssetPackage Package;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Material, Package, Error));
		REQUIRE(FAssetPackageStore::Save(Temp.File("Grouped.asset"), Package, Error));
		FMaterialAsset Loaded;
		REQUIRE(FRenderAssetCodec::DecodeMaterial(Package, Loaded, Error));
		CHECK(Loaded.Graph->Find(P)->ParameterGroup == "Surface Controls");
		FUITestApp App;
		const auto Registry = MakeShared<FAssetRegistry>();
		REQUIRE(Registry->SetMounts({{"/Game", FString::FromWide(Temp.Path.wstring())}}, Error));
		REQUIRE(Registry->Scan());
		FMaterialEditorArgs Args;
		Args.Registry = Registry;
		Args.Material = {Package.Id, "/Game/Grouped"};
		Args.Name = "Grouped";
		auto First = FMaterialEditor::Open(Args, Error), Second = FMaterialEditor::Open(Args, Error);
		REQUIRE(First);
		CHECK(First == Second);
		First->RequestClose();
		App.Tick();
		auto Reopened = FMaterialEditor::Open(Args, Error);
		REQUIRE(Reopened);
		CHECK(Reopened != First);
		Reopened->RequestClose();
	}

	TEST_CASE("Material attribute layers compose six channels through reusable typed functions")
	{
		FMaterialAsset Material;
		Material.Graph = FMaterialGraph{};
		auto& G = *Material.Graph;
		G.bUseMaterialAttributes = true;
		const auto Red = G.Add(EMaterialExpression::Constant3Vector),
				   Blue = G.Add(EMaterialExpression::Constant3Vector);
		G.Find(Red)->Value[0] = 1;
		G.Find(Blue)->Value[2] = 1;
		const auto A = G.Add(EMaterialExpression::MakeMaterialAttributes),
				   B = G.Add(EMaterialExpression::MakeMaterialAttributes);
		G.Find(A)->Inputs[0] = Red;
		G.Find(B)->Inputs[0] = Blue;
		const auto Blend = G.Add(EMaterialExpression::BlendMaterialAttributes);
		G.Find(Blend)->Inputs[0] = A;
		G.Find(Blend)->Inputs[1] = B;
		G.Find(Blend)->Value[2] = .25f;
		FMaterialGraph Function;
		Function.bFunction = true;
		const auto Input = Function.Add(EMaterialExpression::FunctionInput);
		Function.Find(Input)->Mask = 8;
		Function.Outputs[0] = Input;
		FAssetReference Ref{FAssetId::New(), "/Game/Layer"};
		G.Functions.push_back({Ref, std::make_shared<const FMaterialGraph>(Function)});
		const auto Call = G.Add(EMaterialExpression::MaterialFunctionCall);
		G.Find(Call)->Function = Ref;
		G.Find(Call)->Inputs[0] = Blend;
		G.Outputs[0] = Call;
		FString Error;
		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(Material, Error), *Error);
		auto Flat = ColorGraph(.75f, 0, .25f);
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Flat, Error));
		for (auto Backend : {ERHIBackend::D3D12, ERHIBackend::Vulkan})
		{
			FRHIDeviceDesc DD;
			DD.Backend = Backend;
			DD.bSoftwareAdapter = Backend == ERHIBackend::D3D12;
			auto Device = FRHIDevice::Create(DD);
			REQUIRE(Device);
			const auto Actual = DrawMaterial(*Device, Material), Expected = DrawMaterial(*Device, Flat);
			CHECK(Actual == Expected);
		}
		FAssetPackage Package;
		REQUIRE(FRenderAssetCodec::EncodeMaterial(Material, Package, Error));
		FMaterialAsset Loaded;
		REQUIRE(FRenderAssetCodec::DecodeMaterial(Package, Loaded, Error));
		CHECK(Loaded.Graph->bUseMaterialAttributes);
		CHECK(Loaded.Graph->Find(Call)->Function.Id == Ref.Id);
		auto Broken = G;
		Broken.Find(Call)->Inputs[0] = Red;
		FMaterialGraphProgram Program;
		CHECK_FALSE(FMaterialGraphCompiler::Compile(Broken, Program, Error));
		CHECK(Error.Contains("Function input type mismatch"));
		CHECK(Error.Contains(FString::Format("Node {}:", Call).View()));
		auto Break = Material;
		Break.Graph->bUseMaterialAttributes = false;
		const auto Field = Break.Graph->Add(EMaterialExpression::BreakMaterialAttributes);
		Break.Graph->Find(Field)->Inputs[0] = Call;
		Break.Graph->Find(Field)->Mask = 0;
		Break.Graph->Outputs[0] = Field;
		REQUIRE(FMaterialEditingLibrary::CompileMaterial(Break, Error));
		auto View = std::make_shared<FMaterialAsset>(Material);
		const auto Canvas = SNew(SMaterialGraph).Material(View);
		Canvas->SetUseMaterialAttributes(false);
		CHECK_FALSE(View->Graph->bUseMaterialAttributes);
		Canvas->Undo();
		CHECK(View->Graph->bUseMaterialAttributes);
		Canvas->Redo();
		CHECK_FALSE(View->Graph->bUseMaterialAttributes);
	}

	TEST_CASE("Function interfaces expose named sparse inputs and settings participate in graph undo")
	{
		FMaterialGraph Function;
		Function.bFunction = true;
		const auto Input = Function.Add(EMaterialExpression::FunctionInput);
		Function.Find(Input)->InputIndex = 7;
		Function.Find(Input)->ParameterName = "Layer Color";
		Function.Find(Input)->Mask = 3;
		Function.Outputs[0] = Input;
		FAssetReference Ref{FAssetId::New(), "/Game/Function"};
		auto Material = std::make_shared<FMaterialAsset>();
		Material->Graph = FMaterialGraph{};
		auto& G = *Material->Graph;
		G.Functions.push_back({Ref, std::make_shared<const FMaterialGraph>(Function)});
		const auto Color = G.Add(EMaterialExpression::Constant3Vector);
		G.Find(Color)->Value[1] = 1;
		const auto Call = G.Add(EMaterialExpression::MaterialFunctionCall);
		G.Find(Call)->Function = Ref;
		G.Find(Call)->Inputs[7] = Color;
		G.Outputs[0] = Call;
		CHECK(MaterialNodeInputCount(G, *G.Find(Call)) == 8);
		CHECK(MaterialNodeHasInput(G, *G.Find(Call), 7));
		CHECK_FALSE(MaterialNodeHasInput(G, *G.Find(Call), 0));
		CHECK(MaterialNodeInputName(G, *G.Find(Call), 7) == "Layer Color");
		FString Error;
		REQUIRE_MESSAGE(FMaterialEditingLibrary::CompileMaterial(*Material, Error), *Error);
		auto Bad = G;
		Bad.Find(Call)->Inputs[0] = Color;
		FMaterialGraphProgram Program;
		CHECK_FALSE(FMaterialGraphCompiler::Compile(Bad, Program, Error));
		CHECK(Error.Contains("undeclared input"));
		const auto Canvas = SNew(SMaterialGraph).Material(Material);
		Canvas->SetMaterialSettings(EMaterialBlendMode::Masked, .6f);
		CHECK(Material->BlendMode == EMaterialBlendMode::Masked);
		CHECK(Material->OpacityMaskClip == .6f);
		Canvas->Undo();
		CHECK(Material->BlendMode == EMaterialBlendMode::Opaque);
		CHECK(Material->OpacityMaskClip == .333f);
		Canvas->Redo();
		CHECK(Material->BlendMode == EMaterialBlendMode::Masked);
		CHECK(Material->OpacityMaskClip == .6f);
	}
}
