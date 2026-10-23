# Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
param([ValidateSet('All','Lua','Python','DotNet')][string]$Language='All',[ValidateSet('d3d12','vulkan')][string]$Backend='d3d12')
$ErrorActionPreference='Stop'
$repositoryDirectory=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$editorExecutable=Join-Path $repositoryDirectory 'Engine/Binaries/Win64/GenesisEditor.exe'
$languages=if($Language -eq 'All'){@('Lua','Python','DotNet')}else{@($Language)}
foreach($provider in $languages){
 $projectDirectory=Join-Path $PSScriptRoot $provider
 $savedDirectory=Join-Path $projectDirectory 'Saved'
 New-Item -ItemType Directory -Path $savedDirectory -Force | Out-Null
 New-Item -ItemType Directory -Path (Join-Path $projectDirectory 'Content/Maps') -Force | Out-Null
 $arguments=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Source/Scenario.json') -Raw | ConvertFrom-Json
 $arguments.Content=Join-Path $projectDirectory 'Content'
 $arguments.Map=Join-Path $projectDirectory 'Content/Maps/Main.gmap'
 $argumentFile=Join-Path $savedDirectory 'Arguments.json'
 $resultFile=Join-Path $savedDirectory "Result_$Backend.json"
 $arguments | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $argumentFile -Encoding utf8
 $projectFile=Join-Path $projectDirectory 'MaterialAuthoring.gproject'
 $editorArguments=@('"'+$projectFile+'"','-scriptmodule=main','"-scriptargs='+$argumentFile+'"','"-scriptresult='+$resultFile+'"','-exitafterscript','-nodebug',"-$Backend")
 $process=Start-Process -FilePath $editorExecutable -ArgumentList $editorArguments -WindowStyle Hidden -PassThru
 if(-not $process.WaitForExit(60000)){Stop-Process -Id $process.Id;throw "$provider material scenario exceeded 60 seconds."}
 if($process.ExitCode -ne 0){throw "$provider material scenario failed; inspect $resultFile"}
 $result=Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json
 if(-not $result.Success -or -not $result.Result.Complete){throw "$provider material scenario failed: $($result.Error)"}
 Write-Output "$provider / ${Backend}: material authoring project generated and verified."
}
