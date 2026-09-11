# The Nightmare row: what the sweep measured

`--nightmare` retunes one spare row of `gAIPlayerParams` and points every CPU
rider in the race at it, player 1 included. This is what that row should hold
and how the numbers were arrived at.

## What the row actually controls

`gAIPlayerParams[8][0x11]` lives in `src/race/race_main.c:391`. Each entry is
four bytes, and the field order is the one in `include/race/hit_reactions.h`:

```c
typedef struct AIPlayerParamEntry {
    /* 0x0 */ u8 useChance;
    /* 0x1 */ u8 delay;
    /* 0x2 */ u8 altChance;
    /* 0x3 */ u8 pad;
} AIPlayerParamEntry;
```

Two places read it, and they read different slots of it:

| read | slot | what it does |
| --- | --- | --- |
| `race_main.c:803` | `[d][0].useChance` | `maxSpeedCap -= useChance * 0x202` -- a **top-speed tax** |
| `hit_reactions.c` | `[d][1..0x10]` | per item: `delay` before use, `useChance` / `altChance` to use it |

So slot 0 is not an item at all: it is the rider's own speed handicap. Slots
1..0x10 are the sixteen items.

## The game's own eight rows

Worth reading before inventing a ninth. Parsed straight out of the table:

| row | tax (slot 0 `useChance`) | item `useChance` avg | item `delay` avg | item `delay` min |
| --- | --- | --- | --- | --- |
| 0 (easiest) | 168 | 93 | 231 | 90 |
| 1 | 128 | 117 | 211 | 120 |
| 2 | 96 | 107 | 244 | 180 |
| 3 | 64 | 135 | 200 | 120 |
| 4 | 32 | 127 | 213 | 150 |
| 5 (hardest named) | 16 | 153 | 183 | 120 |
| 6 | 0 | 138 | 221 | 120 |
| 7 | 0 | 166 | 150 | 120 |

Two things fall out of it:

* **The tax is the difficulty ladder.** It runs 168 down to 0 across the rows;
  everything else wobbles. But because `--nightmare` puts *every* rider on the
  same row, the tax is not a difficulty dial here -- it is a speed limit on the
  whole field. Lowering it makes the rivals faster too.
* **`delay` never goes below 90, and only row 0 goes that low.** The floor
  across every authored row is effectively 120.

## The row that hung the first sweep

The first `--nightmare` wrote `tax=0, delay=0, useChance=255, altChance=255`:
every item thrown the instant it is held, by all four riders, for the whole
race. That is not "the hardest row" -- it is about six times faster than the
fastest item the designers ever shipped, and off the end of the scale in the
one direction the game cannot absorb.

What it does is starve the task scheduler. Every item and its effects are
scheduled tasks, and `scheduleTask` returns NULL when the pool is full. Most
callers shrug that off. One cannot:

```c
case 1:                                   /* memoryPoolId 1 == Turtle Island */
    if (spawnChairliftEffect(player)) {   /* scheduleTask(initChairliftEffect, ...) */
        ...
        player->behaviorStep = 8;         /* <- the only way out of the lift wait */
    }
    break;
```

(`race_main.c:4516`, and `spawnChairliftEffect` is `particle_items.c:2221`.)

A lap in this game does not wrap at a finish line -- it wraps at the
**chairlift**. All three `currentLap++` sites in `race_main.c` (4604, 4752,
4959) are lift steps. While a rider waits for the lift it is pinned at
`storedPosition` every frame, so if `spawnChairliftEffect` cannot get a task,
the rider sits there, byte-identical, for ever, and the race never ends.

That is the wedge, and every observation fits it:

* Player 1 froze at Turtle Island sector 96, `prog=0`, `lap=0`, with its
  position byte-identical frame to frame **and identical across two separate
  runs** -- a rider being *held*, not one that got lost.
* The rivals were already round onto lap 1 at sectors 18-25: they reached the
  lift earlier, while the pool still had room. Player 1 arrived last.
* Plain `--autoplay` with no Nightmare row finished Sunny Mountain three times.
* Turtle Island with this row wedged on both attempts; with the searched row it
  finished twice in a row.

(The two DNFs the first sweep recorded for this row are *not* evidence for it.
They were level 0, and they were a 600 s timeout cutting a slow race off --
level 0 with this row does finish, in about 349 s of racing plus the menu walk.
That is why the harness now watches the rider's position instead of a clock.)

It is not the borrowed path table, which was the standing suspicion. The attach
now logs the asset's header, and on Turtle Island the offsets are
`16,16,504,992` -- slot 0 and slot 1 are the same table, so lending either one
is the same lend.

## The grid

Anchored on the rows above rather than on round numbers, and run on Sunny
Mountain (level 0) because it is the short course -- with the winner then
checked on Turtle Island, which is the one that exposes the lift.

`nightmare_search.py nm 0` -- Sunny Mountain, `char=0 board=8`, player 1 and
all three rivals on the row. `place` is player 1's finish; DNF means the stall
detector saw its position stop changing for 150 s.

| tax | item delay | item useChance | place | frames | gold | wall |
| ---:| ---:| ---:| ---:| ---:| ---:| ---:|
| 0 | 120 | 205 | **1st** | **14,106** | 2,600 | 337 s |
| 0 | 120 | 255 | DNF | -- | -- | 428 s |
| 0 | 150 | 205 | 2nd | 24,598 | 2,900 | 514 s |
| 0 | 90 | 205 | 3rd | 14,906 | 1,300 | 345 s |
| 168 | 120 | 205 | DNF | -- | -- | 340 s |
| 0 | 0 | 255 | 2nd | 15,170 | 1,300 | 349 s |

Three things in it are worth more than the winning line:

* **`delay` 120 is a real optimum, not the end of a trend.** 150 drops to
  second and 90 to third, from either side.
* **`useChance` 255 does not finish even at a sane delay.** The item *rate*
  starves the pool on its own; the delay is not the only lever.
* **The old row finished here.** `delay=0, useChance=255` came second on Sunny
  Mountain. It is Turtle Island that it cannot survive -- which is the honest
  version of this story: the old row is not reliably fatal, it is reliably
  fatal *somewhere*, and a sweep run only on course 0 would have missed it.
  The two DNFs recorded for it in the first sweep were a 600 s timeout cutting
  a slow race off, not the wedge; the wedge is the one that froze the rider's
  position byte-identically on level 1.

`tax=168` failing is the other surprise. The full easiest-row tax is applied to
every rider on the row, player 1 included, so the whole field crawls -- long
enough at the lift for the pool pressure to catch someone.

## The chosen row

```
tax = 0    item delay = 120    item useChance = 205    item altChance = 205
```

Now the built-in default of `--nightmare` (`port/src/debug/race_dbg.c`), so
`--nightmare` with no other flags is the searched row rather than the guessed
one. `--trial nmtax=..,nmdelay=..,nmuse=..,nmalt=..` still overrides all four.

Verified on both courses: Sunny Mountain first place in 14,106 frames, and
Turtle Island -- the course the old row could not get past -- raced twice in a
row with an EEPROM write after each (`sbk-nav: saved slot 0, gold=5900`, then
`gold=10800`).

## Goldens

`port/scripts/golden/level0.m64`, recorded with
`nightmare_search.py record 0` and replayed by `nightmare_search.py regress`.

The determinism recipe is the sequel's, not the first game's `--nopak --nopad`:

| flag | why |
| --- | --- |
| `--nopad` | no gamepad: no Rumble Pak probe, no stray stick |
| `--nopak` | no Controller Pak |
| `--eeprom FILE` | a scratch save, deleted before every trial -- the sequel keeps the whole campaign in the EEPROM, so a shared save changes the menus run to run |
| `--unlockall` | the game's own `unlockAllContent`, so `level=N` can aim past course 0 on a fresh scratch save |
| the Nightmare row | pinned into the golden's own spec, not left to the built-in default |

The last one is a trap worth naming: a golden movie replayed against a
different `gAIPlayerParams` row is not a regression test. `golden_spec()` now
writes the four `nm*` values into the spec it replays.

## The campaign run

`--autonav --autoplay --nightmare --saveevery 1 --plan 0:0:8:0`, fullscreen, on
the user's own `eeprom.sav` (backed up first to
`~/eeprom.sav.bak-2026-09-11-campaign`).

It picked the save up where it stood -- Sunny Mountain already won, 12,500 gold
-- and then drove itself:

```
sbk-nav: town -> leave for the course list (exit #1, since race 0)
sbk-nav: level list: cursor -> 1 (level 1 of 2 offered)
sbk-nav: cutscene skipped
sbk-nav: race 1 finished, gold=12500, want=SAVE
sbk-nav: saved slot 0, gold=18400 (save #1)
sbk-nav: race 2 finished, gold=18400, want=SAVE
sbk-nav: saved slot 0, gold=41200 (save #2)
```

Two courses won and two EEPROM writes verified in one sitting, then on to
**level 3, the Jingle Town boss race** -- lap 1/1, boss HUD, the ten snowman
markers along the bottom (`g4-shots/sbk2-campaign-level3.png`). Turtle Island,
the course that could not be finished at all before, is now just the second one
on the way through.

Screenshots: `g4-shots/sbk2-campaign-fullscreen.png` (a fullscreen race start,
which is also what settles that fullscreen works for the sequel) and
`g4-shots/sbk2-campaign-level3.png` (the boss race).

Worth noting for whoever runs this next: the navigator logged
`town -> leave for the course list (exit #1, since race 2)` three times in a
row around the level-3 handover. `nav_town_exits` is being reset each time, so
the loop guard that counts town exits never sees a run -- if the campaign ever
does stall on the boss courses, that counter is the first thing to look at.
