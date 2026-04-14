# NARINA

Portable acid techno loop generator. Single .exe, no installation.

![Windows](https://img.shields.io/badge/platform-Windows%2064--bit-blue)

![screenshot](screenshot.png)

## What it does

Generates random acid techno loops with one button press. TB-303-style sawtooth oscillator through a resonant SVF filter with envelope modulation. DSP core written in x86-64 NASM assembly.

## Features

- **5 styles**: Dark Acid, Trance, Hardcore, Hypnotic, Hard Acid
- **10 scales**: minor pentatonic, blues, phrygian, minor, harmonic minor, dorian, chromatic, major, major pentatonic, lydian
- **29 rhythm templates** with per-step randomization
- **Percussion**: kick (sine pitch sweep), hihat (metallic oscillators + noise), clap (noise burst with flam)
- **Hihat loop generator**: rule-based closed/open patterns, regenerated on each toggle
- **Time signatures**: 4/4, 3/4, 6/8 with shuffle
- **Real-time controls**: BPM (80-300), cutoff, resonance, volume
- **Tap tempo**: click BPM label or press T
- **WAV recording** with unique filenames
- **Custom borderless UI** with bitmap font, window dragging, acid smiley icon

## Hotkeys

| Key | Action |
|-----|--------|
| Space | Play / Stop |
| Enter | Randomize |
| R | Record start / stop |
| T | Tap tempo |
| 1 | Kick toggle |
| 2 | Hihat toggle (generates new pattern) |
| 3 | Clap toggle |
| H | Help overlay |
| Esc | Close help |

## Building

### Requirements

- [MinGW-w64](https://winlibs.com/) (GCC)
- [NASM](https://www.nasm.us/)
- [SDL2 development libraries](https://github.com/libsdl-org/SDL/releases) (mingw, extract to project root)
- [UPX](https://upx.github.io/) (optional, for compression)

### Build

```
build.bat
```

Produces `narina.exe` (~1.7 MB, ~590 KB with UPX).

### Compress (optional)

```
upx --best narina.exe
```

## Architecture

```
src/
  main.c     - SDL2 window, UI, bitmap font, input handling
  audio.c    - Pattern generation, sequencer, percussion, audio callback
  audio.h    - Shared types (DspState, Step, Pattern, Sequencer)
  dsp.asm    - NASM x86-64 DSP core (oscillator, SVF filter, envelopes)
  narina.rc  - Windows resource file (icon)
tools/
  gen_icon.c - Generates narina.ico (acid smiley)
build.bat    - Build script
```

### DSP signal chain (NASM)

```
Sawtooth osc → Frequency slide → Filter envelope → SVF filter (+ tanh saturation) → Amp envelope → Output
```

### Audio callback flow

```
DSP block → Waveform copy → Percussion mix → Volume/mute → Record buffer
```

## License

[MIT](LICENSE)
