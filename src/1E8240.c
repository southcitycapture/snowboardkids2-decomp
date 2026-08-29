#include "common.h"

typedef struct {
    u8 padding[0x28];
    u8 enabled;
    u8 stopRequested;
} CutsceneEditorTextGrid;

u8 func_800BB1F0_1E8240(CutsceneEditorTextGrid *grid) {
    return grid->enabled;
}

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB1F8_1E8248);

void func_800BB2E8_1E8338(CutsceneEditorTextGrid *grid) {
    grid->stopRequested = TRUE;
}

INCLUDE_ASM("asm/nonmatchings/1E8240", renderCutsceneSlotMenuItem);

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB320_1E8370);

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB388_1E83D8);

INCLUDE_ASM("asm/nonmatchings/1E8240", renderCutsceneEditorText);

INCLUDE_ASM("asm/nonmatchings/1E8240", setupCutsceneCommandLayout);

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB550_1E85A0);

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB56C_1E85BC);

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB584_1E85D4);

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB5C0_1E8610);

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB5D4_1E8624);

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB720_1E8770);

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB82C_1E887C);
