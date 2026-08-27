#pragma once

#include "common.h"
#include "data/data_table.h"
#include "gbi.h"
#include "math/geometry.h"
#include "system/task_scheduler.h"

typedef enum {
    SPRITE_ASSET_ENABLED = 1 << 0,
    SPRITE_ASSET_VISIBLE = 1 << 1,
} SpriteAssetStatusFlag;

typedef struct {
    void *romStart;
    void *romEnd;
    s32 decompressedSize;
} SpriteDmaEntry;

typedef struct {
    /* 0x00 */ union {
        u16 packedOffsets;
        struct {
            s8 x;
            s8 y;
        } components;
    } offset;
    /* 0x02 */ s16 command;
    /* 0x04 */ union {
        u16 spriteFrame;
        u16 textureIndex;
    } image;
    /* 0x06 */ u16 duration;
} SpriteAssetFrame;

typedef struct {
    /* 0x00 */ SpriteAssetFrame *frames;
    /* 0x04 */ union {
        u16 value;
        s16 signedValue;
    } frameCount;
    /* 0x06 */ u16 initialDelay;
} SpriteAnimationSet;

typedef struct {
    /* 0x00 */ DataTable_19E80 *assetData;
    /* 0x04 */ s16 assetIndex;
    /* 0x06 */ u8 statusFlags;
    /* 0x07 */ u8 initialDelay;
    /* 0x08 */ SpriteAnimationSet *animationSet;
    /* 0x0C */ SpriteAssetFrame *frames;
    /* 0x10 */ s16 animationIndex;
    /* 0x12 */ s16 currentSpriteFrame;
    /* 0x14 */ s16 frameIndex;
    /* 0x16 */ s16 frameTimer;
    /* 0x18 */ void *vertexData;
    /* 0x1C */ Vec3i position;
    /* 0x28 */ DataTable_19E80 *textureTable;
    /* 0x2C */ u16 textureIndex;
    /* 0x2E */ u8 alpha;
    /* 0x2F */ u8 flipHorizontal;
    /* 0x30 */ Mtx *translationMtx;
    /* 0x34 */ Mtx *scaleMtx;
    /* 0x38 */ Mtx *yRotationMtx;
    /* 0x3C */ Mtx *zRotationMtx;
    /* 0x40 */ s32 scaleX;
    /* 0x44 */ s32 scaleY;
    /* 0x48 */ u16 renderMode;
    /* 0x4A */ u16 renderFlags;
} SpriteAssetState;

s32 loadSpriteAsset(SpriteAssetState *state, s16 assetIndex);
void *loadSpriteAssetData(s16 assetIndex);
void releaseNodeMemoryRef(void **ptr);
void setSpriteAnimation(SpriteAssetState *state, s32 playbackRate, s32 animationIndex, s32 loopFrame);
s32 updateSpriteAnimation(SpriteAssetState *state, s32 playbackRate);
void renderOpaqueSprite(
    SpriteAssetState *state,
    s32 slot,
    s32 posX,
    s32 posY,
    s32 posZ,
    s32 scaleX,
    s32 scaleY,
    s16 renderMode,
    u8 flipH
);
void renderSprite(
    SpriteAssetState *state,
    s32 slot,
    s32 posX,
    s32 posY,
    s32 posZ,
    s32 scaleX,
    s32 scaleY,
    s16 renderMode,
    u8 flipH,
    u8 alpha
);
s32 getSpriteFrameWidth(SpriteAssetState *state);
void initOscillatingModelTask(void);
void initOscillatingSpriteTask(void *state);

void enqueueTranslucentSprite(u16 slot, Node *node);
void setupAndEnqueueSprite(
    SpriteAssetState *state,
    s32 slot,
    s32 posX,
    s32 posY,
    s32 posZ,
    s32 scaleX,
    s32 scaleY,
    s16 renderMode,
    u8 flipH,
    u8 alpha,
    s16 renderFlags
);
