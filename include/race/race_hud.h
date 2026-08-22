#pragma once

#include "common.h"
#include "gamestate.h"
#include "graphics/displaylist.h"
#include "math/geometry.h"

typedef struct {
    Transform3D transform;
    DisplayLists *skyDisplayLists;
    void *skyAsset1;
    void *skyAsset2;
    s32 unk2C;
    u8 _pad30[0xC];
    Transform3D courseFogTransform;
    DisplayLists *fogDisplayLists;
    void *skyAsset1Copy;
    void *skyAsset2Copy;
    s32 unk68;
    u8 _pad6C[0xC];
    Transform3D defaultFogTransform;
    DisplayLists *defaultFogDisplayLists;
    void *unk9C;
    void *unkA0;
    s32 unkA4;
    u8 _padA8[0xC];
    s16 skyType;
} SkyRenderTaskState;

void schedulePlayerHaloTask(Player *);

void scheduleCourseTasks(s32 courseId, s32 playerCount);

void scheduleLevelEnvironmentTasks(s32 poolId);

void spawnBossHomingProjectileTask(void *boss);

void spawnBossHomingProjectileVariant1Task(Player *);

void spawnBossHomingProjectileVariant2Task(Player *);

void *spawnItemHomingProjectile(void *, u32, void *, s16, s32);

void scheduleSkyRenderTask(s32 skyType);

typedef struct FlyingSceneryState FlyingSceneryState;

void initFlyingSceneryTask(FlyingSceneryState *state);
void schedulePlayerAuraTask(void *player);
void *spawnPanelProjectile(void *player);
