# Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
param([ValidateSet('All','Lua','Python','DotNet')][string]$Language='All')
$ErrorActionPreference='Stop'
$repositoryDirectory=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runtimeExecutable=Join-Path $repositoryDirectory 'Engine/Binaries/Win64/RuntimeDataCheck.exe'
$languages=if($Language -eq 'All'){@('Lua','Python','DotNet')}else{@($Language)}
foreach($provider in $languages) {
 $projectFile=Join-Path $PSScriptRoot "$provider/$provider.gproject"
 & $runtimeExecutable "-scriptproject=$projectFile"
 if($LASTEXITCODE -ne 0){throw "$provider runtime lifecycle regression failed ($LASTEXITCODE)."}
}
