#include "common.h"
#include "system/task_scheduler.h"

typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ s16 unk4;
    /* 0x06 */ u8 padding6[2];
    /* 0x08 */ u8 *text;
} CutsceneEditorTextEntry;

typedef struct CutsceneEditorTextGrid {
    /* 0x00 */ void *context;
    /* 0x04 */ CutsceneEditorTextEntry *entries;
    /* 0x08 */ s16 x;
    /* 0x0A */ s16 y;
    /* 0x0C */ u16 columnCount;
    /* 0x0E */ u16 rowCount;
    /* 0x10 */ s16 cursorX;
    /* 0x12 */ s16 cursorY;
    /* 0x14 */ s16 unk14;
    /* 0x16 */ u8 padding16[2];
    /* 0x18 */ char *text;
    /* 0x1C */ char textBuffer[8];
    /* 0x24 */ s16 scrollX;
    /* 0x26 */ s16 scrollY;
    /* 0x28 */ u8 enabled;
    /* 0x29 */ u8 stopRequested;
} CutsceneEditorTextGrid;

typedef struct {
    /* 0x00 */ u8 initialized;
    /* 0x01 */ u8 padding1[3];
    /* 0x04 */ CutsceneEditorTextGrid *grid;
} CutsceneEditorTextGridTask;

void func_800BB5D4_1E8624(CutsceneEditorTextGridTask *task);

u8 func_800BB1F0_1E8240(CutsceneEditorTextGrid *grid) {
    return grid->enabled;
}

void func_800BB1F8_1E8248(CutsceneEditorTextGrid *grid, void *context, s16 x, s16 y, u16 columnCount, u16 rowCount) {
    CutsceneEditorTextGridTask *task;

    task = (CutsceneEditorTextGridTask *)scheduleTask(&func_800BB5D4_1E8624, 0, 0, 0x65);
    if (task != NULL) {
        grid->unk14 = 5;
        grid->text = grid->textBuffer;
        grid->textBuffer[0] = '*';
        grid->textBuffer[2] = ' ';
        grid->textBuffer[3] = ' ';
        grid->textBuffer[4] = ' ';
        grid->textBuffer[5] = ' ';
        grid->textBuffer[6] = ' ';
        grid->context = context;
        grid->x = x;
        grid->y = y;
        grid->columnCount = columnCount;
        grid->rowCount = rowCount;
        grid->cursorX = 0;
        grid->cursorY = 0;
        grid->textBuffer[1] = '\0';
        grid->textBuffer[7] = '\0';
        grid->scrollX = 0;
        grid->scrollY = 0;
        grid->enabled = TRUE;
        grid->stopRequested = FALSE;
        grid->entries = NULL;
        task->initialized = FALSE;
        task->grid = grid;
    }
}

void func_800BB2E8_1E8338(CutsceneEditorTextGrid *grid) {
    grid->stopRequested = TRUE;
}

void renderCutsceneSlotMenuItem(CutsceneEditorTextGrid *grid, s16 row, s16 colorIndex) {
    if (grid->entries != NULL) {
        grid->entries[row].unk4 = colorIndex;
    }
}

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
