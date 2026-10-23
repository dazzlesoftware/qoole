# Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$SkipPython, [switch]$SkipDotNetRuntime)
$ErrorActionPreference = 'Stop'
$engineDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repositoryDirectory = [IO.Path]::GetFullPath((Join-Path $engineDirectory '..'))
if (-not $SkipPython) { & (Join-Path $PSScriptRoot 'SetupPythonRuntime.ps1') }
$bridgeDirectory = Join-Path $engineDirectory 'Binaries/ThirdParty/DotNet'
if (-not $SkipDotNetRuntime) {
 $runtimeDirectory = Join-Path $bridgeDirectory 'Runtime'
 $expectedHash = '844fa99e16fd6f44e0a7c29def7a82d7846902334d6a955248a9519a4dddb3f5acceb9c9223bef69f8c83b8ae2417537e5b76dddf79fb7117dc85b5039bc1297'
 $marker = Join-Path $runtimeDirectory '.genesis-dotnet-10.0.12.sha512'
 if (-not ((Test-Path -LiteralPath $marker) -and (Test-Path -LiteralPath (Join-Path $runtimeDirectory 'host/fxr/10.0.12/hostfxr.dll')) -and ((Get-Content -LiteralPath $marker -Raw).Trim() -eq $expectedHash))) {
  $archive = Join-Path ([IO.Path]::GetTempPath()) ('Genesis-DotNet-' + [guid]::NewGuid().ToString('N') + '.zip')
  Invoke-WebRequest -Uri 'https://builds.dotnet.microsoft.com/dotnet/Runtime/10.0.12/dotnet-runtime-10.0.12-win-x64.zip' -OutFile $archive
  if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash.ToLowerInvariant() -ne $expectedHash) { throw '.NET runtime checksum mismatch.' }
  # Pin from Microsoft's official 10.0 releases.json metadata.
  New-Item -ItemType Directory -Path $runtimeDirectory -Force | Out-Null
  Expand-Archive -LiteralPath $archive -DestinationPath $runtimeDirectory -Force
  Set-Content -LiteralPath $marker -Value $expectedHash -Encoding ascii
 }
}
$bridgeProject = Join-Path $engineDirectory 'Source/Runtime/Engine/Private/Scripting/DotNet/Genesis.ScriptBridge.csproj'
& dotnet build $bridgeProject --configuration $Configuration --output $bridgeDirectory --nologo --verbosity quiet
if ($LASTEXITCODE -ne 0) { throw 'Building the .NET script bridge failed; .NET 10 SDK is required.' }
$exampleProject = Join-Path $repositoryDirectory 'Samples/Scripting/DotNet/Source/Genesis.ScriptExample.csproj'
& dotnet build $exampleProject --configuration $Configuration --output (Join-Path $bridgeDirectory 'Examples') --nologo --verbosity quiet
if ($LASTEXITCODE -ne 0) { throw 'Building the C# scripting example failed.' }
Write-Output 'Lua, Python and .NET scripting dependencies are ready for Win64 testing.'
