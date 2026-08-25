#pragma once

#include "common.h"
#include "ui/level_preview_3d.h"

typedef struct {
    SceneModel *owner;
    SpriteAssetState spriteState;
    union {
        struct {
            Vec3i position;
            Vec3i velocity;
            s16 lifetime;
            s16 renderFlags;
        } falling;
        struct {
            s32 scale;
            Vec3i positionOffset;
            Vec3i targetOffset;
            Vec3i velocity;
            Vec3i targetVelocity;
            Vec3s retargetTimers;
            s16 renderFlags;
            s8 flipHorizontal;
        } drifting;
    } mode;
} ParticleState;

void initDriftingParticle(ParticleState *state);
void initTrailingParticle(ParticleState *state);
