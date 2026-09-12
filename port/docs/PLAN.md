# Snowboard Kids 2: native port to the Power Mac G4

Goal: run the Snowboard Kids 2 decompilation natively on the Quicksilver G4
(Leopard 10.5, Radeon 9000), next to the finished port of the first game. The
blueprint is `~/Apps/snowboardkids-decomp/port` on branch `ppc-port`; this
document records only what is *different* here, plus the facts about the sequel
that drive the design. For the shared design (pinning, the coroutine libultra,
the display-list interpreter, the GL 1.3 backend) read the first game's
`port/docs/PLAN.md` first — all of it still applies.

## Architecture

```
 game code (src/*, 166 files)                                    unchanged
 libultra audio synthesizer + gu + pfs (lib/ultralib/src)        unchanged C
 libmus player_fx (lib/libmus/src)                               unchanged C
 ---------------------------------------------------------------------------
 port/src/ultra   libultra replacement: threads as coroutines, message queues,
                  events, VI, SP task dispatch, PI DMA from the ROM file, AI,
                  controllers, Controller Pak, EEPROM, overlay dispatch
 port/src/gfx     gfx_pc.c display-list interpreter (F3DEX_GBI_2) + the
                  fixed-function OpenGL 1.3 backend + SDL2 window/input
 port/src/audio   interpreter for the aspMain (ABI 1) audio command lists
 port/src/main.c  host loop: run threads, deliver retraces/events, pace 60 Hz
```

## What the sequel does differently

### Toolchain

* **KMC GCC 2.7.2, not IDO.** Fewer compiler-isms to absorb, and upstream
  already has the switch for "this is not the KMC compiler": `CC_CHECK`, which
  its Makefile sets for the clang syntax-check pass. `-DCC_CHECK=1` turns off
  the MIPS register pinning (`register s32 x __asm__("$22")`), the register
  clobber lists and the `AUDIO_REG` pins, and enables the clang pragmas the
  decomp uses to silence a missing return. The port compiles with it.
* **No `#pragma weak`.** The first game had fifteen IDO-only aliases that
  crashed GCC on Mach-O; the sequel has none of its own. Only libultra's
  `gu/sinf.c` and `gu/cosf.c` alias, and the host's libm already supplies
  `sinf`/`cosf`, so `port/src/port_aliases.c` is down to `fsin`/`fcos`.
* **`__attribute__((section(".bss")))` everywhere.** `include/common.h`'s `BSS`
  macro (and `BSS_ALIGN` in `src/common_bss.c`) forces zero-initialised globals
  into `.bss` so the decomp can match the original data layout. Darwin's
  assembler rejects `.section .bss` ("Expected comma after segment-name"), and
  the attribute buys the port nothing — every one of those globals is pinned
  into the emulated RDRAM anyway — so `mirror_src.py` strips it, keeping any
  alignment it was paired with. The macro *definitions* are left alone; the
  strip is line-wise and skips `#` lines.
* **Four MIPS-isms a PowerPC compiler cannot take** (`port/patches.txt`):
  `race_main.c`'s `MIPS_REG_T3` (`__asm__("t3")`) and its two `mult/mflo` lean
  offsets (`leanOffset` is a `Vec3i` at `sp+0x10`, so `0x10` is `.x` and `0x18`
  `.z`), and `save_slot_select.c`'s bare `lui %0, 0x38` (= 0x380000).
* **The assets are built by python tools at link time**, and the generated
  headers live under `build/include`. The port does not need any of it: assets
  are read from the ROM file by DMA exactly as on the N64. It only adds
  `-I build/include` and the `assets/courses` / `assets/modelpayload` include
  dirs, because a few sources include the generated size headers.
* **textconv.** `tools/textconv.py tools/charmap.txt in out` turns `_("...")`
  into the game's own character encoding; the upstream compile rule runs it
  before the compiler, and so does the port's mirror step.
* **libultra and libmus are archives upstream**, not sources in `src/`. The
  port compiles *exactly the archive members the N64 link pulls in* — derived
  from `snowboardkids2.ld` — restricted to `audio/`, `gu/`, `io/pfs*` and
  `vimodes/`: everything else in libultra is what `port/src/ultra` replaces.
  Of libmus only `player_fx.c` is linked; the rest of MusPlayer is decompiled
  into `src/race/player.c`.

### Survey facts (see also the first game's PLAN.md)

* Boot: `entrypoint.s` (0x80000400) clears the main BSS, sets `$sp` and jumps
  to `mainproc` (`src/core/boot.c`). **There is no `main`.** `mainproc` starts
  the idle thread, which starts `mainThreadEntrypoint` (`src/core/mainEntrypoint.c`).
  The port's `main()` calls `mainproc()` directly.
* Threads: idle 1, main 2, audio manager 3, VI 4, display 5, controller 6, PI
  7, audio 9, scheduler 0xA, audio command 0xB.
* The retrace fan-out is a **`ViConfig` linked list**, not a fixed set:
  `addViConfig(cfg, queue, n)` asks for a message every `n` retraces. The game
  loop registers `n = 2` (30 Hz logic on a 60 Hz VI) and the audio manager
  `n = 1`. The main loop blocks once on its queue, then drains it with
  `OS_MESG_NOBLOCK` — the same idle-gating the first game's host loop assumes.
* VI: `OS_VI_NTSC_LAN1`, 320x240 RGBA5551, **three colour buffers plus a Z
  buffer**, and `src/core/buffers.c` under-declares `gAuxFrameBuffers[1]` while
  the game uses three: the other two occupy the unlinked tail of RDRAM and end
  exactly at 0x80400000. The game uses **all 4 MB**, which is what the port
  maps.
* Graphics ucode: **F3DEX2** (`gspF3DEX2_fifo`), with `gspS2DEX_fifo` for the
  2D sprite path; `microcodeGroups[]` in `src/graphics/graphics.c` picks one by
  `uses3DRendering`. See "S2DEX" below for what the sequel actually asks that
  microcode to do.
* Audio: libmus over libultra's libaudio, `alAudioFrame`, ucode `aspMain` —
  **audio ABI 1, and `assets/rsp/aspMain.textbin.bin` is byte-identical to the
  first game's**, so `port/src/audio/audio_task.c` was reused unchanged and
  works. `n_aspMain` appears nowhere. Output rate 22050 Hz.
* Saves: **EEPROM 4 Kbit** (64 blocks of 8 bytes; `controller_io.c` reads
  0x58-byte records and wipes 0x200) *and* the Controller Pak, plus the Rumble
  Pak. The first game had no EEPROM; `port/src/ultra/os_eeprom.c` is new.
* Segments: only 0, 1, 2, 3, set per display-list object.
* No `osGetTime` / `osSetTimer` / `osGetCount` anywhere in `src/`.

### S2DEX: the 2D microcode, and what the sequel uses it for

Viewports are grouped into tasks by `uses3DRendering`, and a 2D group's task
carries `gspS2DEX_fifoTextStart` as its ucode. `port/src/gfx/gfx_task.c`
compares `task->t.ucode` against that address (the ROM symbols make it a plain
constant, 0x800854E0) and sends the list to `port/src/gfx/gfx_s2dex.c` instead
of gfx_pc's F3DEX2 loop; `gfx_run_ucode(dl, s2dex)` is the one entry point that
knows the difference, and the F3DEX2 path is byte-for-byte the old one.

The interpreter owns the list's *control flow* and the object commands, and
hands every run of consecutive plain RDP commands back to gfx_pc unchanged
(`gfx_pc_run_dl`, a run at a time so that a G_TEXRECT and its two
`G_RDPHALF` words stay together). Implemented: `G_OBJ_RENDERMODE` (the
`G_OBJRM_BILERP` bit picks the texture filter), `G_OBJ_LOADTXTR` for all three
block types (TXTRBLOCK, TXTRTILE and TLUT), `G_OBJ_RECTANGLE`,
`G_OBJ_RECTANGLE_R`, `G_OBJ_SPRITE`, the `G_OBJ_LDTX_*` load-and-draw trio,
`G_OBJ_MOVEMEM` (uObjMtx and uObjSubMtx), `G_BG_1CYC` and `G_BG_COPY`,
`G_SELECT_DL` with `G_RDPHALF_0`, and `G_DL`/`G_ENDDL`.

Two facts about the port's shape here:

* **gfx_pc has no TMEM.** Its "loaded texture" is a pointer into RDRAM plus a
  line stride, while an object command addresses TMEM in 64-bit words. So
  `G_OBJ_LOADTXTR` only *records* which RDRAM address each TMEM word came from
  (an eight-entry table), and a sprite's `imageAdrs` is resolved against that
  table at draw time, when the load is replayed as the SETTIMG / SETTILE /
  LOADBLOCK / SETTILESIZE gfx_pc already understands. A background is drawn as
  horizontal bands sized to stay under the 4 KB a TMEM load can hold.
* **The sequel never sends an object command.** Its 2D lists are plain RDP
  work -- combiners, blender modes, texture loads, `gSPTextureRectangle` --
  with exactly one S2DEX command in them, the `gSPObjRenderMode` at the head of
  `gSpriteRDPSetupDL` (`src/graphics/sprite_rdp.c`) and two more in
  `sprite_rdp.c` / `text/text_layout.c`. A census over the attract demo, the
  title, the file select and a race counts tens of thousands of
  `G_OBJ_RENDERMODE` and zero of everything else; `guS2DInitBg` and the uObj
  structs appear nowhere in `src/`. The interpreter above is therefore mostly
  insurance for screens not yet reached (`--s2dextrace` decodes a task's
  objects, and the per-10-second `sbk: s2dex ...` line counts them).

### What was really hiding the 2D: a combiner input gfx_pc did not have

The title logo, the tile-map backgrounds behind the menus, the copyright
lines and the Rumble Pak badge were all missing, and none of it was the
microcode's fault. `gSpriteRDPSetupDL` sets

    gsDPSetCombineLERP(1, 0, TEXEL0, 0,  1, 0, TEXEL0, 0, ...)

-- (1 - 0) * TEXEL0 + 0, i.e. "just the texel" -- and gfx_pc's combiner model
has no constant 1 among its inputs (`color_comb_component` returns CC_0 for
anything it does not know). The 1 arrived as 0, sm64-port's simplifier saw
`a == b`, zeroed the product, and the result was transparent black. Everything
that draws under that setup list without setting a combiner of its own --
`renderSpriteFrame`, `renderTiledTextureMap` -- was invisible; everything that
does set one (`gDPSetCombineMode(G_CC_DECALRGBA)` in the text paths) was fine,
which is why menu text rendered and nothing else did. `color_comb` now folds
`(1, 0, c, d)` into the addend, and the title screen matches the reference.

The lesson generalises: when a 2D element is missing, dump the task
(`--dumpdl N --dumptris`) and look at the `sbk-tri:` line's `cc=` field before
suspecting geometry. The quads were always there.

### The big one: overlays

The sequel loads code at run time. `snowboardkids2.ld` has three tiers above
the resident image:

| VRAM | what |
| --- | --- |
| 0x800AFF30 | `.rand` — alone, so effectively resident |
| 0x800B00C0 | `.race`, `.credits`, `.cutscene`, the character-select and player-select sprite overlays |
| 0x800BB2B0 | the fourteen level overlays |
| 0x8016A000 | `buffers_bss`: Z buffer, the 2 MB heap, the colour buffers |

Two consequences, and the second one is the whole reason this port needed a
tool the first one did not.

**1. Overlapping globals cannot be pinned.** `gen_pins.py --pin-skip
0x800B00C0:0x8016A000` leaves everything in the overlay region native: whose
value would the pinned address hold? The resident image, `.rand` and the
buffers BSS are pinned as usual. (`gen_pins` also decides what is BSS from the
map's *section name* now, not from `main_RODATA_END`: several segments have
data of their own above it.)

**2. One address means several functions.** This is not a detail. Resident code
calls into an overlay *by address*:
`scheduleTask(&renderFlyingEnemy, 0, 0, 0xD3)` in `src/race/race_hud.c` really
means "the function at 0x800BB2B0" — which is `initSunnyMountainChairLiftTask`
on Sunny Mountain, `updateGhostAnimation` in the Haunted House,
`updateJingleTownBoss` in the Jingle Town boss race and `renderFlyingEnemy`
only in Linda's Castle. The decomp had to name that one address after one of
the fourteen. A native link has no addresses left, so every such call went to
Linda's Castle, and the first run on the G4 died 70 s into the attract demo in
`enqueueDisplayListWithFrustumCull` on a pointer read out of the wrong task
state.

`port/tools/gen_overlays.py` rebuilds the indirection from the map and the
linker script (2 groups, 19 overlays, 676 slots, 710 thunks):

* every name an overlay address carries becomes a generated PowerPC thunk
  (`lis/lwz/mtctr/bctr` through `sbk_ov_slot[k]`), so the *name* is the
  dispatch point and the thunk's address is what callers store — function
  pointer comparisons keep working;
* each overlay's own definitions are renamed `NAME__sbk_ov_<segment>`, by a
  `#define` prepended to the mirrored source so the header declaration is
  renamed with it, and intra-overlay calls bind directly;
* `dmaLoadAndInvalidate` gets a one-line hook (patches.txt) that calls
  `sbk_overlay_activate(romStart)`, which points that group's slots at the
  loaded segment's functions. The DMA itself still runs — it writes MIPS code
  nobody will execute into the emulated RDRAM, and clears the overlay's BSS
  there — which costs nothing and keeps the game's own bookkeeping honest.
* Slots the loaded overlay does not define are pointed at a trap, not at the
  previous overlay's code: reaching one would be a port bug and should say so.

Still open on overlays: only *text* symbols are dispatched. If resident code
ever reads an overlay's **data** by address the same way, it will read the
wrong level's table and nothing will complain. Nothing has been seen to do it
yet.

## Build-time tooling: what changed in the shared tools

`mirror_src.py` grew five cases the sequel needs:

* a `/* 0x89240 */` ROM-offset comment in front of a definition (`find_definition`
  used to skip any line starting with `/`) — but only at column 0, or indented
  struct *members* get renamed and the struct falls apart;
* `struct { ... } gFoo = { ... };` at column 0: an anonymous type cannot be
  declared `extern`, so the declaration is split in three — a tagged type, the
  extern for the pinned symbol, and the twin's definition;
* `sizeof()` / `ARRAY_SIZE()` of a pinned global measures the **twin**: the
  extern for the pinned symbol cannot carry a size when the definition left the
  first dimension open (`T name[] = {...}`);
* a tentative declaration (`T gFoo[];`) and its definition both match, so the
  size lines are de-duplicated;
* library sources whose tentative definitions duplicate the game's own
  (`gReverbFx`, `__osPfsInodeCache` are defined in `src/common_bss.c` *and*
  in `player_fx.c` / `contpfs.c`) get file-local twins (`--twins-static`).

`gen_pins.py` runs **twice**: once for `pins.s`/`pins.txt` (which the mirror
needs), and again after every source has been mirrored, against the list of
twins the mirror actually emitted. A pinned global need not have a C definition
to rename — several are `__asm__(".globl X\nX = base + N")` aliases into
another array — and a copy row naming a twin that was never emitted is an
undefined symbol at load time (`dyld: _D_8008DDE6_8E9E6__sbk_size not found`).

`gen_rom_syms.py` emits the whole `_TEXT_/_DATA_/_RODATA_/_BSS_` family of
segment boundary symbols, not just `_ROM_START`/`_VRAM`: `LOAD_OVERLAY` takes
the address of every one of them. It also takes `--skip-file` (symbols the port
compiles itself, from `gen_pins --native-txt`) and `--fixed` for the RSP
microcode symbols, whose blobs are not compiled but whose addresses the game
subtracts to size the boot ucode.

## Gotchas worth keeping

* **`src/core/main.c` `#include`s `src/data/shared_model_part_display_lists.c`.**
  The mirror has to shadow the whole `src` tree (`-I$(GEN)/src` first in
  `GAME_INC`) or the include pulls in the unfiltered original and defines a
  few hundred pinned globals a second time.
* **`GEN_FRAGMENTS` must be defined before the rule that uses it.** Make
  expands a prerequisite list when the rule is read, so a `$(GEN)/twins.txt`
  rule placed above the assignment had *no* prerequisites, ran before anything
  was mirrored and quietly produced a pin table with zero copy rows.
* **`lib/ultralib/include/PR/gbi.h` and `lib/f3dex2/PR/gbi.h` are
  byte-identical** and both honour `F3DEX_GBI_2`, so gfx_pc's `<PR/gbi.h>` and
  the game's own `"gbi.h"` agree. Do *not* put `lib/f3dex2` on the `<PR/...>`
  path to "make sure": its `rcp.h` and `ultratypes.h` are older than
  ultralib's and shadow them (`VI_CTRL_ANTIALIAS_MODE_0` undeclared).
* **G_BRANCH_Z** is the F3DEX2 level-of-detail branch F3DEX does not have. The
  target arrives in the `G_RDPHALF_1` before it. The port always takes the
  branch — the nearest level of detail — because the fall-through path is the
  *other* model rather than a return, so one of the two has to be chosen, and
  there is no reason to draw the coarse one on a machine that is not the RSP.

## Milestones

0. **Toolchain** (done 2026-09-11): all 166 game sources, the libultra audio
   synthesizer, gu, pfs and the VI mode table, and libmus's `player_fx` compile
   for `powerpc-apple-darwin8`; the whole thing links.
1. **Boot to first frame** (done 2026-09-11): threads, ROM DMA, display lists,
   frames presented and audio samples produced on the real G4. The attract
   demo renders a course (`g4-shots/sbk2-boot-20s.png`).
2. **Menus** (done 2026-09-11): the title screen now matches the reference
   frame -- logo, snow background, START / TRAINING / OPTION, the copyright
   lines and the Rumble Pak badge (`g4-shots/sbk2-s2dex-fix1.png` against
   `snowboard_kids2-020.png`) -- and a race runs with its full HUD
   (`g4-shots/sbk2-fix-130s.png`). Before the combiner fix only the menu text
   was on screen (`g4-shots/sbk2-ov-45s.png`). Earlier notes: the title screen
   with the character line-up and the START / TRAINING / OPTION menu
   (`g4-shots/sbk2-ov-45s.png`), the file-select screen and its ~MENU~
   (`sbk2-save-52s.png`), and the level preview with its 3D fly-through and
   character model (`sbk2-menu-45s.png`, `sbk2-menu-65s.png`). A blind walk
   with A every four seconds reaches a race and drives it without a crash.
3. **Audio**: ABI 1 confirmed and `audio_task.c` reused as-is; the demo
   produces samples. Not yet listened to on the G4, and the menus so far report
   a peak of zero — worth checking whether the sequel simply has no menu music
   before that run, or whether `MusInitialize`'s heap is not reaching the
   synthesizer.
4. **Controller Pak, EEPROM, fullscreen, pad, launcher slot**: EEPROM written
   and read (the file-select screen proves it, and the navigator's own
   `saved slot 0` lines confirm the write lands mid-campaign). **Fullscreen
   works** -- `g4-shots/sbk2-campaign-fullscreen.png` is a fullscreen race on
   the G4, HUD and all four riders correct. The Controller Pak, the pad and the
   launcher slot are still inherited from the first game's port and untested
   here.

## Self-play: what the sequel's state looks like from outside

The first game's `race_dbg.c` and `menu_nav.c` read globals -- `gRacePlayers`,
`gRaceCourseIndex`, `gActiveGameTaskList` -- and the sequel has none of them.
It keeps **every screen's state in the task scheduler's own allocation**, so
both tools were rebuilt against that shape instead:

```
gSchedulerListSentinel.next     priority-ordered chain of TaskScheduler
  .gamestateHandler             what this screen runs next  -> dladdr() names it
  .allocatedState               this screen's state struct  -> where cursors are parked
  .renderContext                a tag; 0x37 means "this is the race"
```

* **Finding the race.** `initRace` (`src/race/race_session.c`) calls
  `setRenderContext(0x37)`, and that is the *only* call to `setRenderContext`
  in the whole game -- an unambiguous tag no name matching can beat. Its
  allocation is the `GameState` of `include/gamestate.h`: `players`,
  `numPlayers` (all four riders), `playerCount` (the human ones),
  `rankOrder[]`, `raceType`, `memoryPoolId` (the level), `finalLapNumber`.
* **Finding a screen.** `dladdr()` on each scheduler's `gamestateHandler`
  gives the real function name, so `--menutrace` prints the sequel's menu map
  without a hand-written table. `sbk_menu_alloc("name")` then hands back that
  screen's struct, which is what the navigator parks.
* **A race allocation is not a race.** The results screens run on the *same*
  scheduler as the race, so "hands off the pad while a race allocation exists"
  sits on the results screen for ever. Only `handleRaceStateUpdate` means a
  race is being played. This was the first bug the navigator had.

The map, read straight off a `--menutrace` soak on the G4:

```
initLogoSplash / updateLogoSplash          the logo
startDemoRace ... handleRaceStateUpdate    the attract demo (gameMode 3)
handleTitleMenuInput                       START / TRAINING / OPTION
updateSaveSlotSelectionScreen              the file select (and the only EEPROM writer)
gameStateCleanupHandler                    the walkable town (game_state_init.c)
storyMapHandlePlayerInput                  the rider picker on a town map (overlay 1BBA0)
handleLevelSelectInput                     the course list
updateCharacterSelect                      rider + board
updateCutscenePlayback                     the pre- and post-race cutscenes
handleRaceStateUpdate                      the race
handle*GameResult / await*AwardGold / await*ContinuePress   the results
```

### The rider

`--autoplay` sets `players[0].isCpuControlled`. Two things the first game did
not need:

* **The path table.** Only the riders the game itself made CPUs get a
  `bossRaceData` asset, and the AI path preferences live inside it at an offset
  indexed by `playerIndex` (`race_main.c` ~1105). Player 1 has none, so the
  port lends it rider 2's copy and reads slot 0 out of it -- and has to keep
  retrying, because the asset does not exist yet at the handoff.
* **The tuning.** `applyCharacterSnowboardStats`
  (`src/race/character_stats.c`) builds `baseMaxSpeed`, `handling`,
  `cornering`, `lateralDeadzone`, `baseGravity` and `baseAcceleration` from
  `gSnowboardStatsTable[snowboardId][characterId]`; the port recomputes the
  same six (it cannot *call* it -- the game's version reads
  `getCurrentAllocation()`, and `gActiveScheduler` points at whatever the last
  dispatch left when the host loop runs) and folds `--trial boost=N` into the
  top speed in 1/256ths.
* **...and the tuning has to be *held*, because the game undoes it.** This is
  the single most expensive thing in this document, so it gets its own
  paragraph. `initPlayer` (`race_main.c` ~1061) ends with **its own** call to
  `applyCharacterSnowboardStats`, and `initPlayer` runs from
  `waitForFadeAndInitPlayers` -- which `initRace` queues *after* the
  `setRenderContext(0x37)` the autoplay handoff hangs off. So all six fields
  were recomputed from the stats table a second or two into every race, with
  no boost in them.

  **The boost lever therefore never reached a race.** The handicap ladder spent
  six attempts on course 8 climbing rungs 1..5 -- +11%, +16%, +21% -- and the
  rider raced every one of them at exactly the same speed. That is why the
  places came back 4th, 3rd, 2nd, 2nd, 3rd, 2nd with no trend: they were six
  samples of one experiment, and the "overshoot past +20%" the boss ladder's
  note predicted was read into noise. The log had said so from the first
  campaign run and nobody put the two lines side by side:

  ```
  sbk: autoplay: rider 0: cpu=1 boss=0 diff=7 char=0 board=8 top=1394607
  sbk-race: r=9240 p0 ... spd=959922/1257111
  ```

  -- and `1257111 * (1 + 28/256) = 1394608`, the boost exactly undone. The
  lesson for the next lever: a handicap that is applied at the handoff is not
  applied to the race, and `--racedbg`'s `spd=x/y` is where to check that it
  survived.

  Nothing else writes `baseMaxSpeed` (`race_main.c:802` only copies it into
  `maxSpeedCap` each frame; the two health bosses derive their own from it), so
  the fix is one comparison per tick: `retune_hold` re-applies the retune
  whenever the field is not what it left, and says so in the log. Held rather
  than hooked because the write is inside a game source file, and everything
  that changes game behaviour is supposed to be one line in `patches.txt` or
  nothing at all.

### Nightmare

The sequel has no `actionTriggerChance` / `itemTriggerChance`. Its CPU riders
are tuned by `gAIPlayerParams[8][0x11]` (`src/race/race_main.c`, in the `.race`
overlay, so a plain native symbol), three bytes per entry:

| read | what it does |
| --- | --- |
| `race_main.c` ~804 | `maxSpeedCap -= gAIPlayerParams[d][0].useChance * 0x202` |
| `hit_reactions.c` | per item: `delay` before use, `useChance` / `altChance` to use it |

So **row 0's `useChance` is a top-speed tax** -- 0xA8 on the easiest row costs
about a quarter of the rider's speed -- and every other slot decides how soon
and how often an item is thrown. The rows the game's own data names
(`gCpuCharacterSnowboardConfigs`) are 0-5, so **row 7 is spare**: `--nightmare`
writes the retune there and points every CPU rider at it, leaving the game's
own table untouched. The four values are exposed to `--trial` as
`nmtax/nmdelay/nmuse/nmalt` so `nightmare_search.py nm` can **search** them
rather than have them guessed -- which turned out to matter more than it
sounds. The first guess was `tax=0, delay=0, useChance=255, altChance=255`:
every item thrown the instant it is held, by every rider. That row does not
make the game hard, it makes races **stop ending** -- items are scheduled
tasks, and the only way out of the lift wait that wraps a lap is a
`scheduleTask` that returns NULL once the pool is full. `docs/nightmare-row.md`
has the chain and the sweep.

The searched row is `tax=0, delay=120, useChance=205, altChance=205`, and it is
now the default. It sits inside the shape the game's own eight rows use (their
item `delay` never goes below 90). On Sunny Mountain it comes first in 14,106
frames; `useChance=255` at the same delay does not finish, `delay=150` drops to
second and `delay=90` to third -- so 120 is a real optimum, not the end of a
monotonic trend.

### The board, which was the worst one in the game

`--nightmare` used to give player 1 `SNOWBOARD_SPEED_LEVEL_3`, described here
as "the fastest board that has no drawback". Read `gSnowboardStatsTable` for
Slash and it is the opposite:

| board | spd | han | cor | dz | grv | acc |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `SNOWBOARD_SPEED_LEVEL_3` (8) | 83 | 35 | 50 | 37 | 75 | 40 |
| `SNOWBOARD_STAR` (9) | 83 | 60 | 40 | 65 | 75 | 70 |

-- the same top speed, and STAR is better on every other axis. Three of those
are the racing line:

* `handling` is the **turn rate**: `race_main.c:1392`,
  `turnRate = (steeringAngle / 2) * handling / 125`.
* `cornering` is the **drag a turn costs**: `race_main.c:1401`,
  `cornering * scaledTurnRate^2 / turnRate` subtracted from the speed, so
  *lower* is faster.
* `lateralDeadzone` is how much sideways velocity is killed each frame
  (`applyVelocityDeadzone`, `race_main.c:3170`), so *higher* is less sideslip.

So the self-play rider had the worst-handling, worst-deadzone, worst-
accelerating level-3 board in the game, and the ladder was (nominally) putting
up to +21% on top of its top speed. STAR is not a cheat board: `race_session.c`
:575 and :615 hand it to the game's own riders, and unlike DRAGON, HIGH_TECH,
NINJA, RICH and POVERTY nothing in `hit_reactions.c` or `particle_items.c`
gives it a behaviour. The only thing `>= SNOWBOARD_STAR` changes anywhere is
that `race_main.c:6070` loads no palette for it.

For reference, the game's own riders on each course are in
`gCpuCharacterSnowboardConfigs[course][character]` (snowboardId, colorSlot,
1P difficulty, MP difficulty) -- on course 8 they ride boards 2 and 5 at
difficulty 5-6, which is exactly what the roster dump prints.

### The navigator

`--autonav` answers each screen by name, and everything it can it **parks**
rather than presses -- a queued A lands on a prompt the frame it appears, so a
stick-up queued behind it is always a frame late (the first game's lesson,
still true):

* `handleTitleMenuInput` -> `TitleScreenState.menuSelection = 0` (START).
* `handleLevelSelectInput` -> `LevelSelectState.selectedIndex`, a *cursor into
  its own `levelIdList[]*, is parked on `--trial level=N`; the confirm is the
  navigator's own A. Exactly the first game's course-cursor move.
* `updateCutscenePlayback` -> `playbackState = CUTSCENE_STATE_SKIP_START`.
  The sequel plays a 55-second cutscene before every story race and another
  after it, and **START is the only skip** -- which a navigator must never
  queue, because a START still in flight when a race begins pauses it. Parking
  the state is what the button does anyway, fade and all. It takes a race cycle
  from ~7 minutes to ~5.
* `gameStateCleanupHandler` (the walkable town) -> `discoveredLocationId` and
  `unk427`. A location is normally entered by *walking into* it: a trigger sets
  `locationDiscovered` / `discoveredLocationId`, the travel task writes
  `unk427 = id + 1`, and the cleanup handler turns that into
  `storyMapLocationIndex`, which `map_state.c` uses to dispatch
  `storyMapLocationHandlers[]`. A bot cannot be asked to walk across a town, so
  the navigator writes the pair the trigger would have. **id 6** (handler 7,
  `initSaveSlotScreen`) is the save point. Ids 2, 5 and 8 are the three Cross
  minigames, which `handleGameStateComplete` intercepts and turns into levels
  0xD, 0xE and 0xC.

  **A story course is not a location at all.** `gameStateCleanupHandler` splits
  on that one byte two ways, and the second way is the campaign's:

  ```
  unk427 == id + 1   storyMapLocationIndex = id + 1, return 1   -> a building
  unk427 == 0xFF     return 0xFF                                -> LEAVE
  ```

  and `awaitStoryMapSelection` (`session_manager.c`) maps **both** 0x44 and
  0xFF to `loadLevelSelectScreen`. So 0xFF -- despite its callback being called
  `onStoryMapExitToMenu` -- is "walk out of Jingle Town and go race", which in
  the game is `finalizeStoryMapExit` (`map_character_anim.c`) writing the byte
  once the fade lands. The navigator writes the same one.

  This cost an evening. `loadOverlay_1BBA0` (handler 4, id 3) is **not** the
  ski-area map: it is the *rider picker* laid over a map of the town, nine
  riders in a 3x3, `okPromptSprites` and all. Its `onStoryMapNormalExit`
  returns the course-list code 0x44 only while
  `EepromSaveData->levelUnlockStatus[0] == 5` -- true only until the first
  course has been won. After that it returns 1, the rider lands back in the
  town, and a navigator that keeps asking for id 3 loops for ever: 177 laps of
  it are in the log, each lap a *different* chain of screen names, so the
  "stuck on one screen" timer never fired. The navigator now counts town exits
  and says so instead.
* **Which course next.** The course list opens on
  `gGameSessionContext->currentLevel`, which is the course just played, so a
  navigator with nothing to say re-runs Sunny Mountain for ever. The game marks
  its own intent: `updateStorySlotUnlockStatus` (`session_manager.c`) leaves
  finished courses at `levelUnlockStatus` 1 and writes **5** into the next one
  -- the same 5 the rider picker's exit and the list's cursor read. With no
  `--trial level` the navigator parks on the first slot holding 5.
* `updateSaveSlotSelectionScreen` is both the way into the game and the game's
  **only writer of the EEPROM** (`eepromWriteAsync`, `save_slot_select.c`).
  Three of its states default to the answer that backs out, so all three are
  parked: `saveSlotMenuState 1` -> `selectedSaveSlot = 0`; `0x33` with
  `saveSlotDialogType 0xA` -> `saveSlotDialogSelection = 0` (STORY, not
  EXPERT); `0x33` otherwise -> 0 to save (or, entering the game, 1 when
  `numValidSlots` says a file exists -- the sequel's version of the first
  game's "USE THIS SAVE, not START A NEW GAME" trap).

### Knowing that a race has ended, and where it finished

The navigator drives on two facts per race: *it is over*, and *what place we
came*. Both are harder than they look, and each cost the campaign a stall.

**"It is over" is a set of screen names, and the set is not obvious.** The
test began as three substrings -- `GameResult`, `ContinuePress`, `AwardGold` --
which covers every ordinary course and silently cannot see a **boss race**. The
boss's handlers are `handleBossRaceResult`, `handleBossDefeatResult` and
`awaitBossResultAndFadeOut`: `BossRaceResult` is a *Race*Result, not a
*Game*Result. The Jingle Town boss therefore looped -- race, town, race again --
with no finish counted, no EEPROM write and no climb up the handicap ladder.
That loop is the one shape the town-exit guard is blind to, because there is a
genuine race in every lap of it.

Widening the test to the word `Result` fixes the boss and breaks everything
else, which is the more interesting half:

| name | what it actually is |
| --- | --- |
| `awaitRaceResult` (`session_manager.c:131`) | the game state `loadRace` sets **before** `initRace` is queued |
| `awaitStoryModeRaceResult` (`race_state_machine.c:180`) | ditto, for story mode |
| `awaitVersusRaceResult` | ditto, for versus |
| `*SkillGameResultTimerDisplay` | the skill game's **HUD**, running during the race |

The first three are not result screens; they are the state that *waits* for a
result, and they are up for the whole pre-race cutscene and the whole race. So
the log read `level list: cursor -> 3` immediately followed by `race 1 finished
on level 3` -- a race reported finished before it had started -- and then,
because the funnel stays up until the race really ends, the true result screen
was no longer a rising edge and the actual finish was never seen at all. A race
reported finished too early is worse than one never reported, because
everything downstream believes it. The three funnels are excluded by exact
name; `TimerDisplay` by substring.

**"What place" cannot be read off the result screen.** A boss race tears its
`GameState` down before its result handler appears, so `sbk_race_state()`
returns NULL there and the place reads -1 -- which the handicap ladder can only
treat as a loss. A boss race that was *won* would therefore climb the ladder as
though it had been lost, and grind on at ever-higher rungs having already beaten
the thing. The place is settled much earlier: `race_main.c` writes
`finishPosition` and then sets the `0x80000` finished bit in `animationFlags`
the moment the rider crosses the line, with the race still on screen. The
navigator latches it there, clears the latch on the rising edge of each race so
a stale place cannot be attributed to the next one, and keeps the live read as
the preferred source -- the latch only answers when the race is already gone.

### The handicap ladder, and why its order is the whole design

Only `finishPosition == 0` marks a course won, and an unattended campaign that
re-races a lost course with identical settings loses it again for ever. So each
retry gets a handicap, one rung at a time, reset by a win. There are three
levers and they are **not** interchangeable:

| lever | what it touches | effect on the task pool |
| --- | --- | --- |
| `boost` | player 1's own top speed, via `trial_retune` | neutral |
| `rivaltax` | the rivals' row-0 speed tax (`RIVAL_ROW`) | **bad** |
| `rivalrelief` | the rivals' item chances (`RIVAL_ROW` only) | **good** |

The ordering was learned the hard way on course 1:

```
rung 0  no handicap                     2nd
rung 1  boost 28                        2nd
rung 2  boost 28, rivaltax 160          4th, and the rider WEDGED
rung 2' boost 28, symmetric relief 100  3rd -- worse than no handicap at all
rung 2" boost 28, rival-only relief 100 1st
```

Two traps in that table.

* **A heavy rival tax buys the chairlift wedge.** `docs/nightmare-row.md`
  measured `tax=168` across the whole field into a DNF, and the mechanism
  survives intact when only the rivals pay: a slower field means a longer race,
  every item in flight is a scheduled task, and `spawnChairliftEffect` -- the
  only way out of the lift wait that wraps a lap -- is a `scheduleTask` that
  returns NULL once the pool is full. The two highest rungs of the original
  ladder were therefore the two most likely to hang it, which is a poor thing
  for a ladder to keep for last.
* **Item relief was not a handicap.** `sbk_item_relief` writes *both* rows, and
  `hit_reactions.c` indexes `gAIPlayerParams` by each rider's **own** row, with
  player 1 on `NIGHTMARE_ROW` and the rivals on `RIVAL_ROW`. Taking the items
  off "the row" took them off ours by exactly as much: a house rule, not a
  handicap, and our rider is worse under it. The variable was only ever the
  wedge remedy; the ladder had borrowed it for a job it could not do. It is now
  split, and `sbk_rival_item_relief` writes `RIVAL_ROW` alone.

So the ladder runs boost, then rival relief, then rival relief plus tax as a
last resort:

```c
{ 0, 0, 0 }, { 28, 0, 0 }, { 28, 0, 100 }, { 28, 0, 165 },
{ 42, 0, 165 }, { 56, 0, 165 }, { 56, 160, 165 }
```

The last three rungs were added when course 8 -- **Starlight Highway**, not
Wendy's House; the course order is Sunny Mountain, Turtle Island, Jingle Town,
Jingle Town boss, Wendy's House, Linda's Castle, Crazy Jungle, Crazy Jungle
boss, Starlight Highway, Haunted House, Ice Land, Ice Land boss, and
`build/include/generated/course_definitions/display_list_assets.inc` is where
to read it off -- sat at 2nd place through rungs 2 and 3 and then **wedged** on
what used to be the top rung, the tax rung this section already calls the one
most likely to hang a race. So the tax rung moved to last and two boost rungs
went in front of it, at the +16% and +21% the boss ladder wins with.

**And then it turned out that none of those rungs had ever done anything.**
`initPlayer` recomputes the rider's stats after the handoff (see "The rider"
above), so every boost rung raced at stock speed. The ladder's whole boost half
was measuring nothing, and the six losses on course 8 -- 4th, 3rd, 2nd, 2nd,
3rd, 2nd -- are six samples of rung 0 with a different label on each. The
"overshoot past +20%" this paragraph used to claim was a pattern read into
noise. Read the rungs below as untested from here down; the retune is held now,
so the next campaign is the first one that actually climbs them.

Rival relief is the cleanest handicap in the port: no rider's speed, handling
or cornering changes, our rider keeps a full item set, and total pool pressure
goes *down*, so unlike the tax it cannot buy the wedge it exists to avoid.

**A course the save already calls lost does not start at rung 0.**
`awaitRaceResult` writes `levelUnlockStatus[level] = 4` for a race finished
outside first place, which is the game's own note that this course has beaten
this rider before. Opening it at rung 0 anyway spends five minutes
re-discovering what the EEPROM already records. Such a course starts at rung 1,
the mildest rung, so one lost narrowly still gets a nearly-honest race. Making
that work meant moving the ladder's per-course state out of `nav_handicap`'s
statics into a `nav_level_begin()` the **course list** also calls:
`nav_handicap` only ever runs off a result screen, which is one race too late
to choose a handicap.

**`--startrung N`** hands the ladder back across a restart. It lives in memory,
so redeploying a binary mid-course would otherwise drop a course that had
climbed two rungs back to rung 0 and re-lose the same races at five minutes
each. It applies to the first course after boot and is consumed by it.

### The boss races, which no handicap can win

Courses 3 and 7 -- Jingle Town and Ice Land -- are not races, and the campaign
spent four attempts and half an hour proving it, losing by exactly one place at
every rung of the ladder without ever earning a coin.

`race_main.c` ~5188 gives `RACE_TYPE_BOSS_JINGLE` and `RACE_TYPE_BOSS_ICE`
exactly two endings:

```
the boss crosses the line     showPlacementAnnouncement(0, 2), both riders
                              flagged finished        -> gRaceResultCode 4, lost
boss animationFlags & 0x100000  showPlacementAnnouncement(0, 1), snowflakes
                                                      -> gRaceResultCode 3, won
```

Our own rider crossing the line appears in neither. So no amount of top speed
can win a boss race, and the rival levers cannot even reach the boss: the roster
dump (`sbk: autoplay: rider N:`, printed at every handoff) shows the boss on
`RIVAL_ROW` with `baseMaxSpeed` **0**, because `updateJingleTownBoss` writes its
own `maxSpeedCap` from its distance to us and never reads the racer speed model
the tax subtracts from.

`0x100000` is set when `bossHealth` reaches 0 -- the ten snowman heads along the
bottom of the screen, `initJingleTownBoss` writes `0xA`. Health only falls in the
two hover phases, and the hover phases are entered from exactly one test in
`updateJingleTownBoss`: `hitReactionState` 0x3D or 0x3E, one head each. Tracing
those two back through `hit_reactions.c` leaves a very short list of things that
can hurt a boss:

| state | set by | reached from |
| --- | --- | --- |
| 0x3D | `setPlayerStarHitState` | `checkStarProjectileHit` -- the **star**, primary item 5, projectile type 4 |
| 0x3E | `setPlayerBouncedBackState` | `descendWarpEffect` -- the **frying pan** secondary, and a couple of level hazards |

Every other throwable lands a state the boss ignores: parachute 0x34, shrink
0x35, panel 0x36, frozen 0x3C. One primary item in seven, and the pan.

And the CPU rider will not throw the one that counts. `findPrimaryItemTarget`
finds the boss, rolls its `altChance`, and then asks whether a real rider is
still ahead of it:

```c
i = player->finishPosition;
for (; i >= 0; i--) if (players[rankOrder[i]].isBossRacer == 0) break;
if (i >= 0) return -1;
```

The scan starts at our **own** rank, and `rankOrder[our rank]` is us -- a
non-boss rider -- so it breaks on the first step every time and returns -1. In a
two-rider boss race a CPU rider can never throw at the boss, and self-play *is* a
CPU rider.

**The boss pilot** (`port/src/debug/boss_pilot.c`) is therefore one hook, in
`processPlayerItemUsage` (`port/patches.txt`), that answers that one question
differently for our rider on a health boss:

* the boss inside `sbk_boss_range`, and the heading error inside a window
  computed from the geometry rather than guessed -- the star flies straight at
  `0x1B8000` a frame, so an error of *e* radians misses by `dist * e`, and
  `checkStarProjectileHit` forgives `0x1EC000 + 0xC0000`; a full turn is
  `0x2000`, so the window is `1303 * 0x2AC000 / dist`, clamped to
  `0x40..0x200` -> return the targeting mode, and the game's own
  `spawnAttackProjectile` does everything after that. The first run ignored
  distance and threw 21 stars from as far as `0x27C0000`, landing five.
* nothing else is faked: the projectile, its flight, the collision test, the
  boss's reaction and the result code are all the game's.

**The supply is the honest handicap, and it is logged as one.** A boss course has
no item boxes -- a handful of items lie on the ground, and the borrowed CPU path
does not steer to them (`processItemTriggers` wants the rider within `0x100000`
of one). A rider with nothing in its hands cannot use the mechanic at all, so
after a loss the navigator's boss ladder opens a supply, and the pilot puts an
item in the rider's hand every *N* retraces while it is empty-handed:

```c
static const struct { s16 boost, supply; } nav_boss_ladder[] = {
    { 14, 0 }, { 28, 180 }, { 28, 120 }, { 28, 60 }, { 28, 30 },
};
```

Rung 0 is the pure mechanic -- throw what the course gives us -- and every star
or pan the pilot conjures is counted and printed against the ones the rider
picked up itself, at every hit and once at the end:

```
sbk: bosspilot: supplied pan (#5) at r=4627, boss hp=7
sbk: bosspilot: boss hp 7 -> 6 (thrown=5 supplied=2 picked=0)
sbk: bosspilot: race over -- boss hp=0 defeated=1, stars thrown=6
                (supplied=2, pans=11, picked up=1)
```

The **pan is the supply's first choice**, because the pan cannot miss:
`processPlayerItemUsage` spawns a warp effect over *every* other rider and
`descendWarpEffect` calls `setPlayerBouncedBackState` on the boss when it lands.
No aiming, one head, and the rider throws it through the game's ordinary
`shouldUseSecondaryItem` path -- the pilot only fills its hand. It is also what
the course intends: the three pans lying on the Jingle Town run are the
walkthrough's weapon of choice. Stars fill the gaps while a pan is in flight.
That is the difference between the run that stalled at five heads and the run
that took all ten.

**A won boss race does not look won from `finishPosition`.** The boss is usually
still ahead on the track when its last head goes, so the rank order the finish
flag freezes says 2nd, and the navigator would have filed a win as a loss and
ground the course for ever at rising rungs. `nav_place_of()` asks the question
the game asks -- `0x100000` on the boss -- and falls back to `finishPosition`
everywhere else.

**Course 0xB, the Crazy Jungle boss, is not one of these.** It is
`RACE_TYPE_BOSS_JUNGLE`, it has no `bossHealth` at all, and `race_main.c` decides
it on who reaches the line first, so it stays on the ordinary ladder where boost
is the lever that works.

### What the levers actually do, measured

The trial harness runs a whole race in about **thirty seconds of wall clock**
on the G4 -- the note under "The trial harness" saying `--headless` buys no
speedup for the sequel is stale, and it cost this port two days of reasoning
about levers that could have been swept in an afternoon. `--trial relief=N` and
`tax=N` now pin the rival levers against the navigator's ladder, so a trial can
reproduce a campaign rung exactly.

Every row is one race, `--nightmare`, char 0, `SNOWBOARD_STAR`, and each
configuration is deterministic (the same spec twice gives the same frame count
to the frame):

| course | boost | tax | relief | result |
| --- | ---: | ---: | ---: | --- |
| 0 Sunny Mountain | 0 | 0 | 0 | **1st**, 14,360 frames |
| 0 | 0 | 60 | 165 | wedged |
| 0 | 0 | 100 | 165 | wedged |
| 8 Starlight Highway | 0 | 0 | 0 | 2nd, 21,384 |
| 8 | 14 | 0 | 0 | wedged |
| 8 | 42 | 0 | 0 | wedged |
| 8 | 0 | 0 | 165 | wedged |
| 8 | 0 | 60 | 0 | 2nd, 21,538 |
| 8 | 0 | 30 | 165 | 4th |
| 8 | 0 | 60 | 165 | **1st**, 20,744 |
| 8 | 0 | 100 | 165 | **1st**, 22,164 |
| 8 | 0 | 160 | 165 | wedged, 1,796 frames of wall contact |
| 9 Haunted House | anything tried | | | wedged, always -- the CPU low-speed lock-out, see below |
| 10 Ice Land | 0 | 0 | 0 | **1st**, 25,904 |
| 10 | 0 | 60 | 165 | wedged |

* **Rung 0 wins two of the three.** The board change did that; nothing else in
  rung 0 is new.
* **Boost is a liability.** +5% wedges course 8 and so does +16%, while no
  boost at all comes 2nd on the same course. It moves the rider off the line
  the borrowed path was authored for. It is below the rival levers in the
  ladder now.
* **Relief and tax only work as a pair, and that is course 8's answer.**
  Relief alone wedges it, tax alone leaves it 2nd, and the two together win it
  at two different tax values. Relief keeps the task pool clear, which is what
  stops the tax buying the chairlift wedge these notes have warned about since
  Turtle Island; the tax is the only lever that makes a margin without touching
  our rider's physics. A tax of 160 still wedges, so the warning was right
  about the size, not about the lever.
* **A handicap that wins one course loses another.** Relief+tax wedges courses
  0 and 10, which rung 0 wins outright. There is no single setting for the
  campaign, which is what the ladder is for.

`--racedbg` gained two gauges for this work. `pool=c0,c1,c2,c3` is the race
scheduler's free-task counters -- `scheduleTask` returns NULL when the one it
wants is 0, which is the chairlift wedge in one number. `wall=a,b,c,d` counts
the retraces each rider spent with `animationFlags & 0x10` set, which
`race_main.c:5355` sets on any frame `handlePlayerTrackWallCollision` moved the
rider: the game's own answer to "am I scraping something", and the way to tell
a rider that is off the racing line from one that is merely slow.

### Course 9, the Haunted House: the CPU's own low-speed lock-out

Course 9 wedged under every configuration the ladder could offer -- rung 0,
boost 28, boost 56, relief 165, relief 255, tax 100, relief+tax, path slots 0,
2 and 3, the balance board instead of the star, and with `--nightmare` off
entirely so the rivals run the game's own difficulty rows. Thirteen races,
thirteen wedges, always at sector 50. That is not a tuning problem, and the
first guesses in this file -- the ghost around `haunted_house.c` ~300, the
pendulum's `isPlayerInRangeAndPull` -- were both wrong.

#### The instrument

`--racedbg` grew a **pin autopsy**. The first time any rider in a race stops
improving `currentLap`/`lapProgressRemaining` for 300 retraces, it prints, as
`sbk-pin:` lines:

* every task on the race scheduler by name (through the navigator's name
  cache), and for each one every aligned `s32` triple anywhere in its inline
  payload that lands within 0x400000 of the pinned rider, with the offset it
  was found at -- so "which placed thing is standing on top of this rider" is
  answered rather than guessed;
* all four riders' positions, stored positions and collision radii;
* the sector graph for thirteen sectors around the pin (next / previous /
  right / left / length / progress) **and every rider's own path-preference
  row** for those sectors, so a rider that gets through and one that does not
  can be compared side by side;
* then 150 consecutive frames of the pinned rider's whole physical state:
  position and its per-frame delta, velocity, aiTarget, rotY, steering,
  hit-reaction state and knockback vector, animation and behaviour flags,
  behaviour mode/phase/step/counter, track face, surface, slowdown, stun
  counter, lift flags, speed caps.

Two things that cost a crash each and are worth writing down. `Node.payload`
(`task_scheduler.h` 0x28) is **not** a pointer -- `scheduleTask` returns
`&newNode->payload`, so the field *is* the inline storage; reading it as a
pointer and dereferencing it is a SIGSEGV. And the start-line countdown is four
riders making no progress on purpose, with `lapProgressRemaining` still 0
rather than 8192, so the autopsy has to take its baseline again when the intro
ends or every rider looks pinned from sector 1 on.

#### What it found

Nothing was near the pinned rider but its own `updateSkiTrailTask` and
`updateDualSnowSprayParticles`. The nearest other rider was 3.8 world units
away against a collision radius of 0.65. Sectors 44..56 are an unbranched
chain (`right`/`left` all negative) and every rider's path choice through them
is 0. The task pool had fifty free nodes. So: not the ghost, not the pendulum,
not a push zone (Haunted House's two are zones 6 and 7, at y 724M, and the
rider is at 551M), not player-vs-player, not the chairlift.

The frames say it plainly instead:

```
beh=1/4/1/1  steer=0  aim=253038898,0,44991337   (frozen, 15 frames apart)
beh=1/4/2/1  steer=0  aim=253038898,0,44991337
beh=1/4/3/1  steer=0  aim=253038898,0,44991337
beh=1/4/4/2  steer=0  aim=253038898,0,44991337
beh=1/0/0/0  steer=0  aim=253038898,0,44991337
```

behaviourMode 1, behaviourPhase **4**, cycling steps 1..4 and dropping back to
phase 0 only to be sent straight back. Phase 4 is
`dispatchPostTrickLandingStep` -- the ollie. The steering angle never moves and
the AI target is frozen at a waypoint about eight sectors *behind* the rider.

`updatePlayerNormalDriving` (race_main.c ~1313) is why:

```c
if (player->isCpuControlled != 0) {
    player->cpuInputFlags = determineAIPathChoice(player);
    if (player->cpuInputFlags) { setPlayerBehaviorPhase(player, 4); return 1; }
    if ((speed <= 0x5FFFF && player->snowboardId < SNOWBOARD_HIGH_TECH) || ...) {
        if (isPlayerNearLiftEntry(player) == 0) {
            player->cpuInputFlags = 0;
            setPlayerBehaviorPhase(player, 4);
            return 1;                       /* <-- */
        }
    }
} else {
    if (player->inputButtonsHeld & 0x8000) { setPlayerBehaviorPhase(player, 4); return 1; }
}
...
calculateAITargetPosition(player);          /* never reached on that branch */
steerTarget = computeAngleToPosition(player->aiTarget.x, ...) - rotY;
```

**A CPU rider below 0x5FFFF -- about two thirds of top speed -- away from the
lift entry cannot steer and cannot re-aim.** The hop is meant to be a nudge
that gravity finishes: hop, roll down the hill, cross the threshold, drive. It
is a dead end anywhere gravity cannot do that.

On Haunted House it cannot. Measured, one trial, one rider:

| retrace | sector | units/frame | what |
| ---: | ---: | ---: | --- |
| 15240 | 47 | 570,190 | racing |
| 15300 | 47 | 532,193 | `behaviorMode` 2 -- stunned |
| 15480 | 48 | 1,058 | stopped; x and z byte-identical, only y moving: hopping |
| 16320 | 50 | 138,063 | best it ever recovers to; still under the threshold |
| 16500 | 51 | 6,114 | turned round |
| 17580 | 50 | 0 | byte-identical for ever |

The rider is stunned at sector 47, comes off the drop between 46 and 47 below
the threshold, spends the whole of 48..51 unable to steer, drifts off the
racing line onto the bank above it -- at x 183.6M it sits at y 551.12M where
the previous lap's line through the same x was about y 550.45M -- and settles
into the dip there. The same course, the same rider, one lap earlier: it was
also under the threshold from sector 43 (694 units/frame at r=6180, hopping)
and it *did* get out, because sectors 43..48 run downhill. Sector 50 is where
the hill runs out.

#### Is it a port bug, and would a human hit it?

No, and no. It is the game's own code on the game's own data, and the human
branch above is a button test -- a human keeps steering at any speed, so a
human's own rider is never locked out. What a human *would* see on a real N64
is a rival hopping in a corner of the Haunted House for the rest of the race,
which is exactly what the port shows: two of the game's own CPU rivals were
pinned at sector 50 beside us in the campaign, and one in every trial. Self-play
hits it because self-play **is** a CPU rider.

#### The fix: a marshal, on the autoplay side only

`marshal_tick` in `port/src/debug/race_dbg.c`. When player 1 has made no
progress for 240 retraces **and** its velocity magnitude is at or under
0x5FFFF -- the same threshold that locks it out -- the marshal aims at the
centre of the end of the sector two ahead and sets the rider's x/z velocity to
0x68000 along that line, with `rotY` to match. One frame later the rider is
over the threshold, the game's own phase 0 runs again, and it steers and
accelerates itself. Above the threshold the marshal does nothing: a rider that
is moving and getting nowhere is a different wedge and the watchdog owns it.

Only player 1, and only under `--autoplay`. The rivals are left alone on
purpose -- a rival stuck at sector 50 is a rival we beat -- and no game code is
patched, so `port/patches.txt` is unchanged.

Two traps in writing it. `calculateAITargetPosition` is the obvious way to ask
where the line is and the wrong one: it opens with `getCurrentAllocation()`,
which answers with whatever task the scheduler is *inside*, and the autoplay
tick is not inside one -- calling it from there exits 138 (SIGBUS). The track
graph is plain data on the race's own `GameState`, so the marshal reads
`gameData.sectors[...].endCenterVertexIndex` out of `gameData.vertices`
directly. And the push has to set `rotY` as well as the velocity, or
`applyVelocityDeadzone` kills it as sideslip on the next frame.

Measured: `--trial level=9` went from wedged-at-25,322-frames to **finished,
place 2, 25,322 frames, one push**.

### The three Cross minigames, which are not races

Slot 10 -- the last two courses, and so the credits -- is opened by
`updateStorySlotUnlockStatus` only when slots 0..9 are all 1 **and** slots
12..14 are as well, and 12..14 are the Speed / Shot / X Cross minigames. They
are not on the course list: they are buildings in Jingle Town, and
`handleGameStateComplete` (`src/story/map_state.c`) intercepts
`storyMapLocationIndex` 3, 6 and 9 and sets `currentLevel` to 0xD, 0xE and 0xC
itself. A location id is one less than its handler index, so the navigator
walks into ids 2, 5 and 8.

The part that would have quietly cost a night: **their "place" is not a
place.** `initRace` gives each of them `totalRacers = 1`, so
`players[0].finishPosition` is 0 whether the rider passed or failed, and a
navigator reading it files every attempt as a win, never climbs the ladder, and
waits for ever for a slot 10 the save is never going to open. Each has its own
pass mark, and it is on the GameState the race is still holding when the finish
flag goes up:

| level | raceType | result handler | won when |
| --- | --- | --- | --- |
| 0xC Speed Cross | `RACE_TYPE_SPEED_CROSS` | `handleSkillGameResult` | `playerLost == 0` |
| 0xD Shot Cross | `RACE_TYPE_SHOOT_CROSS` | `handleShotCrossGameResult` | `playerLost == 0` and `shootCrossTargetsHit == 0x14` |
| 0xE X Cross | `RACE_TYPE_X_CROSS` | `handleMeterGameResult` | `playerLost == 0` and `players[0].skillPoints >= 0x12C` |

Shot Cross is the trap in that table: 19 targets out of 20 takes the *win*
branch of the state machine, prints a score, awards gold -- and still sets
`gRaceResultCode` 6, which `awaitRaceResult` writes into the save as a 4. Only
a 5 (or a 3) becomes the 1 that opens a gate. `nav_cross_place` asks the
question the result handler asks, and the navigator prints the numbers on the
finish edge (`sbk-nav: cross game level N type=T: lost=.. targets=../20
skill=../300 -> PASS|fail`) because the GameState is gone by the time the
result screen is up, and "how far short" is the only thing a diagnosis has.

### The loop guard, which had latched on

`nav_town_exits` counts town exits since a race last *started*, and is cleared
on the rising edge of a non-demo race. The edge test used to live inside the
`if (racing)` branch, so `was_racing` was only ever assigned while a race was on
screen: it stuck at 1 when the first story race ended, no later race was ever a
rising edge, and the counter was never reset again. `exit #2, since race 2` --
where the second race should have put it back to `#1` -- is the whole bug in one
line. It would have accused a perfectly healthy campaign of looping about six
courses later, and a guard that cries wolf on a good run is worse than no guard,
because the next person to read the log believes it.

### The trial harness

`port/tools/nightmare_search.py` is the sweep/record/regress/campaign loop.
Unlike the first game's it needs **no input script**: `--autonav` walks the
sequel's menus from the logo to the start line on its own, and `--trial
level=N` aims the course list.

The first game's determinism recipe was `--nopak --nopad`. The sequel needs two
more flags, because it keeps the entire campaign in the EEPROM:

| flag | why |
| --- | --- |
| `--nopad` | no gamepad: no Rumble Pak probe, no stray stick |
| `--nopak` | no Controller Pak (and, alone, no EEPROM either) |
| `--eeprom FILE` | the save device points at a scratch file, deleted before every trial. Without it a trial rewrites the user's `eeprom.sav` *and* stops repeating, because the menus differ once the progress does |
| `--unlockall` | `buildUnlockedLevelList` (`story_intro.c`) only offers courses whose `levelUnlockStatus` is non-zero, so on a fresh scratch save `level=N` can only ever aim at course 0. This runs the game's **own** cheat, `unlockAllContent` (`title_screen.c`), rather than a hand-written save block, and re-runs it whenever `loadSaveData` overwrites it |

A golden movie and the navigator do not fight, which is worth knowing before
reading a `regress` failure as one. `sbk_input_play_read` prefers the movie
whenever one is loaded and only falls back to the script otherwise
(`port/src/platform/input_play.c`), so on a replay the `.m64` supplies every
button and `--autonav` supplies only the things a button never carried anyway
-- the parked cursors, the skipped cutscene, the town's exit byte. That split is
what makes the replay reproducible at all: the navigator's presses are in the
movie, its memory writes are not.

`--frames 60000` is the other half: at `--headless`'s 12x that is about 80 s of
wall clock, and a Sunny Mountain race plus the menu walk is under 20000 frames,
so a rider that wedges itself costs a minute instead of a quarter of an hour.

## What is not done

* ~~**The CPU rider wedges itself on Turtle Island.**~~ **Found and fixed** --
  see `docs/nightmare-row.md` for the whole chain. It was never the borrowed
  path table. The attach now logs the asset's header, and Turtle Island's is
  `16,16,504,992`: three authored sets for four `playerIndex` values, with the
  human's index aliased onto rider 2's, so lending slot 0 and lending slot 1
  hand over identical bytes. The wedge reproduced either way.

  What it actually is: **a lap wraps at the chairlift, not a finish line.** All
  three `currentLap++` sites in `race_main.c` (4604, 4752, 4959) are lift steps,
  and while a rider waits for the lift it is pinned at `storedPosition` every
  frame. On Turtle Island the way out of that wait is
  `if (spawnChairliftEffect(player))` (`race_main.c:4516`, `memoryPoolId == 1`
  has its own branch), and `spawnChairliftEffect` is a `scheduleTask` that
  returns NULL when the task pool is full (`particle_items.c:2221`).

  The old `--nightmare` row filled that pool. It wrote `delay=0,
  useChance=255`: every item thrown the instant it is held, by all four riders,
  for the whole race -- against an authored floor of `delay` 120 across every
  one of the game's own eight rows. The last rider to reach the lift cannot get
  a task, and sits there for ever. Every observation fits: player 1 froze at
  sector 96 with `prog=0`, `lap=0`, its position byte-identical frame to frame
  **and identical across two separate runs** (a rider being *held*, not lost);
  the rivals were already onto lap 1 because they reached the lift while the
  pool still had room; plain `--autoplay` with no Nightmare row finished Sunny
  Mountain three times; and both hung trials in the first sweep were that row.

  With the row re-anchored on the game's own data, Turtle Island finishes.

* `nightmare_search.py sweep` -- the *per-course rider ladder*, which is a
  different search from `nm` -- still has not been run. Only `nm`, the
  Nightmare row itself, has, and only one course (0) has a golden movie. The
  ladder is what would fill `--plan` out beyond its single row.
* A full progression through **every** course has still not been watched end to
  end in one sitting. What has been watched: Sunny Mountain and Turtle Island
  each raced and won, an EEPROM write verified after each
  (`sbk-nav: saved slot 0`), the town left by the right door, and the course
  list parked on the game's own next-up marker.
* The near player model renders as a **black silhouette** during a race while
  the distant riders are correct (`g4-shots/sbk2-autoplay-race.png`). This is a
  graphics bug the self-play tooling made easy to see, not a self-play bug --
  probably the same class as the combiner fix above, and `--dumpdl` on a race
  task is the way in.
* ~~The draw-distance patch never matches.~~ **Done.** The three entries in
  `patches.txt` name the sequel's own `src/race/race_session.c` and its
  `RACE_VIEWPORT_FAR_PLANE 3800.0f` / `MULTIPLAYER_RACE_VIEW_FAR_PLANE 3000.0f`
  / `BOSS_RACE_VIEW_FAR_PLANE 2000.0f`, and the mirrored source comes out as
  `(3800.0f * sbk_far_scale)`. `sbk_far_scale` reaches every game translation
  unit through `port/src/port_override.h`, which `port/Makefile` force-includes,
  so no game file has to be touched to see it.
* Registering the sequel in the first game's launcher (`src/settings.c` there
  lists the games) — a small separate change in the other repo, once this one
  plays a race.
