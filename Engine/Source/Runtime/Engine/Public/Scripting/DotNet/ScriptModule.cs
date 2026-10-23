// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
using System.Text.Json;
namespace Genesis.Scripting;
public interface IScriptHost
{
    JsonElement Invoke(string api, JsonElement arguments);
}
// The desktop provider loads exactly one public module implementation per DLL.
// Explicit dispatch keeps the engine API independent of reflection-based bindings.
public interface IScriptModule
{
    JsonElement Call(string function, JsonElement arguments, IScriptHost host);
}
