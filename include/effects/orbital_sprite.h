#pragma once

#include "common.h"
#include "graphics/sprite_table.h"

typedef struct {
    s32 modelAddress;
    SpriteAssetState spriteState;
} OrbitalSpriteRingInitArg;

void initOrbitalSpriteRing(OrbitalSpriteRingInitArg *state);
