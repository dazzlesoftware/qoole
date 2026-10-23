// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/DotNetScriptRuntime.h"
#include "Internationalization/TextLocalization.h"
#include "Scripting/ScriptLimits.h"
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
#include <exception>
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
		DWORD Size = GetEnvironmentVariableW(Name, Buffer, std::size(Buffer));
		return Size && Size < std::size(Buffer) ? FString::FromWide(Buffer) : FString();
	}
	bool InitializeBridge(const FString& Requested, FString& Error)
	{
		std::lock_guard Lock(InitializationMutex);
		const FString Default = FPaths::Combine(FPaths::ExecutableDir(), "../ThirdParty/DotNet");
		const std::filesystem::path Directory =
		    std::filesystem::absolute(std::filesystem::path((Requested.IsEmpty() ? Default : Requested).ToWide()));
		const std::filesystem::path Bridge = Directory / L"Genesis.ScriptBridge.dll",
		                            Config = Directory / L"Genesis.ScriptBridge.runtimeconfig.json";
		if (Dispatch)
		{
			if (LoadedBridge != FString::FromWide(Bridge.wstring()))
			{
				Error = GENESIS_TEXT("A different .NET script bridge is already loaded.");
				return false;
			}
			return true;
		}
		if (!std::filesystem::exists(Bridge) || !std::filesystem::exists(Config))
		{
			Error = GENESIS_TEXT(".NET script bridge unavailable. Run Engine/Build/SetupScriptingRuntimes.ps1.");
			return false;
		}
		FString RootString = Environment(L"GENESIS_DOTNET_ROOT");
		if (RootString.IsEmpty())
		{
			const std::filesystem::path Bundled = Directory / L"Runtime";
			RootString = std::filesystem::exists(Bundled / L"host/fxr/10.0.12/hostfxr.dll")
			    ? FString::FromWide(Bundled.wstring())
			    : FPaths::Combine(Environment(L"ProgramFiles"), "dotnet");
		}
		const std::filesystem::path Root = std::filesystem::absolute(std::filesystem::path(RootString.ToWide()));
		const std::filesystem::path Host = Root / L"host/fxr/10.0.12/hostfxr.dll";
		if (!std::filesystem::exists(Host))
		{
			Error = GENESIS_TEXT(".NET 10.0.12 host unavailable. Install the runtime or set GENESIS_DOTNET_ROOT.");
			return false;
		}
		if (!HostLibrary)
			HostLibrary = LoadLibraryExW(
			    Host.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!HostLibrary)
		{
			Error = GENESIS_TEXT("Cannot load .NET hostfxr.");
			return false;
		}
		hostfxr_initialize_for_runtime_config_fn Initialize =
		    reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
		        GetProcAddress(HostLibrary, "hostfxr_initialize_for_runtime_config"));
		hostfxr_get_runtime_delegate_fn GetDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
		    GetProcAddress(HostLibrary, "hostfxr_get_runtime_delegate"));
		hostfxr_close_fn Close = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(HostLibrary, "hostfxr_close"));
		if (!Initialize || !GetDelegate || !Close)
		{
			Error = GENESIS_TEXT("Missing .NET hosting entry points.");
			return false;
		}
		hostfxr_handle Context = nullptr;
		hostfxr_initialize_parameters Parameters{sizeof(hostfxr_initialize_parameters), nullptr, Root.c_str()};
		int Status = Initialize(Config.c_str(), &Parameters, &Context);
		if (Status < 0 || !Context)
		{
			Error = FTextLocalization::Format("Cannot initialize .NET runtime ({}).", Status);
			if (Context)
				Close(Context);
			return false;
		}
		load_assembly_and_get_function_pointer_fn Load = nullptr;
		Status = GetDelegate(Context, hdt_load_assembly_and_get_function_pointer, reinterpret_cast<void**>(&Load));
		Close(Context);
		if (Status < 0 || !Load)
		{
			Error = GENESIS_TEXT("Cannot get .NET assembly loader.");
			return false;
		}
		Status = Load(Bridge.c_str(), L"Genesis.Scripting.ScriptBridge, Genesis.ScriptBridge", L"Dispatch",
		    UNMANAGEDCALLERSONLY_METHOD, nullptr, reinterpret_cast<void**>(&Dispatch));
		if (Status < 0 || !Dispatch)
		{
			Error = GENESIS_TEXT("Cannot load .NET script bridge entry point.");
			Dispatch = nullptr;
			return false;
		}
		Status = Load(Bridge.c_str(), L"Genesis.Scripting.ScriptBridge, Genesis.ScriptBridge", L"FreeOutput",
		    UNMANAGEDCALLERSONLY_METHOD, nullptr, reinterpret_cast<void**>(&FreeOutput));
		if (Status < 0 || !FreeOutput)
		{
			Error = GENESIS_TEXT("Cannot load .NET script bridge release entry point.");
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
				Success = [&]() -> bool
				{
					std::shared_ptr<FHostContext> Host;
					{
						std::lock_guard Lock(ContextMutex);
						const std::map<uint64, std::weak_ptr<FHostContext>>::iterator It =
						    Contexts.find(static_cast<uint64>(Context));
						if (It != Contexts.end())
							Host = It->second.lock();
					}
					if (!Host || Host->Owner != std::this_thread::get_id() || !Host->Active)
					{
						Error = GENESIS_TEXT("Host call requires an active script session on the owning thread.");
						return false;
					}
					if (!Bytes || Size < 0 || Size > ScriptLimits::MaxTransferBytes)
					{
						Error = GENESIS_TEXT("Invalid host request size.");
						return false;
					}
					TOptional<FJsonValue> Request =
					    FJsonValue::Parse(std::string_view(reinterpret_cast<const char*>(Bytes), Size), &Error);
					if (!Request || !Request->IsObject() || Request->Get("API").GetType() != EJsonType::String ||
					    Request->Get("API").AsString().IsEmpty() ||
					    Request->Get("API").AsString().Len() > ScriptLimits::MaxNameLength ||
					    Request->Get("API").AsString().View().find('\0') != std::string_view::npos)
					{
						Error = GENESIS_TEXT("Invalid host request.");
						return false;
					}
					if (!ValidateScriptValue(Request->Get("Arguments"), Error))
					{
						return false;
					}
					const bool Invoked =
					    Host->Host->Invoke(Request->Get("API").AsString(), Request->Get("Arguments"), Result, Error);
					if (Invoked && !ValidateScriptValue(Result, Error))
					{
						return false;
					}
					if (!Invoked && Error.IsEmpty())
						Error = GENESIS_TEXT("Script host call failed.");
					return Invoked;
				}();
			}
			catch (const std::exception& E)
			{
				Error = E.what();
			}
			catch (...)
			{
				Error = GENESIS_TEXT("Host binding failed with an unknown exception.");
			}
			FString Text = Success ? Result.ToString(false) : Error;
			if (Text.Len() > ScriptLimits::MaxTransferBytes)
			{
				Text = GENESIS_TEXT("Host response exceeds 4 MiB.");
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
				FJsonValue Request = FJsonValue::MakeObject();
				Request["Operation"] = "Dispose";
				Exchange(Request, {}, Result, Error);
			}
		}
		bool Initialize(const FString& Directory, FString& Error)
		{
			if (!InitializeBridge(Directory, Error))
				return false;
			FJsonValue Request = FJsonValue::MakeObject();
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
				Error = GENESIS_TEXT("Invalid .NET script session.");
				return false;
			}
			return true;
		}
		bool LoadModule(const FString& Name, TArrayView<const uint8> Payload, FString& Error) override
		{
			if (!Check(Error))
				return false;
			if (!ValidName(Name) || Payload.empty() || Payload.size() > ScriptLimits::MaxTransferBytes)
			{
				Error = GENESIS_TEXT(".NET requires a module name and compiled assembly payload up to 4 MiB.");
				return false;
			}
			FJsonValue Request = FJsonValue::MakeObject();
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
				Error = GENESIS_TEXT("Invalid .NET module or function name.");
				return false;
			}
			if (!ValidateScriptValue(Arguments, Error))
				return false;
			FJsonValue Request = FJsonValue::MakeObject();
			Request["Operation"] = "Call";
			Request["Module"] = Module;
			Request["Function"] = Function;
			Request["Arguments"] = Arguments;
			return Exchange(Request, {}, Result, Error);
		}

	private:
		static bool ValidName(const FString& S)
		{
			return !S.IsEmpty() && S.Len() <= ScriptLimits::MaxNameLength &&
			    S.View().find('\0') == std::string_view::npos;
		}
		bool Check(FString& Error)
		{
			if (Context->Owner != std::this_thread::get_id())
			{
				Error = GENESIS_TEXT(".NET runtime calls must run on the owning thread.");
				return false;
			}
			if (Context->Active)
			{
				Error = GENESIS_TEXT(".NET runtime calls cannot be reentrant.");
				return false;
			}
			return true;
		}
		bool Exchange(FJsonValue Request, TArrayView<const uint8> Payload, FJsonValue& Result, FString& Error)
		{
			Request["Session"] = static_cast<double>(Session);
			const FString Text = Request.ToString(false);
			if (Text.Len() > ScriptLimits::MaxTransferBytes)
			{
				Error = GENESIS_TEXT(".NET request exceeds 4 MiB.");
				return false;
			}
			Context->Active = true;
			uint8* Response = nullptr;
			int Size = 0;
			const int Status = Dispatch(reinterpret_cast<const uint8*>(Text.View().data()), Text.Len(), Payload.data(),
			    static_cast<int>(Payload.size()), &Response, &Size);
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
			if (Status || !Response || Size < 0 || Size > ScriptLimits::MaxTransferBytes)
			{
				Error = GENESIS_TEXT(".NET bridge returned an invalid response.");
				return false;
			}
			TOptional<FJsonValue> Parsed =
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
	P.Create = [Directory](const FScriptRuntimeRequest&, std::shared_ptr<IScriptHost> Host,
	               FString& Error) -> std::unique_ptr<IScriptRuntime>
	{
		std::unique_ptr<FDotNetScriptRuntime> R = std::make_unique<FDotNetScriptRuntime>(std::move(Host));
		if (!R->Initialize(Directory, Error))
			return {};
		return R;
	};
	return Registry.Register(std::move(P), Error);
}
