#!/usr/bin/env python3
"""Run race trials for Snowboard Kids 2 on the G4 and keep the results.

One trial is one headless race (12x real time) with player 1 handed to the
game's own CPU rider, aimed at a level, set up with a character and a board and
given a speed edge; the port prints one `sbk-trial: result` line when player 1
finishes and quits.

    nightmare_search.py run level=0,char=3,board=8
    nightmare_search.py sweep 0 2 4        # rider sweep + boost ladder per level
    nightmare_search.py table              # the best setup per level
    nightmare_search.py record 0 2         # re-run the winners with --record
    nightmare_search.py regress            # replay every golden movie, pass/fail
    nightmare_search.py nm                 # search the Nightmare row itself
    nightmare_search.py nmtable            # ...and the table it produced
    nightmare_search.py plan               # the book, as --plan wants it
    nightmare_search.py campaign           # a long self-playing session

Unlike the first game's version this needs **no input script**: `--autonav`
(port/src/debug/menu_nav.c) walks the sequel's menus by name from the logo to
the start line, and `--trial level=N` parks the course list's cursor on the
level the trial wants. `--nopak --nopad` is what makes a trial reproducible --
with the EEPROM and a gamepad in play the menus differ run to run.
"""
import csv, os, subprocess, sys, time

G4 = os.path.expanduser("~/Apps/isle-ppc-tools/g4/g4")
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "nightmare_results.csv")
GOLDEN = os.path.join(HERE, "..", "scripts", "golden")
FIELDS = ["spec", "level", "char", "board", "boost", "diff", "nmtax", "nmdelay",
          "nmuse", "nmalt", "place", "finished_before", "frames", "gold", "wall_s"]

# Where a trial's save block lives. The sequel keeps the whole campaign in the
# EEPROM -- which courses the level list offers included -- so a trial that
# shared the user's eeprom.sav would both rewrite it and stop being
# reproducible. --eeprom points the save device at this scratch file, which is
# deleted before every trial, and --unlockall then runs the game's own
# unlockAllContent so `level=N` can aim past course 0. Together with --nopak
# (no Controller Pak) and --nopad (no gamepad: no Rumble Pak probe, no stray
# stick) that is the sequel's determinism recipe, the equivalent of the first
# game's --nopak --nopad.
TRIAL_EEPROM = "/Users/zach/trial-eeprom.sav"

# Level ids are the game's own (build/include/generated/course_definitions).
LEVELS = [0, 1, 2, 4, 5, 6, 8, 9, 10]
LEVEL_NAMES = {0: "Sunny Mountain", 1: "Turtle Island", 2: "Jingle Town",
               3: "Jingle Town (boss)", 4: "Wendy's House", 5: "Linda's Castle",
               6: "Crazy Jungle", 7: "Crazy Jungle (boss)", 8: "Starlight Highway",
               9: "Haunted House", 10: "Ice Land", 11: "Ice Land (boss)",
               12: "Speed Cross", 13: "Shot Cross", 14: "X Cross"}


def g4(*args, **kw):
    return subprocess.run([G4, *args], capture_output=True, text=True, **kw)


def trial(spec, frames=60000, timeout=1500, stall=150, extra=()):
    """One headless race, ended either by the result line or by the rider
    standing still.

    --headless implies --turbo, which takes a retrace "as soon as the game is
    idle" -- and the sequel is never idle: three threads stay runnable every
    frame, so headless buys no wall clock at all here and a trial runs at 1x,
    not the first game's 12x. A three-lap Turtle Island race is about eleven
    minutes of that, which is why `timeout` is 1500 s and not the 600 s it
    started at: 600 s cut healthy races off mid-final-lap and recorded them as
    hangs.

    A fixed timeout is the wrong instrument anyway. What a hung trial actually
    looks like is a rider that has stopped moving -- pinned at `storedPosition`
    waiting for a chairlift task that will never be allocated -- so this watches
    p0's position in the --racedbg lines and gives up after `stall` seconds
    without it changing. A wedge costs two and a half minutes instead of
    twenty-five, and a slow course is still allowed to finish."""
    g4("stop")
    g4("ssh", "rm -f %s" % TRIAL_EEPROM)
    t0 = time.time()
    # --menutrace costs nothing and is the only way to read a DNF afterwards:
    # without it the log cannot say whether the rider was still racing, sitting
    # on a results screen, or never left a menu. --racedbg is what the stall
    # detector reads; it prints once a second.
    g4("run", "--headless", "--nopak", "--nopad", "--eeprom", TRIAL_EEPROM,
       "--unlockall", "--autonav", "--nightmare", "--menutrace", "--status",
       "--racedbg", "--trial", spec + ",quit=1", "--frames", str(frames), *extra)
    log = ""
    last_pos, last_move = None, time.time()
    while time.time() - t0 < timeout:
        time.sleep(10)
        log = g4("ssh", "grep -E 'sbk-trial: result|EXITCODE' isle-log.txt").stdout
        if "EXITCODE" in log or "sbk-trial: result" in log:
            break
        # p0's world position, straight off the last --racedbg line for it.
        pos = g4("ssh", "grep 'sbk-race' isle-log.txt | grep ' p0 ' | tail -1").stdout
        pos = next((f for f in pos.split() if f.startswith("pos=")), None)
        if pos is not None and pos != last_pos:
            last_pos, last_move = pos, time.time()
        elif pos is not None and time.time() - last_move > stall:
            print("stalled trial (p0 still for %ds at %s), stopping: %s"
                  % (stall, last_pos, spec), flush=True)
            g4("stop")
            break
    else:
        print("hung trial (>%ds), stopping: %s" % (timeout, spec), flush=True)
        g4("stop")
    row = {"spec": spec, "wall_s": round(time.time() - t0)}
    for kv in spec.split(","):
        if "=" in kv:
            k, v = kv.split("=", 1)
            if k in ("nmtax", "nmdelay", "nmuse", "nmalt"):
                row[k] = int(v)
    for line in log.splitlines():
        if line.startswith("sbk-trial: result"):
            for kv in line.split()[2:]:
                k, v = kv.split("=")
                row[k] = int(v)
    return row


def save(row):
    new = not os.path.exists(OUT)
    with open(OUT, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        if new:
            w.writeheader()
        w.writerow({k: row.get(k, "") for k in FIELDS})
    print(row, flush=True)
    return row


def rows():
    if not os.path.exists(OUT):
        return []
    with open(OUT) as f:
        return [r for r in csv.DictReader(f) if r.get("place")]


def update_row(spec, out):
    """Re-measure every CSV row with this spec: a golden movie is checked
    against the row it was recorded from, so a stale row fails regress for the
    wrong reason."""
    if not os.path.exists(OUT) or not out.get("place"):
        return
    with open(OUT) as f:
        all_rows = list(csv.DictReader(f))
    for r in all_rows:
        if r["spec"] == spec:
            for k in ("place", "finished_before", "frames", "gold", "wall_s"):
                if k in out:
                    r[k] = out[k]
    with open(OUT, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows([{k: r.get(k, "") for k in FIELDS} for r in all_rows])


def done(spec):
    """Measured already -- *including* the ones that never finished.

    This reads all_rows(), not rows(). rows() drops anything with no place,
    and a row with no place is a trial that burned the full timeout: exactly
    the ones worth not repeating. Checking rows() here made every hung point
    in the grid cost its ten minutes again on the next sweep."""
    return any(r["spec"] == spec for r in all_rows())


def run(spec):
    if done(spec):
        print("skip (already measured):", spec, flush=True)
        return next(r for r in rows() if r["spec"] == spec)
    return save(trial(spec))


# Boards worth trying: the three speed levels and the two specials that are
# fast without a drawback (gSnowboardStatsTable, src/race/character_stats.c).
RIDERS = ((0, 8), (4, 8), (8, 8), (0, 9), (4, 2))


def sweep_level(level, riders=RIDERS):
    best = None
    for chr_, board in riders:
        r = run("level=%d,char=%d,board=%d" % (level, chr_, board))
        if not r.get("place"):
            continue
        key = (int(r["place"]), int(r["frames"]))
        if best is None or key < best[0]:
            best = (key, chr_, board)
        if int(r["place"]) == 1:
            break
    if best is None:
        print("level %d: no result" % level, flush=True)
        return
    (place, frames), chr_, board = best
    print("level %d: best rider char=%d board=%d place=%d frames=%d"
          % (level, chr_, board, place, frames), flush=True)
    if place == 1:
        return
    # The boost ladder has to be *searched*, not extrapolated: the sequel gives
    # every rider a rank handicap too (race_main.c ~807, D_800BAA9C_AA94C
    # indexed by finishPosition), so more top speed can take the lead sooner
    # and buy the throttle sooner.
    for boost in (32, 64, 96, 128):
        r = run("level=%d,char=%d,board=%d,boost=%d" % (level, chr_, board, boost))
        if r.get("place") == 1:
            print("level %d: wins with boost=%d" % (level, boost), flush=True)
            return
    print("level %d: still losing at boost=128" % level, flush=True)


def sweep_nightmare(level=0):
    """Search the Nightmare row itself rather than guessing it.

    gAIPlayerParams[7] is what --nightmare writes: slot 0's useChance is the
    CPU's top-speed tax (race_main.c ~804 subtracts useChance * 0x202 from
    maxSpeedCap) and every other slot's delay/useChance decide how soon and how
    often an item is thrown. The trial exposes all four as nmtax/nmdelay/
    nmuse/nmalt, so the row can be measured like anything else.

    The grid separates the two things the row does, because they pull opposite
    ways. Every rider in the race sits on this row, player 1 included, so the
    tax is not a difficulty dial: it is a speed limit on the whole field, and
    lowering it makes everyone faster, not just the rivals. The items are the
    real difficulty.

    The grid is anchored on the game's *own* eight rows rather than on round
    numbers, because the first attempt at this row was not merely aggressive,
    it was off the end of the authored scale -- and that is what hung the first
    sweep. Reading gAIPlayerParams (race_main.c:391) across all eight rows:

        row   0 (easiest) ......... 5 (hardest named) .. 6/7 (spare)
        tax    168   128   96   64   32   16              0
        item useChance (avg)  93 -> 153, never above 230
        item delay     (avg) 231 -> 183, and **never below 120 in any row**

    The old row wrote delay=0, useChance=255: six times faster than the
    fastest item the designers ever shipped, on every rider at once. The field
    stunlocks itself -- every rider spends the race in a hit reaction -- and
    the race genuinely never ends. Both hung trials in the first sweep were
    that row, and it is kept below as the control that reproduces the hang.

    What the search wants is the row that finishes *fastest in first place*
    while staying inside the shape the game's own data uses."""
    grid = [
        # (tax, delay, useChance). Tax 0 = row 6's, the hardest authored.
        (0, 120, 205),    # the hardest authored shape with the tax taken off
        (0, 120, 255),    # ...and every item certain to be used
        (0, 150, 205),    # item delay slower than row 5's average
        (0, 90, 205),     # ...and faster than any authored row
        (168, 120, 205),  # row 0's tax: the floor, for the table
        (0, 0, 255),      # the control: the row that hung the first sweep
    ]
    for tax, delay, use in grid:
        run("level=%d,char=0,board=8,nmtax=%d,nmdelay=%d,nmuse=%d,nmalt=%d"
            % (level, tax, delay, use, use))


def all_rows():
    """Every measured row, finished or not. rows() drops the ones with no
    place, because a golden movie can only be cut from a race that ended -- but
    a trial that never ended is the whole point of a Nightmare search, so the nm
    table reads this instead."""
    if not os.path.exists(OUT):
        return []
    with open(OUT) as f:
        return list(csv.DictReader(f))


def nm_rows():
    return [r for r in all_rows() if r.get("nmuse") not in (None, "")]


def nmtable():
    """The Nightmare sweep as a table: one line per row tried."""
    print("%-6s %-6s %-8s %-6s %-6s %-6s %-8s %-7s %s"
          % ("level", "tax", "delay", "use", "alt", "place", "frames", "gold", "wall_s"))
    for r in sorted(nm_rows(), key=lambda r: (int(r["nmtax"] or 0), int(r["nmdelay"] or 0),
                                              -int(r["nmuse"] or 0))):
        # No place means the rider never crossed the line inside the trial's
        # window: with every rider on the row throwing everything on sight, the
        # whole field can stunlock itself and the race simply does not end.
        print("%-6s %-6s %-8s %-6s %-6s %-6s %-8s %-7s %s"
              % (r["level"], r["nmtax"], r["nmdelay"], r["nmuse"], r["nmalt"],
                 r["place"] or "DNF", r["frames"] or "-", r["gold"] or "-", r["wall_s"]))
    wins = [r for r in nm_rows() if r["place"] and int(r["place"]) == 1]
    if wins:
        best = min(wins, key=lambda r: (int(r["nmtax"]), int(r["frames"])))
        print("\nbest (wins on the lowest tax): tax=%s delay=%s use=%s alt=%s "
              "place=%s frames=%s" % (best["nmtax"], best["nmdelay"], best["nmuse"],
                                      best["nmalt"], best["place"], best["frames"]))


# ---------------------------------------------------------------- reporting

def best_row(level):
    cands = [r for r in rows() if r["level"] == str(level)]
    if not cands:
        return None
    return min(cands, key=lambda r: (int(r["place"]), int(r["frames"])))


def table():
    for l in LEVELS:
        r = best_row(l)
        if r is None:
            print("level %-2s  no result   %s" % (l, LEVEL_NAMES.get(l, "")))
        else:
            print("level %-2s  char=%s board=%s boost=%-3s place=%s frames=%-6s gold=%-6s %s"
                  % (l, r["char"], r["board"], r["boost"] or 0, r["place"], r["frames"],
                     r["gold"], LEVEL_NAMES.get(l, "")))


def plan_arg():
    """The book as the port's --plan wants it: LEVEL:CHAR:BOARD:BOOST."""
    out = []
    for l in LEVELS:
        r = best_row(l)
        if r is not None and int(r["place"]) == 1:
            out.append("%s:%s:%s:%s" % (l, r["char"], r["board"], r["boost"] or 0))
    return ",".join(out)


# ------------------------------------------------------------ golden movies

def golden_spec(level):
    r = best_row(level)
    if r is None or int(r["place"]) != 1:
        return None, r
    spec = "level=%s,char=%s,board=%s" % (r["level"], r["char"], r["board"])
    if int(r["boost"] or 0):
        spec += ",boost=%s" % r["boost"]
    # Pin the Nightmare row into the spec when the winning row carried one.
    # Leaving it implicit works only while the port's built-in defaults happen
    # to equal the searched row -- and a golden movie that replays against a
    # different row is not a regression test, it is a coin toss.
    if r.get("nmuse"):
        spec += ",nmtax=%s,nmdelay=%s,nmuse=%s,nmalt=%s" % (
            r["nmtax"], r["nmdelay"], r["nmuse"], r["nmalt"])
    return spec, r


def record(level):
    """Replay the winning setup once more with --record and keep the movie.

    The rider is the game's own CPU logic, so the movie holds the *navigator's*
    controller input (the menu walk); replaying it needs the same trial spec,
    which regress() reads back out of the CSV."""
    spec, r = golden_spec(level)
    if spec is None:
        print("level %s: no winning row to record" % level, flush=True)
        return None
    remote = "/Users/zach/golden-level%s.m64" % level
    out = trial(spec, extra=("--record", remote))
    out["spec"] = spec
    update_row(spec, out)
    if out.get("place") != 1:
        print("level %s: record run did not win (%r), movie not kept" % (level, out), flush=True)
        return None
    os.makedirs(GOLDEN, exist_ok=True)
    local = os.path.join(GOLDEN, "level%s.m64" % level)
    g4("pull", remote, local)
    print("level %s: golden movie %s (place=1 frames=%s)" % (level, local, out["frames"]), flush=True)
    return local


def regress(levels=None):
    if levels is None:
        levels = [l for l in LEVELS if os.path.exists(os.path.join(GOLDEN, "level%s.m64" % l))]
    fails = 0
    print("%-8s %-30s %-16s %-16s %s" % ("level", "spec", "expected", "got", "result"))
    for l in levels:
        local = os.path.join(GOLDEN, "level%s.m64" % l)
        spec, r = golden_spec(l)
        if spec is None or not os.path.exists(local):
            print("%-8s %-30s %-16s %-16s %s" % (l, spec or "-", "-", "-", "SKIP (no movie)"))
            continue
        remote = "/Users/zach/regress-level%s.m64" % l
        subprocess.run([G4, "ssh", "cat > %s" % remote], stdin=open(local, "rb"))
        got = trial(spec, extra=("--play", remote))
        exp = "place=%s/%s" % (r["place"], r["frames"])
        gots = "place=%s/%s" % (got.get("place"), got.get("frames"))
        ok = got.get("place") == int(r["place"]) and got.get("frames") == int(r["frames"])
        fails += not ok
        print("%-8s %-30s %-16s %-16s %s" % (l, spec, exp, gots, "PASS" if ok else "FAIL"), flush=True)
    print("%d level(s) checked, %d failed" % (len(levels), fails))
    return fails


# ----------------------------------------------------------------- campaign

def status():
    out = g4("ssh", "grep 'sbk-status' isle-log.txt | tail -1").stdout.strip()
    d = {}
    for kv in out.split()[1:]:
        if "=" in kv:
            k, v = kv.split("=", 1)
            d[k] = v
    return d


def campaign_start(save_every="1", frames="4000000"):
    """A long self-playing session: --autonav walks the sequel's menus, every
    race is played by the CPU rider on the book's setup for that level, and
    after every `save_every` races the navigator aims the rider at the story
    map's save point so the purse reaches the EEPROM."""
    g4("stop")
    plan = plan_arg()
    args = ["run", "--fullscreen", "--nightmare", "--status", "--nopad",
            "--menutrace", "--autonav", "--saveevery", str(save_every),
            "--autoplay", "--frames", str(frames)]
    if plan:
        args += ["--plan", plan]
    g4(*args)
    print("campaign started: plan=%s" % (plan or "(none yet)"), flush=True)


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "run":
        run(a[1])
    elif len(a) > 1 and a[0] == "sweep":
        for l in a[1:]:
            sweep_level(int(l))
    elif a and a[0] == "nm":
        sweep_nightmare(int(a[1]) if len(a) > 1 else 0)
    elif a and a[0] == "table":
        table()
    elif a and a[0] == "nmtable":
        nmtable()
    elif a and a[0] == "plan":
        print(plan_arg())
    elif a and a[0] == "record":
        for l in (a[1:] or LEVELS):
            record(int(l))
    elif a and a[0] == "regress":
        sys.exit(1 if regress([int(l) for l in a[1:]] or None) else 0)
    elif a and a[0] == "campaign":
        campaign_start(*a[1:])
    elif a and a[0] == "status":
        print(status())
    else:
        print(__doc__)
