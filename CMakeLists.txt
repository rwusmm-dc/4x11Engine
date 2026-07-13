cmake_minimum_required(VERSION 3.15)
project(4x11Engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Put all outputs in the source root
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_SOURCE_DIR}")
endforeach()

# ----------------------------------------------------------------------------
# Options
# ----------------------------------------------------------------------------
option(ENABLE_WARNINGS "Show compiler warnings" OFF)
option(ENABLE_EXTRA_OPT "Enable extra optimizations" ON)
set(ENGINE_MARCH "native" CACHE STRING "Target -march value (e.g. native, haswell, nehalem)")

# Which targets to build (buildc.bat toggles these).
option(BUILD_EDITOR "Build editor main.exe" ON)
option(BUILD_GAME "Build runtime game.exe" ON)
option(BUILD_PACKASSETS "Build PackAssets.exe" ON)

# ----------------------------------------------------------------------------
# Common flags
# ----------------------------------------------------------------------------
set(_MARCH_FLAGS -O2)
if(ENGINE_MARCH)
    list(APPEND _MARCH_FLAGS -march=${ENGINE_MARCH})
endif()

set(_OPT_FLAGS "")
if(ENABLE_EXTRA_OPT)
    set(_OPT_FLAGS -fomit-frame-pointer -ftree-vectorize -funroll-loops
                   -ffunction-sections -fdata-sections)
endif()

if(ENABLE_WARNINGS)
    set(_WARN_FLAGS -Wall -Wextra -Wno-unused-parameter)
else()
    set(_WARN_FLAGS -w)
endif()

set(ENGINE_COMPILE_FLAGS ${_MARCH_FLAGS} ${_OPT_FLAGS} ${_WARN_FLAGS} -mwindows)

set(ENGINE_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
    ${CMAKE_SOURCE_DIR}/imgui/backends
    ${CMAKE_SOURCE_DIR}/zstd/include
    ${CMAKE_SOURCE_DIR}/zstd/lib
    ${CMAKE_SOURCE_DIR}/LuaJIT/src
    ${CMAKE_SOURCE_DIR}/bullet3/src)

if(EXISTS "${CMAKE_SOURCE_DIR}/libzstd.dll")
    set(ZSTD_LIB "${CMAKE_SOURCE_DIR}/libzstd.dll")
else()
    find_library(ZSTD_LIB zstd HINTS ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/zstd/lib)
    if(NOT ZSTD_LIB)
        set(ZSTD_LIB zstd)
    endif()
endif()
find_library(LUAJIT_LIB luajit HINTS ${CMAKE_SOURCE_DIR}/LuaJIT/src)
if(NOT LUAJIT_LIB)
    set(LUAJIT_LIB ${CMAKE_SOURCE_DIR}/LuaJIT/src/libluajit.a)
endif()

set(ENGINE_LINK_LIBS
    ${ZSTD_LIB}
    ${LUAJIT_LIB}
    d3d10 d3d11 d3dcompiler dxgi
    user32 kernel32 dwmapi comdlg32 uuid ole32 shell32 dbghelp psapi)

set(ENGINE_LINK_FLAGS "-static-libgcc -static-libstdc++ -mwindows -Wl,--gc-sections")

# ----------------------------------------------------------------------------
# Bullet3 amalgamated sources (shared)
# ----------------------------------------------------------------------------
set(BULLET_SOURCES
    ${CMAKE_SOURCE_DIR}/bullet3/src/btLinearMathAll.cpp
    ${CMAKE_SOURCE_DIR}/bullet3/src/btBulletCollisionAll.cpp
    ${CMAKE_SOURCE_DIR}/bullet3/src/btBulletDynamicsAll.cpp)

if(BUILD_EDITOR)
    file(GLOB_RECURSE EDITOR_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)

    set(IMGUI_SOURCES
        ${CMAKE_SOURCE_DIR}/imgui/imgui.cpp
        ${CMAKE_SOURCE_DIR}/imgui/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/imgui/imgui_demo.cpp
        ${CMAKE_SOURCE_DIR}/imgui/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/imgui/imgui_widgets.cpp
        ${CMAKE_SOURCE_DIR}/imgui/backends/imgui_impl_win32.cpp
        ${CMAKE_SOURCE_DIR}/imgui/backends/imgui_impl_dx10.cpp
        ${CMAKE_SOURCE_DIR}/imgui/backends/imgui_impl_dx11.cpp)

    add_executable(main WIN32 ${EDITOR_SRC} ${IMGUI_SOURCES} ${BULLET_SOURCES})
    set_target_properties(main PROPERTIES OUTPUT_NAME main)
    target_include_directories(main PRIVATE ${ENGINE_INCLUDES})
    target_compile_options(main PRIVATE ${ENGINE_COMPILE_FLAGS})
    target_compile_definitions(main PRIVATE EDITOR_BUILD)
    target_link_libraries(main PRIVATE ${ENGINE_LINK_LIBS})
    set_target_properties(main PROPERTIES LINK_FLAGS "${ENGINE_LINK_FLAGS}")
endif()

if(BUILD_GAME)
    set(GAME_SRC ${CMAKE_SOURCE_DIR}/src/main.cpp)
    foreach(_dir core ai d3d10 d3d11 ecs io phy renderer script)
        file(GLOB _files CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/${_dir}/*.cpp)
        list(APPEND GAME_SRC ${_files})
    endforeach()
    # main.cpp is already added explicitly; drop any duplicate from core glob.
    list(REMOVE_ITEM GAME_SRC ${CMAKE_SOURCE_DIR}/src/core/main.cpp)

    add_executable(game WIN32 ${GAME_SRC} ${BULLET_SOURCES})
    set_target_properties(game PROPERTIES OUTPUT_NAME game)
    target_include_directories(game PRIVATE ${ENGINE_INCLUDES})
    target_compile_options(game PRIVATE ${ENGINE_COMPILE_FLAGS})
    target_link_libraries(game PRIVATE ${ENGINE_LINK_LIBS})
    set_target_properties(game PROPERTIES LINK_FLAGS "${ENGINE_LINK_FLAGS}")
endif()

if(BUILD_PACKASSETS)
    add_executable(PackAssets ${CMAKE_SOURCE_DIR}/tools/PackAssets.cpp)
    target_include_directories(PackAssets PRIVATE
        ${CMAKE_SOURCE_DIR}/zstd/include
        ${CMAKE_SOURCE_DIR}/zstd/lib)
    target_compile_options(PackAssets PRIVATE -O2)
    target_link_libraries(PackAssets PRIVATE ${ZSTD_LIB} dbghelp psapi)
    set_target_properties(PackAssets PROPERTIES
        LINK_FLAGS "-static-libgcc -static-libstdc++")
endif()
