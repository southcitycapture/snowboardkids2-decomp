#include "effects/static_sprite_effect.h"
#include "assets.h"
#include "common.h"
#include "graphics/displaylist.h"
#include "math/geometry.h"
#include "math/rand.h"
#include "system/task_scheduler.h"

typedef struct {
    s32 unk0;
    s32 unk4;
    Vec3i position;
    s32 unk14;
    s32 unk18;
    s32 unk1C;
} StaticSpriteEffectPositionNode;

typedef struct {
    void *modelData;
    BillboardSprite sprite1;
    BillboardSprite sprite2;
    Vec3i velocity;
    s16 frameCounter;
} StaticSpriteEffectTaskData;

typedef struct {
    u8 padding[0x8];
    Vec3i startPosition;
    u8 padding2[0x14];
    Vec3i endPosition;
    u8 padding3[0x10];
    Vec3i velocity;
    u8 padding4[0x4];
    s16 frameCounter;
} StaticSpriteEffectTaskMemory;

s32 staticSpriteEffectTexture[] = {
    (s32)0xFFE00040, 0x00000000,      (s32)0xFFF0FFF0, (s32)0xFFFFFFFF, 0x00200040, 0x00000000,
    0x03F0FFF0,      (s32)0xFFFFFFFF, 0x00200000,      0x00000000,      0x03F003F0, (s32)0xFFFFFFFF,
    (s32)0xFFE00000, 0x00000000,      (s32)0xFFF003F0, (s32)0xFFFFFFFF,
};

void updateStaticSpriteEffectTask(StaticSpriteEffectTaskData *);
void cleanupStaticSpriteEffectTask(void **);

void initStaticSpriteEffectTask(StaticSpriteEffectTaskData *arg0) {
    arg0->modelData = loadCompressedData(&spriteEffectModelData_ROM_START, &spriteEffectModelData_ROM_END, 0xF18);
    arg0->sprite1.vertices = (Vtx *)&staticSpriteEffectTexture;
    arg0->sprite1.alpha = (randA() & 0x1F) + 0x70;
    arg0->frameCounter = 0;
    arg0->sprite2.vertices = arg0->sprite1.vertices;
    arg0->sprite2.alpha = arg0->sprite1.alpha;
    setCleanupCallback(&cleanupStaticSpriteEffectTask);
    setCallbackWithContinue(&updateStaticSpriteEffectTask);
}

void updateStaticSpriteEffectTask(StaticSpriteEffectTaskData *arg0) {
    s32 i;
    BillboardSprite *sprite1_ptr = &arg0->sprite1;

    loadAssetMetadata(sprite1_ptr, arg0->modelData, arg0->frameCounter / 4);

    arg0->sprite2.textureData = arg0->sprite1.textureData;
    arg0->sprite2.paletteData = arg0->sprite1.paletteData;
    arg0->sprite2.textureWidth = arg0->sprite1.textureWidth;
    arg0->sprite2.textureHeight = arg0->sprite1.textureHeight;

    enqueueAlphaSprite(0, sprite1_ptr);
    enqueueAlphaSprite(0, &arg0->sprite2);

    if (arg0->frameCounter != 0) {
        StaticSpriteEffectPositionNode *node = (StaticSpriteEffectPositionNode *)arg0;
        for (i = 0; i < 2; i++) {
            node[i].position.x += arg0->velocity.x;
            node[i].position.y += arg0->velocity.y;
            node[i].position.z += arg0->velocity.z;
        }
    }

    arg0->frameCounter++;
    if (arg0->frameCounter == 20) {
        terminateCurrentTask();
    }
}

void cleanupStaticSpriteEffectTask(void **arg0) {
    *arg0 = freeNodeMemory(*arg0);
}

void scheduleStaticSpriteEffectTask(const Vec3i *startPosition, const Vec3i *endPosition, Vec3i *velocity, s32 unused) {
    StaticSpriteEffectTaskMemory *task =
        (StaticSpriteEffectTaskMemory *)scheduleTask(&initStaticSpriteEffectTask, 0, 0, 0);
    if (task != NULL) {
        memcpy(&task->startPosition, startPosition, sizeof(Vec3i));
        memcpy(&task->endPosition, endPosition, sizeof(Vec3i));
        task->frameCounter = 0;
        task->velocity.x = velocity->x / 2;
        task->velocity.y = velocity->y / 2;
        task->velocity.z = velocity->z / 2;
    }
}
