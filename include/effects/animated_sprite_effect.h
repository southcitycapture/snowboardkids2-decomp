#pragma once

#include "common.h"
#include "math/geometry.h"

void *loadSpriteEffectTextureData(void);
void *loadSpriteEffectModelData(void);
void *freeSpriteEffectTextureData(void *data);
void *freeSpriteEffectModelData(void *data);
void scheduleSpriteEffectTask(const Vec3i *startPosition, const Vec3i *endPosition, Vec3i *velocity, s32 textureIndex);
