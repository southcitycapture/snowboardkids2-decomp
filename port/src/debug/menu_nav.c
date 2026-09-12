/* Campaign navigator for Snowboard Kids 2: --autonav, --saveevery, --menutrace.
 *
 * The sequel has no single "current menu" variable either. What it does have is
 * the task scheduler list: gSchedulerListSentinel.next is a priority-ordered
 * chain of TaskScheduler, each with the `gamestateHandler` it will run next and
 * its own allocation (`allocatedState`) holding that screen's state. Naming
 * those handlers with dladdr() gives the screen's real identity without a
 * hand-written table -- the trick the first game's menu_nav.c used on
 * gActiveGameTaskList -- and the allocation beside the name is what a navigator
 * parks its cursors in.
 *
 * The map, read off a --menutrace of a soak run on the G4:
 *
 *   initLogoSplash / updateLogoSplash          the logo
 *   startDemoRace ... handleRaceStateUpdate    the attract demo (mode 3)
 *   handleTitleMenuInput                       START / TRAINING / OPTION
 *   updateSaveSlotSelectionScreen              the file select (EEPROM)
 *   storyMapHandlePlayerInput                  the story overworld
 *   handleLevelSelectInput                     the course list
 *   updateCharacterSelect                      rider + board
 *   updateCutscenePlayback                     the pre-race cutscene
 *   handleRaceStateUpdate                      the race itself
 *   handle*GameResult / await*ContinuePress    the results
 *
 * Only `handleRaceStateUpdate` means "a race is being played": the results
 * screens run on the *same* scheduler as the race, so a navigator that keeps
 * its hands off whenever a race allocation exists sits on the results screen
 * for ever. That was the first bug this file had.
 *
 * Never START: a START still queued when a race begins pauses it.
 */
#include "../ultra/ultra.h"
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include "common.h"
#include "gamestate.h"
#include "system/task_scheduler.h"
#include "ui/save_slot_gfx.h"
#include "ui/title_ui_elements.h"
#include "ui/level_preview.h"
#include "effects/cutscene_keyframes.h"
#include "ui/save_data.h"
#include "../platform/input.h"

extern TaskScheduler gSchedulerListSentinel;
extern GameSessionContext *gGameSessionContext;
/* src/common_bss.c: which entry of storyMapLocationHandlers[] the map will run
 * next. 7 is initSaveSlotScreen, and the map sets it from the location the
 * rider walked into (discoveredLocationId + 1). */
extern u8 storyMapLocationIndex;
GameState *sbk_race_state(void);
int sbk_race_is_demo(const GameState *gs);
/* race_dbg.c: the handicap ladder's two levers, and the wedge flag its race
 * watchdog raises. */
extern int sbk_campaign_boost;
extern int sbk_rival_tax;
extern int sbk_campaign_wedge;
extern int sbk_item_relief;
extern int sbk_rival_item_relief;

int sbk_menutrace;
int sbk_autonav;
int sbk_autonav_shop;
int sbk_autonav_every = 1;
/* --trial level=N: which course the next race should be. -1 = whatever the menu
 * offers. Set by race_dbg.c's trial parser. */
int sbk_nav_target_level = -1;

/* The story map's save point: location handler 7 = initSaveSlotScreen, and the
 * map's own id for it is one less. */
#define STORY_SAVE_LOCATION 6
/* The town's *other* exit. gameStateCleanupHandler (src/core/game_state_init.c)
 * reads one byte, unk427, and splits on it:
 *
 *   unk427 == id + 1   storyMapLocationIndex = id + 1, return 1
 *                      -> handleStoryMapLocationComplete runs
 *                         storyMapLocationHandlers[id + 1]: a building
 *   unk427 == 0xFF     return 0xFF -> handleGameStateComplete ->
 *                      onStoryMapExitToMenu -> awaitStoryMapSelection, which
 *                      maps BOTH 0x44 and 0xFF to loadLevelSelectScreen
 *
 * so 0xFF -- despite the name of the callback -- is "leave the town", which is
 * the campaign's route to the next course. In the game the rider walks off the
 * map and finalizeStoryMapExit (src/story/map_character_anim.c) writes the same
 * byte once the fade lands. */
#define STORY_LEAVE_TOWN 0xFF

/* ------------------------------------------------------------- screen naming */

#define MAX_NAMES 24
static const void *cur_fn[MAX_NAMES];
static TaskScheduler *cur_sched[MAX_NAMES];
static int cur_count;

/* dladdr() walks the binary's symbol table -- a few hundred microseconds on
 * the G4 -- and the navigator asks for the name of every live scheduler a
 * dozen times a retrace. Uncached that was ~15 ms a frame: the campaign ran
 * at 73% of real time with the perf counters showing nothing, because the
 * tick runs in the host loop, outside every stamped phase. Handlers are code,
 * so the name of an address never changes: look each one up once. */
#define NAME_CACHE 256
static struct { const void *fn; char name[64]; } name_cache[NAME_CACHE];
static int name_cache_n;

const char *sbk_fn_name(void *fn) {
    static char buf[64];
    Dl_info info;
    int i;
    if (fn == NULL) return "-";
    for (i = 0; i < name_cache_n; i++) {
        if (name_cache[i].fn == fn) return name_cache[i].name;
    }
    if (dladdr(fn, &info) && info.dli_sname != NULL) {
        const char *n = info.dli_sname;
        if (n[0] == '_') n++; /* Mach-O underscore */
        snprintf(buf, sizeof(buf), "%s", n);
    } else {
        snprintf(buf, sizeof(buf), "%p", fn);
    }
    if (name_cache_n < NAME_CACHE) {
        name_cache[name_cache_n].fn = fn;
        snprintf(name_cache[name_cache_n].name, sizeof(name_cache[0].name), "%s", buf);
        return name_cache[name_cache_n++].name;
    }
    return buf;
}

static void collect(void) {
    TaskScheduler *s = gSchedulerListSentinel.next;
    cur_count = 0;
    while (s != NULL && cur_count < MAX_NAMES) {
        if (s->gamestateHandler != NULL) {
            cur_sched[cur_count] = s;
            cur_fn[cur_count++] = (const void *)s->gamestateHandler;
        }
        s = s->next;
    }
}

int sbk_menu_on(const char *name) {
    int i;
    for (i = 0; i < cur_count; i++) {
        if (strcmp(sbk_fn_name((void *)cur_fn[i]), name) == 0) return 1;
    }
    return 0;
}

/* The allocation of the scheduler whose handler is `name`: that screen's own
 * state struct. */
void *sbk_menu_alloc(const char *name) {
    int i;
    for (i = 0; i < cur_count; i++) {
        if (strcmp(sbk_fn_name((void *)cur_fn[i]), name) == 0) return cur_sched[i]->allocatedState;
    }
    return NULL;
}

const char *sbk_menu_names(void) {
    static char buf[768];
    int i;
    buf[0] = 0;
    for (i = 0; i < cur_count; i++) {
        strncat(buf, sbk_fn_name((void *)cur_fn[i]), sizeof(buf) - strlen(buf) - 2);
        strncat(buf, " ", sizeof(buf) - strlen(buf) - 2);
    }
    return buf;
}

/* True when any active handler's name contains `frag` -- the results screens
 * are one per race type (handleSpeedCrossGameResult, handleMeterGameResult,
 * awaitSkillWinContinuePress, ...) and are best matched by shape. */
static int menu_has(const char *frag) {
    int i;
    for (i = 0; i < cur_count; i++) {
        if (strstr(sbk_fn_name((void *)cur_fn[i]), frag) != NULL) return 1;
    }
    return 0;
}

static void menutrace(unsigned long retraces) {
    static char last[768];
    const char *now = sbk_menu_names();
    if (strcmp(now, last) != 0) {
        snprintf(last, sizeof(last), "%s", now);
        printf("sbk-menu: r=%lu gold=%d mode=%d level=%d :: %s\n", retraces,
               gGameSessionContext ? (int)gGameSessionContext->gold : -1,
               gGameSessionContext ? gGameSessionContext->gameMode : -1,
               gGameSessionContext ? gGameSessionContext->currentLevel : -1, now);
        fflush(stdout);
    }
}

/* ------------------------------------------------------------------- autonav */

enum { NAV_RACE = 0, NAV_SAVE = 1 };
static int nav_want = NAV_RACE;
static int nav_races, nav_saves;
/* How many times the town has been asked to hand over to the course list since
 * a race last actually *started*. It is the detector for the loop this
 * navigator used to run: a screen chain that keeps *changing* never trips the
 * "stuck on one screen" timer below, and 177 laps went by unnoticed before the
 * exit was understood.
 *
 * It used to be cleared on every tick that saw `handleRaceStateUpdate`, which
 * made it useless twice over: the attract demo on the title screen runs that
 * handler too, so a fresh boot zeroed the count before the campaign had even
 * begun, and there was no edge -- the counter could only ever hold exits from
 * the current menu walk, never accumulate across the town/rider-picker circle
 * the guard exists to catch. It is now cleared once, on the rising edge of a
 * *story* race (the demo is excluded by sbk_race_is_demo), and the warning
 * re-arms with it so a second loop is reported as loudly as the first. */
static int nav_town_exits;
static int nav_loop_warned;
static unsigned long nav_last_action;
/* The credits: the campaign's finish line. awaitCreditsSequence is reached only
 * from awaitPostRaceCutscene when currentLevel == 0xB (src/core/session_manager.c),
 * i.e. after the last story course has been won. */
static int nav_credits_seen;

/* The rider's finish, latched while the race is still on screen.
 *
 * A boss race tears its GameState down before its result screen comes up, so
 * sbk_race_state() is already NULL when nav_watch fires and the place reads
 * -1. nav_handicap can only read that as a loss, which means a *won* boss race
 * would climb the handicap ladder and a lost one would be indistinguishable
 * from it -- the campaign would grind the boss for ever at ever-higher rungs
 * having already beaten it. The Jingle Town boss is where that showed up.
 *
 * The place is settled long before the result screen: race_main.c writes
 * finishPosition and then sets the 0x80000 "finished" bit in animationFlags
 * the moment the rider crosses the line, with the race still running. So the
 * navigator latches it there, and the result screen goes back to being what it
 * should have been all along -- the trigger, not the source. */
#define PLAYER_FINISHED_FLAG 0x80000
/* ...and on a boss course the place is not the place at all. race_main.c ~5188
 * never gives our rider finishPosition 0 for beating a health boss: the boss is
 * usually still ahead of us on the track when its last snowman head goes, so
 * the rank order that the finish flag freezes says 2nd. The win is written
 * somewhere else -- 0x100000 on the boss, which is also what handleBossDefeatResult
 * reads to choose gRaceResultCode 3 over 4. So the navigator asks the same
 * question the game asks. Without this a *won* boss race would be recorded as a
 * loss and the campaign would grind it for ever at ever-higher rungs. */
#define BOSS_DEFEATED_FLAG 0x100000
static int nav_place_of(GameState *gs) {
    extern int sbk_is_hp_boss_race(int);
    extern Player *sbk_boss_rider(GameState *);
    Player *boss;
    if (gs == NULL) return -1;
    if (sbk_is_hp_boss_race(gs->raceType) && (boss = sbk_boss_rider(gs)) != NULL) {
        return (boss->animationFlags & BOSS_DEFEATED_FLAG) ? 0 : 1;
    }
    return (int)gs->players[0].finishPosition;
}

static int nav_latched_place = -1;
static int nav_latched_level = -1;

static void nav_press(const char *line) { sbk_input_play_add(line); }

/* Which course the campaign should play next, when no --trial level says.
 *
 * The game marks it itself: updateStorySlotUnlockStatus (src/core/session_manager.c)
 * leaves every finished course at levelUnlockStatus 1 and writes 5 into the
 * next one, which is the same 5 the level list's own cursor logic and the
 * rider picker's exit read. Without this the navigator confirms whatever the
 * list opens on -- which is gGameSessionContext->currentLevel, the course just
 * played -- and the campaign grinds Sunny Mountain for ever instead of
 * advancing. Slots 12..14 are the Cross minigames, which are entered from the
 * town, so the scan stops at 12. */
static int nav_next_story_level(void) {
    int i;
    if (EepromSaveData == NULL) return -1;
    for (i = 0; i < 12; i++) {
        if (EepromSaveData->levelUnlockStatus[i] == 5) return i;
    }
    /* No 5 anywhere does not mean the campaign is over: it means a course was
     * *played and lost*. awaitRaceResult (src/core/session_manager.c) writes
     *
     *     (result == 3) | (result == 5)   -> levelUnlockStatus[level] = 1
     *     result 4 or 6                   -> levelUnlockStatus[level] = 4
     *
     * -- 3 is "finishPosition == 0" out of handleSpeedCrossGameResult /
     * handleBossRaceResult, so 1 is won and **4 is raced and not won** -- and
     * updateStorySlotUnlockStatus only writes the 5 that marks the next course
     * once every slot below the next gate is a 1 (slot 4 needs 0..3, slot 8
     * needs 0..7, slot 10 needs 0..9 and all three Cross games). A 4 therefore
     * *blocks* the campaign, and it is the state the user's own save was in:
     * [1,4,1,4,0,...], courses 1 and 3 attempted and lost.
     *
     * The navigator used to return -1 here, which left the course list opening
     * on gGameSessionContext->currentLevel -- course 0, already won -- so the
     * campaign re-raced Sunny Mountain for ever and could never reach the gate.
     * The course that has to be played is the first one that is not a 1: a 5 if
     * the game has marked one, otherwise the earliest 4. */
    for (i = 0; i < 12; i++) {
        if (EepromSaveData->levelUnlockStatus[i] == 4) return i;
    }
    return -1;
}

/* The three Cross minigames, which are *not* on the course list: they are
 * buildings in the town. handleGameStateComplete (src/story/map_state.c)
 * intercepts storyMapLocationIndex 3, 6 and 9 -- the three
 * initStoryMapLocationIntro entries of storyMapLocationHandlers[] -- and sets
 * gGameSessionContext->currentLevel to 0xD, 0xE and 0xC itself before running
 * initStoryModeRace, so their save slots are levelUnlockStatus[13], [14] and
 * [12]. A location id is one less than its handler index.
 *
 * The campaign cannot skip them. updateStorySlotUnlockStatus only opens slot 10
 * -- the last two courses, and so the credits -- when slots 0..9 are all 1 *and*
 * slots 12..14 are all 1 as well, and a slot still at 0 is never offered by the
 * course list at all. Returns the location id to walk into, or -1. */
static int nav_next_cross(void) {
    static const struct { u8 slot, location; } cross[3] = { { 13, 2 }, { 14, 5 }, { 12, 8 } };
    int i;
    if (EepromSaveData == NULL) return -1;
    for (i = 0; i < 3; i++) {
        u8 st = EepromSaveData->levelUnlockStatus[cross[i].slot];
        if (st != 0 && st != 1) return cross[i].location;
    }
    return -1;
}

static int nav_saves_pending(void) { return nav_want == NAV_SAVE; }

/* The campaign's own progress bar. levelUnlockStatus is the one place the game
 * records how far the story has got -- 1 for a course already won, 5 for the
 * one it means to offer next, 0 for the ones still locked -- so printing it
 * whenever it changes turns "which course is the campaign on" from a guess into
 * a line in the log, and dates every EEPROM write beside it. */
static void nav_progress(const char *why) {
    static char last[80];
    char now[80];
    int i, n = 0, won = 0;
    if (EepromSaveData == NULL) return;
    for (i = 0; i < 12; i++) {
        n += snprintf(now + n, sizeof(now) - n, "%d", (int)EepromSaveData->levelUnlockStatus[i]);
        if (EepromSaveData->levelUnlockStatus[i] == 1) won++;
    }
    if (strcmp(now, last) == 0) return;
    snprintf(last, sizeof(last), "%s", now);
    printf("sbk-nav: progress [%s] won=%d next=%d gold=%d (%s)\n", now, won, nav_next_story_level(),
           (int)EepromSaveData->gold, why);
    fflush(stdout);
}

/* The handicap.
 *
 * Only finishPosition 0 marks a course won, and the campaign's first real stall
 * was not a menu at all: the CPU rider came *second* on course 1, and a retry
 * with identical settings comes second again -- for ever, unattended. The
 * navigator therefore makes each retry of the same course different, by the one
 * lever that changes nothing but player 1: sbk_campaign_boost, 1/256ths added
 * to its top speed by trial_retune (race_dbg.c). Every rival, every item and
 * every course stays exactly as the game authored it.
 *
 * A win resets it, so the help is never carried into a course that does not
 * need it, and it is capped: past about +50% the rider overshoots the course's
 * own corners and gets slower, so a boost that has run to the cap is a real
 * finding to report rather than a knob to keep turning. */
/* The ladder. Two levers, and the order matters:
 *
 *   boost      1/256ths added to player 1's own top speed (trial_retune)
 *   rivaltax   added to the rivals' row-0 speed tax, which race_main.c ~804
 *              takes off maxSpeedCap for CPU riders -- and only the rivals pay
 *              it, because they are put on RIVAL_ROW and player 1 is not
 *
 * One boost step (+10%) is safe and won nothing; two (+21%) wedged the rider on
 * course 1 -- it stopped moving and, since a standard race only ends when the
 * human slot's rider finishes, the race never ended at all. So the boost is
 * held at one step and everything above it slows the opposition instead, which
 * changes no physics on our side. The tax tops out at 255, beyond which the
 * byte simply saturates.
 *
 * A course that loses every rung is a real finding about the rider, not a knob
 * to keep turning, and the last rung says so. */
/* The third lever: `relief`, taken off the RIVALS' item chances alone by
 * nightmare_write_row (race_dbg.c, sbk_rival_item_relief). At the searched
 * row's 205 a relief of 165 leaves 40, the floor that function clamps to --
 * the rivals still race, but they almost never throw anything, while our rider
 * keeps a full item set.
 *
 * The order of these rungs is the whole point of them, and course 1 of the
 * campaign had to teach it twice.
 *
 *   rung 0  no handicap                     2nd
 *   rung 1  boost 28                        2nd
 *   rung 2  boost 28, rivaltax 160          4th, and the rider WEDGED
 *   rung 2' boost 28, *symmetric* relief    3rd -- worse than no handicap
 *
 * The wedge at rung 2 is not bad luck, it is the ladder biting itself.
 * docs/nightmare-row.md measured `tax=168` applied to the whole field and got a
 * DNF out of it, for a reason that applies just as well when only the rivals
 * pay: a heavy tax makes the whole race longer, every item is a scheduled task,
 * and the one call that lets a rider out of the chairlift wait is a
 * scheduleTask that returns NULL once the pool is full. Slowing the opposition
 * buys time for exactly the pressure that stops the race ending. So the two
 * highest rungs of the old ladder were the two most likely to hang it.
 *
 * Rung 2' is the subtler lesson. The first version of these rungs reached for
 * sbk_item_relief, which writes *both* rows -- and hit_reactions.c indexes
 * gAIPlayerParams by each rider's own row, so it disarmed our rider by exactly
 * as much as the rivals. That is not a handicap, it is a house rule, and our
 * rider is worse under it: third, from a standing second. The lever had to be
 * split before the rung could mean anything.
 *
 * With that done the relief rungs come first, because rival relief is the only
 * lever that is *good* for the pool -- fewer items thrown is less pressure on
 * it, not more -- and it touches no rider's speed, handling or cornering at
 * all. The tax survives as the last rung only, on top of a pool the relief has
 * already emptied. */
static const struct { s16 boost, tax, relief; } nav_ladder[] = {
    /* Boost past +10% is new, and it is above the relief rather than below it:
     * course 8 sat at 2nd place through rungs 2 and 3 -- close, and not going to
     * be closed by taking more items off rivals who were already relieved of
     * 165 -- and then *wedged* on the old top rung, which is the tax rung the
     * ladder's own notes call the one most likely to hang a race. So the tax
     * rung moves to last and two boost rungs go in front of it, at the +16% and
     * +21% the boss ladder already wins races with. */
    { 0, 0, 0 },  { 28, 0, 0 },   { 28, 0, 100 },  { 28, 0, 165 },
    { 42, 0, 165 }, { 56, 0, 165 }, { 56, 160, 165 },
};

/* A boss race needs a different ladder, because on a boss none of the rival
 * levers reach anything -- and neither does our own top speed.
 *
 * The campaign lost the Jingle Town boss four times running, at every rung,
 * always by exactly one place, never earning a coin. The roster dump said the
 * first half of why in two lines:
 *
 *     rider 0 (us):   cpu=1 boss=0 diff=7 top=1394607
 *     rider 1 (boss): cpu=1 boss=1 diff=6 top=0
 *
 * The boss *is* on RIVAL_ROW, so the tax was reaching the right rider -- but its
 * baseMaxSpeed is 0. A boss does not move by the racer speed model at all
 * (updateJingleTownBoss writes maxSpeedCap itself, from its distance to us), so
 * the speed tax subtracts from something the boss never reads.
 *
 * The other half is that a boss race is not won by racing. race_main.c ~5188
 * ends RACE_TYPE_BOSS_JINGLE / BOSS_ICE two ways only: the boss reaches the line
 * (we lose) or the boss's animationFlags gain 0x100000 (we win), and that flag
 * comes from bossHealth hitting 0 -- the ten snowman heads. Our own rider
 * crossing the line does nothing whatever, so boost cannot win it either.
 *
 * What wins it is hitting the boss ten times, and that is the boss pilot
 * (port/src/debug/boss_pilot.c). This ladder is the pilot's supply line: rung 0
 * is the pure mechanic -- throw what the course gives us -- and each rung after
 * a loss shortens the interval at which the pilot hands the rider another star
 * while it is empty-handed. The boost column stays small and constant: on a
 * boss the only thing speed buys is staying in throwing range. */
static const struct { s16 boost, supply; } nav_boss_ladder[] = {
    { 14, 0 }, { 28, 180 }, { 28, 120 }, { 42, 60 }, { 56, 30 },
};

/* Which courses are boss courses is not a list: it is asked of the race.
 * Guessing it by level number was wrong twice over -- course 7 reports
 * RACE_TYPE_BOSS_JUNGLE (1), the type that is won by reaching the line, not the
 * health type its level file's name suggests, and course 6 is an ordinary race
 * whose three rivals all carry isBossRacer. race_dbg records each course's real
 * raceType at the autoplay handoff, so from the second visit on the ladder
 * knows; the first visit falls back to the one course measured by hand.
 *
 * All three boss types share this ladder because on any of them the boss's
 * speed is scripted (baseMaxSpeed 0) and the rival levers reach nothing. What
 * differs is which column pays: supply for a health boss, boost for a
 * race-to-the-line one, and hitting the boss helps both -- a hovering boss has
 * its velocity zeroed, which is how a rider that cannot out-run it gets by. */
static int nav_level_is_boss(int level) {
    extern int sbk_level_race_type[16];
    extern int sbk_is_boss_race(int);
    int t = (level >= 0 && level < 16) ? sbk_level_race_type[level] : -1;
    if (t >= 0) return sbk_is_boss_race(t);
    return level == 3;
}

#define NAV_LADDER_TOP(boss)                                                                                       \
    ((boss) ? (int)(sizeof(nav_boss_ladder) / sizeof(nav_boss_ladder[0])) - 1                                        \
            : (int)(sizeof(nav_ladder) / sizeof(nav_ladder[0])) - 1)

/* Which course the ladder is armed for. It is declared here, above
 * nav_ladder_set, because the two ladders are chosen by course. */
static int nav_level = -1;

/* The wedge relief is kept apart from the ladder's own, because the two are
 * raised by different things and neither may quietly undo the other: climbing a
 * rung must not hand the items back to a course that is wedging on them. */
static int nav_wedge_relief;

static void nav_ladder_set(int rung) {
    int boss = nav_level_is_boss(nav_level);
    extern int sbk_boss_supply;
    if (rung < 0) rung = 0;
    if (rung > NAV_LADDER_TOP(boss)) rung = NAV_LADDER_TOP(boss);
    if (boss) {
        sbk_campaign_boost = nav_boss_ladder[rung].boost;
        sbk_boss_supply = nav_boss_ladder[rung].supply;
        sbk_rival_tax = 0;
        sbk_rival_item_relief = 0;
        sbk_item_relief = nav_wedge_relief;
        return;
    }
    sbk_boss_supply = 0;
    sbk_campaign_boost = nav_ladder[rung].boost;
    sbk_rival_tax = nav_ladder[rung].tax;
    /* The ladder's relief is the rivals' alone; the wedge's is both rows,
     * because a wedge is a task-pool problem and the pool does not care whose
     * item it is. Keeping them in separate variables is what stops climbing a
     * rung from handing the items back to a course that is wedging on them. */
    sbk_rival_item_relief = nav_ladder[rung].relief;
    sbk_item_relief = nav_wedge_relief;
}

/* --startrung N: the rung the first course after boot starts on. See main.c. */
int sbk_nav_start_rung;

/* The ladder's per-course state. It is file scope rather than nav_handicap's
 * own statics because the course has to be armed when it is *chosen*, not when
 * its first race ends: nav_handicap runs off a result screen, so a rung applied
 * only there always throws the first race of a course away. nav_level_begin is
 * therefore called from the course list too, and is idempotent. */
static int nav_rung, nav_losses, nav_wedges;

static void nav_level_begin(int level) {
    /* A course the save already records as raced-and-not-won does not deserve
     * a rung-0 attempt.
     *
     * awaitRaceResult writes levelUnlockStatus[level] = 4 for a race that
     * finished outside first place, so a 4 is the game's own note that this
     * course has beaten this rider before. Starting it at rung 0 anyway spends
     * five minutes re-learning what the EEPROM already knows -- and the
     * campaign did exactly that on course 1, losing at rung 0 and again at rung
     * 1 before the handicap that finally won it. Rung 1 is as far as this goes:
     * the boost-only rung, the mildest there is, so a course that was lost
     * narrowly still gets a nearly-honest race. */
    int known_lost;
    if (level < 0 || level == nav_level) return;
    known_lost = EepromSaveData != NULL && level < 15 && EepromSaveData->levelUnlockStatus[level] == 4;
    nav_level = level;
    nav_rung = sbk_nav_start_rung;
    if (known_lost && nav_rung < 1) nav_rung = 1;
    sbk_nav_start_rung = 0; /* a resume, not a floor: one course only */
    nav_losses = nav_wedges = 0;
    nav_wedge_relief = 0;
    nav_ladder_set(nav_rung);
    if (nav_rung != 0) {
        printf("sbk-nav: level %d starts at rung %d%s (boost=%d rivaltax=%d rivalrelief=%d)\n", level, nav_rung,
               known_lost ? " -- the save records it as lost before" : "", sbk_campaign_boost, sbk_rival_tax,
               sbk_rival_item_relief);
        fflush(stdout);
    }
}

static void nav_handicap(int level, int place) {
    nav_level_begin(level);

    /* A wedge is not a loss: the rider never finished, so the race says nothing
     * about whether the ladder is high enough. Climbing on one would also climb
     * *towards* the setting that caused it, since the boost is what wedges. */
    if (sbk_campaign_wedge) {
        sbk_campaign_wedge = 0;
        nav_wedges++;
        /* The rung is kept. The first wedge on course 1 came at a rung whose
         * boost had already finished a race, and its signature -- lap 0, a
         * fixed sector, the position byte-identical frame to frame -- is the
         * chairlift wedge of docs/nightmare-row.md, not the boost. Dropping a
         * rung on it only threw away a handicap that had never been tried, and
         * the ladder oscillated between the two.
         *
         * From the second wedge on the same course the item chance comes down
         * instead, which is the documented cause: the way out of the lift wait
         * is a scheduleTask, and item spam is what fills the pool it needs.
         *
         * The relief starts on the *first* wedge, not the second: course 1
         * wedged at three different rungs, twice at the same sector and the
         * same world position to the byte, so a second identical race only buys
         * the same wedge again at the cost of six minutes. */
        nav_wedge_relief += 55;
        nav_ladder_set(nav_rung);
        printf("sbk-nav: level %d wedged (%d so far); not a loss, retrying at rung %d "
               "(boost=%d rivaltax=%d itemrelief=%d)\n",
               level, nav_wedges, nav_rung, sbk_campaign_boost, sbk_rival_tax, sbk_item_relief);
        fflush(stdout);
        return;
    }

    if (place == 0) {
        if (nav_rung != 0) {
            printf("sbk-nav: level %d won at rung %d (boost=%d rivaltax=%d) after %d loss(es); back to rung 0\n",
                   level, nav_rung, sbk_campaign_boost, sbk_rival_tax, nav_losses);
        }
        nav_rung = nav_losses = nav_wedges = 0;
        nav_wedge_relief = 0;
        nav_ladder_set(0);
        fflush(stdout);
        return;
    }

    nav_losses++;
    if (nav_rung < NAV_LADDER_TOP(nav_level_is_boss(nav_level))) {
        nav_rung++;
        nav_ladder_set(nav_rung);
        printf("sbk-nav: level %d lost %d time(s); retrying at rung %d (boost=%d +%d%% top speed, rivaltax=%d, "
               "rivalrelief=%d)\n",
               level, nav_losses, nav_rung, sbk_campaign_boost, sbk_campaign_boost * 100 / 256, sbk_rival_tax,
               sbk_rival_item_relief);
    } else {
        printf("sbk-nav: WARNING -- level %d lost %d time(s) at the top of the handicap ladder "
               "(boost=%d rivaltax=%d rivalrelief=%d wedgerelief=%d); the rider cannot win this course\n",
               level, nav_losses, sbk_campaign_boost, sbk_rival_tax, sbk_rival_item_relief, sbk_item_relief);
    }
    fflush(stdout);
}

/* A race has ended when a result handler comes up. The purse is in
 * gGameSessionContext->gold; it only reaches the EEPROM through the map's save
 * point, so after every N races the navigator aims the rider at it. */
/* Is one of the race's result screens up?
 *
 * This used to be three fragments -- "GameResult", "ContinuePress",
 * "AwardGold" -- and it silently could not see a boss race. The boss's result
 * handlers are handleBossRaceResult, handleBossDefeatResult and
 * awaitBossResultAndFadeOut, and not one of those contains any of the three:
 * "BossRaceResult" is a RaceResult, not a GameResult. So the navigator watched
 * the Jingle Town boss finish, never counted the race, never saved, never
 * climbed the handicap ladder, and sent the rider straight back in at the same
 * rung -- a loop with a real race in it every time, which is exactly the shape
 * the town-exit guard cannot catch.
 *
 * Matching "Result" outright is the fix, with one exclusion that matters: the
 * skill game's HUD has init/update/cleanupSkillGameResultTimerDisplay, and
 * those run *during* the race. A rule keyed on the word alone would call a
 * race finished the moment it started. */
static int menu_is_result(void) {
    /* Two kinds of name carry "Result" and are not a result screen.
     *
     * The funnels are the dangerous ones. loadRace and loadStoryModeRace set
     * awaitRaceResult / awaitStoryModeRaceResult as the game state *before*
     * initRace is even queued (session_manager.c:131, race_state_machine.c:180),
     * so those two are up for the whole race and the whole cutscene ahead of
     * it. Counting them cost this campaign a lap: the navigator announced
     * "race finished" the instant the course list was confirmed, and then --
     * because the funnel stays up until the race really ends -- the true
     * result screen was never a fresh rising edge, so the actual finish went
     * unseen. A race that is reported finished before it starts is worse than
     * one that is never reported at all.
     *
     * The other kind is the skill game's HUD: init/update/cleanup of
     * SkillGameResultTimerDisplay run *during* the race. */
    static const char *const not_a_result[] = {
        "awaitRaceResult", "awaitStoryModeRaceResult", "awaitVersusRaceResult", NULL,
    };
    int i, j;
    for (i = 0; i < cur_count; i++) {
        const char *n = sbk_fn_name((void *)cur_fn[i]);
        int skip = 0;
        if (strstr(n, "TimerDisplay") != NULL) continue;
        for (j = 0; not_a_result[j] != NULL; j++) {
            if (strcmp(n, not_a_result[j]) == 0) { skip = 1; break; }
        }
        if (skip) continue;
        if (strstr(n, "Result") != NULL || strstr(n, "ContinuePress") != NULL || strstr(n, "AwardGold") != NULL)
            return 1;
    }
    return 0;
}

static void nav_watch(unsigned long retraces) {
    static int in_results;
    static unsigned long last_counted;
    int results = menu_is_result();
    /* One race can show two result handlers in succession -- a screen, then
     * session_manager's own awaitRaceResult funnel -- and if the list happens
     * to be empty of both for a frame between them that is two rising edges
     * and one race counted twice. A race takes minutes; nothing legitimate
     * finishes two of them inside ten seconds. */
    if (results && in_results == 0 && last_counted != 0 && retraces - last_counted < 600) {
        in_results = results;
        return;
    }
    if (results && !in_results) {
        last_counted = retraces;
        /* The place the rider finished in decides everything downstream:
         * handleSpeedCrossGameResult / handleBossRaceResult return 3 only for
         * finishPosition 0, and only a 3 (or a 5) turns the course's
         * levelUnlockStatus into the 1 that lets the next gate open. A campaign
         * that never prints the place cannot tell a stall from a loss. */
        GameState *gs = sbk_race_state();
        int place = nav_place_of(gs);
        int level = gGameSessionContext ? gGameSessionContext->currentLevel : -1;
        if (place < 0) place = nav_latched_place;
        if (level < 0) level = nav_latched_level;
        nav_latched_place = nav_latched_level = -1;
        nav_races++;
        if (sbk_autonav_every > 0 && nav_races % sbk_autonav_every == 0) nav_want = NAV_SAVE;
        printf("sbk-nav: race %d finished on level %d, place=%d (%s), gold=%d, want=%s\n", nav_races, level, place + 1,
               place == 0 ? "WON" : "lost", gGameSessionContext ? (int)gGameSessionContext->gold : -1,
               nav_want == NAV_SAVE ? "SAVE" : "RACE");
        nav_handicap(level, place);
        fflush(stdout);
    }
    in_results = results;
}

/* The file select, which is both the game's entry point (pick a slot to play)
 * and its only writer of the EEPROM (the map's save point comes back here with
 * hasCurrentSaveData set).
 *
 *   saveSlotMenuState 1    the three-slot list, cursor selectedSaveSlot
 *   saveSlotMenuState 3    confirm, and the A here is what writes
 *   saveSlotMenuState 0x33 the YES/NO dialog, cursor saveSlotDialogSelection
 *
 * Every one of these defaults to the answer that backs out, and a queued A
 * lands on the prompt the frame it appears -- so park the choice, exactly as
 * the first game's navigator had to.
 */
static void nav_save_screen(SaveSlotScreenState *st) {
    if (st == NULL) return;
    if (st->saveSlotMenuState == 1) {
        st->selectedSaveSlot = 0; /* always slot 1 */
    } else if (st->saveSlotMenuState == 0x33) {
        if (st->saveSlotDialogType == 0xA) {
            st->saveSlotDialogSelection = 0; /* STORY, not EXPERT */
        } else if (st->hasCurrentSaveData == 0) {
            /* Entering the game: 1 is "use a saved file" and only exists when
             * one does; 0 starts a new game. */
            st->saveSlotDialogSelection = (u8)(st->numValidSlots != 0 ? 1 : 0);
        } else if (nav_saves_pending()) {
            st->saveSlotDialogSelection = 0; /* SAVE */
        } else {
            st->saveSlotDialogSelection = 1; /* done: leave */
        }
    }
    /* The write has landed when the EEPROM operation reports success. */
    if (st->hasCurrentSaveData != 0 && st->eepromOperationStatus == 1 && nav_want == NAV_SAVE) {
        nav_saves++;
        nav_want = NAV_RACE;
        printf("sbk-nav: saved slot %d, gold=%d (save #%d)\n", st->selectedSaveSlot,
               gGameSessionContext ? (int)gGameSessionContext->gold : -1, nav_saves);
        fflush(stdout);
    }
}

static void nav_act(unsigned long retraces) {
    static char last_screen[768];
    static unsigned long screen_since;
    const char *now = sbk_menu_names();

    if (strcmp(now, last_screen) != 0) {
        snprintf(last_screen, sizeof(last_screen), "%s", now);
        screen_since = retraces;
    }
    if (retraces - nav_last_action < 60) return;
    nav_last_action = retraces;

    /* An unattended session must not sit on a screen it does not understand. */
    if (retraces - screen_since > 3600) {
        printf("sbk-nav: stuck on %s for %lu retraces, backing out\n", now, retraces - screen_since);
        fflush(stdout);
        screen_since = retraces;
        nav_press("press B 3");
        return;
    }

    /* The credits -- the end of the campaign, and the one screen the navigator
     * must not touch. It is reached from awaitPostRaceCutscene once the last
     * story course (currentLevel 0xB) has been won, and every name in the chain
     * carries "redits": loadCreditsSequence / initCreditsController /
     * updateCreditsSequence / fadeOutCreditsSequence / awaitCreditsSequence, and
     * then loadPostCreditsSaveScreen, which puts the file select back up.
     *
     * The generic A below would skip them, so this branch takes its hands off
     * and instead arms a save, so the run that reached the credits also records
     * that it did (awaitCreditsSequence sets postCreditsCutscenePending, and
     * loadPostCreditsSaveScreen exists precisely so the player can keep it).
     * The save select's own branch, above, still drives that screen. */
    if (menu_has("redits") && !sbk_menu_on("updateSaveSlotSelectionScreen")) {
        if (!nav_credits_seen) {
            nav_credits_seen = 1;
            nav_want = NAV_SAVE;
            nav_progress("credits");
            printf("sbk-nav: ***** CREDITS ***** the campaign is finished after %d races, gold=%d\n", nav_races,
                   gGameSessionContext ? (int)gGameSessionContext->gold : -1);
            fflush(stdout);
        }
        return;
    }

    /* The cutscenes. The sequel plays a long one before and after every story
     * race (the pre-race one alone is 55 s), and the only way out is START --
     * which a navigator must never queue, because a START still in flight when
     * the next race begins pauses it. So the *state* is parked instead:
     * CUTSCENE_STATE_SKIP_START is exactly what the button sets
     * (src/effects/cutscene_keyframes.c), fade and all. */
    if (sbk_menu_on("updateCutscenePlayback")) {
        CutsceneTaskMemory *c = (CutsceneTaskMemory *)sbk_menu_alloc("updateCutscenePlayback");
        if (c != NULL && c->playbackState == CUTSCENE_STATE_PLAYING) {
            c->playbackState = CUTSCENE_STATE_SKIP_START;
            printf("sbk-nav: cutscene skipped\n");
            fflush(stdout);
        }
        return;
    }

    /* The title: START is entry 0, and a monkey that drifts onto TRAINING or
     * OPTION never comes back to the campaign. */
    if (sbk_menu_on("handleTitleMenuInput")) {
        TitleScreenState *t = (TitleScreenState *)sbk_menu_alloc("handleTitleMenuInput");
        if (t != NULL && t->menuMode == 0) t->menuSelection = 0;
        nav_press("press A 3");
        return;
    }

    /* The course list. It keeps a *cursor index* into its own levelIdList[] in
     * selectedIndex and only turns it into gGameSessionContext->currentLevel
     * when the choice is confirmed -- the same shape as the first game's
     * character-select course menu. So a trial parks the cursor on the course
     * it wants and lets the navigator's own A press confirm it. */
    if (sbk_menu_on("handleLevelSelectInput")) {
        LevelSelectState *ls = (LevelSelectState *)sbk_menu_alloc("handleLevelSelectInput");
        int want = sbk_nav_target_level >= 0 ? sbk_nav_target_level : nav_next_story_level();
        /* Arm the ladder for the course being chosen, not for the one whose
         * result screen last went by. This is the only place the campaign
         * knows what it is about to race *before* it races it, and a course
         * the save marks as lost has to start above rung 0 or the first five
         * minutes are spent proving what the EEPROM already recorded. */
        nav_level_begin(want);
        if (ls != NULL && want >= 0 && ls->menuState == 0) {
            int i;
            for (i = 0; i < ls->maxLevelCount && i < 12; i++) {
                if (ls->levelIdList[i] == (u8)want) {
                    if (ls->selectedIndex != (s8)i) {
                        static int said = -1;
                        ls->selectedIndex = (s8)i;
                        ls->selectedLevelId = (u8)want;
                        ls->previousLevelId = (u8)want;
                        if (said != want) {
                            said = want;
                            printf("sbk-nav: level list: cursor -> %d (level %d of %d offered)\n", i, want,
                                   ls->maxLevelCount);
                            fflush(stdout);
                        }
                    }
                    break;
                }
            }
        }
        nav_press("press A 3");
        return;
    }

    if (sbk_menu_on("updateSaveSlotSelectionScreen")) {
        nav_save_screen((SaveSlotScreenState *)sbk_menu_alloc("updateSaveSlotSelectionScreen"));
        nav_press("press A 3");
        return;
    }

    /* The story overworld proper (src/core/game_state_init.c). Jingle Town is a
     * walkable 3D town: a trigger fires when the rider reaches a building, sets
     * locationDiscovered / discoveredLocationId on the map's own GameState, and
     * the travel task then writes unk427 = id + 1, which is what
     * gameStateCleanupHandler turns into
     * storyMapLocationIndex (src/story/map_state.c dispatches
     * storyMapLocationHandlers[] off it). A bot cannot be asked to walk across
     * a town, so the navigator parks the pair the trigger would have written:
     *
     *   id 6 -> handler 7  initSaveSlotScreen, the save point -- the game's
     *                      ONLY writer of the EEPROM
     *   id 3 -> handler 4  loadOverlay_1BBA0, which is NOT the ski-area map: it
     *                      is the rider picker on a map of Jingle Town, and its
     *                      onStoryMapNormalExit returns the "go to the course
     *                      list" code 0x44 only while
     *                      EepromSaveData->levelUnlockStatus[0] == 5, i.e. only
     *                      until the first course has been won. After that it
     *                      returns 1, handleStoryMapLocationComplete puts the
     *                      rider back in the town, and a navigator that keeps
     *                      asking for it loops for ever. That was this file's
     *                      second bug, and 177 laps of it are in the log.
     *   id 2/5/8 -> the Speed / X / Shot Cross minigames (handleGameStateComplete
     *                      intercepts handlers 3, 6 and 9 and sets currentLevel
     *                      0xD / 0xE / 0xC itself)
     *
     * A story *course* is not a location at all: it is reached by leaving the
     * town (STORY_LEAVE_TOWN above), which is what the navigator asks for.
     */
    if (sbk_menu_on("gameStateCleanupHandler")) {
        GameState *m = (GameState *)sbk_menu_alloc("gameStateCleanupHandler");
        if (m != NULL && m->unk427 == 0) {
            int cross;
            if (nav_want == NAV_SAVE) {
                m->discoveredLocationId = STORY_SAVE_LOCATION;
                m->locationDiscovered = 1;
                m->unk427 = (u8)(STORY_SAVE_LOCATION + 1);
                printf("sbk-nav: town -> save point (location %d, handler %d)\n", STORY_SAVE_LOCATION,
                       STORY_SAVE_LOCATION + 1);
            } else if (nav_next_story_level() < 0 && (cross = nav_next_cross()) >= 0) {
                /* Nothing left on the course list means the campaign is at the
                 * slot-10 gate, which wants the three Cross minigames won. They
                 * are entered from the town like any other building. */
                m->discoveredLocationId = (u8)cross;
                m->locationDiscovered = 1;
                m->unk427 = (u8)(cross + 1);
                printf("sbk-nav: town -> Cross minigame (location %d, handler %d)\n", cross, cross + 1);
            } else {
                m->unk427 = STORY_LEAVE_TOWN;
                nav_town_exits++;
                printf("sbk-nav: town -> leave for the course list (exit #%d, since race %d)\n", nav_town_exits,
                       nav_races);
            }
            fflush(stdout);
        }
        return;
    }

    /* The rider picker on the town map (overlay 1BBA0). Nine riders laid out as
     * a 3x3, and the sequence its state machine wants is A (take the rider
     * under the cursor, selectionState 0 -> 10), seventeen frames of animation
     * (10 -> 1), everyone ready (1 -> 3), then A again to confirm (3 ->
     * allConfirmed). Two A presses a second apart is exactly that, so the
     * navigator just presses -- and the screen is only ever seen once, on the
     * way into a new game, because after the first course is won its exit stops
     * leading to the course list. */
    if (sbk_menu_on("storyMapHandlePlayerInput")) {
        nav_press("press A 3");
        return;
    }

    nav_press("press A 3");
}

/* --unlockall: keep the game's own cheat applied.
 *
 * The level list only offers courses whose levelUnlockStatus is non-zero
 * (buildUnlockedLevelList, src/story/story_intro.c), so on the scratch save a
 * trial runs with, `--trial level=N` can only ever aim at course 0. Rather than
 * hand-write a save file, this runs unlockAllContent (src/ui/title_screen.c) --
 * the cheat the title screen already has -- and re-runs it whenever the game
 * overwrites the block, which loadSaveData and resetSaveDataToDefaults both do
 * on the way in. Pair it with --eeprom or --nopak; on the user's real save it
 * would erase the campaign's progression. */
int sbk_unlockall;

static void nav_unlockall(void) {
    extern void unlockAllContent(void);
    static int said;
    if (EepromSaveData == NULL || EepromSaveData->levelUnlockStatus[1] == 1) return;
    unlockAllContent();
    if (!said) {
        said = 1;
        printf("sbk-nav: --unlockall: every course and board offered (the game's own unlockAllContent)\n");
        fflush(stdout);
    }
}

void sbk_menu_nav_tick(unsigned long retraces) {
    collect();
    if (sbk_unlockall) nav_unlockall();
    if (sbk_menutrace) menutrace(retraces);
    if (!sbk_autonav) return;
    /* The ladder has to be standing before the first race, not just after the
     * first result: nav_handicap only runs when a race ends, so a --startrung
     * applied there alone would still lose one race at rung 0 -- exactly the
     * five minutes the flag exists to save. */
    {
        static int armed;
        if (!armed) {
            armed = 1;
            nav_ladder_set(sbk_nav_start_rung);
            if (sbk_nav_start_rung != 0) {
                printf("sbk-nav: --startrung %d: boost=%d rivaltax=%d rivalrelief=%d before the first race\n",
                       sbk_nav_start_rung, sbk_campaign_boost, sbk_rival_tax, sbk_rival_item_relief);
                fflush(stdout);
            }
        }
    }
    nav_watch(retraces);
    nav_progress("tick");
    /* Clear the loop guard on the rising edge of a *story* race only. The
     * attract demo runs this same handler, so the demo is excluded; and the
     * edge has to be evaluated on *every* tick, not only on the ticks that see
     * a race.
     *
     * That last part was the third bug this counter had. The test used to live
     * inside the `if (racing)` branch below, so `was_racing` was only ever
     * assigned while a race was on screen -- it latched at 1 when the first
     * story race ended and nothing ever cleared it, so no later race could be a
     * rising edge and the counter was never reset again. It showed up in the
     * log as "exit #2, since race 2" where the second race should have put it
     * back to #1: harmless in itself, but it means the count creeps up by one
     * per course until it crosses the threshold and cries "looping" at a
     * campaign that is doing exactly what it should. A guard that fires on a
     * healthy run is worse than no guard, because the next person to read the
     * log believes it. */
    {
        static int was_racing;
        int racing = 0;
        if (sbk_menu_on("handleRaceStateUpdate")) {
            GameState *gs = sbk_race_state();
            racing = gs != NULL && !sbk_race_is_demo(gs);
            if (racing && (gs->players[0].animationFlags & PLAYER_FINISHED_FLAG)) {
                nav_latched_place = nav_place_of(gs);
                nav_latched_level = gGameSessionContext ? gGameSessionContext->currentLevel : -1;
            }
        }
        if (racing && !was_racing) {
            nav_town_exits = 0;
            nav_loop_warned = 0;
            nav_latched_place = nav_latched_level = -1;
        }
        was_racing = racing;
    }
    /* Hands off the pad only while a race is actually being played -- including
     * the attract demo, which must be left to run itself out. */
    if (sbk_menu_on("handleRaceStateUpdate")) return;
    /* A race should follow the very next town exit. More than a handful without
     * one means the campaign is going round in a circle again, and an
     * unattended run should say so -- with the save's own progress table, which
     * is what a diagnosis needs -- rather than fill the log with menu names. */
    if (nav_town_exits > 6 && !nav_loop_warned) {
        nav_loop_warned = 1;
        printf("sbk-nav: WARNING -- %d town exits since race %d and no race started; "
               "the campaign is looping\n",
               nav_town_exits, nav_races);
        nav_progress("loop");
        fflush(stdout);
    }
    nav_act(retraces);
}
