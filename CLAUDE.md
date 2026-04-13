# Claude instructions for Narina

Read [AGENTS.md](AGENTS.md) for project architecture, constraints, and conventions.

## Key rules

- **DspState offsets**: if you change `audio.h` DspState struct, update `dsp.asm` offsets to match
- **Audio callback**: no allocations, no blocking calls, no I/O inside `audio_callback`
- **Build**: run `build.bat` or replicate its commands. SDL2 and NASM must be in PATH
- **No co-author lines** in git commit messages
- **Finnish**: user communicates in Finnish, respond in Finnish for discussion, English for code/docs
