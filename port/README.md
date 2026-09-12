# Snowboard Kids 2 on the Power Mac G4

A native PowerPC port of the [Snowboard Kids 2 decompilation](https://github.com/cdlewis/snowboardkids2-decomp)
for a Quicksilver G4 running Mac OS X 10.5 with a Radeon 9000, built on the
platform layer of the [first game's port](https://github.com/southcitycapture/snowboardkids-decomp/tree/ppc-port).
The game sources are untouched; everything lives in this `port/` directory: a
libultra replacement, an interpreter for the display lists and the audio
command lists, a fixed-function OpenGL 1.3 backend, scripted input.

## Status (12 September 2026)

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
| The campaign end to end (every course, a save after each race) | **finished. Every course in the game is won on the user's own save** -- `sbk-nav: progress [111111111111] won=12`, 737,350 gold, and the credits roll. `docs/PLAN.md` has the campaign table |
| Course 9, the Haunted House | **the wedge is fixed.** Thirteen trials had wedged at sector 50 under every rung, path slot and board, and with `--nightmare` off; a pin autopsy (`--racedbg`, `sbk-pin:`) ruled out the ghost, the pendulum, the push zones, the other riders and the task pool, and named the real cause: `updatePlayerNormalDriving` sends any **CPU** rider whose speed drops below 0x5FFFF away from the lift to behaviour phase 4 (the ollie) and returns before `calculateAITargetPosition`, so a slow CPU rider cannot steer or re-aim. Not a port bug, and a human never hits it -- the human branch is a button test. Self-play is a CPU rider, so it does. Fixed by a marshal in `race_dbg.c` that pushes player 1 back over the threshold along the track graph. The course finishes now. `docs/PLAN.md`, "Course 9, the Haunted House" |
| The three Cross minigames (the gate on the last two courses) | **all three are won on the user's own save.** Shoot Cross took an aiming shot pilot (the course does not aim for you and its targets push the rider away rather than pull it in) plus one target twenty-one million units above the road that is reached by moving it in front of the rider for one shot, capped and counted. **X Cross** turned out to be a port bug and not a game rule: `beginPostTrickLaunchStep` puns three adjacent globals as a `Vec3i` and the Mach-O linker laid them out *descending*, so every jump in the port -- by every rider on every course -- was launched with two words of unrelated data. With the pun made real in `patches.txt` the rider scores 435 of the 300 it needs. **Speed Cross** is 87.8 seconds of a ninety-second clock on handling, cornering and lateral deadzone; top speed was never the lever, because `race_main.c:854` clamps every rider to 0x180000. `docs/PLAN.md`, "X Cross", "Speed Cross" |
| Course 11, the Ice Land boss (the last course, and the credits) | **won, with one declared handicap.** Four heads of thirteen for ten races running, and the cause this file used to give -- "the rider takes a worse line than the boss and gets knocked off it", read off `wall=14,0,0,0` -- was wrong. A per-sector census (`--sectorlog`, one line per rider per sector) puts the two riders side by side: **wall 22 retraces, hop 938, stun 1808** against a boss that never touches a wall, never hops and is never stunned. The rider loses five sectors in the first three hundred frames to one knockback and the Haunted House lock-out, and our rider and the boss cruise at very nearly the same speed, so it never gets them back. Fixed as far as it can be fixed: a lock-out breaker armed on the lock-out itself (the marshal cannot reach a rider that is hopping *downhill*, because that rider is still making progress), the throw window widened to the star's own homing capture radius rather than the boss's collision node, the trigger cut to the star's real flight time, and a governor that will not let the rider overtake -- `updateIceLandBoss` is a rubber band gated at 0xE00000 whose wind-up is +0x1000 a frame against a wind-down of -0x80, so one overtake hands the boss the game's own 0x180000 ceiling for longer than the race lasts. The last of it is a handicap and is logged as one in every race it applies to: `--bossslow 48` holds the boss's own speed cap at 48% of what `updateIceLandBoss` just asked for, and nothing else changes. `boss hp=0 defeated=1, 13 hp drops from 74 throws`, and the credits roll for four minutes on the real G4 (`g4-shots/sbk2-credits.png`, `sbk2-credits-mid.png`). `docs/PLAN.md`, "Course 11: the Ice Land boss, in full" |
| **A real port bug a human hits: the ollie** | `beginPostTrickLaunchStep` builds the jump impulse in three separate file-scope globals and hands the address of the first to `transformVector2`, which reads three consecutive `s32` as a `Vec3i`. On the N64 they sit in declaration order and the pun is exact; the Mach-O linker laid them out **descending** (0xb428, 0xb424, 0xb420), so every jump in the port, by every rider on every course, was launched with two words of somebody else's data. Nothing crashes and nothing looks obviously wrong -- the rider just never gets the air the game intends, and multiplying `baseAcceleration` by five changes the launch by not one unit, which is the measurement that says the field is not being read at all. Fixed in `patches.txt` by making the three one array. `docs/PLAN.md`, "The audit for the ollie bug's siblings", sweeps the rest of the source for the same shape |
| The audit for more of that class | done. All 1,477 file-scope globals swept for every construct that reads outside one global. **Pinning protects almost all of it** -- a pinned global keeps its N64 address in the emulated RDRAM, so its neighbours are still its N64 neighbours -- and only the 248 unpinned ones are exposed. Two are read across: the ollie, and `gCharacterEffectSpawnPointsBoard`, four snowboard corners declared `s16[16]` and read as 24 `s16`, whose last two corners live in `gShortcutChanceByMemoryPool` on the N64. One more of the family turned up without any pointer arithmetic to give it away: the cutscene category count, read as a big-endian `u16` across two `u8`s that the port puts in different *sections*, 132 KB apart |

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
                   [--shotdbg] [--shotsnap N] [--shotrange N] [--shotcooldown N]
                   [--shotcarry N] [--shotdetour N] [--notrickpilot] [--trickperiod N]
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

**The Cross minigames.** They are buildings in Jingle Town, not courses, and
each is first a clock: 150 seconds on Shoot and Speed Cross, 90 on X Cross and
a second 90 on Speed Cross's own race timer, after which `playerLost` is 1
whatever the score is. So the Cross ladder is three ladders, one per game,
kept per Cross game across the rotation between them -- and they do not share
a rung, because the levers that pass them are different levers.

On Shoot Cross the shot pilot hangs off the same hook as the boss pilot and
does the aiming the course will not do: it walks the twenty targets, writes the
rider's heading straight at the nearest one still standing for two frames, and
puts it back. `--shotdbg` prints the target table and a line a second;
`--shotsnap`, `--shotrange`, `--shotcooldown`, `--shotcarry` and `--shotdetour`
are its knobs, and `--noshotpilot` turns it off. On X Cross the trick pilot
(`--notrickpilot`, `--trickperiod N`, `--trickflags N`, `--tricktarget N`)
answers `determineAIPathChoice` for our rider so it jumps at all, and then
stops once it has banked enough skill points, because X Cross is two tests --
300 points *and* a ninety-second clock -- and a rider that tricks the whole
street scores 2,610 and still fails.

Five levers reach the rider now rather than one. `--trial boost=N` was the
only handicap for eight courses and it is a top-speed lever, which is the
wrong lever for both of the last two Cross games: `accel`, `hand`, `corner`
and `dead` put the same 1/256ths on `baseAcceleration`, `handling`,
`cornering` (which takes a negative -- it is the *drag* a turn costs) and
`lateralDeadzone`. `--trial level=12|13|14` runs one of them on its own.

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
