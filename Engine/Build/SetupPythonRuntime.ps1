# Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
$ErrorActionPreference = 'Stop'
$engineDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runtimeDirectory = Join-Path $engineDirectory 'Binaries/ThirdParty/Python'
$expectedHash = 'df901e84a896ff1ee720ad03377e0c8d8c2244fda79808aeeaff6316df1cb75c'
$marker = Join-Path $runtimeDirectory '.genesis-python-3.14.6.sha256'
if ((Test-Path -LiteralPath $marker) -and (Test-Path -LiteralPath (Join-Path $runtimeDirectory 'python314.dll')) -and ((Get-Content -LiteralPath $marker -Raw).Trim() -eq $expectedHash)) { Write-Output 'CPython 3.14.6 is ready.'; return }
$archive = Join-Path ([IO.Path]::GetTempPath()) ('Genesis-Python-' + [guid]::NewGuid().ToString('N') + '.zip')
Invoke-WebRequest -Uri 'https://www.python.org/ftp/python/3.14.6/python-3.14.6-embed-amd64.zip' -OutFile $archive
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) { throw 'CPython archive checksum mismatch.' }
# Hash matches the PSF release SBOM for this exact archive.
# https://www.python.org/ftp/python/3.14.6/python-3.14.6-embed-amd64.zip.spdx.json
New-Item -ItemType Directory -Path $runtimeDirectory -Force | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $runtimeDirectory -Force
Set-Content -LiteralPath $marker -Value $expectedHash -Encoding ascii
Write-Output "CPython 3.14.6 installed in $runtimeDirectory (license included)."
