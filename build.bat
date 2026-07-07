@echo off
setlocal EnableExtensions EnableDelayedExpansion

if /I "%1"=="clean" goto :CLEAN

set "WARNINGS=0"
choice /C YN /N /T 5 /D N /M "Show warnings..? y/n, auto n in 5 secs..."
if errorlevel 2 set "WARNINGS=0"
if errorlevel 1 if not errorlevel 2 set "WARNINGS=1"

echo.
echo === Checking Dependencies ===
git --version >nul 2>&1
if not errorlevel 1 goto :HAVE_GIT
echo.
echo [ERROR] Git is not installed or not found in PATH.
echo Please download and install Git from:
echo   https://git-scm.com/install/windows
echo Then re-run build.bat.
pause
exit /b 1

:HAVE_GIT
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
echo Dependencies cloned. Please re-run build.bat to compile.
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

echo.
echo === CPU Architecture Detection ===
call :DetectCPU
echo Using optimization flags: %MARCH%

echo.
echo === Extra Optimizations ===
choice /C YN /N /T 5 /D Y /M "Enable extra optimizations (faster code)? y/n, auto y in 5s..."
if not errorlevel 2 set "OPTFLAGS=-fomit-frame-pointer -ftree-vectorize -funroll-loops -ffunction-sections -fdata-sections"

echo === Building LuaJIT library ===
if not exist LuaJIT\src\libluajit.a (
    call build_luajit.bat "%GCC%"
    if errorlevel 1 exit /b 1
    echo Successfully built LuaJIT library
) else (
    echo LuaJIT library already built, skipping...
)

set "INCS=-Isrc -Iimgui -Iimgui/backends -Izstd/include -ILuaJIT/src -Ibullet3/src"
set "LIBS=-L. -lzstd -LLuaJIT/src -lluajit"

set "CFLAGS_BASE=-std=c++17 -mwindows %MARCH% %OPTFLAGS% -DEDITOR_BUILD"
if "%WARNINGS%"=="1" (
    set "CFLAGS=%CFLAGS_BASE% -Wall -Wextra -Wno-unused-parameter"
) else (
    set "CFLAGS=%CFLAGS_BASE% -w"
)
set "LFLAGS=-static-libgcc -static-libstdc++ -mwindows -Wl,--gc-sections -ld3d10 -ld3d11 -ld3dcompiler -ldxgi -luser32 -lkernel32 -ldwmapi -lcomdlg32 -luuid -lole32 -lshell32 -ldbghelp -lpsapi"

set "OBJS="
set "BULLET_OBJS= bullet3\src\btLinearMathAll.o bullet3\src\btBulletCollisionAll.o bullet3\src\btBulletDynamicsAll.o"
set "IMGUI_OBJS= imgui\imgui.o imgui\imgui_draw.o imgui\imgui_demo.o imgui\imgui_tables.o imgui\imgui_widgets.o imgui\backends\imgui_impl_win32.o imgui\backends\imgui_impl_dx10.o imgui\backends\imgui_impl_dx11.o"
set "LINK_NEEDED=0"
set "COMPILED_COUNT=0"

echo === Incremental compile: Bullet3 amalgamated ===
call :compile_if_needed "bullet3/src/btLinearMathAll.cpp" "bullet3\src\btLinearMathAll.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_if_needed "bullet3/src/btBulletCollisionAll.cpp" "bullet3\src\btBulletCollisionAll.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_if_needed "bullet3/src/btBulletDynamicsAll.cpp" "bullet3\src\btBulletDynamicsAll.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

echo === Incremental compile: src/ ===
for /R src %%f in (*.cpp) do (
    set "CPPFILE=%%f"
    set "OBJFILE=!CPPFILE:.cpp=.o!"
    call :compile_if_needed "!CPPFILE!" "!OBJFILE!"
    if errorlevel 1 exit /b 1
    if "!COMPILE_RESULT!"=="1" (
        set /a COMPILED_COUNT+=1
        set "LINK_NEEDED=1"
    )
    set "OBJS=!OBJS! !OBJFILE!"
)

echo === Incremental compile: imgui + backends ===
call :compile_if_needed "imgui/imgui.cpp" "imgui\imgui.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_if_needed "imgui/imgui_draw.cpp" "imgui\imgui_draw.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_if_needed "imgui/imgui_demo.cpp" "imgui\imgui_demo.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_if_needed "imgui/imgui_tables.cpp" "imgui\imgui_tables.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_if_needed "imgui/imgui_widgets.cpp" "imgui\imgui_widgets.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_if_needed "imgui/backends/imgui_impl_win32.cpp" "imgui\backends\imgui_impl_win32.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_if_needed "imgui/backends/imgui_impl_dx10.cpp" "imgui\backends\imgui_impl_dx10.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

call :compile_if_needed "imgui/backends/imgui_impl_dx11.cpp" "imgui\backends\imgui_impl_dx11.o"
if errorlevel 1 exit /b 1
if "%COMPILE_RESULT%"=="1" (set /a COMPILED_COUNT+=1 & set "LINK_NEEDED=1")

if not exist main.exe set "LINK_NEEDED=1"

if "%LINK_NEEDED%"=="1" (
    echo === Linking main.exe (changed files: %COMPILED_COUNT%) ===
    "%GCC%" -o main.exe %OBJS% %IMGUI_OBJS% %BULLET_OBJS% %LIBS% %LFLAGS%
    if errorlevel 1 (
        echo [FAILED] Linking main.exe
        exit /b 1
    )
) else (
    echo === No changes detected, skipping link ===
)

if exist main.exe (echo === Built main.exe ===) & dir main.exe

echo.
echo === Building PackAssets.exe ===
"%GCC%" -std=c++17 -O2 -Izstd/include tools/PackAssets.cpp -o PackAssets.exe -L. -lzstd -static-libgcc -static-libstdc++ -ldbghelp -lpsapi
if errorlevel 1 (
    echo [FAILED] Building PackAssets.exe
    exit /b 1
)
if exist PackAssets.exe (echo === Built PackAssets.exe ===) & dir PackAssets.exe

echo.
echo === Building game.exe (runtime, no editor code) ===
call build_game.bat "%CD%" "%WARNINGS%"
if errorlevel 1 exit /b 1

echo.
echo === Checking libzstd.dll ===
if not exist libzstd.dll (
    echo [WARNING] libzstd.dll not found. Place it in the project root for zstd support.
) else (
    echo libzstd.dll found
)

echo.
echo === Cleaning root g_*.o artifacts ===
for %%f in (g_*.o) do del /q "%%f" >nul 2>&1

goto :eof

exit /b 0

:CLEAN
echo.
echo === Cleaning all .o files ===
if exist "src" (
    for /R src %%f in (*.o) do (
        del /q "%%f" >nul 2>&1
        echo Deleted: %%f
    )
)
if exist "imgui" (
    for /R imgui %%f in (*.o) do (
        del /q "%%f" >nul 2>&1
        echo Deleted: %%f
    )
)
if exist "bullet3\src" (
    for %%f in (bullet3\src\*.o) do (
        del /q "%%f" >nul 2>&1
        echo Deleted: %%f
    )
)
if exist "main.exe" (
    del /q main.exe >nul 2>&1
    echo Deleted: main.exe
)
if exist "game.exe" (
    del /q game.exe >nul 2>&1
    echo Deleted: game.exe
)
if exist "PackAssets.exe" (
    del /q PackAssets.exe >nul 2>&1
    echo Deleted: PackAssets.exe
)
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

:compile_if_needed
set "COMPILE_RESULT=0"

if not exist "%~2" (
    echo [COMPILE] %~1
    "%GCC%" %CFLAGS% -c "%~1" %INCS% -o "%~2"
    if errorlevel 1 (
        echo [FAILED] %~1
        exit /b 1
    )
    set "COMPILE_RESULT=1"
    exit /b 0
)

powershell -NoProfile -Command "$src=(Get-Item '%~1').LastWriteTimeUtc; $obj=(Get-Item '%~2').LastWriteTimeUtc; if($src -gt $obj){exit 0}else{exit 1}"
if errorlevel 1 (
    echo [SKIP] %~1
    exit /b 0
)

echo [COMPILE] %~1
"%GCC%" %CFLAGS% -c "%~1" %INCS% -o "%~2"
if errorlevel 1 (
    echo [FAILED] %~1
    exit /b 1
)
set "COMPILE_RESULT=1"
exit /b 0
