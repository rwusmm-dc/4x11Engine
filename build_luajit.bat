@echo off
REM Builds LuaJIT for Windows/MinGW without requiring 'make'.
REM Replicates LuaJIT/src/Makefile's bootstrap: minilua -> buildvm -> headers -> lib.
REM IMPORTANT: LuaJIT is C code and MUST be compiled with gcc, not g++.

setlocal EnableExtensions EnableDelayedExpansion

set "GPP=%~1"
if "%GPP%"=="" (
    echo ERROR: compiler path not provided
    exit /b 1
)

for %%f in ("%GPP%") do set "GCCBIN=%%~dpf"
set "CC=%GCCBIN%gcc.exe"
if not exist "%CC%" (
    echo ERROR: Could not find gcc.exe next to %GPP%
    exit /b 1
)
echo Using C compiler: %CC%

echo.
echo === Extra Optimizations ===
if "%OPTFLAGS%"=="" (
    choice /C YN /N /T 5 /D Y /M "Enable extra optimizations (faster code)? y/n, auto y in 5s..."
    if not errorlevel 2 set "OPTFLAGS=-fomit-frame-pointer -ftree-vectorize -funroll-loops -ffunction-sections -fdata-sections"
)

pushd LuaJIT\src

echo Cleaning old build artifacts...
del *.o *.a host\*.o host\*.exe lj_vm.o lj_bcdef.h lj_ffdef.h lj_libdef.h lj_recdef.h lj_folddef.h host\buildvm_arch.h luajit.h luajit_relver.txt 2>nul

echo === [1/6] Generating version files ===
type ..\.relver > luajit_relver.txt

echo === [2/6] Building minilua (host tool) ===
start "" /b /wait "%CC%" -O2 -o host\minilua.exe host\minilua.c -lm
if not exist host\minilua.exe (
    call :prompt_ctrlc
    if errorlevel 1 popd & exit /b 1
)

host\minilua.exe host\genversion.lua
if errorlevel 1 (
    echo ERROR: Failed to generate luajit.h
    popd
    exit /b 1
)

echo === [3/6] Generating VM assembly header (DynASM) ===
host\minilua.exe ..\dynasm\dynasm.lua -D WIN -D JIT -D FFI -D ENDIAN_LE -D FPU -D P64 -o host\buildvm_arch.h vm_x64.dasc
if errorlevel 1 (
    echo ERROR: Failed to run DynASM
    popd
    exit /b 1
)

echo === [4/6] Building buildvm (host tool) ===
start "" /b /wait "%CC%" -O2 -I. -I..\dynasm -c host\buildvm.c       -o host\buildvm.o
if not exist host\buildvm.o call :ctrlc_or_fail
start "" /b /wait "%CC%" -O2 -I. -I..\dynasm -c host\buildvm_asm.c   -o host\buildvm_asm.o
if not exist host\buildvm_asm.o call :ctrlc_or_fail
start "" /b /wait "%CC%" -O2 -I. -I..\dynasm -c host\buildvm_peobj.c -o host\buildvm_peobj.o
if not exist host\buildvm_peobj.o call :ctrlc_or_fail
start "" /b /wait "%CC%" -O2 -I. -I..\dynasm -c host\buildvm_lib.c   -o host\buildvm_lib.o
if not exist host\buildvm_lib.o call :ctrlc_or_fail
start "" /b /wait "%CC%" -O2 -I. -I..\dynasm -c host\buildvm_fold.c  -o host\buildvm_fold.o
if not exist host\buildvm_fold.o call :ctrlc_or_fail

start "" /b /wait "%CC%" -O2 -o host\buildvm.exe host\buildvm.o host\buildvm_asm.o host\buildvm_peobj.o host\buildvm_lib.o host\buildvm_fold.o
if not exist host\buildvm.exe call :ctrlc_or_fail
goto :buildvm_ok
:buildvm_fail
echo ERROR: Failed to build buildvm
popd
exit /b 1
:buildvm_ok

echo === [5/6] Generating LuaJIT VM object and headers ===
set "LJLIB_C=lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c lib_package.c lib_debug.c lib_jit.c lib_ffi.c lib_buffer.c"

host\buildvm.exe -m peobj  -o lj_vm.o
if errorlevel 1 goto :hdr_fail
host\buildvm.exe -m bcdef  -o lj_bcdef.h %LJLIB_C%
if errorlevel 1 goto :hdr_fail
host\buildvm.exe -m ffdef  -o lj_ffdef.h %LJLIB_C%
if errorlevel 1 goto :hdr_fail
host\buildvm.exe -m libdef -o lj_libdef.h %LJLIB_C%
if errorlevel 1 goto :hdr_fail
host\buildvm.exe -m recdef -o lj_recdef.h %LJLIB_C%
if errorlevel 1 goto :hdr_fail
host\buildvm.exe -m folddef -o lj_folddef.h lj_opt_fold.c
if errorlevel 1 goto :hdr_fail
goto :hdr_ok
:hdr_fail
echo ERROR: Failed to generate LuaJIT headers
popd
exit /b 1
:hdr_ok

echo === [6/6] Compiling LuaJIT core as C and archiving ===
set "CFLAGS=-O2 -fomit-frame-pointer %OPTFLAGS% -I."
set "LJSRC=lj_assert.c lj_gc.c lj_err.c lj_char.c lj_bc.c lj_obj.c lj_buf.c lj_str.c lj_tab.c lj_func.c lj_udata.c lj_meta.c lj_debug.c lj_prng.c lj_state.c lj_dispatch.c lj_vmevent.c lj_vmmath.c lj_strscan.c lj_strfmt.c lj_strfmt_num.c lj_serialize.c lj_api.c lj_profile.c lj_lex.c lj_parse.c lj_bcread.c lj_bcwrite.c lj_load.c lj_ir.c lj_opt_mem.c lj_opt_fold.c lj_opt_narrow.c lj_opt_dce.c lj_opt_loop.c lj_opt_split.c lj_opt_sink.c lj_mcode.c lj_snap.c lj_record.c lj_crecord.c lj_ffrecord.c lj_asm.c lj_trace.c lj_gdbjit.c lj_ctype.c lj_cdata.c lj_cconv.c lj_ccall.c lj_ccallback.c lj_carith.c lj_clib.c lj_cparse.c lj_lib.c lj_alloc.c lib_aux.c lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c lib_package.c lib_debug.c lib_jit.c lib_ffi.c lib_buffer.c lib_init.c"

start "" /b /wait "%CC%" %CFLAGS% -c %LJSRC%
if not exist lj_assert.o (
    call :prompt_ctrlc
    if errorlevel 1 popd & exit /b 1
)

ar rcus libluajit.a lj_vm.o *.o
if errorlevel 1 (
    echo ERROR: Failed to create LuaJIT static library
    popd
    exit /b 1
)

echo Cleaning intermediate object files...
del *.o host\*.o 2>nul

popd
echo Successfully built LuaJIT\src\libluajit.a
exit /b 0

:prompt_ctrlc
choice /C YN /M "Do you want to exit the build ?"
if errorlevel 2 exit /b 0
exit /b 1

:ctrlc_or_fail
call :prompt_ctrlc
if errorlevel 1 (
    echo ERROR: Failed to build buildvm
    popd
    exit /b 1
)
exit /b 0
