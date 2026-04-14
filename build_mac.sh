#!/bin/bash
set -e

# macOS build script for NARINA
# Requires: Xcode Command Line Tools, SDL2 (brew install sdl2)

CC=${CC:-cc}
CFLAGS="-Os -Wall -std=c99 -DDSP_C_FALLBACK $(sdl2-config --cflags)"
LDFLAGS="$(sdl2-config --libs) -lm"

echo "[1/3] Compiling main.c ..."
$CC $CFLAGS -c src/main.c -o main.o

echo "[2/3] Compiling audio.c ..."
$CC $CFLAGS -c src/audio.c -o audio.o

echo "[3/3] Compiling dsp.c (C fallback) ..."
$CC $CFLAGS -c src/dsp.c -o dsp.o

echo "[4/4] Linking narina ..."
$CC main.o audio.o dsp.o -o narina $LDFLAGS
strip narina

echo "Build OK: ./narina"
