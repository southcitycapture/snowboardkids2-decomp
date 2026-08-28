#pragma once

#include "common.h"
#include "graphics/graphics.h"

typedef struct {
    /* 0x000 */ ViewportNode base;
    union {
        struct {
            /* 0x1D8 */ DirectionalLightData lightColor;
            /* 0x1E0 */ AmbientLightData ambientLight;
        } lighting;
        struct {
            /* 0x1D8 */ u8 pad1D8[0x0C];
            /* 0x1E4 */ Vec3i position;
            /* 0x1F0 */ Vec3i velocity;
            /* 0x1FC */ s16 posFramesX;
            /* 0x1FE */ s16 posFramesY;
            /* 0x200 */ s16 posFramesZ;
            /* 0x202 */ s16 pad202;
            /* 0x204 */ s32 scale;
            /* 0x208 */ s32 scaleVelocity;
            /* 0x20C */ s16 scaleFrames;
            /* 0x20E */ u16 rotationAngle;
            /* 0x210 */ s32 renderFlags;
            /* 0x214 */ u8 wipeR;
            /* 0x215 */ u8 wipeG;
            /* 0x216 */ u8 wipeB;
            /* 0x217 */ u8 pad217;
            /* 0x218 */ s8 renderMode;
        } anim;
    } state;
} __attribute__((packed, aligned(4))) SceneRenderNode;

void enableSceneRenderViewportEffect(SceneRenderNode *node);
void disableSceneRenderViewportEffect(SceneRenderNode *node);
void setNodeRenderMode(SceneRenderNode *node, s32 renderMode);
void setNodeWipeColor(SceneRenderNode *node, u8 r, u8 g, u8 b);
void setNodeScaleTarget(SceneRenderNode *node, s32 scale, s16 frames);
void setNodePositionTarget(SceneRenderNode *node, Vec3i *targetPosition, s16 frames);
void initSceneRenderNode(
    SceneRenderNode *node,
    u16 slotIndex,
    u8 priority,
    u16 sceneId,
    u8 renderMode,
    u16 taskArg1,
    u16 taskArg2
);
