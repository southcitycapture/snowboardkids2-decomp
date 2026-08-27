#include "graphics/quad_displaylist.h"
#include "common.h"
#include "graphics/graphics.h"
#include "graphics/sprite_table.h"
#include "math/rand.h"
#include "system/task_scheduler.h"

void cleanupTrailingParticle(ParticleState *);
void updateTrailingParticle(ParticleState *);
void cleanupFallingParticle(ParticleState *);
void updateFallingParticle(ParticleState *);
void cleanupDriftingParticle(ParticleState *);
void updateDriftingParticle(ParticleState *);

void initFallingParticle(ParticleState *arg0) {
    s32 rand1;
    s32 rand2;
    u8 rand3;

    loadSpriteAsset(&arg0->spriteState, 2);
    setSpriteAnimation(&arg0->spriteState, 0x10000, 1, -1);

    rand1 = randA();
    rand1 = rand1 & 0xFF;
    rand1 = rand1 << 11;
    rand2 = randA();

    arg0->mode.falling.velocity.x = rand1 + 0xFFFC0000;
    arg0->mode.falling.velocity.y = 0;
    arg0->mode.falling.velocity.z = (((u8)rand2) << 11) + 0xFFFC0000;

    rand3 = randA();
    arg0->mode.falling.renderFlags = (rand3 % 3) + 2;

    setCleanupCallback(cleanupFallingParticle);
    setCallback(updateFallingParticle);
}

void updateFallingParticle(ParticleState *arg0) {
    SceneModel *particleOwner;
    s16 lifetime;
    s16 newLifetime;
    SpriteAssetState *sprite;

    if (arg0->owner->isDestroyed == 1) {
        terminateCurrentTask();
        return;
    }

    lifetime = arg0->mode.falling.lifetime;
    if (lifetime < 0) {
        terminateCurrentTask();
        return;
    }

    newLifetime = lifetime - 1;
    sprite = &arg0->spriteState;
    arg0->mode.falling.lifetime = newLifetime;
    updateSpriteAnimation(sprite, 0x10000);

    particleOwner = arg0->owner;
    if (particleOwner->visibilityEnabled != 0) {
        if (particleOwner->displayEnabled != 0) {
            setupAndEnqueueSprite(
                sprite,
                particleOwner->viewport->callbackSlotIndex,
                arg0->mode.falling.position.x + arg0->mode.falling.velocity.x,
                arg0->mode.falling.position.y + arg0->mode.falling.velocity.y,
                arg0->mode.falling.position.z + arg0->mode.falling.velocity.z,
                0x18000,
                0x18000,
                0,
                0,
                0xFF,
                arg0->mode.falling.renderFlags
            );
        }
    }

    arg0->mode.falling.velocity.y = arg0->mode.falling.velocity.y + 0xFFFEB852;
}

void cleanupFallingParticle(ParticleState *arg0) {
    releaseNodeMemoryRef((void **)&arg0->spriteState);
}

void initDriftingParticle(ParticleState *arg0) {
    loadSpriteAsset(&arg0->spriteState, 2);
    setSpriteAnimation(&arg0->spriteState, 0x10000, 0, -1);

    arg0->mode.drifting.scale = 0x10000;
    arg0->mode.drifting.positionOffset.z = 0;
    arg0->mode.drifting.positionOffset.y = 0;
    arg0->mode.drifting.positionOffset.x = 0;
    arg0->mode.drifting.velocity.z = 0;
    arg0->mode.drifting.velocity.y = 0;
    arg0->mode.drifting.velocity.x = 0;
    arg0->mode.drifting.targetVelocity.z = 0;
    arg0->mode.drifting.targetVelocity.y = 0;
    arg0->mode.drifting.targetVelocity.x = 0;
    arg0->mode.drifting.retargetTimers.z = 0;
    arg0->mode.drifting.retargetTimers.y = 0;
    arg0->mode.drifting.retargetTimers.x = 0;
    arg0->mode.drifting.renderFlags = 0;
    arg0->mode.drifting.flipHorizontal = 0;

    setCleanupCallback(cleanupDriftingParticle);
    setCallback(updateDriftingParticle);
}

void updateDriftingParticle(ParticleState *arg0) {
    s8 signs[2];
    s32 posX;
    s32 posY;
    s32 posZ;
    s32 velX;
    s32 targetVal;
    s32 deltaX;
    s32 deltaY;
    s32 deltaZ;

    do {
    } while (0);

    signs[0] = 1;
    signs[1] = -1;

    if (arg0->owner->isDestroyed == 1) {
        terminateCurrentTask();
        return;
    }

    switch (arg0->owner->actionMode) {
        case 0:
        default:
            arg0->mode.drifting.flipHorizontal = 0;
            break;
        case 1:
            arg0->mode.drifting.flipHorizontal = 1;
            break;
        case 2:
            arg0->mode.drifting.renderFlags = 0;
            break;
        case 3:
            arg0->mode.drifting.renderFlags = 1;
            break;
    }

    if (arg0->mode.drifting.retargetTimers.x < 0) {
        arg0->mode.drifting.retargetTimers.x = (randA() & 0x1F) + 4;
        targetVal = signs[randA() & 1] * (((randA() & 0xFF) << 16) % 0x66666);
        arg0->mode.drifting.targetOffset.x = targetVal;
        arg0->mode.drifting.targetVelocity.x =
            (targetVal - arg0->mode.drifting.positionOffset.x) / arg0->mode.drifting.retargetTimers.x;
    } else {
        arg0->mode.drifting.positionOffset.x += arg0->mode.drifting.velocity.x;
    }

    if (arg0->mode.drifting.retargetTimers.y < 0) {
        arg0->mode.drifting.retargetTimers.y = (randA() & 0x1F) + 4;
        targetVal = signs[randA() & 1] * (((randA() & 0xFF) << 16) % 419430);
        arg0->mode.drifting.targetOffset.y = targetVal;
        arg0->mode.drifting.targetVelocity.y =
            (targetVal - arg0->mode.drifting.positionOffset.y) / arg0->mode.drifting.retargetTimers.y;
    } else {
        arg0->mode.drifting.positionOffset.y += arg0->mode.drifting.velocity.y;
    }

    if (arg0->mode.drifting.retargetTimers.z < 0) {
        arg0->mode.drifting.retargetTimers.z = (randA() & 0x1F) + 4;
        targetVal = signs[randA() & 1] * (((randA() & 0xFF) << 16) % 419430);
        arg0->mode.drifting.targetOffset.z = targetVal;
        arg0->mode.drifting.targetVelocity.z =
            (targetVal - arg0->mode.drifting.positionOffset.z) / arg0->mode.drifting.retargetTimers.z;
    } else {
        arg0->mode.drifting.positionOffset.z += arg0->mode.drifting.velocity.z;
    }

    deltaX = (arg0->mode.drifting.targetVelocity.x - arg0->mode.drifting.velocity.x) / 4;
    deltaY = (arg0->mode.drifting.targetVelocity.y - arg0->mode.drifting.velocity.y) / 4;
    deltaZ = (arg0->mode.drifting.targetVelocity.z - arg0->mode.drifting.velocity.z) / 4;

    arg0->mode.drifting.velocity.x += deltaX;
    arg0->mode.drifting.velocity.y += deltaY;
    arg0->mode.drifting.velocity.z += deltaZ;

    if (deltaX == 0) {
        arg0->mode.drifting.velocity.x = arg0->mode.drifting.targetVelocity.x;
    }
    if (deltaY == 0) {
        arg0->mode.drifting.velocity.y = arg0->mode.drifting.targetVelocity.y;
    }
    if (deltaZ == 0) {
        arg0->mode.drifting.velocity.z = arg0->mode.drifting.targetVelocity.z;
    }

    arg0->mode.drifting.retargetTimers.x--;
    arg0->mode.drifting.retargetTimers.y--;
    arg0->mode.drifting.retargetTimers.z--;
    posX = arg0->owner->transform.translation.x + arg0->mode.drifting.positionOffset.x;
    posY = arg0->owner->transform.translation.y + arg0->mode.drifting.positionOffset.y;
    posZ = arg0->owner->transform.translation.z + arg0->mode.drifting.positionOffset.z;
    updateSpriteAnimation(&arg0->spriteState, 0x10000);

    if (arg0->owner->visibilityEnabled != 0 && arg0->owner->displayEnabled != 0) {
        setupAndEnqueueSprite(
            &arg0->spriteState,
            arg0->owner->viewport->callbackSlotIndex,
            posX,
            posY,
            posZ,
            arg0->mode.drifting.scale,
            arg0->mode.drifting.scale,
            0,
            (s32)(u8)arg0->mode.drifting.flipHorizontal,
            0xFF,
            arg0->mode.drifting.renderFlags
        );
    }
}

void cleanupDriftingParticle(ParticleState *arg0) {
    releaseNodeMemoryRef((void **)&arg0->spriteState);
}

void initTrailingParticle(ParticleState *arg0) {
    SceneModel *inner = arg0->owner;

    if (inner->index == 0x4F) {
        loadSpriteAsset(&arg0->spriteState, 6);
    } else {
        loadSpriteAsset(&arg0->spriteState, 3);
    }
    setSpriteAnimation(&arg0->spriteState, 0x10000, 0, -1);
    setCleanupCallback(cleanupTrailingParticle);
    setCallback(updateTrailingParticle);
}

void updateTrailingParticle(ParticleState *arg0) {
    s8 unused[2];
    SceneModel *inner;
    SceneModel *a0_inner;
    s32 posX;
    s32 posY;
    s32 posZ;

    do {
        unused[0] = 1;
    } while (0);

    do {
        unused[1] = -1;
    } while (0);

    inner = arg0->owner;

    if (inner->isDestroyed == 1) {
        terminateCurrentTask();
        return;
    }

    posX = inner->transform.translation.x;
    posY = inner->transform.translation.y;
    posZ = inner->transform.translation.z;

    updateSpriteAnimation(&arg0->spriteState, 0x10000);

    a0_inner = arg0->owner;
    if (a0_inner->visibilityEnabled != 0) {
        if (a0_inner->displayEnabled != 0) {
            renderOpaqueSprite(
                &arg0->spriteState,
                a0_inner->viewport->callbackSlotIndex,
                posX,
                posY,
                posZ,
                0x10000,
                0x10000,
                0,
                0
            );
        }
    }
}

void cleanupTrailingParticle(ParticleState *arg0) {
    releaseNodeMemoryRef((void **)&arg0->spriteState);
}
