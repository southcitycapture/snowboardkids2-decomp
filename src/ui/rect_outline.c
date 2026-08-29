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
    u8 mode;
} RectOutline;

typedef struct {
    RectOutline *outline;
    ViewportNode *viewport;
    RectOutline edges[4];
} RectOutlineTask;

void setRectOutlineRect(RectOutline *, s16, s16, u16, u16);
void setRectOutlineColor(RectOutline *, u8, u8, u8);
void initRectOutlineTask(void);
void updateRectOutlineTask(RectOutlineTask *task);
void cleanupRectOutlineTask(void);
void drawColorRect(RectOutline *rect);

void startRectOutline(
    RectOutline *outline,
    ViewportNode *viewport,
    s16 x,
    s16 y,
    u16 width,
    u16 height,
    u8 red,
    u8 green,
    u8 blue
) {
    ViewportNode *targetViewport;
    RectOutlineTask *task;

    targetViewport = viewport;
    task = scheduleTask(initRectOutlineTask, 0U, 0U, 0x64U);
    if (task != NULL) {
        setRectOutlineRect(outline, x, y, width, (u16)(s32)height);
        setRectOutlineColor(outline, red, green, blue);
        task->outline = outline;
        task->viewport = targetViewport;
    }
}

void setRectOutlineRect(RectOutline *outline, s16 x, s16 y, u16 width, u16 height) {
    outline->x = x;
    outline->y = y;
    outline->width = width;
    outline->height = height;
}

void setRectOutlineBounds(RectOutline *outline, s16 left, s16 top, s32 right, s32 bottom) {
    outline->x = left;
    outline->y = top;
    outline->width = right - left;
    outline->height = bottom - top;
}

void setRectOutlineColor(RectOutline *outline, u8 red, u8 green, u8 blue) {
    outline->red = red;
    outline->green = green;
    outline->blue = blue;
}

void setRectOutlineMode0(RectOutline *outline) {
    outline->mode = 0;
}

void hideRectOutline(RectOutline *outline) {
    outline->mode = 1;
}

void setRectOutlineMode2(RectOutline *outline) {
    outline->mode = 2;
}

void initRectOutlineTask(void) {
    setCleanupCallback(&cleanupRectOutlineTask);
    setCallback(&updateRectOutlineTask);
}

void updateRectOutlineTask(RectOutlineTask *task) {
    s32 i;
    RectOutline *outline;

    outline = task->outline;
    if (outline->mode != 1) {
        task->edges[0].x = outline->x;
        task->edges[0].y = outline->y;
        task->edges[0].width = outline->width - 1;
        task->edges[0].height = 1;
        task->edges[1].x = outline->x + outline->width - 1;
        task->edges[1].y = outline->y;
        task->edges[1].width = 1;
        task->edges[1].height = outline->height - 1;
        task->edges[2].x = outline->x;
        task->edges[2].y = outline->y + outline->height - 1;
        task->edges[2].width = outline->width - 1;
        task->edges[2].height = 1;
        task->edges[3].x = outline->x;
        task->edges[3].y = outline->y;
        task->edges[3].width = 1;
        task->edges[3].height = outline->height - 1;

        for (i = 0; i < 4; i++) {
            task->edges[i].red = outline->red;
            task->edges[i].green = outline->green;
            task->edges[i].blue = outline->blue;
            task->edges[i].mode = 0;
            pushViewportCallbackBySlot(
                task->viewport->callbackSlotIndex,
                VIEWPORT_CALLBACK_LAYER_FINAL,
                drawColorRect,
                &task->edges[i]
            );
        }
    }
}

void cleanupRectOutlineTask(void) {
}

__asm__(".text\n.align 4");

s32 unusedReturnZero(void) {
    return 0;
}
