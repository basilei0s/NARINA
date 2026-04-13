@echo off
setlocal

:: Find SDL2 directory (first match)
for /d %%d in (SDL2-*) do set SDL2_DIR=%%d
if not defined SDL2_DIR (
    echo ERROR: SDL2 directory not found. Download SDL2-devel-mingw and extract here.
    exit /b 1
)

set SDL2_INC=%SDL2_DIR%\x86_64-w64-mingw32\include\SDL2
set SDL2_LIB=%SDL2_DIR%\x86_64-w64-mingw32\lib
set CFLAGS=-Os -Wall -std=c99 -flto -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -I%SDL2_INC%
set LDFLAGS=-L%SDL2_LIB% -lmingw32 -lSDL2main -lSDL2 -mwindows -s -flto -Wl,--gc-sections -static -lsetupapi -lole32 -loleaut32 -limm32 -lversion -lgdi32 -lwinmm -luser32

echo [1/5] Assembling dsp.asm (NASM) ...
nasm -f win64 src\dsp.asm -o dsp.obj
if errorlevel 1 goto :fail

echo [2/5] Compiling main.c ...
gcc %CFLAGS% -c src\main.c -o main.obj
if errorlevel 1 goto :fail

echo [3/5] Compiling audio.c ...
gcc %CFLAGS% -c src\audio.c -o audio.obj
if errorlevel 1 goto :fail

echo [4/5] Compiling resource (icon) ...
windres src\narina.rc -o res.obj
if errorlevel 1 goto :fail

echo [5/5] Linking narina.exe ...
gcc main.obj audio.obj dsp.obj res.obj -o narina.exe %LDFLAGS%
if errorlevel 1 goto :fail

echo Build OK: narina.exe
goto :eof

:fail
echo BUILD FAILED
exit /b 1
