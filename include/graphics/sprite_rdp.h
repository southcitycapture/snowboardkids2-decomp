#pragma once

#include "common.h"

typedef struct {
    /* 0x00 */ s32 textureOffset;
    /* 0x04 */ u16 paletteIndex;
    /* 0x06 */ u16 width;
    /* 0x08 */ u16 height;
    /* 0x0A */ u16 paletteTableIndex;
    /* 0x0C */ u16 formatIndex;
} SpriteFrameEntry;

typedef struct {
    /* 0x00 */ s32 textureBase;
    /* 0x04 */ s32 numFrames;
    /* 0x08 */ SpriteFrameEntry frames[1];
} SpriteSheetData;

typedef union {
    u16 value;
    s16 signedValue;
    struct {
        u8 paletteIndex;
        union {
            u8 intensity;
            u8 alpha;
        } effect;
    } components;
} SpritePaletteEffect;

typedef union {
    u16 value;
    struct {
        u8 padding;
        u8 intensity;
    } components;
} SpriteShade;

typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ SpriteSheetData *spriteData;
    /* 0x08 */ u16 frameIndex;
    /* 0x0A */ u8 paletteIndex;
} SpriteRenderArg;

typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ SpriteSheetData *spriteData;
    /* 0x08 */ u16 frameIndex;
    /* 0x0A */ SpritePaletteEffect paletteEffect;
    /* 0x0C */ u8 tileMode;
    /* 0x0D */ u8 overridePaletteCount;
    /* 0x0E */ u8 primitiveAlpha;
} PaletteSpriteArg;

typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ SpriteSheetData *spriteData;
    /* 0x08 */ u16 frameIndex;
    /* 0x0A */ u8 envR;
    /* 0x0B */ u8 envG;
    /* 0x0C */ u8 envB;
    /* 0x0D */ u8 envA;
    /* 0x0E */ u8 tileMode;
    /* 0x0F */ u8 overridePaletteCount;
    /* 0x10 */ u8 primitiveAlpha;
} TintedSpriteArg;

typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ SpriteSheetData *spriteData;
    /* 0x08 */ u16 frameIndex;
    /* 0x0A */ u16 scaleX;
    /* 0x0C */ u16 scaleY;
    /* 0x0E */ union {
        struct {
            s16 rotation;
            SpriteShade shade;
        } shaded;
        struct {
            s16 renderWidth;
            s16 renderHeight;
        } cropped;
    } mode;
    /* 0x12 */ u8 tileMode;
    /* 0x13 */ u8 overridePaletteCount;
} ScaledSpriteArg;

typedef struct {
    /* 0x00 */ ScaledSpriteArg base;
    /* 0x14 */ union {
        u8 alpha;
        u8 flipX;
    } effect;
} TransformedSpriteArg;

void renderSpriteFrame(SpriteRenderArg *arg0);
void renderSpriteFrameWithPalette(SpriteRenderArg *arg0);
void renderHalfSizeSpriteFrame(SpriteRenderArg *arg0);
void renderHalfSizeSpriteWithCustomPalette(SpriteRenderArg *arg0);
void renderCharSelectIconSprite(ScaledSpriteArg *sprite);
void renderScaledShadedSpriteFrame(ScaledSpriteArg *arg0);
void renderScaledAlphaSpriteFrame(TransformedSpriteArg *sprite);
void renderAlphaBlendedTextSprite(PaletteSpriteArg *arg0);
void initDefaultFontPalette(void);
void loadSpriteTexture(s32 textureAddr, u16 width, u16 height, u16 format, s32 paletteMode);
void renderTintedSprite(TintedSpriteArg *arg0);
void renderTextSpriteWithTransparency(PaletteSpriteArg *arg0);
void renderFlippedScaledSpriteFrame(TransformedSpriteArg *sprite);
void renderTextSprite(PaletteSpriteArg *arg0);
