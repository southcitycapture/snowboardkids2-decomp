#include "ui/text_grid.h"
#include "common.h"
#include "graphics/graphics.h"
#include "system/task_scheduler.h"
#include "text/font_assets.h"

struct TextGrid {
    /* 0x00 */ ViewportNode *viewport;
    /* 0x04 */ TextData *rows;
    /* 0x08 */ s16 x;
    /* 0x0A */ s16 y;
    /* 0x0C */ u16 columnCount;
    /* 0x0E */ u16 rowCount;
    /* 0x10 */ TextData cursorText;
    /* 0x1C */ u8 cursorString[8];
    /* 0x24 */ s16 cursorColumn;
    /* 0x26 */ s16 cursorRow;
    /* 0x28 */ u8 visible;
    /* 0x29 */ u8 stopRequested;
};

typedef struct {
    /* 0x00 */ u8 unused;
    /* 0x01 */ u8 padding1[3];
    /* 0x04 */ TextGrid *grid;
} TextGridTask;

void initTextGridTask(TextGridTask *task);
void updateTextGridTask(TextGridTask *task);
void cleanupTextGridTask(TextGridTask *task);

u8 isTextGridVisible(TextGrid *grid) {
    return grid->visible;
}

void startTextGrid(TextGrid *grid, ViewportNode *viewport, s16 x, s16 y, u16 columnCount, u16 rowCount) {
    TextGridTask *task;

    task = (TextGridTask *)scheduleTask(&initTextGridTask, 0, 0, 0x65);
    if (task != NULL) {
        grid->cursorText.palette = 5;
        grid->cursorText.string = grid->cursorString;
        grid->cursorString[0] = '*';
        grid->cursorString[2] = ' ';
        grid->cursorString[3] = ' ';
        grid->cursorString[4] = ' ';
        grid->cursorString[5] = ' ';
        grid->cursorString[6] = ' ';
        grid->viewport = viewport;
        grid->x = x;
        grid->y = y;
        grid->columnCount = columnCount;
        grid->rowCount = rowCount;
        grid->cursorText.x = 0;
        grid->cursorText.y = 0;
        grid->cursorString[1] = '\0';
        grid->cursorString[7] = '\0';
        grid->cursorColumn = 0;
        grid->cursorRow = 0;
        grid->visible = TRUE;
        grid->stopRequested = FALSE;
        grid->rows = NULL;
        task->unused = FALSE;
        task->grid = grid;
    }
}

void requestTextGridStop(TextGrid *grid) {
    grid->stopRequested = TRUE;
}

void setTextGridRowPalette(TextGrid *grid, s16 row, u16 palette) {
    if (grid->rows != NULL) {
        grid->rows[row].palette = palette;
    }
}

void setTextGridCell(TextGrid *grid, s16 column, s16 row, s8 value) {
    TextData *rows;

    rows = grid->rows;
    if (rows != NULL) {
        if (column >= 0) {
            if ((column < grid->columnCount) & (row >= 0)) {
                if (row < grid->rowCount) {
                    rows[row].string[column] = value;
                }
            }
        }
    }
}

void writeTextGridText(TextGrid *grid, s16 column, s16 row, char *text) {
    TextData *rows;
    s32 columnIndex;
    s32 rowIndex;
    u8 *rowText;
    u8 *destination;

    rows = grid->rows;
    columnIndex = column;
    if (rows != NULL) {
        if (columnIndex < grid->columnCount) {
            rowIndex = row;
            if (rowIndex < grid->rowCount) {
                rowText = rows[rowIndex].string;
                if (*text != '\0') {
                    /* Keep the index on the left to preserve KMC's original addu operand order. */
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

void writeTextGridTextWithPalette(TextGrid *grid, s16 column, s16 row, char *text, u16 palette) {
    if (grid->rows != NULL) {
        setTextGridRowPalette(grid, row, palette);
        writeTextGridText(grid, column, row, text);
    }
}

INCLUDE_ASM("asm/nonmatchings/ui/text_grid", fillTextGridRect);

void setTextGridCursorPosition(TextGrid *grid, s16 column, s16 row) {
    if (grid->rows != NULL) {
        grid->cursorColumn = column;
        grid->cursorRow = row;
    }
}

void setTextGridCursorPalette(TextGrid *grid, s32 palette) {
    if (grid->rows != NULL) {
        grid->cursorText.palette = (s16)(palette & 0xFF);
    }
}

void setTextGridCursorText(TextGrid *grid, u8 *text) {
    s32 character;
    s32 i;

    if (grid->rows != NULL) {
        for (i = 0; i < 7; i++) {
            character = *text;
            if (character == 0) {
                return;
            }
            grid->cursorString[i] = character;
            text++;
        }
        grid->cursorString[7] = 0;
    }
}

void setTextGridVisible(TextGrid *grid, s8 visible) {
    if (grid->rows != NULL) {
        grid->visible = visible;
    }
}

void initTextGridTask(TextGridTask *task) {
    TextGrid *grid;
    s32 row;
    s32 column;
    s32 count;
    s32 pad[4];

    (void)pad;

    grid = task->grid;
    grid->rows = allocateNodeMemory(grid->rowCount * sizeof(TextData));
    column = grid->rowCount;
    row = 0;
    if (column > 0) {
        do {
            grid->rows[row].x = (u16)grid->viewport->viewportLeft + (grid->x * 8);
            grid->rows[row].y = (u16)grid->viewport->viewportTop + (grid->y * 8) + (row * 8);
            grid->rows[row].palette = 0;
            grid->rows[row].string = allocateNodeMemory(grid->columnCount + 1);
            column = 0;
            count = grid->columnCount;
            if (count > 0) {
                do {
                    grid->rows[row].string[column] = ' ';
                    column++;
                } while (column < (s32)grid->columnCount);
            }
            grid->rows[row].string[grid->columnCount] = '\0';
            row++;
        } while (row < (s32)grid->rowCount);
    }
    setCleanupCallback(cleanupTextGridTask);
    setCallback(updateTextGridTask);
}

INCLUDE_ASM("asm/nonmatchings/ui/text_grid", updateTextGridTask);

void cleanupTextGridTask(TextGridTask *task) {
    TextGrid *grid;
    s32 i;

    grid = task->grid;
    for (i = 0; i < grid->rowCount; i++) {
        grid->rows[i].string = freeNodeMemory(grid->rows[i].string);
    }
    grid->rows = freeNodeMemory(grid->rows);
}
