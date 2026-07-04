@echo off
setlocal EnableExtensions EnableDelayedExpansion

call :FindGCC
if "%GCC%"=="" echo ERROR: Could not find g++ (TDM-GCC), searched C:\TDM-GCC* and PATH & exit /b 1
echo Using compiler: %GCC%

set "INCS=-Isrc -Iimgui -Iimgui/backends -Izstd/include -ILuaJIT/src"
set "LIBS=-Lzstd/dll -lzstd -LLuaJIT/src -lluajit"

set "CFLAGS=-std=c++17 -Wall -Wextra -Wno-unused-parameter -mwindows -O2 -DEDITOR_BUILD"
set "LFLAGS=-static-libgcc -static-libstdc++ -mwindows -ld3d10 -ld3d11 -ld3dcompiler -ldxgi -luser32 -lkernel32 -ldwmapi -lcomdlg32 -luuid -lole32 -lshell32"

set "OBJS="

echo === Cleaning stale object files ===
if exist src\d3d11\Pipeline.o     del src\d3d11\Pipeline.o
if exist src\d3d11\skybox.o         del src\d3d11\skybox.o
REM CloudLayer removed
if exist src\d3d10\Pipeline.o     del src\d3d10\Pipeline.o
if exist src\main.o               del src\main.o
if exist src\ui\Overlay.o         del src\ui\Overlay.o
if exist src\gizmo\Gizmo.o        del src\gizmo\Gizmo.o
if exist src\phy\Physics.o         del src\phy\Physics.o
if exist src\core\CullingSystem.o del src\core\CullingSystem.o
if exist src\core\Project.o       del src\core\Project.o
if exist src\io\Archive.o          del src\io\Archive.o
if exist src\script\ScriptEngine.o del src\script\ScriptEngine.o
if exist src\core\SdkMeshLoader.o del src\core\SdkMeshLoader.o

echo === Compiling src/io/Archive.cpp ===
"%GCC%" %CFLAGS% -c src/io/Archive.cpp %INCS% -o src\io\Archive.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\io\Archive.o"

echo === Compiling src/script/ScriptEngine.cpp ===
"%GCC%" %CFLAGS% -c src/script/ScriptEngine.cpp %INCS% -o src\script\ScriptEngine.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\script\ScriptEngine.o"

echo === Compiling src/core/Project.cpp ===
"%GCC%" %CFLAGS% -c src/core/Project.cpp %INCS% -o src\core\Project.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\core\Project.o"

echo === Compiling src/main.cpp ===
"%GCC%" %CFLAGS% -c src/main.cpp %INCS% -o src\main.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\main.o"

echo === Compiling src/core/Window.cpp ===
"%GCC%" %CFLAGS% -c src/core/Window.cpp %INCS% -o src\core\Window.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\core\Window.o"

echo === Compiling src/core/FPSCamera.cpp ===
"%GCC%" %CFLAGS% -c src/core/FPSCamera.cpp %INCS% -o src\core\FPSCamera.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\core\FPSCamera.o"

echo === Compiling src/core/ObjLoader.cpp ===
"%GCC%" %CFLAGS% -c src/core/ObjLoader.cpp %INCS% -o src\core\ObjLoader.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\core\ObjLoader.o"

echo === Compiling src/core/SdkMeshLoader.cpp ===
"%GCC%" %CFLAGS% -c src/core/SdkMeshLoader.cpp %INCS% -o src\core\SdkMeshLoader.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\core\SdkMeshLoader.o"

echo === Compiling src/core/CullingSystem.cpp ===
"%GCC%" %CFLAGS% -c src/core/CullingSystem.cpp %INCS% -o src\core\CullingSystem.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\core\CullingSystem.o"

echo === Compiling src/phy/Physics.cpp ===
"%GCC%" %CFLAGS% -c src/phy/Physics.cpp %INCS% -o src\phy\Physics.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\phy\Physics.o"

echo === Compiling src/ecs/ECS.cpp ===
"%GCC%" %CFLAGS% -c src/ecs/ECS.cpp %INCS% -o src\ecs\ECS.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\ecs\ECS.o"

echo === Compiling src/d3d10/Device.cpp ===
"%GCC%" %CFLAGS% -c src/d3d10/Device.cpp %INCS% -o src\d3d10\Device.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\d3d10\Device.o"

echo === Compiling src/d3d10/Pipeline.cpp ===
"%GCC%" %CFLAGS% -c src/d3d10/Pipeline.cpp %INCS% -o src\d3d10\Pipeline.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\d3d10\Pipeline.o"

echo === Compiling src/d3d11/Device.cpp ===
"%GCC%" %CFLAGS% -c src/d3d11/Device.cpp %INCS% -o src\d3d11\Device.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\d3d11\Device.o"

echo === Compiling src/d3d11/Pipeline.cpp ===
"%GCC%" %CFLAGS% -c src/d3d11/Pipeline.cpp %INCS% -o src\d3d11\Pipeline.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\d3d11\Pipeline.o"

echo === Compiling src/d3d11/skybox.cpp ===
"%GCC%" %CFLAGS% -c src/d3d11/skybox.cpp %INCS% -o src\d3d11\skybox.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\d3d11\skybox.o"

REM CloudLayer removed (was src/d3d11/CloudLayer.cpp and src/d3d10/CloudLayer.cpp)

echo === Compiling src/ui/Overlay.cpp ===
"%GCC%" %CFLAGS% -c src/ui/Overlay.cpp %INCS% -o src\ui\Overlay.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\ui\Overlay.o"

echo === Compiling src/ui/ProjectManagerUI.cpp ===
"%GCC%" %CFLAGS% -c src/ui/ProjectManagerUI.cpp %INCS% -o src\ui\ProjectManagerUI.o
if errorlevel 1 exit /b 1
set "OBJS=%OBJS% src\ui\ProjectManagerUI.o"

call :try_compile imgui/imgui.cpp imgui\imgui.o
call :try_compile imgui/imgui_draw.cpp imgui\imgui_draw.o
call :try_compile imgui/imgui_tables.cpp imgui\imgui_tables.o
call :try_compile imgui/imgui_widgets.cpp imgui\imgui_widgets.o
call :try_compile imgui/backends/imgui_impl_win32.cpp imgui\backends\imgui_impl_win32.o
call :try_compile imgui/backends/imgui_impl_dx10.cpp imgui\backends\imgui_impl_dx10.o
call :try_compile imgui/backends/imgui_impl_dx11.cpp imgui\backends\imgui_impl_dx11.o
call :try_compile src/gizmo/Gizmo.cpp src\gizmo\Gizmo.o
call :try_compile src/ui/CodeEditor.cpp src\ui\CodeEditor.o

echo === Linking main.exe ===
"%GCC%" -o main.exe %OBJS% imgui\imgui.o imgui\imgui_draw.o imgui\imgui_tables.o imgui\imgui_widgets.o imgui\backends\imgui_impl_win32.o imgui\backends\imgui_impl_dx10.o imgui\backends\imgui_impl_dx11.o src\gizmo\Gizmo.o src\ui\CodeEditor.o %LIBS% %LFLAGS%
if errorlevel 1 exit /b 1

if exist main.exe (echo === Built main.exe ===) & dir main.exe

echo.
echo === Building PackAssets.exe ===
"%GCC%" -std=c++17 -O2 -Izstd/include tools/PackAssets.cpp -o PackAssets.exe -Lzstd/dll -lzstd -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1
if exist PackAssets.exe (echo === Built PackAssets.exe ===) & dir PackAssets.exe

echo.
echo === Building game.exe (runtime, no editor code) ===
call build_game.bat "%CD%"
if errorlevel 1 exit /b 1

echo.
echo === Copying libzstd.dll ===
if not exist libzstd.dll (
    copy zstd\dll\libzstd.dll libzstd.dll >nul
    echo Copied libzstd.dll to project root
) else (
    echo libzstd.dll already present
)

goto :eof

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

:try_compile
if exist "%~2" (
    echo   SKIPPED %~1 (already compiled^)
    exit /b 0
)
if not exist "%~2" (
    echo === Compiling %~1 ===
    "%GCC%" %CFLAGS% -c "%~1" %INCS% -o "%~2"
    if errorlevel 1 exit /b 1
)
exit /b 0