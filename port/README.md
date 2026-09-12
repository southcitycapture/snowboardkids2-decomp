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
| 2D: the title logo, the tile-map backgrounds, sprites and the race HUD | matches the reference |
| Audio: ABI 1 confirmed, aspMain byte-identical to the first game, samples produced | plays, not yet listened to |
| A race | played end to end by the game's own CPU rider, first place |
| Self-play: `--racedbg`, `--peek`, `--autoplay`, `--soak`, `--nightmare` | works |
| The navigator: `--autonav`, `--menutrace`, `--saveevery`, `--trial`, `--plan` | works; the campaign wins a course, writes the EEPROM, leaves the town by the right door and aims itself at the next course |
| The lift wedge that stopped the campaign on Turtle Island | fixed -- it was the Nightmare row starving the task pool, not the borrowed path table. `docs/nightmare-row.md` |
| The boss races (courses 3 and 7): ten snowman heads, not a finish line | won by the boss pilot -- `docs/PLAN.md`, "The boss races, which no handicap can win" |
| The campaign end to end (every course, a save after each race) | in progress: courses 0-7 won on the user's own save, both bosses included. Course 8 resisted for six races and the cause was two bugs, not a missing handicap: the boost lever never reached a race (`initPlayer` recomputes the rider's stats after the handoff) and the rider was on the worst-handling board in the game. It is **won**, on the user's own save, at relief 165 + tax 60 and no boost at all -- first place, 405,600 gold, EEPROM written (`g4-shots/sbk2-campaign-8.png`). `docs/PLAN.md`, "What the levers actually do, measured" |
| Course 9, the Haunted House | **the wedge is fixed.** Thirteen trials had wedged at sector 50 under every rung, path slot and board, and with `--nightmare` off; a pin autopsy (`--racedbg`, `sbk-pin:`) ruled out the ghost, the pendulum, the push zones, the other riders and the task pool, and named the real cause: `updatePlayerNormalDriving` sends any **CPU** rider whose speed drops below 0x5FFFF away from the lift to behaviour phase 4 (the ollie) and returns before `calculateAITargetPosition`, so a slow CPU rider cannot steer or re-aim. Not a port bug, and a human never hits it -- the human branch is a button test. Self-play is a CPU rider, so it does. Fixed by a marshal in `race_dbg.c` that pushes player 1 back over the threshold along the track graph. The course finishes now. `docs/PLAN.md`, "Course 9, the Haunted House" |
| The three Cross minigames (the gate on the last two courses) | the navigator enters them and knows each one's real pass mark; Shot Cross has a pilot, because a rider alone on a course has nothing to aim at. Not yet reached by a campaign |

The title screen matches the emulator reference frame
(`g4-shots/sbk2-s2dex-fix1.png` against Mupen64Plus's `snowboard_kids2-020.png`):
logo, background, menu, copyright lines, Rumble Pak badge. Until the combiner
fix in `gfx_pc.c` (see `docs/PLAN.md`, "a combiner input gfx_pc did not have")
everything drawn under the game's sprite setup list was transparent black.

## Screenshots (taken on the G4)

| | |
| --- | --- |
| ![Title](docs/screenshots/title.png) | ![Race](docs/screenshots/race-hud.png) |
| The title screen: logo, snow background, menu, copyright, Rumble Pak badge. Pixel-for-pixel the layout of the emulator's frame. | A race with the full HUD: lap, item slots, place, the progress bar with rider heads, coins, and a sky that reaches the horizon. |
| ![File select](docs/screenshots/file-select.png) | |
| The file select, drawn from the 512-byte EEPROM image. | |

More, uncropped, are in `~/Apps/islandPowerPC/g4-shots`.

## Build and run

    ./build-mac.sh                # the upstream N64 build first (map, linker script, generated headers)
    port/build-ppc.sh -j6         # Docker cross build -> port/build-ppc-darwin/snowboardkids2
    port/tools/make_bundle.sh     # SnowboardKids2.app with the ROM in Resources

    snowboardkids2 [--fullscreen] [--windowed] [--nolauncher]
                   [--play SCRIPT|MOVIE.m64] [--record MOVIE.m64]
                   [--frames N] [--hashframe] [--mute] [--noaudio] [--wav OUT.wav]
                   [--pak FILE.mpk] [--nopak] [--eeprom FILE] [--unlockall]
                   [--nopad] [--trace] [--dumpdl N]
                   [--s2dextrace]
                   [--dumpframes N] [--dumptris] [--bigtri N] [--perf]
                   [--racedbg] [--peek ADDR:LEN] [--autoplay] [--soak] [--nightmare]
                   [--autonav] [--menutrace] [--saveevery N] [--status]
                   [--startrung N] [--nobosspilot] [--bosssupply N]
                   [--trial SPEC] [--plan LEVEL:CHAR:BOARD:BOOST,...]
                   [snowboardkids2.z64]

`--nopak` turns off the EEPROM as well as the Controller Pak, and the game then
says "Backup memory is corrupted" on the save screen -- which is how the EEPROM
was proved to work. `--eeprom FILE` is the exception: a scratch save file that
works alongside `--nopak`, so an unattended trial never touches the real
`eeprom.sav`. `--unlockall` runs the game's own `unlockAllContent` cheat on
whatever save block is loaded, so the course list offers every course; it
belongs with `--eeprom` and nowhere near a save you want to keep.

Scripts in `scripts/`: `title-start.txt`, `menu-walk.txt`, `menu-soak.txt`.

## Self-play

The game beta-tests itself. `--autoplay` hands player 1 to the game's own CPU
rider; `--nightmare` retunes a spare row of `gAIPlayerParams` so every rider
throws items hard and carries no speed handicap, and gives player 1 the fastest
board -- the row's numbers are **searched, not guessed**, and
`docs/nightmare-row.md` says what they are and what happens when they go off
the end of the game's own scale (the race stops ending); `--autonav` walks the menus by name and drives the campaign
between races; `--soak` is the same with the save point skipped: the town cannot be left by pressing A (A walks into the rider picker and out again for ever), so a soak uses the navigator too, and only writes the EEPROM if `--saveevery` says so.

    snowboardkids2 --fullscreen --autonav --autoplay --nightmare --saveevery 1

`--menutrace` prints the sequel's menu map as it moves -- every screen is named
with `dladdr()` off the task scheduler's `gamestateHandler`, so there is no
hand-written table. `--racedbg` prints the four riders once a second and
`--peek ADDR:LEN` dumps RDRAM.

**Boss races.** Courses 3 and 7 are decided by the boss's ten snowman heads,
not by the finish line -- our own rider crossing it does nothing -- and the CPU
rider can never throw the one item that takes a head off. The boss pilot
(`port/src/debug/boss_pilot.c`, on by default with `--autoplay`, off with
`--nobosspilot`) hooks the game's own `processPlayerItemUsage` and throws the
star when the boss is in range and inside a firing window worked out from the
star's flight; the pan, which cannot miss, is what the navigator's boss ladder
supplies after a loss (`--bosssupply N`, retraces between hand-outs). Every
conjured item is logged and counted against the ones the rider picked up itself.
`docs/PLAN.md`, "The boss races, which no handicap can win", has the chain.

`--autonav` plays the campaign, not one course: it leaves Jingle Town by the
door that leads to the course list (`unk427 = 0xFF`, not a location id -- see
`docs/PLAN.md`) and parks the list on whichever course the game has marked
`levelUnlockStatus == 5`, which is its own "next up". When the course list has
nothing left it walks into the three Cross minigames instead -- they are
buildings in the town, not courses, and slot 10 does not open until all three
are won. They are also not races: each has its own pass mark (`playerLost`,
twenty targets, 300 skill points) and `finishPosition` says 1st either way.

**A handicap applied at the handoff is not applied to the race.** `initPlayer`
re-runs `applyCharacterSnowboardStats` a second or two after `initRace`, which
wiped the retune -- the boost lever did nothing at all for the first six
courses. It is held now, and `--racedbg`'s `spd=<current>/<baseMaxSpeed>` is
where to check that a lever survived into the race.

`--trial level=N,char=C,board=B,boost=X,relief=R,tax=T,pathslot=S,quit=1` runs
one race as an experiment and prints a result line -- place, frames, gold, and
the per-rider count of frames spent scraping a track wall. A trial takes about
**thirty seconds** of wall clock on the G4 and is deterministic, so the
handicap levers are worth sweeping rather than reasoning about; `relief` and
`tax` pin themselves against the navigator's ladder so a rung can be
reproduced exactly.

 `port/tools/nightmare_search.py` sweeps trials on the
G4, keeps `port/tools/nightmare_results.csv`, records golden movies into
`port/scripts/golden/` and replays them (`regress`). `docs/PLAN.md`,
"Self-play", has the state the tooling keys on and why.

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
distribution only (`src/gfx/gfx_s2dex.c`, the S2DEX interpreter beside it, is
this port's own code). Share this branch as source; do not distribute binaries.
