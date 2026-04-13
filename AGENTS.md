# Agents guide for Narina

## Project overview

Narina is a portable Windows acid techno loop generator. Single .exe, C + SDL2 + NASM x86-64.

## Stack

- **C99** (GCC MinGW-w64) for application logic
- **SDL2** (static) for window, rendering, input, audio device
- **NASM x86-64** for DSP hot loop (oscillator, filter, envelopes)
- **UPX** for exe compression

## File structure

| File | Purpose | Touch frequency |
|------|---------|-----------------|
| `src/audio.h` | All shared types and public API | Rarely - struct layout must match dsp.asm |
| `src/audio.c` | Pattern generation, sequencer, percussion, audio callback | Most changes happen here |
| `src/main.c` | SDL2 window, UI drawing, input, bitmap font | UI changes only |
| `src/dsp.asm` | NASM DSP core - oscillator, SVF filter, envelopes | Rarely - must match audio.h struct offsets |
| `src/narina.rc` | Windows resource (icon) | Never |
| `build.bat` | Build script | Rarely |
| `tools/gen_icon.c` | Icon generator | Never |

## Critical constraints

### DspState struct layout
The `DspState` struct in `audio.h` must match byte-for-byte with the offsets defined in `dsp.asm`. If you add/remove/reorder fields in the struct, update the `%define OFF_*` constants in the NASM file.

### Audio callback is lock-free
`audio_callback` in `audio.c` runs on a separate audio thread. No malloc, no printf, no file I/O, no mutexes inside it. Only exception: `SDL_LockAudioDevice` is used in `audio_rec_stop` to safely stop recording.

### Pattern double-buffer swap
Patterns use double-buffering. `audio_generate_pattern` writes to the inactive buffer, sets `pending_swap = 1`. The audio callback swaps atomically during the mute gap. Sonic presets (cutoff, resonance, envelope params) are stored per-pattern and applied at swap time.

### Thread safety
- Single float writes (volume, bpm) are atomic on x86-64
- Pattern swap uses volatile flags, no mutexes needed
- UI reads `g_dsp`/`g_seq` fields directly for slider sync
- Recording stop uses `SDL_LockAudioDevice` for safety

## Build

```
build.bat
```

Flags: `-Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections` for size. NASM: `-f win64`.

## Dependencies location

- SDL2: `SDL2-2.32.6/x86_64-w64-mingw32/` (not in repo, download from GitHub releases)
- raylib: `raylib-5.5_win64_mingw-w64/` (legacy, not used)

## Style

- Minimal C99, no unnecessary abstractions
- No external dependencies beyond SDL2 and system libs
- Bitmap font embedded in code (5x7 pixel glyphs)
- Audio synthesis in pure C except DSP hot loop (NASM)
- UI drawn with SDL2 renderer primitives (rects, lines, points)

## Disabled features (kept in code for future iteration)

- **Bassline system**: `BassState`, `BassPattern`, bass synth functions exist but `bass_enabled` is always 0 and there's no UI button. Code is functional if re-enabled.
