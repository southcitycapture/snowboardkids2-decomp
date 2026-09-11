/* S2DEX: the N64's 2D sprite microcode, interpreted on top of gfx_pc's RDP
 * state machine.
 *
 * The sequel picks a microcode per graphics task (microcodeGroups[] in
 * src/graphics/graphics.c): F3DEX2 for the 3D viewports, gspS2DEX_fifo for the
 * 2D ones.  An S2DEX list is mostly ordinary RDP work -- combiners, blender
 * modes, texture loads, texture rectangles -- with the microcode's own object
 * commands mixed in, and those are what the F3DEX2 interpreter cannot read:
 * its opcode 0x01 is G_VTX where S2DEX's is G_OBJ_RECTANGLE.
 *
 * So this file owns the control flow of an S2DEX list: it executes the object
 * commands itself and hands every run of plain RDP commands back to
 * gfx_pc_run_dl() unchanged.  The F3DEX2 path never comes through here.
 *
 * The object commands address TMEM directly, which gfx_pc does not model: its
 * "loaded texture" is a pointer into RDRAM plus a line stride.  G_OBJ_LOADTXTR
 * therefore only records where in RDRAM each TMEM word came from (tmem_map
 * below), and a sprite's imageAdrs is resolved against that table when it is
 * drawn, at which point the load is replayed as the RDP commands gfx_pc
 * already knows.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>
#include <PR/gs2dex.h>

#include "gfx_pc.h"

int sbk_s2dex_trace;       /* --s2dextrace: log the first few of every object command */
static int trace_left;

unsigned sbk_s2dex_counts[16];   /* census by object command, for the report */
unsigned sbk_s2dex_unknown;

#define TRACE(...) do { if (sbk_s2dex_trace && trace_left > 0) { trace_left--; printf(__VA_ARGS__); } } while (0)

/* ---------------------------------------------------------------------------
 * TMEM
 * ------------------------------------------------------------------------ */

#define TMEM_ENTRIES 8

static struct {
    uint32_t tmem;   /* TMEM address in 64-bit words */
    uint32_t words;  /* how many words the load covered */
    uint32_t image;  /* N64 address of the first of them */
} tmem_map[TMEM_ENTRIES];
static int tmem_map_n;

static void tmem_record(uint32_t tmem, uint32_t words, uint32_t image) {
    int i;
    for (i = 0; i < tmem_map_n; i++) {
        if (tmem_map[i].tmem == tmem) {
            tmem_map[i].words = words;
            tmem_map[i].image = image;
            return;
        }
    }
    if (tmem_map_n < TMEM_ENTRIES) {
        i = tmem_map_n++;
    } else {
        i = 0; /* oldest */
        memmove(&tmem_map[0], &tmem_map[1], (TMEM_ENTRIES - 1) * sizeof(tmem_map[0]));
        i = TMEM_ENTRIES - 1;
    }
    tmem_map[i].tmem = tmem;
    tmem_map[i].words = words;
    tmem_map[i].image = image;
}

/* The RDRAM address a sprite's imageAdrs stands for, or 0. */
static uint32_t tmem_lookup(uint32_t adrs, uint32_t *words_left) {
    int i;
    for (i = tmem_map_n - 1; i >= 0; i--) {
        if (adrs >= tmem_map[i].tmem && adrs < tmem_map[i].tmem + tmem_map[i].words) {
            if (words_left != NULL) {
                *words_left = tmem_map[i].tmem + tmem_map[i].words - adrs;
            }
            return tmem_map[i].image + (adrs - tmem_map[i].tmem) * 8;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Microcode state
 * ------------------------------------------------------------------------ */

static uObjMtx_t objmtx = { 1 << 16, 0, 0, 1 << 16, 0, 0, 1 << 10, 1 << 10 };
static uint32_t objrm;          /* G_OBJRM_* from G_OBJ_RENDERMODE */
static uint32_t genstat[4];     /* gSPSetStatus / G_SELECT_DL */
static uint32_t rdphalf_0_w0, rdphalf_0_w1;

/* ---------------------------------------------------------------------------
 * Synthesised RDP commands
 * ------------------------------------------------------------------------ */

static void rdp_run(Gfx *dl, int n) {
    dl[n].words.w0 = (uint32_t)G_ENDDL << 24;
    dl[n].words.w1 = 0;
    gfx_pc_run_dl(dl);
}

static int put_settimg(Gfx *dl, int n, uint32_t fmt, uint32_t siz, uint32_t width, uint32_t addr) {
    dl[n].words.w0 = ((uint32_t)G_SETTIMG << 24) | (fmt << 21) | (siz << 19) | ((width - 1) & 0xFFF);
    dl[n].words.w1 = addr;
    return n + 1;
}

static int put_settile(Gfx *dl, int n, uint32_t fmt, uint32_t siz, uint32_t line, uint32_t tmem,
                       uint32_t tile, uint32_t pal, uint32_t cms, uint32_t cmt) {
    dl[n].words.w0 = ((uint32_t)G_SETTILE << 24) | (fmt << 21) | (siz << 19) |
                     ((line & 0x1FF) << 9) | (tmem & 0x1FF);
    dl[n].words.w1 = (tile << 24) | ((pal & 0xF) << 20) | ((cmt & 3) << 18) | ((cms & 3) << 8);
    return n + 1;
}

static int put_loadblock(Gfx *dl, int n, uint32_t tile, uint32_t lrs, uint32_t dxt) {
    dl[n].words.w0 = (uint32_t)G_LOADBLOCK << 24;
    dl[n].words.w1 = (tile << 24) | ((lrs & 0xFFF) << 12) | (dxt & 0xFFF);
    return n + 1;
}

static int put_loadtile(Gfx *dl, int n, uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    dl[n].words.w0 = ((uint32_t)G_LOADTILE << 24) | ((uls & 0xFFF) << 12) | (ult & 0xFFF);
    dl[n].words.w1 = (tile << 24) | ((lrs & 0xFFF) << 12) | (lrt & 0xFFF);
    return n + 1;
}

static int put_settilesize(Gfx *dl, int n, uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    dl[n].words.w0 = ((uint32_t)G_SETTILESIZE << 24) | ((uls & 0xFFF) << 12) | (ult & 0xFFF);
    dl[n].words.w1 = (tile << 24) | ((lrs & 0xFFF) << 12) | (lrt & 0xFFF);
    return n + 1;
}

static uint32_t texel_bits(uint32_t siz) {
    switch (siz) {
        case G_IM_SIZ_4b:  return 4;
        case G_IM_SIZ_8b:  return 8;
        case G_IM_SIZ_16b: return 16;
        default:           return 32;
    }
}

/* gfx_pc turns G_LOADBLOCK's lrs into a byte count by size, so go the other way. */
static uint32_t block_lrs_for_bytes(uint32_t siz, uint32_t bytes) {
    uint32_t shift = siz == G_IM_SIZ_16b ? 1 : siz == G_IM_SIZ_32b ? 2 : 0;
    uint32_t units = bytes >> shift;
    return units != 0 ? units - 1 : 0;
}

/* ---------------------------------------------------------------------------
 * Sprites
 * ------------------------------------------------------------------------ */

/* Load a sprite's texels out of the RDRAM its TMEM address stands for and make
 * them the render tile.  Returns 0 if the texture is not known. */
static int sprite_tile(const uObjSprite_t *sp) {
    Gfx dl[8];
    int n = 0;
    uint32_t words_left = 0;
    uint32_t addr = tmem_lookup(sp->imageAdrs, &words_left);
    uint32_t w = sp->imageW >> 5, h = sp->imageH >> 5;
    uint32_t stride = sp->imageStride;
    uint32_t line_bytes, bytes;
    uint32_t clamp = (objrm & G_OBJRM_NOTXCLAMP) ? G_TX_WRAP : G_TX_CLAMP;

    if (addr == 0 || w == 0 || h == 0 || stride == 0) {
        TRACE("sbk-s2dex: sprite with no texture: adrs=%u w=%u h=%u stride=%u\n",
              sp->imageAdrs, w, h, stride);
        return 0;
    }

    line_bytes = stride * 8;
    bytes = line_bytes * h;
    if (bytes > words_left * 8) {
        bytes = words_left * 8;
    }
    if (bytes > 4096) {
        bytes = 4096;   /* TMEM is 4 KB; so is gfx_pc's idea of a loaded texture */
    }
    if (bytes < line_bytes) {
        return 0;
    }

    n = put_settimg(dl, n, sp->imageFmt, sp->imageSiz, stride * 16 / (texel_bits(sp->imageSiz) / 4), addr);
    n = put_settile(dl, n, sp->imageFmt, sp->imageSiz, 0, 0, G_TX_LOADTILE, 0, 0, 0);
    n = put_loadblock(dl, n, G_TX_LOADTILE, block_lrs_for_bytes(sp->imageSiz, bytes), 0);
    n = put_settile(dl, n, sp->imageFmt, sp->imageSiz, stride, 0, G_TX_RENDERTILE, sp->imagePal, clamp, clamp);
    n = put_settilesize(dl, n, G_TX_RENDERTILE, 0, 0, (w - 1) << 2, (h - 1) << 2);
    rdp_run(dl, n);
    return 1;
}

/* The texture coordinates of a sprite, honouring its flip flags. */
static void sprite_uv(const uObjSprite_t *sp, int16_t *uls, int16_t *ult, int16_t *dsdx, int16_t *dtdy,
                      int32_t sdx, int32_t sdy) {
    int32_t w = sp->imageW, h = sp->imageH;      /* S10.5 texels */

    if (sp->imageFlags & G_OBJ_FLAG_FLIPS) {
        *uls = (int16_t)(w - 32);
        *dsdx = (int16_t)-sdx;
    } else {
        *uls = 0;
        *dsdx = (int16_t)sdx;
    }
    if (sp->imageFlags & G_OBJ_FLAG_FLIPT) {
        *ult = (int16_t)(h - 32);
        *dtdy = (int16_t)-sdy;
    } else {
        *ult = 0;
        *dtdy = (int16_t)sdy;
    }
}

static int32_t scaled_span(uint32_t image, uint32_t scale) {
    if (scale == 0) {
        scale = 1 << 10;
    }
    return (int32_t)(((uint64_t)image * 128) / scale);  /* texels(10.5) -> pixels(10.2) */
}

/* G_OBJ_RECTANGLE / G_OBJ_RECTANGLE_R */
static void obj_rectangle(const uObjSprite_t *sp, int relative) {
    int32_t ulx, uly, lrx, lry;
    int32_t sdx = sp->scaleW, sdy = sp->scaleH;
    int16_t uls, ult, dsdx, dtdy;

    if (!sprite_tile(sp)) {
        return;
    }

    if (relative) {
        uint32_t bsx = objmtx.BaseScaleX != 0 ? objmtx.BaseScaleX : (1 << 10);
        uint32_t bsy = objmtx.BaseScaleY != 0 ? objmtx.BaseScaleY : (1 << 10);
        ulx = objmtx.X + (int32_t)(((int64_t)sp->objX * (int32_t)bsx) >> 10);
        uly = objmtx.Y + (int32_t)(((int64_t)sp->objY * (int32_t)bsy) >> 10);
        lrx = ulx + (int32_t)(((int64_t)scaled_span(sp->imageW, sp->scaleW) * (int32_t)bsx) >> 10);
        lry = uly + (int32_t)(((int64_t)scaled_span(sp->imageH, sp->scaleH) * (int32_t)bsy) >> 10);
        sdx = (int32_t)(((uint64_t)sp->scaleW << 10) / bsx);
        sdy = (int32_t)(((uint64_t)sp->scaleH << 10) / bsy);
    } else {
        ulx = sp->objX;
        uly = sp->objY;
        lrx = ulx + scaled_span(sp->imageW, sp->scaleW);
        lry = uly + scaled_span(sp->imageH, sp->scaleH);
    }

    sprite_uv(sp, &uls, &ult, &dsdx, &dtdy, sdx, sdy);
    TRACE("sbk-s2dex: rect%s (%d,%d)-(%d,%d) %ux%u fmt=%u siz=%u pal=%u adrs=%u stride=%u flags=%02x\n",
          relative ? "_r" : "", ulx / 4, uly / 4, lrx / 4, lry / 4, sp->imageW >> 5, sp->imageH >> 5,
          sp->imageFmt, sp->imageSiz, sp->imagePal, sp->imageAdrs, sp->imageStride, sp->imageFlags);

    if (lrx <= ulx || lry <= uly) {
        return;
    }
    gfx_pc_tex_rect(ulx, uly, lrx, lry, G_TX_RENDERTILE, uls, ult, dsdx, dtdy, 0);
}

/* G_OBJ_SPRITE: the sprite through the full 2x2 of the object matrix. */
static void obj_sprite(const uObjSprite_t *sp) {
    float xs[4], ys[4], us[4], vs[4];
    float x0, y0, x1, y1;
    float a = objmtx.A / 65536.0f, b = objmtx.B / 65536.0f;
    float c = objmtx.C / 65536.0f, d = objmtx.D / 65536.0f;
    float ox = objmtx.X, oy = objmtx.Y;      /* U10.2, like the corners */
    float u0 = 0.0f, v0 = 0.0f, u1 = (float)sp->imageW, v1 = (float)sp->imageH;
    int i;

    if (!sprite_tile(sp)) {
        return;
    }

    x0 = (float)sp->objX;
    y0 = (float)sp->objY;
    x1 = x0 + (float)scaled_span(sp->imageW, sp->scaleW);
    y1 = y0 + (float)scaled_span(sp->imageH, sp->scaleH);

    if (sp->imageFlags & G_OBJ_FLAG_FLIPS) { float t = u0; u0 = u1 - 32.0f; u1 = t; }
    if (sp->imageFlags & G_OBJ_FLAG_FLIPT) { float t = v0; v0 = v1 - 32.0f; v1 = t; }

    xs[0] = x0; ys[0] = y0; us[0] = u0; vs[0] = v0;   /* upper left  */
    xs[1] = x0; ys[1] = y1; us[1] = u0; vs[1] = v1;   /* lower left  */
    xs[2] = x1; ys[2] = y1; us[2] = u1; vs[2] = v1;   /* lower right */
    xs[3] = x1; ys[3] = y0; us[3] = u1; vs[3] = v0;   /* upper right */

    for (i = 0; i < 4; i++) {
        float x = xs[i], y = ys[i];
        xs[i] = a * x + b * y + ox;
        ys[i] = c * x + d * y + oy;
    }
    TRACE("sbk-s2dex: sprite (%.0f,%.0f) %ux%u mtx A=%.3f B=%.3f C=%.3f D=%.3f X=%d Y=%d\n",
          xs[0] / 4.0f, ys[0] / 4.0f, sp->imageW >> 5, sp->imageH >> 5, a, b, c, d, objmtx.X / 4, objmtx.Y / 4);
    gfx_pc_tex_quad(xs, ys, us, vs);
}

/* ---------------------------------------------------------------------------
 * Loading into TMEM
 * ------------------------------------------------------------------------ */

static void obj_loadtxtr(const uObjTxtr *tx) {
    uint32_t type = tx->block.type;
    uint32_t image = gfx_pc_seg_n64((uint32_t)(uintptr_t)tx->block.image);

    if (type == G_OBJLT_TXTRBLOCK) {
        uint32_t words = (uint32_t)tx->block.tsize + 1;
        tmem_record(tx->block.tmem, words, image);
        TRACE("sbk-s2dex: loadtxtr block tmem=%u words=%u image=%08x\n", tx->block.tmem, words, image);
    } else if (type == G_OBJLT_TXTRTILE) {
        uint32_t words = (uint32_t)((tx->tile.twidth + 1) >> 2) * (uint32_t)((tx->tile.theight + 1) >> 2);
        tmem_record(tx->tile.tmem, words, image);
        TRACE("sbk-s2dex: loadtxtr tile tmem=%u words=%u image=%08x\n", tx->tile.tmem, words, image);
    } else if (type == G_OBJLT_TLUT) {
        Gfx dl[4];
        int n = 0;
        uint32_t count = (uint32_t)tx->tlut.pnum + 1;
        n = put_settimg(dl, n, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, image);
        n = put_settile(dl, n, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, tx->tlut.phead, G_TX_LOADTILE, 0, 0, 0);
        dl[n].words.w0 = (uint32_t)G_LOADTLUT << 24;
        dl[n].words.w1 = ((uint32_t)G_TX_LOADTILE << 24) | (((count - 1) & 0x3FF) << 14);
        n++;
        rdp_run(dl, n);
        TRACE("sbk-s2dex: loadtxtr tlut phead=%u count=%u image=%08x\n", tx->tlut.phead, count, image);
    } else {
        TRACE("sbk-s2dex: loadtxtr of unknown type %08x\n", type);
    }
}

/* ---------------------------------------------------------------------------
 * Backgrounds
 * ------------------------------------------------------------------------ */

/* One horizontal band of a background image: load it and draw it. */
static void bg_band(uint32_t image, uint32_t fmt, uint32_t siz, uint32_t imageW,
                    uint32_t srcX, uint32_t srcY, uint32_t srcW, uint32_t srcH,
                    int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, int flips) {
    Gfx dl[8];
    int n = 0;
    uint32_t bits = texel_bits(siz);
    uint32_t row_bytes = (srcW * bits + 7) / 8;
    uint32_t line = (row_bytes + 7) / 8;
    int16_t dsdx, dtdy, uls, ult;

    if (srcW == 0 || srcH == 0 || lrx <= ulx || lry <= uly) {
        return;
    }
    n = put_settimg(dl, n, fmt, siz, imageW, image);
    n = put_settile(dl, n, fmt, siz, line, 0, G_TX_LOADTILE, 0, 0, 0);
    n = put_loadtile(dl, n, G_TX_LOADTILE, srcX << 2, srcY << 2, (srcX + srcW - 1) << 2, (srcY + srcH - 1) << 2);
    n = put_settile(dl, n, fmt, siz, line, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, G_TX_CLAMP);
    n = put_settilesize(dl, n, G_TX_RENDERTILE, srcX << 2, srcY << 2, (srcX + srcW - 1) << 2, (srcY + srcH - 1) << 2);
    rdp_run(dl, n);

    dsdx = (int16_t)(((uint32_t)srcW << 10) / (uint32_t)((lrx - ulx) / 4 > 0 ? (lrx - ulx) / 4 : 1));
    dtdy = (int16_t)(((uint32_t)srcH << 10) / (uint32_t)((lry - uly) / 4 > 0 ? (lry - uly) / 4 : 1));
    uls = (int16_t)(srcX << 5);
    ult = (int16_t)(srcY << 5);
    if (flips) {
        uls = (int16_t)((srcX + srcW - 1) << 5);
        dsdx = (int16_t)-dsdx;
    }
    gfx_pc_tex_rect(ulx, uly, lrx, lry, G_TX_RENDERTILE, uls, ult, dsdx, dtdy, 0);
}

static void obj_bg(const uObjBg *bg, int scalable) {
    uint32_t image = gfx_pc_seg_n64((uint32_t)(uintptr_t)bg->b.imagePtr);
    uint32_t fmt = bg->b.imageFmt, siz = bg->b.imageSiz;
    uint32_t imageW = bg->b.imageW >> 2;                 /* u10.2 -> texels */
    uint32_t imageH = bg->b.imageH >> 2;
    uint32_t scaleW = scalable ? bg->s.scaleW : (1 << 10);
    uint32_t scaleH = scalable ? bg->s.scaleH : (1 << 10);
    int32_t frameX = bg->b.frameX, frameY = bg->b.frameY;     /* s10.2 pixels */
    uint32_t frameW = bg->b.frameW, frameH = bg->b.frameH;
    uint32_t srcX = bg->b.imageX >> 5;
    int32_t srcY0 = scalable ? (bg->s.imageYorig >> 5) : (int32_t)(bg->b.imageY >> 5);
    uint32_t bits = texel_bits(siz);
    uint32_t frame_px_w = frameW >> 2, frame_px_h = frameH >> 2;
    uint32_t src_w = (frame_px_w * scaleW + 1023) >> 10;
    uint32_t row_bytes, rows, y;
    int flips = (bg->b.imageFlip & G_BG_FLAG_FLIPS) != 0;

    if (scaleW == 0) scaleW = 1 << 10;
    if (scaleH == 0) scaleH = 1 << 10;
    if (image == 0 || imageW == 0 || imageH == 0 || frame_px_w == 0 || frame_px_h == 0) {
        return;
    }
    if (src_w > imageW) {
        src_w = imageW;
    }
    row_bytes = (src_w * bits + 7) / 8;
    rows = row_bytes != 0 ? 4096 / row_bytes : 1;
    if (rows == 0) rows = 1;
    if (rows > 32) rows = 32;
    if (rows > 2 && ((rows - 1) & (rows - 2)) == 0) {
        rows--;    /* gfx_pc trims a row when (h - 1) is a power of two */
    }

    TRACE("sbk-s2dex: bg%s image=%08x %ux%u fmt=%u siz=%u frame (%d,%d) %ux%u src (%u,%d) scale %u/%u\n",
          scalable ? "_1cyc" : "_copy", image, imageW, imageH, fmt, siz, frameX / 4, frameY / 4,
          frame_px_w, frame_px_h, srcX, srcY0, scaleW, scaleH);

    for (y = 0; y < frame_px_h;) {
        uint32_t band_px = (uint32_t)(((uint64_t)rows << 10) / scaleH);
        int32_t src_row;
        uint32_t band_src;

        if (band_px == 0) {
            band_px = 1;
        }
        if (y + band_px > frame_px_h) {
            band_px = frame_px_h - y;
        }
        src_row = srcY0 + (int32_t)(((uint64_t)y * scaleH) >> 10);
        while (src_row < 0) {
            src_row += (int32_t)imageH;
        }
        src_row %= (int32_t)imageH;
        band_src = (uint32_t)(((uint64_t)band_px * scaleH + 1023) >> 10);
        if (band_src == 0) {
            band_src = 1;
        }
        if ((uint32_t)src_row + band_src > imageH) {
            band_src = imageH - (uint32_t)src_row;      /* stop at the wrap; the next band restarts */
            band_px = (uint32_t)(((uint64_t)band_src << 10) / scaleH);
            if (band_px == 0) {
                band_px = 1;
            }
        }
        bg_band(image, fmt, siz, imageW, srcX, (uint32_t)src_row, src_w, band_src,
                frameX, frameY + (int32_t)((y) * 4), frameX + (int32_t)(frame_px_w * 4),
                frameY + (int32_t)((y + band_px) * 4), flips);
        y += band_px;
    }
}

/* ---------------------------------------------------------------------------
 * The list
 * ------------------------------------------------------------------------ */

/* Everything that is not an object command is ordinary RDP/RSP work that
 * gfx_pc already knows.  G_OBJ_RECTANGLE_R, G_OBJ_MOVEMEM and the low opcodes
 * are the ones whose F3DEX2 meaning differs. */
static int is_object_command(uint32_t op, const Gfx *cmd) {
    switch (op) {
        case G_OBJ_RECTANGLE:      /* 0x01 */
        case G_OBJ_SPRITE:         /* 0x02 */
        case G_SELECT_DL:          /* 0x04 */
        case G_OBJ_LOADTXTR:       /* 0x05 */
        case G_OBJ_LDTX_SPRITE:    /* 0x06 */
        case G_OBJ_LDTX_RECT:      /* 0x07 */
        case G_OBJ_LDTX_RECT_R:    /* 0x08 */
        case G_BG_1CYC:            /* 0x09 */
        case G_BG_COPY:            /* 0x0a */
        case G_OBJ_RENDERMODE:     /* 0x0b */
        case (uint8_t)G_OBJ_RECTANGLE_R: /* 0xda, G_MTX under F3DEX2 */
        case (uint8_t)G_OBJ_MOVEMEM:     /* 0xdc, G_MOVEMEM under F3DEX2 */
        case (uint8_t)G_DL:
        case (uint8_t)G_ENDDL:
            return 1;
        case (uint8_t)G_RDPHALF_0:
            /* 0xe4 is also G_TEXRECT: only a G_SELECT_DL behind it makes it
             * the microcode's own half-word. */
            return ((cmd[1].words.w0 >> 24) == G_SELECT_DL);
        default:
            return 0;
    }
}

#define PASSTHROUGH_MAX 64

static void s2dex_run_dl(Gfx *cmd, int depth) {
    static Gfx run[PASSTHROUGH_MAX + 1];

    if (depth > 8) {
        return;
    }
    for (;;) {
        uint32_t op = cmd->words.w0 >> 24;

        if (!is_object_command(op, cmd)) {
            int n = 0;
            while (n < PASSTHROUGH_MAX && !is_object_command(cmd->words.w0 >> 24, cmd)) {
                run[n++] = *cmd++;
            }
            rdp_run(run, n);
            continue;
        }

        /* census, for the per-10-second line in the log: the two high object
         * opcodes are folded into the spare slots 0x0c and 0x0d */
        if (op <= 0x0B) {
            sbk_s2dex_counts[op]++;
        } else if (op == (uint8_t)G_OBJ_RECTANGLE_R) {
            sbk_s2dex_counts[0x0C]++;
        } else if (op == (uint8_t)G_OBJ_MOVEMEM) {
            sbk_s2dex_counts[0x0D]++;
        }

        switch (op) {
            case G_OBJ_RENDERMODE:
                objrm = cmd->words.w1;
                gfx_pc_set_texture_filter((objrm & G_OBJRM_BILERP) != 0);
                break;

            case G_OBJ_LOADTXTR:
                obj_loadtxtr((const uObjTxtr *)gfx_pc_seg_addr(cmd->words.w1));
                break;

            case G_OBJ_RECTANGLE:
                obj_rectangle((const uObjSprite_t *)gfx_pc_seg_addr(cmd->words.w1), 0);
                break;

            case (uint8_t)G_OBJ_RECTANGLE_R:
                obj_rectangle((const uObjSprite_t *)gfx_pc_seg_addr(cmd->words.w1), 1);
                break;

            case G_OBJ_SPRITE:
                obj_sprite((const uObjSprite_t *)gfx_pc_seg_addr(cmd->words.w1));
                break;

            case G_OBJ_LDTX_SPRITE:
            case G_OBJ_LDTX_RECT:
            case G_OBJ_LDTX_RECT_R: {
                const uObjTxSprite *ts = (const uObjTxSprite *)gfx_pc_seg_addr(cmd->words.w1);
                obj_loadtxtr(&ts->txtr);
                if (op == G_OBJ_LDTX_SPRITE) {
                    obj_sprite(&ts->sprite.s);
                } else {
                    obj_rectangle(&ts->sprite.s, op == G_OBJ_LDTX_RECT_R);
                }
                break;
            }

            case (uint8_t)G_OBJ_MOVEMEM: {
                uint32_t index = (cmd->words.w0 >> 16) & 0xFF;
                const void *src = gfx_pc_seg_addr(cmd->words.w1);
                if (index == 23) {                    /* gSPObjMatrix */
                    memcpy(&objmtx, src, sizeof(uObjMtx_t));
                } else {                              /* gSPObjSubMatrix */
                    const uObjSubMtx_t *sm = (const uObjSubMtx_t *)src;
                    objmtx.X = sm->X;
                    objmtx.Y = sm->Y;
                    objmtx.BaseScaleX = sm->BaseScaleX;
                    objmtx.BaseScaleY = sm->BaseScaleY;
                }
                break;
            }

            case G_BG_1CYC:
                obj_bg((const uObjBg *)gfx_pc_seg_addr(cmd->words.w1), 1);
                break;

            case G_BG_COPY:
                obj_bg((const uObjBg *)gfx_pc_seg_addr(cmd->words.w1), 0);
                break;

            case (uint8_t)G_RDPHALF_0:
                rdphalf_0_w0 = cmd->words.w0;
                rdphalf_0_w1 = cmd->words.w1;
                break;

            case G_SELECT_DL: {
                /* gSPSelectDL splits the target address and the test across the
                 * G_RDPHALF_0 ahead of it: sid and the low half there, the high
                 * half and the mask here. */
                uint32_t sid = ((rdphalf_0_w0 >> 16) & 0xFF) >> 2;
                uint32_t flag = rdphalf_0_w1;
                uint32_t mask = cmd->words.w1;
                uint32_t target = ((cmd->words.w0 & 0xFFFF) << 16) | (rdphalf_0_w0 & 0xFFFF);
                int push = ((cmd->words.w0 >> 16) & 0xFF) == G_DL_PUSH;

                if ((genstat[sid & 3] & mask) == flag) {
                    Gfx *next = (Gfx *)gfx_pc_seg_addr(target);
                    if (push) {
                        s2dex_run_dl(next, depth + 1);
                    } else {
                        cmd = next;
                        continue;
                    }
                }
                break;
            }

            case (uint8_t)G_DL:
                if (((cmd->words.w0 >> 16) & 0xFF) == G_DL_NOPUSH) {
                    cmd = (Gfx *)gfx_pc_seg_addr(cmd->words.w1);
                    continue;
                }
                s2dex_run_dl((Gfx *)gfx_pc_seg_addr(cmd->words.w1), depth + 1);
                break;

            case (uint8_t)G_ENDDL:
                return;

            default:
                sbk_s2dex_unknown++;
                break;
        }
        cmd++;
    }
}

void gfx_s2dex_run(Gfx *commands) {
    if (sbk_s2dex_trace && trace_left == 0) {
        trace_left = 400;
        sbk_s2dex_trace = 0;   /* one task's worth, then quiet */
    }
    s2dex_run_dl(commands, 0);
}
