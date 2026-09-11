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
* Both hung trials in the first sweep were this row.

It is not the borrowed path table, which was the standing suspicion. The attach
now logs the asset's header, and on Turtle Island the offsets are
`16,16,504,992` -- slot 0 and slot 1 are the same table, so lending either one
is the same lend.

## The grid

Anchored on the rows above rather than on round numbers, and run on Turtle
Island (level 1), the course that exposes the lift.

<!-- SWEEP TABLE -->

## The chosen row

<!-- CHOSEN ROW -->
