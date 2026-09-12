/* The boss pilot: how self-play beats a Snowboard Kids 2 boss race.
 *
 * A boss race is not a race. src/race/race_main.c ~5188 gives RACE_TYPE_BOSS_JINGLE
 * and RACE_TYPE_BOSS_ICE exactly two outcomes:
 *
 *   - the boss (rider 1) reaches the line  -> showPlacementAnnouncement(0, 2),
 *     both riders flagged finished, gRaceResultCode 4: we lost;
 *   - the boss's animationFlags gain 0x100000 -> showPlacementAnnouncement(0, 1),
 *     victory snowflakes, gRaceResultCode 3: we won.
 *
 * Our own rider crossing the line does nothing at all. That is why the campaign
 * lost the Jingle Town boss at every rung of the handicap ladder: the ladder's
 * levers are top speed and the rivals' item rate, and neither can reach an
 * outcome that is decided by 0x100000.
 *
 * 0x100000 is set by src/levels/jingle_town_boss.c (and its Ice Land twin) when
 * bossHealth reaches 0 -- the ten snowman heads along the bottom of the screen,
 * initialised to 0xA in initJingleTownBoss. Health only ever falls in the hover
 * phases, and the hover phases are only entered from updateJingleTownBoss's
 * hitReactionState test: 0x3D or 0x3E, one head each.
 *
 * Following those two states back through src/race/hit_reactions.c:
 *
 *   0x3D  setPlayerStarHitState        <- checkStarProjectileHit
 *                                         (obstacle_sprites.c, projectile type 4)
 *   0x3E  setPlayerBouncedBackState    <- descendWarpEffect (the frying-pan
 *                                         secondary item) and a couple of level
 *                                         hazards
 *
 * Every other primary item lands a state the boss ignores -- parachute 0x34,
 * shrink 0x35, panel 0x36, frozen 0x3C. So of the seven throwables exactly one,
 * the star (primaryItemId 5, projectile type 4), takes a head off. The pans do
 * too, and the CPU rider already throws those on its own.
 *
 * What the CPU rider will not do is throw a star at the boss. findPrimaryItemTarget
 * (hit_reactions.c ~522) finds the boss, rolls its altChance, and then runs
 *
 *      i = player->finishPosition;
 *      for (; i >= 0; i--) if (players[rankOrder[i]].isBossRacer == 0) break;
 *      if (i >= 0) return -1;
 *
 * -- "don't waste it on the boss while a real rider is still ahead of you". The
 * scan starts at our *own* rank and rankOrder[our rank] is us, so it always
 * breaks on the first step and always returns -1. In a two-rider boss race a CPU
 * rider can therefore never throw at the boss, and self-play is a CPU rider.
 *
 * So the pilot hooks processPlayerItemUsage (port/patches.txt) and answers that
 * one question differently for our rider on a boss course: boss in range and in
 * the game's own firing window -> return the targeting mode, and the game's own
 * code does the rest, spawnAttackProjectile -> star -> collision ->
 * setPlayerStarHitState -> one head. Nothing about the projectile, the hit test,
 * the boss or the result is faked; the pilot only decides when to press the
 * button.
 *
 * The supply is the honest handicap. The boss courses have no item boxes: a
 * handful of items lie on the ground and the borrowed CPU path does not steer to
 * them (processItemTriggers wants the rider within 0x100000 of the item). A
 * rider that never picks up a star has nothing to throw. When sbk_boss_supply is
 * non-zero the pilot hands our rider a star every sbk_boss_supply retraces while
 * it is empty-handed, and says so in the log; the navigator's boss ladder starts
 * at 0 -- pick-ups only -- and only opens the supply after a loss. Every star the
 * pilot conjures is counted and printed at the end of the race next to the ones
 * the rider actually collected, so a run's log always says how much of the win
 * was the game's and how much was ours.
 *
 * The Crazy Jungle boss (RACE_TYPE_BOSS_JUNGLE, course 0xB) is a different
 * animal: it has no bossHealth at all and race_main.c decides it on who reaches
 * the line first. The pilot leaves it alone -- there the speed ladder is the
 * right lever.
 */
#include "../ultra/ultra.h"
#include <stdio.h>
#include "common.h"
#include "gamestate.h"
#include "math/geometry.h"

#define RACE_TYPE_BOSS_JINGLE 2
#define RACE_TYPE_BOSS_ICE 3
#define BOSS_DEFEATED_FLAG 0x100000
#define PLAYER_FINISHED_FLAG 0x80000
#define STAR_ITEM_ID 5 /* spawnAttackProjectile type 4: the only boss-damaging throw */

int sbk_boss_pilot = 1;   /* --nobosspilot turns the whole thing off */
int sbk_boss_supply;      /* retraces between conjured stars while empty; 0 = pick-ups only */
/* How far away the pilot will throw. The star is ballistic -- it leaves at
 * 0x1B8000 a frame, drops 0x40000 a frame and lives 240 frames -- so a long
 * throw at a boss that is turning is a wasted one: the first run at Jingle Town
 * threw 21 stars from as far as 0x27C0000 and landed 5. The boss's own
 * rubber-band keeps it inside 0x1000000 of us whenever it is ahead
 * (updateJingleTownBoss), so there is no reason to fire from further than
 * that plus a margin. */
int sbk_boss_range = 0x2000000;
int sbk_boss_cooldown = 10;

extern int sbk_autoplay;

static unsigned long pilot_now;      /* fed by the host loop, see sbk_boss_pilot_tick */
static unsigned long last_throw, last_supply;
static void *pilot_gs;               /* the race this run's counters belong to */
static int n_thrown, n_supplied, n_picked, hp_seen, last_ammo, reported;

int sbk_is_hp_boss_race(int raceType) {
    return raceType == RACE_TYPE_BOSS_JINGLE || raceType == RACE_TYPE_BOSS_ICE;
}

/* rider 1 is the boss in every boss race the game builds, but look for the flag
 * rather than trust the index. */
Player *sbk_boss_rider(GameState *gs) {
    int i;
    if (gs == NULL) return NULL;
    for (i = 0; i < gs->numPlayers && i < 4; i++) {
        if (gs->players[i].isBossRacer) return &gs->players[i];
    }
    return NULL;
}

static void pilot_reset(GameState *gs, Player *boss) {
    pilot_gs = (void *)gs;
    last_throw = last_supply = pilot_now;
    n_thrown = n_supplied = n_picked = 0;
    last_ammo = 0;
    reported = 0;
    hp_seen = boss != NULL ? (int)boss->bossHealth : -1;
    printf("sbk: bosspilot: armed on level %d type=%d (boss hp=%d, supply=%d, range=%d)\n", gs->memoryPoolId,
           gs->raceType, hp_seen, sbk_boss_supply, sbk_boss_range);
    fflush(stdout);
}

/* processPlayerItemUsage's decision, second-guessed for our rider on a boss.
 * Called from game code (port/patches.txt), so getCurrentAllocation() and the
 * task scheduler are the race's own. Returns the targeting mode the game will
 * pass to spawnAttackProjectile, or -1 for "do not throw". */
s32 sbk_boss_pilot_item(GameState *gs, Player *p, s32 result) {
    Player *boss;
    s32 dx, dz, dist, err, tol;
    s32 mode;

    if (!sbk_boss_pilot || !sbk_autoplay || gs == NULL || p == NULL) return result;
    if (!sbk_is_hp_boss_race(gs->raceType)) return result;
    if (p != &gs->players[0] || !p->isCpuControlled) return result;
    if (p->animationFlags & PLAYER_FINISHED_FLAG) return result;

    boss = sbk_boss_rider(gs);
    if (boss == NULL) return result;

    /* A new race: a different allocation, or the same one handed back with the
     * ten heads restored. */
    if (pilot_gs != (void *)gs || (int)boss->bossHealth > hp_seen) pilot_reset(gs, boss);

    /* The heads, watched from the one place that is called every frame with the
     * race's own allocation in hand. A drop is a hit that landed. */
    if ((int)boss->bossHealth != hp_seen) {
        printf("sbk: bosspilot: boss hp %d -> %d (thrown=%d supplied=%d picked=%d)\n", hp_seen,
               (int)boss->bossHealth, n_thrown, n_supplied, n_picked);
        fflush(stdout);
        hp_seen = (int)boss->bossHealth;
    }
    if (boss->animationFlags & BOSS_DEFEATED_FLAG) return result;

    /* A pick-up is ammo that appeared without the pilot putting it there:
     * processItemTriggers writes 3 (9 on the Ice Land boss) straight into the
     * rider when it drives over a ground item. Counting them is how the log can
     * say what the course gave us and what we conjured. */
    if ((int)p->primaryItemAmmo > last_ammo && pilot_now != last_supply) {
        n_picked++;
        printf("sbk: bosspilot: picked up item %d x%d at r=%lu\n", (int)p->primaryItemId, (int)p->primaryItemAmmo,
               pilot_now);
        fflush(stdout);
    }
    last_ammo = (int)p->primaryItemAmmo;

    /* Empty-handed, and the ladder has opened the supply: hand over a star. */
    if (sbk_boss_supply > 0 && p->primaryItemAmmo == 0 && pilot_now - last_supply >= (unsigned long)sbk_boss_supply) {
        p->primaryItemId = STAR_ITEM_ID;
        p->primaryItemAmmo = 3;
        p->itemHudNotificationFlags |= 1;
        last_supply = pilot_now;
        last_ammo = 3;
        n_supplied++;
        printf("sbk: bosspilot: supplied star x3 (#%d) at r=%lu, boss hp=%d\n", n_supplied, pilot_now, hp_seen);
        fflush(stdout);
    }

    if (result >= 0) return result;                          /* the AI is already throwing */
    if (p->primaryItemAmmo == 0) return result;
    if (p->primaryItemId != STAR_ITEM_ID) return result;     /* nothing else marks the boss */
    if (pilot_now - last_throw < (unsigned long)sbk_boss_cooldown) return result;

    /* The firing window, in the game's own terms (findPrimaryItemTarget): the
     * angle is measured from the boss *back* to us, so "in front" is an error
     * near 0 against our own heading, and item 5 -- the star -- is the one item
     * the game also throws backwards, through a rear window at half a turn.
     * The window tightens with distance -- see the tolerance below. */
    dx = boss->worldPos.x - p->worldPos.x;
    dz = boss->worldPos.z - p->worldPos.z;
    dist = distance_2d(dx, dz);
    if (dist > sbk_boss_range) return result;

    err = (s32)((atan2Fixed(-dx, -dz) - (u16)p->rotY) & 0x1FFF);
    if (err >= 0x1000) err -= 0x2000;

    /* How far off the heading may be, worked out from the geometry rather than
     * guessed: the star flies straight, so a heading error of e radians puts it
     * dist*e to the side of where the boss is, and checkStarProjectileHit lands
     * anything inside the boss's collision node (0x1EC000) plus the star's own
     * 0xC0000. A full turn is 0x2000, so a radian is 0x2000/2pi = 1303 units.
     * Firing outside that window is how the first Jingle Town run threw 21
     * stars from as far as 0x27C0000 and landed only five. */
    tol = (s32)((1303LL * (0x1EC000 + 0xC0000)) / (dist > 0 ? dist : 1));
    if (tol > 0x200) tol = 0x200;
    if (tol < 0x40) tol = 0x40;

    if (err > -tol && err < tol) {
        mode = 0;
    } else if (err > 0x1000 - tol || err < -(0x1000 - tol)) {
        mode = 1;
    } else {
        return result;
    }

    last_throw = pilot_now;
    n_thrown++;
    printf("sbk: bosspilot: throw #%d mode=%d dist=%d err=%d tol=%d ammo=%d boss hp=%d\n", n_thrown, (int)mode,
           (int)dist, (int)err, (int)tol, (int)p->primaryItemAmmo, hp_seen);
    fflush(stdout);
    return mode;
}

/* The host loop's clock, and the end-of-race line. Called from
 * sbk_autoplay_tick every retrace with whatever race (or none) is on screen.
 * sbk_race_state() goes NULL for a few frames mid-race (the scheduler leaves
 * SCHEDULER_STATE_RUNNING at a cutaway), so this never tears the pilot down --
 * only a new race does, in the hook, where the allocation is certain. */
void sbk_boss_pilot_tick(GameState *gs, unsigned long retraces) {
    Player *boss;
    pilot_now = retraces;
    if (gs == NULL || pilot_gs != (void *)gs || !sbk_is_hp_boss_race(gs->raceType)) return;
    boss = sbk_boss_rider(gs);
    if (boss == NULL) return;
    if (!(gs->players[0].animationFlags & PLAYER_FINISHED_FLAG)) return;
    if (reported) return;
    reported = 1;
    printf("sbk: bosspilot: race over -- boss hp=%d defeated=%d, stars thrown=%d (supplied=%d, picked up=%d)\n",
           (int)boss->bossHealth, (boss->animationFlags & BOSS_DEFEATED_FLAG) ? 1 : 0, n_thrown, n_supplied, n_picked);
    fflush(stdout);
}
