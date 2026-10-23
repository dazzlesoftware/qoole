@echo off
rem Compatibility entry point; shaders are owned by Runtime/Renderer.
call "%~dp0..\..\..\..\Runtime\Renderer\Private\Shaders\BuildSceneShaders.cmd" %*
exit /b %errorlevel%
