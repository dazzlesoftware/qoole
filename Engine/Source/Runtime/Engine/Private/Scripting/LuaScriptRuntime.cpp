// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/LuaScriptRuntime.h"
#include "Internationalization/TextLocalization.h"
#include "Scripting/ScriptLimits.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <exception>
#include "Misc/ValidationContext.h"
#include <thread>
namespace
{
	char ArrayTag, ObjectTag, NullTag;

	// All entry points run under lua_pcall. Lua is built as C++ so raising a Lua
	// error safely unwinds FJsonValue/string/container temporaries in bindings.
	template <class F> int Guard(lua_State* L, F&& Function)
	{
		// Lua's own errors continue through lua_pcall. Expected native validation
		// failures are returned as status; only foreign C++ callback failures use
		// this exception barrier. Destroy native temporaries before lua_error.
		{
			FString Error;
			FValidationContext Validation(Error);
			try
			{
				const int Results = Function(Validation);
				if (Validation.IsValid())
				{
					return Results;
				}
			}
			catch (const std::exception& Exception)
			{
				Error = Exception.what();
			}
			lua_pushlstring(L, Error.View().data(), Error.Len());
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
	void PushJsonValue(
	    FValidationContext& Validation, lua_State* L, const FJsonValue& V, int& Nodes, size_t& Bytes, int Depth = 0)
	{
		if (!Validation.Check(++Nodes <= ScriptLimits::MaxJsonNodes &&
		            (Bytes += ScriptLimits::JsonNodeBytes) <= ScriptLimits::MaxTransferBytes,
		        GENESIS_TEXT("Script JSON exceeds transfer limits.")))
		{
			return;
		}

		if (!Validation.Check(Depth <= ScriptLimits::MaxJsonDepth, GENESIS_TEXT("Script JSON exceeds maximum depth.")))
		{
			return;
		}

		if (!Validation.Check(
		        lua_checkstack(L, ScriptLimits::LuaStackReserve), GENESIS_TEXT("Script stack exhausted.")))
		{
			return;
		}

		switch (V.GetType())
		{
		case EJsonType::Null:
			lua_pushlightuserdata(L, &NullTag);
			break;
		case EJsonType::Bool:
			lua_pushboolean(L, V.AsBool());
			break;
		case EJsonType::Number:
		{
			if (!Validation.Check(std::isfinite(V.AsNumber()), GENESIS_TEXT("Script JSON requires finite numbers.")))
			{
				return;
			}
		}
			lua_pushnumber(L, V.AsNumber());
			break;
		case EJsonType::String:
		{
			const FString S = V.AsString();
			if (!Validation.Check((Bytes += S.Len()) <= ScriptLimits::MaxTransferBytes,
			        GENESIS_TEXT("Script JSON exceeds transfer limits.")))
			{
				return;
			}

			lua_pushlstring(L, S.View().data(), S.Len());
			break;
		}
		case EJsonType::Array:
		{
			if (!Validation.Check(V.Num() <= ScriptLimits::MaxJsonNodes, GENESIS_TEXT("Script array is too large.")))
			{
				return;
			}
		}
			lua_createtable(L, V.Num(), 0);
			SetTag(L, &ArrayTag);
			for (int I = 0; I < V.Num(); ++I)
			{
				PushJsonValue(Validation, L, V[I], Nodes, Bytes, Depth + 1);
				if (!Validation.IsValid())
				{
					return;
				}

				lua_rawseti(L, -2, I + 1);
			}
			break;
		case EJsonType::Object:
		{
			if (!Validation.Check(V.Num() <= ScriptLimits::MaxJsonNodes, GENESIS_TEXT("Script object is too large.")))
			{
				return;
			}
		}
			lua_createtable(L, 0, V.Num());
			SetTag(L, &ObjectTag);
			for (const FString& Key : V.GetKeys())
			{
				if (!Validation.Check((Bytes += Key.Len()) <= ScriptLimits::MaxTransferBytes,
				        GENESIS_TEXT("Script JSON exceeds transfer limits.")))
				{
					return;
				}

				lua_pushlstring(L, Key.View().data(), Key.Len());
				PushJsonValue(Validation, L, V.Get(Key.View()), Nodes, Bytes, Depth + 1);
				if (!Validation.IsValid())
				{
					return;
				}

				lua_rawset(L, -3);
			}
			break;
		}
	}
	void PushJson(FValidationContext& Validation, lua_State* L, const FJsonValue& V)
	{
		int Nodes = 0;
		size_t Bytes = 0;
		PushJsonValue(Validation, L, V, Nodes, Bytes);
		if (!Validation.IsValid())
		{
			return;
		}
	}
	FJsonValue ReadJson(FValidationContext& Validation, lua_State* L, int Index, std::set<const void*>& Active,
	    int& Count, size_t& Bytes, int Depth = 0)
	{
		if (!Validation.Check(
		        lua_checkstack(L, ScriptLimits::LuaStackReserve), GENESIS_TEXT("Script stack exhausted.")))
		{
			return {};
		}

		if (!Validation.Check((Bytes += ScriptLimits::JsonNodeBytes) <= ScriptLimits::MaxTransferBytes,
		        GENESIS_TEXT("Script JSON exceeds transfer limits.")))
		{
			return {};
		}

		if (!Validation.Check(Depth <= ScriptLimits::MaxJsonDepth && ++Count <= ScriptLimits::MaxJsonNodes,
		        GENESIS_TEXT("Script result exceeds JSON limits.")))
		{
			return {};
		}

		Index = lua_absindex(L, Index);
		switch (lua_type(L, Index))
		{
		case LUA_TNIL:
			return {};
		case LUA_TLIGHTUSERDATA:
		{
			if (!Validation.Check(lua_touserdata(L, Index) == &NullTag,
			        GENESIS_TEXT("Native pointers cannot cross script bindings.")))
			{
				return {};
			}
		}
			return {};
		case LUA_TBOOLEAN:
			return FJsonValue(lua_toboolean(L, Index) != 0);
		case LUA_TNUMBER:
		{
			const double V = lua_tonumber(L, Index);
			if (!Validation.Check(std::isfinite(V), GENESIS_TEXT("Script result requires finite numbers.")))
			{
				return {};
			}
			return FJsonValue(V);
		}
		case LUA_TSTRING:
		{
			size_t Size = 0;
			const char* S = lua_tolstring(L, Index, &Size);
			if (!Validation.Check((Bytes += Size) <= ScriptLimits::MaxTransferBytes,
			        GENESIS_TEXT("Script JSON exceeds transfer limits.")))
			{
				return {};
			}
			return FJsonValue(FString(std::string(S, Size)));
		}
		case LUA_TTABLE:
		{
			const void* Identity = lua_topointer(L, Index);
			if (!Validation.Check(Active.insert(Identity).second, GENESIS_TEXT("Script result contains a cycle.")))
			{
				return {};
			}

			bool Array = HasTag(L, Index, &ArrayTag);
			const bool Object = HasTag(L, Index, &ObjectTag);
			size_t Numeric = 0, Strings = 0, Max = 0;
			lua_pushnil(L);
			while (lua_next(L, Index))
			{
				if (!Validation.Check(
				        ++Count <= ScriptLimits::MaxJsonNodes, GENESIS_TEXT("Script result exceeds JSON limits.")))
				{
					return {};
				}

				if (lua_type(L, -2) == LUA_TSTRING)
					++Strings;
				else
				{
					if (!Validation.Check(lua_isinteger(L, -2) && lua_tointeger(L, -2) > 0 &&
					            lua_tointeger(L, -2) <= ScriptLimits::MaxJsonNodes,
					        GENESIS_TEXT("JSON object keys must be strings; array indices must be dense positive "
					                     "integers.")))
					{
						return {};
					}

					++Numeric;
					Max = std::max(Max, static_cast<size_t>(lua_tointeger(L, -2)));
				}
				lua_pop(L, 1);
			}
			if (!Validation.Check(!Strings || !Numeric, GENESIS_TEXT("Mixed Lua table keys cannot represent JSON.")))
			{
				return {};
			}

			if (!Validation.Check(!Object || !Numeric, GENESIS_TEXT("JSON objects require string keys.")))
			{
				return {};
			}

			if (Numeric)
				Array = true;
			if (!Validation.Check(
			        !Array || (!Strings && Numeric == Max), GENESIS_TEXT("JSON arrays require dense integer indices.")))
			{
				return {};
			}

			FJsonValue Result = Array ? FJsonValue::MakeArray() : FJsonValue::MakeObject();
			if (Array)
			{
				for (size_t I = 1; I <= Max; ++I)
				{
					lua_rawgeti(L, Index, I);
					Result.Push(ReadJson(Validation, L, -1, Active, Count, Bytes, Depth + 1));
					if (!Validation.IsValid())
					{
						return {};
					}

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
					if (!Validation.Check((Bytes += Size) <= ScriptLimits::MaxTransferBytes,
					        GENESIS_TEXT("Script JSON exceeds transfer limits.")))
					{
						return {};
					}

					Result[std::string_view(Key, Size)] = ReadJson(Validation, L, -1, Active, Count, Bytes, Depth + 1);
					if (!Validation.IsValid())
					{
						return {};
					}

					lua_pop(L, 1);
				}
			}
			Active.erase(Identity);
			return Result;
		}
		default:
			Validation.Fail(GENESIS_TEXT("Script result contains a function, thread or unsupported userdata."));
			return {};
		}
	}
	FJsonValue ReadJson(FValidationContext& Validation, lua_State* L, int Index)
	{
		std::set<const void*> Active;
		int Count = 0;
		size_t Bytes = 0;
		return ReadJson(Validation, L, Index, Active, Count, Bytes);
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
			if (MemoryLimit < 256 * 1024 || MemoryLimit > 1024ull * 1024 * 1024 || Budget < 1000 || Budget > 1000000000)
			{
				Error = GENESIS_TEXT("Lua limits require 256 KiB-1 GiB memory and 1000-1000000000 instructions.");
				return false;
			}
			State = lua_newstate(Allocate, this);
			if (!State)
			{
				Error = GENESIS_TEXT("Cannot allocate Lua VM.");
				return false;
			}
			*static_cast<FLuaScriptRuntime**>(lua_getextraspace(State)) = this;
			return Run(Bootstrap, nullptr, Error);
		}
		bool LoadModule(const FString& Name, TArrayView<const uint8> Payload, FString& Error) override
		{
			if (!Check(Error))
				return false;
			if ((!Modules.contains(Name) && Modules.size() >= ScriptLimits::MaxModules) || !ValidName(Name) ||
			    Payload.empty() || Payload.size() > ScriptLimits::MaxTransferBytes)
			{
				Error = GENESIS_TEXT("Lua requires a module name and source payload of at most 4 MiB.");
				return false;
			}
			FLoad Job{this, Name, Payload};
			if (!Run(Load, &Job, Error))
			{
				Error = GENESIS_TEXT("Lua module ") + Name + ": " + Error;
				return false;
			}
			std::map<FString, int>::iterator It = Modules.find(Name);
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
			const std::map<FString, int>::iterator It = Modules.find(Module);
			if (It == Modules.end() || !ValidName(Function))
			{
				Error = GENESIS_TEXT("Lua module or function is unavailable.");
				return false;
			}
			FCall Job{this, It->second, Function, Arguments};
			if (!Run(CallFunction, &Job, Error))
			{
				Error = GENESIS_TEXT("Lua ") + Module + "." + Function + ": " + Error;
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
			return !Name.IsEmpty() && Name.Len() <= ScriptLimits::MaxNameLength &&
			    Name.View().find('\0') == std::string_view::npos;
		}
		bool Check(FString& Error)
		{
			if (std::this_thread::get_id() != Owner)
			{
				Error = GENESIS_TEXT("Lua runtime must execute on its owning thread.");
				return false;
			}
			if (bRunning)
			{
				Error = GENESIS_TEXT("Lua runtime calls cannot be reentrant.");
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
				Error = Text ? FString(std::string(Text, std::min(Size, size_t(ScriptLimits::MaxErrorBytes))))
				             : GENESIS_TEXT("Lua execution failed.");
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
			FLuaScriptRuntime& R = Runtime(L);
			if (R.Remaining <= 1000)
				luaL_error(L, "%s", *GENESIS_TEXT("Lua instruction budget exceeded."));
			R.Remaining -= 1000;
		}
		static void* Allocate(void* User, void* Pointer, size_t OldSize, size_t NewSize)
		{
			FLuaScriptRuntime& R = *static_cast<FLuaScriptRuntime*>(User);
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
			return Guard(L, [&](FValidationContext&) -> int
			{
				for (std::pair<const char*, int (*)(lua_State*)> const& Lib :
				    std::initializer_list<std::pair<const char*, lua_CFunction>>{{LUA_GNAME, luaopen_base},
				        {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
				        {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}})
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
			return Guard(L, [&](FValidationContext& Validation) -> int
			{
				if (!Validation.Check(lua_istable(L, 1) && (lua_isnoneornil(L, 2) || lua_istable(L, 2)),
				        GENESIS_TEXT("setmetatable requires a table and table/nil metatable.")))
				{
					return {};
				}

				if (lua_getmetatable(L, 1))
				{
					lua_pushliteral(L, "__metatable");
					lua_rawget(L, -2);
					const bool Protected = !lua_isnil(L, -1);
					lua_pop(L, 2);
					if (!Validation.Check(!Protected, GENESIS_TEXT("Cannot change a protected metatable.")))
					{
						return {};
					}
				}
				if (lua_istable(L, 2))
					for (const char* Key : {"__gc", "__close"})
					{
						lua_pushstring(L, Key);
						lua_rawget(L, 2);
						const bool HasFinalizer = !lua_isnil(L, -1);
						lua_pop(L, 1);
						if (!Validation.Check(!HasFinalizer,
						        GENESIS_TEXT("Script finalizers are unavailable; use explicit lifecycle callbacks.")))
						{
							return {};
						}
					}
				lua_settop(L, 2);
				lua_setmetatable(L, 1);
				lua_pushvalue(L, 1);
				return 1;
			});
		}
		static int Array(lua_State* L)
		{
			return Guard(L, [&](FValidationContext& Validation) -> int
			{
				if (!Validation.Check(lua_istable(L, 1), GENESIS_TEXT("Genesis.array requires a table.")))
				{
					return {};
				}

				lua_settop(L, 1);
				SetTag(L, &ArrayTag);
				return 1;
			});
		}
		static int Invoke(lua_State* L)
		{
			return Guard(L, [&](FValidationContext& Validation) -> int
			{
				if (!Validation.Check(
				        !Runtime(L).bClosing, GENESIS_TEXT("Host calls are unavailable during Lua shutdown.")))
				{
					return {};
				}

				if (!Validation.Check(
				        lua_type(L, 1) == LUA_TSTRING, GENESIS_TEXT("Genesis.invoke requires an API name.")))
				{
					return {};
				}

				size_t Size = 0;
				const char* Name = lua_tolstring(L, 1, &Size);
				if (!Validation.Check(Size > 0 && Size <= ScriptLimits::MaxNameLength &&
				            std::string_view(Name, Size).find('\0') == std::string_view::npos,
				        GENESIS_TEXT("Invalid host API name.")))
				{
					return {};
				}

				FJsonValue Arguments = ReadJson(Validation, L, 2);
				if (!Validation.IsValid())
				{
					return {};
				}

				FJsonValue Result;
				FString Error;
				if (!Runtime(L).Host->Invoke(FString(std::string(Name, Size)), Arguments, Result, Error))
				{
					Validation.Fail(Error.IsEmpty() ? GENESIS_TEXT("Script host call failed.") : Error);
					return 0;
				}
				PushJson(Validation, L, Result);
				if (!Validation.IsValid())
				{
					return {};
				}
				return 1;
			});
		}
		static int Load(lua_State* L)
		{
			return Guard(L, [&](FValidationContext& Validation) -> int
			{
				FLoad& Job = *static_cast<FLoad*>(lua_touserdata(L, 1));
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
				if (!Validation.Check(lua_istable(L, -1), GENESIS_TEXT("Lua modules must return an export table.")))
				{
					return {};
				}

				Job.Reference = luaL_ref(L, LUA_REGISTRYINDEX);
				return 0;
			});
		}
		static int CallFunction(lua_State* L)
		{
			return Guard(L, [&](FValidationContext& Validation) -> int
			{
				FCall& Job = *static_cast<FCall*>(lua_touserdata(L, 1));
				lua_rawgeti(L, LUA_REGISTRYINDEX, Job.Reference);
				lua_pushlstring(L, Job.Function.View().data(), Job.Function.Len());
				lua_rawget(L, -2);
				if (!Validation.Check(lua_isfunction(L, -1), GENESIS_TEXT("Lua export is not a function.")))
				{
					return {};
				}

				PushJson(Validation, L, Job.Arguments);
				if (!Validation.IsValid())
				{
					return {};
				}

				lua_call(L, 1, 1);
				Job.Result = ReadJson(Validation, L, -1);
				if (!Validation.IsValid())
				{
					return {};
				}
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
	Provider.Create = [](const FScriptRuntimeRequest& Request, std::shared_ptr<IScriptHost> Host,
	                      FString& Error) -> std::unique_ptr<IScriptRuntime>
	{
		std::unique_ptr<FLuaScriptRuntime> R = std::make_unique<FLuaScriptRuntime>(Request, std::move(Host));
		if (!R->Initialize(Error))
			return {};
		return R;
	};
	return Registry.Register(std::move(Provider), Error);
}
