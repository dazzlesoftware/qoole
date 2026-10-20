# Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#
# Runs the self-test of every test application (Engine/Source/Programs/TestApps)
# and reports which passed. Screenshots and logs go to <OutDir>\<App>
# (default: Engine\Saved\TestAppResults).
#
#   .\RunTestApps.ps1                       Debug builds, D3D12
#   .\RunTestApps.ps1 -Release -Vulkan
#   .\RunTestApps.ps1 -Filter UIText,Map*   only the apps matching these names
#
# The apps open real windows and drive them with simulated input: leave the
# desktop alone while they run.

param(
	[string]$OutDir = "",
	[switch]$Release,
	[switch]$Vulkan,
	[string[]]$Filter = @("*"),
	[int]$TimeoutSeconds = 180
)

$ErrorActionPreference = "Stop"
$ExecDir = Join-Path $PSScriptRoot "..\..\..\Binaries\Win64"
$Suffix = if ($Release) { "" } else { "D" }
if (-not $OutDir)
{
	$OutDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\Saved\TestAppResults"))
}
elseif (-not [System.IO.Path]::IsPathRooted($OutDir))
{
	$OutDir = Join-Path (Get-Location) $OutDir
}

$Apps = Get-ChildItem -Path $PSScriptRoot -Directory |
	Where-Object { Test-Path (Join-Path $_.FullName "$($_.Name).vcxproj") } |
	Where-Object { $_.Name -ne "TestAppHarness" } |
	Where-Object { $Name = $_.Name; ($Filter | Where-Object { $Name -like "$_*" }).Count -gt 0 } |
	ForEach-Object { $_.Name }

$Failed = @()
foreach ($App in $Apps)
{
	$Exe = Join-Path $ExecDir "$App$Suffix.exe"
	if (-not (Test-Path $Exe))
	{
		Write-Host ("{0,-22} not built ({1})" -f $App, $Exe) -ForegroundColor Yellow
		$Failed += $App
		continue
	}
	$Dir = Join-Path $OutDir $App
	$Arguments = @("-selftest=`"$Dir`"")
	if ($Vulkan) { $Arguments += "-vulkan" }
	$Start = Get-Date
	$Process = Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru
	if (-not $Process.WaitForExit($TimeoutSeconds * 1000))
	{
		$Process.Kill()
		Write-Host ("{0,-22} TIMED OUT after {1}s" -f $App, $TimeoutSeconds) -ForegroundColor Red
		$Failed += $App
		continue
	}
	$Seconds = ((Get-Date) - $Start).TotalSeconds
	$Log = Get-ChildItem -Path $Dir -Filter "*_log.txt" -ErrorAction SilentlyContinue | Select-Object -First 1
	$Summary = if ($Log) { (Select-String -Path $Log.FullName -Pattern "checks, \d+ failed" | Select-Object -Last 1).Line } else { "" }
	$Summary = if ($Summary) { $Summary -replace "^.*: (\d+ checks, \d+ failed)$", '$1' } else { "no summary" }
	if ($Process.ExitCode -eq 0)
	{
		Write-Host ("{0,-22} passed  ({1}, {2:N1}s)" -f $App, $Summary, $Seconds) -ForegroundColor Green
	}
	else
	{
		Write-Host ("{0,-22} FAILED  ({1}, exit {2})" -f $App, $Summary, $Process.ExitCode) -ForegroundColor Red
		if ($Log)
		{
			Select-String -Path $Log.FullName -Pattern "Check FAILED|Error:" | ForEach-Object { Write-Host "    $($_.Line)" }
		}
		$Failed += $App
	}
}

Write-Host ""
if ($Failed.Count -eq 0)
{
	Write-Host "All $($Apps.Count) test apps passed. Results: $OutDir" -ForegroundColor Green
	exit 0
}
Write-Host "$($Failed.Count) of $($Apps.Count) failed: $($Failed -join ', '). Results: $OutDir" -ForegroundColor Red
exit 1
