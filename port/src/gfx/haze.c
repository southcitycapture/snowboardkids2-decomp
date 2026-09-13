/* Enhanced-mode distance haze.
 *
 * --drawdistance N multiplies the race camera's far plane (patches.txt), and
 * the extra range is honest geometry: the N64 never drew it, so nobody ever
 * made it look like anything.  At N=4 the top of the screen grows a hard band
 * of far terrain standing in front of the sky, a mountain outline behind the
 * chairlift, clouds against hillside.  The fix is the one every draw-distance
 * mod ends up at: fade the new range into the sky.
 *
 * The technique is the N64's own fog, not a post-process and not a shader.
 * F3DEX already has per-vertex fog -- gfx_pc.c computes a fog factor for
 * G_FOG geometry and gfx_gl13.c hands it to GL_FOG as a per-vertex fog
 * coordinate (GL_EXT_fog_coord, GL_LINEAR, start 0 end 1).  The Radeon 9000
 * does that in fixed function for nothing.  So the haze is not new machinery
 * at all: it is the same vertex slot and the same GL_FOG, filled in by the
 * port for race geometry the game did not ask to fog.  Geometry the game
 * *does* fog (Turtle Island's course fog, the screen fades) keeps its own fog
 * and is never touched twice -- see the `use_fog` test in gfx_sp_tri1.
 *
 * The distance is exact rather than estimated.  setViewportPerspective ->
 * guPerspective(..., scale = 1.0f), so the projection's w column is a unit
 * vector rotated by the view matrix and the clip-space w of a vertex *is* its
 * eye distance in the game's own world units -- the same units the far plane
 * is written in.  gfx_pc takes the length of that column anyway, so a game
 * that ever passed a scale would still measure right.
 *
 * The colour is the game's own.  Every course carries
 * LevelConfig.environmentColors.fog (src/data/course_data.c): the colour
 * race_session.c already hands to setViewportFogById for the course fog the
 * N64 shipped switched off (fog positions 0x3E3..0x3E7 -- the last half a
 * percent of the depth range, which is to say never).  It is authored per
 * course and per time of day: 50 70 F0 for Sunny Mountain's blue sky, FF FF
 * C0 for the sunset course, 07 00 20 and 00 10 20 for the night ones.  So the
 * haze needs no table of its own and no guessing at the sky: it is the colour
 * the artists picked for that course's air.
 *
 * The range runs from the far plane the *unmodified* game would have clipped
 * at (less a margin, so the fade starts inside the old horizon rather than
 * exactly on it) to the extended far plane.  Nothing the N64 ever drew changes
 * colour by so much as a bit, because everything the N64 drew is nearer than
 * the start.  That also takes care of the props: the cull range scales with
 * sbk_far_scale too, so an item box or a rider appearing at the edge of it
 * appears already ~90% haze and fades up rather than popping.
 *
 * Off in Original mode, off in a scripted or golden run, and off whenever
 * --drawdistance is 1: at scale 1 the start and the end of the range would be
 * the same distance.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "haze.h"
#include "../ultra/ultra.h"
#include "common.h"
#include "gamestate.h"
#include "data/course_data.h"

/* race_dbg.c: the running race's GameState, or NULL when no race is on
 * screen (it keys on the race scheduler's renderContext, 0x37). */
GameState *sbk_race_state(void);

extern float sbk_far_scale;
/* --hazeflat: paint every hazed vertex flat in the haze colour.  Not a look,
 * a probe: it is what showed the band the haze covers, and that the band is
 * exactly the ground past the N64's own horizon.  g4-shots/sbk2-haze-flat.png */
extern int sbk_haze_flat;

int sbk_haze_enabled;
int sbk_haze_debug;

int sbk_haze_on;
int sbk_race_proj_on;
int sbk_fadein_enabled;
int sbk_fadein_debug;
int sbk_fadein_on;
float sbk_fadein_start;
float sbk_fadein_end;
float sbk_fadein_inv_span;
unsigned sbk_fadein_dbg_draws, sbk_fadein_dbg_faded;
float sbk_fadein_dbg_min = 1.0f;
float sbk_haze_start;
float sbk_haze_end;
float sbk_haze_inv_span;
float sbk_haze_color[3];
int sbk_haze_persp_norm;
unsigned long sbk_haze_retrace;

/* The ramp is anchored to the far plane the *unmodified* game clipped at, not
 * to the extended one, and this is the whole of what makes the haze work.
 *
 * The first version spread the fade across the entire extra range (the old far
 * plane out to the new one) and it was invisible, which --hazedbg and a
 * three-way pixel diff between --drawdistance 1, 4-without-haze and
 * 4-with-haze said plainly: the geometry --drawdistance actually adds is not
 * spread over the new range at all.  It is a band sitting just past where the
 * N64 clipped -- the row of trees and the hut on the ridge at Sunny Mountain,
 * 23,000 pixels of one 640x480 frame, all of it between 3800 and about 5000
 * units.  A ramp that reached 100% at 15,200 was 6% there.  Anchored to the
 * old horizon instead, the same band comes out at 6% -> 40% -> 75% across the
 * distance it occupies, and everything past twice the old horizon is flat
 * course-coloured air.
 *
 * START: the fade begins slightly inside the old clip distance, so it starts
 * before the new geometry appears rather than exactly on it.  6% haze at the
 * N64's own horizon is a whisper and nothing the N64 drew moves by more.
 * END: one and a half times the old clip distance, capped at the extended far
 * plane.  1.5 and not 2 because the haze has to *beat* the fog the sequel
 * already applies to a race, which at --drawdistance 4 runs about 28% at the
 * old horizon and 58% at 6000 units: a gentler ramp than that changes not one
 * pixel, because the two fogs are the same colour and the thicker one wins.
 * At 1.5 the haze passes the game's own at around 4000 units and is flat
 * course-coloured air by 5700. */
#define HAZE_START_FRAC 0.85f
#define HAZE_END_MULT   1.50f

static float haze_far;      /* the race far plane, already scaled */
static float cull_range;    /* isObjectCulled's half extent, in world units */

float sbk_haze_note_far(float f) {
    haze_far = f;
    return f;
}

/* isObjectCulled (src/graphics/graphics.c) keeps every object inside a cube of
 * RACE_CULL_BOX_HALF_EXTENT_FIXED (0x0FEA0000 = 4074 units) around the
 * viewport's own translation, and --drawdistance does *not* scale it: only the
 * far planes scale.  So at --drawdistance 4 the ground is drawn out to 15,200
 * units and hazed out by 5,700, but a prop, a rider or an item box still
 * vanishes at 4,074, where the haze is only about 40% thick -- a visible pop.
 * That is what `fadein` is for, and this is where the port learns the number
 * (patches.txt).  Returns its argument. */
int sbk_fadein_note_cull(int range) {
    cull_range = (float)range / 65536.0f;
    return range;
}

/* The last 14% of the cull range: 3504..4074 units.  Shorter than that and an
 * object still arrives visibly; longer and props are half-transparent while
 * they are plainly in view. */
#define FADEIN_FRAC 0.86f

/* --- which matrices belong to a cullable object -------------------------
 *
 * The first version of the fade keyed on the object's origin distance alone
 * (MP[3][3]), guarded by "all three vertices are past the start of the band".
 * That is not enough, and the sequel said so loudly: at --drawdistance 4 it
 * faded 20,000 of 40,000 triangles a second down to alpha 0, because the
 * course's own terrain is drawn in chunks with their own far-away origins and
 * every chunk past 3,504 units looked exactly like a distant prop.
 *
 * So the port stops guessing and lets the game say which matrices belong to
 * the objects the cull applies to.  Each game has one function that builds an
 * object's transform matrix -- allocFixedTransformMatrix in the first game,
 * setupDisplayListMatrix in the sequel -- and patches.txt has it hand the
 * pointer over.  gfx_pc then fades a draw only when the modelview it loaded
 * is one of those.  Terrain, the sky and the HUD never appear in the table.
 *
 * The table is cleared after every frame's display list is walked, because
 * the matrices come out of a per-frame scratch allocator: a pointer from the
 * last frame means nothing.  A collision loses one object's fade, never
 * anything else, so eviction is a shrug rather than a problem. */
#define FADEIN_TABLE 1024
#define FADEIN_PROBE 4
static const void *fadein_mtx[FADEIN_TABLE];
static int fadein_mtx_used;

void sbk_fadein_note_object(const void *mtx) {
    unsigned long h;
    int i;
    if (!sbk_fadein_on || mtx == NULL) return;
    h = ((unsigned long)mtx >> 6) ^ ((unsigned long)mtx >> 3);
    for (i = 0; i < FADEIN_PROBE; i++) {
        unsigned k = (unsigned)((h + (unsigned long)i) & (FADEIN_TABLE - 1));
        if (fadein_mtx[k] == mtx) return;
        if (fadein_mtx[k] == NULL) { fadein_mtx[k] = mtx; fadein_mtx_used = 1; return; }
    }
    fadein_mtx[(unsigned)(h & (FADEIN_TABLE - 1))] = mtx;   /* evict: one lost fade */
    fadein_mtx_used = 1;
}

int sbk_fadein_is_object(const void *mtx) {
    unsigned long h;
    int i;
    if (!fadein_mtx_used || mtx == NULL) return 0;
    h = ((unsigned long)mtx >> 6) ^ ((unsigned long)mtx >> 3);
    for (i = 0; i < FADEIN_PROBE; i++) {
        unsigned k = (unsigned)((h + (unsigned long)i) & (FADEIN_TABLE - 1));
        if (fadein_mtx[k] == mtx) return 1;
        if (fadein_mtx[k] == NULL) return 0;
    }
    return 0;
}

void sbk_fadein_frame_end(void) {
    if (!fadein_mtx_used) return;
    memset(fadein_mtx, 0, sizeof(fadein_mtx));
    fadein_mtx_used = 0;
}

void sbk_haze_frame(void) {
    GameState *gs;
    LevelConfig *lc;
    static int last_course = -1;
    static unsigned long ticks;

    sbk_haze_retrace++;
    sbk_haze_on = 0;
    sbk_fadein_on = 0;
    sbk_race_proj_on = 0;
    if (haze_far <= 1.0f) return;          /* no race viewport has been built */

    gs = sbk_race_state();
    if (gs == NULL) return;

    lc = getLevelConfig(gs->memoryPoolId);
    if (lc == NULL) return;

    sbk_haze_color[0] = lc->environmentColors.fog.r / 255.0f;
    sbk_haze_color[1] = lc->environmentColors.fog.g / 255.0f;
    sbk_haze_color[2] = lc->environmentColors.fog.b / 255.0f;

    /* The race camera's perspNorm, computed whether or not either effect is
     * switched on: widescreen needs the same answer -- "is this draw the race
     * camera's?" -- to leave the menu passes in their 4:3 box. */
    sbk_haze_persp_norm = (int)(131072.0f / haze_far);
    sbk_race_proj_on = 1;

    if (sbk_fadein_enabled && cull_range > 1.0f) {
        sbk_fadein_end = cull_range;
        sbk_fadein_start = cull_range * FADEIN_FRAC;
        sbk_fadein_inv_span = 1.0f / (sbk_fadein_end - sbk_fadein_start);
        sbk_fadein_on = 1;
    }

    if (sbk_fadein_debug && (sbk_haze_retrace % 60 == 0)) {
        printf("sbk-fade: r%lu cull %.0f fade %.0f..%.0f draws %u faded %u minalpha %.2f\n",
               sbk_haze_retrace, cull_range, sbk_fadein_start, sbk_fadein_end,
               sbk_fadein_dbg_draws, sbk_fadein_dbg_faded, sbk_fadein_dbg_min);
        sbk_fadein_dbg_draws = sbk_fadein_dbg_faded = 0;
        sbk_fadein_dbg_min = 1.0f;
    }

    if (!sbk_haze_enabled) return;
    if (sbk_far_scale <= 1.001f) return;   /* nothing extra was drawn to hide */

    {
        float orig_far = haze_far / sbk_far_scale;   /* what the N64 clipped at */
        sbk_haze_start = orig_far * HAZE_START_FRAC;
        sbk_haze_end = orig_far * HAZE_END_MULT;
        if (sbk_haze_end > haze_far) sbk_haze_end = haze_far;
    }
    if (sbk_haze_end - sbk_haze_start < 1.0f) return;
    sbk_haze_inv_span = 1.0f / (sbk_haze_end - sbk_haze_start);

    /* guPerspective's perspNorm is 2*65536/(near+far), and near is ~0.1% of
     * far here, so the integer the race camera carries is exactly
     * (int)(131072/far).  The sky and the HUD are drawn under the *root*
     * viewport, whose far plane is a flat 10000 that --drawdistance does not
     * scale; its perspNorm is a different integer, so gfx_pc can tell the two
     * apart without a heuristic and the sky is never fogged into itself. */
    sbk_haze_on = 1;

    if (sbk_haze_debug && (ticks++ % 60 == 0 || gs->memoryPoolId != last_course)) {
        last_course = gs->memoryPoolId;
        printf("sbk-haze: r%lu course %d rgb %02x%02x%02x range %.0f..%.0f perspnorm %d "
               "projtris %u hazed %u maxf %.2f maxdist %.0f wscale %.4f\n",
               sbk_haze_retrace, (int)gs->memoryPoolId,
               lc->environmentColors.fog.r, lc->environmentColors.fog.g, lc->environmentColors.fog.b,
               sbk_haze_start, sbk_haze_end, sbk_haze_persp_norm,
               sbk_haze_dbg_proj_tris, sbk_haze_dbg_tris, sbk_haze_dbg_max,
               sbk_haze_dbg_maxdist, sbk_haze_dbg_scale);
        {
            int i;
            printf("sbk-haze: perspnorms");
            for (i = 0; i < 8 && sbk_haze_dbg_pn_tris[i] != 0; i++)
                printf(" %u=%u", (unsigned)sbk_haze_dbg_pn[i], sbk_haze_dbg_pn_tris[i]);
            printf("\n");
            for (i = 0; i < 8; i++) { sbk_haze_dbg_pn[i] = 0; sbk_haze_dbg_pn_tris[i] = 0; }
        }
        sbk_haze_dbg_proj_tris = sbk_haze_dbg_tris = 0;
        sbk_haze_dbg_max = 0.0f;
        sbk_haze_dbg_maxdist = 0.0f;
    }
}
