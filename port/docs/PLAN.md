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
  `uses3DRendering`.
* Audio: libmus over libultra's libaudio, `alAudioFrame`, ucode `aspMain` —
  **audio ABI 1, and `assets/rsp/aspMain.textbin.bin` is byte-identical to the
  first game's**, so `port/src/audio/audio_task.c` was reused unchanged and
  works. `n_aspMain` appears nowhere. Output rate 22050 Hz.
* Saves: **EEPROM 4 Kbit** (64 blocks of 8 bytes; `controller_io.c` reads
  0x58-byte records and wipes 0x200) *and* the Controller Pak, plus the Rumble
  Pak. The first game had no EEPROM; `port/src/ultra/os_eeprom.c` is new.
* Segments: only 0, 1, 2, 3, set per display-list object.
* No `osGetTime` / `osSetTimer` / `osGetCount` anywhere in `src/`.

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
2. **Menus** (partly done 2026-09-11): the title screen with the character
   line-up and the START / TRAINING / OPTION menu
   (`g4-shots/sbk2-ov-45s.png`), the file-select screen and its ~MENU~
   (`sbk2-save-52s.png`), and the level preview with its 3D fly-through and
   character model (`sbk2-menu-45s.png`, `sbk2-menu-65s.png`). A race has not
   been driven yet.
3. **Audio**: ABI 1 confirmed and `audio_task.c` reused as-is; the demo
   produces samples. Not yet listened to on the G4, and the menus so far report
   a peak of zero — worth checking whether the sequel simply has no menu music
   before that run, or whether `MusInitialize`'s heap is not reaching the
   synthesizer.
4. **Controller Pak, EEPROM, fullscreen, pad, launcher slot**: EEPROM written
   and read (the file-select screen proves it); the rest is inherited from the
   first game's port and untested here.

## What is not done

* `--autoplay`, `--soak`, `--nightmare`, `--trial`, `--plan`, `--autonav`,
  `--status`, `--peek`: the first game's `race_dbg.c` and `menu_nav.c` reach
  into *its* menu and race state by symbol name, so none of it carries over.
  `port/src/debug/game_hooks.c` keeps the switches parsing and says so.
  Rebuilding them against the sequel's state (`RaceState` in
  `src/race/race_session.c`, the task list in `src/system/task_scheduler.c`) is
  the next large piece of work.
* The draw-distance patch (`--drawdistance`): the sequel's far planes are
  `RACE_VIEWPORT_FAR_PLANE 3800.0f`, `MULTIPLAYER_RACE_VIEW_FAR_PLANE 3000.0f`
  and `BOSS_RACE_VIEW_FAR_PLANE 2000.0f` in `src/race/race_session.c`; the
  patches.txt entries still name the *first* game's files and so never match.
* Registering the sequel in the first game's launcher (`src/settings.c` there
  lists the games) — a small separate change in the other repo, once this one
  plays a race.
