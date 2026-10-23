# Installs the desktop editor's DXIL/SPIR-V compiler. Not used by shipped games.
$ErrorActionPreference = 'Stop'
$compilerDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Binaries/ThirdParty/DXC'))
$archivePath = Join-Path ([IO.Path]::GetTempPath()) ('genesis-dxc-' + [guid]::NewGuid() + '.zip')
try {
    Invoke-WebRequest 'https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip' -OutFile $archivePath
    $expectedHash = 'ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1'
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Material compiler archive checksum mismatch.' }
    New-Item -ItemType Directory -Path $compilerDir -Force | Out-Null
    Expand-Archive -LiteralPath $archivePath -DestinationPath $compilerDir -Force
    Write-Output "Material compiler installed in $compilerDir"
} finally {
    if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath }
}
