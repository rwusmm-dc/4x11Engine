@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "OUTPUT=%~1"
if "%OUTPUT%"=="" set "OUTPUT=."

call :FindGCC
if "%GCC%"=="" echo ERROR: Could not find g++ (TDM-GCC), searched C:\TDM-GCC* and PATH & exit /b 1

set "INCS=-Isrc -Iimgui -Iimgui/backends -Izstd/include -ILuaJIT/src"
set "LIBS=-Lzstd/dll -lzstd -LLuaJIT/src -lluajit"
set "CFLAGS=-std=c++17 -Wall -Wextra -Wno-unused-parameter -mwindows -O2"
set "LFLAGS=-static-libgcc -static-libstdc++ -mwindows -ld3d10 -ld3d11 -ld3dcompiler -ldxgi -luser32 -lkernel32 -ldwmapi -lcomdlg32 -luuid -lole32 -lshell32"

set "GAME_OBJS="

echo === [GAME] Compiling src/io/Archive.cpp ===
"%GCC%" %CFLAGS% -c src/io/Archive.cpp %INCS% -o "%OUTPUT%\g_Archive.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_Archive.o"

echo === [GAME] Compiling src/script/ScriptEngine.cpp ===
"%GCC%" %CFLAGS% -c src/script/ScriptEngine.cpp %INCS% -o "%OUTPUT%\g_ScriptEngine.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_ScriptEngine.o"

echo === [GAME] Compiling src/main.cpp ===
"%GCC%" %CFLAGS% -c src/main.cpp %INCS% -o "%OUTPUT%\g_main.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_main.o"

echo === [GAME] Compiling src/core/Window.cpp ===
"%GCC%" %CFLAGS% -c src/core/Window.cpp %INCS% -o "%OUTPUT%\g_Window.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_Window.o"

echo === [GAME] Compiling src/core/FPSCamera.cpp ===
"%GCC%" %CFLAGS% -c src/core/FPSCamera.cpp %INCS% -o "%OUTPUT%\g_FPSCamera.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_FPSCamera.o"

echo === [GAME] Compiling src/core/CullingSystem.cpp ===
"%GCC%" %CFLAGS% -c src/core/CullingSystem.cpp %INCS% -o "%OUTPUT%\g_CullingSystem.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_CullingSystem.o"

echo === [GAME] Compiling src/phy/Physics.cpp ===
"%GCC%" %CFLAGS% -c src/phy/Physics.cpp %INCS% -o "%OUTPUT%\g_Physics.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_Physics.o"

echo === [GAME] Compiling src/ecs/ECS.cpp ===
"%GCC%" %CFLAGS% -c src/ecs/ECS.cpp %INCS% -o "%OUTPUT%\g_ECS.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_ECS.o"

echo === [GAME] Compiling src/d3d10/Device.cpp ===
"%GCC%" %CFLAGS% -c src/d3d10/Device.cpp %INCS% -o "%OUTPUT%\g_d3d10_Device.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_d3d10_Device.o"

echo === [GAME] Compiling src/d3d10/Pipeline.cpp ===
"%GCC%" %CFLAGS% -c src/d3d10/Pipeline.cpp %INCS% -o "%OUTPUT%\g_d3d10_Pipeline.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_d3d10_Pipeline.o"

echo === [GAME] Compiling src/d3d11/Device.cpp ===
"%GCC%" %CFLAGS% -c src/d3d11/Device.cpp %INCS% -o "%OUTPUT%\g_d3d11_Device.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_d3d11_Device.o"

echo === [GAME] Compiling src/d3d11/Pipeline.cpp ===
"%GCC%" %CFLAGS% -c src/d3d11/Pipeline.cpp %INCS% -o "%OUTPUT%\g_d3d11_Pipeline.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_d3d11_Pipeline.o"

echo === [GAME] Compiling src/d3d11/skybox.cpp ===
"%GCC%" %CFLAGS% -c src/d3d11/skybox.cpp %INCS% -o "%OUTPUT%\g_d3d11_skybox.o"
if errorlevel 1 exit /b 1
set "GAME_OBJS=%GAME_OBJS% %OUTPUT%\g_d3d11_skybox.o"

echo === [GAME] Linking game.exe ===
"%GCC%" -o "%OUTPUT%\game.exe" %GAME_OBJS% %LIBS% %LFLAGS%
if errorlevel 1 exit /b 1

echo === [GAME] Built game.exe ===

rem Clean up object files
for %%f in ("%OUTPUT%\g_*.o") do del "%%f" >nul 2>&1

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
