#include "common.h"
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

void func_800BB084_1E80D4(void);
void func_800BB1CC_1E821C(void);

INCLUDE_ASM("asm/nonmatchings/1E7F60", func_800BAF10_1E7F60);

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

INCLUDE_ASM("asm/nonmatchings/1E7F60", func_800BB084_1E80D4);

void func_800BB1CC_1E821C(void) {
}
