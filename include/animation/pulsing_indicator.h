#pragma once

#include "common.h"
#include "graphics/sprite_table.h"
#include "math/geometry.h"

typedef struct {
    u8 _pad[0x16];
    u16 unk16;
} PulsingSpriteOwnerData;

typedef struct {
    u8 _pad[0x10];
    PulsingSpriteOwnerData *ownerData;
    u8 _pad2[0x4];
    Transform3D transformMatrix;
    u8 _pad3[0x4];
    s8 isDestroyed;
    s8 actionMode;
    u8 _pad4;
    s8 displayEnabled;
    u8 _pad5[0x48];
    s8 unk88;
} PulsingIndicatorOwner;

typedef struct {
    PulsingIndicatorOwner *owner;
    SpriteAssetState spriteState;
    u8 padding[4];
    s32 scale;
    s32 scaleVelocity;
} PulsingSpriteState;

void initPulsingSpriteIndicator(PulsingSpriteState *state);
