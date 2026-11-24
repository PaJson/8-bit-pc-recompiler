# References and links

*Moved here word for word from CLAUDE.md on 2026-11-24 (the dates in the text are when each note was written; later notes can correct earlier ones). CLAUDE.md keeps the rules for every task and lists all notes files.*

## Useful / ruled-out links (checked 2026-11-12)
- https://www.smspower.org/Hacks/Golvellius-SMS-Bugfix - Golvellius ROM data layout only (overworld 13x14 screens of 15x12 tiles at file `0xC000-0x13FFC`, room table near `0x15CB9`); no mapper/code info. Minor use.
- **Ruled out** (nothing for SMS/Golvellius): msx.org "disassembly-rom-toolchain" (mentions z80dasm, Ghidra, Emulicious (auto-labels, loads symbols), openMSX debugger) and "msx-games-dissasembled-source-code-archives".
- https://github.com/NationalSecurityAgency/ghidra - has a Z80 module but no SMS bank-switch awareness (import each 16 KB bank as an overlay block by hand). Prefer Emulicious' banking-aware debugger/tracer for manual investigation; the runner's `dispatch_misses.log` (failing addr + slot banks) is the first thing to read.
- **Emulicious** (the user keeps it outside the project since 2026-11-15; banking-aware debugger/tracer; use it to trace a computed jump when static reading is not enough).
- https://codeberg.org/chn/MasterlatorSDL/ - SMS/GG emulator + debugger (C/C++, SDL2, wla-dx .sym support), **GPL-3.0**. Was extracted in `Masterlator/` (never used; deleted 2026-11-15): **use it only as a tool to run/inspect ROMs** (disassembly, breakpoints, VDP/tile view). Never copy its source into this engine (license clash).
## Audio references (for sound problems)

- https://github.com/PaJson/banjo (user's fork — contents not reviewed yet)
- https://github.com/FigBug/SN76489 — SN76489 PSG emulation reference
- https://consolemods.org/wiki/Master_System:FM_Audio_Information — YM2413 FM add-on (Japanese SMS) info
- https://www.smspower.org/forums/6237-MasterSystemSoundHardwareDetails — SMS sound hardware details thread
Relevant PS facts: sound driver lives in **bank 12 at `$8000`** (`ps1disasm-master/sound/sound_driver.asm`, entry `Snd_UpdateAll=$8043`, init `$8000`), called through `CallSndUpdate/Init/Mute` at `$02E5/$02F5/...`; command byte at RAM `$C004` (e.g. `$80` = idle/stop, `$81` = title music); channel structs at `$C06E+` (`$20` bytes each). Runner PSG: `smsggrecomp/runner/audio/sn76489.*` (clean-room).
