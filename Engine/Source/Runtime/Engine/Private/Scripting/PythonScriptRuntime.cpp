// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/PythonScriptRuntime.h"
#include "HAL/File.h"
#include "HAL/PlatformMisc.h"
#define Py_NO_LINK_LIB
#ifdef _DEBUG
#define GENESIS_RESTORE_DEBUG
#undef _DEBUG
#endif
#include "Python.h"
#undef PyCFunction_NewEx
#ifdef GENESIS_RESTORE_DEBUG
#define _DEBUG
#endif
#include <Windows.h>
#include <filesystem>
#include <mutex>
#include <thread>
#include <stdexcept>
namespace
{
#define PYTHON_API(X)                                                                                        \
	X(PyConfig_InitIsolatedConfig)                                                                           \
	X(PyConfig_SetString)                                                                                    \
	X(PyConfig_Clear) X(Py_InitializeFromConfig) X(Py_GetVersion) X(Py_IsInitialized) X(PyEval_SaveThread)   \
		X(PyEval_RestoreThread) X(PyThreadState_Swap) X(Py_NewInterpreterFromConfig) X(Py_EndInterpreter)    \
			X(PyDict_New) X(PyDict_SetItemString) X(PyDict_GetItemString) X(PyEval_GetBuiltins)              \
				X(PyRun_StringFlags) X(Py_DecRef) X(PyTuple_New) X(PyTuple_SetItem) X(PyTuple_GetItem)       \
					X(PyTuple_Size) X(PyUnicode_FromStringAndSize) X(PyUnicode_AsUTF8AndSize)                \
						X(PyObject_CallObject) X(PyObject_Str) X(PyErr_GetRaisedException)                   \
							X(PyErr_SetString) X(PyErr_Clear) X(PyCapsule_New) X(PyCapsule_GetPointer)       \
								X(PyCFunction_NewEx)
struct FPythonAPI
{
#define FIELD(Name) decltype(&::Name) Name = nullptr;
	PYTHON_API(FIELD)
#undef FIELD
	HMODULE Library = nullptr;
	PyObject* RuntimeError = nullptr;
	PyThreadState* Main = nullptr;
	std::thread::id Owner;
	FString Home;
};
FPythonAPI API;
std::mutex InitializationMutex;
thread_local bool PythonBusy = false;
FString ErrorText()
{
	auto* Exception = API.PyErr_GetRaisedException();
	if (!Exception)
		return "Python operation failed.";
	auto* Text = API.PyObject_Str(Exception);
	FString Message = "Python exception";
	if (Text)
	{
		Py_ssize_t Size = 0;
		const char* UTF8 = API.PyUnicode_AsUTF8AndSize(Text, &Size);
		if (UTF8)
			Message = FString(std::string(UTF8, std::min(size_t(Size), size_t(4096))));
		API.Py_DecRef(Text);
	}
	API.Py_DecRef(Exception);
	API.PyErr_Clear();
	return Message;
}
struct FPy
{
	PyObject* Value = nullptr;
	explicit FPy(PyObject* V = nullptr) : Value(V)
	{
	}
	~FPy()
	{
		if (Value)
			API.Py_DecRef(Value);
	}
	FPy(const FPy&) = delete;
	FPy& operator=(const FPy&) = delete;
};
struct FGIL
{
	explicit FGIL(PyThreadState* State)
	{
		PythonBusy = true;
		API.PyEval_RestoreThread(State);
	}
	~FGIL()
	{
		API.PyEval_SaveThread();
		PythonBusy = false;
	}
};
bool InitializePython(const FString& RequestedHome, FString& Error)
{
	std::lock_guard Lock(InitializationMutex);
	if (API.Main)
	{
		if (API.Owner != std::this_thread::get_id())
		{
			Error = "Python runtimes must be created on the engine scripting thread.";
			return false;
		}
		if (!RequestedHome.IsEmpty() &&
			FString::FromWide(
				std::filesystem::weakly_canonical(std::filesystem::path(RequestedHome.ToWide())).wstring()) !=
				API.Home)
		{
			Error = "Python is already initialized with a different runtime home.";
			return false;
		}
		return true;
	}
	FString Home = RequestedHome;
	if (Home.IsEmpty())
	{
		wchar_t Env[32768];
		const auto Size = GetEnvironmentVariableW(L"GENESIS_PYTHON_HOME", Env, std::size(Env));
		if (Size && Size < std::size(Env))
			Home = FString::FromWide(Env);
		else
			Home = FPaths::Combine(FPaths::ExecutableDir(), "../ThirdParty/Python");
	}
	const auto Root = std::filesystem::absolute(std::filesystem::path(Home.ToWide()));
	const auto Dll = Root / L"python314.dll";
	if (!std::filesystem::exists(Dll) || (!std::filesystem::exists(Root / L"python314.zip") &&
										  !std::filesystem::exists(Root / L"Lib/encodings")))
	{
		Error =
			"Python runtime unavailable. Run Engine/Build/SetupPythonRuntime.ps1 or set GENESIS_PYTHON_HOME.";
		return false;
	}
	if (!API.Library)
	{
		API.Library = LoadLibraryExW(Dll.c_str(), nullptr,
									 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!API.Library)
		{
			Error = "Cannot load the CPython runtime DLL.";
			return false;
		}
	}
#define LOAD(Name)                                                                                           \
	API.Name = reinterpret_cast<decltype(API.Name)>(GetProcAddress(API.Library, #Name));                     \
	if (!API.Name)                                                                                           \
	{                                                                                                        \
		Error = "Missing CPython embedding entry point: " #Name;                                             \
		return false;                                                                                        \
	}
	PYTHON_API(LOAD)
#undef LOAD
	const auto* Version = API.Py_GetVersion();
	if (!std::string_view(Version).starts_with("3.14.6 "))
	{
		Error = "Python adapter requires the pinned CPython 3.14.6 runtime.";
		return false;
	}
	auto** RuntimeError = reinterpret_cast<PyObject**>(GetProcAddress(API.Library, "PyExc_RuntimeError"));
	if (!RuntimeError)
	{
		Error = "Cannot resolve Python exception type.";
		return false;
	}
	API.RuntimeError = *RuntimeError;
	if (API.Py_IsInitialized())
	{
		Error = "CPython was already initialized by another host.";
		return false;
	}
	PyConfig Config;
	API.PyConfig_InitIsolatedConfig(&Config);
	Config.site_import = 0;
	Config.install_signal_handlers = 0;
	Config.parse_argv = 0;
	auto Status = API.PyConfig_SetString(&Config, &Config.home, Root.c_str());
	if (Status._type == 0)
		Status = API.Py_InitializeFromConfig(&Config);
	if (Status._type != 0)
		Error = Status.err_msg ? Status.err_msg : "Cannot initialize CPython.";
	API.PyConfig_Clear(&Config);
	if (Status._type != 0)
		return false;
	API.Owner = std::this_thread::get_id();
	API.Home = FString::FromWide(std::filesystem::weakly_canonical(Root).wstring());
	API.Main = API.PyEval_SaveThread();
	Error.Empty();
	return true;
}
const char* Bootstrap = R"PY(
import json as _json
import sys as _sys
import types as _types
_modules = {}

def _host_invoke(api, args):
    text = _json.dumps(args, ensure_ascii=False, allow_nan=False, separators=(',', ':'))
    return _json.loads(_genesis_native_invoke(api, text))

genesis = _types.ModuleType('genesis')
genesis.invoke = _host_invoke
_sys.modules['genesis'] = genesis

def _budget_call(budget, fn, *args):
    remaining = budget
    def trace(frame, event, arg):
        nonlocal remaining
        frame.f_trace_opcodes = True
        if event == 'opcode':
            remaining -= 1
            if remaining <= 0:
                raise RuntimeError('Python instruction budget exceeded')
        return trace
    previous = _sys.gettrace()
    _sys.settrace(trace)
    try:
        return fn(*args)
    finally:
        _sys.settrace(previous)

def _load(name, source, budget):
    if name not in _modules and len(_modules) >= 256:
        raise RuntimeError('Python module limit exceeded')
    module = _types.ModuleType(name)
    module.__dict__['genesis'] = genesis
    _budget_call(budget, exec, compile(source, name, 'exec'), module.__dict__, module.__dict__)
    _modules[name] = module
    _sys.modules[name] = module
    return ''

def _call(name, function, arguments, budget):
    module = _modules[name]
    fn = module.__dict__[function]
    value = _budget_call(budget, fn, _json.loads(arguments))
    return _json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(',', ':'))
)PY";
class FPythonScriptRuntime final : public IScriptRuntime
{
  public:
	FPythonScriptRuntime(const FScriptRuntimeRequest& R, std::shared_ptr<IScriptHost> H)
		: Host(std::move(H)), Budget(R.InstructionBudget), Owner(std::this_thread::get_id())
	{
	}
	~FPythonScriptRuntime() override
	{
		if (State)
		{
			PythonBusy = true;
			API.PyEval_RestoreThread(State);
			Closing = true;
			if (Globals)
				API.Py_DecRef(Globals);
			API.Py_EndInterpreter(State);
			API.PyThreadState_Swap(API.Main);
			API.PyEval_SaveThread();
			PythonBusy = false;
		}
	}
	bool Initialize(const FString& Home, FString& Error)
	{
		if (PythonBusy)
		{
			Error = "Python runtime creation cannot be reentrant.";
			return false;
		}
		if (Budget < 1000 || Budget > 1000000000)
		{
			Error = "Python instruction budget must be 1000-1000000000.";
			return false;
		}
		if (!InitializePython(Home, Error))
			return false;
		PythonBusy = true;
		API.PyEval_RestoreThread(API.Main);
		PyInterpreterConfig Config{};
		Config.use_main_obmalloc = 0;
		Config.allow_fork = 0;
		Config.allow_exec = 0;
		Config.allow_threads = 0;
		Config.allow_daemon_threads = 0;
		Config.check_multi_interp_extensions = 1;
		Config.gil = PyInterpreterConfig_SHARED_GIL;
		const auto Status = API.Py_NewInterpreterFromConfig(&State, &Config);
		if (Status._type != 0)
		{
			Error = Status.err_msg ? Status.err_msg : "Cannot create Python subinterpreter.";
			API.PyThreadState_Swap(API.Main);
			API.PyEval_SaveThread();
			PythonBusy = false;
			return false;
		}
		bool Success = false;
		{
			Globals = API.PyDict_New();
			FPy Self(API.PyCapsule_New(this, "Genesis.PythonRuntime", nullptr));
			static PyMethodDef Method = {"invoke", NativeInvoke, METH_VARARGS,
										 "Invoke a Genesis host binding."};
			FPy Native(Self.Value ? API.PyCFunction_NewEx(&Method, Self.Value, nullptr) : nullptr);
			if (Globals && Native.Value &&
				API.PyDict_SetItemString(Globals, "__builtins__", API.PyEval_GetBuiltins()) == 0 &&
				API.PyDict_SetItemString(Globals, "_genesis_native_invoke", Native.Value) == 0)
			{
				FPy Result(API.PyRun_StringFlags(Bootstrap, Py_file_input, Globals, Globals, nullptr));
				Success = Result.Value != nullptr;
			}
			if (!Success)
				Error = ErrorText();
		}
		API.PyEval_SaveThread();
		PythonBusy = false;
		return Success;
	}
	bool LoadModule(const FString& Name, TArrayView<const uint8> Payload, FString& Error) override
	{
		if (!Check(Error))
			return false;
		if (!ValidName(Name) || Payload.empty() || Payload.size() > 4 * 1024 * 1024)
		{
			Error = "Python requires a module name and UTF-8 source payload up to 4 MiB.";
			return false;
		}
		FGIL Lock(State);
		FPy Args(API.PyTuple_New(3));
		if (!Args.Value)
		{
			Error = ErrorText();
			return false;
		}
		if (!SetString(Args.Value, 0, Name.View()) ||
			!SetString(Args.Value, 1,
					   std::string_view(reinterpret_cast<const char*>(Payload.data()), Payload.size())))
		{
			Error = ErrorText();
			return false;
		}
		// Budget is decoded by a small wrapper rather than depending on Python integer layout.
		FPy BudgetValue(
			API.PyRun_StringFlags(std::to_string(Budget).c_str(), Py_eval_input, Globals, Globals, nullptr));
		if (!BudgetValue.Value)
		{
			Error = ErrorText();
			return false;
		}
		API.PyTuple_SetItem(Args.Value, 2, BudgetValue.Value);
		BudgetValue.Value = nullptr;
		FPy Result(API.PyObject_CallObject(API.PyDict_GetItemString(Globals, "_load"), Args.Value));
		if (!Result.Value)
		{
			Error = "Python module " + Name + ": " + ErrorText();
			return false;
		}
		Error.Empty();
		return true;
	}
	bool Call(const FString& Module, const FString& Function, const FJsonValue& Arguments, FJsonValue& Result,
			  FString& Error) override
	{
		if (!Check(Error))
			return false;
		if (!ValidName(Module) || !ValidName(Function))
		{
			Error = "Invalid Python module/function name.";
			return false;
		}
		if (!ValidateScriptValue(Arguments, Error))
			return false;
		const auto Text = Arguments.ToString(false);
		if (Text.Len() > 4 * 1024 * 1024)
		{
			Error = "Python call arguments exceed 4 MiB.";
			return false;
		}
		FGIL Lock(State);
		FPy Args(API.PyTuple_New(4));
		if (!Args.Value)
		{
			Error = ErrorText();
			return false;
		}
		if (!SetString(Args.Value, 0, Module.View()) || !SetString(Args.Value, 1, Function.View()) ||
			!SetString(Args.Value, 2, Text.View()))
		{
			Error = ErrorText();
			return false;
		}
		FPy BudgetValue(
			API.PyRun_StringFlags(std::to_string(Budget).c_str(), Py_eval_input, Globals, Globals, nullptr));
		if (!BudgetValue.Value)
		{
			Error = ErrorText();
			return false;
		}
		API.PyTuple_SetItem(Args.Value, 3, BudgetValue.Value);
		BudgetValue.Value = nullptr;
		FPy Value(API.PyObject_CallObject(API.PyDict_GetItemString(Globals, "_call"), Args.Value));
		if (!Value.Value)
		{
			Error = "Python " + Module + "." + Function + ": " + ErrorText();
			return false;
		}
		Py_ssize_t Size = 0;
		const char* UTF8 = API.PyUnicode_AsUTF8AndSize(Value.Value, &Size);
		if (!UTF8)
		{
			Error = ErrorText();
			return false;
		}
		if (Size > 4 * 1024 * 1024)
		{
			Error = "Python result exceeds 4 MiB.";
			return false;
		}
		auto Parsed = FJsonValue::Parse(std::string_view(UTF8, Size), &Error);
		if (!Parsed || !ValidateScriptValue(*Parsed, Error))
			return false;
		Result = std::move(*Parsed);
		Error.Empty();
		return true;
	}

  private:
	static bool ValidName(const FString& S)
	{
		return !S.IsEmpty() && S.Len() <= 128 && S.View().find('\0') == std::string_view::npos;
	}
	bool Check(FString& Error)
	{
		if (Owner != std::this_thread::get_id())
		{
			Error = "Python runtime calls must run on the owning thread.";
			return false;
		}
		if (PythonBusy)
		{
			Error = "Python runtime calls cannot reenter the Python interpreter.";
			return false;
		}
		return true;
	}
	static bool SetString(PyObject* Tuple, int Index, std::string_view Value)
	{
		auto* S = API.PyUnicode_FromStringAndSize(Value.data(), Value.size());
		return S && API.PyTuple_SetItem(Tuple, Index, S) == 0;
	}
	static PyObject* NativeInvoke(PyObject* Self, PyObject* Args)
	{
		try
		{
			auto* Runtime =
				static_cast<FPythonScriptRuntime*>(API.PyCapsule_GetPointer(Self, "Genesis.PythonRuntime"));
			if (!Runtime || Runtime->Closing || API.PyTuple_Size(Args) != 2)
				throw std::runtime_error("Invalid Genesis host invocation.");
			Py_ssize_t NameSize = 0, Size = 0;
			const char* Name = API.PyUnicode_AsUTF8AndSize(API.PyTuple_GetItem(Args, 0), &NameSize);
			const char* Text = API.PyUnicode_AsUTF8AndSize(API.PyTuple_GetItem(Args, 1), &Size);
			if (!Name || !Text || NameSize <= 0 || NameSize > 128 || Size > 4 * 1024 * 1024)
				throw std::runtime_error("Invalid host API name or JSON arguments.");
			FString Error;
			auto Arguments = FJsonValue::Parse(std::string_view(Text, Size), &Error);
			if (!Arguments)
				throw std::runtime_error(Error.ToAnsi());
			FJsonValue Result;
			if (!Runtime->Host->Invoke(FString(std::string(Name, NameSize)), *Arguments, Result, Error))
				throw std::runtime_error(Error.IsEmpty() ? "Script host call failed." : Error.ToAnsi());
			if (!ValidateScriptValue(Result, Error))
				throw std::runtime_error(Error.ToAnsi());
			const auto Response = Result.ToString(false);
			if (Response.Len() > 4 * 1024 * 1024)
				throw std::runtime_error("Host response exceeds 4 MiB.");
			return API.PyUnicode_FromStringAndSize(Response.View().data(), Response.Len());
		}
		catch (const std::exception& E)
		{
			API.PyErr_Clear();
			API.PyErr_SetString(API.RuntimeError, E.what());
			return nullptr;
		}
		catch (...)
		{
			API.PyErr_Clear();
			API.PyErr_SetString(API.RuntimeError, "Host binding failed with an unknown exception.");
			return nullptr;
		}
	}
	std::shared_ptr<IScriptHost> Host;
	PyThreadState* State = nullptr;
	PyObject* Globals = nullptr;
	uint64 Budget;
	std::thread::id Owner;
	bool Closing = false;
};
} // namespace
bool RegisterPythonScriptProvider(FScriptRuntimeRegistry& Registry, FString& Error, const FString& Home)
{
	FScriptProvider P;
	P.Language = "Python";
	P.GameplayTargets = {"Win64"};
	P.EditorTargets = {"Win64"};
	P.Create = [Home](const auto& Request, auto Host, FString& Error) -> std::unique_ptr<IScriptRuntime> {
		auto R = std::make_unique<FPythonScriptRuntime>(Request, std::move(Host));
		if (!R->Initialize(Home, Error))
			return {};
		return R;
	};
	return Registry.Register(std::move(P), Error);
}
