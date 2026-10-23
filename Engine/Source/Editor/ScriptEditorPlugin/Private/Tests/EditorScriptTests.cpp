// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "EditorScriptBindings.h"
#include "EditorScriptExecution.h"
#include "Landscape/Landscape.h"
#include "Foliage/Foliage.h"
#include "Framework/Commands/UICommands.h"
#include <filesystem>
#include "MapCoreHeaders.h"
#include "MapDocument.h"
#include "Scripting/LuaScriptRuntime.h"
#include "Scripting/PythonScriptRuntime.h"
#include "Scripting/DotNetScriptRuntime.h"
#include "HAL/File.h"
#include "MapAssetLibrary.h"
#include "Renderer/NativeMeshRenderer.h"
namespace
{
using J = FJsonValue;
void Startup()
{
	FMapEditorEnvironment::FStartupOptions Options;
	Options.EditorContentDir = FPaths::Combine(FPaths::FindContentDir(), "Editor");
	if (!FMapEditorEnvironment::IsStarted())
		REQUIRE(FMapEditorEnvironment::Startup(Options));
	REQUIRE(FMapEditorEnvironment::Activate(FMapEditorEnvironment::GetBuiltInGameSet()));
}

TArray<uint8> RenderScriptMaterial(FRHIDevice& Device, std::shared_ptr<const FLoadedMeshAsset> Asset)
{
	FString Error;
	FNativeMeshRenderer Renderer(Device);
	FNativeMeshDrawList List;
	Matrix44 Transform;
	Transform.SetTranslation(Vector3d(0, 220, 0));
	REQUIRE(List.Append(Asset, Transform, Error));
	FRHITextureDesc TD;
	TD.Width = TD.Height = 64;
	TD.bRenderTarget = true;
	auto Target = Device.CreateTexture(TD);
	TD.Format = EPixelFormat::D32_FLOAT;
	auto Depth = Device.CreateTexture(TD);
	REQUIRE(Target);
	REQUIRE(Depth);
	auto& Cmd = Device.BeginFrame();
	auto Frame = Renderer.Prepare(List, Target->GetFormat(), Error);
	REQUIRE_MESSAGE(Frame, *Error);
	Cmd.BeginRenderPass(*Target, *Depth, FLinearColor(0, 0, 0, 1), 1.f);
	FNativeMeshView View;
	View.Width = View.Height = 64;
	View.ProjectionScale = 64;
	View.bLit = false;
	Renderer.Draw(Cmd, *Frame, View, false);
	Cmd.EndRenderPass();
	Device.EndFrame();
	TArray<uint8> Pixels;
	REQUIRE(Device.ReadbackTexture(*Target, Pixels));
	Device.WaitIdle();
	return Pixels;
}
J Args(const char* Text)
{
	auto A = J::Parse(Text);
	REQUIRE(A);
	return *A;
}
} // namespace
TEST_SUITE("EditorScripting")
{
	TEST_CASE("All language providers edit real editor documents with undo and rollback")
	{
		Startup();
		FScriptRuntimeRegistry Providers;
		FString Error;
		REQUIRE(RegisterLuaScriptProvider(Providers, Error));
		REQUIRE(RegisterPythonScriptProvider(Providers, Error));
		REQUIRE(RegisterDotNetScriptProvider(Providers, Error));
		for (const FString Language : {FString("Lua"), FString("Python"), FString("DotNet")})
		{
			CAPTURE(Language);
			auto Doc = FMapDocument::CreateNew();
			REQUIRE(Doc);
			auto Context = std::make_shared<FEditorScriptContext>();
			Context->GetDocument = [&] { return Doc.get(); };
			auto Host = std::make_shared<FScriptBindingRegistry>(
				EScriptPurpose::Editor, TArray<FString>{"Editor.Read", "Editor.Write"});
			REQUIRE(RegisterEditorScriptBindings(*Host, Context, Error));
			auto Runtime = Providers.Create({Language, "Win64", EScriptPurpose::Editor}, Host, Error);
			REQUIRE_MESSAGE(Runtime, *Error);
			const auto Source = Language == "Lua" ? "../../../Samples/Scripting/Lua/Source/Main.lua"
								: Language == "Python"
									? "../../../Samples/Scripting/Python/Source/Main.py"
									: "../ThirdParty/DotNet/Examples/Genesis.ScriptExample.dll";
			TArray<uint8> Payload;
			REQUIRE(FFile::ReadAllBytes(FPaths::Combine(FPaths::ExecutableDir(), Source), Payload));
			REQUIRE_MESSAGE(Runtime->LoadModule("main", Payload, Error), *Error);
			J Result;
			const auto Invoke = [&](const char* API, const J& A) {
				auto Wrapper = J::MakeObject();
				Wrapper["API"] = API;
				Wrapper["Arguments"] = A;
				return Runtime->Call("main", "Invoke", Wrapper, Result, Error);
			};
			const auto Empty = J::MakeObject();
			REQUIRE(Invoke("Editor.BeginTransaction", Args(R"({"Description":"Build terrain"})")));
			REQUIRE_MESSAGE(Invoke("Landscape.Create",
								   Args(R"({"QuadsX":8,"QuadsY":8,"QuadSize":32,"Position":[0,0,0]})")),
							*Error);
			auto Id = Result.Get("ActorId").AsString();
			REQUIRE(!Id.IsEmpty());
			auto Edit = Args(R"({"Offset":0,"Heights":[10,20,30]})");
			Edit["ActorId"] = Id;
			REQUIRE(Invoke("Landscape.SetHeights", Edit));
			REQUIRE(Invoke("Editor.EndTransaction", Empty));
			CHECK(Doc->GetObjects().size() == 1);
			REQUIRE(Invoke("Editor.Undo", Empty));
			CHECK(Doc->GetObjects().empty());
			REQUIRE(Invoke("Editor.Redo", Empty));
			CHECK(Doc->GetObjects().size() == 1);
			auto Query = Args(R"({"Offset":0,"Count":3})");
			Query["ActorId"] = Id;
			REQUIRE(Invoke("Landscape.GetHeights", Query));
			CHECK(Result[1].AsInt() == 20);
			REQUIRE(Invoke("Editor.BeginTransaction", Args(R"({"Description":"Rollback"})")));
			Edit["Heights"] = Args("[99,98,97]");
			REQUIRE(Invoke("Landscape.SetHeights", Edit));
			Edit["Offset"] = 80; // last grid vertex: this update must fail atomically
			CHECK_FALSE(Invoke("Landscape.SetHeights", Edit));
			CHECK_FALSE(Doc->IsInTransaction());
			REQUIRE(Invoke("Landscape.GetHeights", Query));
			CHECK(Result[0].AsInt() == 10);
			Doc->BeginTransaction("UI owns this");
			const auto Transaction = Doc->GetActiveTransactionId();
			CHECK_FALSE(Invoke("Editor.SetName", Args(R"({"ActorId":"missing","Name":"Bad"})")));
			CHECK(Doc->GetActiveTransactionId() == Transaction);
			Doc->CancelTransaction();
			REQUIRE(Invoke("Editor.BeginTransaction", Args(R"({"Description":"Shutdown rollback"})")));
			Edit["Offset"] = 0;
			REQUIRE(Invoke("Landscape.SetHeights", Edit));
			CancelEditorScriptTransaction(*Context);
			REQUIRE(Invoke("Landscape.GetHeights", Query));
			CHECK(Result[0].AsInt() == 10);
			Doc.reset();
			CHECK_FALSE(Invoke("Editor.GetDocument", Empty));
			CHECK(Error.Contains("expired"));
		}
	}
	TEST_CASE("Editor capabilities and context lifetime are enforced")
	{
		Startup();
		auto Doc = FMapDocument::CreateNew();
		auto Context = std::make_shared<FEditorScriptContext>();
		Context->GetDocument = [&] { return Doc.get(); };
		FString Error;
		J Result;
		const auto Empty = J::MakeObject();
		FScriptBindingRegistry Gameplay(EScriptPurpose::Gameplay, {"Editor.Read", "Editor.Write"});
		REQUIRE(RegisterEditorScriptBindings(Gameplay, Context, Error));
		CHECK_FALSE(Gameplay.Invoke("Editor.GetDocument", Empty, Result, Error));
		FScriptBindingRegistry ReadOnly(EScriptPurpose::Editor, {"Editor.Read"});
		REQUIRE(RegisterEditorScriptBindings(ReadOnly, Context, Error));
		REQUIRE(ReadOnly.Invoke("Editor.GetDocument", Empty, Result, Error));
		CHECK_FALSE(ReadOnly.Invoke("Landscape.Create",
									Args(R"({"QuadsX":8,"QuadsY":8,"QuadSize":32,"Position":[0,0,0]})"),
									Result, Error));
		CHECK(Doc->GetObjects().empty());
		Context.reset();
		CHECK_FALSE(ReadOnly.Invoke("Editor.GetDocument", Empty, Result, Error));
		CHECK(Error.Contains("expired"));
	}

	TEST_CASE("Terrain feature modules run through the public editor entry point and preserve native content")
	{
		Startup();
		for (const FString Language : {FString("Lua"), FString("Python"), FString("DotNet")})
		{
			auto Doc = FMapDocument::CreateNew();
			auto Context = std::make_shared<FEditorScriptContext>();
			Context->GetDocument = [&] { return Doc.get(); };
			FEditorScriptRequest Request;
			Request.Language = Language;
			Request.Function = "BuildTerrain";
			Request.Capabilities.push_back("Editor.Files");
			const auto Source = Language == "Lua" ? "../../../Samples/Scripting/Lua/Source/Main.lua"
								: Language == "Python"
									? "../../../Samples/Scripting/Python/Source/Main.py"
									: "../ThirdParty/DotNet/Examples/Genesis.ScriptExample.dll";
			Request.ModulePath = FPaths::Combine(FPaths::ExecutableDir(), Source);
			const auto Path = FPaths::Combine(
				FPaths::ExecutableDir(), FString::Format("../../Saved/Scripting/Terrain_{}.gmap", Language));
			std::filesystem::create_directories(std::filesystem::path(Path.ToWide()).parent_path());
			Request.Arguments["Output"] = Path;
			FString Error;
			J Result;
			REQUIRE_MESSAGE(ExecuteEditorScript(Request, Context, Result, Error), *Error);
			CHECK_FALSE(Doc->IsInTransaction());
			CHECK_FALSE(Doc->IsModified());
			CHECK(Doc->GetObjects().size() == 2);
			auto Reloaded = FMapDocument::Open(Path, Error);
			REQUIRE_MESSAGE(Reloaded, *Error);
			CHECK(Reloaded->GetObjects().size() == 2);
			int Lands = 0, Foliages = 0;
			for (auto* O : Reloaded->GetObjects())
			{
				if (O->HasLandscape())
				{
					++Lands;
					CHECK(O->GetIdentity().Actor == Result.Get("Landscape").AsString());
					CHECK(O->GetLandscape().GetMaxHeight() > 0);
					REQUIRE(O->GetLandscape().GetNumLayers() == 1);
					CHECK(O->GetLandscape().GetLayers()[0].weights[16 * 33 + 16] > 0);
				}
				if (O->HasFoliage())
				{
					++Foliages;
					CHECK(O->GetIdentity().Actor == Result.Get("Foliage").AsString());
					CHECK(O->GetFoliage().GetNumInstances() == 1);
				}
			}
			CHECK(Lands == 1);
			CHECK(Foliages == 1);
			Doc->Undo();
			CHECK(Doc->GetObjects().empty());
			Doc->Redo();
			CHECK(Doc->GetObjects().size() == 2);
		}
	}

	TEST_CASE("Command automation preserves mapping, visibility, capabilities and transaction boundaries")
	{
		Startup();
		auto Doc = FMapDocument::CreateNew();
		auto Context = std::make_shared<FEditorScriptContext>();
		Context->GetDocument = [&] { return Doc.get(); };
		auto Commands = MakeShared<FUICommandList>();
		Context->GetCommands = [&] { return Commands; };
		auto Binding = MakeShared<FBindingContext>("ScriptTest", "Script commands");
		auto Command = Binding->AddCommand("Run", "Run", "Test run");
		FInputBindingManager::Get().RegisterContext(Binding);
		struct FUnregister
		{
			~FUnregister()
			{
				FInputBindingManager::Get().UnregisterContext("ScriptTest");
			}
		} Unregister;
		bool Enabled = false;
		int Calls = 0;
		Commands->MapAction(Command, [&] { ++Calls; }, [&] { return Enabled; });
		FScriptBindingRegistry Host(EScriptPurpose::Editor,
									{"Editor.Read", "Editor.Write", "Editor.Commands"});
		FString Error;
		J Result;
		REQUIRE(RegisterEditorScriptBindings(Host, Context, Error));
		const auto Empty = J::MakeObject();
		const auto A = Args(R"({"Context":"ScriptTest","Name":"Run"})");
		REQUIRE(Host.Invoke("Editor.GetCommands", Empty, Result, Error));
		CHECK(Result.Num() == 1);
		CHECK_FALSE(Result[0].Get("CanExecute").AsBool());
		CHECK_FALSE(Host.Invoke("Editor.ExecuteCommand", A, Result, Error));
		CHECK(Calls == 0);
		Enabled = true;
		REQUIRE(Host.Invoke("Editor.ExecuteCommand", A, Result, Error));
		CHECK(Calls == 1);
		REQUIRE(Host.Invoke("Editor.BeginTransaction", Args(R"({"Description":"Batch"})"), Result, Error));
		CHECK_FALSE(Host.Invoke("Editor.ExecuteCommand", A, Result, Error));
		CHECK(Calls == 1);
		CancelEditorScriptTransaction(*Context);
		Commands->UnmapAction(Command);
		CHECK_FALSE(Host.Invoke("Editor.ExecuteCommand", A, Result, Error));
		CHECK(Calls == 1);
	}
	TEST_CASE("Public execution rolls back an unfinished script transaction")
	{
		Startup();
		auto Doc = FMapDocument::CreateNew();
		auto Context = std::make_shared<FEditorScriptContext>();
		Context->GetDocument = [&] { return Doc.get(); };
		const auto Path = FPaths::Combine(FPaths::ExecutableDir(), "../../Saved/Scripting/Unfinished.lua");
		std::filesystem::create_directories(std::filesystem::path(Path.ToWide()).parent_path());
		REQUIRE(FFile::WriteAllText(
			Path,
			R"(return {Main=function(args) Genesis.invoke('Editor.BeginTransaction',{Description='Unfinished'});Genesis.invoke('Landscape.Create',{QuadsX=8,QuadsY=8,QuadSize=32,Position={0,0,0}});return true end})"));
		FEditorScriptRequest Request;
		Request.Language = "Lua";
		Request.ModulePath = Path;
		FString Error;
		J Result = "sentinel";
		CHECK_FALSE(ExecuteEditorScript(Request, Context, Result, Error));
		CHECK(Error.Contains("unfinished"));
		CHECK(Result.AsString() == "sentinel");
		CHECK_FALSE(Doc->IsInTransaction());
		CHECK(Doc->GetObjects().empty());
		CHECK_FALSE(Doc->CanUndo());
	}

	TEST_CASE("Every language can author translate cook and reload graph materials through editor execution")
	{
		Startup();
		for (const FString Language : {FString("Lua"), FString("Python"), FString("DotNet")})
		{
			auto Doc = FMapDocument::CreateNew();
			auto Context = std::make_shared<FEditorScriptContext>();
			Context->GetDocument = [&] { return Doc.get(); };
			FEditorScriptRequest Request;
			Request.Language = Language;
			Request.Function = "Invoke";
			Request.Capabilities.push_back("Editor.Files");
			const auto Source = Language == "Lua" ? "../../../Samples/Scripting/Lua/Source/Main.lua"
								: Language == "Python"
									? "../../../Samples/Scripting/Python/Source/Main.py"
									: "../ThirdParty/DotNet/Examples/Genesis.ScriptExample.dll";
			Request.ModulePath = FPaths::Combine(FPaths::ExecutableDir(), Source);
			FString Error;
			J Result;
			const auto Invoke = [&](const char* API, const J& A) {
				Request.Arguments = J::MakeObject();
				Request.Arguments["API"] = API;
				Request.Arguments["Arguments"] = A;
				return ExecuteEditorScript(Request, Context, Result, Error);
			};
			REQUIRE_MESSAGE(Invoke("Material.Describe", J::MakeObject()), *Error);
			CHECK(Result.Get("Outputs").Num() == 6);
			int VectorParameter = -1;
			for (const auto& Expression : Result.Get("Expressions").GetItems())
				if (Expression.Get("Name").AsString() == "VectorParameter")
					VectorParameter = Expression.Get("Id").AsInt();
			REQUIRE(VectorParameter >= 0);
			REQUIRE(Invoke("Material.CreateGraph", J::MakeObject()));
			auto Graph = Result.Get("Graph");
			auto Settings = Result.Get("Settings");
			Graph["Nodes"][0]["Expression"] = VectorParameter;
			Graph["Nodes"][0]["ParameterName"] = "Tint";
			auto A = J::MakeObject();
			A["Graph"] = Graph;
			REQUIRE_MESSAGE(Invoke("Material.Translate", A), *Error);
			REQUIRE(Result.Get("Parameters").Num() == 1);
			CHECK(Result.Get("Parameters")[0].Get("Name").AsString() == "Tint");
			const auto Path =
				FPaths::Combine(FPaths::ExecutableDir(),
								FString::Format("../../Saved/Scripting/Material_{}.asset", Language));
			A["Path"] = Path;
			A["Settings"] = Settings;
			REQUIRE_MESSAGE(Invoke("Material.CookGraph", A), *Error);
			const auto Id = Result.Get("Id").AsString();
			CHECK(Result.Get("DXILBytes").AsInt() > 0);
			CHECK(Result.Get("SPIRVBytes").AsInt() > 0);
			auto Read = J::MakeObject();
			Read["Path"] = Path;
			REQUIRE(Invoke("Material.LoadGraph", Read));
			CHECK(Result.Get("Graph") == Graph);
			CHECK(Result.Get("Id").AsString() == Id);
			auto Bad = A;
			Bad["Graph"]["Outputs"][0] = 999999;
			CHECK_FALSE(Invoke("Material.CookGraph", Bad));
			REQUIRE(Invoke("Material.LoadGraph", Read));
			CHECK(Result.Get("Graph") == Graph);
			CHECK(Result.Get("Id").AsString() == Id);
			REQUIRE(Invoke("Material.CookGraph", A));
			CHECK(Result.Get("Id").AsString() == Id);
		}
	}

	TEST_CASE("Material authoring projects execute in every language save native scenes and render both "
			  "permutations")
	{
		Startup();
		const auto Previous = FMapAssetLibrary::GetRegistry();
		struct FRestore
		{
			std::shared_ptr<FAssetRegistry> Previous;
			~FRestore()
			{
				FMapAssetLibrary::SetRegistry(Previous);
			}
		} Restore{Previous};
		for (const FString Language : {FString("Lua"), FString("Python"), FString("DotNet")})
		{
			FString Error;
			const auto Folder = FPaths::Combine(
				FPaths::ExecutableDir(), FString::Format("../../Saved/MaterialAuthoring/{}", Language));
			std::filesystem::create_directories(std::filesystem::path(Folder.ToWide()));
			auto Registry = std::make_shared<FAssetRegistry>();
			REQUIRE(Registry->SetMounts({{"/Game", Folder}}, Error));
			REQUIRE(Registry->Scan());
			FMapAssetLibrary::SetRegistry(Registry);
			auto Doc = FMapDocument::CreateNew();
			auto Context = std::make_shared<FEditorScriptContext>();
			Context->GetDocument = [&] { return Doc.get(); };
			Context->Assets = Registry;
			FEditorScriptRequest Request;
			Request.Language = Language;
			Request.Function = "RunScenario";
			Request.Capabilities.push_back("Editor.Files");
			const auto Source = Language == "Lua" ? "../../../Samples/Scripting/Lua/Source/Main.lua"
								: Language == "Python"
									? "../../../Samples/Scripting/Python/Source/Main.py"
									: "../ThirdParty/DotNet/Examples/Genesis.ScriptExample.dll";
			Request.ModulePath = FPaths::Combine(FPaths::ExecutableDir(), Source);
			FString Text;
			REQUIRE(FFile::ReadAllText(
				FPaths::Combine(FPaths::ExecutableDir(),
								"../../../Samples/Features/MaterialAuthoring/Source/Scenario.json"),
				Text));
			auto Scenario = FJsonValue::Parse(Text.View(), &Error);
			REQUIRE(Scenario);
			Request.Arguments = *Scenario;
			Request.Arguments["Content"] = Folder;
			const auto Map = FPaths::Combine(Folder, "Main.gmap");
			Request.Arguments["Map"] = Map;
			FJsonValue Result;
			REQUIRE_MESSAGE(ExecuteEditorScript(Request, Context, Result, Error), *Error);
			CHECK(Result.Get("Complete").AsBool());
			CHECK(Doc->GetObjects().size() == 2);
			CHECK_FALSE(Doc->IsInTransaction());
			Doc->Undo();
			CHECK(Doc->GetObjects().empty());
			Doc->Redo();
			CHECK(Doc->GetObjects().size() == 2);
			auto Reloaded = FMapDocument::Open(Map, Error);
			REQUIRE_MESSAGE(Reloaded, *Error);
			REQUIRE(Reloaded->GetObjects().size() == 2);
			auto Master = Reloaded->GetObjects()[0]->GetMeshAsset(),
				 Instance = Reloaded->GetObjects()[1]->GetMeshAsset();
			REQUIRE(Master);
			REQUIRE(Instance);
			REQUIRE(Master->Material);
			REQUIRE(Instance->Material);
			CHECK(Master->Material->Graph->Functions.size() == 2);
			CHECK(Master->GraphTextures.size() == 1);
			CHECK(Instance->GraphTextures.empty());
			CHECK(Master->Material->GraphDXIL != Instance->Material->GraphDXIL);
			for (auto Backend : {ERHIBackend::D3D12, ERHIBackend::Vulkan})
			{
				FRHIDeviceDesc DD;
				DD.Backend = Backend;
				DD.bSoftwareAdapter = Backend == ERHIBackend::D3D12;
				auto Device = FRHIDevice::Create(DD);
				REQUIRE(Device);
				const auto A = RenderScriptMaterial(*Device, Master),
						   B = RenderScriptMaterial(*Device, Instance);
				const int Center = (32 * 64 + 32) * 4;
				CHECK(A[Center + 2] > A[Center + 1]);
				CHECK(A[Center + 1] > A[Center]);
				CHECK(B[Center] > 200);
				CHECK(B[Center + 1] < 5);
				CHECK(B[Center + 2] < 5);
			}
		}
	}
}
