@echo off
setlocal

cd /d "%~dp0"

set W64DEVKIT_DIR=build\w64devkit\bin
set "PATH=%W64DEVKIT_DIR%;%PATH%"

REM Convert backslashes to forward slashes for gcc
set RAYLIB_DIR=%cd%\build\raylib
set RAYLIB_DIR=%RAYLIB_DIR:\=/%

if /i "%1"=="debug"    goto debug
if /i "%1"=="release"  goto release
if /i "%1"=="clean"    goto clean
if "%1"==""            goto release

echo Usage: build [release^|debug^|clean]
echo   release  - Build release version (default, no console window)
echo   debug    - Build debug version
echo   clean    - Remove built executable
goto end

:release
echo Building RELEASE ...
make BUILD_MODE=RELEASE RAYLIB_DIR="%RAYLIB_DIR%"
if errorlevel 1 goto end
echo Copying bvhview.exe to C:\Tools\bvhview...
if not exist "C:\Tools\bvhview" mkdir "C:\Tools\bvhview"
copy /y bvhview.exe "C:\Tools\bvhview\bvhview.exe" >nul
echo Done.
goto end

:debug
echo Building DEBUG ...
make BUILD_MODE=DEBUG RAYLIB_DIR="%RAYLIB_DIR%"
if errorlevel 1 goto end
echo Copying bvhview.exe to C:\Tools\bvhview...
if not exist "C:\Tools\bvhview" mkdir "C:\Tools\bvhview"
copy /y bvhview.exe "C:\Tools\bvhview\bvhview.exe" >nul
echo Done.
goto end

:clean
echo Cleaning ...
make clean
goto end

:end
endlocal
