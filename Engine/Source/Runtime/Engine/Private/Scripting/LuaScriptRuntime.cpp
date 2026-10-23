// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/LuaScriptRuntime.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
namespace
{
char ArrayTag, ObjectTag, NullTag;
void Require(bool Value, const char* Message)
{
	if (!Value)
		throw std::runtime_error(Message);
}
// All entry points run under lua_pcall. Lua is built as C++ so raising a Lua
// error safely unwinds FJsonValue/string/container temporaries in bindings.
template <class F> int Guard(lua_State* L, F&& Function)
{
	try
	{
		return Function();
	}
	catch (const std::exception& E)
	{
		lua_pushstring(L, E.what());
	}
	return lua_error(L);
}
void SetTag(lua_State* L, const void* Tag)
{
	lua_rawgetp(L, LUA_REGISTRYINDEX, Tag);
	lua_setmetatable(L, -2);
}
bool HasTag(lua_State* L, int Index, const void* Tag)
{
	if (!lua_getmetatable(L, Index))
		return false;
	lua_rawgetp(L, LUA_REGISTRYINDEX, Tag);
	const bool Same = lua_rawequal(L, -1, -2);
	lua_pop(L, 2);
	return Same;
}
void PushJsonValue(lua_State* L, const FJsonValue& V, int& Nodes, size_t& Bytes, int Depth = 0)
{
	Require(++Nodes <= 100000 && (Bytes += 16) <= 4 * 1024 * 1024, "Script JSON exceeds transfer limits.");
	Require(Depth <= 64, "Script JSON exceeds maximum depth.");
	Require(lua_checkstack(L, 8), "Script stack exhausted.");
	switch (V.GetType())
	{
	case EJsonType::Null:
		lua_pushlightuserdata(L, &NullTag);
		break;
	case EJsonType::Bool:
		lua_pushboolean(L, V.AsBool());
		break;
	case EJsonType::Number:
		Require(std::isfinite(V.AsNumber()), "Script JSON requires finite numbers.");
		lua_pushnumber(L, V.AsNumber());
		break;
	case EJsonType::String: {
		const auto S = V.AsString();
		Require((Bytes += S.Len()) <= 4 * 1024 * 1024, "Script JSON exceeds transfer limits.");
		lua_pushlstring(L, S.View().data(), S.Len());
		break;
	}
	case EJsonType::Array:
		Require(V.Num() <= 100000, "Script array is too large.");
		lua_createtable(L, V.Num(), 0);
		SetTag(L, &ArrayTag);
		for (int I = 0; I < V.Num(); ++I)
		{
			PushJsonValue(L, V[I], Nodes, Bytes, Depth + 1);
			lua_rawseti(L, -2, I + 1);
		}
		break;
	case EJsonType::Object:
		Require(V.Num() <= 100000, "Script object is too large.");
		lua_createtable(L, 0, V.Num());
		SetTag(L, &ObjectTag);
		for (const auto& Key : V.GetKeys())
		{
			Require((Bytes += Key.Len()) <= 4 * 1024 * 1024, "Script JSON exceeds transfer limits.");
			lua_pushlstring(L, Key.View().data(), Key.Len());
			PushJsonValue(L, V.Get(Key.View()), Nodes, Bytes, Depth + 1);
			lua_rawset(L, -3);
		}
		break;
	}
}
void PushJson(lua_State* L, const FJsonValue& V)
{
	int Nodes = 0;
	size_t Bytes = 0;
	PushJsonValue(L, V, Nodes, Bytes);
}
FJsonValue ReadJson(lua_State* L, int Index, std::set<const void*>& Active, int& Count, size_t& Bytes,
					int Depth = 0)
{
	Require(lua_checkstack(L, 8), "Script stack exhausted.");
	Require((Bytes += 16) <= 4 * 1024 * 1024, "Script JSON exceeds transfer limits.");
	Require(Depth <= 64 && ++Count <= 100000, "Script result exceeds JSON limits.");
	Index = lua_absindex(L, Index);
	switch (lua_type(L, Index))
	{
	case LUA_TNIL:
		return {};
	case LUA_TLIGHTUSERDATA:
		Require(lua_touserdata(L, Index) == &NullTag, "Native pointers cannot cross script bindings.");
		return {};
	case LUA_TBOOLEAN:
		return FJsonValue(lua_toboolean(L, Index) != 0);
	case LUA_TNUMBER: {
		const double V = lua_tonumber(L, Index);
		Require(std::isfinite(V), "Script result requires finite numbers.");
		return FJsonValue(V);
	}
	case LUA_TSTRING: {
		size_t Size = 0;
		const char* S = lua_tolstring(L, Index, &Size);
		Require((Bytes += Size) <= 4 * 1024 * 1024, "Script JSON exceeds transfer limits.");
		return FJsonValue(FString(std::string(S, Size)));
	}
	case LUA_TTABLE: {
		const void* Identity = lua_topointer(L, Index);
		Require(Active.insert(Identity).second, "Script result contains a cycle.");
		bool Array = HasTag(L, Index, &ArrayTag);
		const bool Object = HasTag(L, Index, &ObjectTag);
		size_t Numeric = 0, Strings = 0, Max = 0;
		lua_pushnil(L);
		while (lua_next(L, Index))
		{
			Require(++Count <= 100000, "Script result exceeds JSON limits.");
			if (lua_type(L, -2) == LUA_TSTRING)
				++Strings;
			else
			{
				Require(lua_isinteger(L, -2) && lua_tointeger(L, -2) > 0 && lua_tointeger(L, -2) <= 100000,
						"JSON object keys must be strings; array indices must be dense positive integers.");
				++Numeric;
				Max = std::max(Max, static_cast<size_t>(lua_tointeger(L, -2)));
			}
			lua_pop(L, 1);
		}
		Require(!Strings || !Numeric, "Mixed Lua table keys cannot represent JSON.");
		Require(!Object || !Numeric, "JSON objects require string keys.");
		if (Numeric)
			Array = true;
		Require(!Array || (!Strings && Numeric == Max), "JSON arrays require dense integer indices.");
		auto Result = Array ? FJsonValue::MakeArray() : FJsonValue::MakeObject();
		if (Array)
		{
			for (size_t I = 1; I <= Max; ++I)
			{
				lua_rawgeti(L, Index, I);
				Result.Push(ReadJson(L, -1, Active, Count, Bytes, Depth + 1));
				lua_pop(L, 1);
			}
		}
		else
		{
			lua_pushnil(L);
			while (lua_next(L, Index))
			{
				size_t Size = 0;
				const char* Key = lua_tolstring(L, -2, &Size);
				Require((Bytes += Size) <= 4 * 1024 * 1024, "Script JSON exceeds transfer limits.");
				Result[std::string_view(Key, Size)] = ReadJson(L, -1, Active, Count, Bytes, Depth + 1);
				lua_pop(L, 1);
			}
		}
		Active.erase(Identity);
		return Result;
	}
	default:
		throw std::runtime_error("Script result contains a function, thread or unsupported userdata.");
	}
}
FJsonValue ReadJson(lua_State* L, int Index)
{
	std::set<const void*> Active;
	int Count = 0;
	size_t Bytes = 0;
	return ReadJson(L, Index, Active, Count, Bytes);
}
class FLuaScriptRuntime final : public IScriptRuntime
{
  public:
	FLuaScriptRuntime(const FScriptRuntimeRequest& Request, std::shared_ptr<IScriptHost> InHost)
		: Host(std::move(InHost)), MemoryLimit(Request.MemoryLimitBytes), Budget(Request.InstructionBudget),
		  Owner(std::this_thread::get_id())
	{
	}
	~FLuaScriptRuntime() override
	{
		if (State)
		{
			bClosing = true;
			Remaining = Budget;
			lua_sethook(State, Hook, LUA_MASKCOUNT, 1000);
			lua_close(State);
		}
	}
	bool Initialize(FString& Error)
	{
		if (MemoryLimit < 256 * 1024 || MemoryLimit > 1024ull * 1024 * 1024 || Budget < 1000 ||
			Budget > 1000000000)
		{
			Error = "Lua limits require 256 KiB-1 GiB memory and 1000-1000000000 instructions.";
			return false;
		}
		State = lua_newstate(Allocate, this);
		if (!State)
		{
			Error = "Cannot allocate Lua VM.";
			return false;
		}
		*static_cast<FLuaScriptRuntime**>(lua_getextraspace(State)) = this;
		return Run(Bootstrap, nullptr, Error);
	}
	bool LoadModule(const FString& Name, TArrayView<const uint8> Payload, FString& Error) override
	{
		if (!Check(Error))
			return false;
		if ((!Modules.contains(Name) && Modules.size() >= 256) || !ValidName(Name) || Payload.empty() ||
			Payload.size() > 4 * 1024 * 1024)
		{
			Error = "Lua requires a module name and source payload of at most 4 MiB.";
			return false;
		}
		FLoad Job{this, Name, Payload};
		if (!Run(Load, &Job, Error))
		{
			Error = "Lua module " + Name + ": " + Error;
			return false;
		}
		auto It = Modules.find(Name);
		if (It != Modules.end())
			luaL_unref(State, LUA_REGISTRYINDEX, It->second);
		Modules[Name] = Job.Reference;
		return true;
	}
	bool Call(const FString& Module, const FString& Function, const FJsonValue& Arguments, FJsonValue& Result,
			  FString& Error) override
	{
		if (!Check(Error))
			return false;
		const auto It = Modules.find(Module);
		if (It == Modules.end() || !ValidName(Function))
		{
			Error = "Lua module or function is unavailable.";
			return false;
		}
		FCall Job{this, It->second, Function, Arguments};
		if (!Run(CallFunction, &Job, Error))
		{
			Error = "Lua " + Module + "." + Function + ": " + Error;
			return false;
		}
		Result = std::move(Job.Result);
		return true;
	}

  private:
	struct FLoad
	{
		FLuaScriptRuntime* Runtime;
		FString Name;
		TArrayView<const uint8> Payload;
		int Reference = LUA_NOREF;
	};
	struct FCall
	{
		FLuaScriptRuntime* Runtime;
		int Reference;
		FString Function;
		const FJsonValue& Arguments;
		FJsonValue Result;
	};
	static bool ValidName(const FString& Name)
	{
		return !Name.IsEmpty() && Name.Len() <= 128 && Name.View().find('\0') == std::string_view::npos;
	}
	bool Check(FString& Error)
	{
		if (std::this_thread::get_id() != Owner)
		{
			Error = "Lua runtime must execute on its owning thread.";
			return false;
		}
		if (bRunning)
		{
			Error = "Lua runtime calls cannot be reentrant.";
			return false;
		}
		return true;
	}
	bool Run(lua_CFunction Function, void* Job, FString& Error)
	{
		bRunning = true;
		Remaining = Budget;
		lua_sethook(State, Hook, LUA_MASKCOUNT, 1000);
		lua_pushcfunction(State, Function);
		lua_pushlightuserdata(State, Job);
		const int Status = lua_pcall(State, 1, 0, 0);
		lua_sethook(State, nullptr, 0, 0);
		bRunning = false;
		if (Status != LUA_OK)
		{
			size_t Size = 0;
			const char* Text = lua_type(State, -1) == LUA_TSTRING ? lua_tolstring(State, -1, &Size) : nullptr;
			Error = Text ? FString(std::string(Text, std::min(Size, size_t(4096))))
						 : FString("Lua execution failed.");
			lua_settop(State, 0);
			return false;
		}
		lua_settop(State, 0);
		Error.Empty();
		return true;
	}
	static FLuaScriptRuntime& Runtime(lua_State* L)
	{
		return **static_cast<FLuaScriptRuntime**>(lua_getextraspace(L));
	}
	static void Hook(lua_State* L, lua_Debug*)
	{
		auto& R = Runtime(L);
		if (R.Remaining <= 1000)
			luaL_error(L, "Lua instruction budget exceeded.");
		R.Remaining -= 1000;
	}
	static void* Allocate(void* User, void* Pointer, size_t OldSize, size_t NewSize)
	{
		auto& R = *static_cast<FLuaScriptRuntime*>(User);
		const size_t Old = Pointer ? OldSize : 0;
		if (!NewSize)
		{
			std::free(Pointer);
			R.MemoryUsed -= Old;
			return nullptr;
		}
		if (NewSize > R.MemoryLimit || R.MemoryUsed - Old > R.MemoryLimit - NewSize)
			return nullptr;
		void* New = std::realloc(Pointer, NewSize);
		if (New)
			R.MemoryUsed = R.MemoryUsed - Old + NewSize;
		return New;
	}
	static int Bootstrap(lua_State* L)
	{
		return Guard(L, [&] {
			for (const auto& Lib : std::initializer_list<std::pair<const char*, lua_CFunction>>{
					 {LUA_GNAME, luaopen_base},
					 {LUA_TABLIBNAME, luaopen_table},
					 {LUA_STRLIBNAME, luaopen_string},
					 {LUA_MATHLIBNAME, luaopen_math},
					 {LUA_UTF8LIBNAME, luaopen_utf8}})
			{
				luaL_requiref(L, Lib.first, Lib.second, 1);
				lua_pop(L, 1);
			}
			for (const char* Name : {"dofile", "loadfile", "load", "print", "warn"})
			{
				lua_pushnil(L);
				lua_setglobal(L, Name);
			}
			for (const void* Tag : {static_cast<void*>(&ArrayTag), static_cast<void*>(&ObjectTag)})
			{
				lua_newtable(L);
				lua_pushliteral(L, "Genesis JSON container");
				lua_setfield(L, -2, "__metatable");
				lua_rawsetp(L, LUA_REGISTRYINDEX, Tag);
			}
			lua_pushcfunction(L, SetMetatable);
			lua_setglobal(L, "setmetatable");
			lua_newtable(L);
			lua_pushcfunction(L, Invoke);
			lua_setfield(L, -2, "invoke");
			lua_pushcfunction(L, Array);
			lua_setfield(L, -2, "array");
			lua_pushlightuserdata(L, &NullTag);
			lua_setfield(L, -2, "null");
			lua_setglobal(L, "Genesis");
			return 0;
		});
	}
	static int SetMetatable(lua_State* L)
	{
		return Guard(L, [&] {
			Require(lua_istable(L, 1) && (lua_isnoneornil(L, 2) || lua_istable(L, 2)),
					"setmetatable requires a table and table/nil metatable.");
			if (lua_getmetatable(L, 1))
			{
				lua_pushliteral(L, "__metatable");
				lua_rawget(L, -2);
				const bool Protected = !lua_isnil(L, -1);
				lua_pop(L, 2);
				Require(!Protected, "Cannot change a protected metatable.");
			}
			if (lua_istable(L, 2))
				for (const char* Key : {"__gc", "__close"})
				{
					lua_pushstring(L, Key);
					lua_rawget(L, 2);
					const bool HasFinalizer = !lua_isnil(L, -1);
					lua_pop(L, 1);
					Require(!HasFinalizer,
							"Script finalizers are unavailable; use explicit lifecycle callbacks.");
				}
			lua_settop(L, 2);
			lua_setmetatable(L, 1);
			lua_pushvalue(L, 1);
			return 1;
		});
	}
	static int Array(lua_State* L)
	{
		return Guard(L, [&] {
			Require(lua_istable(L, 1), "Genesis.array requires a table.");
			lua_settop(L, 1);
			SetTag(L, &ArrayTag);
			return 1;
		});
	}
	static int Invoke(lua_State* L)
	{
		return Guard(L, [&] {
			Require(!Runtime(L).bClosing, "Host calls are unavailable during Lua shutdown.");
			Require(lua_type(L, 1) == LUA_TSTRING, "Genesis.invoke requires an API name.");
			size_t Size = 0;
			const char* Name = lua_tolstring(L, 1, &Size);
			Require(Size > 0 && Size <= 128, "Invalid host API name.");
			auto Arguments = ReadJson(L, 2);
			FJsonValue Result;
			FString Error;
			if (!Runtime(L).Host->Invoke(FString(std::string(Name, Size)), Arguments, Result, Error))
				throw std::runtime_error(Error.IsEmpty() ? "Script host call failed." : Error.ToAnsi());
			PushJson(L, Result);
			return 1;
		});
	}
	static int Load(lua_State* L)
	{
		return Guard(L, [&] {
			auto& Job = *static_cast<FLoad*>(lua_touserdata(L, 1));
			if (luaL_loadbufferx(L, reinterpret_cast<const char*>(Job.Payload.data()), Job.Payload.size(),
								 Job.Name.ToAnsi().c_str(), "t") != LUA_OK)
				return lua_error(L);
			lua_newtable(L);
			lua_newtable(L);
			lua_pushglobaltable(L);
			lua_setfield(L, -2, "__index");
			lua_setmetatable(L, -2);
			lua_pushvalue(L, -1);
			lua_setfield(L, -2, "_G");
			lua_setupvalue(L, 2, 1);
			lua_call(L, 0, 1);
			Require(lua_istable(L, -1), "Lua modules must return an export table.");
			Job.Reference = luaL_ref(L, LUA_REGISTRYINDEX);
			return 0;
		});
	}
	static int CallFunction(lua_State* L)
	{
		return Guard(L, [&] {
			auto& Job = *static_cast<FCall*>(lua_touserdata(L, 1));
			lua_rawgeti(L, LUA_REGISTRYINDEX, Job.Reference);
			lua_pushlstring(L, Job.Function.View().data(), Job.Function.Len());
			lua_rawget(L, -2);
			Require(lua_isfunction(L, -1), "Lua export is not a function.");
			PushJson(L, Job.Arguments);
			lua_call(L, 1, 1);
			Job.Result = ReadJson(L, -1);
			return 0;
		});
	}
	std::shared_ptr<IScriptHost> Host;
	lua_State* State = nullptr;
	std::map<FString, int> Modules;
	uint64 MemoryLimit, Budget, Remaining = 0;
	size_t MemoryUsed = 0;
	std::thread::id Owner;
	bool bRunning = false, bClosing = false;
};
} // namespace
bool RegisterLuaScriptProvider(FScriptRuntimeRegistry& Registry, FString& Error)
{
	FScriptProvider Provider;
	Provider.Language = "Lua";
	Provider.GameplayTargets = {"Win64"};
	Provider.EditorTargets = {"Win64"};
	Provider.Create = [](const auto& Request, auto Host, FString& Error) -> std::unique_ptr<IScriptRuntime> {
		auto R = std::make_unique<FLuaScriptRuntime>(Request, std::move(Host));
		if (!R->Initialize(Error))
			return {};
		return R;
	};
	return Registry.Register(std::move(Provider), Error);
}
