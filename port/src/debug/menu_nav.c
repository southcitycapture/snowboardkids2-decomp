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
#include "../platform/input.h"

extern TaskScheduler gSchedulerListSentinel;
extern GameSessionContext *gGameSessionContext;
/* src/common_bss.c: which entry of storyMapLocationHandlers[] the map will run
 * next. 7 is initSaveSlotScreen, and the map sets it from the location the
 * rider walked into (discoveredLocationId + 1). */
extern u8 storyMapLocationIndex;
GameState *sbk_race_state(void);
int sbk_race_is_demo(const GameState *gs);

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

const char *sbk_fn_name(void *fn) {
    static char buf[160];
    Dl_info info;
    if (fn == NULL) return "-";
    if (dladdr(fn, &info) && info.dli_sname != NULL) {
        const char *n = info.dli_sname;
        if (n[0] == '_') n++; /* Mach-O underscore */
        snprintf(buf, sizeof(buf), "%s", n);
        return buf;
    }
    snprintf(buf, sizeof(buf), "%p", fn);
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
/* How many times the town has been asked to hand over to the course list. It is
 * reset by every race that actually starts, so it doubles as the detector for
 * the loop this navigator used to run: a screen chain that keeps *changing*
 * never trips the "stuck on one screen" timer below, and 177 laps went by
 * unnoticed before the exit was understood. */
static int nav_town_exits;
static unsigned long nav_last_action;

static void nav_press(const char *line) { sbk_input_play_add(line); }

static int nav_saves_pending(void) { return nav_want == NAV_SAVE; }

/* A race has ended when a result handler comes up. The purse is in
 * gGameSessionContext->gold; it only reaches the EEPROM through the map's save
 * point, so after every N races the navigator aims the rider at it. */
static void nav_watch(void) {
    static int in_results;
    int results = menu_has("GameResult") || menu_has("ContinuePress") || menu_has("AwardGold");
    if (results && !in_results) {
        nav_races++;
        if (sbk_autonav_every > 0 && nav_races % sbk_autonav_every == 0) nav_want = NAV_SAVE;
        printf("sbk-nav: race %d finished, gold=%d, want=%s\n", nav_races,
               gGameSessionContext ? (int)gGameSessionContext->gold : -1, nav_want == NAV_SAVE ? "SAVE" : "RACE");
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
        if (ls != NULL && sbk_nav_target_level >= 0 && ls->menuState == 0) {
            int i;
            for (i = 0; i < ls->maxLevelCount && i < 12; i++) {
                if (ls->levelIdList[i] == (u8)sbk_nav_target_level) {
                    if (ls->selectedIndex != (s8)i) {
                        static int said = -1;
                        ls->selectedIndex = (s8)i;
                        ls->selectedLevelId = (u8)sbk_nav_target_level;
                        ls->previousLevelId = (u8)sbk_nav_target_level;
                        if (said != sbk_nav_target_level) {
                            said = sbk_nav_target_level;
                            printf("sbk-nav: level list: cursor -> %d (level %d of %d offered)\n", i,
                                   sbk_nav_target_level, ls->maxLevelCount);
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
            if (nav_want == NAV_SAVE) {
                m->discoveredLocationId = STORY_SAVE_LOCATION;
                m->locationDiscovered = 1;
                m->unk427 = (u8)(STORY_SAVE_LOCATION + 1);
                printf("sbk-nav: town -> save point (location %d, handler %d)\n", STORY_SAVE_LOCATION,
                       STORY_SAVE_LOCATION + 1);
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

void sbk_menu_nav_tick(unsigned long retraces) {
    collect();
    if (sbk_menutrace) menutrace(retraces);
    if (!sbk_autonav) return;
    nav_watch();
    /* Hands off the pad only while a race is actually being played. */
    if (sbk_menu_on("handleRaceStateUpdate")) {
        nav_town_exits = 0;
        return;
    }
    /* A race should follow the very next town exit. More than a handful without
     * one means the campaign is going round in a circle again, and an
     * unattended run should say so rather than fill the log with menu names. */
    if (nav_town_exits > 6) {
        static int said;
        if (!said) {
            said = 1;
            printf("sbk-nav: WARNING -- %d town exits since race %d and no race started; "
                   "the campaign is looping\n",
                   nav_town_exits, nav_races);
            fflush(stdout);
        }
    }
    nav_act(retraces);
}
