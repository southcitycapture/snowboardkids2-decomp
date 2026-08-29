#include "common.h"
#include "system/task_scheduler.h"

typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ s16 unk4;
    /* 0x06 */ u8 padding6[2];
    /* 0x08 */ u8 *text;
} CutsceneEditorTextEntry;

typedef struct {
    /* 0x00 */ void *context;
    /* 0x04 */ CutsceneEditorTextEntry *entries;
    /* 0x08 */ s16 x;
    /* 0x0A */ s16 y;
    /* 0x0C */ u16 columnCount;
    /* 0x0E */ u16 rowCount;
    /* 0x10 */ u8 padding10[0x18];
    /* 0x28 */ u8 enabled;
    /* 0x29 */ u8 stopRequested;
} CutsceneEditorTextGrid;

typedef struct {
    /* 0x00 */ u8 initialized;
    /* 0x01 */ u8 padding1[3];
    /* 0x04 */ CutsceneEditorTextGrid *grid;
} CutsceneEditorTextGridTask;

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

void func_800BB82C_1E887C(CutsceneEditorTextGridTask *task) {
    CutsceneEditorTextGrid *grid;
    s32 i;

    grid = task->grid;
    for (i = 0; i < grid->rowCount; i++) {
        grid->entries[i].text = freeNodeMemory(grid->entries[i].text);
    }
    grid->entries = freeNodeMemory(grid->entries);
}
