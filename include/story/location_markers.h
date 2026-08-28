#pragma once

#include "common.h"
#include "math/geometry.h"

typedef struct {
    u8 padding0[0x34];
    Vec3i position;
} StoryMapPlayerState;

typedef struct {
    u8 baseLocationIndex;
    u8 padding;
} StoryMapSpecialLocationTriggerState;

void initDiscoveryDisplaySystem(s8 *arg0);
void initTownExitTrigger(void *);
s32 checkStoryMapLocationSelection(StoryMapPlayerState *);
