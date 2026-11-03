#!/bin/sh
# Cross-compile Black Cauldron Enhanced for Windows (x86-64) with clang + mingw-w64 + SDL3,
# and the headless test runner for the build machine.
set -e
CC=${CC:-/opt/mingw/bin/x86_64-w64-mingw32-clang}
RC=${RC:-/opt/mingw/bin/x86_64-w64-mingw32-windres}
SDL=${SDL:-/opt/sdl3-win}
ENGINE="src/res.c src/objects.c src/text.c src/logic.c src/engine.c"
mkdir -p build/win
$RC -I src src/bce.rc -O coff -o build/win/bce_res.o
$CC -O2 -std=c11 -Wall -Wno-unused-function \
  -I$SDL/include -Isrc -o build/win/BC-Enhanced.exe \
  src/main_sdl.c src/crt_gl.c src/sound.c $ENGINE build/win/bce_res.o \
  $SDL/lib/libSDL3.a -lm -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 \
  -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 -mwindows -static
echo "built build/win/BC-Enhanced.exe"
cc -O2 -std=c11 -Wall -Wno-unused-function -o build/headless tools/headless.c $ENGINE
echo "built build/headless"
