#ifndef UI_TEXT_GRID_H
#define UI_TEXT_GRID_H

#include "common.h"

typedef struct TextGrid TextGrid;

void setTextGridRowPalette(TextGrid *grid, s16 row, u16 palette);
void writeTextGridText(TextGrid *grid, s16 column, s16 row, char *text);
void writeTextGridTextWithPalette(TextGrid *grid, s16 column, s16 row, char *text, u16 palette);
void fillTextGridRect(TextGrid *grid, s16 column, s16 row, u16 width, u16 height, u8 value);

#endif
