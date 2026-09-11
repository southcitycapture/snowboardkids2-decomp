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
   and read (the file-select screen proves it); the rest is inherited from the
   first game's port and untested here.

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
own table untouched. The values are `tax=0, delay=0, useChance=255,
altChance=255` and are exposed to `--trial` as `nmtax/nmdelay/nmuse/nmalt` so
`nightmare_search.py nm` can search them rather than have them guessed.

`--nightmare` also gives player 1 `SNOWBOARD_SPEED_LEVEL_3`, the fastest board
that has no drawback.

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

`--frames 60000` is the other half: at `--headless`'s 12x that is about 80 s of
wall clock, and a Sunny Mountain race plus the menu walk is under 20000 frames,
so a rider that wedges itself costs a minute instead of a quarter of an hour.

## What is not done

* **The CPU rider wedges itself on Turtle Island.** The campaign now walks
  itself: Sunny Mountain is won (12,500 gold), the EEPROM is written, the town
  is left by the right door, and the course list is parked on course 1 -- "level
  list: cursor -> 1 (level 1 of 2 offered)", the game's own next-up marker read
  back. Then the rider reaches Turtle Island's **LIFT IN** gate and stops dead:
  lap 1 of 3, 4th, no movement for ten minutes of wall clock
  (`g4-shots/sbk2-turtle-stuck.png`). The handoff itself is clean -- "path table
  attached (0x802ad430)" -- so this is not the missing-asset retry; it is the
  borrowed path table. Player 1 has no `bossRaceData` of its own, so the port
  lends it rider 2's and reads **slot 0**, and slot 0 is the preference set for
  a rider standing somewhere else. On a course whose route forks at a lift that
  is enough to park it. The way in is `--racedbg` on level 1 watching
  `sectorIndex` / `lapProgressRemaining` / `behaviorMode` at the gate, and then
  whether the slot should be `playerIndex`-indexed after all (`race_main.c`
  ~1105) rather than always 0.
* A full progression through every course, with a verified EEPROM write after
  each race, therefore still has not been watched end to end.
  `nightmare_search.py sweep` (the per-course rider ladder) has not been run
  either; only `nm`, the Nightmare row itself, has.
* The near player model renders as a **black silhouette** during a race while
  the distant riders are correct (`g4-shots/sbk2-autoplay-race.png`). This is a
  graphics bug the self-play tooling made easy to see, not a self-play bug --
  probably the same class as the combiner fix above, and `--dumpdl` on a race
  task is the way in.
* The draw-distance patch (`--drawdistance`): the sequel's far planes are
  `RACE_VIEWPORT_FAR_PLANE 3800.0f`, `MULTIPLAYER_RACE_VIEW_FAR_PLANE 3000.0f`
  and `BOSS_RACE_VIEW_FAR_PLANE 2000.0f` in `src/race/race_session.c`; the
  patches.txt entries still name the *first* game's files and so never match.
* Registering the sequel in the first game's launcher (`src/settings.c` there
  lists the games) — a small separate change in the other repo, once this one
  plays a race.
