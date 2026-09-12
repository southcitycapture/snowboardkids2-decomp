/* Self-play for Snowboard Kids 2: --racedbg, --peek, --autoplay, --soak,
 * --nightmare, --trial.
 *
 * The first game's race_dbg.c reached into globals (gRacePlayers, gRaceCourseIndex)
 * that the sequel does not have. Everything here is reached through the *task
 * scheduler* instead, because the sequel keeps every screen's state in the
 * scheduler's own allocation:
 *
 *   gSchedulerListSentinel.next        priority-ordered list of TaskScheduler
 *   TaskScheduler.allocatedState       that screen's state struct
 *   TaskScheduler.gamestateHandler     what it will run next (named with dladdr)
 *
 * The race is the scheduler whose renderContext is 0x37: `setRenderContext(0x37)`
 * in initRace (src/race/race_session.c) is the *only* call to setRenderContext
 * in the whole game, which makes it an unambiguous tag. Its allocation is the
 * GameState of include/gamestate.h, and `players` / `numPlayers` in it are the
 * race's riders.
 */
#include "../ultra/ultra.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include "gamestate.h"
#include "race/hit_reactions.h"
#include "system/task_scheduler.h"
#include "graphics/displaylist.h"
#include "data/course_data.h"
#include "../platform/input.h"

/* race/race_session.h drags in half the graphics headers; the four enum values
 * this file needs are copied instead (enum RaceType / enum GameMode there). */
#define RACE_TYPE_DEMO 10
#define RACE_TYPE_INTRO 11
#define GAME_MODE_DEMO 2
#define GAME_MODE_INTRO 3

extern TaskScheduler gSchedulerListSentinel;
extern GameSessionContext *gGameSessionContext;

/* src/race/character_stats.c, resident and pinned. */
typedef struct {
    u8 maxSpeed;
    u8 handling;
    u8 cornering;
    u8 lateralDeadzone;
    u8 gravity;
    u8 acceleration;
} SbkSnowboardStats;
extern SbkSnowboardStats gSnowboardStatsTable[SNOWBOARD_COUNT][9];

/* race_main.c lives in the .race overlay, so gAIPlayerParams is a plain native
 * symbol (the overlay region is pin-skipped and only *text* symbols are
 * renamed). One overlay defines it, so the name is unambiguous. */

#define RACE_RENDER_CONTEXT 0x37
#define SCHEDULER_STATE_RUNNING 1
/* race_main.c ~5190: the rider has crossed the line (and its input is cut). */
#define PLAYER_FINISHED_FLAG 0x80000

int sbk_race_debug_enabled;
/* --pintrace: the pin autopsy below. It is a diagnostic, not a gauge -- a
 * campaign whose rivals stall writes about 1,200 lines a minute of it -- so it
 * is off unless asked for, and --racedbg alone stays readable. */
int sbk_pin_trace;
int sbk_autoplay;   /* --autoplay: player 1 driven by the game's own CPU rider */
int sbk_soak;       /* --soak: keep confirming through the menus between races */
int sbk_nightmare;  /* --nightmare: a difficulty row above the hardest */
int sbk_dumpon;
int sbk_status;
int sbk_course_trace;
/* 1/256ths added to player 1's top speed, raised by the navigator every time the
 * campaign loses the same course again (menu_nav.c). A campaign cannot finish a
 * course the CPU rider is simply not fast enough to win, and an unattended run
 * that re-races a lost course with identical settings re-loses it: this is the
 * one lever that makes the retry different. It is folded in by trial_retune,
 * which is applyCharacterSnowboardStats with the boost in it, so nothing but
 * player 1's own baseMaxSpeed changes -- the rivals, the items and the course
 * are untouched. */
int sbk_campaign_boost;
/* --trial relief=/tax=: the trial owns the rival levers, the ladder does not. */
int sbk_trial_pins_levers;

/* ------------------------------------------------------------------ anchors */

GameState *sbk_race_state(void) {
    TaskScheduler *s = gSchedulerListSentinel.next;
    while (s != NULL) {
        if (s->renderContext == (u8)RACE_RENDER_CONTEXT && s->schedulerState == SCHEDULER_STATE_RUNNING &&
            s->allocatedState != NULL) {
            GameState *gs = (GameState *)s->allocatedState;
            if (gs->players != NULL && gs->numPlayers >= 1 && gs->numPlayers <= 4) return gs;
        }
        s = s->next;
    }
    return NULL;
}

/* The race scheduler's free-task counters. scheduleTask (task_scheduler.c:350)
 * hands out a node only while counters[nodeType] is non-zero, so this is the
 * gauge the chairlift wedge lives on. -1 when there is no race. */
int sbk_race_pool(int nodeType) {
    TaskScheduler *s = gSchedulerListSentinel.next;
    if (nodeType < 0 || nodeType > 7) return -1;
    while (s != NULL) {
        if (s->renderContext == (u8)RACE_RENDER_CONTEXT && s->schedulerState == SCHEDULER_STATE_RUNNING &&
            s->allocatedState != NULL)
            return (int)s->counters[nodeType];
        s = s->next;
    }
    return -1;
}

/* A demo/attract/intro race must be left alone: its riders replay a recorded
 * input stream and handing one to the CPU desynchronises the whole thing. */
int sbk_race_is_demo(const GameState *gs) {
    if (gs == NULL) return 1;
    if (gs->raceType == RACE_TYPE_DEMO || gs->raceType == RACE_TYPE_INTRO) return 1;
    if (gGameSessionContext != NULL &&
        (gGameSessionContext->gameMode == GAME_MODE_DEMO || gGameSessionContext->gameMode == GAME_MODE_INTRO))
        return 1;
    if (gs->players[0].inputPlaybackMode != 0) return 1;
    return 0;
}

/* ------------------------------------------------------------- the nightmare
 *
 * The sequel's CPU riders are tuned by gAIPlayerParams[difficulty][item]:
 * 8 difficulty rows of 17 entries (one per item plus row 0, which is the
 * rider's *own* handicap). Two things read it:
 *
 *   race_main.c ~804   maxSpeedCap -= gAIPlayerParams[d][0].useChance * 0x202
 *   hit_reactions.c    per-item use delay / chance / alt chance
 *
 * so row 0's useChance is a top-speed *tax* (0xA8 on the easiest row costs
 * about a quarter of the speed) and every other row's delay/useChance decide
 * how fast and how often an item is thrown.
 *
 * The obvious row -- no tax, delay 0, every chance 255 -- is the wrong one, and
 * not by a little: it is the row that stops races ending. Items are scheduled
 * tasks, `spawnChairliftEffect` (particle_items.c:2221) is the only way out of
 * the lift wait that wraps a lap, and it is a `scheduleTask` that returns NULL
 * when the pool is full. Spam every item on every rider and the last one to
 * reach the lift waits for ever, pinned at storedPosition. docs/nightmare-row.md
 * has the whole chain and the sweep that measured it.
 *
 * So the defaults below are searched, not guessed
 * (`nightmare_search.py nm 0`), and they sit inside the shape the game's own
 * eight rows use -- whose item `delay` never goes below 90, and averages 183
 * even on the hardest. On Sunny Mountain, tax=0/delay=120/use=205 comes first
 * in 14,106 frames; use=255 at the same delay does not finish at all, delay=150
 * drops to second and delay=90 to third.
 *
 * The game only ever uses rows named by gCpuCharacterSnowboardConfigs, and
 * those are 0..5; row 7 is spare, so the retune writes there and the riders
 * that should be terrifying are pointed at it. Nothing in the game's own data
 * is touched.
 */
#define NIGHTMARE_ROW 7
/* The rivals' own row.
 *
 * Under --autoplay player 1 is a CPU rider too, so it pays the row-0 speed tax
 * along with everyone else -- which makes a shared row useless as a difficulty
 * lever: taxing the rivals taxed us by exactly as much. Row 6 is the second
 * spare (gCpuCharacterSnowboardConfigs names only 0..5) and is written as a
 * copy of the Nightmare row with one byte changed, the tax. The rivals are
 * pointed at it and player 1 stays on the untaxed row 7, so sbk_rival_tax is a
 * lever that slows *only* the opposition.
 *
 * This is the lever the campaign needed. Raising player 1's own top speed
 * instead works for one step and then wedges the rider: at +21% it overshot
 * something on course 1 and the race never ended (46,000 retraces against a
 * normal 20,000), because a standard race only finishes when the *human* slot's
 * rider crosses the line. Slowing the rivals changes no physics on our side at
 * all. */
#define RIVAL_ROW 6
static int nightmare_written;
/* Tunable by --trial nm*: searched, not guessed (port/tools/nightmare_search.py). */
static int nm_tax = 0, nm_delay = 120, nm_use = 205, nm_alt = 205;
/* Added to the rivals' row-0 tax by the navigator's handicap ladder. */
int sbk_rival_tax;
/* Taken off every item's useChance, on both rows, when a course keeps wedging.
 *
 * docs/nightmare-row.md has the chain: a lap wraps at the chairlift, the only
 * way out of the lift wait is spawnChairliftEffect, and that is a scheduleTask
 * that returns NULL once the task pool is full -- which item spam fills. The
 * searched row (use=205) was measured on one Sunny Mountain race; course 1
 * wedges at it, at exactly the documented signature (lap 0, a fixed sector, the
 * position byte-identical frame to frame). Backing the item chance off is the
 * lever that attacks the cause rather than the symptom, and it is only reached
 * after the watchdog has had to rescue the same course twice. */
int sbk_item_relief;
/* Taken off every item's chances on the RIVAL row only -- the handicap that
 * sbk_item_relief above is not.
 *
 * hit_reactions.c reads gAIPlayerParams[player->aiDifficultyIndex][...], i.e.
 * each rider's *own* row, and this port puts player 1 on NIGHTMARE_ROW and the
 * rivals on RIVAL_ROW. sbk_item_relief writes both, so it disarms our rider by
 * exactly as much as theirs: it is a remedy for the task-pool wedge, not a
 * difficulty lever, and the campaign found that out the hard way. Course 1 at
 * relief 100 came *third*, worse than the 2nd it managed with no handicap at
 * all, because a no-items race is a race our rider is worse at.
 *
 * This one writes RIVAL_ROW alone. It is the cleanest handicap in the port:
 * every rider's speed, handling and cornering are untouched, our rider keeps a
 * full item set, and it lowers total pool pressure rather than raising it --
 * so unlike the tax it cannot buy the chairlift wedge it is meant to avoid. */
int sbk_rival_item_relief;

static void nightmare_write_row(void) {
    int i;
    int rival = nm_tax + sbk_rival_tax;
    int use = nm_use - sbk_item_relief;
    int rival_use, rival_alt;
    if (rival > 255) rival = 255;
    if (use < 40) use = 40;
    rival_use = use - sbk_rival_item_relief;
    rival_alt = nm_alt - sbk_rival_item_relief;
    if (rival_use < 40) rival_use = 40;
    if (rival_alt < 40) rival_alt = 40;
    gAIPlayerParams[NIGHTMARE_ROW][0].useChance = (u8)nm_tax;
    gAIPlayerParams[NIGHTMARE_ROW][0].delay = (u8)nm_delay;
    gAIPlayerParams[NIGHTMARE_ROW][0].altChance = (u8)nm_alt;
    gAIPlayerParams[RIVAL_ROW][0].useChance = (u8)rival;
    gAIPlayerParams[RIVAL_ROW][0].delay = (u8)nm_delay;
    gAIPlayerParams[RIVAL_ROW][0].altChance = (u8)nm_alt;
    for (i = 1; i < 0x11; i++) {
        gAIPlayerParams[NIGHTMARE_ROW][i].useChance = (u8)use;
        gAIPlayerParams[NIGHTMARE_ROW][i].delay = (u8)nm_delay;
        gAIPlayerParams[NIGHTMARE_ROW][i].altChance = (u8)nm_alt;
        gAIPlayerParams[RIVAL_ROW][i] = gAIPlayerParams[NIGHTMARE_ROW][i];
        gAIPlayerParams[RIVAL_ROW][i].useChance = (u8)rival_use;
        gAIPlayerParams[RIVAL_ROW][i].altChance = (u8)rival_alt;
    }
    if (!nightmare_written) {
        nightmare_written = 1;
        printf("sbk: nightmare: gAIPlayerParams row %d retuned tax=%d delay=%d use=%d alt=%d; "
               "rivals on row %d at tax=%d use=%d alt=%d\n",
               NIGHTMARE_ROW, nm_tax, nm_delay, use, nm_alt, RIVAL_ROW, rival, rival_use, rival_alt);
        fflush(stdout);
    }
}

/* ------------------------------------------------------------------- --trial
 *
 * --trial char=N,board=N,boost=N,nm=1,quit=1,level=N: one race experiment.
 * `board` is a SnowboardId (0..17), `boost` is 1/256ths added to the rider's
 * top speed. The setup is applied at the autoplay handoff, which is the first
 * frame the race allocation exists and the rider's own stats have been
 * applied; one result line is printed when player 1 finishes.
 */
static struct {
    int on, chr, board, boost, quit, level, nm, gold;
} trial = { 0, -1, -1, 0, 0, -1, -1, -1 };

/* --trial pathslot=N: which slot of the borrowed path table to lend player 1.
 * -1 (the default) means "the lender's own", which is the only kind the game
 * itself ever drives. See path_table_attach() below for why that matters. */
static int trial_pathslot = -1;

static unsigned long trial_start, trial_frames;
static int trial_gold0, trial_done;

/* The four extra retune levers; see trial_retune below for what each is for
 * and why the boost lever alone was never going to pass either Cross game. */
int sbk_boost_accel;    /* --trial accel=N   baseAcceleration */
int sbk_boost_hand;     /* --trial hand=N    handling: the turn rate */
int sbk_boost_corner;   /* --trial corner=N  cornering: the drag a turn costs */
int sbk_boost_dead;     /* --trial dead=N    lateralDeadzone */
int sbk_boost_grav;     /* --trial grav=N    baseGravity (negative = floatier) */

int sbk_trial_parse(const char *spec) {
    const char *p = spec;
    trial.on = 1;
    while (*p) {
        char key[16];
        int val;
        if (sscanf(p, "%15[a-z]=%d", key, &val) == 2) {   /* %d takes a leading '-' */
            if (!strcmp(key, "char")) trial.chr = val;
            else if (!strcmp(key, "board")) trial.board = val;
            else if (!strcmp(key, "boost")) trial.boost = val;
            else if (!strcmp(key, "quit")) trial.quit = val;
            else if (!strcmp(key, "level")) {
                extern int sbk_nav_target_level;
                trial.level = val;
                sbk_nav_target_level = val;
            }
            else if (!strcmp(key, "nm")) trial.nm = val;
            else if (!strcmp(key, "gold")) trial.gold = val;
            else if (!strcmp(key, "nmtax")) nm_tax = val;
            else if (!strcmp(key, "nmdelay")) nm_delay = val;
            else if (!strcmp(key, "nmuse")) nm_use = val;
            else if (!strcmp(key, "nmalt")) nm_alt = val;
            else if (!strcmp(key, "pathslot")) trial_pathslot = val;
            else if (!strcmp(key, "accel")) sbk_boost_accel = val;
            else if (!strcmp(key, "corner")) sbk_boost_corner = val;
            else if (!strcmp(key, "hand")) sbk_boost_hand = val;
            else if (!strcmp(key, "dead")) sbk_boost_dead = val;
            else if (!strcmp(key, "grav")) sbk_boost_grav = val;
            else if (!strcmp(key, "tflags")) { extern int sbk_trick_flags; sbk_trick_flags = val; }
            else if (!strcmp(key, "ttarget")) { extern int sbk_trick_target; sbk_trick_target = val; }
            /* The two handicap-ladder levers the navigator drives, so a trial
             * can reproduce a campaign rung exactly instead of approximating
             * it. Setting them is not enough on its own: nav_level_begin runs
             * off the course list and writes the ladder's own values over the
             * top, which is why the first relief=165 trial came back
             * byte-identical to relief=0 -- the same frames, gold and wall
             * counts, which is not what a real difference looks like. So a
             * spec that names either lever pins both against the ladder. */
            else if (!strcmp(key, "relief")) { sbk_rival_item_relief = val; sbk_trial_pins_levers = 1; }
            else if (!strcmp(key, "tax")) { sbk_rival_tax = val; sbk_trial_pins_levers = 1; }
        }
        while (*p && *p != ' ' && *p != ',') p++;
        while (*p == ' ' || *p == ',') p++;
    }
    sbk_autoplay = 1;
    return 0;
}

/* --plan LEVEL:CHAR:BOARD:BOOST,... : the rider's book, one row per course. */
#define PLAN_MAX 16
static struct { int level, chr, board, boost; } plan[PLAN_MAX];
static int nplan;

int sbk_plan_parse(const char *spec) {
    const char *p = spec;
    while (*p && nplan < PLAN_MAX) {
        int c, ch, b, bo;
        if (sscanf(p, "%d:%d:%d:%d", &c, &ch, &b, &bo) == 4) {
            plan[nplan].level = c;
            plan[nplan].chr = ch;
            plan[nplan].board = b;
            plan[nplan].boost = bo;
            nplan++;
        }
        while (*p && *p != ',') p++;
        while (*p == ',') p++;
    }
    printf("sbk: plan: %d level rows\n", nplan);
    return nplan;
}

static void plan_apply(int level) {
    int i;
    for (i = 0; i < nplan; i++) {
        if (plan[i].level != level) continue;
        trial.chr = plan[i].chr;
        trial.board = plan[i].board;
        trial.boost = plan[i].boost;
        printf("sbk: plan: level %d -> char=%d board=%d boost=%d\n", level, trial.chr, trial.board, trial.boost);
        return;
    }
}

/* applyCharacterSnowboardStats (src/race/character_stats.c), port side, with
 * the boost folded into the top speed. Recomputed here rather than called: the
 * game's own version reads getCurrentAllocation(), and gActiveScheduler points
 * at whatever the last dispatch left when the host loop runs. */
/* The other three stats, which the boost lever never touched.
 *
 * `boost` is a 1/256th multiplier on the rider's top speed, and for eight
 * courses that was the whole handicap. It is the wrong lever for both Cross
 * games and the measurements say so in two different ways.
 *
 * Speed Cross: at +112% the rider reads `spd=1572864/2985638` -- it is running
 * at half a cap it never approaches, so raising the cap again buys nothing.
 * What ninety seconds of Snowboard Street wants is a rider that gets back up
 * to speed out of a corner, which is `baseAcceleration`, and one that does not
 * scrub speed going round it, which is `cornering` and `lateralDeadzone`.
 *
 * X Cross: `beginPostTrickLaunchStep` builds the jump out of
 * `unkB8C + baseAcceleration` (race_main.c ~1911), and unkB8C is a fixed
 * 0x10000 for a CPU rider. So the *only* thing that makes a CPU's ollie bigger
 * is its acceleration stat -- and X Cross is lost because the ollie is too
 * small to leave the ground at all. One lever, both walls.
 *
 * Each is the same shape as `boost`: 1/256ths added to the stat the game's own
 * table computed, so zero is exactly the old behaviour. */
static s32 stat_boost(s32 v, int pct) {
    if (pct == 0) return v;
    return v + (s32)(((long long)v * pct) >> 8);
}

static s32 stat_clamp8(s32 v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void trial_retune(Player *p, int boost) {
    const SbkSnowboardStats *s = &gSnowboardStatsTable[p->snowboardId % SNOWBOARD_COUNT][p->characterId % 9];
    s32 top = (s32)(s->maxSpeed * 353894 / 100 + 0xEB333);
    top += (s32)(((long long)top * boost) >> 8);
    p->baseMaxSpeed = top;
    p->maxSpeedCap = top;
    /* handling and cornering are single bytes (gamestate.h 0xAC0/0xAC1), so
     * the boost has to be clamped rather than allowed to wrap: a "+300%"
     * cornering that came out as 44 would be a *worse* rider reported as a
     * better one, and that is exactly the kind of silent inversion the boost
     * ladder already cost this port two days over. */
    /* These two do not pull the same way and one lever for both was a mistake
     * worth writing down. `handling` (race_main.c:1392, steeringAngle/2 *
     * handling / 125) is the turn *rate*, so more is a rider that gets round;
     * `cornering` (1401) is the *drag* the turn costs, so more is a rider
     * that scrubs off speed doing it. A single corner= lever that raised both
     * took Speed Cross's wall contacts from 30 to 6 and left the finish
     * exactly where it was, because it bought the line and paid for it in
     * speed. They are separate now, and both take a negative. */
    p->handling = (u8)stat_clamp8(stat_boost(s->handling + 0x19, sbk_boost_hand));
    p->cornering = (u8)stat_clamp8(stat_boost(s->cornering + 1, sbk_boost_corner));
    p->lateralDeadzone = stat_boost((s->lateralDeadzone << 15) / 100 + 0x1000, sbk_boost_dead);
    p->baseGravity = stat_boost((s->gravity << 14) / 100 + 0x3000, sbk_boost_grav);
    p->baseAcceleration = stat_boost((s->acceleration << 17) / 100 + 0x28000, sbk_boost_accel);
}

/* ...and the retune has to be *held*, because the game undoes it.
 *
 * `initPlayer` (race_main.c ~1061) ends with its own call to
 * applyCharacterSnowboardStats, and initPlayer runs from
 * waitForFadeAndInitPlayers -- which is queued by initRace *after* the
 * setRenderContext(0x37) the autoplay handoff hangs off. So the six fields
 * autoplay_arm writes are recomputed from the stats table a second or two
 * later, with no boost in them, every single race.
 *
 * The boost lever therefore never reached a race. The handicap ladder spent
 * six attempts on course 8 climbing rungs 1..5 -- +11%, +16%, +21% -- and the
 * rider raced every one of them at exactly the same speed, which is why the
 * places came back 4th, 3rd, 2nd, 2nd, 3rd, 2nd with no trend: they were six
 * samples of one experiment. The log said so all along and nobody read it
 * against the race: `sbk: autoplay: rider 0: ... top=1394607` at the handoff,
 * and `spd=.../1257111` a minute later in --racedbg -- 1257111 * (1 + 28/256)
 * = 1394608, the boost exactly undone.
 *
 * Nothing else writes baseMaxSpeed (race_main.c:802 only copies it into
 * maxSpeedCap each frame; the two boss levels derive their own from it), so
 * holding it is a one-line test per tick: if the field is not what the retune
 * left, the game has recomputed it, and it is recomputed again. Held rather
 * than hooked because the write is inside a game source file, and everything
 * that changes game behaviour is supposed to be one line in patches.txt or
 * nothing at all. */
static int retune_boost = -1;   /* the boost the current race was armed with */
static s32 retune_top;          /* what baseMaxSpeed should read all race */
static s32 retune_accel;        /* ...and what baseAcceleration should read */

/* Watching baseMaxSpeed alone was enough while boost was the only lever, and
 * became wrong the moment there were others: a trial with `accel=1024` and no
 * boost leaves baseMaxSpeed at exactly the value applyCharacterSnowboardStats
 * recomputes, so the hold saw nothing to do and the acceleration lever was
 * silently undone a second into every race. The evidence was a run that came
 * back byte-identical to the unlevered one -- same 5,222 frames, same 716
 * airborne retraces, same 92 jumps -- which is what "the lever never reached
 * the race" looks like, and this port has now been told that twice. */
static void retune_hold(Player *p) {
    s32 was;
    if (retune_boost < 0) return;
    if (p->baseMaxSpeed == retune_top && p->baseAcceleration == retune_accel) return;
    was = p->baseMaxSpeed;
    trial_retune(p, retune_boost);
    printf("sbk: autoplay: the game recomputed the rider's stats; retune re-applied "
           "(top %d -> %d, accel %d, boost=%d = +%d%%)\n",
           (int)was, (int)p->baseMaxSpeed, (int)p->baseAcceleration, retune_boost, retune_boost * 100 / 256);
    fflush(stdout);
}

/* Remember what the retune left, so retune_hold can tell "the game undid it"
 * from "nobody has armed a race yet". */
static void retune_arm(Player *p, int boost) {
    trial_retune(p, boost);
    retune_boost = boost;
    retune_top = p->baseMaxSpeed;
    retune_accel = p->baseAcceleration;
}


/* -------------------------------------------------- the boss-race lead gauge
 *
 * This was written as a *brake*, on the reading that the rider kept
 * overtaking the Ice Land boss and sailing out of the star's reach. It never
 * fired once, and the gauge it printed while not firing is the most useful
 * thing to come out of course 11.
 *
 *   sbk-brake: r=900  lead=-341 (p prog 6623 sect 26 | boss prog 6282 sect 30)
 *   sbk-brake: r=1800 lead=-492 (p prog 4745 sect 52 | boss prog 4253 sect 59)
 *   sbk-brake: r=3600 lead=-652 (p prog 2069 sect 99 | boss prog 1417 sect 112)
 *
 * The boss is ahead on every single sample and the gap **grows all race**,
 * from 150 units of lapProgressRemaining to 650. The rider never overtakes
 * anything. (The earlier reading of "neck and neck, the lead changing hands
 * six or seven times" was lap-wrapped samples from two different laps lined
 * up against each other -- see docs/PLAN.md, which now says so.)
 *
 * And the rider is 43% faster in a straight line while it loses that ground:
 * 1,532,104 against the boss's 1,072,168, with `wall=14,0,0,0` -- fourteen
 * wall contacts to the boss's none. The boss drives a scripted path and
 * spends the race shooting guided stars at us
 * (iceLandBossChaseAttackPhase -> spawnPlayerGuidedStarProjectile). So the
 * thing standing between four heads and thirteen is not the pilot's aim, not
 * its ammunition and not its trigger: it is that our rider takes a worse line
 * than the boss and gets knocked off it, and no amount of top speed has ever
 * fixed that on this port -- courses 8, 9 and 10 all said the same.
 *
 * The action is gone and the gauge is kept, behind --racedbg, because the
 * next attempt at this course needs the number and not the guess.
 */
int sbk_boss_lead_gauge = 1;

static void boss_brake_tick(GameState *gs, Player *p) {
    extern int sbk_is_hp_boss_race(int);
    extern Player *sbk_boss_rider(GameState *);
    Player *boss;
    int lead;

    if (!sbk_boss_lead_gauge || !sbk_race_debug_enabled) return;
    if (gs == NULL || p == NULL || !sbk_is_hp_boss_race(gs->raceType)) return;
    boss = sbk_boss_rider(gs);
    if (boss == NULL || (boss->animationFlags & 0x100000)) return;
    if (p->animationFlags & 0x80000) return;
    if ((gs->raceFrameCounter % 300) != 0) return;

    /* lapProgressRemaining counts down, so the rider with less of it is in
     * front; a positive lead would mean the rider is ahead of the boss.
     * Across a lap boundary the two numbers are not comparable at all. */
    lead = (p->currentLap != boss->currentLap)
               ? 0
               : (int)boss->lapProgressRemaining - (int)p->lapProgressRemaining;
    printf("sbk-bosslead: r=%d lead=%d (p lap %d prog %d sect %d | boss lap %d prog %d sect %d)\n",
           (int)gs->raceFrameCounter, lead, (int)p->currentLap, (int)p->lapProgressRemaining,
           (int)p->sectorIndex, (int)boss->currentLap, (int)boss->lapProgressRemaining,
           (int)boss->sectorIndex);
    fflush(stdout);
}

/* ----------------------------------------------------------- the race watchdog
 *
 * A standard race ends when every *human* slot's rider has the finished flag
 * (race_session.c ~1135: `count == gs->playerCount` over animationFlags &
 * 0x80000), and in story mode that is player 1 alone. So a wedged player 1 is
 * not a slow race, it is a race that never ends -- and an unattended campaign
 * behind it never moves again. It happened at the third course-1 retry: 46,000
 * retraces against a normal 20,000, the rider pinned in place.
 *
 * docs/nightmare-row.md has the mechanism for the first one found: a lap wraps
 * at the chairlift, the only way out of the lift wait is spawnChairliftEffect,
 * and that is a scheduleTask returning NULL once the task pool is full.
 *
 * The watchdog does not try to diagnose which wedge it is. When the rider stops
 * getting anywhere for forty seconds it ends the race the game's own way --
 * finishPosition last, then the finished flag -- so the result comes back as a
 * 4 (raced, not won), the course keeps its "go back for this one" marker, and
 * the campaign carries on. Forcing the flag *without* first forcing the place
 * would be a save-corrupting bug: a finishPosition that happened to be 0 would
 * write the course down as won.
 *
 * "Stops getting anywhere" used to mean a byte-identical lap, sector and world
 * position, which is what a rider held at the lift looks like -- and only that.
 * The second wedge found on Starlight Highway was a rider at full speed with
 * `lap=2 prog=511 sect=138` frozen for two thousand retraces while its position
 * jittered by a few hundred thousandths of a unit a frame: driving into
 * geometry, not held by the game. Byte-equality could not see it, the race
 * never ended, and the campaign sat there until somebody looked.
 *
 * So the test is progress, not stillness: `currentLap` and
 * `lapProgressRemaining` are what the game itself ranks riders by, and a rider
 * that has not improved either of them in forty seconds is stuck whatever its
 * position is doing. That covers the lift (pinned, no progress) and the wall
 * (moving, no progress) with one rule.
 */
/* How many retraces each rider has spent in contact with a track wall.
 *
 * race_main.c:5355 sets animationFlags 0x10 on any frame where
 * handlePlayerTrackWallCollision moved the rider, so it is the game's own
 * answer to "am I scraping something", and comparing our rider's count with
 * the rivals' is how "the CPU rider is off the racing line" stops being a
 * guess. On Starlight Highway our rider crawled sectors 104-115 at half its
 * speed cap with 0x10 set on every sample while the rivals sailed past. */
/* "Is this a different race from the one I was watching?"
 *
 * The pointer alone is not enough and this cost three campaign races: the
 * scheduler hands the next race the *same* allocation, so `gs` compares equal
 * across races and every per-race counter in this file carried over. The
 * marshal's stall clock came into a fresh race already sixty seconds old and
 * carried the rider at sector 33; the wall counters printed 3444 for a rider
 * that had raced for nine seconds. The race's own frame counter going
 * backwards is the other half of the test. */
static int race_is_new(GameState *gs, void **seen, unsigned *last_frame) {
    unsigned f = (unsigned)gs->raceFrameCounter;
    int fresh = (*seen != (void *)gs) || f < *last_frame;
    *seen = (void *)gs;
    *last_frame = f;
    return fresh;
}

int sbk_wall_frames[4];
static void *wall_gs;
static unsigned wall_frame;

static void wall_watch(GameState *gs) {
    int i;
    if (race_is_new(gs, &wall_gs, &wall_frame)) {
        for (i = 0; i < 4; i++) sbk_wall_frames[i] = 0;
    }
    for (i = 0; i < gs->numPlayers && i < 4; i++) {
        if (gs->players[i].animationFlags & 0x10) sbk_wall_frames[i]++;
    }
}

int sbk_campaign_wedge; /* set here, consumed by menu_nav.c's handicap ladder */
#define WEDGE_RETRACES 2400

static void race_watchdog(GameState *gs, unsigned long retraces) {
    static unsigned long since;
    static s32 last[5];
    static s32 last_prog;
    static void *last_gs;
    Player *p = &gs->players[0];
    s32 now[5];
    extern int sbk_menu_on(const char *);

    if (!sbk_autoplay || !sbk_menu_on("handleRaceStateUpdate")) {
        last_gs = NULL;
        return;
    }
    now[0] = p->currentLap;
    now[1] = p->sectorIndex;
    now[2] = (s32)p->worldPos.x;
    now[3] = (s32)p->worldPos.y;
    now[4] = (s32)p->worldPos.z;

    /* Progress, the way the game ranks it: a later lap, or less of this lap
     * left. Anything else -- a new sector entered sideways, a position that
     * jitters against a wall -- is not progress and must not restart the
     * clock. */
    if (last_gs != (void *)gs) {
        last_gs = (void *)gs;
        last[0] = now[0];
        last_prog = p->lapProgressRemaining;
        since = retraces;
        return;
    }
    if (now[0] > last[0] || (now[0] == last[0] && p->lapProgressRemaining < last_prog)) {
        last[0] = now[0];
        last_prog = p->lapProgressRemaining;
        since = retraces;
        return;
    }
    if (retraces - since < WEDGE_RETRACES) return;
    since = retraces;

    printf("sbk: WEDGE -- player 1 has got nowhere for %d retraces on level %d "
           "(lap=%d prog=%d sect=%d pos=%d,%d,%d anim=%08x spd=%d pool=%d); ending the race as a loss\n",
           WEDGE_RETRACES, gs->memoryPoolId, p->currentLap, (int)p->lapProgressRemaining, p->sectorIndex,
           (int)now[2], (int)now[3], (int)now[4], (unsigned)p->animationFlags, (int)p->smoothedSpeedCap,
           sbk_race_pool(0));
    fflush(stdout);
    /* Last place first, THEN the finished flag: a race that was never finished
     * must never be recorded as won. */
    p->finishPosition = (u8)(gs->numPlayers > 0 ? gs->numPlayers - 1 : 3);
    p->animationFlags |= PLAYER_FINISHED_FLAG;
    sbk_campaign_wedge = 1;
}

/* ------------------------------------------------------------------ autoplay */

/* ------------------------------------------------- the borrowed path table
 *
 * The CPU steering wants the course's path-preference table. Despite the
 * decomp name, `bossRaceData` is not a boss thing: loadPlayerCharacterAssets
 * (race_main.c ~6079) hands every rider the game itself made a CPU
 * `gBossHudAssetTable[memoryPoolId]` -- the *course's* AI data -- and
 * race_session.c ~436 makes every racer above `activePlayerCount` a CPU on
 * every course, boss or not. So in a one-human story race riders 2..4 have it
 * and player 1 does not.
 *
 * The asset begins with one s32 byte-offset per playerIndex and race_main.c
 * ~1105 reads `bossRaceData + offsets[playerIndex]`. The offsets are read back
 * and logged at the attach (`sbk: autoplay: path slot N:`), and on Turtle
 * Island they are 16,16,504,992 -- **slot 0 and slot 1 are the same table**.
 * Three authored sets for four indices, with the human's index aliased onto
 * rider 2's. So "slot 0 is an empty set" was the wrong guess: lending slot 0
 * and lending slot 1 hand over the identical bytes, and neither is the wedge.
 * The slot is still taken from the lender rather than hard-coded, because a
 * course whose header does differ should follow the rider that demonstrably
 * gets round; `--trial pathslot=N` overrides for experiments. Do NOT set
 * players[0].bossRaceData -- that pointer is freed per rider. */
static void path_table_attach(GameState *gs, Player *p, int verbose) {
    const s32 *off;
    void *d;
    int slot;

    if (p->aiPathData != NULL || gs->numPlayers < 2) return;
    d = gs->players[1].bossRaceData;
    if (d == NULL) return;

    slot = trial_pathslot >= 0 ? trial_pathslot : (int)gs->players[1].playerIndex;
    if (slot < 0 || slot > 3) slot = 1;
    off = (const s32 *)d;
    /* An empty slot is still possible (a course whose table is shorter than
     * four sets): an offset of 0 would alias the header itself. Fall back. */
    if (off[slot] <= 0) slot = 1;
    if (off[slot] <= 0) return;

    p->aiPathData = (void *)((s32)d + off[slot]);
    if (verbose) {
        int i, k;
        printf("sbk: autoplay: path table attached (%p) asset=%p slot=%d offsets=%ld,%ld,%ld,%ld\n", p->aiPathData, d, slot,
               (long)off[0], (long)off[1], (long)off[2], (long)off[3]);
        /* The evidence for the slot choice, so a log can be read back: the
         * first sectors of every slot. An all-zero row is an unauthored set. */
        for (i = 0; i < 4; i++) {
            const unsigned char *t;
            if (off[i] <= 0) continue;
            t = (const unsigned char *)d + off[i];
            printf("sbk: autoplay: path slot %d:", i);
            for (k = 0; k < 24; k++) printf("%s%02x", (k % 4) ? "" : " ", t[k]);
            printf("\n");
        }
        fflush(stdout);
    }
}

/* Every course's real raceType, learned at the handoff. menu_nav's boss ladder
 * reads it: a course's type cannot be guessed from its number (course 7 is
 * RACE_TYPE_BOSS_JUNGLE, course 6 is an ordinary race whose rivals all carry
 * isBossRacer), and it is the same for every visit to a course. -1 is unknown. */
int sbk_level_race_type[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

static void autoplay_arm(GameState *gs, unsigned long retraces) {
    Player *p = &gs->players[0];
    if (gs->memoryPoolId >= 0 && gs->memoryPoolId < 16) sbk_level_race_type[gs->memoryPoolId] = (int)gs->raceType;

    /* The ladder for *this* course, before the rider is retuned for it -- see
     * sbk_nav_arm_level. Not in a trial: the trial's levers are the
     * experiment. */
    if (!trial.on) {
        extern void sbk_nav_arm_level(int);
        sbk_nav_arm_level(gs->memoryPoolId);
    }

    p->isCpuControlled = 1;
    path_table_attach(gs, p, 0);
    p->aiDifficultyIndex = (u8)(sbk_nightmare ? NIGHTMARE_ROW : 0);

    if (trial.on) {
        plan_apply(gs->memoryPoolId);
        if (trial.chr >= 0) p->characterId = (u8)trial.chr;
        if (trial.board >= 0) p->snowboardId = (u8)trial.board;
        if (trial.nm >= 0) p->aiDifficultyIndex = (u8)(trial.nm ? NIGHTMARE_ROW : 0);
        retune_arm(p, trial.boost);
        if (trial.gold >= 0) p->raceGold = trial.gold;
        trial_start = retraces ? retraces : 1;
        trial_gold0 = p->raceGold;
        trial_done = 0;
        printf("sbk-trial: start r=%lu level=%d type=%d char=%d board=%d top=%d accel=%d cor=%d/%d dz=%d "
               "grv=%d diff=%d\n",
               retraces, gs->memoryPoolId, gs->raceType, p->characterId, p->snowboardId, (int)p->baseMaxSpeed,
               (int)p->baseAcceleration, (int)p->handling, (int)p->cornering, (int)p->lateralDeadzone,
               (int)p->baseGravity, p->aiDifficultyIndex);
    } else if (sbk_nightmare) {
        /* Amazing, not just aggressive: the best board in the game on the
         * rider's own character, and the speed tax taken off.
         *
         * This used to be SNOWBOARD_SPEED_LEVEL_3, described here as "the
         * fastest board that has no drawback". It is not. Read
         * gSnowboardStatsTable for Slash and the two boards sit like this:
         *
         *   board            spd  han  cor  dz  grv  acc
         *   SPEED_LEVEL_3     83   35   50  37   75   40
         *   STAR              83   60   40  65   75   70
         *
         * -- the same top speed, and STAR is better on every other axis. The
         * three that matter are the three the racing line is made of:
         * `handling` is the turn rate (race_main.c:1392,
         * `steeringAngle/2 * handling / 125`), `cornering` is the *drag* a turn
         * costs (1401, `cornering * turnRate^2 / turnRate`, so lower is
         * faster), and `lateralDeadzone` is how much sideways velocity is
         * killed each frame (applyVelocityDeadzone, 3170), so higher is less
         * sideslip. On SPEED_LEVEL_3 the rider had the *worst* handling and
         * the worst deadzone of any level-3 board -- and then the ladder put
         * up to +21% on top of its top speed. That is the overshoot course 8
         * kept showing: more boost, worse place, because the rider was leaving
         * the line rather than running out of speed.
         *
         * STAR is not a cheat board either: race_session.c:575 and :615 hand
         * it to the game's own riders, and nothing in hit_reactions.c or
         * particle_items.c gives it a special behaviour the way DRAGON,
         * HIGH_TECH, NINJA, RICH and POVERTY get one. It is pure stats. The
         * only thing >= SNOWBOARD_STAR changes is that race_main.c:6070 loads
         * no palette for it (segment3 = NULL), which is what the star board's
         * own texture expects. */
        p->snowboardId = SNOWBOARD_STAR;
        retune_arm(p, sbk_campaign_boost);
    } else if (sbk_campaign_boost != 0) {
        retune_arm(p, sbk_campaign_boost);
    }
    printf("sbk: autoplay: player 1 handed to the CPU rider (level=%d type=%d diff=%d boost=%d top=%d path=%p)\n",
           gs->memoryPoolId, gs->raceType, p->aiDifficultyIndex, sbk_campaign_boost, (int)p->baseMaxSpeed,
           p->aiPathData);
    /* Who else is in this race, and is the handicap reaching them?
     *
     * The campaign lost the Jingle Town boss four times running, at every rung
     * of the ladder, always by exactly one place and never earning a coin --
     * the signature of a handicap that is not being applied to anybody. The
     * levers only reach riders that race_dbg put on RIVAL_ROW, and it only
     * does that for isCpuControlled riders; a boss is flagged isBossRacer and
     * takes its own path through applyCharacterSnowboardStats. Printing the
     * roster once per race turns that from a guess into a line in the log. */
    {
        int i;
        for (i = 0; i < gs->numPlayers && i < 4; i++) {
            Player *q = &gs->players[i];
            printf("sbk: autoplay: rider %d: cpu=%d boss=%d diff=%d char=%d board=%d top=%d\n", i,
                   (int)q->isCpuControlled, (int)q->isBossRacer, (int)q->aiDifficultyIndex, (int)q->characterId,
                   (int)q->snowboardId, (int)q->baseMaxSpeed);
        }
    }
    fflush(stdout);
}

static void trial_tick(GameState *gs, unsigned long retraces) {
    Player *p;
    if (!trial.on || trial_done || gs == NULL) return;
    p = &gs->players[0];
    if (p->animationFlags & PLAYER_FINISHED_FLAG) {
        int i, ahead = 0;
        trial_done = 1;
        trial_frames = trial_start ? retraces - trial_start : 0;
        for (i = 1; i < gs->numPlayers; i++) {
            if (gs->players[i].animationFlags & PLAYER_FINISHED_FLAG) ahead++;
        }
        printf("sbk-trial: result level=%d char=%d board=%d boost=%d diff=%d pathslot=%d place=%d "
               "finished_before=%d frames=%lu gold=%d wall=%d,%d,%d,%d\n",
               gs->memoryPoolId, p->characterId, p->snowboardId, trial.boost, p->aiDifficultyIndex, trial_pathslot,
               p->finishPosition + 1, ahead, trial_frames, (int)(p->raceGold - trial_gold0), sbk_wall_frames[0],
               sbk_wall_frames[1], sbk_wall_frames[2], sbk_wall_frames[3]);
        fflush(stdout);
        if (trial.quit) {
            extern void sbk_request_quit_now(void);
            sbk_request_quit_now();
        }
    }
}

/* ------------------------------------------------------------------- --status */

static void status_tick(unsigned long retraces) {
    static int last_gold = -1;
    int gold;
    if (!sbk_status || gGameSessionContext == NULL) return;
    gold = (int)gGameSessionContext->gold;
    if (retraces % 300 != 0 && gold == last_gold) return;
    last_gold = gold;
    printf("sbk-status: r=%lu gold=%d mode=%d level=%d players=%d lap=%d slot=%d story=%d credits=%d\n", retraces,
           gold, gGameSessionContext->gameMode, gGameSessionContext->currentLevel, gGameSessionContext->numPlayers,
           gGameSessionContext->lapCount, gGameSessionContext->saveSlotIndex,
           gGameSessionContext->modeState.isStoryMode, gGameSessionContext->creditsCompleted);
    fflush(stdout);
}

/* ------------------------------------------------------------ pin autopsy
 *
 * --racedbg prints every 30th retrace, which is enough to say "this rider is
 * not getting anywhere" and useless for saying why: a rider held by a
 * per-frame positional constraint looks, at 30-frame sampling, like a rider
 * wandering. The autopsy is the other end of that: the first time any rider in
 * a race stops making progress for PIN_ARM retraces, it prints a census of
 * every task the race scheduler is running (by name, through the same cache
 * the navigator uses) and then PIN_BURST *consecutive* frames of that rider's
 * whole physical state. One burst per rider per race. */
#define PIN_ARM 300
#define PIN_BURST 150

static struct {
    void *gs;
    s16 best_prog[4];
    u8 best_lap[4];
    unsigned long since[4];
    int burst[4];  /* frames of dense trace still owed */
    int done[4];   /* already autopsied this rider this race */
    unsigned frame;
} pin;

/* Which task is standing on top of the rider?
 *
 * A scheduled task's payload is up to TASK_NODE_INLINE_PAYLOAD_SIZE bytes of
 * whatever struct the level wanted, and almost every placed thing in this game
 * keeps its world position in there as a Vec3i -- a RenderObject's
 * transform.translation, a sprite's position, a projectile's own worldPos. The
 * offset differs per struct and there is no table of them, so the census sweeps
 * the payload for *any* aligned s32 triple that lands within NEAR of the pinned
 * rider and prints the offset it found it at. On a course with a hundred
 * obstacles that would be noise; standing still at one spot with one thing
 * touching you, it is the answer. */
#define PIN_NEAR 0x400000

static void pin_census(GameState *gs, int who, unsigned long retraces) {
    TaskScheduler *s = gSchedulerListSentinel.next;
    Player *p = &gs->players[who];
    extern const char *sbk_fn_name(void *);
    while (s != NULL) {
        if (s->renderContext == (u8)RACE_RENDER_CONTEXT && s->allocatedState == (void *)gs) {
            Node *n = s->activeList;
            int k = 0;
            while (n != NULL && k < 96) {
                printf("sbk-pin: r=%lu p%d task[%d] pri=%d cb=%s payload=%p", retraces, who, k, (int)n->priority,
                       sbk_fn_name((void *)n->callback), (void *)&n->payload);
                /* scheduleTask returns `&newNode->payload`: the `payload`
                 * field IS the inline storage, not a pointer to it
                 * (task_scheduler.c:406). Reading it as a pointer and
                 * dereferencing it is a SIGSEGV, not a diagnosis. */
                {
                    const s32 *w = (const s32 *)&n->payload;
                    unsigned o;
                    for (o = 0; o + 3 <= TASK_NODE_INLINE_PAYLOAD_SIZE / 4; o++) {
                        s32 dx = w[o] - (s32)p->worldPos.x;
                        s32 dy = w[o + 1] - (s32)p->worldPos.y;
                        s32 dz = w[o + 2] - (s32)p->worldPos.z;
                        if (dx > -PIN_NEAR && dx < PIN_NEAR && dy > -PIN_NEAR && dy < PIN_NEAR && dz > -PIN_NEAR &&
                            dz < PIN_NEAR) {
                            printf(" NEAR+0x%02x d=%d,%d,%d", o * 4, (int)dx, (int)dy, (int)dz);
                        }
                    }
                }
                printf("\n");
                n = n->next;
                k++;
            }
            printf("sbk-pin: r=%lu p%d %d tasks on the race scheduler\n", retraces, who, k);
            break;
        }
        s = s->next;
    }
    /* The sector graph and every rider's own path-preference row around the
     * pin. `aiTarget` is computed by walking sectors[cur].next/left/right
     * under the rider's aiPathData (ai_pathfinding.c:97), so a target that
     * points backwards is either a graph link or a preference byte, and this
     * prints both -- for the riders that get through as well as the one that
     * does not. */
    {
        TrackData *td = &gs->gameData;
        int sec, j;
        int lo = (int)p->sectorIndex - 6, hi = (int)p->sectorIndex + 7;
        if (lo < 0) lo = 0;
        if (hi > (int)td->sectorCount) hi = (int)td->sectorCount;
        for (sec = lo; sec < hi; sec++) {
            TrackSector *t = &td->sectors[sec];
            printf("sbk-pin: r=%lu p%d sector[%d] next=%d prev=%d right=%d left=%d len=%u prog=%d", retraces, who,
                   sec, (int)t->nextSectorIndex, (int)t->previousSectorIndex, (int)t->rightSectorIndex,
                   (int)t->leftSectorIndex, (unsigned)t->segmentLength, (int)t->lapProgressRemaining);
            for (j = 0; j < gs->numPlayers && j < 4; j++) {
                const signed char *row = (const signed char *)gs->players[j].aiPathData;
                if (row == NULL) {
                    printf(" p%d=--", j);
                } else {
                    row += sec * 4;
                    printf(" p%d=%d/%u/%u/%d", j, (int)row[0], (unsigned)(unsigned char)row[1],
                           (unsigned)(unsigned char)row[2], (int)row[3]);
                }
            }
            printf("\n");
        }
    }
    {
        int i;
        for (i = 0; i < gs->numPlayers && i < 4; i++) {
            Player *q = &gs->players[i];
            printf("sbk-pin: r=%lu p%d rider%d pos=%d,%d,%d stored=%d,%d,%d cr=%d nspheres=%d d=%d,%d,%d\n", retraces,
                   who, i, (int)q->worldPos.x, (int)q->worldPos.y, (int)q->worldPos.z, (int)q->storedPosition.x,
                   (int)q->storedPosition.y, (int)q->storedPosition.z, (int)q->collisionRadius,
                   (int)q->collisionSphereCount, (int)(q->worldPos.x - p->worldPos.x),
                   (int)(q->worldPos.y - p->worldPos.y), (int)(q->worldPos.z - p->worldPos.z));
        }
    }
    fflush(stdout);
}

static void pin_frame(GameState *gs, int i, unsigned long retraces) {
    Player *p = &gs->players[i];
    printf("sbk-pin: r=%lu p%d sect=%d seg=%d prog=%d pos=%d,%d,%d d=%d,%d,%d vel=%d,%d,%d "
           "aim=%d,%d,%d rotY=%d steer=%d hit=%d kang=%d kvel=%d,%d,%d anim=%08x behf=%08x "
           "beh=%d/%d/%d/%d unkB8C=%d unkB90=%d face=%d/%d/%d surf=%d slow=%d stun=%d lift=%02x "
           "spd=%d/%d cr=%d inv=%d\n",
           retraces, i, p->sectorIndex, (int)p->segmentProgress, (int)p->lapProgressRemaining, (int)p->worldPos.x,
           (int)p->worldPos.y, (int)p->worldPos.z, (int)(p->worldPos.x - p->prevWorldPos.x),
           (int)(p->worldPos.y - p->prevWorldPos.y), (int)(p->worldPos.z - p->prevWorldPos.z), (int)p->velocity.x,
           (int)p->velocity.y, (int)p->velocity.z, (int)p->aiTarget.x, (int)p->aiTarget.y, (int)p->aiTarget.z,
           (int)p->rotY, (int)p->steeringAngle, (int)p->hitReactionState, (int)p->knockbackAngle,
           (int)p->knockbackVelocity.x, (int)p->knockbackVelocity.y, (int)p->knockbackVelocity.z,
           (unsigned)p->animationFlags, (unsigned)p->behaviorFlags, p->behaviorMode, p->behaviorPhase,
           p->behaviorStep, p->behaviorCounter, (int)p->unkB8C, (int)p->unkB90, p->trackFaceType,
           p->trackFaceSubtype, p->trackFaceType1Timer, p->surfaceInfo, p->slowdownLevel, p->stunCollisionCounter,
           p->chairliftFlags, (int)p->smoothedSpeedCap, (int)p->maxSpeedCap, (int)p->collisionRadius,
           (int)p->invincibilityTimer);
}

static void pin_watch(GameState *gs, unsigned long retraces) {
    int i;
    if (race_is_new(gs, &pin.gs, &pin.frame)) {
        void *g = pin.gs;
        unsigned f = pin.frame;
        memset(&pin, 0, sizeof(pin));
        pin.gs = g;
        pin.frame = f;
        for (i = 0; i < 4; i++) {
            pin.best_prog[i] = 0x7FFF;
            pin.since[i] = retraces;
        }
    }
    for (i = 0; i < gs->numPlayers && i < 4; i++) {
        Player *p = &gs->players[i];
        if (pin.burst[i] > 0) {
            pin.burst[i]--;
            pin_frame(gs, i, retraces);
            if (pin.burst[i] == 0) fflush(stdout);
            continue;
        }
        if (p->currentLap > pin.best_lap[i] ||
            (p->currentLap == pin.best_lap[i] && p->lapProgressRemaining < pin.best_prog[i])) {
            pin.best_lap[i] = p->currentLap;
            pin.best_prog[i] = p->lapProgressRemaining;
            pin.since[i] = retraces;
            pin.done[i] = 0; /* one burst per stall, not one per race */
            continue;
        }
        if (pin.done[i] || (p->animationFlags & PLAYER_FINISHED_FLAG)) continue;
        /* The start-line countdown is four riders making no progress on
         * purpose; it is not a pin. */
        /* The start-line countdown is four riders making no progress on
         * purpose, and lapProgressRemaining is 0 rather than 8192 until the
         * race arms -- so the baseline has to be taken again here, not just
         * the clock. Without the re-baseline `best_prog` stays at that 0 and
         * every rider looks pinned from the first sector on. */
        if (gs->raceIntroState != 0 || gs->raceFrameCounter < PIN_ARM) {
            pin.best_lap[i] = p->currentLap;
            pin.best_prog[i] = p->lapProgressRemaining;
            pin.since[i] = retraces;
            continue;
        }
        if (retraces - pin.since[i] < PIN_ARM) continue;
        pin.done[i] = 1;
        pin.burst[i] = PIN_BURST;
        printf("sbk-pin: r=%lu p%d has made no progress for %d retraces on level %d "
               "(lap=%d prog=%d sect=%d); %d frames of autopsy follow\n",
               retraces, i, PIN_ARM, gs->memoryPoolId, p->currentLap, (int)p->lapProgressRemaining, p->sectorIndex,
               PIN_BURST);
        pin_census(gs, i, retraces);
    }
}

/* -------------------------------------------------------------- the marshal
 *
 * What Haunted House's wedge actually is, and why only self-play hits it.
 *
 * updatePlayerNormalDriving (race_main.c ~1313) has one branch for a CPU rider
 * that a human rider does not have:
 *
 *     if (player->isCpuControlled != 0) {
 *         player->cpuInputFlags = determineAIPathChoice(player);
 *         if (player->cpuInputFlags) { setPlayerBehaviorPhase(player, 4); return 1; }
 *         if ((speed <= 0x5FFFF && player->snowboardId < SNOWBOARD_HIGH_TECH) || ...) {
 *             if (isPlayerNearLiftEntry(player) == 0) {
 *                 player->cpuInputFlags = 0;
 *                 setPlayerBehaviorPhase(player, 4);
 *                 return 1;
 *             }
 *         }
 *     } else {
 *         if (player->inputButtonsHeld & 0x8000) { setPlayerBehaviorPhase(player, 4); return 1; }
 *     }
 *     ...
 *     calculateAITargetPosition(player);   <-- never reached on that branch
 *
 * Phase 4 is dispatchPostTrickLandingStep -- the ollie. So a CPU rider whose
 * velocity magnitude drops below 0x5FFFF (about two thirds of top speed) away
 * from the lift entry hops, and the early `return 1` means it never reaches
 * calculateAITargetPosition or the steering below it. **A slow CPU rider
 * cannot steer and cannot re-aim.** It is meant to be a nudge: the rider hops,
 * gravity rolls it down the hill, it crosses 0x5FFFF again and drives. It is a
 * dead end wherever gravity cannot do that.
 *
 * On Haunted House it cannot. The measured run (port/docs/PLAN.md): the rider
 * is stunned at sector 47, comes off the drop into sectors 48-50 below the
 * threshold, spends the whole of 48-51 unable to steer, drifts off the racing
 * line onto the bank above it -- 0.67 world units higher than the line it took
 * on the previous lap at the same x -- and settles into the dip there.
 * Position becomes byte-identical, velocity zero, behaviourPhase 4 cycling its
 * four trick steps for ever, aiTarget frozen at a waypoint eight sectors
 * behind. It is not the ghost, not the pendulum, not a push zone, not another
 * rider and not the task pool: the autopsy (--racedbg, `sbk-pin:`) prints
 * every task in the race with any position within 0x400000 of the rider and
 * the only ones are the rider's own snow spray.
 *
 * So it is not a port bug, and a human would not hit it: the human branch
 * above is a button test, and a human keeps steering at any speed. It bites
 * self-play because self-play *is* a CPU rider -- and it bites the game's own
 * rivals too, which is why two riders were pinned at sector 50 in the campaign
 * and one in every trial.
 *
 * The fix belongs on the autoplay side, and it is a marshal: when player 1 has
 * made no progress for MARSHAL_ARM retraces *and* is under the threshold that
 * locks it out, aim at the racing line two sectors ahead
 * and give it exactly enough velocity along that line to be over the threshold
 * on the next frame. One frame later the game's own phase 0 runs again, steers,
 * and accelerates. Nothing else is touched, no game code is patched, and the
 * rivals are left alone -- a rival stuck at sector 50 is a rival we beat.
 */
#define MARSHAL_SPEED 0x5FFFF /* race_main.c:1319, the lock-out threshold */
#define MARSHAL_PUSH  0x68000 /* a shade over it, along the racing line */
#define MARSHAL_ARM   240     /* four seconds of no progress before pushing */
#define MARSHAL_MAX   3600    /* and never for longer than a minute of them */
#define MARSHAL_LIFT  900     /* pushing for fifteen seconds and still nowhere:
                               * pick the rider up and put it on the line */

int sbk_marshal_pushes;

static void marshal_tick(GameState *gs, unsigned long retraces) {
    static void *m_gs;
    static unsigned m_frame;
    static unsigned long since;
    static s32 best;
    static u8 best_lap;
    static int pushes, said, in_push, push_frames;
    Player *p = &gs->players[0];
    extern s32 computeAngleToPosition(s32, s32, s32, s32);
    double vx, vy, vz, speed, dx, dz, len;
    s32 tx = 0, tz = 0;
    int at_end = 0;

    if (!sbk_autoplay || !p->isCpuControlled) return;
    /* Re-baseline on a race the frame test somehow did not catch as well: a
     * rider whose lap went *down*, or whose remaining progress jumped a
     * quarter of a lap the wrong way, is not the rider this state describes.
     * Getting this wrong is not harmless -- a stale `best` means no frame
     * ever counts as progress, and the marshal carries a rider that is
     * racing perfectly well. */
    if (race_is_new(gs, &m_gs, &m_frame) || p->currentLap < best_lap ||
        (p->currentLap == best_lap && p->lapProgressRemaining > best + 2048)) {
        best = p->lapProgressRemaining;
        best_lap = p->currentLap;
        since = retraces;
        pushes = 0;
        said = 0;
        in_push = 0;
        push_frames = 0;
        return;
    }
    if ((p->animationFlags & PLAYER_FINISHED_FLAG) || gs->raceIntroState != 0) {
        since = retraces;
        return;
    }
    /* Not at the lift.
     *
     * A lap wraps by waiting at the chairlift: tryEnterLift() hands the rider
     * to initKnockbackBehavior, so behaviourMode is 3 and the rider makes no
     * lap progress at about 110,000 units a frame -- under the threshold and
     * over the arming time, which is exactly the shape the marshal is looking
     * for. Pushing a rider that is queueing for the lift is how this port
     * earned its first wedge; leave mode 3 and anything the lift has flagged
     * alone. The game does the same thing itself: the lock-out branch is
     * guarded by isPlayerNearLiftEntry(). */
    if (p->behaviorMode == 3 || p->chairliftFlags != 0) {
        /* Not a reason to forget how long the rider has been here: a lift wait
         * that never ends is a wedge too, and the log should say that the
         * marshal stood back rather than that nothing happened. */
        if (retraces - since >= MARSHAL_ARM && !said) {
            said = 1;
            printf("sbk-marshal: r=%lu player 1 has made no progress for %lu retraces at level %d lap %d "
                   "sect %d prog %d, but it is at the lift (mode=%d liftflags=%02x); standing back\n",
                   retraces, retraces - since, gs->memoryPoolId, p->currentLap, p->sectorIndex,
                   (int)p->lapProgressRemaining, p->behaviorMode, p->chairliftFlags);
            fflush(stdout);
        }
        return;
    }
    if (p->currentLap > best_lap || (p->currentLap == best_lap && p->lapProgressRemaining < best)) {
        best_lap = p->currentLap;
        best = p->lapProgressRemaining;
        since = retraces;
        said = 0;   /* one line per stall, not one per race */
        in_push = 0;
        push_frames = 0;
        return;
    }
    if (retraces - since < MARSHAL_ARM || pushes >= MARSHAL_MAX) return;

    vx = (double)(s32)p->velocity.x;
    vy = (double)(s32)p->velocity.y;
    vz = (double)(s32)p->velocity.z;
    speed = sqrt(vx * vx + vy * vy + vz * vz);
    /* Above the threshold the rider is not locked out; whatever is holding it
     * is a different wedge and the watchdog owns that one. */
    if (speed > (double)MARSHAL_SPEED) {
        if (!said) {
            said = 1;
            printf("sbk-marshal: r=%lu player 1 has made no progress for %lu retraces at level %d lap %d "
                   "sect %d prog %d but is moving at %d (> %d): not the lock-out, leaving it to the "
                   "watchdog (mode=%d phase=%d anim=%08x)\n",
                   retraces, retraces - since, gs->memoryPoolId, p->currentLap, p->sectorIndex,
                   (int)p->lapProgressRemaining, (int)speed, MARSHAL_SPEED, p->behaviorMode, p->behaviorPhase,
                   (unsigned)p->animationFlags);
            fflush(stdout);
        }
        return;
    }

    /* Where "along the line" is.
     *
     * calculateAITargetPosition() is the obvious way to ask and the wrong one:
     * it opens with getCurrentAllocation(), which answers with whatever task
     * the scheduler is *inside*, and this tick is not inside one -- calling it
     * from here is a SIGBUS (exit 138, measured). The track graph is plain
     * data hanging off the race's own GameState, so the marshal reads it
     * directly and aims at the centre of the end of the sector two ahead,
     * which is the same place the racing line goes. */
    {
        TrackData *td = &gs->gameData;
        int sec = (int)p->sectorIndex, k;
        if (sec < 0 || sec >= (int)td->sectorCount) return;
        for (k = 0; k < 2; k++) {
            int nxt = (int)td->sectors[sec].nextSectorIndex;
            if (nxt < 0 || nxt >= (int)td->sectorCount) {
                at_end = 1;
                break;
            }
            sec = nxt;
        }
        if (at_end) {
            /* The last sector does not end at a vertex you can stand on, it
             * ends at the lift. A lap -- the final one included -- wraps at
             * race_main.c:4504, `distanceToTarget < 0x1000 && rollAngle == 0`,
             * and that target is the level's own liftEntryPos. Parked on the
             * end centre of sector 116 instead, the rider sat 48 million units
             * short of it with sectorIndex still reading 115 and
             * lapProgressRemaining still 55, which is three races of course 9
             * lost at the goal. calculateAITargetPosition does exactly this
             * when sectors[cur].nextSectorIndex < 0; so does the marshal. */
            LevelConfig *lc = getLevelConfig(gs->memoryPoolId);
            tx = lc->liftEntryPosX;
            tz = lc->liftEntryPosZ;
        } else {
            tx = (s32)td->vertices[td->sectors[sec].endCenterVertexIndex].x << 16;
            tz = (s32)td->vertices[td->sectors[sec].endCenterVertexIndex].z << 16;
        }
    }
    dx = (double)tx - (double)(s32)p->worldPos.x;
    dz = (double)tz - (double)(s32)p->worldPos.z;
    len = sqrt(dx * dx + dz * dz);
    if (len < 1.0) return;

    /* How long the marshal has been *pushing*, which is not how long the
     * rider has been stuck: a stall that starts above the threshold spends
     * its first seconds being declined, and counting elapsed time rather than
     * pushes made the very first push escalate straight to a carry. Counting
     * the pushes themselves cannot be fooled by either. */
    if (!in_push) {
        in_push = 1;
        push_frames = 0;
    }
    push_frames++;
    p->velocity.x = (s32)(dx / len * (double)MARSHAL_PUSH);
    p->velocity.z = (s32)(dz / len * (double)MARSHAL_PUSH);
    p->rotY = (s16)computeAngleToPosition(tx, tz, (s32)p->worldPos.x, (s32)p->worldPos.z);
    p->steeringAngle = 0;
    pushes++;

    /* A push that is not working.
     *
     * Course 9's final lap found a spot the push cannot solve: the rider sat
     * at `lap=2 prog=55 sect=115` with a position byte-identical across two
     * separate races while the marshal pushed it every frame -- being *held*
     * rather than being slow, and a velocity the game overwrites before it
     * integrates it is no use. Fifteen seconds of that and the marshal stops
     * pushing and carries: the rider is put on the centre of the end of the
     * sector it is in, at the track's own height there, with prevWorldPos
     * moved with it so nothing downstream sees a teleport-sized delta. The
     * watchdog still owns anything this does not fix. */
    if (push_frames >= MARSHAL_LIFT) {
        TrackData *td = &gs->gameData;
        int sec = (int)p->sectorIndex;
        int ok = 1;
        if (at_end) {
            /* Put it at the lift, where the lap wrap is waiting for it. */
            p->worldPos.x = tx;
            p->worldPos.z = tz;
        } else if (sec >= 0 && sec < (int)td->sectorCount) {
            /* The end of the rider's *own* sector is not far enough: carried
             * there at level 9 sector 115 it slid straight back and
             * lapProgressRemaining never left 55, twice. Carry it to the end
             * of the next sector, which is past the boundary, so the carry is
             * progress by the game's own measure. */
            int nxt = (int)td->sectors[sec].nextSectorIndex;
            if (nxt >= 0 && nxt < (int)td->sectorCount) sec = nxt;
            {
                const Vec3s *v = &td->vertices[td->sectors[sec].endCenterVertexIndex];
                p->worldPos.x = (s32)v->x << 16;
                p->worldPos.y = ((s32)v->y << 16) + 0x20000;
                p->worldPos.z = (s32)v->z << 16;
            }
        } else {
            ok = 0;
        }
        if (ok) {
            memcpy(&p->prevWorldPos, &p->worldPos, sizeof(Vec3i));
            p->rollAngle = 0; /* race_main.c:4504 wants it zero to let the lap wrap */
            printf("sbk-marshal: r=%lu %d frames of pushing at level %d lap %d sect %d prog %d moved player 1 "
                   "nowhere; carrying it to %s at %d,%d,%d\n",
                   retraces, push_frames, gs->memoryPoolId, p->currentLap, p->sectorIndex,
                   (int)p->lapProgressRemaining, at_end ? "the lift entry" : "the end of the next sector",
                   (int)p->worldPos.x, (int)p->worldPos.y, (int)p->worldPos.z);
            fflush(stdout);
            push_frames = 0; /* give it another MARSHAL_LIFT to get going */
        }
    }
    sbk_marshal_pushes++;
    if (!said) {
        said = 1;
        printf("sbk-marshal: r=%lu player 1 locked out at level %d lap %d sect %d prog %d "
               "(speed %d <= %d, phase %d); pushing it along the line to %d,%d\n",
               retraces, gs->memoryPoolId, p->currentLap, p->sectorIndex, (int)p->lapProgressRemaining, (int)speed,
               MARSHAL_SPEED, p->behaviorPhase, (int)tx, (int)tz);
        fflush(stdout);
    }
}

/* --------------------------------------------------------------------- ticks */

void sbk_autoplay_tick(unsigned long retraces) {
    static unsigned soak_step;
    static int was_racing;
    GameState *gs;
    extern void sbk_menu_nav_tick(unsigned long);
    extern int sbk_menu_on(const char *);

    sbk_menu_nav_tick(retraces);
    status_tick(retraces);

    if (sbk_nightmare) nightmare_write_row();

    gs = sbk_race_state();
    /* The boss pilot's clock. It is fed on every tick, race or no race: the
     * hook it drives runs inside the game's own item code, which has no idea
     * what a retrace is. */
    { extern void sbk_boss_pilot_tick(GameState *, unsigned long); sbk_boss_pilot_tick(gs, retraces); }
    if (gs != NULL && !sbk_race_is_demo(gs)) {
        int i;
        Player *p1 = &gs->players[0];
        if (sbk_nightmare) {
            /* The rivals go on RIVAL_ROW, which is the Nightmare row plus the
             * handicap ladder's tax; player 1 is put on the untaxed row 7 by
             * autoplay_arm. Slot 0 is skipped here even when it is a CPU. */
            for (i = 1; i < gs->numPlayers; i++) {
                if (gs->players[i].isCpuControlled) gs->players[i].aiDifficultyIndex = RIVAL_ROW;
            }
        }
        wall_watch(gs);
        if (sbk_pin_trace) pin_watch(gs, retraces);
        race_watchdog(gs, retraces);
        marshal_tick(gs, retraces);
        boss_brake_tick(gs, p1);
        if (sbk_autoplay && p1->isCpuControlled == 0) {
            autoplay_arm(gs, retraces);
        } else if (sbk_autoplay) {
            /* initPlayer's own applyCharacterSnowboardStats lands a second or
             * two after the handoff and wipes the boost. Put it back. */
            retune_hold(p1);
        }
        /* The path table only exists once the level's assets have landed, which
         * is after the handoff; keep trying until it does. */
        if (sbk_autoplay) path_table_attach(gs, p1, 1);
        trial_tick(gs, retraces);
        was_racing = 1;
        /* The results screens run on the race's own scheduler, so "a race
         * allocation exists" is not "a race is being played": only
         * handleRaceStateUpdate is. */
        if (sbk_menu_on("handleRaceStateUpdate")) return;
    } else if (was_racing) {
        was_racing = 0;
        /* Do NOT clear trial_start here. sbk_race_state() wants a scheduler in
         * SCHEDULER_STATE_RUNNING, and the race's scheduler leaves that state
         * for a few frames at a lift cutaway and at the goal banner -- so this
         * branch runs *during* a race, not only between races. Clearing the
         * stamp here silently disarmed trial_tick for the rest of the race:
         * three races finished with anim=00080000 on player 1 and not one
         * printed a result or honoured quit=1. autoplay_arm() re-stamps it for
         * every new race anyway, which is the only place that should. */
    }

    /* Menus. --autonav (menu_nav.c) drives them by name; --soak on its own is
     * the monkey: A, A, A, up-then-A every 1.5 s, never START (a queued START
     * pauses the race that is about to begin). */
    if (sbk_soak && retraces % 90 == 0) {
        extern int sbk_autonav;
        if (sbk_autonav) return;
        switch (soak_step++ & 3) {
            case 0:
                sbk_input_play_add("stick 0 80 3");
                sbk_input_play_add("wait 6");
                sbk_input_play_add("press A 3");
                break;
            default:
                sbk_input_play_add("press A 3");
                break;
        }
    }
}

/* --------------------------------------------------------- --peek / --racedbg */

static struct { unsigned addr, len; } peeks[8];
static int npeeks;

int sbk_peek_add(const char *spec) {
    unsigned a, n;
    if (npeeks >= 8 || sscanf(spec, "%x:%x", &a, &n) != 2) return -1;
    peeks[npeeks].addr = a;
    peeks[npeeks].len = n > 256 ? 256 : n;
    npeeks++;
    sbk_race_debug_enabled = 1;
    return 0;
}

static void dump_peeks(unsigned long retraces) {
    int i;
    unsigned k;
    for (i = 0; i < npeeks; i++) {
        const unsigned char *p = (const unsigned char *)(uintptr_t)peeks[i].addr;
        printf("sbk-peek: r=%lu %08x:", retraces, peeks[i].addr);
        for (k = 0; k < peeks[i].len; k++) printf("%s%02x", (k % 4) ? "" : " ", p[k]);
        printf("\n");
    }
}

void sbk_race_debug(unsigned long retraces) {
    GameState *gs;
    int i;
    extern const char *sbk_menu_names(void);

    dump_peeks(retraces);
    if (npeeks) return;

    gs = sbk_race_state();
    if (gs == NULL) {
        printf("sbk-race: r=%lu no race (screens: %s)\n", retraces, sbk_menu_names());
        fflush(stdout);
        return;
    }
    /* counters[] is the task pool, and counters[0] is the one that matters: the
     * lift step out of the lap wait is `scheduleTask(..., nodeType 0, ...)`
     * (race_main.c:4505 for most courses, :4516 for Turtle Island), and
     * scheduleTask returns NULL the moment counters[nodeType] hits 0. A rider
     * frozen byte-identically at the lift with counters[0] == 0 beside it is
     * the wedge, named, rather than inferred from a still position.
     * docs/nightmare-row.md has the chain. */
    printf("sbk-race: r=%lu level=%d type=%d players=%d/%d lap=%d/%d frame=%u paused=%d intro=%d demo=%d pool=%d,%d,%d,%d wall=%d,%d,%d,%d "
           "rank=",
           retraces, gs->memoryPoolId, gs->raceType, gs->playerCount, gs->numPlayers, gs->players[0].currentLap,
           gs->finalLapNumber, (unsigned)gs->raceFrameCounter, gs->gamePaused, gs->raceIntroState,
           sbk_race_is_demo(gs), sbk_race_pool(0), sbk_race_pool(1), sbk_race_pool(2), sbk_race_pool(3),
           sbk_wall_frames[0], sbk_wall_frames[1], sbk_wall_frames[2], sbk_wall_frames[3]);
    for (i = 0; i < gs->numPlayers && i < 4; i++) printf("%s%d", i ? "," : "", gs->rankOrder[i]);
    printf("\n");
    for (i = 0; i < gs->numPlayers && i < 4; i++) {
        Player *p = &gs->players[i];
        printf("sbk-race: r=%lu p%d cpu=%d diff=%d chr=%d board=%d place=%d lap=%d prog=%d sect=%d stick=%d,%d "
               "btn=%04x pos=%d,%d,%d spd=%d/%d anim=%08x beh=%d/%d/%d wait=%d roll=%d hit=%d slow=%d inv=%d "
               "item=%d/%d ammo=%d boss=%d hp=%d gold=%d\n",
               retraces, i, p->isCpuControlled, p->aiDifficultyIndex, p->characterId, p->snowboardId,
               p->finishPosition + 1, p->currentLap, p->lapProgressRemaining, p->sectorIndex, p->inputStickX,
               p->inputStickY, (unsigned)p->inputButtonsHeld, (int)p->worldPos.x, (int)p->worldPos.y, (int)p->worldPos.z,
               (int)p->smoothedSpeedCap, (int)p->baseMaxSpeed, (unsigned)p->animationFlags, p->behaviorMode,
               p->behaviorStep, p->behaviorCounter, (int)p->unkB8C, (int)p->rollAngle, (int)p->hitReactionState,
               (int)p->slowdownLevel, (int)p->invincibilityTimer, p->primaryItemId,
               p->secondaryItemId, (int)p->primaryItemAmmo, (int)p->isBossRacer, (int)p->bossHealth, (int)p->raceGold);
    }
    fflush(stdout);
}
