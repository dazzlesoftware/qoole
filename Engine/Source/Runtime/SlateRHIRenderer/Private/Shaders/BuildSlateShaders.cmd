@echo off
rem Regenerates the SPIR-V headers (SlateShaders_*_SPIRV.h) used by the Vulkan
rem backend from SlateShaders.hlsl. Run it after changing the shaders. (The DXIL
rem used by D3D12 is compiled at build time by SlateRHIRenderer.vcxproj.)
rem
rem Needs a dxc.exe built with SPIR-V support: the Vulkan SDK's, or a release
rem from https://github.com/microsoft/DirectXShaderCompiler. The dxc.exe in the
rem Windows SDK cannot emit SPIR-V.
rem
rem The Vulkan backend flips Y with a negative viewport height, so the same
rem HLSL works for both backends without -fvk-invert-y.
rem
rem Usage: BuildSlateShaders.cmd [path\to\dxc.exe]
rem        (defaults to %VULKAN_SDK%\Bin\dxc.exe)

setlocal
set DXC=%~1
if "%DXC%"=="" if defined VULKAN_SDK set DXC=%VULKAN_SDK%\Bin\dxc.exe
if "%DXC%"=="" (
	echo usage: %~nx0 path\to\dxc.exe
	exit /b 1
)

cd /d "%~dp0"
set OPTS=-nologo -O3 -spirv -fspv-target-env=vulkan1.1

"%DXC%" %OPTS% -T vs_6_0 -E VSMain -Vn g_SlateShaders_VSMain_SPIRV -Fh SlateShaders_VSMain_SPIRV.h SlateShaders.hlsl || exit /b 1
"%DXC%" %OPTS% -T ps_6_0 -E PSDefault -Vn g_SlateShaders_PSDefault_SPIRV -Fh SlateShaders_PSDefault_SPIRV.h SlateShaders.hlsl || exit /b 1
"%DXC%" %OPTS% -T ps_6_0 -E PSRoundedBox -Vn g_SlateShaders_PSRoundedBox_SPIRV -Fh SlateShaders_PSRoundedBox_SPIRV.h SlateShaders.hlsl || exit /b 1
"%DXC%" %OPTS% -T ps_6_0 -E PSLine -Vn g_SlateShaders_PSLine_SPIRV -Fh SlateShaders_PSLine_SPIRV.h SlateShaders.hlsl || exit /b 1
