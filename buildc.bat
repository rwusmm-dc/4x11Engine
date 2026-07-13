@echo off
setlocal EnableExtensions EnableDelayedExpansion


REM  CMake-based builb

if /I "%1"=="clean" goto :CLEAN

set "BUILD_DIR=build_cmake"

set "WARNINGS=OFF"
choice /C YN /N /T 5 /D N /M "Show warnings..? y/n, auto n in 5 secs..."
if errorlevel 2 set "WARNINGS=OFF"
if errorlevel 1 if not errorlevel 2 set "WARNINGS=ON"

echo.
echo === Checking Dependencies ===
git --version >nul 2>&1
if not errorlevel 1 goto :HAVE_GIT
echo.
echo [ERROR] Git is not installed or not found in PATH.
echo Please download and install Git from:
echo   https://git-scm.com/install/windows
echo Then re-run buildc.bat.
pause
exit /b 1

:HAVE_GIT
cmake --version >nul 2>&1
if not errorlevel 1 goto :HAVE_CMAKE
echo.
echo [ERROR] CMake is not installed or not found in PATH.
echo Please download and install CMake from:
echo   https://cmake.org/download/
echo Then re-run buildc.bat.
pause
exit /b 1

:HAVE_CMAKE
if not exist "bullet3" goto :CLONE_DEPS
if not exist "imgui" goto :CLONE_DEPS
if not exist "LuaJIT" goto :CLONE_DEPS
if exist "zstd" goto :DEPS_OK
:CLONE_DEPS
echo.
echo Some dependency folders are missing. Cloning from GitHub...
echo.
if not exist "bullet3" git clone --depth 1 https://github.com/bulletphysics/bullet3 bullet3
if not exist "imgui" git clone --depth 1 https://github.com/ocornut/imgui imgui
if not exist "LuaJIT" git clone --depth 1 https://github.com/luajit/luajit LuaJIT
if not exist "zstd" git clone --depth 1 https://github.com/facebook/zstd zstd
echo.
echo Dependencies cloned. Please re-run buildc.bat to compile.
pause
exit /b 0

:DEPS_OK
call :FindGCC
if not errorlevel 1 goto :HAVE_GCC
echo.
echo [ERROR] TDM-GCC (g++) not found.
echo Please download and install from:
echo   https://jmeubank.github.io/tdm-gcc/
echo Or use the DirectXMath bundle:
echo   https://github.com/rwusmm-dc/directx-examples/releases/download/dxmath/tdm-gcc-directxmathInstaller.exe
pause
exit /b 1

:HAVE_GCC
echo Using compiler: %GCC%
for %%f in ("%GCC%") do set "GCCBIN=%%~dpf"
set "CC=%GCCBIN%gcc.exe"

echo.
echo === CPU Architecture Detection ===
call :DetectCPU
echo Using -march=%MARCH%

echo.
echo === Extra Optimizations ===
set "EXTRA_OPT=OFF"
choice /C YN /N /T 5 /D Y /M "Enable extra optimizations (faster code)? y/n, auto y in 5s..."
if not errorlevel 2 set "EXTRA_OPT=ON"

echo === Building LuaJIT library ===
if not exist LuaJIT\src\libluajit.a (
    call build_luajit.bat "%GCC%"
    if errorlevel 1 exit /b 1
    echo Successfully built LuaJIT library
) else (
    echo LuaJIT library already built, skipping...
)

echo.
echo === Configuring with CMake (MinGW Makefiles) ===
cmake -S . -B "%BUILD_DIR%" -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_COMPILER="%GCC%" ^
    -DCMAKE_C_COMPILER="%CC%" ^
    -DENGINE_MARCH=%MARCH% ^
    -DENABLE_WARNINGS=%WARNINGS% ^
    -DENABLE_EXTRA_OPT=%EXTRA_OPT% ^
    -DBUILD_EDITOR=ON -DBUILD_GAME=ON -DBUILD_PACKASSETS=ON
if errorlevel 1 (
    echo [FAILED] CMake configure
    exit /b 1
)

echo.
echo === Building all targets ===
cmake --build "%BUILD_DIR%" --parallel
if errorlevel 1 (
    echo [FAILED] CMake build
    exit /b 1
)

if exist main.exe (echo === Built main.exe ===) & dir main.exe
if exist game.exe (echo === Built game.exe ===) & dir game.exe
if exist PackAssets.exe (echo === Built PackAssets.exe ===) & dir PackAssets.exe

echo.
echo === Checking libzstd.dll ===
if not exist libzstd.dll (
    echo [WARNING] libzstd.dll not found. Place it in the project root for zstd support.
) else (
    echo libzstd.dll found
)

goto :eof

:CLEAN
echo.
echo === Cleaning CMake build tree and binaries ===
if exist "build_cmake" (
    rmdir /s /q "build_cmake"
    echo Deleted: build_cmake
)
if exist "main.exe" del /q main.exe >nul 2>&1 & echo Deleted: main.exe
if exist "game.exe" del /q game.exe >nul 2>&1 & echo Deleted: game.exe
if exist "PackAssets.exe" del /q PackAssets.exe >nul 2>&1 & echo Deleted: PackAssets.exe
echo === Clean complete ===
goto :eof

:FindGCC
set "GCC="
set "GCC_CANDIDATES=C:\TDM-GCC\bin\g++.exe C:\TDM-GCC-64\bin\g++.exe C:\TDM-GCC-32\bin\g++.exe C:\mingw64\bin\g++.exe C:\mingw32\bin\g++.exe"
for %%c in (%GCC_CANDIDATES%) do (
    if exist "%%c" (
        set "GCC=%%c"
        exit /b 0
    )
)
for /d %%d in (C:\TDM-GCC* C:\mingw* C:\MinGW* 2^>nul) do (
    if exist "%%d\bin\g++.exe" (
        set "GCC=%%d\bin\g++.exe"
        exit /b 0
    )
)
where g++ >nul 2>&1
if not errorlevel 1 (
    for /f "delims=" %%p in ('where g++ 2^>nul') do (
        set "GCC=%%p"
        exit /b 0
    )
)
exit /b 1

:DetectCPU
echo Detecting CPU capabilities from PROCESSOR_IDENTIFIER...
echo %PROCESSOR_IDENTIFIER% | findstr /I "AVX512" >nul
if %errorlevel%==0 (
    echo   AVX-512 detected
    set "MARCH=skylake-avx512"
    exit /b 0
)
echo %PROCESSOR_IDENTIFIER% | findstr /I "AVX2" >nul
if %errorlevel%==0 (
    echo   Haswell or newer detected (AVX2/FMA3 support found)
    set "MARCH=haswell"
    exit /b 0
)
echo %PROCESSOR_IDENTIFIER% | findstr /I "AVX" >nul
if %errorlevel%==0 (
    echo   Sandy Bridge/Ivy Bridge detected (AVX support found)
    set "MARCH=corei7"
    exit /b 0
)
echo   No AVX detected, using SSE4.2 (Nehalem compatible)
set "MARCH=nehalem"
exit /b 0
