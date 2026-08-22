#pragma once

#include "common.h"
#include "ui/level_preview_3d.h"

typedef struct {
    SceneModel *model;
    Transform3D transformMatrix;
    s32 velocity;
    s16 rotationAngle;
    s16 delayTimer;
    s16 bounceCount;
} ModelScaleAnimationState;

void initModelScaleAnimation(ModelScaleAnimationState *state);
