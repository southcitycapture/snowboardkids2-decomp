#include "credits/credits_decorations.h"
#include "graphics/graphics.h"
#include "graphics/sprite_rdp.h"

void initCreditsCornerDecorationSprites(CreditsState *arg0) {
    volatile CreditsState *state = (volatile CreditsState *)arg0;
    void *asset0;
    void *asset1;
    void *asset2;
    void *asset3;
    s16 spriteX;
    s16 topSpriteY;
    s16 alpha;

    asset0 = state->cornerDecorationAsset;
    asset1 = state->cornerDecorationAsset;
    asset2 = state->cornerDecorationAsset;
    asset3 = state->cornerDecorationAsset;
    spriteX = -0x40;
    topSpriteY = -0x10;
    alpha = 0xFF;

    do {
        state->leftBottomCornerSprite.frameIndex = 1;
        state->rightTopCornerSprite.frameIndex = 2;
        state->leftCornerAlpha = 0;
        state->leftCornerFadeSpeed = 0;
        state->rightCornerAlpha = 0;
        state->rightCornerFadeSpeed = 0;
        state->leftTopCornerSprite.x = spriteX;
        state->leftTopCornerSprite.y = topSpriteY;
        state->leftTopCornerSprite.frameIndex = 0;
        state->leftTopCornerSprite.paletteEffect.value = alpha;
        state->leftTopCornerSprite.tileMode = 0;
        state->leftTopCornerSprite.overridePaletteCount = 0;
        state->leftBottomCornerSprite.x = spriteX;
        state->leftBottomCornerSprite.y = 0;
        state->leftBottomCornerSprite.paletteEffect.value = alpha;
        state->leftBottomCornerSprite.tileMode = 0;
        state->leftBottomCornerSprite.overridePaletteCount = 0;
        state->rightTopCornerSprite.x = spriteX;
        state->rightTopCornerSprite.y = topSpriteY;
        state->rightTopCornerSprite.paletteEffect.value = alpha;
        state->rightTopCornerSprite.tileMode = 0;
        state->rightTopCornerSprite.overridePaletteCount = 0;
        state->rightBottomCornerSprite.x = spriteX;
        state->rightBottomCornerSprite.y = 0;
        state->rightBottomCornerSprite.frameIndex = 3;
        state->leftTopCornerSprite.spriteData = asset0;
        state->leftBottomCornerSprite.spriteData = asset1;
        state->rightTopCornerSprite.spriteData = asset2;
    } while (0);

    state->rightBottomCornerSprite.spriteData = asset3;
    state->rightBottomCornerSprite.paletteEffect.value = alpha;
    state->rightBottomCornerSprite.tileMode = 0;
    state->rightBottomCornerSprite.overridePaletteCount = 0;
}

void updateCreditsCornerDecorationSprites(CreditsState *state) {
    s16 temp_v1;
    s32 temp_v0;
    s32 value;

    temp_v1 = state->frameCounter;

    switch (temp_v1) {
        case 0x1A22:
            state->rightCornerFadeSpeed = 0xC0000;
            break;
        case 0x1AD6:
            state->rightCornerFadeSpeed = 0xFFF40000;
            break;
        case 0x1B30:
            state->leftCornerFadeSpeed = 0xC0000;
            break;
    }

    value = state->leftCornerFadeSpeed;
    if (value != 0) {
        temp_v0 = state->leftCornerAlpha + value;
        state->leftCornerAlpha = temp_v0;
        if (temp_v0 > 0xFF0000) {
            state->leftCornerAlpha = 0xFF0000;
        }
        if (state->leftCornerAlpha < 0) {
            state->leftCornerAlpha = 0;
        }
    }

    value = state->rightCornerFadeSpeed;
    if (value != 0) {
        temp_v0 = state->rightCornerAlpha + value;
        state->rightCornerAlpha = temp_v0;
        if (temp_v0 > 0xFF0000) {
            state->rightCornerAlpha = 0xFF0000;
        }
        if (state->rightCornerAlpha < 0) {
            state->rightCornerAlpha = 0;
        }
    }

    temp_v0 = state->leftCornerAlpha;
    if (temp_v0 != 0) {
        s16 shortVal = temp_v0 >> 16;
        void *callback = renderTextSprite;
        state->leftBottomCornerSprite.paletteEffect.value = shortVal;
        state->leftTopCornerSprite.paletteEffect.value = shortVal;
        pushViewportCallbackBySlot(1, VIEWPORT_CALLBACK_LAYER_SPRITES, callback, &state->leftTopCornerSprite);
        pushViewportCallbackBySlot(1, VIEWPORT_CALLBACK_LAYER_SPRITES, callback, &state->leftBottomCornerSprite);
    }

    temp_v0 = state->rightCornerAlpha;
    if (temp_v0 != 0) {
        s16 shortVal = temp_v0 >> 16;
        void *callback = renderTextSprite;
        state->rightBottomCornerSprite.paletteEffect.value = shortVal;
        state->rightTopCornerSprite.paletteEffect.value = shortVal;
        pushViewportCallbackBySlot(1, VIEWPORT_CALLBACK_LAYER_SPRITES, callback, &state->rightTopCornerSprite);
        pushViewportCallbackBySlot(1, VIEWPORT_CALLBACK_LAYER_SPRITES, callback, &state->rightBottomCornerSprite);
    }
}
