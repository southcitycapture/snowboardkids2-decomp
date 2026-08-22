#pragma once

#include "common.h"
#include "math/geometry.h"

typedef struct {
    u8 padding[0x3C];
    s8 isDestroyed;
    s8 actionMode;
    u8 padding2[0xB2];
    u8 transformMatrix[0x20];
} SteppedMatrixOwner;

typedef struct {
    SteppedMatrixOwner *owner;
    Transform3D matrix;
    s16 frameDelay;
    u8 padding[0x2];
    s16 stepIndex;
} SteppedMatrixState;

void initSteppedMatrixController(SteppedMatrixState *state);
