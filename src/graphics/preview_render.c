#include "graphics/preview_render.h"
#include "assets.h"
#include "common.h"
#include "data/asset_metadata.h"
#include "font_encoding.h"
#include "gamestate.h"
#include "graphics/displaylist.h"
#include "graphics/graphics.h"
#include "graphics/sprite_rdp.h"
#include "graphics/tiled_sprite_grid.h"
#include "math/geometry.h"
#include "math/rand.h"
#include "race/race_session.h"
#include "story/map_events.h"
#include "story/shop_ui.h"
#include "system/task_scheduler.h"
#include "text/font_assets.h"
#include "text/font_render.h"
#include "text/text_layout.h"
#include "ui/level_preview_3d.h"
#include "ui/save_data.h"

typedef struct {
    u16 start;
    u16 end;
} D_8008F16C_8FD6C_type;

u16 D_8008F150_8FD50[] = { 0x0064, 0x0064, 0x0064, 0x00FA, 0x00FA, 0x012C, 0x012C,
                           0x0190, 0x0190, 0x01C2, 0x01C2, 0x01F4, 0x01F4, 0x0320 };

D_8008F16C_8FD6C_type D_8008F16C_8FD6C[] = {
    { 0x03E8, 0x04B0 },
    { 0x0011, 0x0011 },
    { 0x0012, 0x0012 },
    { 0x0013, 0x0015 }
};

s16 boardIconTargetYPositions[] = { 0xFFC7, 0xFFEF, 0x0017, 0x003F };

u16 D_8008F184_8FD84[] = { 0x0040, 0x00E0, 0x0480, 0x0000 };

u8 boardShopChooseBoardToPaintPromptText[] = { _("Which board do you want to paint@") };

u8 boardShopChooseDesignPromptText[48] = { _NT("Which design do you want") };

struct {
    s32 unk0[2];
    u16 unk8;
    u16 unkA[1];
} D_8008F200_8FE00 = {
    { 0x8054FFFF, (s32)boardShopChooseBoardToPaintPromptText },
    0x8008,
    { 0xF1D0 }
};

u16 D_8008F20C_8FE0C = 0xFF88;
u16 D_8008F20E_8FE0E = 0xFF98;

extern const char D_8009E47C_9F07C[];
extern const char D_8009E480_9F080;

void animateBoardShopSnowParticles(SnowParticleState *);
void blinkBoardShopBoardIconConfirmation(BoardShopBoardIconsState *arg0);
void freeBoardShopCharacterPreviewAssets(BoardShopCharacterPreviewState *arg0);
void waitBoardShopCharacterPreview(void);
void animateBoardShopCharacterSlideIn(BoardShopCharacterPreviewState *arg0);
void updateBoardShopCharacterPreview(BoardShopCharacterPreviewState *arg0);
void loadBoardShopCharacterAssets(BoardShopCharacterPreviewState *arg0);
void animateBoardShopCharacterSwitch(BoardShopCharacterPreviewState *arg0);
void freeBoardShopPurchaseAssets(BoardShopCharacterPreviewState *arg0);
void animateBoardShopCharacterSlideOut(BoardShopCharacterPreviewState *arg0);
void loadBoardShopPurchaseAssets(BoardShopCharacterPreviewState *arg0);
void cleanupBoardShopShopkeeper(BoardShopShopkeeperState *arg0);
void waitBoardShopShopkeeper(BoardShopShopkeeperState *arg0);
void updateBoardShopShopkeeper(BoardShopShopkeeperState *arg0);
void initBoardShopBackgroundRenderState(TileMapRenderTaskState *arg0);
void cleanupBoardShopBackground(TileMapRenderTaskState *arg0);
void cleanupBoardShopColumnSelectorArrow(TextRenderArg *);
void updateBoardShopExitOverlay(void *arg0);
void cleanupBoardShopExitOverlay(SpriteRenderArg *arg0);
void updateBoardShopTitleCorners(TextRenderArg *arg0);
void cleanupBoardShopTitleCorners(TextRenderArg *arg0);
void updateBoardShopTitleText(BoardShopTitleTextState *arg0);
void cleanupBoardShopTitleText(BoardShopTitleTextState *arg0);
void animateBoardShopSnowflakeSlideIn(BoardShopSnowflakeSpriteState *arg0);
void queueBoardShopSnowflakeRender(void *);
void cleanupBoardShopSnowflakeSprite(SpriteRenderArg *);
void cleanupBoardShopBoardIcons(BoardShopBoardIconsState *arg0);
void freeBoardShopCharacterTransitionAssets(BoardShopCharacterPreviewState *arg0);
void animateBoardShopCharacterTransition(BoardShopCharacterPreviewState *arg0);
void enqueueBoardShopBackgroundRender(TileMapRenderTaskState *state);
void cleanupBoardShopSnowParticles(SnowParticleState *arg0);
void waitBoardShopSnowParticles(SnowParticleState *arg0);
void animateBoardShopCharacterPortraitsSlideIn(BoardShopBoardIconsState *arg0);
void animateBoardShopBoardIconsSlideIn(BoardShopBoardIconsState *arg0);
void updateBoardShopGoldDisplay(BoardShopGoldDisplayState *arg0);
void initBoardShopCharacterPortraitsSlideIn(BoardShopBoardIconsState *arg0);
void animateBoardShopBoardIconsSlideOut(BoardShopBoardIconsState *arg0);
void cleanupBoardShopGoldDisplay(BoardShopGoldDisplayState *arg0);
void cleanupBoardShopPreviewWipe(BoardShopCharacterPreviewState *arg0);
void animateBoardShopPreviewWipe(BoardShopCharacterPreviewState *arg0);
void waitBoardShopPreviewWipe(BoardShopCharacterPreviewState *arg0);
void updateBoardShopBoardIconSelection(BoardShopBoardIconsState *arg0);

void initBoardShopPreviewWipe(BoardShopCharacterPreviewState *arg0) {
    s32 perspectiveParams[8];
    Transform3D rotationYX;
    Transform3D rotationZ;

    u8 charIndex;
    s32 unused;
    u8 paletteId;
    void *transformMatrix;
    BoardShopState *state;
    ViewportNode *cameraNode;
    Transform3D *rotationZPtr;

    rotationZPtr = &rotationZ;

    state = getCurrentAllocation();
    cameraNode = allocateNodeMemory(0x1D8);
    arg0->render.wipe.camera = cameraNode;
    initMenuCameraNode(cameraNode, 1, 0xB, 0);
    arg0->mode.wipe.left = -0x34;
    arg0->mode.wipe.right = 0x34;
    state->previewWipeLeft = arg0->mode.wipe.left;
    state->previewWipeRight = arg0->mode.wipe.right;

    setModelCameraTransform(arg0->render.wipe.camera, 0, 0, -0x98, arg0->mode.wipe.left, 0x97, arg0->mode.wipe.right);
    createViewportTransform(perspectiveParams, 0, 0, 0x580000, 0, 0, 0);
    setViewportTransformById(arg0->render.wipe.camera->viewportId, perspectiveParams);

    transformMatrix = &arg0->mode.wipe.baseTransform;
    memcpy(transformMatrix, &identityMatrix, sizeof(Transform3D));
    memcpy(rotationZPtr, transformMatrix, sizeof(Transform3D));
    memcpy(&rotationYX, rotationZPtr, sizeof(Transform3D));

    createRotationMatrixYX(&rotationYX, 0x1000, 0x800);
    createZRotationMatrix(rotationZPtr, 0x1F00);

    composeTransform3D(&rotationYX, rotationZPtr, (Transform3D *)transformMatrix);

    arg0->mode.wipe.baseTransform.translation.z = 0xFFF80000;
    charIndex = state->selectedBoard.index + (state->selectedCategoryIndex * 3);
    paletteId = EepromSaveData->characterPaletteIds[charIndex & 0xFF];

    memcpy(&arg0->render.wipe.transform, transformMatrix, sizeof(Transform3D));

    charIndex = charIndex & 0xFF;
    arg0->render.wipe.characterAsset = loadAssetByIndex_95728(charIndex);
    arg0->render.wipe.modelAsset = loadAssetByIndex_95500(charIndex);
    arg0->render.wipe.animationAsset = loadAssetByIndex_95590(charIndex);
    arg0->render.wipe.paletteAsset = loadAssetByIndex_95668(paletteId - 1);

    setCleanupCallback(&cleanupBoardShopPreviewWipe);
    arg0->mode.wipe.delayTimer = 0xC;
    setCallback(&waitBoardShopPreviewWipe);
}

void waitBoardShopPreviewWipe(BoardShopCharacterPreviewState *arg0) {
    u8 delayTimer;
    arg0->mode.wipe.delayTimer--;
    delayTimer = arg0->mode.wipe.delayTimer;
    if (delayTimer == 0) {
        setCallback(&animateBoardShopPreviewWipe);
    }
    enableViewportDisplayList(arg0->render.wipe.camera);
    enqueueDisplayListObject(1, (DisplayListObject *)&arg0->render.wipe.transform);
}

void animateBoardShopPreviewWipe(BoardShopCharacterPreviewState *arg0) {
    BoardShopState *state = getCurrentAllocation();

    arg0->mode.wipe.left++;
    arg0->mode.wipe.right--;
    state->previewWipeLeft = arg0->mode.wipe.left;
    state->previewWipeRight = arg0->mode.wipe.right;

    setModelCameraTransform(arg0->render.wipe.camera, 0, 0, -0x98, arg0->mode.wipe.left, 0x97, arg0->mode.wipe.right);

    if (arg0->mode.wipe.left == 0) {
        state->delayTimer = 1;
        unlinkNode(arg0->render.wipe.camera);
        terminateCurrentTask();
    } else {
        enableViewportDisplayList(arg0->render.wipe.camera);
        enqueueDisplayListObject(1, (DisplayListObject *)&arg0->render.wipe.transform);
    }
}

void cleanupBoardShopPreviewWipe(BoardShopCharacterPreviewState *arg0) {
    arg0->render.wipe.camera = freeNodeMemory(arg0->render.wipe.camera);
    arg0->render.wipe.modelAsset = freeNodeMemory(arg0->render.wipe.modelAsset);
    arg0->render.wipe.animationAsset = freeNodeMemory(arg0->render.wipe.animationAsset);
    arg0->render.wipe.paletteAsset = freeNodeMemory(arg0->render.wipe.paletteAsset);
}

void initBoardShopSnowParticles(SnowParticleState *arg0) {
    BoardShopState *state;
    void *snowflakeSprite;
    s32 i;
    int new_var;
    s32 j;
    s32 randOffset;

    state = getCurrentAllocation();
    snowflakeSprite = loadCompressedData(&snowflakeSprite_ROM_START, &snowflakeSprite_ROM_END, 0x9488);
    arg0->particles = allocateNodeMemory(0xF0);
    setCleanupCallback(cleanupBoardShopSnowParticles);

    i = j = 0;
    while (i < 0x14) {
        if (i < 10) {
            arg0->particles[j].x = -0x24 + (i * 6);
            randOffset = (randB() & 7) - 10;
            arg0->particles[j].y = state->previewWipeLeft + randOffset;
        } else {
            arg0->particles[j].x = -0x60 + (i * 6);
            arg0->particles[j].y = state->previewWipeRight + (new_var = (randB() & 7) - 10);
        }

        arg0->particles[j].sprite = snowflakeSprite;
        arg0->particles[j].speed = (randB() & 7) | 0x10;

        i++;
        j++;
    }

    arg0->delayTimer = 0xC;
    setCallback(waitBoardShopSnowParticles);
}

void waitBoardShopSnowParticles(SnowParticleState *arg0) {
    arg0->delayTimer--;
    if (arg0->delayTimer == 0) {
        setCallback(animateBoardShopSnowParticles);
    }
}

void animateBoardShopSnowParticles(SnowParticleState *arg0) {
    BoardShopState *state;
    s32 i;
    u16 baseY;
    s16 randVal;
    SnowParticle *particle;
    s16 newSpeed;
    s32 new_var;

    state = getCurrentAllocation();

    arg0->delayTimer = (arg0->delayTimer + 1) & 3;

    for (i = 0; i < 0x14; i++) {
        if (i < 0xA) {
            randVal = randB() % 8 - 10;
            arg0->particles[i].y = state->previewWipeLeft + randVal;
        } else {
            arg0->particles[i].y = state->previewWipeRight + ((new_var = randB() % 8) - 10);
        }

        if (arg0->delayTimer == 0) {
            arg0->particles[i].speed++;

            if (arg0->particles[i].speed >= 0x18) {
                arg0->particles[i].speed = 0x10;
            }
        }

        pushViewportCallbackBySlot(8, VIEWPORT_CALLBACK_LAYER_INITIAL, renderSpriteFrame, &arg0->particles[i]);
    };

    if (state->previewWipeLeft == 0) {
        terminateCurrentTask();
    }
}

void cleanupBoardShopSnowParticles(SnowParticleState *arg0) {
    arg0->particles[0].sprite = freeNodeMemory(arg0->particles[0].sprite);
    arg0->particles = freeNodeMemory(arg0->particles);
}

void initBoardShopCharacterPreview(BoardShopCharacterPreviewState *arg0) {
    Transform3D rotationYX;
    Transform3D *pRotationZ;
    Transform3D *pRotationYX;
    Transform3D rotationZ;
    u8 paletteIndex;
    void *transformMatrix;

    pRotationZ = &rotationZ;
    pRotationYX = &rotationYX;

    getCurrentAllocation();
    transformMatrix = &arg0->mode.character.baseTransform;
    memcpy(transformMatrix, &identityMatrix, sizeof(Transform3D));
    memcpy(pRotationZ, transformMatrix, sizeof(Transform3D));
    memcpy(pRotationYX, pRotationZ, sizeof(Transform3D));
    createRotationMatrixYX(pRotationYX, 0x1000, 0x800);
    createZRotationMatrix(pRotationZ, 0x1F00);
    composeTransform3D(pRotationYX, pRotationZ, (Transform3D *)transformMatrix);
    arg0->mode.character.baseTransform.translation.x = 0x600000;
    arg0->mode.character.baseTransform.translation.z = 0xFFF80000;
    paletteIndex = EepromSaveData->characterPaletteIds[0];
    memcpy(arg0, transformMatrix, sizeof(Transform3D));
    arg0->render.character.characterAsset = loadAssetByIndex_95728(0);
    arg0->render.character.modelAsset = loadAssetByIndex_95500(0);
    arg0->render.character.animationAsset = loadAssetByIndex_95590(0);
    arg0->render.character.paletteAsset = loadAssetByIndex_95668(paletteIndex - 1);
    setCleanupCallback(&freeBoardShopCharacterPreviewAssets);
    setCallback(&waitBoardShopCharacterPreview);
}

void waitBoardShopCharacterPreview(void) {
    BoardShopState *allocation;

    allocation = getCurrentAllocation();
    if (allocation->shopState == 1) {
        setCallbackWithContinue(animateBoardShopCharacterSlideIn);
    }
}

void animateBoardShopCharacterSlideIn(BoardShopCharacterPreviewState *arg0) {
    BoardShopState *allocation;

    allocation = getCurrentAllocation();

    arg0->mode.character.baseTransform.translation.x += 0xFFF00000;

    memcpy(&arg0->render.character.transform, &arg0->mode.character.baseTransform, sizeof(Transform3D));

    if (arg0->mode.character.baseTransform.translation.x == 0) {
        allocation->shopState = 0xC;
        setCallbackWithContinue(&updateBoardShopCharacterPreview);
    }

    enqueueDisplayListObject(0, &arg0->render.displayObject);
}

void updateBoardShopCharacterPreview(BoardShopCharacterPreviewState *arg0) {
    BoardShopState *allocation;
    u8 state;

    allocation = getCurrentAllocation();
    state = allocation->shopState;

    if (state == 3 || state == 0x1A) {
        if (state == 3) {
            arg0->render.character.modelAsset = freeNodeMemory(arg0->render.character.modelAsset);
            arg0->render.character.animationAsset = freeNodeMemory(arg0->render.character.animationAsset);
            arg0->render.character.paletteAsset = freeNodeMemory(arg0->render.character.paletteAsset);
            setCallback(&loadBoardShopCharacterAssets);
        } else {
            setCallback(&freeBoardShopPurchaseAssets);
            enqueueDisplayListObject(0, &arg0->render.displayObject);
        }
    } else if (state == 0x32) {
        setCallback(&animateBoardShopCharacterSlideOut);
        enqueueDisplayListObject(0, &arg0->render.displayObject);
    } else {
        enqueueDisplayListObject(0, &arg0->render.displayObject);
    }
}

void loadBoardShopCharacterAssets(BoardShopCharacterPreviewState *arg0) {
    u8 paletteIndex;
    u8 temp_v1;
    BoardShopState *allocation;

    allocation = getCurrentAllocation();

    if (allocation->scrollDirection == 0) {
        arg0->mode.character.baseTransform.translation.x = 0xFFA00000;
    } else {
        arg0->mode.character.baseTransform.translation.x = 0x600000;
    }

    paletteIndex = EepromSaveData->characterPaletteIds[allocation->newTransitionIndex];

    memcpy(&arg0->render.character.transform, &arg0->mode.character.baseTransform, sizeof(Transform3D));

    arg0->render.character.characterAsset = loadAssetByIndex_95728(allocation->newTransitionIndex);
    arg0->render.character.modelAsset = loadAssetByIndex_95500(allocation->newTransitionIndex);
    arg0->render.character.animationAsset = loadAssetByIndex_95590(allocation->newTransitionIndex);
    arg0->render.character.paletteAsset = loadAssetByIndex_95668(paletteIndex - 1);

    setCallback(&animateBoardShopCharacterSwitch);
}

void animateBoardShopCharacterSwitch(BoardShopCharacterPreviewState *arg0) {
    BoardShopState *allocation;
    s32 slideSpeed;

    allocation = getCurrentAllocation();

    if (allocation->scrollDirection == 1) {
        slideSpeed = 0xFFF00000;
    } else {
        slideSpeed = 0x100000;
    }

    arg0->mode.character.baseTransform.translation.x += slideSpeed;

    memcpy(&arg0->render.character.transform, &arg0->mode.character.baseTransform, sizeof(Transform3D));

    if (arg0->mode.character.baseTransform.translation.x == 0) {
        allocation->transitionDirection--;
        setCallback(&updateBoardShopCharacterPreview);
    }

    enqueueDisplayListObject(0, &arg0->render.displayObject);
}

void freeBoardShopPurchaseAssets(BoardShopCharacterPreviewState *arg0) {
    arg0->render.character.modelAsset = freeNodeMemory(arg0->render.character.modelAsset);
    arg0->render.character.animationAsset = freeNodeMemory(arg0->render.character.animationAsset);
    arg0->render.character.paletteAsset = freeNodeMemory(arg0->render.character.paletteAsset);
    setCallback(&loadBoardShopPurchaseAssets);
}

void loadBoardShopPurchaseAssets(BoardShopCharacterPreviewState *arg0) {
    BoardShopState *allocation;
    s16 assetIndex;
    int new_var;
    u8 characterIndex;

    allocation = getCurrentAllocation();
    new_var = allocation->boardDisplayIndices[allocation->selectedSlot];
    assetIndex = allocation->selectedCategoryIndex;
    characterIndex = allocation->selectedBoard.index;
    assetIndex = (characterIndex + (assetIndex * 3)) & 0xFF;
    characterIndex = allocation->boardIndexMap[new_var];

    memcpy(&arg0->render.character.transform, &arg0->mode.character.baseTransform, sizeof(Transform3D));

    arg0->render.character.characterAsset = loadAssetByIndex_95728(assetIndex);
    arg0->render.character.modelAsset = loadAssetByIndex_95500(assetIndex);
    arg0->render.character.animationAsset = loadAssetByIndex_95590(assetIndex);
    arg0->render.character.paletteAsset = loadAssetByIndex_95668(characterIndex);

    setCallback(&updateBoardShopCharacterPreview);
}

void animateBoardShopCharacterSlideOut(BoardShopCharacterPreviewState *arg0) {
    BoardShopState *allocation;

    allocation = getCurrentAllocation();
    arg0->mode.character.baseTransform.translation.x += 0x100000;
    memcpy(&arg0->render.character.transform, &arg0->mode.character.baseTransform, sizeof(Transform3D));

    if (arg0->mode.character.baseTransform.translation.x == 0x600000) {
        allocation->exitMode = 1;
        terminateCurrentTask();
    } else {
        enqueueDisplayListObject(0, &arg0->render.displayObject);
    }
}

void freeBoardShopCharacterPreviewAssets(BoardShopCharacterPreviewState *arg0) {
    arg0->render.character.modelAsset = freeNodeMemory(arg0->render.character.modelAsset);
    arg0->render.character.animationAsset = freeNodeMemory(arg0->render.character.animationAsset);
    arg0->render.character.paletteAsset = freeNodeMemory(arg0->render.character.paletteAsset);
}

void initBoardShopCharacterTransition(BoardShopCharacterPreviewState *arg0) {
    BoardShopState *state;
    Transform3D rotationYX;
    Transform3D rotationZ;
    u8 characterIndex;
    u8 paletteIndex;
    Transform3D *pRotationYX;
    Transform3D *pRotationZ;

    state = getCurrentAllocation();

    memcpy(&arg0->mode.character.baseTransform, &identityMatrix, sizeof(Transform3D));

    pRotationZ = &rotationZ;
    memcpy(pRotationZ, &arg0->mode.character.baseTransform, sizeof(Transform3D));

    pRotationYX = &rotationYX;
    memcpy(pRotationYX, pRotationZ, sizeof(Transform3D));

    createRotationMatrixYX(pRotationYX, 0x1000, 0x800);
    createZRotationMatrix(pRotationZ, 0x1F00);
    composeTransform3D(pRotationYX, pRotationZ, &arg0->mode.character.baseTransform);

    arg0->mode.character.baseTransform.translation.z = 0xFFF80000;
    memcpy(&arg0->render.character.transform, &arg0->mode.character.baseTransform, 0x20U);

    characterIndex = state->oldTransitionIndex;
    paletteIndex = EepromSaveData->characterPaletteIds[characterIndex];

    arg0->render.character.characterAsset = loadAssetByIndex_95728(characterIndex);
    arg0->render.character.modelAsset = loadAssetByIndex_95500(characterIndex);
    arg0->render.character.animationAsset = loadAssetByIndex_95590(characterIndex);
    arg0->render.character.paletteAsset = loadAssetByIndex_95668(paletteIndex - 1);

    setCleanupCallback(&freeBoardShopCharacterTransitionAssets);
    setCallbackWithContinue(&animateBoardShopCharacterTransition);
}

void animateBoardShopCharacterTransition(BoardShopCharacterPreviewState *arg0) {
    s32 pad[8];
    BoardShopState *state;
    s32 slideSpeed;
    s32 targetPosition;
    s32 newPosition;

    state = getCurrentAllocation();

    slideSpeed = 0x100000;
    targetPosition = 0x600000;

    if (state->scrollDirection == 1) {
        slideSpeed = 0xFFF00000;
        targetPosition = 0xFFA00000;
    }

    newPosition = arg0->mode.character.baseTransform.translation.x + slideSpeed;
    arg0->mode.character.baseTransform.translation.x = newPosition;
    memcpy(&arg0->render.character.transform, &arg0->mode.character.baseTransform, sizeof(Transform3D));

    enqueueDisplayListObject(0, &arg0->render.displayObject);

    if (arg0->mode.character.baseTransform.translation.x == targetPosition) {
        terminateCurrentTask();
    }
}

void freeBoardShopCharacterTransitionAssets(BoardShopCharacterPreviewState *arg0) {
    arg0->render.character.modelAsset = freeNodeMemory(arg0->render.character.modelAsset);
    arg0->render.character.animationAsset = freeNodeMemory(arg0->render.character.animationAsset);
    arg0->render.character.paletteAsset = freeNodeMemory(arg0->render.character.paletteAsset);
}

void initBoardShopShopkeeper(BoardShopShopkeeperState *arg0) {
    arg0->model = createSceneModelEx(0x3A, &((GameState *)getCurrentAllocation())->audioPlayer2, 0, -1, 0, 0x12);

    memcpy(&arg0->transform, &identityMatrix, sizeof(Transform3D));

    arg0->transform.translation.x = 0xFFE70000;
    arg0->transform.translation.y = 0xFFE00000;
    arg0->transform.translation.z = 0;

    createYRotationMatrix(&arg0->transform, 0x200);

    arg0->animationFrame = 0x10;
    arg0->animationEndFrame = 0x10;
    arg0->animationState = 0;

    setCleanupCallback(&cleanupBoardShopShopkeeper);
    setCallback(&waitBoardShopShopkeeper);
}

void waitBoardShopShopkeeper(BoardShopShopkeeperState *arg0) {
    BoardShopState *state;

    state = getCurrentAllocation();
    applyTransformToModel(arg0->model, &arg0->transform);
    setItemDisplayEnabled(arg0->model, 1);
    setModelAnimation(arg0->model, arg0->animationFrame);
    updateModelGeometry(arg0->model);
    if (state->shopState != 0) {
        setCallback(&updateBoardShopShopkeeper);
    }
}

void updateBoardShopShopkeeper(BoardShopShopkeeperState *arg0) {
    BoardShopState *allocation;
    s32 result;
    s32 animState;
    u16 frame;
    volatile u8 pad[8];

    allocation = getCurrentAllocation();
    animState = arg0->animationState;

    if (animState != 2) {
        result = clearModelRotation(arg0->model);
        animState = arg0->animationState;

        if (animState == 1) {
            if (result != NULL) {
                frame = arg0->animationFrame + 1;
                arg0->animationFrame = frame;

                if (arg0->animationEndFrame < frame) {
                    arg0->animationState = 0;
                    arg0->animationFrame = 0x10;
                }

                setModelAnimation(arg0->model, arg0->animationFrame);
                animState = arg0->animationState;
            }
        }
    }

    if (animState != 1 || allocation->forceShopkeeperAnimUpdate != 0) {
        if (allocation->shopkeeperAnimIndex != 0) {
            arg0->animationFrame = D_8008F16C_8FD6C[allocation->shopkeeperAnimIndex].start;
            arg0->animationEndFrame = D_8008F16C_8FD6C[allocation->shopkeeperAnimIndex].end;
            setModelAnimation(arg0->model, arg0->animationFrame);
            allocation->shopkeeperAnimIndex = 0;
            arg0->animationState = 1;
            allocation->forceShopkeeperAnimUpdate = 0;
        }
    }

    enableViewportDisplayList(&allocation->secondaryViewport);
    updateModelGeometry(arg0->model);
}

void cleanupBoardShopShopkeeper(BoardShopShopkeeperState *arg0) {
    destroySceneModel(arg0->model);
}

void loadBoardShopBackground(TileMapRenderTaskState *state) {
    state->asset = loadCompressedData(&previewBackgroundAsset_ROM_START, &previewBackgroundAsset_ROM_END, 0x14410);
    setCleanupCallback(&cleanupBoardShopBackground);
    setCallback(&initBoardShopBackgroundRenderState);
}

void initBoardShopBackgroundRenderState(TileMapRenderTaskState *state) {
    initScrollingTileMapState(&state->renderState, state->asset);
    setCallback(&enqueueBoardShopBackgroundRender);
}

void enqueueBoardShopBackgroundRender(TileMapRenderTaskState *state) {
    pushViewportCallbackBySlot(9, VIEWPORT_CALLBACK_LAYER_INITIAL, &renderTiledTextureMap, &state->renderState);
}

void cleanupBoardShopBackground(TileMapRenderTaskState *state) {
    state->asset = freeNodeMemory(state->asset);
}

void cleanupBoardShopComparisonIcons(TextRenderArg *arg0);

void updateBoardShopComparisonIcons(BoardShopComparisonIconsState *arg0);

void initBoardShopComparisonIcons(BoardShopComparisonIconsState *arg0) {
    s32 i;
    void *spriteAsset;

    spriteAsset = loadCompressedData(&menuUiSprites_ROM_START, &menuUiSprites_ROM_END, 0x8A08);
    setCleanupCallback(&cleanupBoardShopComparisonIcons);

    for (i = 0; i < 2; i++) {
        arg0->comparisonIcons[i].x = i * 0x50 + -0x30;
        arg0->comparisonIcons[i].y = -0x18;
        arg0->comparisonIcons[i].frameIndex = i;
        arg0->comparisonIcons[i].spriteData = spriteAsset;
        arg0->comparisonIcons[i].color.paletteAndAlphaSigned = 0xFF;
        arg0->comparisonIcons[i].overridePaletteCount = 0;
        arg0->comparisonIcons[i].tileMode = 0;
    }

    arg0->animationCounter = 0;
    setCallback(&updateBoardShopComparisonIcons);
}

void updateBoardShopComparisonIcons(BoardShopComparisonIconsState *arg0) {
    BoardShopState *allocation;
    TextRenderArg *icon;
    u8 state;
    s16 s4;
    s16 s3;

    allocation = getCurrentAllocation();

    if (allocation->shopState < 15) {
        s4 = 0xFF;
        s3 = 8;
        icon = arg0->comparisonIcons;

    loop:
        state = allocation->shopState;
        if ((state == 2) | (state == 5)) {
            if (arg0->animationCounter < 16) {
                icon->color.paletteAndAlphaSigned = icon->color.paletteAndAlphaSigned - 8;
            } else {
                icon->color.paletteAndAlphaSigned = icon->color.paletteAndAlphaSigned + 8;
            }
        } else {
            arg0->animationCounter = 0;
            icon->color.paletteAndAlphaSigned = s4;
            state = allocation->shopState;
            if ((state == 4) & (state == 7)) {
                if (allocation->delayTimer & 1) {
                    icon->overridePaletteCount = s4;
                } else {
                    icon->overridePaletteCount = 0;
                }
            }
        }

        state = allocation->shopState;
        if (state >= 5) {
            icon->y = s3;
        } else {
            icon->y = -24;
        }

        if (allocation->shopState == 3) {
            if (allocation->transitionDirection < 0) {
                icon->y = s3;
            }
        }

        pushViewportCallbackBySlot(8, VIEWPORT_CALLBACK_LAYER_INITIAL, renderTextSprite, icon);
        icon++;
        if ((s32)icon < (s32)(arg0->comparisonIcons + 2)) {
            goto loop;
        }

        state = allocation->shopState;
        if ((state != 4) & (state != 7)) {
            if (state != 3) {
                arg0->animationCounter = (arg0->animationCounter + 1) & 0x1F;
            }
        }
    }
}

void cleanupBoardShopComparisonIcons(TextRenderArg *arg0) {
    arg0->spriteData = freeNodeMemory(arg0->spriteData);
}

void cleanupBoardShopRowSelectorArrow(TextRenderArg *arg0);
void updateBoardShopRowSelectorArrow(TextRenderArg *arg0);

void initBoardShopRowSelectorArrow(TextRenderArg *arg0) {
    void *asset;

    getCurrentAllocation();
    asset = loadCompressedData(&menuUiSprites_ROM_START, &menuUiSprites_ROM_END, 0x8A08);
    setCleanupCallback(&cleanupBoardShopRowSelectorArrow);
    arg0->x = -0x1C;
    arg0->y = -0x18;
    arg0->frameIndex = 0x1D;
    arg0->color.paletteAndAlphaSigned = 0xFF;
    arg0->tileMode = 0;
    arg0->overridePaletteCount = 0;
    arg0->spriteData = asset;
    setCallback(&updateBoardShopRowSelectorArrow);
}

void updateBoardShopRowSelectorArrow(TextRenderArg *arg0) {
    BoardShopState *allocation;
    u8 temp;

    allocation = getCurrentAllocation();

    if (allocation->shopState < 0xF) {
        if (allocation->shopState != 3) {
            arg0->frameIndex = allocation->selectedCategoryIndex + 0x1D;

            if (allocation->shopState == 4) {
                if (allocation->delayTimer & 1) {
                    temp = 0xFF;
                    arg0->overridePaletteCount = temp;
                } else {
                    arg0->overridePaletteCount = 0;
                }
            }

            pushViewportCallbackBySlot(8, VIEWPORT_CALLBACK_LAYER_INITIAL, renderTextSprite, arg0);
        }
    }
}

void cleanupBoardShopRowSelectorArrow(TextRenderArg *arg0) {
    arg0->spriteData = freeNodeMemory(arg0->spriteData);
}

void updateBoardShopColumnSelectorArrow(TextRenderArg *arg0);

void initBoardShopColumnSelectorArrow(TextRenderArg *arg0) {
    void *asset;

    getCurrentAllocation();
    asset = loadCompressedData(&menuUiSprites_ROM_START, &menuUiSprites_ROM_END, 0x8A08);
    setCleanupCallback(&cleanupBoardShopColumnSelectorArrow);
    arg0->x = -8;
    arg0->y = 8;
    arg0->frameIndex = 0x24;
    arg0->color.paletteAndAlphaSigned = 0xFF;
    arg0->tileMode = 0;
    arg0->overridePaletteCount = 0;
    arg0->spriteData = asset;
    setCallback(&updateBoardShopColumnSelectorArrow);
}

void updateBoardShopColumnSelectorArrow(TextRenderArg *arg0) {
    BoardShopState *allocation;
    u8 state;
    u8 temp;

    allocation = getCurrentAllocation();
    state = allocation->shopState;

    if (state < 0xF) {
        if ((state >= 5) && (state != 6)) {
            arg0->frameIndex = allocation->selectedBoard.signedIndex + 0x24;

            if (allocation->shopState == 7) {
                if (allocation->delayTimer & 1) {
                    temp = 0xFF;
                    arg0->overridePaletteCount = temp;
                } else {
                    arg0->overridePaletteCount = 0;
                }
            }

            pushViewportCallbackBySlot(8, VIEWPORT_CALLBACK_LAYER_INITIAL, &renderTextSprite, arg0);
        }
    }
}

void cleanupBoardShopColumnSelectorArrow(TextRenderArg *arg0) {
    arg0->spriteData = freeNodeMemory(arg0->spriteData);
}

void initBoardShopExitOverlay(SpriteRenderArg *arg0) {
    void *overlayAsset = loadCompressedData(&okPromptSprites_ROM_START, &okPromptSprites_ROM_END, 0x1B48);
    setCleanupCallback(&cleanupBoardShopExitOverlay);
    arg0->x = -0x2C;
    arg0->y = -0x14;
    arg0->frameIndex = 0xD;
    arg0->spriteData = overlayAsset;
    setCallback(&updateBoardShopExitOverlay);
}

void updateBoardShopExitOverlay(void *arg0) {
    BoardShopState *allocation;

    allocation = getCurrentAllocation();
    if (allocation->shopState == 0x19) {
        pushViewportCallbackBySlot(8, VIEWPORT_CALLBACK_LAYER_FINAL, &renderSpriteFrame, arg0);
    }
}

void cleanupBoardShopExitOverlay(SpriteRenderArg *arg0) {
    arg0->spriteData = freeNodeMemory(arg0->spriteData);
}

void initBoardShopGoldDisplay(BoardShopGoldDisplayState *arg0) {
    void *digitAsset = loadCompressedData(&digit_sprite_ROM_START, &digit_sprite_ROM_END, 0x508);
    void *iconAsset = loadCompressedData(&goldIconSprite_ROM_START, &goldIconSprite_ROM_END, 0x388);
    s32 i;

    setCleanupCallback(&cleanupBoardShopGoldDisplay);

    for (i = 0; i < 7; i++) {
        arg0->digits[i].x = 0x48 + (i * 8);
        arg0->digits[i].y = 0x58;
        arg0->digits[i].spriteData = digitAsset;
    }

    arg0->icon.x = 0x38;
    arg0->icon.y = 0x58;
    arg0->icon.frameIndex = 0;
    arg0->icon.paletteIndex = 0;
    arg0->icon.spriteData = iconAsset;

    setCallback(&updateBoardShopGoldDisplay);
}

void updateBoardShopGoldDisplay(BoardShopGoldDisplayState *arg0) {
    SpriteRenderArg *digit;
    s32 space;
    s32 i;
    s32 colorStyle;

    if (gGameSessionContext->gold < 100) {
        colorStyle = 1;
        i = 6;
        do {
            arg0->digits[i].paletteIndex = colorStyle;
        } while (--i >= 0);
    } else {
        colorStyle = 2;
        i = 6;
        do {
            arg0->digits[i].paletteIndex = colorStyle;
        } while (--i >= 0);
    }

    sprintf(arg0->goldString, D_8009E47C_9F07C, gGameSessionContext->gold);

    i = 0;
    space = 0x20;
    digit = arg0->digits;

    for (; i < 7; i++) {
        if (arg0->goldString[i] != space) {
            digit->frameIndex = arg0->goldString[i] - 0x30;
            pushViewportCallbackBySlot(9, VIEWPORT_CALLBACK_LAYER_FINAL, renderSpriteFrameWithPalette, digit);
        }
        digit++;
    }

    pushViewportCallbackBySlot(9, VIEWPORT_CALLBACK_LAYER_FINAL, renderSpriteFrameWithPalette, &arg0->icon);
}

void cleanupBoardShopGoldDisplay(BoardShopGoldDisplayState *arg0) {
    arg0->digits[0].spriteData = freeNodeMemory(arg0->digits[0].spriteData);
    arg0->icon.spriteData = freeNodeMemory(arg0->icon.spriteData);
}

void initBoardShopBoardIcons(BoardShopBoardIconsState *arg0) {
    BoardShopState *state;
    void *spriteAsset;
    s32 i;
    u8 boardIndex;

    state = getCurrentAllocation();
    spriteAsset = loadCompressedData(&snowflakeSprite_ROM_START, &snowflakeSprite_ROM_END, 0x9488);

    for (i = 0; i < 4; i++) {
        arg0->sprites[i].x = 0x60;
        arg0->sprites[i].y = -0x91;

        boardIndex = state->boardDisplayIndices[i];
        boardIndex = state->boardIndexMap[boardIndex];

        arg0->sprites[i].frameIndex = boardIndex;
        arg0->sprites[i].scaleX = 0x400;
        arg0->sprites[i].scaleY = 0x400;
        arg0->sprites[i].rotation = 0;
        arg0->sprites[i].alpha.value = 0xFF;
        arg0->sprites[i].overridePaletteCount = 0;
        arg0->sprites[i].tileMode = 0;
        arg0->sprites[i].flipX = 0;
        arg0->sprites[i].spriteData = spriteAsset;
        arg0->animation.animationCounters[i] = 0;
    }

    arg0->priceTextX = 0x58;
    arg0->priceTextY = -0x2A;
    arg0->priceTextStyle = 0;
    arg0->priceTextPtr = arg0->priceTextBuffer;

    setCleanupCallback(&cleanupBoardShopBoardIcons);
    setCallback(&animateBoardShopBoardIconsSlideIn);
}

void animateBoardShopBoardIconsSlideIn(BoardShopBoardIconsState *arg0) {
    BoardShopState *allocation;
    s32 animatingCount;
    s32 i;
    s16 currentY;
    s32 delta;
    s16 absDelta;

    allocation = getCurrentAllocation();
    animatingCount = 0;

    for (i = 0; i < 4; i++) {
        currentY = arg0->sprites[i].y;
        if (currentY < boardIconTargetYPositions[i]) {
            delta = boardIconTargetYPositions[i] - currentY;
            absDelta = ABS(delta);
            if (absDelta >= 20) {
                arg0->sprites[i].y = currentY + 20;
            } else {
                arg0->sprites[i].y = currentY + absDelta;
            }
            animatingCount++;
        }
        pushViewportCallbackBySlot(
            8,
            VIEWPORT_CALLBACK_LAYER_INITIAL,
            renderFlippedScaledSpriteFrame,
            &arg0->sprites[i]
        );
    }

    if ((animatingCount & 0xFF) == 0) {
        allocation->delayTimer = 1;
        setCallback(updateBoardShopBoardIconSelection);
    }
}

void updateBoardShopBoardIconSelection(BoardShopBoardIconsState *arg0) {
    BoardShopState *state;
    s32 i;
    int new_var;
    u8 temp;
    s16 adjustment;
    state = getCurrentAllocation();
    for (i = 0; i < 4; i++) {
        if (state->selectedSlot == i) {
            arg0->sprites[i].alpha.value = 0xFF;
            if (state->delayTimer >= 5) {
                temp = arg0->animation.animationCounters[i];
                if (temp < 30) {
                    adjustment = D_8008F184_8FD84[temp / 10];
                    arg0->sprites[i].scaleY = arg0->sprites[i].scaleY + adjustment;
                } else {
                    adjustment = D_8008F184_8FD84[2 - ((temp - 30) / 10)];
                    arg0->sprites[i].scaleY = arg0->sprites[i].scaleY - adjustment;
                }
                arg0->animation.animationCounters[i]++;
                temp = arg0->animation.animationCounters[i];
                if (temp == 60) {
                    arg0->animation.animationCounters[i] = 0;
                    arg0->sprites[i].scaleY = 0x400;
                } else if (temp == 30) {
                    arg0->sprites[i].flipX = (arg0->sprites[i].flipX + 1) & 1;
                }
            }
        } else {
            arg0->sprites[i].alpha.value = 0x80;
            arg0->animation.animationCounters[i] = 0;
            arg0->sprites[i].flipX = 0;
            arg0->sprites[i].scaleY = 0x400;
        }
        pushViewportCallbackBySlot(
            8,
            VIEWPORT_CALLBACK_LAYER_INITIAL,
            renderFlippedScaledSpriteFrame,
            &arg0->sprites[i]
        );
    }

    if (state->shopState == 0x11) {
        for (i = 0; i < 4; i++) {
            arg0->sprites[i].alpha.value = 0x80;
            arg0->sprites[i].scaleY = 0x400;
            arg0->sprites[i].flipX = 0;
            arg0->sprites[i].frameIndex = state->boardIndexMap[state->boardDisplayIndices[i]];
        }

        arg0->animation.animationCounters[0] = 1;
        setCallback(animateBoardShopBoardIconsSlideOut);
    } else if (state->shopState == 0x13) {
        new_var = 0x400;
        for (i = 3; i >= 0; i--) {
            arg0->sprites[i].scaleY = new_var;
        }

        setCallback(initBoardShopCharacterPortraitsSlideIn);
    } else {
        temp = state->boardIndexMap[state->boardDisplayIndices[state->selectedSlot]];
        sprintf(arg0->priceTextBuffer, &D_8009E480_9F080, D_8008F150_8FD50[temp]);
        arg0->priceTextY = (state->selectedSlot * 0x28) - 0x2A;
        pushViewportCallbackBySlot(8, VIEWPORT_CALLBACK_LAYER_FINAL, renderTextPalette, &arg0->priceTextX);
        if (state->shopState == 0x14) {
            setCallback(blinkBoardShopBoardIconConfirmation);
        }
    }
}

void blinkBoardShopBoardIconConfirmation(BoardShopBoardIconsState *arg0) {
    BoardShopState *state;
    s32 i;
    u8 temp;
    s16 temp2;

    state = getCurrentAllocation();

    for (i = 0; i < 4; i++) {
        arg0->sprites[i].scaleY = 0x400;
        arg0->animation.animationCounters[i] = 0;
        temp2 = 0xFF;

        if (state->selectedSlot == i) {
            arg0->sprites[i].alpha.value = 0xFF;
            arg0->sprites[i].overridePaletteCount = 0;
            if (state->shopState == 0x14) {
                if ((state->delayTimer & 1) != 0) {
                    __asm__ volatile("" ::: "memory");
                    arg0->sprites[i].overridePaletteCount = temp2;
                }
            }
        } else {
            arg0->sprites[i].alpha.value = 0x80;
        }

        pushViewportCallbackBySlot(
            8,
            VIEWPORT_CALLBACK_LAYER_INITIAL,
            &renderFlippedScaledSpriteFrame,
            &arg0->sprites[i]
        );
    }

    temp = state->boardDisplayIndices[state->selectedSlot];
    temp = state->boardIndexMap[temp];

    sprintf(arg0->priceTextBuffer, &D_8009E480_9F080, D_8008F150_8FD50[temp]);
    arg0->priceTextY = state->selectedSlot * 0x28 - 0x2A;
    pushViewportCallbackBySlot(8, VIEWPORT_CALLBACK_LAYER_FINAL, &renderTextPalette, &arg0->priceTextX);
    if (state->shopState < 0x14) {
        setCallback(&updateBoardShopBoardIconSelection);
    }
}

void initBoardShopCharacterPortraitsSlideIn(BoardShopBoardIconsState *arg0) {
    BoardShopState *allocation;
    s32 i;
    s32 startX;
    s16 currentX;

    allocation = getCurrentAllocation();

    if (allocation->scrollDirection == 1) {
        startX = -0x11;
    } else {
        startX = -0x61;
    }

    i = 0;
    currentX = startX;

    for (i = 0; i < 4; i++) {
        arg0->sprites[i].y = currentX;
        arg0->sprites[i].frameIndex = allocation->boardIndexMap[allocation->boardDisplayIndices[i]];
        arg0->animation.portraitFrameCounters[i] = 0;
        pushViewportCallbackBySlot(
            8,
            VIEWPORT_CALLBACK_LAYER_INITIAL,
            &renderFlippedScaledSpriteFrame,
            &arg0->sprites[i]
        );
        currentX += 0x28;
    }

    setCallback(&animateBoardShopCharacterPortraitsSlideIn);
}

void animateBoardShopCharacterPortraitsSlideIn(BoardShopBoardIconsState *arg0) {
    BoardShopState *gameState;
    s32 i;

    gameState = getCurrentAllocation();

    for (i = 0; i < 4; i++) {
        arg0->animation.portraitFrameCounters[i]++;

        if (gameState->scrollDirection == 1) {
            arg0->sprites[i].y -= 10;
        } else {
            arg0->sprites[i].y += 10;
        }

        pushViewportCallbackBySlot(
            8,
            VIEWPORT_CALLBACK_LAYER_INITIAL,
            &renderFlippedScaledSpriteFrame,
            &arg0->sprites[i]
        );
    }

    if (arg0->animation.portraitFrameCounters[0] == 4) {
        gameState->shopState = 0x10;

        for (i = 0; i < 4; i++) {
            arg0->animation.portraitFrameCounters[i] = 0;
        }

        setCallback(&updateBoardShopBoardIconSelection);
    }
}

void animateBoardShopBoardIconsSlideOut(BoardShopBoardIconsState *arg0) {
    BoardShopState *allocation;
    FlippedScaledSpriteArg *icons;
    u8 slideCount;
    s32 i;

    allocation = getCurrentAllocation();

    icons = arg0->sprites;
    for (i = 0; i < 4; i++) {
        if (i >= (4 - arg0->animation.slideOut.slidingIconCount)) {
            icons[i].y -= 0x14;
            if (i != 0) {
                slideCount = arg0->animation.slideOut.slidingIconCount & 0xFF;
                if (arg0->sprites[4 - slideCount].y == arg0->sprites[3 - slideCount].y) {
                    arg0->animation.slideOut.slidingIconCount++;
                }
            }
        }
        pushViewportCallbackBySlot(8, VIEWPORT_CALLBACK_LAYER_INITIAL, &renderFlippedScaledSpriteFrame, &icons[i]);
    }

    if (arg0->sprites[0].y < (-0x88)) {
        allocation->delayTimer = 1;
        terminateCurrentTask();
    }
}

void cleanupBoardShopBoardIcons(BoardShopBoardIconsState *arg0) {
    arg0->sprites[0].spriteData = freeNodeMemory(arg0->sprites[0].spriteData);
}

void initBoardShopSnowflakeSlideIn(BoardShopSnowflakeSpriteState *arg0) {
    void *snowflakeAsset;
    BoardShopState *allocation = getCurrentAllocation();

    snowflakeAsset = loadCompressedData(&snowflakeSprite_ROM_START, &snowflakeSprite_ROM_END, 0x9488);

    arg0->sprite.x = 0x60;
    arg0->sprite.frameIndex = allocation->boardIndexMap[allocation->scrollOutBoardIndex];
    arg0->sprite.scaleX = 0x400;
    arg0->sprite.scaleY = 0x400;
    arg0->sprite.rotation = 0;
    arg0->sprite.alpha.value = 0x80;
    arg0->sprite.overridePaletteCount = 0;
    arg0->sprite.tileMode = 0;
    arg0->sprite.flipX = 0;
    arg0->sprite.spriteData = snowflakeAsset;
    arg0->task.frameCounter = 0;

    if (allocation->scrollDirection != 1) {
        arg0->sprite.y = 0x3F;
    } else {
        arg0->sprite.y = -0x39;
    }

    setCleanupCallback(&cleanupBoardShopSnowflakeSprite);
    setCallback(&queueBoardShopSnowflakeRender);
}

void queueBoardShopSnowflakeRender(void *arg0) {
    pushViewportCallbackBySlot(8, VIEWPORT_CALLBACK_LAYER_INITIAL, &renderFlippedScaledSpriteFrame, arg0);
    setCallback(&animateBoardShopSnowflakeSlideIn);
}

void animateBoardShopSnowflakeSlideIn(BoardShopSnowflakeSpriteState *arg0) {
    BoardShopState *allocation = getCurrentAllocation();

    arg0->task.frameCounter++;
    if (allocation->scrollDirection == 1) {
        arg0->sprite.y = arg0->sprite.y - 0x14;
    } else {
        arg0->sprite.y = arg0->sprite.y + 0x14;
    }

    pushViewportCallbackBySlot(8, VIEWPORT_CALLBACK_LAYER_INITIAL, &renderFlippedScaledSpriteFrame, &arg0->sprite);

    if (arg0->task.frameCounter == 4) {
        terminateCurrentTask();
    }
}

void cleanupBoardShopSnowflakeSprite(SpriteRenderArg *arg0) {
    arg0->spriteData = freeNodeMemory(arg0->spriteData);
}

void initBoardShopTitleText(BoardShopTitleTextState *arg0) {
    void *textAsset = loadTextRenderAsset(1);
    setCleanupCallback(&cleanupBoardShopTitleText);
    arg0->y = -0x60;
    arg0->textData = boardShopChooseBoardToPaintPromptText;
    arg0->primaryColor = 0xFF;
    arg0->secondaryColor = 0xFF;
    arg0->textAsset = textAsset;
    arg0->textWidth = D_8008F20C_8FE0C;
    arg0->textStyle = 5;
    setCallback(&updateBoardShopTitleText);
}

void updateBoardShopTitleText(BoardShopTitleTextState *arg0) {
    BoardShopState *allocation = getCurrentAllocation();
    u16 *new_var;

    if (allocation->viewMode != 0) {
        new_var = D_8008F200_8FE00.unkA;
        arg0->textWidth = new_var[allocation->viewMode];
        // this makes no sense but it matches
        new_var = (void *)&renderTextLayout;
        arg0->textData = (void *)D_8008F200_8FE00.unk0[allocation->viewMode];
        pushViewportCallbackBySlot(9, VIEWPORT_CALLBACK_LAYER_FINAL, new_var, arg0);
    }
}

void cleanupBoardShopTitleText(BoardShopTitleTextState *arg0) {
    arg0->textAsset = freeNodeMemory(arg0->textAsset);
}

void initBoardShopTitleCorners(TextRenderArg *arg0) {
    s32 i;
    void *cornerAsset = loadCompressedData(&uiCornerSprites_ROM_START, &uiCornerSprites_ROM_END, 0x1548);
    setCleanupCallback(&cleanupBoardShopTitleCorners);

    for (i = 0; i < 4; i++) {
        TextRenderArg *corner = &arg0[i];

        if (i % 2 != 0) {
            corner->x = -0x80;
        } else {
            corner->x = 0;
        }

        corner->y = (s16)(((i / 2) * 0x10) - 0x66);
        corner->spriteData = cornerAsset;
        corner->frameIndex = i;
        corner->color.paletteAndAlphaSigned = 0xFF;
        corner->overridePaletteCount = 0;
        corner->tileMode = 1;
    }

    setCallback(&updateBoardShopTitleCorners);
}

void updateBoardShopTitleCorners(TextRenderArg *arg0) {
    s32 i;

    if (((BoardShopState *)getCurrentAllocation())->viewMode != 0) {
        for (i = 0; i < 4; i++) {
            pushViewportCallbackBySlot(9, VIEWPORT_CALLBACK_LAYER_OPAQUE, &renderTextSprite, &arg0[i]);
        }
    }
}

void cleanupBoardShopTitleCorners(TextRenderArg *arg0) {
    arg0->spriteData = freeNodeMemory(arg0->spriteData);
}
