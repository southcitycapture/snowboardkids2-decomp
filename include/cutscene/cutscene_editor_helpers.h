#ifndef CUTSCENE_EDITOR_HELPERS_H
#define CUTSCENE_EDITOR_HELPERS_H

#include "common.h"

typedef struct CutsceneEditorTextGrid CutsceneEditorTextGrid;

void renderCutsceneSlotMenuItem(CutsceneEditorTextGrid *grid, s16 row, s16 colorIndex);
void renderCutsceneEditorText(s32 uiResourceId, s32 x, s32 y, char *text, s32 colorIndex);
void setupCutsceneCommandLayout(s32 commandIndex, s32 x, s32 y, s32 width, s32 height, s32 flags);

#endif
