// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "ProjectDescriptor.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/LuaScriptRuntime.h"
#include "Scripting/ScriptBindings.h"
#include "Scripting/PythonScriptRuntime.h"
#include "Scripting/DotNetScriptRuntime.h"
#include "Scripting/WorldScriptBindings.h"
#include "World/World.h"
#include "Scripting/AssetScriptBindings.h"
#include "Scripting/ScriptWorldSubsystem.h"
#include <filesystem>
#include "HAL/File.h"
#include "HAL/PlatformMisc.h"
#include <thread>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace
{
	class FTestScriptHost : public IScriptHost
	{
	public:
		bool Invoke(const FString&, const FJsonValue& A, FJsonValue& R, FString& E) override
		{
			R = A;
			E.Empty();
			return true;
		}
	};
	class FTestScriptRuntime : public IScriptRuntime
	{
	public:
		bool LoadModule(const FString&, TArrayView<const uint8>, FString& E) override
		{
			E.Empty();
			return true;
		}
		bool Call(const FString&, const FString&, const FJsonValue& A, FJsonValue& R, FString& E) override
		{
			R = A;
			E.Empty();
			return true;
		}
	};
} // namespace
TEST_SUITE("Scripting")
{
	TEST_CASE("All three languages execute the live world API through stable IDs")
	{
		FScriptRuntimeRegistry Providers;
		FString Error;
		REQUIRE(RegisterLuaScriptProvider(Providers, Error));
		REQUIRE(RegisterPythonScriptProvider(Providers, Error));
		REQUIRE(RegisterDotNetScriptProvider(Providers, Error));
		for (const FString Language : {FString("Lua"), FString("Python"), FString("DotNet")})
		{
			TSharedPtr<FWorld> World = std::make_shared<FWorld>();
			TSharedPtr<FScriptBindingRegistry> Host = std::make_shared<FScriptBindingRegistry>(
			    EScriptPurpose::Gameplay, TArray<FString>{"API.Read", "World.Read", "World.Write", "Log.Write"});
			REQUIRE(RegisterCoreScriptBindings(*Host, Error));
			REQUIRE(RegisterWorldScriptBindings(*Host, World, Error));
			std::unique_ptr<IScriptRuntime> Runtime =
			    Providers.Create({Language, "Win64", EScriptPurpose::Gameplay}, Host, Error);
			REQUIRE_MESSAGE(Runtime, *Error);
			const char* const Source = Language == "Lua" ? "../../../Samples/Scripting/Lua/Source/Main.lua"
			    : Language == "Python"                   ? "../../../Samples/Scripting/Python/Source/Main.py"
			                                             : "../ThirdParty/DotNet/Examples/Genesis.ScriptExample.dll";
			TArray<uint8> Payload;
			REQUIRE(FFile::ReadAllBytes(FPaths::Combine(FPaths::ExecutableDir(), Source), Payload));
			REQUIRE_MESSAGE(Runtime->LoadModule("main", Payload, Error), *Error);
			FJsonValue Result;
			REQUIRE_MESSAGE(Runtime->Call("main", "BuildWorld", FJsonValue::MakeObject(), Result, Error), *Error);
			const FString ActorId = Result.AsString();
			REQUIRE(World->FindActor(ActorId));
			const TFunction<bool(const char*, const FJsonValue&)> Invoke = [&](const char* API, const FJsonValue& A)
			{
				FJsonValue Args = FJsonValue::MakeObject();
				Args["API"] = API;
				Args["Arguments"] = A;
				return Runtime->Call("main", "Invoke", Args, Result, Error);
			};
			FJsonValue Args = FJsonValue::MakeObject();
			Args["ActorId"] = ActorId;
			FTransform3D Transform;
			Transform.Location = Vector3d(10, 20, 30);
			Args["Transform"] = Transform.ToJson();
			REQUIRE_MESSAGE(Invoke("World.SetTransform", Args), *Error);
			CHECK(World->FindActor(ActorId)->Root.Transform.Location.GetY() == 20);
			Args = FJsonValue::MakeObject();
			Args["ActorId"] = ActorId;
			REQUIRE(Invoke("World.GetWorldTransform", Args));
			CHECK(Result.Num() == 16);
			CHECK(Result[3].AsNumber() == 10);
			FJsonValue Component = FJsonValue::MakeObject();
			Component["Type"] = "Example.Component";
			Component["Properties"] = FJsonValue::MakeObject();
			Component["Properties"]["Value"] = 42;
			Args["Component"] = Component;
			REQUIRE_MESSAGE(Invoke("World.AddComponent", Args), *Error);
			const FString ComponentId = Result.AsString();
			REQUIRE(World->FindComponent(ActorId, ComponentId));
			Args = FJsonValue::MakeObject();
			Args["ActorId"] = ActorId;
			Args["ComponentId"] = ComponentId;
			REQUIRE(Invoke("World.FindComponent", Args));
			CHECK(Result.Get("Properties").Get("Value").AsInt() == 42);
			Result["Properties"]["Value"] = 99;
			Component = Result;
			Args = FJsonValue::MakeObject();
			Args["ActorId"] = ActorId;
			Args["Component"] = Component;
			REQUIRE(Invoke("World.SetComponent", Args));
			CHECK(World->FindComponent(ActorId, ComponentId)->Properties.Get("Value").AsInt() == 99);
			Args = FJsonValue::MakeObject();
			Args["ActorId"] = ActorId;
			REQUIRE(Invoke("World.DuplicateActor", Args));
			const FString DuplicateId = Result.AsString();
			CHECK(DuplicateId != ActorId);
			CHECK(World->GetActors().size() == 2);
			Args["ActorId"] = DuplicateId;
			REQUIRE(Invoke("World.DestroyActor", Args));
			CHECK(World->GetActors().size() == 1);
			CHECK_FALSE(Invoke("World.FindActor", Args));
			REQUIRE(Invoke("API.Describe", FJsonValue::MakeObject()));
			CHECK(Result.Get("Bindings").Num() == 20);
			World.reset();
			CHECK_FALSE(Invoke("World.GetActors", FJsonValue::MakeObject()));
			CHECK(Error.Contains("expired"));
		}
	}

	TEST_CASE("Hosted .NET executes compiled C# modules with isolated state and host bindings")
	{
		FScriptRuntimeRegistry Providers;
		FString Error;
		REQUIRE(RegisterDotNetScriptProvider(Providers, Error));
		std::shared_ptr<FTestScriptHost> Host = std::make_shared<FTestScriptHost>();
		std::unique_ptr<IScriptRuntime> A = Providers.Create(
		                                    {"DotNet", "Win64", EScriptPurpose::Gameplay}, Host, Error),
		                                B = Providers.Create({"DotNet", "Win64", EScriptPurpose::Editor}, Host, Error);
		REQUIRE_MESSAGE(A, *Error);
		REQUIRE_MESSAGE(B, *Error);
		TArray<uint8> Payload;
		REQUIRE(FFile::ReadAllBytes(
		    FPaths::Combine(FPaths::ExecutableDir(), "../ThirdParty/DotNet/Examples/Genesis.ScriptExample.dll"),
		    Payload));
		REQUIRE_MESSAGE(A->LoadModule("main", Payload, Error), *Error);
		REQUIRE_MESSAGE(B->LoadModule("main", Payload, Error), *Error);
		FJsonValue Args = FJsonValue::MakeObject();
		Args["Values"] = FJsonValue::MakeArray().Push(1).Push(nullptr).Push(false);
		Args["Object"] = FJsonValue::MakeObject();
		FJsonValue Result;
		REQUIRE_MESSAGE(A->Call("main", "Echo", Args, Result, Error), *Error);
		CHECK(Result == Args);
		REQUIRE(A->Call("main", "Step", {}, Result, Error));
		CHECK(Result.AsInt() == 1);
		REQUIRE(A->Call("main", "Step", {}, Result, Error));
		CHECK(Result.AsInt() == 2);
		REQUIRE(B->Call("main", "Step", {}, Result, Error));
		CHECK(Result.AsInt() == 1);
		Result = "unchanged";
		CHECK_FALSE(A->Call("main", "Fail", {}, Result, Error));
		CHECK(Error.Contains("Expected C# failure"));
		CHECK(Result.AsString() == "unchanged");
		CHECK_FALSE(A->Call("main", "Missing", {}, Result, Error));
		const TArray<uint8> Invalid = {0, 1, 2, 3};
		CHECK_FALSE(A->LoadModule("main", Invalid, Error));
		REQUIRE(A->Call("main", "Step", {}, Result, Error));
		CHECK(Result.AsInt() == 3);
		bool CrossThread = true;
		std::thread Other([&]
		{
			FJsonValue V;
			FString E;
			CrossThread = A->Call("main", "Step", {}, V, E);
		});
		Other.join();
		CHECK_FALSE(CrossThread);
		B.reset();
		REQUIRE(A->Call("main", "Step", {}, Result, Error));
		CHECK(Result.AsInt() == 4);
	}

	TEST_CASE("Embedded Python runs isolated modules and the same host contract as Lua")
	{
		std::shared_ptr<FTestScriptHost> Host = std::make_shared<FTestScriptHost>();
		FScriptRuntimeRegistry Providers;
		FString Error;
		REQUIRE(RegisterPythonScriptProvider(Providers, Error));
		FScriptRuntimeRequest Request{"Python", "Win64", EScriptPurpose::Gameplay};
		Request.InstructionBudget = 100000;
		std::unique_ptr<IScriptRuntime> A = Providers.Create(Request, Host, Error),
		                                B = Providers.Create(Request, Host, Error);
		REQUIRE_MESSAGE(A, *Error);
		REQUIRE_MESSAGE(B, *Error);
		const TFunction<bool(IScriptRuntime&, const char*)> Load = [&](IScriptRuntime& Runtime, const char* Source)
		{
			return Runtime.LoadModule(
			    "main", TArrayView<const uint8>(reinterpret_cast<const uint8*>(Source), std::strlen(Source)), Error);
		};
		const char* Source = R"PY(
import genesis
_count=0
def echo(args):
    return genesis.invoke('Echo',args)
def step(args):
    global _count
    _count+=1
    return _count
def fail(args):
    raise RuntimeError('Expected Python failure')
def cycle(args):
    a=[]
    a.append(a)
    return a
def nonfinite(args):
    return float('nan')
def infinite(args):
    while True:
        pass
)PY";
		REQUIRE_MESSAGE(Load(*A, Source), *Error);
		REQUIRE_MESSAGE(Load(*B, Source), *Error);
		FJsonValue Input = FJsonValue::MakeObject();
		Input["array"] = FJsonValue::MakeArray().Push(1).Push(nullptr).Push(false);
		Input["empty"] = FJsonValue::MakeArray();
		Input["object"] = FJsonValue::MakeObject();
		Input["text"] = "caf\xc3\xa9";
		FJsonValue Result;
		REQUIRE_MESSAGE(A->Call("main", "echo", Input, Result, Error), *Error);
		CHECK(Result == Input);
		REQUIRE(A->Call("main", "step", {}, Result, Error));
		CHECK(Result.AsInt() == 1);
		REQUIRE(A->Call("main", "step", {}, Result, Error));
		CHECK(Result.AsInt() == 2);
		REQUIRE(B->Call("main", "step", {}, Result, Error));
		CHECK(Result.AsInt() == 1);
		for (const char* Function : {"fail", "cycle", "nonfinite", "infinite", "missing"})
		{
			Result = "unchanged";
			CHECK_FALSE(A->Call("main", Function, {}, Result, Error));
			CHECK_FALSE(Error.IsEmpty());
			CHECK(Result.AsString() == "unchanged");
		}
		CHECK_FALSE(Load(*A, "def invalid("));
		REQUIRE(A->Call("main", "step", {}, Result, Error));
		CHECK(Result.AsInt() == 3);
		bool CrossThread = true;
		std::thread Other([&]
		{
			FJsonValue V;
			FString E;
			CrossThread = A->Call("main", "step", {}, V, E);
		});
		Other.join();
		CHECK_FALSE(CrossThread);
		B.reset();
		REQUIRE(A->Call("main", "step", {}, Result, Error));
		CHECK(Result.AsInt() == 4);
	}

	TEST_CASE("Project chooses independent language providers and preserves extension data")
	{
		FProjectDescriptor D;
		D.Name = "ScriptTest";
		FString E;
		for (const char* Language : {"Lua", "Python", "DotNet"})
		{
			D.GameplayLanguage = Language;
			D.EditorLanguages = {"Lua", "Python", "DotNet"};
			FJsonValue J = D.ToJson();
			J["Scripting"]["FutureSetting"] = "retain";
			TOptional<FProjectDescriptor> B = FProjectDescriptor::FromJson(J, E);
			REQUIRE(B);
			CHECK(B->GameplayLanguage == Language);
			CHECK(B->EditorLanguages.size() == 3);
			CHECK(B->ToJson().Get("Scripting").Get("FutureSetting").AsString() == "retain");
		}
		FJsonValue J = D.ToJson();
		J["Scripting"]["EditorLanguages"].Push("Lua");
		CHECK_FALSE(FProjectDescriptor::FromJson(J, E));
		J = D.ToJson();
		J["Scripting"]["GameplayLanguage"] = 7;
		CHECK_FALSE(FProjectDescriptor::FromJson(J, E));
		D.GameplayLanguage = "None";
		D.EditorLanguages.clear();
		TOptional<FProjectDescriptor> Legacy = FProjectDescriptor::FromJson(D.ToJson(), E);
		REQUIRE(Legacy);
		CHECK(Legacy->GameplayLanguage == "None");
	}
	TEST_CASE("Uninstalled and unvalidated providers fail explicitly while worlds get isolated VMs")
	{
		FScriptRuntimeRegistry R;
		FString E;
		FScriptRuntimeRequest Request{"Lua", "Win64", EScriptPurpose::Gameplay};
		std::shared_ptr<FTestScriptHost> Host = std::make_shared<FTestScriptHost>();
		CHECK_FALSE(R.Create(Request, Host, E));
		CHECK(E.Contains("not installed"));
		FScriptProvider P;
		P.Language = "Lua";
		P.GameplayTargets = {"Win64"};
		P.EditorTargets = {"Win64"};
		P.Create = [](const FScriptRuntimeRequest&, std::shared_ptr<IScriptHost>,
		               FString&) -> std::unique_ptr<IScriptRuntime> { return std::make_unique<FTestScriptRuntime>(); };
		REQUIRE(R.Register(P, E));
		CHECK_FALSE(R.Register(P, E));
		std::unique_ptr<IScriptRuntime> A = R.Create(Request, Host, E), B = R.Create(Request, Host, E);
		REQUIRE(A);
		REQUIRE(B);
		CHECK(A.get() != B.get());
		for (const char* Target : {"Switch2", "XboxSeries", "PS5"})
		{
			Request.Target = Target;
			CHECK_FALSE(R.Create(Request, Host, E));
		}
		Request.Target = "Win64";
		CHECK_FALSE(R.Create(Request, {}, E));
	}

	TEST_CASE("Shared script binding descriptors enforce capabilities and argument contracts")
	{
		TSharedPtr<FScriptBindingRegistry> Host = std::make_shared<FScriptBindingRegistry>(
		    EScriptPurpose::Gameplay, TArray<FString>{"World.Read", "Editor.Transactions"});
		FString Error;
		int Calls = 0;
		FScriptBinding Binding;
		Binding.Name = "Actor.GetName";
		Binding.Capability = "World.Read";
		Binding.Arguments = {{"ActorId", EJsonType::String, true}};
		Binding.ResultType = EJsonType::String;
		Binding.Invoke = [&](const FJsonValue& A, FJsonValue& R, FString&)
		{
			++Calls;
			R = A.Get("ActorId");
			return true;
		};
		REQUIRE(Host->Register(Binding, Error));
		CHECK_FALSE(Host->Register(Binding, Error));
		Binding.Name = "Editor.Undo";
		Binding.Capability = "Editor.Transactions";
		Binding.bEditorOnly = true;
		REQUIRE(Host->Register(Binding, Error));
		FJsonValue Args = FJsonValue::MakeObject();
		Args["ActorId"] = "actor-uuid";
		FJsonValue Result;
		REQUIRE(Host->Invoke("Actor.GetName", Args, Result, Error));
		CHECK(Result.AsString() == "actor-uuid");
		CHECK_FALSE(Host->Invoke("Editor.Undo", Args, Result, Error));
		CHECK_FALSE(Host->Invoke("Unknown", Args, Result, Error));
		Args["Unexpected"] = 1;
		CHECK_FALSE(Host->Invoke("Actor.GetName", Args, Result, Error));
		CHECK(Calls == 1);
		CHECK(Host->Describe().Get("Bindings").Num() == 2);
		FScriptRuntimeRegistry Providers;
		REQUIRE(RegisterLuaScriptProvider(Providers, Error));
		std::unique_ptr<IScriptRuntime> Runtime =
		    Providers.Create({"Lua", "Win64", EScriptPurpose::Gameplay}, Host, Error);
		REQUIRE(Runtime);
		const char* Source =
		    "return {name=function(a)return Genesis.invoke('Actor.GetName',a)end,editor=function(a)return "
		    "Genesis.invoke('Editor.Undo',a)end}";
		REQUIRE(Runtime->LoadModule(
		    "bindings", TArrayView<const uint8>(reinterpret_cast<const uint8*>(Source), std::strlen(Source)), Error));
		Args = FJsonValue::MakeObject();
		Args["ActorId"] = "actor-uuid";
		REQUIRE(Runtime->Call("bindings", "name", Args, Result, Error));
		CHECK(Result.AsString() == "actor-uuid");
		CHECK_FALSE(Runtime->Call("bindings", "editor", Args, Result, Error));
		CHECK(Error.Contains("capability denied"));
	}
	TEST_CASE("Embedded Lua executes real modules and shares JSON host bindings")
	{
		FScriptRuntimeRegistry Registry;
		FString Error;
		REQUIRE(RegisterLuaScriptProvider(Registry, Error));
		FScriptRuntimeRequest Request{"Lua", "Win64", EScriptPurpose::Gameplay};
		std::shared_ptr<FTestScriptHost> Host = std::make_shared<FTestScriptHost>();
		std::unique_ptr<IScriptRuntime> A = Registry.Create(Request, Host, Error),
		                                B = Registry.Create(Request, Host, Error);
		REQUIRE_MESSAGE(A, *Error);
		REQUIRE(B);
		const TFunction<bool(IScriptRuntime&, const FString&, const char*)> Load =
		    [&](IScriptRuntime& Runtime, const FString& Name, const char* Source)
		{
			return Runtime.LoadModule(
			    Name, TArrayView<const uint8>(reinterpret_cast<const uint8*>(Source), std::strlen(Source)), Error);
		};
		const char* Source = R"lua(
            local count = 0
            return {
                echo=function(args) return Genesis.invoke("Echo", args) end,
                step=function(args) count=count+1; return count end,
                containers=function(args) return {emptyArray=Genesis.array({}),emptyObject={},null=Genesis.null,values={1,true,"text",Genesis.null}} end,
                fail=function(args) error("Expected Lua error") end,
                cycle=function(args) local x={}; x.self=x; return x end,
                mixed=function(args) return {[1]="number",x="string"} end,
                nonfinite=function(args) return 0/0 end,
                unsupported=function(args) return function()end end,
                infinite=function(args) while true do end end,
                capability=function(args) return {io=io==nil,os=os==nil,package=package==nil,debug=debug==nil,loadfile=loadfile==nil} end
            }
        )lua";
		REQUIRE_MESSAGE(Load(*A, "main", Source), *Error);
		REQUIRE(Load(*B, "main", Source));
		FJsonValue Input = FJsonValue::MakeObject();
		Input["actor"] = "stable-id";
		Input["array"] = FJsonValue::MakeArray().Push(1).Push(nullptr).Push(false);
		Input["nested"] = FJsonValue::MakeObject();
		Input["empty"] = FJsonValue::MakeArray();
		Input["text"] = "Unicode: caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
		FJsonValue Result;
		REQUIRE_MESSAGE(A->Call("main", "echo", Input, Result, Error), *Error);
		CHECK(Result == Input);
		REQUIRE(A->Call("main", "step", {}, Result, Error));
		CHECK(Result.AsInt() == 1);
		REQUIRE(A->Call("main", "step", {}, Result, Error));
		CHECK(Result.AsInt() == 2);
		REQUIRE(B->Call("main", "step", {}, Result, Error));
		CHECK(Result.AsInt() == 1);
		REQUIRE(A->Call("main", "containers", {}, Result, Error));
		CHECK(Result.Get("emptyArray").IsArray());
		CHECK(Result.Get("emptyObject").IsObject());
		CHECK(Result.Get("null").IsNull());
		CHECK(Result.Get("values").Num() == 4);
		CHECK(Result.Get("values")[3].IsNull());
		REQUIRE(A->Call("main", "capability", {}, Result, Error));
		for (const FString& Key : Result.GetKeys())
			CHECK(Result.Get(Key.View()).AsBool());
		for (const char* Function : {"fail", "cycle", "mixed", "nonfinite", "unsupported", "infinite", "missing"})
		{
			Result = "unchanged";
			CHECK_FALSE(A->Call("main", Function, {}, Result, Error));
			CHECK_FALSE(Error.IsEmpty());
			CHECK(Result.AsString() == "unchanged");
		}
		REQUIRE(A->Call("main", "step", {}, Result, Error));
		CHECK(Result.AsInt() == 3);
		CHECK_FALSE(Load(*A, "main", "return {broken = }"));
		REQUIRE(A->Call("main", "step", {}, Result, Error));
		CHECK(Result.AsInt() == 4);
		bool CrossThread = true;
		std::thread Other([&]
		{
			FString E;
			FJsonValue V;
			CrossThread = A->Call("main", "step", {}, V, E);
		});
		Other.join();
		CHECK_FALSE(CrossThread);
	}
	TEST_CASE("Lua host failures memory limits and finalizer loops stay contained")
	{
		class FFailHost : public IScriptHost
		{
			bool Invoke(const FString&, const FJsonValue&, FJsonValue&, FString& E) override
			{
				E = "Denied host capability";
				return false;
			}
		};
		FScriptRuntimeRegistry Registry;
		FString Error;
		REQUIRE(RegisterLuaScriptProvider(Registry, Error));
		FScriptRuntimeRequest Request{"Lua", "Win64", EScriptPurpose::Editor};
		Request.MemoryLimitBytes = 256 * 1024;
		Request.InstructionBudget = 10000;
		std::unique_ptr<IScriptRuntime> Runtime = Registry.Create(Request, std::make_shared<FFailHost>(), Error);
		REQUIRE_MESSAGE(Runtime, *Error);
		const TFunction<bool(const char*)> Load = [&](const char* Source)
		{
			return Runtime->LoadModule(
			    "main", TArrayView<const uint8>(reinterpret_cast<const uint8*>(Source), std::strlen(Source)), Error);
		};
		REQUIRE(Load("return {denied=function(a)return "
		             "Genesis.invoke('Denied',a)end,memory=function(a)return string.rep('x',1024*1024)end}"));
		FJsonValue Result = "before";
		CHECK_FALSE(Runtime->Call("main", "denied", {}, Result, Error));
		CHECK(Error.Contains("Denied host capability"));
		CHECK(Result.AsString() == "before");
		CHECK_FALSE(Runtime->Call("main", "memory", {}, Result, Error));
		CHECK_FALSE(Error.IsEmpty());
		CHECK_FALSE(Load("while true do end"));
		CHECK(Error.Contains("instruction budget"));
		CHECK_FALSE(Load("local x=setmetatable({}, {__gc=function() while true do end end}); return {keep=x}"));
		CHECK(Error.Contains("finalizers are unavailable"));
		Runtime.reset();
		Request.MemoryLimitBytes = 1;
		CHECK_FALSE(Registry.Create(Request, std::make_shared<FFailHost>(), Error));
	}

	TEST_CASE("Runtime assets preserve native identity authority and bounded bulk access")
	{
		const std::filesystem::path Directory = std::filesystem::temp_directory_path() / FAssetId::New().ToWide();
		std::filesystem::create_directory(Directory);
		// Keep the fixture for diagnostics; it is outside the repository and contains no user data.
		TSharedPtr<FAssetRegistry> Assets = std::make_shared<FAssetRegistry>();
		FString Error;
		FAssetPackage Package;
		Package.Schema = "Script.AssetTest";
		Package.Exports["Value"] = 42;
		Package.Bulk = {1, 2, 3, 4};
		const FString File = FString::FromWide((Directory / L"Example.asset").wstring());
		REQUIRE(FAssetPackageStore::Save(File, Package, Error));
		REQUIRE(Assets->SetMounts({{"/Game", FString::FromWide(Directory.wstring())}}, Error));
		REQUIRE(Assets->Scan());
		FScriptBindingRegistry Host(EScriptPurpose::Gameplay, {"Assets.Read"});
		REQUIRE(RegisterAssetScriptBindings(Host, Assets, Error));
		FJsonValue Result;
		FJsonValue A = FJsonValue::MakeObject();
		A["Offset"] = 0;
		A["Count"] = 1;
		REQUIRE(Host.Invoke("Assets.GetAssets", A, Result, Error));
		REQUIRE(Result.Get("Assets").Num() == 1);
		CHECK(Result.Get("Assets")[0].Get("Reference").Get("Id").AsString() == Package.Id);
		A = FJsonValue::MakeObject();
		A["Reference"] = FAssetReference{Package.Id, "/Game/Example"}.ToJson();
		REQUIRE(Host.Invoke("Assets.LoadExports", A, Result, Error));
		CHECK(Result.Get("Exports").Get("Value").AsInt() == 42);
		A["Offset"] = 1;
		A["Count"] = 2;
		REQUIRE(Host.Invoke("Assets.ReadBulk", A, Result, Error));
		CHECK(Result[0].AsInt() == 2);
		CHECK(Result[1].AsInt() == 3);
		A["Count"] = 16385;
		CHECK_FALSE(Host.Invoke("Assets.ReadBulk", A, Result, Error));
		A["Count"] = 2;
		A["Offset"] = 3;
		CHECK_FALSE(Host.Invoke("Assets.ReadBulk", A, Result, Error));
		A = FJsonValue::MakeObject();
		A["Reference"] = FAssetReference{FAssetId::New(), "/Game/Example"}.ToJson();
		CHECK_FALSE(Host.Invoke("Assets.Resolve", A, Result, Error));
		Assets.reset();
		CHECK_FALSE(Host.Invoke("Assets.GetIssues", FJsonValue::MakeObject(), Result, Error));
		CHECK(Error.Contains("expired"));
	}

	TEST_CASE("Gameplay components own isolated instances and receive lifecycle and events in every language")
	{
		FScriptRuntimeRegistry Providers;
		FString Error;
		REQUIRE(RegisterBuiltinScriptProviders(Providers, Error));
		for (const FString Language : {FString("Lua"), FString("Python"), FString("DotNet")})
		{
			TSharedPtr<FWorld> World = std::make_shared<FWorld>();
			FActor Actor = FActor::New("First"), Other = FActor::New("Second");
			FActorComponent Script = FActorComponent::New("Genesis.Script"),
			                OtherScript = FActorComponent::New("Genesis.Script");
			Script.Properties["Module"] = "main";
			OtherScript.Properties["Module"] = "main";
			Actor.Components.push_back(Script);
			Other.Components.push_back(OtherScript);
			REQUIRE(World->AddActor(Actor, Error));
			REQUIRE(World->AddActor(Other, Error));
			TArray<FString> Logs;
			TSharedPtr<FScriptBindingRegistry> Host = std::make_shared<FScriptBindingRegistry>(
			    EScriptPurpose::Gameplay, TArray<FString>{"API.Read", "Log.Write", "World.Read", "World.Write"});
			REQUIRE(RegisterCoreScriptBindings(*Host, Error, [&](const FString& S) { Logs.push_back(S); }));
			REQUIRE(RegisterWorldScriptBindings(*Host, World, Error));
			const char* const Source = Language == "Lua" ? "../../../Samples/Scripting/Lua/Source/Main.lua"
			    : Language == "Python"                   ? "../../../Samples/Scripting/Python/Source/Main.py"
			                                             : "../ThirdParty/DotNet/Examples/Genesis.ScriptExample.dll";
			int Loads = 0;
			FScriptWorldSubsystem System({Language, "Win64", EScriptPurpose::Gameplay}, World, Providers, Host,
			    [&](const FString& Module, TArray<uint8>& Bytes, FString& E)
			{
				++Loads;
				if (Module != "main")
				{
					E = "Missing module";
					return false;
				}
				return FFile::ReadAllBytes(FPaths::Combine(FPaths::ExecutableDir(), Source), Bytes);
			});
			REQUIRE_MESSAGE(System.BeginPlay(Error), *Error);
			CHECK(System.GetInstanceCount() == 2);
			CHECK(World->FindActor(Actor.Id)->Name == "Script Begin");
			REQUIRE_MESSAGE(System.Tick(0.016f, Error), *Error);
			CHECK(World->FindActor(Actor.Id)->Name == "Script Tick 1");
			CHECK(World->FindActor(Other.Id)->Name == "Script Tick 1");
			CHECK(Loads == 2);
			FJsonValue Payload = FJsonValue::MakeObject();
			Payload["Name"] = "Event received";
			REQUIRE(System.SendEvent("Rename", Payload, Error));
			CHECK(World->FindActor(Actor.Id)->Name == "Event received");
			REQUIRE(World->RemoveActor(Other.Id, Error));
			REQUIRE(System.Tick(0.016f, Error));
			CHECK(System.GetInstanceCount() == 1);
			REQUIRE(Logs.size() == 1);
			CHECK(Logs[0] == "Removed");
			CHECK(World->FindActor(Actor.Id)->Name == "Script Tick 2");
			FActor New = FActor::New("New");
			FActorComponent NewScript = FActorComponent::New("Genesis.Script");
			NewScript.Properties["Module"] = "main";
			New.Components.push_back(NewScript);
			REQUIRE(World->AddActor(New, Error));
			REQUIRE(System.Tick(0.016f, Error));
			CHECK(System.GetInstanceCount() == 2);
			CHECK(World->FindActor(New.Id)->Name == "Script Tick 1");
			CHECK(World->FindActor(Actor.Id)->Name == "Script Tick 3");
			CHECK_FALSE(System.Tick(-1, Error));
			CHECK(World->FindActor(Actor.Id)->Name == "Script Tick 3");
			FString ThreadError;
			bool ThreadResult = true;
			std::thread Worker([&] { ThreadResult = System.Tick(0.1f, ThreadError); });
			Worker.join();
			CHECK_FALSE(ThreadResult);
			CHECK(ThreadError.Contains("owning thread"));
			REQUIRE(System.EndPlay(Error));
			CHECK(System.GetInstanceCount() == 0);
			CHECK(Logs.size() == 3);
			CHECK_FALSE(System.Tick(0.016f, Error));
			REQUIRE(System.EndPlay(Error));
		}
	}

	TEST_CASE("Faulted gameplay components stop ticking and failed loads are not retried every frame")
	{
		FScriptRuntimeRegistry Providers;
		FString Error;
		REQUIRE(RegisterLuaScriptProvider(Providers, Error));
		TSharedPtr<FWorld> World = std::make_shared<FWorld>();
		FActor Actor = FActor::New("Fault fixture");
		FActorComponent C = FActorComponent::New("Genesis.Script");
		C.Properties["Module"] = "broken";
		Actor.Components.push_back(C);
		REQUIRE(World->AddActor(Actor, Error));
		TSharedPtr<FScriptBindingRegistry> Host =
		    std::make_shared<FScriptBindingRegistry>(EScriptPurpose::Gameplay, TArray<FString>{"Test.Call"});
		FScriptWorldSubsystem* Current = nullptr;
		int ReentryAttempts = 0;
		REQUIRE(Host->Register({"Test.Reenter", "Test.Call", false, {}, EJsonType::Bool,
		                           [&](const FJsonValue&, FJsonValue& R, FString&)
		{
			FString ReentryError;
			R = Current->Tick(0.1f, ReentryError);
			CHECK(ReentryError.Contains("reenter"));
			++ReentryAttempts;
			return true;
		}},
		    Error));
		int Loads = 0;
		const FString Source =
		    "return {OnBeginPlay=function(a) assert(not Genesis.invoke('Test.Reenter',{})) "
		    "end,OnTick=function(a) error('expected tick fault') end,OnEvent=function(a) error('must not "
		    "run') end,OnEndPlay=function(a) end}";
		FScriptWorldSubsystem System({"Lua", "Win64", EScriptPurpose::Gameplay}, World, Providers, Host,
		    [&](const FString&, TArray<uint8>& Bytes, FString&)
		{
			++Loads;
			Bytes.assign(Source.View().begin(), Source.View().end());
			return true;
		});
		Current = &System;
		REQUIRE(System.BeginPlay(Error));
		CHECK(ReentryAttempts == 1);
		CHECK_FALSE(System.Tick(0.016f, Error));
		CHECK(Error.Contains("expected tick fault"));
		REQUIRE(System.Tick(0.016f, Error));
		REQUIRE(System.SendEvent("Ignored", FJsonValue::MakeObject(), Error));
		CHECK(Loads == 1);
		REQUIRE(System.EndPlay(Error));
		int FailedLoads = 0;
		FScriptWorldSubsystem Missing({"Lua", "Win64", EScriptPurpose::Gameplay}, World, Providers, Host,
		    [&](const FString&, TArray<uint8>&, FString&) -> bool
		{
			++FailedLoads;
			throw std::runtime_error("loader failure");
		});
		CHECK_FALSE(Missing.BeginPlay(Error));
		CHECK(Error.Contains("loader failure"));
		REQUIRE(Missing.Tick(0.016f, Error));
		CHECK(FailedLoads == 1);
		REQUIRE(World->RemoveActor(Actor.Id, Error));
		REQUIRE(Missing.Tick(0.016f, Error));
		CHECK(Missing.GetInstanceCount() == 0);
		REQUIRE(Missing.EndPlay(Error));
	}

	TEST_CASE("Project module catalogs select real source and compiled payloads and preserve extensions")
	{
		FString Error;
		for (const FString Language : {FString("Lua"), FString("Python"), FString("DotNet")})
		{
			const FString Path = FPaths::Combine(FPaths::ExecutableDir(),
			    FString::Format("../../../Samples/Scripting/{}/{}.gproject", Language, Language));
			const TOptional<FProjectContext> Project = FProjectContext::Open(Path, Error);
			REQUIRE_MESSAGE(Project, *Error);
			CHECK(Project->GetDescriptor().GameplayLanguage == Language);
			REQUIRE(Project->GetDescriptor().ScriptModules.size() == 1);
			TArray<uint8> Bytes;
			REQUIRE_MESSAGE(Project->LoadScriptModule("main", Bytes, Error), *Error);
			CHECK(!Bytes.empty());
			FJsonValue Json = Project->GetDescriptor().ToJson();
			Json["Scripting"]["Modules"][0]["FutureModuleOption"] = 42;
			TOptional<FProjectDescriptor> Parsed = FProjectDescriptor::FromJson(Json, Error);
			REQUIRE(Parsed);
			CHECK(Parsed->ToJson().Get("Scripting").Get("Modules")[0].Get("FutureModuleOption").AsInt() == 42);
			FJsonValue Bad = Json;
			Bad["Scripting"]["Modules"].Push(Bad["Scripting"]["Modules"][0]);
			CHECK_FALSE(FProjectDescriptor::FromJson(Bad, Error));
			Bad = Json;
			Bad["Scripting"]["Modules"][0]["Source"] = "C:/absolute/source.lua";
			CHECK_FALSE(FProjectDescriptor::FromJson(Bad, Error));
			Bytes = {7};
			CHECK_FALSE(Project->LoadScriptModule("missing", Bytes, Error));
			CHECK(Bytes == TArray<uint8>{7});
		}
	}
}

TEST_CASE("Native scripting validation preserves world and binding outputs on failed edits" *
    doctest::test_suite("Scripting"))
{
	FString Error;
	TSharedPtr<FWorld> World = std::make_shared<FWorld>();
	FActor Owner = FActor::New("Original");
	REQUIRE(World->AddActor(Owner, Error));
	FScriptBindingRegistry Host(EScriptPurpose::Gameplay, {"World.Read", "World.Write"});
	REQUIRE(RegisterWorldScriptBindings(Host, World, Error));
	const FJsonValue Before = World->ToJson();
	FJsonValue Result = "preserved";
	FJsonValue Arguments = FJsonValue::MakeObject();
	Arguments["ActorId"] = Owner.Id;
	Arguments["Component"] = FActorComponent::New("Example.Missing").ToJson();
	CHECK_FALSE(Host.Invoke("World.SetComponent", Arguments, Result, Error));
	CHECK(Error.Contains("Component is missing"));
	CHECK(Result.AsString() == "preserved");
	CHECK(World->ToJson() == Before);
	Arguments = FJsonValue::MakeObject();
	Arguments["ActorId"] = Owner.Id;
	Arguments["ComponentId"] = Owner.Root.Id;
	CHECK_FALSE(Host.Invoke("World.RemoveComponent", Arguments, Result, Error));
	CHECK(Error.Contains("root component"));
	CHECK(World->ToJson() == Before);
	CHECK(Result.AsString() == "preserved");
	Arguments["ActorId"] = FAssetId::New();
	CHECK_FALSE(Host.Invoke("World.FindComponent", Arguments, Result, Error));
	CHECK(Error.Contains("Actor is missing"));
	Arguments = FJsonValue::MakeObject();
	Arguments["ActorId"] = Owner.Id;
	REQUIRE(Host.Invoke("World.FindActor", Arguments, Result, Error));
	CHECK(Result.Get("Name").AsString() == "Original");
	CHECK(Error.IsEmpty());
}

TEST_CASE("Every provider recovers from rejected host responses and foreign callback exceptions" *
    doctest::test_suite("Scripting"))
{
	class FResponseHost final : public IScriptHost
	{
	public:
		enum class EResponse
		{
			Echo,
			EmptyFailure,
			Nonfinite,
			ForeignFailure
		};
		EResponse Response = EResponse::Echo;
		int Calls = 0;
		bool Invoke(const FString&, const FJsonValue& Arguments, FJsonValue& Result, FString& Error) override
		{
			++Calls;
			Error.Empty();
			if (Response == EResponse::EmptyFailure)
			{
				return false;
			}
			if (Response == EResponse::ForeignFailure)
			{
				throw std::runtime_error("Injected foreign callback failure");
			}
			Result =
			    Response == EResponse::Nonfinite ? FJsonValue(std::numeric_limits<double>::quiet_NaN()) : Arguments;
			return true;
		}
	};
	FString Error;
	FScriptRuntimeRegistry Providers;
	REQUIRE(RegisterBuiltinScriptProviders(Providers, Error));
	for (const FString Language : {FString("Lua"), FString("Python"), FString("DotNet")})
	{
		CAPTURE(Language);
		TSharedPtr<FResponseHost> Host = std::make_shared<FResponseHost>();
		std::unique_ptr<IScriptRuntime> Runtime =
		    Providers.Create({Language, "Win64", EScriptPurpose::Gameplay}, Host, Error);
		REQUIRE_MESSAGE(Runtime, *Error);
		TArray<uint8> Payload;
		if (Language == "DotNet")
		{
			REQUIRE(FFile::ReadAllBytes(
			    FPaths::Combine(FPaths::ExecutableDir(), "../ThirdParty/DotNet/Examples/Genesis.ScriptExample.dll"),
			    Payload));
		}
		else
		{
			const char* Source = Language == "Lua"
			    ? "return {Echo=function(a) return Genesis.invoke('Echo',a) end, Invoke=function(a) return "
			      "Genesis.invoke(a.API,a.Arguments) end}"
			    : "import genesis\ndef Echo(a):\n    return genesis.invoke('Echo',a)\ndef Invoke(a):\n    return "
			      "genesis.invoke(a['API'],a['Arguments'])\n";
			Payload.assign(
			    reinterpret_cast<const uint8*>(Source), reinterpret_cast<const uint8*>(Source) + std::strlen(Source));
		}
		REQUIRE_MESSAGE(Runtime->LoadModule("main", Payload, Error), *Error);
		FJsonValue Arguments = FJsonValue::MakeObject();
		Arguments["Value"] = 42;
		for (FResponseHost::EResponse Response : {FResponseHost::EResponse::EmptyFailure,
		         FResponseHost::EResponse::Nonfinite, FResponseHost::EResponse::ForeignFailure})
		{
			Host->Response = Response;
			FJsonValue Result = "preserved";
			CHECK_FALSE(Runtime->Call("main", "Echo", Arguments, Result, Error));
			CHECK_FALSE(Error.IsEmpty());
			CHECK(Result.AsString() == "preserved");
			if (Response == FResponseHost::EResponse::EmptyFailure)
			{
				CHECK(Error.Contains("host call failed"));
			}
			if (Response == FResponseHost::EResponse::ForeignFailure)
			{
				CHECK(Error.Contains("Injected foreign callback failure"));
			}
			Host->Response = FResponseHost::EResponse::Echo;
			REQUIRE_MESSAGE(Runtime->Call("main", "Echo", Arguments, Result, Error), *Error);
			CHECK(Result == Arguments);
			CHECK(Error.IsEmpty());
		}
		for (const FString Name : {FString(), FString(std::string(129, 'x')), FString(std::string("bad\0name", 8))})
		{
			FJsonValue Wrapper = FJsonValue::MakeObject();
			Wrapper["API"] = Name;
			Wrapper["Arguments"] = Arguments;
			const int BeforeCalls = Host->Calls;
			FJsonValue Result = "preserved";
			CHECK_FALSE(Runtime->Call("main", "Invoke", Wrapper, Result, Error));
			CHECK_FALSE(Error.IsEmpty());
			CHECK(Result.AsString() == "preserved");
			CHECK(Host->Calls == BeforeCalls);
			REQUIRE(Runtime->Call("main", "Echo", Arguments, Result, Error));
			CHECK(Result == Arguments);
		}
	}
}

TEST_CASE("Lua validates nested host arguments without leaking stack state or invoking the host" *
    doctest::test_suite("Scripting"))
{
	class FCountingHost final : public IScriptHost
	{
	public:
		int Calls = 0;
		bool Invoke(const FString&, const FJsonValue& Arguments, FJsonValue& Result, FString& Error) override
		{
			++Calls;
			Result = Arguments;
			Error.Empty();
			return true;
		}
	};
	FString Error;
	FScriptRuntimeRegistry Providers;
	REQUIRE(RegisterLuaScriptProvider(Providers, Error));
	TSharedPtr<FCountingHost> Host = std::make_shared<FCountingHost>();
	std::unique_ptr<IScriptRuntime> Runtime = Providers.Create({"Lua", "Win64", EScriptPurpose::Gameplay}, Host, Error);
	REQUIRE(Runtime);
	const char* Source = R"lua(
return {Check=function(a)
    local cycle={}; cycle.self=cycle
    local values={cycle, {nested=function()end}, {[1]=1,other=2}, {[2]=2}, {number=0/0}}
    for _,value in ipairs(values) do
        local ok=pcall(Genesis.invoke,'Echo',value)
        if ok then error('invalid input accepted') end
    end
    local ok=pcall(Genesis.invoke,42,{})
    if ok then error('invalid API accepted') end
    return Genesis.invoke('Echo',{recovered=true})
end}
)lua";
	REQUIRE(Runtime->LoadModule(
	    "main", TArrayView<const uint8>(reinterpret_cast<const uint8*>(Source), std::strlen(Source)), Error));
	for (int Attempt = 0; Attempt < 3; ++Attempt)
	{
		FJsonValue Result;
		REQUIRE_MESSAGE(Runtime->Call("main", "Check", {}, Result, Error), *Error);
		CHECK(Result.Get("recovered").AsBool());
		CHECK(Host->Calls == Attempt + 1);
		CHECK(Error.IsEmpty());
	}
}
