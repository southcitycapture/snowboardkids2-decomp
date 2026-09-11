/* Overlay dispatch at run time.
 *
 * The game loads an overlay with dmaLoadAndInvalidate(&NAME_ROM_START, ...).
 * The DMA itself still happens -- it writes MIPS code into the emulated RDRAM
 * where nothing will execute it, and clears the overlay's BSS -- but the call
 * also lands here, and this is what actually swaps the code in: every thunk
 * for an address in the group jumps through a slot, and the slots are filled
 * with this segment's own functions.
 *
 * Slots the loaded segment has no function for are left pointing at a trap
 * rather than at the previous overlay's code: a call to one would be a bug in
 * the port (a name resolved through the wrong overlay), and it should say so
 * instead of running something arbitrary. */
#include <stdio.h>
#include <stdlib.h>
#include "sbk_overlay.h"

extern int sbk_trace;
static void sbk_ov_trap(void) {
    fprintf(stderr, "sbk: call into an overlay slot no loaded overlay defines\n");
    abort();
}

void sbk_overlay_activate(void *rom_start) {
    uint32_t rom = (uint32_t)(uintptr_t)rom_start;
    const struct sbk_ov_segment *seg;
    int i;

    for (seg = sbk_ov_segments; seg->name != NULL; seg++) {
        if (seg->rom_start != rom) {
            continue;
        }
        /* Clear only this group's slots: the levels and the
         * race/cutscene/credits partition are independent overlays and both
         * have to keep working across a load of the other. */
        for (i = seg->group_lo; i < seg->group_hi; i++) {
            sbk_ov_slot[i] = sbk_ov_trap;
        }
        for (i = 0; i < seg->count; i++) {
            sbk_ov_slot[seg->entries[i].slot] = seg->entries[i].fn;
        }
        if (sbk_trace) {
            printf("sbk: overlay %s loaded (%d entries)\n", seg->name, seg->count);
        }
        return;
    }
}

void sbk_overlay_init(void) {
    int i;
    for (i = 0; i < sbk_ov_slot_count; i++) {
        sbk_ov_slot[i] = sbk_ov_trap;
    }
}
