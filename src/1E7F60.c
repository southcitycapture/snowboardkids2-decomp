#include "common.h"
#include "system/task_scheduler.h"

void func_800BB084_1E80D4(void);
void func_800BB1CC_1E821C(void);

INCLUDE_ASM("asm/nonmatchings/1E7F60", func_800BAF10_1E7F60);

INCLUDE_ASM("asm/nonmatchings/1E7F60", func_800BAFF0_1E8040);

INCLUDE_ASM("asm/nonmatchings/1E7F60", func_800BB008_1E8058);

INCLUDE_ASM("asm/nonmatchings/1E7F60", func_800BB028_1E8078);

INCLUDE_ASM("asm/nonmatchings/1E7F60", func_800BB038_1E8088);

INCLUDE_ASM("asm/nonmatchings/1E7F60", func_800BB040_1E8090);

INCLUDE_ASM("asm/nonmatchings/1E7F60", func_800BB04C_1E809C);

void func_800BB058_1E80A8(void) {
    setCleanupCallback(&func_800BB1CC_1E821C);
    setCallback(&func_800BB084_1E80D4);
}

INCLUDE_ASM("asm/nonmatchings/1E7F60", func_800BB084_1E80D4);

void func_800BB1CC_1E821C(void) {
}
