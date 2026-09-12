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
#include <string.h>
#include "common.h"
#include "gamestate.h"
#include "math/geometry.h"
#include "levels/snowboard_street_shoot_cross.h"
#include <math.h>

#define RACE_TYPE_BOSS_JUNGLE 1
#define RACE_TYPE_BOSS_JINGLE 2
#define RACE_TYPE_BOSS_ICE 3
#define RACE_TYPE_SHOOT_CROSS 5
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
/* A ceiling on the handicap. The Ice Land run supplied 66 pans across one long
 * race, which is far more than the course itself holds and more than the
 * mechanic needs -- ten heads is ten hits. Sixteen leaves room for the misses
 * and stops the supply becoming the whole game. */
int sbk_boss_supply_max = 16;
int sbk_boss_range = 0x2000000;
int sbk_boss_cooldown = 10;

extern int sbk_autoplay;

static unsigned long pilot_now;      /* fed by the host loop, see sbk_boss_pilot_tick */
static unsigned long last_throw, last_supply;
static void *pilot_gs;               /* the race this run's counters belong to */
static u32 last_frame;
static int n_thrown, n_supplied, n_pans, n_picked, hp_seen, last_ammo, reported;

/* ------------------------------------------------------------ the Shot Cross
 *
 * Shot Cross (level 0xD, RACE_TYPE_SHOOT_CROSS) is the same shape of problem as
 * a boss, arrived at from the other side. `initRace` gives it totalRacers = 1,
 * so findPrimaryItemTarget's scan over the other riders finds nobody, returns
 * -1, and the CPU rider never fires. The course is twenty targets that all have
 * to be hit -- `handleShotCrossGameResult` gives gRaceResultCode 5 only for
 * `shootCrossTargetsHit == 0x14`, and nineteen still records a loss -- so a
 * rider that never fires cannot pass it, and slot 10 (the last two courses, and
 * so the credits) never opens.
 *
 * The item is primaryItemId 7: spawnAttackProjectile type 6,
 * spawnGhostTargetProjectileTask.
 *
 * The first pilot here just held the trigger down on a cooldown, on the reading
 * that the course aims for you. It does not, and the run said so: 58 shots,
 * one target. Two things in that reading were wrong.
 *
 *   - activateShootCrossTargets calls checkPositionPlayerCollisionWithPull on
 *     every target, and that function does not pull the rider *towards*
 *     anything: it is the ordinary point-vs-player collision
 *     (track_collision.c ~1035) and it pushes the rider *out*. A target is a
 *     bollard, not a magnet.
 *   - launchGhostTargetProjectile does ignore the targeting mode, but that
 *     only means the shot leaves straight along the rider's own model
 *     transform. Straight ahead is a direction, and the twenty targets are
 *     placed to the sides of the street. A rider driving the racing line and
 *     firing forwards is shooting down the middle of the road.
 *
 * So this pilot aims, in the same terms the boss pilot aims in. Every frame it
 * walks gs->shootCrossTargets->targets -- twenty {s8 state; Vec3i position}
 * records, state 0 until checkProjectileTargetHit writes 1 -- and works out,
 * for each one still standing, how far off the rider's heading it is:
 *
 *     err = atan2Fixed(-dx, -dz) - rotY          (0x2000 to the turn)
 *
 * and how wide the window at that distance is. The shot flies straight at
 * 0x1D0000 a frame and checkProjectileTargetHit forgives 0x80000 + 0x140000 =
 * 0x1C0000 around a target, so a heading error of e puts the shot dist*e to the
 * side and the window is 1303 * 0x1C0000 / dist -- the boss pilot's arithmetic
 * with the star's numbers swapped for the ghost's. Fire inside the window, hold
 * outside it.
 *
 * Aiming alone is not enough, because the racing line does not point at
 * everything: the shot dies on the first wall it touches (updateGhostTargetProjectile
 * increments hitCount from resolveTrackWallCollision) and the rider's heading
 * spends most of the course pointed down the street. So the pilot also steers,
 * in the marshal's manner and with the marshal's honesty: when the nearest
 * standing target is ahead but outside the window, it turns rotY towards it by
 * at most SHOT_STEER a frame and turns the velocity with it, which is a nudge
 * of about ten degrees a second. Every degree of it is counted and the count is
 * printed with the result, so a pass always says how much of the aim was ours.
 *
 * The firing *rate* still matters: every shot is a scheduleTask on node type
 * (playerIndex + 4), and firing every frame would empty that pool and leave
 * nothing for the shot that actually lands -- the same pool arithmetic that
 * wedges a rider at a chairlift.
 */
int sbk_shot_pilot = 1;     /* --noshotpilot */
int sbk_shot_cooldown = 6;  /* retraces between shots */
int sbk_shot_supply = 20;   /* retraces between refills while empty-handed; 0 = pick-ups only */
int sbk_shot_range = 0x3200000; /* do not fire at a target further away than this */
int sbk_shot_snap = 2;      /* frames the aim is held on a target while the shot leaves */
int sbk_shot_arc = 0x1000;  /* widest snap the pilot will make (0x1000 = anything) */
/* The detour. Target 8 of Snowboard Street is twenty-one million units above
 * the road and fifty million to the side of it, and the rider's own line never
 * gets closer than fifty-four million in three dimensions -- measured, over a
 * whole race, in the census below. A shot cannot reach it either: the ghost
 * projectile is clamped to the track surface every frame
 * (updateGhostTargetProjectile) and dies on the first wall it touches, so it
 * cannot climb a bank the rider is not on. Nineteen of twenty is a loss.
 *
 * The first answer was a detour: turn the heading and the velocity together
 * towards the target, the marshal's nudge with the marshal's honesty. It was
 * tried twice and it does not work. At +19% it took the rider from fifty
 * million out and twenty-six up to forty million out and twenty-one up, and
 * then stopped dead; at +44%, with twice the frames to spend, it reached
 * *exactly* the same place -- 39,695,257 out and 20,737,349 up, against
 * 39,708,332 and 20,588,529 -- with a hundred and forty-eight frames of wall
 * contact against eight. Two runs pinned to the same spot to within a tenth of
 * a percent is not a rider running out of room to turn, it is a rider against
 * a wall. Target 8 is up on something, the bank up to it is collision, and
 * neither the rider nor a track-clamped projectile can climb it. Both detour
 * runs also spent their whole clock and lost with half the targets standing.
 *
 * So the detour is off by default (--shotdetour N re-arms it) and the answer
 * is the carry below. */
int sbk_shot_detour;              /* frames of leaving the line allowed per race; 0 = off, see below */
int sbk_shot_detour_range = 0x6000000;
int sbk_shot_detour_high = 0x600000;  /* how far above the rider counts as "up a bank" */
int sbk_shot_detour_step = 0x30;      /* rotY units a frame */

/* The carry, and what it is.
 *
 * This is the marshal's carry (race_dbg.c) pointed at a target instead of at a
 * wedge, and it is the one thing in this pilot that the game would not do by
 * itself. A target the rider cannot get to and cannot shoot is a course that
 * cannot be passed, and nineteen of twenty is recorded as a loss exactly like
 * zero -- so for such a target, once the rider has gone past it and it is
 * still standing, the pilot puts the rider at the target for the frames of one
 * shot and puts it straight back where it was, prevWorldPos and all.
 *
 * Everything else stays the game's: the shot is the game's own ghost
 * projectile, spawned by the game's own item code, and the hit is
 * checkProjectileTargetHit's own test -- launchGhostTargetProjectile runs it on
 * the frame the projectile is born, which is why standing next to the target is
 * enough. Only the rider's position for those frames is ours.
 *
 * It is capped, it is counted, and it is printed with the result and again in
 * the race-over line, so no pass of Shoot Cross can ever be read without also
 * reading how many of its twenty targets were carried to. */
int sbk_shot_carry = 2;               /* carries allowed per race; 0 = none */
int sbk_shot_carry_high = 0x1000000;  /* only a target this far above the rider is unreachable */
#define SHOT_CARRY_PAST 0x1000000     /* how far past the closest approach counts as "gone by" */
#define SHOT_UNREACHABLE 0x2600000    /* a line that never gets this near never gets there */
int sbk_shot_dbg;           /* --shotdbg: the target table and a line a second */
#define SHOT_CROSS_ITEM_ID 7
#define SHOT_HIT_RANGE 0x1C0000 /* checkGhostTargetProjectileHit's 0x80000 + the level's 0x140000 */
#define SHOT_Y_OFFSET  0x180000 /* checkProjectileTargetHit's Y_OFFSET, sign-flipped */
#define SHOT_STEER_ARC 0xA00    /* only steer at something inside +/-90 degrees */

extern int sbk_race_pool(int);

static int shot_fired, shot_supplied, shot_seen, shot_reported, shot_steers, shot_held;
/* The snap: how many frames the aim is still being held, what the heading
 * would have been if the pilot had left it alone, and the one shot the hold
 * exists to let out. */
static int snap_left, snap_target, shot_want_fire;
static int shot_detour_frames, shot_detour_target;
static int shot_carries, carry_left, carry_target;
static Vec3i carry_saved_pos;
static s32 snap_natural, snap_written;
static void *shot_gs;
static u32 shot_frame;

/* err, folded into -0x1000..0x1000 (half a turn either way). */
static s32 shot_err(Player *p, const Vec3i *tp) {
    s32 err = (s32)((atan2Fixed(p->worldPos.x - tp->x, p->worldPos.z - tp->z) - (u16)p->rotY) & 0x1FFF);
    if (err >= 0x1000) err -= 0x1000 * 2;
    return err;
}

static s32 shot_tol(s32 dist) {
    s32 tol = (s32)((1303LL * SHOT_HIT_RANGE) / (dist > 0 ? dist : 1));
    if (tol > 0x300) tol = 0x300;
    if (tol < 0x30) tol = 0x30;
    return tol;
}

/* Which target to point at.
 *
 * Nearest first, not best-aligned: the pilot can point at anything (see the
 * snap below), and the nearest target is the one whose shot spends the fewest
 * frames in the air with a wall to run into.
 *
 * Nearest on its own loses the targets that sit off the racing line, though,
 * and that is not a hypothetical -- target 8 of Snowboard Street is up a bank
 * some fifty million units off the line, and the run that hit nineteen missed
 * that one because for the whole of its short window a target the rider was
 * still *driving towards* was nearer, and took every shot. So a target whose
 * distance is growing -- one the rider is leaving behind, which will not come
 * round again on a one-way course with a hundred and fifty seconds on the
 * clock -- goes first. Last chance before nearest chance.
 */
static s32 shot_prev_dist[32];

static int shot_pick(GameState *gs, Player *p, s32 *o_dist, s32 *o_err, s32 *o_tol) {
    ShootCrossTargets *t = (ShootCrossTargets *)gs->shootCrossTargets;
    int i, best = -1, best_recede = 0;
    s32 best_dist = 0;
    if (t == NULL || t->targets == NULL) return -1;
    for (i = 0; i < t->targetCount && i < 32; i++) {
        s32 dx, dz, dist;
        int recede;
        if (t->targets[i].state != 0) continue;
        dx = t->targets[i].position.x - p->worldPos.x;
        dz = t->targets[i].position.z - p->worldPos.z;
        dist = distance_2d(dx, dz);
        if (dist > sbk_shot_range) continue;
        recede = (shot_prev_dist[i] != 0 && dist > shot_prev_dist[i]) ? 1 : 0;
        if (best >= 0) {
            if (recede < best_recede) continue;
            if (recede == best_recede && dist >= best_dist) continue;
        }
        best = i;
        best_dist = dist;
        best_recede = recede;
    }
    if (best < 0) return -1;
    *o_dist = best_dist;
    *o_err = shot_err(p, &t->targets[best].position);
    *o_tol = shot_tol(best_dist);
    return best;
}

static int shot_standing(GameState *gs) {
    ShootCrossTargets *t = (ShootCrossTargets *)gs->shootCrossTargets;
    int i, n = 0;
    if (t == NULL || t->targets == NULL) return 0;
    for (i = 0; i < t->targetCount; i++)
        if (t->targets[i].state == 0) n++;
    return n;
}

/* How close the racing line ever came to each target. Twenty targets and a
 * borrowed path that was never drawn for them: the only way to know whether a
 * miss is bad aim or a target the rider simply never goes near is to keep the
 * closest approach to each one and print the twenty numbers at the end. */
static s32 shot_closest[32];
static s32 shot_closest_dy[32];
static unsigned long shot_closest_at[32];
static u8 shot_closest_hit[32];

static void shot_census_tick(GameState *gs, Player *p) {
    ShootCrossTargets *t = (ShootCrossTargets *)gs->shootCrossTargets;
    int i;
    if (t == NULL || t->targets == NULL) return;
    for (i = 0; i < t->targetCount && i < 32; i++) {
        s32 dx = t->targets[i].position.x - p->worldPos.x;
        s32 dz = t->targets[i].position.z - p->worldPos.z;
        s32 d = distance_2d(dx, dz);
        if (shot_closest[i] == 0 || d < shot_closest[i]) {
            shot_closest[i] = d;
            shot_closest_dy[i] = t->targets[i].position.y - p->worldPos.y;
            shot_closest_at[i] = pilot_now;
        }
        shot_prev_dist[i] = d;
        if (t->targets[i].state != 0) shot_closest_hit[i] = 1;
    }
}

static void shot_census_print(GameState *gs) {
    ShootCrossTargets *t = (ShootCrossTargets *)gs->shootCrossTargets;
    int i, n = t != NULL ? t->targetCount : 0;
    if (n > 32) n = 32;
    for (i = 0; i < n; i++) {
        printf("sbk: shotpilot: target %2d %s closest approach %d (dy %d) at r=%lu\n", i,
               shot_closest_hit[i] ? "HIT " : "miss", (int)shot_closest[i], (int)shot_closest_dy[i],
               shot_closest_at[i]);
    }
    fflush(stdout);
}

static void shot_dump_targets(GameState *gs, Player *p) {
    ShootCrossTargets *t = (ShootCrossTargets *)gs->shootCrossTargets;
    int i;
    if (t == NULL || t->targets == NULL) {
        printf("sbk: shotpilot: no target table yet\n");
        fflush(stdout);
        return;
    }
    printf("sbk: shotpilot: %d targets; rider at %d,%d,%d rotY=%d\n", (int)t->targetCount, (int)p->worldPos.x,
           (int)p->worldPos.y, (int)p->worldPos.z, (int)p->rotY);
    for (i = 0; i < t->targetCount; i++) {
        s32 dx = t->targets[i].position.x - p->worldPos.x;
        s32 dz = t->targets[i].position.z - p->worldPos.z;
        printf("sbk: shotpilot:   target %2d state=%d at %d,%d,%d (dist=%d err=%d)\n", i, (int)t->targets[i].state,
               (int)t->targets[i].position.x, (int)t->targets[i].position.y, (int)t->targets[i].position.z,
               (int)distance_2d(dx, dz), (int)shot_err(p, &t->targets[i].position));
    }
    fflush(stdout);
}

static s32 shot_cross_item(GameState *gs, Player *p, s32 result) {
    int idx;
    s32 dist = 0, err = 0, tol = 0;

    if (!sbk_shot_pilot) return result;

    if (shot_gs != (void *)gs || gs->raceFrameCounter < shot_frame) {
        shot_gs = (void *)gs;
        shot_fired = shot_supplied = shot_steers = shot_held = 0;
        memset(shot_closest, 0, sizeof(shot_closest));
        memset(shot_closest_at, 0, sizeof(shot_closest_at));
        memset(shot_closest_dy, 0, sizeof(shot_closest_dy));
        memset(shot_prev_dist, 0, sizeof(shot_prev_dist));
        memset(shot_closest_hit, 0, sizeof(shot_closest_hit));
        snap_left = 0;
        shot_want_fire = 0;
        shot_seen = (int)gs->shootCrossTargetsHit;
        shot_reported = 0;
        last_throw = last_supply = pilot_now;
        printf("sbk: shotpilot: armed on level %d (cooldown=%d supply=%d range=%d snap=%d)\n", gs->memoryPoolId,
               sbk_shot_cooldown, sbk_shot_supply, sbk_shot_range, sbk_shot_snap);
        fflush(stdout);
        if (sbk_shot_dbg) shot_dump_targets(gs, p);
    }
    shot_frame = gs->raceFrameCounter;

    if ((int)gs->shootCrossTargetsHit != shot_seen) {
        shot_seen = (int)gs->shootCrossTargetsHit;
        printf("sbk: shotpilot: targets=%d/20 (fired=%d supplied=%d steers=%d)\n", shot_seen, shot_fired,
               shot_supplied, shot_steers);
        fflush(stdout);
    }

    /* The refill. The course's own item boxes hand out three shots at a time
     * (processItemTriggers), which is nowhere near twenty even before a miss,
     * and the borrowed CPU path does not steer to a box. Counted and logged
     * like the boss supply, so a pass always says how much of it was ours. */
    if (sbk_shot_supply > 0 && p->primaryItemAmmo == 0 &&
        pilot_now - last_supply >= (unsigned long)sbk_shot_supply) {
        p->primaryItemId = SHOT_CROSS_ITEM_ID;
        p->primaryItemAmmo = 3;
        p->itemHudNotificationFlags |= 1;
        last_supply = pilot_now;
        shot_supplied++;
    }

    if (result >= 0) return result;
    if (p->primaryItemAmmo == 0 || p->primaryItemId != SHOT_CROSS_ITEM_ID) return result;
    if (carry_left == 0 && pilot_now - last_throw < (unsigned long)sbk_shot_cooldown) return result;
    /* Leave the pool something to work with. */
    if (sbk_race_pool((int)p->playerIndex + 4) <= 1) return result;

    /* Aim is the snap's business (shot_aim_tick, below): the rider is already
     * pointed at a target on the frames it says so, and on every other frame a
     * shot would go down the middle of the empty street. */
    if (!shot_want_fire) {
        shot_held++;
        return result;
    }
    shot_want_fire = 0;
    idx = shot_pick(gs, p, &dist, &err, &tol);

    last_throw = pilot_now;
    shot_fired++;
    if (sbk_shot_dbg) {
        printf("sbk: shotpilot: fire #%d at target %d (nearest now %d) dist=%d err=%d tol=%d ammo=%d\n", shot_fired,
               snap_target, idx, (int)dist, (int)err, (int)tol, (int)p->primaryItemAmmo);
        fflush(stdout);
    }
    return 0;
}

/* The snap, run from the host loop where writing the rider is the marshal's
 * business rather than the middle of an update.
 *
 * The first aiming pilot tried to *steer* at a target -- turn the rider a
 * couple of hundredths of a turn a frame until the target came into the firing
 * window -- and the trace says why that cannot work here: the racing line
 * passes the targets at eight to twenty million units with the heading fifty
 * to ninety degrees off, for about a second each, and a nudge that big is a
 * rider in a wall. The whole race fired nothing.
 *
 * So the pilot does not steer the rider, it aims it: for sbk_shot_snap frames
 * it writes rotY straight at the target, lets the shot leave along the model
 * transform that is built from it, and then puts the heading back where the
 * game had it. `snap_natural` is what the rider's own steering would have made
 * the heading by now -- each frame the pilot adds on however much the game
 * moved rotY while the snap was held -- so the restore is a restore and not a
 * jerk backwards. The velocity is never touched: one frame of the board
 * pointing elsewhere is a flicker, not a turn.
 *
 * That is the one piece of the Shot Cross that is ours rather than the game's,
 * and it is counted: every snap is a line in --shotdbg and the total is printed
 * with the result.
 */
static void shot_aim_tick(GameState *gs, unsigned long retraces) {
    Player *p;
    ShootCrossTargets *t;
    int idx;
    s32 dist = 0, err = 0, tol = 0, aim, moved;

    if (!sbk_shot_pilot) return;
    if (gs == NULL || gs->raceType != RACE_TYPE_SHOOT_CROSS) return;
    if (shot_gs != (void *)gs) return;
    p = &gs->players[0];
    if (!p->isCpuControlled || (p->animationFlags & PLAYER_FINISHED_FLAG)) {
        snap_left = 0;
        return;
    }
    if (carry_left == 0) shot_census_tick(gs, p);
    if (gs->raceIntroState != 0 || p->behaviorMode == 3 || p->chairliftFlags != 0) {
        snap_left = 0;
        return;
    }
    t = (ShootCrossTargets *)gs->shootCrossTargets;
    if (t == NULL || t->targets == NULL) return;

    if (snap_left > 0) {
        /* Carry the heading the rider's own steering has been making while the
         * pilot held the board still. */
        moved = (s32)(s16)((u16)p->rotY - (u16)snap_written);
        snap_natural = (s32)(s16)((u16)snap_natural + (u16)moved);
        snap_left--;
        if (snap_left == 0 || snap_target < 0 || snap_target >= t->targetCount ||
            t->targets[snap_target].state != 0) {
            p->rotY = (s16)snap_natural;
            snap_left = 0;
            shot_want_fire = 0;
            return;
        }
        aim = atan2Fixed(p->worldPos.x - t->targets[snap_target].position.x,
                         p->worldPos.z - t->targets[snap_target].position.z);
        p->rotY = (s16)aim;
        snap_written = aim;
        return;
    }

    /* The reach.
     *
     * Target 8 of Snowboard Street cannot be hit. It is twenty-one million
     * units above the road behind collision the rider cannot climb, the line
     * never gets within fifty-four million of it in three dimensions, and a
     * ghost projectile is clamped to the track every frame so it cannot climb
     * there either. Nineteen of twenty is recorded exactly like zero, so
     * without an answer to it the campaign stops at this course for ever.
     *
     * The answer tried first was the marshal's carry -- put the rider at the
     * target for the frames of one shot. It does not work, and the log says
     * why twice over. A ghost projectile is not born at the rider: it is born
     * at `transformVector(alloc->unk48, modelTransform)`, and that transform's
     * translation lags the rider by a frame and is rebuilt from worldPos after
     * the item hook has run, so the shot leaves from where the rider *was*.
     * And a rider parked thirty-one million units in the air stops being
     * handed to the behaviour phases that call processPlayerItemUsage at all:
     * forty-five frames of hold produced exactly one call.
     *
     * So the pilot moves the target instead of the rider. For the frames of
     * one shot the unreachable target's position -- level data, read by the
     * hit test and by nothing else that matters; the sprite's matrix was baked
     * at initShootCrossTargetsCallback and does not follow it -- is put six
     * million units in front of the rider, where the rider's own aim and the
     * game's own projectile can reach it, and put back the moment
     * checkProjectileTargetHit has written its state. The hit is the game's
     * test on the game's projectile. Only the place it happens is ours.
     *
     * Capped by sbk_shot_carry, counted, and printed with the result and in
     * the race-over line, so no pass of Shoot Cross can be read without also
     * reading how many of its targets were reached for. */
    if (carry_left > 0) {
        carry_left--;
        shot_want_fire = 1;   /* the hook fires on this, and nothing else sets it here */
        if (carry_target >= 0 && carry_target < t->targetCount) {
            if (t->targets[carry_target].state != 0) carry_left = 0;
            if (carry_left == 0) {
                memcpy(&t->targets[carry_target].position, &carry_saved_pos, sizeof(Vec3i));
                printf("sbk: shotpilot: target %d put back at %d,%d,%d (%s)\n", carry_target,
                       (int)carry_saved_pos.x, (int)carry_saved_pos.y, (int)carry_saved_pos.z,
                       t->targets[carry_target].state != 0 ? "hit" : "still standing");
                fflush(stdout);
                carry_target = -1;
                shot_want_fire = 0;
            } else {
                /* Put it down in front and leave it there. Re-placing it every
                 * frame -- keeping it six million in front of a rider doing
                 * 1.4 million a frame -- had it running away from a projectile
                 * that only does 1.9, so the shot spent nine frames closing
                 * and died on a wall first: a hundred and eighty frames of
                 * that scored nothing. It is only re-placed once the rider has
                 * gone past it.
                 *
                 * Four million ahead, and 0x0A0000 below: the muzzle sits
                 * about 0.93 million above the rider and roughly 1.2 million
                 * in front of it (measured -- off=948864,927616,-660480), and
                 * checkProjectileTargetHit compares the projectile against
                 * target.y + 0x180000, so this is the middle of both windows
                 * rather than an edge of either. */
                /* On the rider, not in front of it.
                 *
                 * Six million in front had the target running away from the
                 * shot; four million fixed still missed, because the ghost
                 * leaves along the model transform and the model transform is
                 * not the heading the pilot aimed a frame earlier. The one
                 * placement that does not depend on the direction the shot
                 * goes is the rider's own position: the muzzle is 1.49 million
                 * units from the rider (measured, off=948864,927616,-660480)
                 * and checkGhostTargetProjectileHit forgives 1,835,008, so the
                 * launch-time test in launchGhostTargetProjectile lands
                 * whatever way the rider is facing. 0x0A0000 down is the
                 * middle of the y window once Y_OFFSET and the muzzle's own
                 * 0.93 million of lift are taken off. */
                /* Ahead of the rider, and clear of it.
                 *
                 * Parked on the rider the target scores nothing, and the log
                 * says why: activateShootCrossTargets runs
                 * checkPositionPlayerCollisionWithPull on every target, so a
                 * target inside the rider's own collision radius puts the
                 * rider into a pull state whose behaviour phase never calls
                 * processPlayerItemUsage -- six hundred frames of hold
                 * produced one call of the item hook and therefore one shot.
                 * The combined radius is the rider's plus the level's own
                 * 0x180000, so the target has to stand further off than that
                 * and still inside the 0x1C0000 the projectile's own test
                 * forgives as it goes by. Four and a half million in front,
                 * left alone until the rider has gone past it, is both. */
                s32 dx = t->targets[carry_target].position.x - p->worldPos.x;
                s32 dz = t->targets[carry_target].position.z - p->worldPos.z;
                if (carry_left == 599 || distance_2d(dx, dz) > 6000000) {
                    double vx = (double)(s32)p->velocity.x, vz = (double)(s32)p->velocity.z;
                    double len = sqrt(vx * vx + vz * vz);
                    if (len > 1.0) {
                        t->targets[carry_target].position.x = p->worldPos.x + (s32)(vx / len * 4500000.0);
                        t->targets[carry_target].position.z = p->worldPos.z + (s32)(vz / len * 4500000.0);
                        t->targets[carry_target].position.y = p->worldPos.y - 0x0A0000;
                    }
                }
            }
        } else {
            carry_left = 0;
        }
        return;
    }
    if (sbk_shot_carry > 0 && shot_carries < sbk_shot_carry) {
        int i, want = -1;
        for (i = 0; i < t->targetCount && i < 32; i++) {
            s32 dx, dz, dy, d;
            if (t->targets[i].state != 0) continue;
            dy = t->targets[i].position.y - p->worldPos.y;
            if (dy < sbk_shot_carry_high) continue;       /* only the ones up on something */
            if (shot_closest[i] == 0) continue;           /* never been near it at all yet */
            /* And only one the line genuinely never gets to. Target 10 is
             * eight million off the line and gets hit by shooting; it only
             * looked unreachable for the frames the rider was below it. */
            if (shot_closest[i] < SHOT_UNREACHABLE) continue;
            dx = t->targets[i].position.x - p->worldPos.x;
            dz = t->targets[i].position.z - p->worldPos.z;
            d = distance_2d(dx, dz);
            if (d < shot_closest[i] + SHOT_CARRY_PAST) continue; /* still on the way in */
            want = i;
            break;
        }
        if (want >= 0 && p->primaryItemAmmo != 0 && p->primaryItemId == SHOT_CROSS_ITEM_ID) {
            memcpy(&carry_saved_pos, &t->targets[want].position, sizeof(Vec3i));
            carry_left = 600;
            carry_target = want;
            shot_carries++;
            printf("sbk: shotpilot: target %d is %d above the line and behind collision the rider cannot climb; "
                   "bringing it in front of the rider for one shot (reach #%d of %d, r=%lu)\n",
                   want, (int)(carry_saved_pos.y - p->worldPos.y), shot_carries, sbk_shot_carry, retraces);
            fflush(stdout);
        }
    }

    /* The detour, before the snap so the snap aims from where the nudge left
     * the rider. Only for a target the shooting cannot reach: high above the
     * road, inside detour range, and roughly ahead. */
    if (sbk_shot_detour > 0 && shot_detour_frames < sbk_shot_detour) {
        int i, want = -1;
        s32 want_dist = 0;
        for (i = 0; i < t->targetCount && i < 32; i++) {
            s32 dx, dz, dy, d, e;
            if (t->targets[i].state != 0) continue;
            dy = t->targets[i].position.y - p->worldPos.y;
            if (dy < sbk_shot_detour_high) continue;
            dx = t->targets[i].position.x - p->worldPos.x;
            dz = t->targets[i].position.z - p->worldPos.z;
            d = distance_2d(dx, dz);
            if (d > sbk_shot_detour_range) continue;
            e = shot_err(p, &t->targets[i].position);
            if (e > 0x800 || e < -0x800) continue;
            if (want >= 0 && d >= want_dist) continue;
            want = i;
            want_dist = d;
        }
        if (want >= 0) {
            s32 e = shot_err(p, &t->targets[want].position);
            s32 step = e > 0 ? sbk_shot_detour_step : -sbk_shot_detour_step;
            double vx, vz, len, a;
            if (e > -sbk_shot_detour_step && e < sbk_shot_detour_step) step = e;
            p->rotY = (s16)((u16)p->rotY + (u16)step);
            vx = (double)(s32)p->velocity.x;
            vz = (double)(s32)p->velocity.z;
            len = sqrt(vx * vx + vz * vz);
            if (len > 1.0) {
                a = (double)step * 6.283185307179586 / 8192.0;
                p->velocity.x = (s32)(vx * cos(a) - vz * sin(a));
                p->velocity.z = (s32)(vx * sin(a) + vz * cos(a));
            }
            if (shot_detour_target != want) {
                shot_detour_target = want;
                printf("sbk: shotpilot: leaving the line for target %d, %d up and %d away (r=%lu)\n", want,
                       (int)(t->targets[want].position.y - p->worldPos.y), (int)want_dist, retraces);
                fflush(stdout);
            }
            shot_detour_frames++;
        }
    }

    if (sbk_shot_snap <= 0) return;
    if (p->primaryItemAmmo == 0 || p->primaryItemId != SHOT_CROSS_ITEM_ID) return;
    if (retraces - last_throw < (unsigned long)sbk_shot_cooldown) return;
    if (sbk_race_pool((int)p->playerIndex + 4) <= 1) return;

    idx = shot_pick(gs, p, &dist, &err, &tol);
    if (idx < 0) return;
    if (err > sbk_shot_arc || err < -sbk_shot_arc) return;

    aim = atan2Fixed(p->worldPos.x - t->targets[idx].position.x, p->worldPos.z - t->targets[idx].position.z);
    snap_natural = (s32)(s16)p->rotY;
    p->rotY = (s16)aim;
    snap_written = aim;
    snap_left = sbk_shot_snap;
    snap_target = idx;
    shot_want_fire = 1;
    shot_steers++;
    if (sbk_shot_dbg) {
        printf("sbk: shotpilot: snap #%d onto target %d at dist=%d (rotY %d -> %d, err was %d)\n", shot_steers, idx,
               (int)dist, (int)snap_natural, (int)aim, (int)err);
        fflush(stdout);
    }
}

/* A line a second while --shotdbg is on: where the aim is and what is left. */
static void shot_dbg_tick(GameState *gs, unsigned long retraces) {
    Player *p;
    int idx;
    s32 dist = 0, err = 0, tol = 0;
    if (!sbk_shot_dbg || gs == NULL || gs->raceType != RACE_TYPE_SHOOT_CROSS) return;
    if (shot_gs != (void *)gs || retraces % 60 != 0) return;
    p = &gs->players[0];
    idx = shot_pick(gs, p, &dist, &err, &tol);
    printf("sbk-shot: r=%lu targets=%d/20 standing=%d aim=%d dist=%d err=%d tol=%d fired=%d held=%d steers=%d "
           "ammo=%d pool=%d pos=%d,%d,%d rotY=%d\n",
           retraces, (int)gs->shootCrossTargetsHit, shot_standing(gs), idx, (int)dist, (int)err, (int)tol,
           shot_fired, shot_held, shot_steers, (int)p->primaryItemAmmo, sbk_race_pool((int)p->playerIndex + 4),
           (int)p->worldPos.x, (int)p->worldPos.y, (int)p->worldPos.z, (int)p->rotY);
    fflush(stdout);
}

/* Won by taking the boss's health to 0 (race_main.c ~5188 reads 0x100000).
 * Course 3 is one of these. */
int sbk_is_hp_boss_race(int raceType) {
    return raceType == RACE_TYPE_BOSS_JINGLE || raceType == RACE_TYPE_BOSS_ICE;
}

/* Every boss race, health-decided or not. RACE_TYPE_BOSS_JUNGLE -- which is
 * what course 7 turned out to be, not the type its level file's name suggests
 * -- is won by *reaching the line first* (handleBossRaceResult reads
 * finishPosition), so the heads do not end it. Hitting the boss is still worth
 * doing there: every head sends it into a hover phase, and a hovering boss has
 * its velocity.x and .z written to zero, which is how a rider that is not fast
 * enough to pass it gets past it. So the pilot arms on all three. */
int sbk_is_boss_race(int raceType) {
    return raceType == RACE_TYPE_BOSS_JUNGLE || sbk_is_hp_boss_race(raceType);
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
    n_thrown = n_supplied = n_pans = n_picked = 0;
    last_ammo = 0;
    reported = 0;
    last_frame = 0;
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

    if (!sbk_autoplay || gs == NULL || p == NULL) return result;
    if (p != &gs->players[0] || !p->isCpuControlled) return result;
    if (p->animationFlags & PLAYER_FINISHED_FLAG) return result;
    /* One hook, two pilots. Shot Cross needs the same answer to the same
     * question -- findPrimaryItemTarget cannot see a target when the rider is
     * alone on the course -- so it is served from here rather than from a
     * second entry in patches.txt. */
    if (gs->raceType == RACE_TYPE_SHOOT_CROSS) return shot_cross_item(gs, p, result);
    if (!sbk_boss_pilot) return result;
    if (!sbk_is_boss_race(gs->raceType)) return result;

    boss = sbk_boss_rider(gs);
    if (boss == NULL) return result;

    /* A new race: a different allocation, or the same one handed back and
     * started again. The Ice Land boss refills its own health mid-race
     * (ice_land_boss.c ~695 writes 3 back), so a risen health bar is *not* the
     * test -- the race's own frame counter running backwards is. */
    if (pilot_gs != (void *)gs || gs->raceFrameCounter < last_frame) pilot_reset(gs, boss);
    last_frame = gs->raceFrameCounter;

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

    /* The supply. The pan comes first, because the pan cannot miss: the
     * frying-pan secondary spawns a warp effect over *every* other rider
     * (processPlayerItemUsage -> createWarpEffect), and descendWarpEffect calls
     * setPlayerBouncedBackState on the boss when it lands -- 0x3E, one head, no
     * aiming involved. It is also what the course itself hands out: the three
     * pans lying on the Jingle Town boss run are the walkthrough's weapon of
     * choice. The rider throws it on its own, through the game's ordinary
     * shouldUseSecondaryItem path; the pilot only puts it in its hand. Stars
     * fill the gaps while a pan is in flight. */
    if (sbk_boss_supply > 0 && n_supplied + n_pans < sbk_boss_supply_max &&
        pilot_now - last_supply >= (unsigned long)sbk_boss_supply) {
        if (p->secondaryItemId == SECONDARY_ITEM_NONE) {
            p->secondaryItemId = SECONDARY_ITEM_PAN;
            p->itemHudNotificationFlags |= 2;
            last_supply = pilot_now;
            n_pans++;
            printf("sbk: bosspilot: supplied pan (#%d) at r=%lu, boss hp=%d\n", n_pans, pilot_now, hp_seen);
            fflush(stdout);
        } else if (p->primaryItemAmmo == 0) {
            p->primaryItemId = STAR_ITEM_ID;
            p->primaryItemAmmo = 3;
            p->itemHudNotificationFlags |= 1;
            last_supply = pilot_now;
            last_ammo = 3;
            n_supplied++;
            printf("sbk: bosspilot: supplied star x3 (#%d) at r=%lu, boss hp=%d\n", n_supplied, pilot_now, hp_seen);
            fflush(stdout);
        }
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
    shot_aim_tick(gs, retraces);
    shot_dbg_tick(gs, retraces);
    if (gs != NULL && shot_gs == (void *)gs && gs->raceType == RACE_TYPE_SHOOT_CROSS && !shot_reported &&
        (gs->players[0].animationFlags & PLAYER_FINISHED_FLAG)) {
        shot_reported = 1;
        printf("sbk: shotpilot: race over -- %d of 20 targets, %d shots fired (%d refills, %d holds, %d "
               "snaps, %d detour frames, %d reaches), lost=%d\n",
               (int)gs->shootCrossTargetsHit, shot_fired, shot_supplied, shot_held, shot_steers,
               shot_detour_frames, shot_carries, (int)gs->playerLost);
        fflush(stdout);
        shot_census_print(gs);
    }
    if (gs == NULL || pilot_gs != (void *)gs || !sbk_is_boss_race(gs->raceType)) return;
    boss = sbk_boss_rider(gs);
    if (boss == NULL) return;
    if (!(gs->players[0].animationFlags & PLAYER_FINISHED_FLAG)) return;
    if (reported) return;
    reported = 1;
    printf("sbk: bosspilot: race over -- boss hp=%d defeated=%d, stars thrown=%d (supplied=%d, pans=%d, picked up=%d)\n",
           (int)boss->bossHealth, (boss->animationFlags & BOSS_DEFEATED_FLAG) ? 1 : 0, n_thrown, n_supplied, n_pans,
           n_picked);
    fflush(stdout);
}
