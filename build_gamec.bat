@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ===========================================================================
REM  build_gamec.bat - CMake-based runtime build (replaces raw build_game.bat)
REM  Builds only game.exe (runtime, no editor/ImGui code) via CMake + MinGW.
REM ===========================================================================

if /I "%1"=="clean" goto :CLEAN

set "BUILD_DIR=build_cmake_game"

set "WARNINGS=%~1"
if /I "%WARNINGS%"=="1" (set "WARNINGS=ON") else (set "WARNINGS=OFF")

echo === Checking Dependencies ===
cmake --version >nul 2>&1
if not errorlevel 1 goto :HAVE_CMAKE
echo.
echo [ERROR] CMake is not installed or not found in PATH.
echo Please download and install CMake from:
echo   https://cmake.org/download/
pause
exit /b 1

:HAVE_CMAKE
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
echo === CPU Architecture Detection (Game Build) ===
call :DetectCPU
echo Using -march=%MARCH%

echo.
echo === Extra Optimizations ===
set "EXTRA_OPT=OFF"
choice /C YN /N /T 5 /D Y /M "Enable extra optimizations (faster code)? y/n, auto y in 5s..."
if not errorlevel 2 set "EXTRA_OPT=ON"

echo === Checking LuaJIT library ===
if not exist "LuaJIT\src\libluajit.a" (
    echo === Building LuaJIT library ===
    call build_luajit.bat "%GCC%"
    if errorlevel 1 exit /b 1
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
    -DBUILD_EDITOR=OFF -DBUILD_GAME=ON -DBUILD_PACKASSETS=OFF
if errorlevel 1 (
    echo [FAILED] CMake configure
    exit /b 1
)

echo.
echo === [GAME] Building game.exe ===
cmake --build "%BUILD_DIR%" --target game --parallel
if errorlevel 1 (
    echo [FAILED] CMake build (game.exe)
    exit /b 1
)

if exist game.exe (echo === [GAME] Built game.exe ===) & dir game.exe
exit /b 0

:CLEAN
echo.
echo === Cleaning game CMake build tree and game.exe ===
if exist "build_cmake_game" (
    rmdir /s /q "build_cmake_game"
    echo Deleted: build_cmake_game
)
if exist "game.exe" del /q game.exe >nul 2>&1 & echo Deleted: game.exe
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
