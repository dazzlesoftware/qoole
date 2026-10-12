@echo off
rem Regenerates the SPIR-V headers (MapShaders_*_SPIRV.h) used by the Vulkan
rem backend from MapShaders.hlsl. Run it after changing the shaders. (The DXIL
rem used by D3D12 is compiled at build time by MapEditor.vcxproj.)
rem
rem Needs a dxc.exe built with SPIR-V support: the Vulkan SDK's, or a release
rem from https://github.com/microsoft/DirectXShaderCompiler. The dxc.exe in the
rem Windows SDK cannot emit SPIR-V.
rem
rem The headers in the repository were not made by this script: they are the
rem SPIR-V of the old Vulkan renderer plugin (Interface\DrawShaders_VK_*.h,
rem compiled from Interface\DrawShaders.hlsl, which flipped Y itself), with
rem the one instruction that flipped Y (y = p * s - 1) changed back to
rem y = 1 - p * s by swapping the operands of its OpFSub. That is exactly
rem MapShaders.hlsl; running this script replaces them with dxc's output.
rem
rem Usage: BuildMapShaders.cmd [path\to\dxc.exe]
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

"%DXC%" %OPTS% -T vs_6_0 -E VSMain -Vn g_MapShaders_VSMain_SPIRV -Fh MapShaders_VSMain_SPIRV.h MapShaders.hlsl || exit /b 1
"%DXC%" %OPTS% -T ps_6_0 -E PSColor -Vn g_MapShaders_PSColor_SPIRV -Fh MapShaders_PSColor_SPIRV.h MapShaders.hlsl || exit /b 1
