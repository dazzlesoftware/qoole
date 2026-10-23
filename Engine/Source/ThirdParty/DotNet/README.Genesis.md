# .NET 10.0.12 native hosting declarations

Unmodified `hostfxr.h` and `coreclr_delegates.h` from Microsoft's .NET 10.0.12
Windows x64 host pack, distributed under the .NET Foundation MIT license.
Source: https://github.com/dotnet/runtime/tree/v10.0.12/src/native/corehost
These declarations introduce no static runtime dependency. Genesis loads hostfxr
only when a project requests its DotNet scripting provider.
