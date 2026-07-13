@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "OUTPUT=%~1"
if "%OUTPUT%"=="" set "OUTPUT=."
set "WARNINGS=%~2"
if "%WARNINGS%"=="" set "WARNINGS=0"

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

echo === CPU Architecture Detection (Game Build) ===
call :DetectCPU
echo Using optimization flags: %MARCH%

echo.
echo === Extra Optimizations ===
choice /C YN /N /T 5 /D Y /M "Enable extra optimizations (faster code)? y/n, auto y in 5s..."
if not errorlevel 2 set "OPTFLAGS=-fomit-frame-pointer -ftree-vectorize -funroll-loops -ffunction-sections -fdata-sections"

echo === Checking LuaJIT library ===
if not exist "LuaJIT\src\libluajit.a" (
    echo === Building LuaJIT library ===
    call build_luajit.bat "%GCC%"
    if errorlevel 1 exit /b 1
)

set "INCS=-Isrc -Iimgui -Iimgui/backends -Izstd/include -Izstd/lib -ILuaJIT/src -Ibullet3/src"
set "LIBS=-L. -Lzstd/lib -lzstd -LLuaJIT/src -lluajit"
set "CFLAGS_BASE=-std=c++17 -mwindows %MARCH% %OPTFLAGS%"
if "%WARNINGS%"=="1" (
    set "CFLAGS=%CFLAGS_BASE% -Wall -Wextra -Wno-unused-parameter"
) else (
    set "CFLAGS=%CFLAGS_BASE% -w"
)
set "LFLAGS=-static-libgcc -static-libstdc++ -mwindows -Wl,--gc-sections -ld3d10 -ld3d11 -ld3dcompiler -ldxgi -luser32 -lkernel32 -ldwmapi -lcomdlg32 -luuid -lole32 -lshell32 -ldbghelp -lpsapi"

set "GAME_OBJS="
set "BULLET_OBJS=%OUTPUT%\g_btLinearMathAll.o %OUTPUT%\g_btBulletCollisionAll.o %OUTPUT%\g_btBulletDynamicsAll.o"
set "LINK_NEEDED=0"
set "COMPILED_COUNT=0"

echo === [GAME] Incremental compile ===

call :compile_game_if_needed "bullet3/src/btLinearMathAll.cpp" "%OUTPUT%\g_btLinearMathAll.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_game_if_needed "bullet3/src/btBulletCollisionAll.cpp" "%OUTPUT%\g_btBulletCollisionAll.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_game_if_needed "bullet3/src/btBulletDynamicsAll.cpp" "%OUTPUT%\g_btBulletDynamicsAll.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
echo === [GAME] NOTE: Excluding UI/Gizmo files (ImGui-dependent) ===

call :compile_game_if_needed "src/main.cpp" "%OUTPUT%\g_main.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
set "GAME_OBJS=!GAME_OBJS! "%OUTPUT%\g_main.o""

REM === CORE FILES ===
for %%f in (src\core\*.cpp) do (
    set "CPPFILE=%%f"
    set "FILENAME=%%~nxf"
    if /i not "!FILENAME!"=="main.cpp" (
        set "RELPATH=!CPPFILE:src\=!"
        set "OBJNAME=!RELPATH:\=_!"
        set "OBJNAME=!OBJNAME:.cpp=.o!"
        set "OBJFILE=%OUTPUT%\g_!OBJNAME!"
        call :compile_game_if_needed "!CPPFILE!" "!OBJFILE!"
        if errorlevel 1 exit /b 1
        if "!COMPILE_RESULT!"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
        set "GAME_OBJS=!GAME_OBJS! "!OBJFILE!""
    )
)

REM === AI FILES ===
for %%f in (src\ai\*.cpp) do (
    set "CPPFILE=%%f"
    set "RELPATH=!CPPFILE:src\=!"
    set "OBJNAME=!RELPATH:\=_!"
    set "OBJNAME=!OBJNAME:.cpp=.o!"
    set "OBJFILE=%OUTPUT%\g_!OBJNAME!"
    call :compile_game_if_needed "!CPPFILE!" "!OBJFILE!"
    if errorlevel 1 exit /b 1
    if "!COMPILE_RESULT!"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
    set "GAME_OBJS=!GAME_OBJS! "!OBJFILE!""
)

REM === D3D10 FILES ===
for %%f in (src\d3d10\*.cpp) do (
    set "CPPFILE=%%f"
    set "RELPATH=!CPPFILE:src\=!"
    set "OBJNAME=!RELPATH:\=_!"
    set "OBJNAME=!OBJNAME:.cpp=.o!"
    set "OBJFILE=%OUTPUT%\g_!OBJNAME!"
    call :compile_game_if_needed "!CPPFILE!" "!OBJFILE!"
    if errorlevel 1 exit /b 1
    if "!COMPILE_RESULT!"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
    set "GAME_OBJS=!GAME_OBJS! "!OBJFILE!""
)

REM === D3D11 FILES ===
for %%f in (src\d3d11\*.cpp) do (
    set "CPPFILE=%%f"
    set "RELPATH=!CPPFILE:src\=!"
    set "OBJNAME=!RELPATH:\=_!"
    set "OBJNAME=!OBJNAME:.cpp=.o!"
    set "OBJFILE=%OUTPUT%\g_!OBJNAME!"
    call :compile_game_if_needed "!CPPFILE!" "!OBJFILE!"
    if errorlevel 1 exit /b 1
    if "!COMPILE_RESULT!"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
    set "GAME_OBJS=!GAME_OBJS! "!OBJFILE!""
)

REM === ECS FILES ===
for %%f in (src\ecs\*.cpp) do (
    set "CPPFILE=%%f"
    set "RELPATH=!CPPFILE:src\=!"
    set "OBJNAME=!RELPATH:\=_!"
    set "OBJNAME=!OBJNAME:.cpp=.o!"
    set "OBJFILE=%OUTPUT%\g_!OBJNAME!"
    call :compile_game_if_needed "!CPPFILE!" "!OBJFILE!"
    if errorlevel 1 exit /b 1
    if "!COMPILE_RESULT!"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
    set "GAME_OBJS=!GAME_OBJS! "!OBJFILE!""
)

REM === IO FILES ===
for %%f in (src\io\*.cpp) do (
    set "CPPFILE=%%f"
    set "RELPATH=!CPPFILE:src\=!"
    set "OBJNAME=!RELPATH:\=_!"
    set "OBJNAME=!OBJNAME:.cpp=.o!"
    set "OBJFILE=%OUTPUT%\g_!OBJNAME!"
    call :compile_game_if_needed "!CPPFILE!" "!OBJFILE!"
    if errorlevel 1 exit /b 1
    if "!COMPILE_RESULT!"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
    set "GAME_OBJS=!GAME_OBJS! "!OBJFILE!""
)

REM === PHYSICS FILES ===
for %%f in (src\phy\*.cpp) do (
    set "CPPFILE=%%f"
    set "RELPATH=!CPPFILE:src\=!"
    set "OBJNAME=!RELPATH:\=_!"
    set "OBJNAME=!OBJNAME:.cpp=.o!"
    set "OBJFILE=%OUTPUT%\g_!OBJNAME!"
    call :compile_game_if_needed "!CPPFILE!" "!OBJFILE!"
    if errorlevel 1 exit /b 1
    if "!COMPILE_RESULT!"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
    set "GAME_OBJS=!GAME_OBJS! "!OBJFILE!""
)

REM === RENDERER FILES ===
for %%f in (src\renderer\*.cpp) do (
    set "CPPFILE=%%f"
    set "RELPATH=!CPPFILE:src\=!"
    set "OBJNAME=!RELPATH:\=_!"
    set "OBJNAME=!OBJNAME:.cpp=.o!"
    set "OBJFILE=%OUTPUT%\g_!OBJNAME!"
    call :compile_game_if_needed "!CPPFILE!" "!OBJFILE!"
    if errorlevel 1 exit /b 1
    if "!COMPILE_RESULT!"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
    set "GAME_OBJS=!GAME_OBJS! "!OBJFILE!""
)

REM === SCRIPT FILES ===
for %%f in (src\script\*.cpp) do (
    set "CPPFILE=%%f"
    set "RELPATH=!CPPFILE:src\=!"
    set "OBJNAME=!RELPATH:\=_!"
    set "OBJNAME=!OBJNAME:.cpp=.o!"
    set "OBJFILE=%OUTPUT%\g_!OBJNAME!"
    call :compile_game_if_needed "!CPPFILE!" "!OBJFILE!"
    if errorlevel 1 exit /b 1
    if "!COMPILE_RESULT!"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")
    set "GAME_OBJS=!GAME_OBJS! "!OBJFILE!""
)

if not exist "%OUTPUT%\game.exe" set "LINK_NEEDED=1"

if "%LINK_NEEDED%"=="1" (
    echo === [GAME] Linking game.exe (changed files: %COMPILED_COUNT%) ===
    "%GCC%" -o "%OUTPUT%\game.exe" !GAME_OBJS! %BULLET_OBJS% %LIBS% %LFLAGS%
    if errorlevel 1 (
        echo [FAILED] Linking game.exe
        exit /b 1
    )
    echo === [GAME] Built game.exe ===
) else (
    echo === [GAME] No changes detected, skipping link ===
)

echo === [GAME] Cleaning root g_*.o artifacts ===
for %%f in (g_*.o) do del /q "%%f" >nul 2>&1

exit /b 0

:compile_game_if_needed
set "COMPILE_RESULT=0"

if not exist "%~2" (
    echo [GAME][COMPILE] %~1
    "%GCC%" %CFLAGS% -c "%~1" %INCS% -o "%~2"
    if errorlevel 1 (
        echo [GAME][FAILED] %~1
        exit /b 1
    )
    set "COMPILE_RESULT=1"
    exit /b 0
)

powershell -NoProfile -Command "$src=(Get-Item '%~1').LastWriteTimeUtc; $obj=(Get-Item '%~2').LastWriteTimeUtc; if($src -gt $obj){exit 0}else{exit 1}"
if errorlevel 1 (
    echo [GAME][SKIP] %~1
    exit /b 0
)

echo [GAME][COMPILE] %~1
"%GCC%" %CFLAGS% -c "%~1" %INCS% -o "%~2"
if errorlevel 1 (
    echo [GAME][FAILED] %~1
    exit /b 1
)
set "COMPILE_RESULT=1"
exit /b 0

:DetectCPU
echo Detecting CPU capabilities from PROCESSOR_IDENTIFIER...
echo %PROCESSOR_IDENTIFIER% | findstr /I "AVX512" >nul
if %errorlevel%==0 (
    echo   ✓ AVX-512 detected
    set "MARCH=-march=skylake-avx512 -O2"
    exit /b 0
)
echo %PROCESSOR_IDENTIFIER% | findstr /I "AVX2" >nul
if %errorlevel%==0 (
    echo   ✓ Haswell or newer detected (AVX2/FMA3 support found)
    set "MARCH=-march=haswell -O2"
    exit /b 0
)
echo %PROCESSOR_IDENTIFIER% | findstr /I "AVX" >nul
if %errorlevel%==0 (
    echo   ✓ Sandy Bridge/Ivy Bridge detected (AVX support found)
    set "MARCH=-march=corei7 -O2"
    exit /b 0
)
echo   ℹ No AVX detected, using SSE4.2 (Nehalem compatible)
set "MARCH=-march=nehalem -O2"
exit /b 0

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
