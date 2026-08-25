#pragma once

#include "common.h"
#include "graphics/displaylist.h"
#include "graphics/graphics.h"
#include "graphics/sprite_rdp.h"
#include "graphics/tiled_sprite_grid.h"
#include "math/geometry.h"
#include "ui/level_preview_3d.h"

typedef struct {
    union {
        DisplayListObject displayObject;
        struct {
            Transform3D transform;
            void *characterAsset;
            void *modelAsset;
            void *animationAsset;
            void *paletteAsset;
        } character;
        struct {
            ViewportNode *camera;
            Transform3D transform;
            void *characterAsset;
            void *modelAsset;
            void *animationAsset;
            void *paletteAsset;
        } wipe;
    } render;
    union {
        struct {
            Transform3D baseTransform;
        } character;
        struct {
            s32 unused3C;
            Transform3D baseTransform;
            s16 left;
            s16 right;
            u8 delayTimer;
        } wipe;
    } mode;
} BoardShopCharacterPreviewState;

typedef struct {
    SceneModel *model;
    Transform3D transform;
    s16 animationFrame;
    u16 animationEndFrame;
    u8 animationState;
} BoardShopShopkeeperState;

typedef struct {
    s16 textWidth;
    s16 y;
    void *textData;
    void *textAsset;
    s16 primaryColor;
    s16 secondaryColor;
    s8 textStyle;
} BoardShopTitleTextState;

typedef union {
    FlippedScaledSpriteArg sprite;
    struct {
        u8 packedSpriteFields[0x15];
        u8 taskData[0x6F];
        u8 frameCounter;
    } task;
} BoardShopSnowflakeSpriteState;

typedef struct {
    FlippedScaledSpriteArg sprites[4];
    u8 taskData60[0x18];
    s16 priceTextX;
    s16 priceTextY;
    s16 priceTextStyle;
    void *priceTextPtr;
    union {
        s8 animationCounters[4];
        u8 portraitFrameCounters[4];
        struct {
            u8 slidingIconCount;
        } slideOut;
    } animation;
    char priceTextBuffer[4];
} BoardShopBoardIconsState;

typedef struct {
    TextRenderArg comparisonIcons[2];
    u8 animationCounter;
} BoardShopComparisonIconsState;

typedef struct {
    u16 x;
    u16 y;
    void *sprite;
    u16 speed;
} SnowParticle;

typedef struct {
    SnowParticle *particles;
    u8 delayTimer;
} SnowParticleState;

typedef struct {
    /* 0x000 */ ViewportNode mainViewport;
    /* 0x1D8 */ ViewportNode secondaryViewport;
    /* 0x3B0 */ ViewportNode tertiaryViewport;
    /* 0x588 */ ViewportNode quaternaryViewport;
    /* 0x760 */ void *assetSlot0;
    /* 0x764 */ void *assetSlot1;
    /* 0x768 */ void *assetSlot2;
    /* 0x76C */ void *assetSlot3;
    /* 0x770 */ void *assetSlot4;
    /* 0x774 */ void *textRenderAsset;
    /* 0x778 */ void *assetSlot5;
    /* 0x77C */ u16 delayTimer;
    /* 0x77E */ u16 shopkeeperAnimIndex;
    /* 0x780 */ s16 previewWipeLeft;
    /* 0x782 */ s16 previewWipeRight;
    /* 0x784 */ u8 boardDisplayIndices[4];
    /* 0x788 */ u8 boardIndexMap[13];
    /* 0x795 */ u8 unlockedBoardsInCategory[3];
    /* 0x798 */ u8 totalBoardCount;
    /* 0x799 */ u8 selectedSlot;
    /* 0x79A */ u8 exitMode;
    /* 0x79B */ u8 shopState;
    /* 0x79C */ u8 scrollDirection;
    /* 0x79D */ s8 transitionDirection;
    /* 0x79E */ u8 newTransitionIndex;
    /* 0x79F */ u8 oldTransitionIndex;
    /* 0x7A0 */ u8 scrollOutBoardIndex;
    /* 0x7A1 */ s8 selectedCategoryIndex;
    /* 0x7A2 */ union {
        u8 index;
        s8 signedIndex;
    } selectedBoard;
    /* 0x7A3 */ u8 forceShopkeeperAnimUpdate;
    /* 0x7A4 */ u8 viewMode;
} BoardShopState;

void initBoardShopBoardIcons(BoardShopBoardIconsState *state);
void initBoardShopCharacterPreview(BoardShopCharacterPreviewState *state);
void initBoardShopCharacterTransition(BoardShopCharacterPreviewState *state);
void initBoardShopColumnSelectorArrow(TextRenderArg *state);
void initBoardShopComparisonIcons(BoardShopComparisonIconsState *state);
void initBoardShopExitOverlay(SpriteRenderArg *state);
void initBoardShopPreviewWipe(BoardShopCharacterPreviewState *state);
void initBoardShopRowSelectorArrow(TextRenderArg *state);
void initBoardShopShopkeeper(BoardShopShopkeeperState *state);
void initBoardShopSnowParticles(SnowParticleState *state);
void initBoardShopSnowflakeSlideIn(BoardShopSnowflakeSpriteState *state);
void initBoardShopTitleCorners(TextRenderArg *state);
void initBoardShopTitleText(BoardShopTitleTextState *state);
void loadBoardShopBackground(TileMapRenderTaskState *state);
