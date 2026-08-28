#pragma once

#include "common.h"
#include "gamestate.h"
#include "graphics/sprite_rdp.h"
#include "math/geometry.h"

typedef struct {
    /* 0x00 */ SpriteRenderArg sprite;
    /* 0x0C */ u8 paddingC[4];
    /* 0x10 */ s32 frameCounter;
    /* 0x14 */ u16 renderPriority;
    /* 0x16 */ s16 halfSizeRender;
} FloatingItemSpriteTask;

void spawnCharacterTrailParticle(Player *player);
void spawnPlayerCharacterTrailParticle(Player *player, s32 characterId);
void spawnImpactStar(Vec3i *arg0);
void spawnCharacterAttackEffect(Player *player);
void spawnCharacterAttackEffectByType(Player *player, s32 characterId);
void spawnGlintEffect(Player *player);
void spawnDualSnowSprayEffect(Vec3i *pos1, Vec3i *pos2, Vec3i *velocity, s32 slotIndex, s32 characterId);
void spawnDualSnowSprayEffect_SingleSlot(Vec3i *pos1, Vec3i *pos2, Vec3i *velocity, s32 particleType);
void spawnSprayEffect(Vec3i *arg0, Vec3i *arg1, s32 arg2);
