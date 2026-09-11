/* Game-specific debug hooks.
 *
 * In the first game these are port/src/debug/race_dbg.c (--autoplay, --trial,
 * --status, --nightmare) and menu_nav.c (--autonav): both reach deep into that
 * game's own menu and race state by symbol name, so none of it carries over.
 * The sequel's equivalents have to be written against its own state; until
 * then the switches parse and do nothing, which keeps the host loop and the
 * command line identical between the two ports. */
#include <stdio.h>

int sbk_autoplay;
int sbk_soak;
int sbk_nightmare;
int sbk_dumpon;
int sbk_race_debug_enabled;
int sbk_status;
int sbk_course_trace;
int sbk_autonav;
int sbk_autonav_shop;
int sbk_autonav_every = 1;
int sbk_menutrace;

static int warned;

static void not_yet(const char *what) {
    if (!warned) {
        warned = 1;
        printf("sbk: %s is not implemented for Snowboard Kids 2 yet (port/src/debug/game_hooks.c)\n", what);
    }
}

int sbk_trial_parse(const char *spec) {
    (void)spec;
    not_yet("--trial");
    return 0;
}

int sbk_plan_parse(const char *spec) {
    (void)spec;
    not_yet("--plan");
    return 0;
}

int sbk_peek_add(const char *spec) {
    (void)spec;
    not_yet("--peek");
    return 0;
}

void sbk_autoplay_tick(unsigned long retraces) {
    (void)retraces;
    if (sbk_autoplay || sbk_autonav) {
        not_yet("--autoplay/--autonav");
    }
}

void sbk_race_debug(unsigned long retraces) {
    (void)retraces;
}
