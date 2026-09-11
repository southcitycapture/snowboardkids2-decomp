/* Overlay dispatch: see port/tools/gen_overlays.py. */
#ifndef SBK_OVERLAY_H
#define SBK_OVERLAY_H

#include <stdint.h>

typedef void (*sbk_ov_fn)(void);

struct sbk_ov_entry {
    int slot;
    sbk_ov_fn fn;
};

struct sbk_ov_segment {
    const char *name;
    uint32_t rom_start;          /* the value of NAME_ROM_START in the N64 build */
    const struct sbk_ov_entry *entries;
    int count;
    int group_lo;   /* the group's slot range: every overlay sharing this VRAM */
    int group_hi;
};

extern sbk_ov_fn sbk_ov_slot[];
extern const int sbk_ov_slot_count;
extern const struct sbk_ov_segment sbk_ov_segments[];

/* Called from the port's hook in dmaLoadAndInvalidate with the segment's
 * NAME_ROM_START: point that group's slots at this overlay's functions. */
void sbk_overlay_activate(void *rom_start);

#endif
