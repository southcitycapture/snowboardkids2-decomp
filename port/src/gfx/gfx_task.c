/* Bridge from an M_GFXTASK OSTask to the gfx_pc display-list interpreter. */
#include "../ultra/ultra.h"
#include <PR/gbi.h>
#include "../ultra/sbk_os.h"
#include "gfx_pc.h"
#include <PR/gs2dex.h>

extern int sbk_trace;
int sbk_dump_task = -1; /* --dumpdl N: print the whole display list of gfx task N */
int sbk_gfx_bad_dl;      /* set by gfx_pc on a malformed command; the task's list is dumped once */

extern void gfx_debug_get_segments(uint32_t out[16]);

/* The dumper's own copy of the segment table, snapshotted when a dump starts.
 * It used to write straight into gfx_pc's, which meant a branch the RSP never
 * takes could leave a segment base behind for the real run of the same list. */
static uint32_t dump_seg[16];

/* The task's microcode: the sequel runs F3DEX2 for 3D viewports and S2DEX for
 * 2D ones, and the two disagree about every opcode below 0x0c. */
static int task_is_s2dex(const OSTask *task) {
    extern u64 gspS2DEX_fifoTextStart[];
    return (uint32_t)(uintptr_t)task->t.ucode == (uint32_t)(uintptr_t)gspS2DEX_fifoTextStart;
}

static const void *dump_addr_ok(uint32_t w1, uint32_t size, uint32_t *room);

static const char *s2dex_name(unsigned op) {
    switch (op) {
        case G_OBJ_RECTANGLE:   return "OBJ_RECTANGLE";
        case G_OBJ_SPRITE:      return "OBJ_SPRITE";
        case G_SELECT_DL:       return "SELECT_DL";
        case G_OBJ_LOADTXTR:    return "OBJ_LOADTXTR";
        case G_OBJ_LDTX_SPRITE: return "OBJ_LDTX_SPRITE";
        case G_OBJ_LDTX_RECT:   return "OBJ_LDTX_RECT";
        case G_OBJ_LDTX_RECT_R: return "OBJ_LDTX_RECT_R";
        case G_BG_1CYC:         return "BG_1CYC";
        case G_BG_COPY:         return "BG_COPY";
        case G_OBJ_RENDERMODE:  return "OBJ_RENDERMODE";
        case (uint8_t)G_OBJ_RECTANGLE_R: return "OBJ_RECTANGLE_R";
        case (uint8_t)G_OBJ_MOVEMEM:     return "OBJ_MOVEMEM";
        default: return NULL;
    }
}

static void dump_obj(unsigned op, uint32_t w1, int depth) {
    switch (op) {
        case G_OBJ_RECTANGLE:
        case G_OBJ_SPRITE:
        case (uint8_t)G_OBJ_RECTANGLE_R: {
            const uObjSprite_t *s = (const uObjSprite_t *)dump_addr_ok(w1, sizeof(uObjSprite_t), NULL);
            if (s == NULL) break;
            printf("sbk-obj:%*s   sprite obj=(%d,%d) scale=(%u,%u) image=%ux%u stride=%u adrs=%u fmt=%u siz=%u pal=%u flags=%02x\n",
                   depth * 2, "", s->objX, s->objY, s->scaleW, s->scaleH, s->imageW >> 5, s->imageH >> 5,
                   s->imageStride, s->imageAdrs, s->imageFmt, s->imageSiz, s->imagePal, s->imageFlags);
            break;
        }
        case G_OBJ_LOADTXTR: {
            const uObjTxtr *t = (const uObjTxtr *)dump_addr_ok(w1, sizeof(uObjTxtr), NULL);
            if (t == NULL) break;
            printf("sbk-obj:%*s   loadtxtr type=%08x image=%p tmem=%u a=%u b=%u sid=%u\n", depth * 2, "",
                   (unsigned)t->block.type, (void *)t->block.image, t->block.tmem, t->block.tsize,
                   t->block.tline, t->block.sid);
            break;
        }
        case G_BG_1CYC:
        case G_BG_COPY: {
            const uObjBg *b = (const uObjBg *)dump_addr_ok(w1, sizeof(uObjBg), NULL);
            if (b == NULL) break;
            printf("sbk-obj:%*s   bg image=%p %ux%u fmt=%u siz=%u frame=(%d,%d) %ux%u src=(%u,%u) load=%04x scale=(%u,%u) yorig=%d\n",
                   depth * 2, "", (void *)b->b.imagePtr, b->b.imageW >> 2, b->b.imageH >> 2, b->b.imageFmt,
                   b->b.imageSiz, b->b.frameX >> 2, b->b.frameY >> 2, b->b.frameW >> 2, b->b.frameH >> 2,
                   b->b.imageX >> 5, b->b.imageY >> 5, b->b.imageLoad, b->s.scaleW, b->s.scaleH,
                   (int)b->s.imageYorig >> 5);
            break;
        }
        case (uint8_t)G_OBJ_MOVEMEM: {
            const uObjMtx_t *m = (const uObjMtx_t *)dump_addr_ok(w1, sizeof(uObjMtx_t), NULL);
            if (m == NULL) break;
            printf("sbk-obj:%*s   mtx A=%d B=%d C=%d D=%d X=%d Y=%d base=(%u,%u)\n", depth * 2, "",
                   (int)m->A, (int)m->B, (int)m->C, (int)m->D, m->X, m->Y, m->BaseScaleX, m->BaseScaleY);
            break;
        }
    }
}

/* A display list, vertex array or uObj the dumper is willing to dereference:
 * `size` bytes inside the emulated RDRAM, on the 8-byte boundary the RSP's own
 * DMA requires, and nothing else.  `room`, when asked for, comes back as the
 * bytes left between the address and the end of RDRAM, so a walk that starts
 * near the top of memory stops instead of running off it.
 *
 * A dump follows branches the game never takes -- a G_DL whose target the RSP
 * would have skipped, a stale word left in a reused buffer -- so it *will* be
 * handed nonsense.  The old check looked at the **unresolved** command word:
 * anything below 0x10000000 was called a segmented address and let through
 * whatever the segment base happened to be, and anything at or above it was
 * dereferenced as a native pointer.  Both holes are how `--dumpdl` on a race
 * task walked into unmapped memory and took the process down with SIGSEGV. */
static const void *dump_addr_ok(uint32_t w1, uint32_t size, uint32_t *room) {
    uint32_t a = w1;
    uint32_t off;
    if (a < 0x10000000u) {
        a = dump_seg[(a >> 24) & 0xF] + (a & 0x00FFFFFFu);
    }
    if (a >= SBK_RDRAM_BASE && a < SBK_RDRAM_BASE + SBK_RDRAM_SIZE) {
        off = a - SBK_RDRAM_BASE;
    } else if (a < SBK_RDRAM_SIZE) {
        off = a;                       /* an N64 "physical" RDRAM address */
    } else {
        return NULL;                   /* unmapped, or a native pointer we cannot vet */
    }
    if ((off & 7u) != 0 || size > SBK_RDRAM_SIZE - off) {
        return NULL;
    }
    if (room != NULL) {
        *room = SBK_RDRAM_SIZE - off;
    }
    return sbk_phys_to_host(a);
}

static unsigned dump_lines_left;

static void dump_dl(const Gfx *dl, unsigned max, int depth, int s2dex) {
    unsigned i;
    if (dl == NULL) {
        return;
    }
    for (i = 0; i < max && dump_lines_left > 0; i++) {
        dump_lines_left--;
        unsigned op = dl[i].words.w0 >> 24;
        const char *obj = s2dex ? s2dex_name(op) : NULL;
        printf("sbk-dl:%*s %08x %08x%s%s\n", depth * 2, "", (unsigned)dl[i].words.w0, (unsigned)dl[i].words.w1,
               obj != NULL ? "  " : "", obj != NULL ? obj : "");
        if (obj != NULL && op != (uint8_t)G_OBJ_RENDERMODE && op != G_SELECT_DL) {
            dump_obj(op, (uint32_t)dl[i].words.w1, depth);
        }
        if (!s2dex && op == (uint8_t)G_VTX) { /* F3DEX: n in bits 10..15, v0*2 in 16..23 */
            unsigned n = (dl[i].words.w0 >> 10) & 0x3F, k;
            const Vtx *v = (const Vtx *)dump_addr_ok((uint32_t)dl[i].words.w1, n * sizeof(Vtx), NULL);
            for (k = 0; k < n && v != NULL; k++) {
                printf("sbk-vtx:%*s   [%u] %d %d %d  st %d %d  rgba %02x%02x%02x%02x\n", depth * 2, "", k,
                       v[k].v.ob[0], v[k].v.ob[1], v[k].v.ob[2], v[k].v.tc[0], v[k].v.tc[1],
                       v[k].v.cn[0], v[k].v.cn[1], v[k].v.cn[2], v[k].v.cn[3]);
            }
        }
        if (op == (uint8_t)G_MOVEWORD && (dl[i].words.w0 & 0xFF) == G_MW_SEGMENT) {
            dump_seg[(((dl[i].words.w0 >> 8) & 0xFFFF) / 4) & 0xF] = (uint32_t)dl[i].words.w1; /* keep the dump's view current */
        }
        if (op == (uint8_t)G_DL && depth < 4) {
            unsigned kind = (dl[i].words.w0 >> 16) & 0xFF;
            uint32_t target = (uint32_t)dl[i].words.w1;
            uint32_t room = 0;
            const Gfx *sub = (const Gfx *)dump_addr_ok(target, sizeof(Gfx), &room);
            if (sub != NULL) {
                unsigned fit = (unsigned)(room / sizeof(Gfx));
                dump_dl(sub, fit < 2048 ? fit : 2048, depth + 1, s2dex);
            }
            if (kind == G_DL_NOPUSH) break; /* branch, not call */
        }
        if (op == (uint8_t)G_ENDDL) break;
    }
}

int sbk_dump_tris;
int sbk_dump_frames = 3; /* --dumpframes N: presented frames to dump from --dumpdl on */
int sbk_dump_tasks_left;  /* --dumpdlat R: gfx tasks still to dump, armed at a retrace */

/* Dump one task's list with a fresh snapshot of the segment table. */
static void dump_task(int count, const OSTask *task, const Gfx *dl, int s2dex, unsigned max) {
    printf("sbk-dl: task %d, %u commands at %p\n", count,
           (unsigned)(task->t.data_size / sizeof(Gfx)), (const void *)dl);
    printf("sbk-dl: microcode %s\n", s2dex ? "S2DEX" : "F3DEX2");
    gfx_debug_get_segments(dump_seg);
    dump_lines_left = 40000;
    dump_dl(dl, max, 0, s2dex);
}

void sbk_gfx_task(OSTask *task) {
    Gfx *dl = (Gfx *)sbk_phys_to_host((uint32_t)(uintptr_t)task->t.data_ptr);
    int s2dex = task_is_s2dex(task);
    static int count;
    count++;
    extern int sbk_tri_dump_all;
    extern int sbk_dump_tris;
    { extern int sbk_frame_dump_left; sbk_tri_dump_all = sbk_dump_tris && ((sbk_dump_task > 0 && count >= sbk_dump_task && count < sbk_dump_task + 4) || sbk_frame_dump_left > 0); }
    if (count == sbk_dump_task) {
        extern int sbk_tex_dump_left;
        extern void gfx_debug_flush_texture_cache(void);
        extern int sbk_frame_dump_left;
        gfx_debug_flush_texture_cache();
        { extern int sbk_tri_drawn; sbk_tri_drawn = 0; }
        sbk_tex_dump_left = 120;
        sbk_frame_dump_left = sbk_dump_frames;
    }
    if ((sbk_trace && count <= 6) || (sbk_dump_task && count >= sbk_dump_task && count < sbk_dump_task + 4)) {
        unsigned n = task->t.data_size / sizeof(Gfx);
        dump_task(count, task, dl, s2dex, count >= sbk_dump_task ? n : 96);
    } else if (sbk_dump_tasks_left > 0) {
        /* --dumpdlat: the interesting list is the one on screen at a given
         * retrace, and a gfx task count is no way to find it. */
        sbk_dump_tasks_left--;
        dump_task(count, task, dl, s2dex, (unsigned)(task->t.data_size / sizeof(Gfx)));
    }
    gfx_run_ucode(dl, s2dex);
    if (sbk_gfx_bad_dl) {
        static int dumped;
        sbk_gfx_bad_dl = 0;
        if (!dumped) {
            dumped = 1;
            printf("sbk-dl: bad task %d\n", count);
            dump_task(count, task, dl, s2dex, (unsigned)(task->t.data_size / sizeof(Gfx)));
        }
    }
}
