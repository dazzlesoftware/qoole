@echo off
rem Regenerates the SPIR-V headers (DrawShaders_VK_*.h) used by FDrawVulkan
rem from DrawShaders.hlsl. Run it after changing the shaders.
rem
rem Needs a dxc.exe built with SPIR-V support: the Vulkan SDK's, or a release
rem from https://github.com/microsoft/DirectXShaderCompiler. The dxc.exe in the
rem Windows SDK cannot emit SPIR-V.
rem
rem Usage: BuildVulkanShaders.cmd [path\to\dxc.exe]
rem        (defaults to %VULKAN_SDK%\Bin\dxc.exe)

setlocal
set DXC=%~1
if "%DXC%"=="" if defined VULKAN_SDK set DXC=%VULKAN_SDK%\Bin\dxc.exe
if "%DXC%"=="" (
	echo usage: %~nx0 path\to\dxc.exe
	exit /b 1
)

cd /d "%~dp0"
set OPTS=-nologo -O3 -spirv -fspv-target-env=vulkan1.0

"%DXC%" %OPTS% -T vs_6_0 -E VSMain -Vn g_VK_VSMain -Fh DrawShaders_VK_VS.h DrawShaders.hlsl || exit /b 1
"%DXC%" %OPTS% -T ps_6_0 -E PSColor -Vn g_VK_PSColor -Fh DrawShaders_VK_PSColor.h DrawShaders.hlsl || exit /b 1
"%DXC%" %OPTS% -T ps_6_0 -E PSTexture -Vn g_VK_PSTexture -Fh DrawShaders_VK_PSTexture.h DrawShaders.hlsl || exit /b 1
