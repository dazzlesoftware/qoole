# Project scripting

For the completed current-renderer material authoring scenario, use
`Samples/Features/MaterialAuthoring/Run.ps1`. Lua, Python and C# create native
functions, collections, attribute layers, master/static-instance materials,
textures and meshes, then transact and save/reload a scene. Both desktop backend
runs pass; native acceptance tests also verify pixels. See
[MaterialGraph.md](MaterialGraph.md) for authoring APIs and supported scope.

Status: executable Lua, Python and .NET providers plus shared core/world
bindings, 2026-10-22. All three execute real modules and pass conformance tests.
Project settings alone do not start gameplay scripts: lifecycle, editor execution
and language-picker integration remain pending. Full API coverage is tracked in
[ScriptingAPICoverage.md](ScriptingAPICoverage.md).

Projects can choose their gameplay language independently and enable several
languages for editor automation. The `.gproject` settings are:

```json
"Scripting": {
  "GameplayLanguage": "Lua",
  "EditorLanguages": ["Lua", "Python", "DotNet"]
}
```

`None` is the default gameplay language for existing projects. Provider IDs are
extensible; future language plugins do not require a project format change.
Malformed settings and duplicate editor providers are rejected. Unknown settings
inside `Scripting` survive load/save. There is no language picker UI yet.

## Shared architecture

`Runtime/Engine/Public/Scripting/ScriptRuntime.h` defines a per-engine provider
registry and isolated runtime instances. Each provider owns VM creation, module
loading, calls, lifetime, errors and cooked payload interpretation. The host API
uses named calls and structured values; native object addresses must not cross
this boundary. Actor/component IDs will resolve through the world at call time,
so deleting an actor cannot leave a script with an unsafe native pointer.

Providers declare gameplay and editor target support separately. An unavailable
language or target fails explicitly. Do not fall back to another language or
enable a console target because its architecture resembles a desktop target.
Editor APIs must be excluded from gameplay hosts, not merely hidden from tools.

Material graphs remain shader programs. Gameplay scripts and any future visual
gameplay graphs use separate execution services; they can update material
parameters through the dynamic material instance API.

## Language options

All three are legitimate provider candidates. Lua's age is not a reason to
exclude it: its embedding API and small portable implementation make it a useful
gameplay candidate. Python offers a substantial automation ecosystem. C# offers
static typing and managed tooling; an AOT-compatible binding generator and
deployment path need to be designed before relying on it for future consoles.
These are design assessments, not statements of SDK approval or benchmarks.

- [Lua embedding and implementation](https://www.lua.org/manual/5.4/readme.html)
- [Python embedding](https://docs.python.org/3/extending/embedding.html)
- [.NET Native AOT capabilities and limits](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/)

Do not force shared projects to use Lua-specific object bindings or .NET-specific
reflection as the engine API. Keep those details inside each provider.

## Delivery sequence for all three providers

1. Define the supported engine binding surface: world/entity lookup, transforms,
   component properties, logging, events and input. Specify value types, errors,
   ownership, main-thread access and versioning. Generate provider bindings from
   one API description rather than maintaining three unrelated public APIs.
2. Establish per-world lifecycle (`BeginPlay`, tick, events, end play), host
   capabilities, script component/package schemas and persisted property types.
   Test script exceptions and destroyed entities without affecting other worlds.
3. Implement Lua, Python and .NET adapters independently against those contracts.
   Pin runtimes and license notices; add build options so unused runtimes are
   excluded. Each adapter must execute a real module and pass the same binding
   conformance suite before being offered as available in the project picker.
4. Add editor automation providers with undo transactions and project-scoped
   access. Add console/output panels, debugging and reload that preserves declared
   state. Reload is an editor capability, not a requirement for cooked games.
5. Add per-target cooking and dependency analysis. For .NET, evaluate generated
   bindings and AOT modules instead of depending on dynamic assembly loading or
   reflection emit. For interpreter providers, constrain standard libraries and
   module loading to the intended host capabilities. This is an API boundary,
   not a promise that arbitrary untrusted scripts are securely sandboxed.
6. Validate each runtime/backend combination with the licensed platform SDK and
   hardware before adding Switch 2, Xbox Series or PS5 to provider capabilities.
   Record code-loading policy, memory, GC pauses, threading, debugging and package
   requirements from those SDKs. No console implementation or approval exists yet.

The first acceptance gate for each adapter is a standalone runtime test that
loads a cooked module, calls an engine binding, handles an error, and destroys
the VM without leaked references. Then demonstrate the same gameplay component
in a real world and the editor. Configuration-only tests do not establish that
any of these language adapters is ready.

## Executable Lua adapter

`RegisterLuaScriptProvider` registers the embedded Lua 5.4.9 provider for Win64
gameplay/editor requests. Each `Create` owns an independent VM, modules and host.
The pinned unmodified source and license are under `Source/ThirdParty/Lua`;
the official archive checksum is recorded in `README.Genesis.md`. Lua builds as
C++ so its protected errors unwind C++ binding temporaries safely.

A module is UTF-8 source returning a table of explicitly exported functions:

```lua
local ticks = 0
return {
    Update = function(args)
        ticks = ticks + 1
        return Genesis.invoke("World.FindActor", { ActorId = args.ActorId })
    end
}
```

The host must register the core/world bindings before that example can run. `IScriptRuntime::Call`
passes one structured argument and receives one structured result. Lua objects
use string-keyed tables; arrays use dense positive integer keys. `Genesis.array({})`
preserves an empty array and `Genesis.null` preserves JSON null members. Mixed,
sparse, cyclic, non-finite and unsupported values fail conversion. A failed call
leaves the caller's result unchanged, but does not roll back script state or host
side effects. A failed module replacement retains the previous module exports.

The initial host profile opens base, table, string, math and UTF-8 libraries.
There is no IO, OS, package loader, debug or coroutine library. File/source
loaders and process-output functions are absent. Ordinary metatables are available;
script finalizers (`__gc`, `__close`) are rejected when setting a metatable. Lua
disables debug hooks during GC finalizers, so engine lifecycle callbacks must own
cleanup. This is an engine capability boundary, not an untrusted-code sandbox.

Runtime requests configure a VM allocation ceiling (default 16 MiB) and Lua
instruction budget (default one million per module initialization/call). Transfer
limits are depth 64, 100,000 values and 4 MiB, including container overhead.
A VM accepts up to 256 named modules, each up to 4 MiB source. Calls execute on
the creating thread and cannot reenter that VM through a host callback. The
instruction hook bounds Lua bytecode execution; native host operations still
need their own bounded implementations. Bytecode/AOT script cooking, debugging,
state migration and gameplay lifecycle are pending.

`FScriptBindingRegistry` implements the shared host contract. Each descriptor
declares a name, capability, editor-only flag, required/optional typed arguments,
result type and callback. Unknown fields, missing/wrong types, absent capabilities
and invalid result values fail before publishing output. Gameplay hosts reject
editor-only bindings even when a capability name is present. `Describe()` exposes
versioned metadata for tooling and future generated language facades. Callbacks
run on the owning thread; they must resolve stable entity IDs at invocation time.

Tests execute actual Lua modules, host calls and descriptor validation. They also
cover VM isolation, errors, memory/bytecode limits, invalid result shapes, failed
module replacement, cross-thread rejection and finalizer rejection. Separate Python/.NET tests execute real modules and the same world API scenarios.

## Python and .NET execution

Run `Engine/Build/SetupScriptingRuntimes.ps1` before scripting tests. It installs
pinned CPython 3.14.6 and .NET 10.0.12 runtimes under ignored `Binaries/ThirdParty`,
verifies the PSF/Microsoft published archive hashes, and builds the managed bridge
and C# example using the .NET 10 SDK. Lua is built from its vendored source.
Runtime deployments use these artifacts without requiring the SDK.

`RegisterBuiltinScriptProviders` installs the three adapter factories without
loading their runtimes. Factories load dependencies only when a project creates
that provider. Win64 is the only validated target. There are no console adapters.

Python modules define functions taking/returning JSON-compatible values. Import
`genesis` and call `genesis.invoke(api, arguments)`. Each runtime has a separate
subinterpreter/module table. CPython initializes once for the process; interpreter
instances are destroyed individually. Embedding functions resolve dynamically,
so Python is not an unconditional executable DLL dependency. `GENESIS_PYTHON_HOME`
can select an installation of the pinned version. This initial profile disallows
subinterpreter threads/fork/exec and checks extensions for interpreter support.
Its opcode-trace budget catches ordinary runaway Python loops; it does not impose
a hard memory ceiling, preempt native extensions, or guarantee interruption if
scripts deliberately disable/catch tracing. The default allocation ceiling is
currently enforced by Lua only. Trusted-code execution/cancellation remains part
of the pending lifecycle/debugging work.

C# modules compile against `Genesis.ScriptBridge` and implement
`Genesis.Scripting.IScriptModule.Call(function, arguments, host)`. Use
`host.Invoke(api, arguments)` for the same engine API. Each loaded DLL exports
one public module class; each runtime/module has an independent load context.
Failed replacement retains the previous module. Native host tokens expire when
the owning runtime is destroyed, and callbacks require an active invocation on
the owning thread. The bridge catches managed exceptions before returning to C++.
The desktop provider uses hostfxr/JIT and reflection for module discovery; generated
AOT registration, managed cancellation and dependency packaging are pending.

The .NET adapter uses its bundled runtime when installed, otherwise .NET 10.0.12
under Program Files. `GENESIS_DOTNET_ROOT` overrides that root. Bridge binaries
are selected explicitly or relative to the engine binary directory. Providers do
not silently substitute another language when dependencies are missing.

The checked-in `.gproject`/source examples under `Samples/Scripting` run against
real world and editor documents. Opening a descriptor does not auto-execute code.

## Editor execution

`Editor/ScriptEditorPlugin` owns provider-independent editor bindings and the
`ExecuteEditorScript` service. GenesisEditor exposes the same service:

```powershell
GenesisEditor.exe -runscript=<absolute-path.py> -scriptfunction=BuildTerrain -scriptargs=<absolute-arguments.json> -scriptresult=<absolute-result.json> -exitafterscript
```

File extension selects Lua/Python/DotNet; `-scriptlanguage=` overrides it. The
argument file contains JSON; the result file contains Success, Result and Error.
Failed execution returns a nonzero process exit code. A script invocation starts
on a new map unless a map is explicitly supplied. It does not load the project's
default or last-opened map, so repeatable generators do not duplicate content.
This is synchronous trusted-code execution on the editor thread.

Document bindings resolve stable actor IDs on every invocation. Editor.Read and
Editor.Write gate queries and edits. Each edit is a native undo transaction;
BeginTransaction/EndTransaction group edits. Failures inside an edit cancel the
script-owned batch. Unfinished transactions are canceled when execution returns,
including language errors. Scripts cannot modify or end another operation's
transaction. Editor.Files gates native map saves and material file operations.
Editor.Commands gates mapped command execution; disabled/hidden commands fail.
Command execution and saves require a closed transaction. Committed transactions
and completed file saves remain committed if a later script operation fails.

Landscape bindings include creation, chunked heights (16,384 samples per call),
layers and native sculpt/smooth/flatten/erosion/noise/paint brushes. Foliage presets,
placement, paint/erase and clearing call native foliage tools. Material bindings
provide expression/output discovery, default graph creation, JSON graph authoring,
HLSL translation, native graph loading and DXIL/SPIR-V cooking. Invalid cooks leave
existing files intact; resaves retain asset IDs. Asset discovery/resolution,
exports, bounded bulk reads, mounts and issues are also available to runtime hosts.
All operations appear in API.Describe; large resources use chunked access.

Run `Samples/Features/LandscapeFoliage/Run.ps1` for the three language projects;
select `-Backend d3d12` or `-Backend vulkan`. Each creates, sculpts, paints, places
foliage and saves its native map. Native tests additionally reopen maps and verify
IDs, heights, weights, instance counts and grouped undo. Rendered assertions for
these script-generated scenes and the remaining feature projects are pending.

## Gameplay component lifecycle

`FScriptWorldSubsystem` is runtime-only. Attach an FActorComponent with Type
`Genesis.Script` and a string `Properties.Module`. Supply the project's language,
provider registry, shared host and module-byte loader. Every component has an
isolated VM/module instance. Up to 256 live components are supported. BeginPlay
starts existing components; Tick reconciles newly added, removed and module-changed
components. SendEvent dispatches a named bounded JSON payload. EndPlay stops all
instances and releases their runtimes; call it before destroying the world.

Modules must export `OnBeginPlay`, `OnTick`, `OnEvent` and `OnEndPlay`. Each receives
ActorId, ComponentId and current Properties. Tick adds DeltaSeconds (0..10 finite
seconds), events add Name/Payload, and end adds Reason (Removed or Stopped).
Callbacks use stable IDs; a removed actor may already be absent during end play.
Mutations inside callbacks go through the same world bindings. Calls are synchronous
on the owning thread and cannot reenter the lifecycle service. A failed tick/event
faults that instance; a failed load leaves a fault marker, avoiding per-frame retries.
Change/remove its component or restart play to retry. Other instances continue.
BeginPlay errors may leave other successfully started components active; EndPlay
still cleans all instances. Startup is not a rollback transaction for world edits.

`Samples/Scripting/RunGameplay.ps1` runs begin/tick/event/end for every language in
RuntimeDataCheck, which links no editor, MapCore, UI or RHI libraries. Automatic
project module loading, Play-In-Editor/input integration, debugging, reload/state
migration and cooked provider packaging remain pending. Full API coverage is
tracked in ScriptingAPICoverage.md and is not claimed complete.

## Project module catalog

Scripting.Modules in `.gproject` is now a validated catalog of unique Name and
relative Source paths, with an optional Runtime path for a cooked/compiled payload.
Unknown per-module fields round-trip unchanged. RuntimeDataCheck -scriptproject
uses GameplayLanguage and loads module bytes from this catalog with a 4 MiB limit.
The C# example specifies a source file and the setup script's compiled DLL.
Authoring projects may reference shared sources outside their own directory;
shipping package closure and target-specific cooking are still pending.

GenesisEditor `<project.gproject> -scriptmodule=main` resolves that same runtime
payload. Optional module EditorFunction supplies the default export; command-line
-scriptfunction overrides it. The landscape/foliage project runner uses this path
for all six language/backend runs. Loading a project alone still does not execute
its modules, and Play-In-Editor/input integration is still pending.
