#include "common.h"
#include "graphics/graphics.h"
#include "system/task_scheduler.h"

typedef struct {
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    u8 red;
    u8 green;
    u8 blue;
    u8 state;
} ColorRectState;

typedef struct {
    ColorRectState *state;
    ViewportNode *viewport;
    ColorRectState rects[4];
} ColorRectTask;

void func_800BAFF0_1E8040(ColorRectState *, s16, s16, u16, u16);
void func_800BB028_1E8078(ColorRectState *, u8, u8, u8);
void func_800BB058_1E80A8(void);
void func_800BB084_1E80D4(ColorRectTask *task);
void func_800BB1CC_1E821C(void);
void drawColorRect(ColorRectState *rect);

void func_800BAF10_1E7F60(
    ColorRectState *arg0,
    ViewportNode *arg1,
    s16 arg2,
    s16 arg3,
    u16 arg4,
    u16 arg5,
    u8 arg6,
    u8 arg7,
    u8 arg8
) {
    ViewportNode *viewport;
    ColorRectTask *temp_v0;

    viewport = arg1;
    temp_v0 = scheduleTask(func_800BB058_1E80A8, 0U, 0U, 0x64U);
    if (temp_v0 != NULL) {
        func_800BAFF0_1E8040(arg0, arg2, arg3, arg4, (u16)(s32)arg5);
        func_800BB028_1E8078(arg0, arg6, arg7, arg8);
        temp_v0->state = arg0;
        temp_v0->viewport = viewport;
    }
}

void func_800BAFF0_1E8040(ColorRectState *arg0, s16 arg1, s16 arg2, u16 arg3, u16 arg4) {
    arg0->x = arg1;
    arg0->y = arg2;
    arg0->width = arg3;
    arg0->height = arg4;
}

void func_800BB008_1E8058(ColorRectState *arg0, s16 arg1, s16 arg2, s32 arg3, s32 arg4) {
    arg0->x = arg1;
    arg0->y = arg2;
    arg0->width = arg3 - arg1;
    arg0->height = arg4 - arg2;
}

void func_800BB028_1E8078(ColorRectState *arg0, u8 arg1, u8 arg2, u8 arg3) {
    arg0->red = arg1;
    arg0->green = arg2;
    arg0->blue = arg3;
}

void func_800BB038_1E8088(ColorRectState *arg0) {
    arg0->state = 0;
}

void func_800BB040_1E8090(ColorRectState *arg0) {
    arg0->state = 1;
}

void func_800BB04C_1E809C(ColorRectState *arg0) {
    arg0->state = 2;
}

void func_800BB058_1E80A8(void) {
    setCleanupCallback(&func_800BB1CC_1E821C);
    setCallback(&func_800BB084_1E80D4);
}

void func_800BB084_1E80D4(ColorRectTask *task) {
    s32 i;
    ColorRectState *state;

    state = task->state;
    if (state->state != 1) {
        task->rects[0].x = state->x;
        task->rects[0].y = state->y;
        task->rects[0].width = state->width - 1;
        task->rects[0].height = 1;
        task->rects[1].x = state->x + state->width - 1;
        task->rects[1].y = state->y;
        task->rects[1].width = 1;
        task->rects[1].height = state->height - 1;
        task->rects[2].x = state->x;
        task->rects[2].y = state->y + state->height - 1;
        task->rects[2].width = state->width - 1;
        task->rects[2].height = 1;
        task->rects[3].x = state->x;
        task->rects[3].y = state->y;
        task->rects[3].width = 1;
        task->rects[3].height = state->height - 1;

        for (i = 0; i < 4; i++) {
            task->rects[i].red = state->red;
            task->rects[i].green = state->green;
            task->rects[i].blue = state->blue;
            task->rects[i].state = 0;
            pushViewportCallbackBySlot(task->viewport->callbackSlotIndex, 7, drawColorRect, &task->rects[i]);
        }
    }
}

void func_800BB1CC_1E821C(void) {
}
