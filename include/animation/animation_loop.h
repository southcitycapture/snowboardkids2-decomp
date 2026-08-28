#pragma once

#include "common.h"
#include "math/geometry.h"

typedef struct {
    u8 _pad[0x20];
    s16 rotXCurrent;
    s16 rotYCurrent;
    s16 rotXTarget;
    s16 rotYTarget;
    s16 rotXStep;
    s16 rotYStep;
    s16 rotXDuration;
    s16 rotYDuration;
    s16 rotXDurationCopy;
    s16 rotYDurationCopy;
    Vec3i position;
    Vec3i targetPosition;
    Vec3i positionStep;
    s16 posXDuration;
    s16 posYDuration;
    s16 posZDuration;
    s16 posXDurationCopy;
    s16 posYDurationCopy;
    s16 posZDurationCopy;
    s32 posYOffset;
    s32 shakeAmplitude;
    s16 shakeDuration;
    s16 nodeId;
    s8 animMode;
    s8 inputMode;
} CutsceneCameraState;

typedef struct {
    u8 _pad[0x68];
    s32 shakeAmplitude;
    s16 shakeDuration;
    s16 unk6E;
    u8 animMode;
    u8 unk71;
} CutsceneCameraShakeState;

void setAnimationLoopMode(CutsceneCameraState *arg0, s8 mode);
void *createAnimationLoopState(u16 nodeId);
void freeAnimationLoopState(void *arg0);
void initCutsceneCameraWithX(CutsceneCameraState *arg0, s16 rotX, s16 rotY, s32 posY, s32 posZ, s32 posX);
void copyRotDurationToPosX(CutsceneCameraState *arg0, CutsceneCameraState *arg1);
void animateCameraRotationX(CutsceneCameraState *arg0, s16 targetRotX, s16 duration);
void animateCameraRotationY(CutsceneCameraState *arg0, s16 targetRotY, s16 duration);
void animateCameraPositionX(CutsceneCameraState *arg0, s32 targetX, s16 duration);
void animateCameraPositionY(CutsceneCameraState *arg0, s32 targetY, s16 duration);
void animateCameraPositionZ(CutsceneCameraState *arg0, s32 targetZ, s16 duration);
void animateCameraRotationYContinuous(CutsceneCameraState *camera, s16 step, s16 duration);
s16 advanceCameraRotationYContinuous(CutsceneCameraState *camera);
s16 advanceCameraAnimation(CutsceneCameraState *arg0);
void initCameraShake(CutsceneCameraShakeState *cameraShake, s32 amplitude, s16 duration);
void finalizeAnimationLoop(CutsceneCameraState *arg0);
s16 advanceSceneManager(CutsceneCameraState *arg0);
