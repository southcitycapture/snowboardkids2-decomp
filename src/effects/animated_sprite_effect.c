#include "effects/animated_sprite_effect.h"

#include "assets.h"
#include "graphics/displaylist.h"
#include "math/geometry.h"
#include "math/rand.h"
#include "system/task_scheduler.h"

typedef struct {
    s32 unk0;
    s32 unk4;
    s32 unk8;
    Vec3i position;
    s32 unk18;
    s32 unk1C;
} SpriteEffectPositionNode;

typedef struct {
    /* 0x00 */ void *modelData;
    /* 0x04 */ void *textureData;
    /* 0x08 */ BillboardSprite sprite1;
    /* 0x28 */ BillboardSprite sprite2;
    /* 0x48 */ Vec3i velocity;
    /* 0x54 */ s16 frameCounter;
    /* 0x56 */ s16 textureIndex;
} SpriteEffectTask;

void updateSpriteEffectTask(SpriteEffectTask *task);
void cleanupSpriteEffectTask(SpriteEffectTask *task);

void initSpriteEffectTask(SpriteEffectTask *task) {
    void *textureData;

    task->modelData = loadCompressedData(&spriteEffectModelData_ROM_START, &spriteEffectModelData_ROM_END, 0xF18);
    textureData = loadCompressedData(&spriteEffectTextureData_ROM_START, &spriteEffectTextureData_ROM_END, 0x240);
    task->textureData = textureData;
    task->sprite1.vertices = (Vtx *)((u8 *)textureData + (task->textureIndex << 6));
    task->sprite1.alpha = (randA() & 0x1F) + 0x70;
    task->frameCounter = 0;
    task->sprite2.vertices = task->sprite1.vertices;
    task->sprite2.alpha = task->sprite1.alpha;
    setCleanupCallback(&cleanupSpriteEffectTask);
    setCallbackWithContinue(&updateSpriteEffectTask);
}

void updateSpriteEffectTask(SpriteEffectTask *task) {
    loadAssetMetadata(&task->sprite1, task->modelData, task->frameCounter);

    task->sprite2.textureData = task->sprite1.textureData;
    task->sprite2.paletteData = task->sprite1.paletteData;
    task->sprite2.textureWidth = task->sprite1.textureWidth;
    task->sprite2.textureHeight = task->sprite1.textureHeight;

    enqueueAlphaSprite(0, &task->sprite1);
    enqueueAlphaSprite(0, &task->sprite2);

    if (task->frameCounter != 0) {
        s32 i;
        SpriteEffectPositionNode *node = (SpriteEffectPositionNode *)task;
        for (i = 0; i < 2; i++) {
            node[i].position.x += task->velocity.x;
            node[i].position.y += task->velocity.y;
            node[i].position.z += task->velocity.z;
        }
    }

    task->frameCounter++;
    if (task->frameCounter == 5) {
        terminateCurrentTask();
    }
}

void cleanupSpriteEffectTask(SpriteEffectTask *task) {
    task->textureData = freeNodeMemory(task->textureData);
    task->modelData = freeNodeMemory(task->modelData);
}

void scheduleSpriteEffectTask(const Vec3i *startPosition, const Vec3i *endPosition, Vec3i *velocity, s32 textureIndex) {
    SpriteEffectTask *task = (SpriteEffectTask *)scheduleTask(&initSpriteEffectTask, 0, 0, 0);
    if (task != NULL) {
        memcpy(&task->sprite1.position, startPosition, sizeof(Vec3i));
        memcpy(&task->sprite2.position, endPosition, sizeof(Vec3i));
        task->textureIndex = textureIndex;
        task->velocity.x = velocity->x / 2;
        task->velocity.y = velocity->y / 2;
        task->velocity.z = velocity->z / 2;
    }
}

void *loadSpriteEffectModelData(void) {
    return loadCompressedData(&spriteEffectModelData_ROM_START, &spriteEffectModelData_ROM_END, 0xF18);
}

void *loadSpriteEffectTextureData(void) {
    return loadCompressedData(&spriteEffectTextureData_ROM_START, &spriteEffectTextureData_ROM_END, 0x240);
}

void *freeSpriteEffectModelData(void *data) {
    return freeNodeMemory(data);
}

void *freeSpriteEffectTextureData(void *data) {
    return freeNodeMemory(data);
}
