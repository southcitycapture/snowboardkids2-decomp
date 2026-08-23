#pragma once

#include "common.h"
#include "graphics/displaylist.h"
#include "graphics/graphics.h"

typedef struct {
    /* 0x00 */ ViewportNode *viewport;
    /* 0x04 */ void *displayListData;
    /* 0x08 */ void *textureData;
    /* 0x0C */ DisplayListObject primaryDisplayList;
    /* 0x48 */ DisplayListObject secondaryDisplayList;
    /* 0x84 */ s16 configIndex;
    /* 0x86 */ s8 isDisposed;
    /* 0x87 */ s8 isVisible;
} ModelEntity;

void setupModelEntityLighting(ModelEntity *entity, ColorData *lightColors, ColorData *ambientColor);

s32 initModelEntity(ModelEntity *entity, s16 index, ViewportNode *viewport);

void renderModelEntity(ModelEntity *entity);

void cleanupModelEntity(ModelEntity *entity);

void setModelEntityVisibility(ModelEntity *entity, s8 isVisible);
