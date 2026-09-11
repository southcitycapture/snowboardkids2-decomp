#ifndef GFX_PC_H
#define GFX_PC_H

#include <stdint.h>
#include <stdbool.h>
#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

struct GfxRenderingAPI;
struct GfxWindowManagerAPI;

struct GfxDimensions {
    uint32_t width, height;
    float aspect_ratio;
};

extern struct GfxDimensions gfx_current_dimensions;

void gfx_init(struct GfxWindowManagerAPI *wapi, struct GfxRenderingAPI *rapi, const char *game_name, bool start_in_fullscreen);
struct GfxRenderingAPI *gfx_get_current_rendering_api(void);

/* Interpret one display list (one RSP graphics task) into the back buffer. */
void gfx_run(Gfx *commands);
/* The same, told which microcode the task asked for: 0 = F3DEX2, 1 = S2DEX. */
void gfx_run_ucode(Gfx *commands, int s2dex);

/* S2DEX (port/src/gfx/gfx_s2dex.c): the 2D interpreter, and the parts of the
 * RDP state machine above that it reuses. */
void gfx_s2dex_run(Gfx *commands);
void gfx_pc_run_dl(Gfx *cmd);
void *gfx_pc_seg_addr(uint32_t w1);
uint32_t gfx_pc_seg_n64(uint32_t w1);
void gfx_pc_tex_rect(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, uint8_t tile,
                     int16_t uls, int16_t ult, int16_t dsdx, int16_t dtdy, int flip);
void gfx_pc_tex_quad(const float *xs, const float *ys, const float *us, const float *vs);
void gfx_pc_set_texture_filter(int bilerp);
/* The game swapped a framebuffer in: show what has been drawn. */
void gfx_present(void);
/* Pump window events (called by the host loop once per retrace). */
void gfx_handle_events(void);

#endif
