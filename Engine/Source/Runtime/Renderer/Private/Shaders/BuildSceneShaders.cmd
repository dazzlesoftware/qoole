@echo off
rem Regenerates the SPIR-V headers (SceneShaders_*_SPIRV.h) used by the Vulkan
rem backend from SceneShaders.hlsl. Run it after changing the shaders. (The DXIL
rem used by D3D12 is compiled at build time by Renderer.vcxproj.)
rem
rem Needs glslc (shaderc, which reads HLSL): the Vulkan SDK's, or the Android
rem NDK's (shader-tools\windows-x86_64\glslc.exe). The headers in the
rem repository were made with the NDK r27c's. The dxc.exe in the Windows SDK
rem cannot emit SPIR-V.
rem
rem Usage: BuildSceneShaders.cmd [path\to\glslc.exe]
rem        (defaults to %VULKAN_SDK%\Bin\glslc.exe)

setlocal
set GLSLC=%~1
if "%GLSLC%"=="" if defined VULKAN_SDK set GLSLC=%VULKAN_SDK%\Bin\glslc.exe
if "%GLSLC%"=="" (
	echo usage: %~nx0 path\to\glslc.exe
	exit /b 1
)

cd /d "%~dp0"
call :Compile vert VSMain || exit /b 1
call :Compile vert VSFoliage || exit /b 1
call :Compile frag PSTextured || exit /b 1
call :Compile frag PSLandscape || exit /b 1
call "%~dp0BuildMeshMaterialShaders.cmd" "%GLSLC%" || exit /b 1
cd /d "%~dp0"
exit /b 0

rem :Compile stage entry -- SceneShaders_<entry>_SPIRV.h, an array of words.
:Compile
"%GLSLC%" -x hlsl -DMAP_SPIRV -fshader-stage=%1 -fentry-point=%2 --target-env=vulkan1.1 -O -mfmt=num SceneShaders.hlsl -o "%TEMP%\SceneShaders_%2.num" || exit /b 1
> SceneShaders_%2_SPIRV.h (
	echo // Copyright ^(C^) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
	echo //
	echo // SPIR-V of SceneShaders.hlsl ^(%2^), made by BuildSceneShaders.cmd: do not edit.
	echo.
	echo const unsigned int g_SceneShaders_%2_SPIRV[] = {
	type "%TEMP%\SceneShaders_%2.num"
	echo };
)
del "%TEMP%\SceneShaders_%2.num"
exit /b 0
