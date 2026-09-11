# Snowboard Kids 2 on the Power Mac G4

A native PowerPC port of the [Snowboard Kids 2 decompilation](https://github.com/cdlewis/snowboardkids2-decomp)
for a Quicksilver G4 running Mac OS X 10.5 with a Radeon 9000, built on the
platform layer of the [first game's port](https://github.com/southcitycapture/snowboardkids-decomp/tree/ppc-port).
The game sources are untouched; everything lives in this `port/` directory: a
libultra replacement, an interpreter for the display lists and the audio
command lists, a fixed-function OpenGL 1.3 backend, scripted input.

## Status (11 September 2026, first day)

| Milestone | State |
| --- | --- |
| Toolchain: all 166 game sources + libultra audio/gu/pfs + libmus cross-compile and link | done |
| First frame: boot, threads, ROM DMA, the attract demo rendering on the real G4 | done |
| Overlay dispatch: one N64 address, fourteen native level functions | done |
| Menus: title screen, file select (EEPROM), level preview, the story overworld | renders and responds |
| Audio: ABI 1 confirmed, aspMain byte-identical to the first game, samples produced | plays, not yet listened to |
| A race | not driven yet |
| Self-play, trials, the campaign navigator | not ported (see docs/PLAN.md) |

Screenshots taken on the G4 itself are in `~/Apps/islandPowerPC/g4-shots`:
`sbk2-boot-20s.png` (the attract demo), `sbk2-ov-45s.png` (the title screen
with the character line-up), `sbk2-title-55s.png` and `sbk2-save-52s.png` (the
file select, without and with the EEPROM), `sbk2-menu-45s.png` /
`sbk2-menu-65s.png` (the level preview), `sbk2-soak-a.png` (the story
overworld).

## Build and run

    ./build-mac.sh                # the upstream N64 build first (map, linker script, generated headers)
    port/build-ppc.sh -j6         # Docker cross build -> port/build-ppc-darwin/snowboardkids2
    port/tools/make_bundle.sh     # SnowboardKids2.app with the ROM in Resources

    snowboardkids2 [--fullscreen] [--windowed] [--nolauncher]
                   [--play SCRIPT|MOVIE.m64] [--record MOVIE.m64]
                   [--frames N] [--hashframe] [--mute] [--noaudio] [--wav OUT.wav]
                   [--pak FILE.mpk] [--nopak] [--nopad] [--trace] [--dumpdl N]
                   [--dumpframes N] [--dumptris] [--bigtri N] [--perf] [snowboardkids2.z64]

`--nopak` turns off the EEPROM as well as the Controller Pak, and the game then
says "Backup memory is corrupted" on the save screen -- which is how the EEPROM
was proved to work.

Scripts in `scripts/`: `title-start.txt`, `menu-walk.txt`, `menu-soak.txt`.

The save files live in `~/Library/Application Support/SnowboardKids2`:
`eeprom.sav` (the raw 512-byte EEPROM image emulators use) and
`controller-pak-1.mpk`.

## What is different from the first game's port

The short version: KMC GCC instead of IDO, F3DEX2 instead of F3DEX, an EEPROM,
and **overlays** -- every level is linked at the same address, so a native link
has to rebuild the indirection the N64 got for free. `docs/PLAN.md` has the
long version, the survey facts and the gotchas.

## Licence note

`src/gfx/gfx_pc.c` derives from sm64-port, whose licence allows source
distribution only. Share this branch as source; do not distribute binaries.
