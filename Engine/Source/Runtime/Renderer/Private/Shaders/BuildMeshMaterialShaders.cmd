@echo off
rem Regenerates the SPIR-V headers (MeshMaterial_*_SPIRV.h) used by the Vulkan
rem backend from MeshMaterial.hlsl. Run it after changing the shaders. (The DXIL
rem used by D3D12 is compiled at build time by Renderer.vcxproj.)
rem
rem Needs glslc (shaderc, which reads HLSL): the Vulkan SDK's, or the Android
rem NDK's (shader-tools\windows-x86_64\glslc.exe). The headers in the
rem repository were made with the NDK r27c's. The dxc.exe in the Windows SDK
rem cannot emit SPIR-V.
rem
rem Usage: BuildMeshMaterialShaders.cmd [path\to\glslc.exe]
rem        (defaults to %VULKAN_SDK%\Bin\glslc.exe)

setlocal
set GLSLC=%~1
if "%GLSLC%"=="" if defined VULKAN_SDK set GLSLC=%VULKAN_SDK%\Bin\glslc.exe
if "%GLSLC%"=="" (
	echo usage: %~nx0 path\to\glslc.exe
	exit /b 1
)

cd /d "%~dp0"
call :Compile vert VSMaterial || exit /b 1
call :Compile frag PSMaterial || exit /b 1
exit /b 0

rem :Compile stage entry -- MeshMaterial_<entry>_SPIRV.h, an array of words.
:Compile
"%GLSLC%" -x hlsl -DMAP_SPIRV -fshader-stage=%1 -fentry-point=%2 --target-env=vulkan1.1 -O -mfmt=num MeshMaterial.hlsl -o "%TEMP%\MeshMaterial_%2.num" || exit /b 1
> MeshMaterial_%2_SPIRV.h (
	echo // Copyright ^(C^) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
	echo //
	echo // SPIR-V of MeshMaterial.hlsl ^(%2^), made by BuildMeshMaterialShaders.cmd: do not edit.
	echo.
	echo const unsigned int g_MeshMaterial_%2_SPIRV[] = {
	type "%TEMP%\MeshMaterial_%2.num"
	echo };
)
del "%TEMP%\MeshMaterial_%2.num"
exit /b 0
