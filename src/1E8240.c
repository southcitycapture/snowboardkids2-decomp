#include "common.h"
#include "system/task_scheduler.h"

typedef struct {
    /* 0x00 */ u8 padding[0xA4];
    /* 0xA4 */ u16 unkA4;
    /* 0xA6 */ u16 unkA6;
} CutsceneEditorContext;

typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ s16 unk4;
    /* 0x06 */ u8 padding6[2];
    /* 0x08 */ u8 *text;
} CutsceneEditorTextEntry;

typedef struct CutsceneEditorTextGrid {
    /* 0x00 */ CutsceneEditorContext *context;
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
void func_800BB720_1E8770(CutsceneEditorTextGridTask *task);
void func_800BB82C_1E887C(CutsceneEditorTextGridTask *task);
void func_800BB388_1E83D8(CutsceneEditorTextGrid *grid, s16 column, s16 row, char *text);

u8 func_800BB1F0_1E8240(CutsceneEditorTextGrid *grid) {
    return grid->enabled;
}

void func_800BB1F8_1E8248(
    CutsceneEditorTextGrid *grid,
    CutsceneEditorContext *context,
    s16 x,
    s16 y,
    u16 columnCount,
    u16 rowCount
) {
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

void renderCutsceneSlotMenuItem(CutsceneEditorTextGrid *grid, s16 row, u16 colorIndex) {
    if (grid->entries != NULL) {
        grid->entries[row].unk4 = colorIndex;
    }
}

void func_800BB320_1E8370(CutsceneEditorTextGrid *grid, s16 column, s16 row, s8 value) {
    CutsceneEditorTextEntry *entries;

    entries = grid->entries;
    if (entries != NULL) {
        if (column >= 0) {
            if ((column < grid->columnCount) & (row >= 0)) {
                if (row < grid->rowCount) {
                    entries[row].text[column] = value;
                }
            }
        }
    }
}

void func_800BB388_1E83D8(CutsceneEditorTextGrid *grid, s16 column, s16 row, char *text) {
    CutsceneEditorTextEntry *entries;
    s32 columnIndex;
    s32 rowIndex;
    u8 *rowText;
    u8 *destination;

    entries = grid->entries;
    columnIndex = column;
    if (entries != NULL) {
        if (columnIndex < grid->columnCount) {
            rowIndex = row;
            if (rowIndex < grid->rowCount) {
                rowText = entries[rowIndex].text;
                if (*text != '\0') {
                    destination = (u8 *)(columnIndex + (s32)rowText);
                    do {
                        if (*destination == '\0') {
                            return;
                        }
                        *destination++ = *text++;
                    } while (*text != '\0');
                }
            }
        }
    }
}

void renderCutsceneEditorText(s32 uiResourceId, s32 x, s32 y, char *text, u16 colorIndex) {
    CutsceneEditorTextGrid *grid;

    grid = (CutsceneEditorTextGrid *)uiResourceId;
    if (grid->entries != NULL) {
        renderCutsceneSlotMenuItem(grid, y, colorIndex);
        func_800BB388_1E83D8(grid, x, y, text);
    }
}

INCLUDE_ASM("asm/nonmatchings/1E8240", setupCutsceneCommandLayout);

void func_800BB550_1E85A0(CutsceneEditorTextGrid *grid, s16 scrollX, s16 scrollY) {
    if (grid->entries != NULL) {
        grid->scrollX = scrollX;
        grid->scrollY = scrollY;
    }
}

void func_800BB56C_1E85BC(CutsceneEditorTextGrid *grid, s32 arg1) {
    if (grid->entries != NULL) {
        grid->unk14 = (s16)(arg1 & 0xFF);
    }
}

INCLUDE_ASM("asm/nonmatchings/1E8240", func_800BB584_1E85D4);

void func_800BB5C0_1E8610(CutsceneEditorTextGrid *grid, s8 enabled) {
    if (grid->entries != NULL) {
        grid->enabled = enabled;
    }
}

void func_800BB5D4_1E8624(CutsceneEditorTextGridTask *task) {
    CutsceneEditorTextGrid *grid;
    s32 row;
    s32 column;
    s32 count;
    s32 pad[4];

    (void)pad;

    grid = task->grid;
    grid->entries = allocateNodeMemory(grid->rowCount * sizeof(CutsceneEditorTextEntry));
    column = grid->rowCount;
    row = 0;
    if (column > 0) {
        do {
            grid->entries[row].x = grid->context->unkA4 + (grid->x * 8);
            grid->entries[row].y = grid->context->unkA6 + (grid->y * 8) + (row * 8);
            grid->entries[row].unk4 = 0;
            grid->entries[row].text = allocateNodeMemory(grid->columnCount + 1);
            column = 0;
            count = grid->columnCount;
            if (count > 0) {
                do {
                    grid->entries[row].text[column] = ' ';
                    column++;
                } while (column < (s32)grid->columnCount);
            }
            grid->entries[row].text[grid->columnCount] = '\0';
            row++;
        } while (row < (s32)grid->rowCount);
    }
    setCleanupCallback(func_800BB82C_1E887C);
    setCallback(func_800BB720_1E8770);
}

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
