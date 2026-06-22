@echo off
setlocal

cd /d "%~dp0"

set W64DEVKIT_DIR=build\w64devkit\bin
set "PATH=%W64DEVKIT_DIR%;%PATH%"

REM Convert backslashes to forward slashes for gcc
set RAYLIB_DIR=%cd%\build\raylib
set RAYLIB_DIR=%RAYLIB_DIR:\=/%

set WEBP_DIR=%cd%\build\libwebp
set WEBP_DIR=%WEBP_DIR:\=/%

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
call :ensure_webp
if errorlevel 1 goto end
echo Building RELEASE ...
make BUILD_MODE=RELEASE RAYLIB_DIR="%RAYLIB_DIR%" WEBP_DIR="%WEBP_DIR%"
if errorlevel 1 goto end
echo Copying bvhview.exe to C:\Tools\bvhview...
if not exist "C:\Tools\bvhview" mkdir "C:\Tools\bvhview"
copy /y bvhview.exe "C:\Tools\bvhview\bvhview.exe" >nul
echo Done.
goto end

:debug
call :ensure_webp
if errorlevel 1 goto end
echo Building DEBUG ...
make BUILD_MODE=DEBUG RAYLIB_DIR="%RAYLIB_DIR%" WEBP_DIR="%WEBP_DIR%"
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

:ensure_webp
if exist "build\libwebp\src\webp\decode.h" exit /b 0
echo Downloading libwebp 1.6.0 ...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "$build = Join-Path (Get-Location) 'build';" ^
  "$zip = Join-Path $build 'libwebp-1.6.0.zip';" ^
  "$source = Join-Path $build 'libwebp-1.6.0';" ^
  "$target = Join-Path $build 'libwebp';" ^
  "Invoke-WebRequest 'https://github.com/webmproject/libwebp/archive/refs/tags/v1.6.0.zip' -OutFile $zip;" ^
  "Expand-Archive -LiteralPath $zip -DestinationPath $build -Force;" ^
  "if (Test-Path $target) { Remove-Item -LiteralPath $target -Recurse -Force };" ^
  "Move-Item -LiteralPath $source -Destination $target;" ^
  "Remove-Item -LiteralPath $zip -Force"
if errorlevel 1 (
    echo ERROR: Failed to download libwebp.
    exit /b 1
)
exit /b 0

:end
endlocal
