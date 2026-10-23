// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/DotNetScriptRuntime.h"
#include "HAL/File.h"
#include "HAL/PlatformMisc.h"
#include "hostfxr.h"
#include "coreclr_delegates.h"
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <cstring>
#include <stdexcept>
namespace
{
using FDispatch = int(__cdecl*)(const uint8*, int, const uint8*, int, uint8**, int*);
using FFree = void(__cdecl*)(uint8*);
FDispatch Dispatch = nullptr;
FFree FreeOutput = nullptr;
HMODULE HostLibrary = nullptr;
FString LoadedBridge;
std::mutex InitializationMutex, ContextMutex;
std::atomic<uint64> NextContext = 1;
struct FHostContext
{
	std::shared_ptr<IScriptHost> Host;
	std::thread::id Owner;
	std::atomic<bool> Active = false;
};
std::map<uint64, std::weak_ptr<FHostContext>> Contexts;
FString Environment(const wchar_t* Name)
{
	wchar_t Buffer[32768];
	auto Size = GetEnvironmentVariableW(Name, Buffer, std::size(Buffer));
	return Size && Size < std::size(Buffer) ? FString::FromWide(Buffer) : FString();
}
bool InitializeBridge(const FString& Requested, FString& Error)
{
	std::lock_guard Lock(InitializationMutex);
	const auto Default = FPaths::Combine(FPaths::ExecutableDir(), "../ThirdParty/DotNet");
	const auto Directory = std::filesystem::absolute(
		std::filesystem::path((Requested.IsEmpty() ? Default : Requested).ToWide()));
	const auto Bridge = Directory / L"Genesis.ScriptBridge.dll",
			   Config = Directory / L"Genesis.ScriptBridge.runtimeconfig.json";
	if (Dispatch)
	{
		if (LoadedBridge != FString::FromWide(Bridge.wstring()))
		{
			Error = "A different .NET script bridge is already loaded.";
			return false;
		}
		return true;
	}
	if (!std::filesystem::exists(Bridge) || !std::filesystem::exists(Config))
	{
		Error = ".NET script bridge unavailable. Run Engine/Build/SetupScriptingRuntimes.ps1.";
		return false;
	}
	auto RootString = Environment(L"GENESIS_DOTNET_ROOT");
	if (RootString.IsEmpty())
	{
		const auto Bundled = Directory / L"Runtime";
		RootString = std::filesystem::exists(Bundled / L"host/fxr/10.0.12/hostfxr.dll")
						 ? FString::FromWide(Bundled.wstring())
						 : FPaths::Combine(Environment(L"ProgramFiles"), "dotnet");
	}
	const auto Root = std::filesystem::absolute(std::filesystem::path(RootString.ToWide()));
	const auto Host = Root / L"host/fxr/10.0.12/hostfxr.dll";
	if (!std::filesystem::exists(Host))
	{
		Error = ".NET 10.0.12 host unavailable. Install the runtime or set GENESIS_DOTNET_ROOT.";
		return false;
	}
	if (!HostLibrary)
		HostLibrary = LoadLibraryExW(Host.c_str(), nullptr,
									 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!HostLibrary)
	{
		Error = "Cannot load .NET hostfxr.";
		return false;
	}
	auto Initialize = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
		GetProcAddress(HostLibrary, "hostfxr_initialize_for_runtime_config"));
	auto GetDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
		GetProcAddress(HostLibrary, "hostfxr_get_runtime_delegate"));
	auto Close = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(HostLibrary, "hostfxr_close"));
	if (!Initialize || !GetDelegate || !Close)
	{
		Error = "Missing .NET hosting entry points.";
		return false;
	}
	hostfxr_handle Context = nullptr;
	hostfxr_initialize_parameters Parameters{sizeof(hostfxr_initialize_parameters), nullptr, Root.c_str()};
	int Status = Initialize(Config.c_str(), &Parameters, &Context);
	if (Status < 0 || !Context)
	{
		Error = FString::Format("Cannot initialize .NET runtime ({}).", Status);
		if (Context)
			Close(Context);
		return false;
	}
	load_assembly_and_get_function_pointer_fn Load = nullptr;
	Status =
		GetDelegate(Context, hdt_load_assembly_and_get_function_pointer, reinterpret_cast<void**>(&Load));
	Close(Context);
	if (Status < 0 || !Load)
	{
		Error = "Cannot get .NET assembly loader.";
		return false;
	}
	Status = Load(Bridge.c_str(), L"Genesis.Scripting.ScriptBridge, Genesis.ScriptBridge", L"Dispatch",
				  UNMANAGEDCALLERSONLY_METHOD, nullptr, reinterpret_cast<void**>(&Dispatch));
	if (Status < 0 || !Dispatch)
	{
		Error = "Cannot load .NET script bridge entry point.";
		Dispatch = nullptr;
		return false;
	}
	Status = Load(Bridge.c_str(), L"Genesis.Scripting.ScriptBridge, Genesis.ScriptBridge", L"FreeOutput",
				  UNMANAGEDCALLERSONLY_METHOD, nullptr, reinterpret_cast<void**>(&FreeOutput));
	if (Status < 0 || !FreeOutput)
	{
		Error = "Cannot load .NET script bridge release entry point.";
		Dispatch = nullptr;
		return false;
	}
	LoadedBridge = FString::FromWide(Bridge.wstring());
	Error.Empty();
	return true;
}
void __cdecl ReleaseHostOutput(intptr_t, uint8* Output)
{
	delete[] Output;
}
int __cdecl InvokeHost(intptr_t Context, const uint8* Bytes, int Size, uint8** Output, int* OutputSize)
{
	if (!Output || !OutputSize)
		return -1;
	*Output = nullptr;
	*OutputSize = 0;
	try
	{
		FString Error;
		FJsonValue Result;
		bool Success = false;
		try
		{
			std::shared_ptr<FHostContext> Host;
			{
				std::lock_guard Lock(ContextMutex);
				const auto It = Contexts.find(static_cast<uint64>(Context));
				if (It != Contexts.end())
					Host = It->second.lock();
			}
			if (!Host || Host->Owner != std::this_thread::get_id() || !Host->Active)
				throw std::runtime_error("Host call requires an active script session on the owning thread.");
			if (!Bytes || Size < 0 || Size > 4 * 1024 * 1024)
				throw std::runtime_error("Invalid host request size.");
			auto Request =
				FJsonValue::Parse(std::string_view(reinterpret_cast<const char*>(Bytes), Size), &Error);
			if (!Request || !Request->IsObject() || Request->Get("API").GetType() != EJsonType::String)
				throw std::runtime_error("Invalid host request.");
			Success =
				Host->Host->Invoke(Request->Get("API").AsString(), Request->Get("Arguments"), Result, Error);
			if (Success && !ValidateScriptValue(Result, Error))
				Success = false;
			if (!Success && Error.IsEmpty())
				Error = "Script host call failed.";
		}
		catch (const std::exception& E)
		{
			Error = E.what();
		}
		catch (...)
		{
			Error = "Host binding failed with an unknown exception.";
		}
		auto Text = Success ? Result.ToString(false) : Error;
		if (Text.Len() > 4 * 1024 * 1024)
		{
			Text = "Host response exceeds 4 MiB.";
			Success = false;
		}
		*Output = new uint8[std::max(1, Text.Len())];
		std::memcpy(*Output, Text.View().data(), Text.Len());
		*OutputSize = Text.Len();
		return Success ? 0 : 1;
	}
	catch (...)
	{
		return -1;
	}
}
class FDotNetScriptRuntime final : public IScriptRuntime
{
  public:
	explicit FDotNetScriptRuntime(std::shared_ptr<IScriptHost> Host)
		: Context(std::make_shared<FHostContext>()), Token(NextContext++)
	{
		Context->Host = std::move(Host);
		Context->Owner = std::this_thread::get_id();
		std::lock_guard Lock(ContextMutex);
		Contexts.emplace(Token, Context);
	}
	~FDotNetScriptRuntime() override
	{
		{
			std::lock_guard Lock(ContextMutex);
			Contexts.erase(Token);
		}
		if (Session)
		{
			FString Error;
			FJsonValue Result;
			auto Request = FJsonValue::MakeObject();
			Request["Operation"] = "Dispose";
			Exchange(Request, {}, Result, Error);
		}
	}
	bool Initialize(const FString& Directory, FString& Error)
	{
		if (!InitializeBridge(Directory, Error))
			return false;
		auto Request = FJsonValue::MakeObject();
		Request["Operation"] = "Create";
		Request["Context"] = FString(std::to_string(Token));
		Request["HostInvoke"] = FString(std::to_string(reinterpret_cast<uintptr_t>(&InvokeHost)));
		Request["HostRelease"] = FString(std::to_string(reinterpret_cast<uintptr_t>(&ReleaseHostOutput)));
		FJsonValue Result;
		if (!Exchange(Request, {}, Result, Error))
			return false;
		Session = static_cast<uint64>(Result.AsNumber());
		if (!Session)
		{
			Error = "Invalid .NET script session.";
			return false;
		}
		return true;
	}
	bool LoadModule(const FString& Name, TArrayView<const uint8> Payload, FString& Error) override
	{
		if (!Check(Error))
			return false;
		if (!ValidName(Name) || Payload.empty() || Payload.size() > 4 * 1024 * 1024)
		{
			Error = ".NET requires a module name and compiled assembly payload up to 4 MiB.";
			return false;
		}
		auto Request = FJsonValue::MakeObject();
		Request["Operation"] = "Load";
		Request["Module"] = Name;
		FJsonValue Result;
		return Exchange(Request, Payload, Result, Error);
	}
	bool Call(const FString& Module, const FString& Function, const FJsonValue& Arguments, FJsonValue& Result,
			  FString& Error) override
	{
		if (!Check(Error))
			return false;
		if (!ValidName(Module) || !ValidName(Function))
		{
			Error = "Invalid .NET module or function name.";
			return false;
		}
		if (!ValidateScriptValue(Arguments, Error))
			return false;
		auto Request = FJsonValue::MakeObject();
		Request["Operation"] = "Call";
		Request["Module"] = Module;
		Request["Function"] = Function;
		Request["Arguments"] = Arguments;
		return Exchange(Request, {}, Result, Error);
	}

  private:
	static bool ValidName(const FString& S)
	{
		return !S.IsEmpty() && S.Len() <= 128 && S.View().find('\0') == std::string_view::npos;
	}
	bool Check(FString& Error)
	{
		if (Context->Owner != std::this_thread::get_id())
		{
			Error = ".NET runtime calls must run on the owning thread.";
			return false;
		}
		if (Context->Active)
		{
			Error = ".NET runtime calls cannot be reentrant.";
			return false;
		}
		return true;
	}
	bool Exchange(FJsonValue Request, TArrayView<const uint8> Payload, FJsonValue& Result, FString& Error)
	{
		Request["Session"] = static_cast<double>(Session);
		const auto Text = Request.ToString(false);
		if (Text.Len() > 4 * 1024 * 1024)
		{
			Error = ".NET request exceeds 4 MiB.";
			return false;
		}
		Context->Active = true;
		uint8* Response = nullptr;
		int Size = 0;
		const auto Status = Dispatch(reinterpret_cast<const uint8*>(Text.View().data()), Text.Len(),
									 Payload.data(), static_cast<int>(Payload.size()), &Response, &Size);
		Context->Active = false;
		struct FOutput
		{
			uint8* Bytes;
			~FOutput()
			{
				if (Bytes)
					FreeOutput(Bytes);
			}
		} Output{Response};
		if (Status || !Response || Size < 0 || Size > 4 * 1024 * 1024)
		{
			Error = ".NET bridge returned an invalid response.";
			return false;
		}
		auto Parsed =
			FJsonValue::Parse(std::string_view(reinterpret_cast<const char*>(Response), Size), &Error);
		if (!Parsed)
			return false;
		if (!Parsed->Get("Success").AsBool())
		{
			Error = Parsed->Get("Error").AsString(".NET script operation failed.");
			return false;
		}
		if (!ValidateScriptValue(Parsed->Get("Result"), Error))
			return false;
		Result = Parsed->Get("Result");
		Error.Empty();
		return true;
	}
	std::shared_ptr<FHostContext> Context;
	uint64 Token, Session = 0;
};
} // namespace
bool RegisterDotNetScriptProvider(FScriptRuntimeRegistry& Registry, FString& Error, const FString& Directory)
{
	FScriptProvider P;
	P.Language = "DotNet";
	P.GameplayTargets = {"Win64"};
	P.EditorTargets = {"Win64"};
	P.Create = [Directory](const auto&, auto Host, FString& Error) -> std::unique_ptr<IScriptRuntime> {
		auto R = std::make_unique<FDotNetScriptRuntime>(std::move(Host));
		if (!R->Initialize(Directory, Error))
			return {};
		return R;
	};
	return Registry.Register(std::move(P), Error);
}
