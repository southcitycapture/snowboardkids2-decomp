#pragma once

#include "common.h"
#include "data/course_data.h"

#define COURSE_DEFINITION_ABI_VERSION 1

typedef enum {
    COURSE_ASSET_NONE,
    COURSE_ASSET_UNCOMPRESSED,
    COURSE_ASSET_SNO,
} CourseAssetCompression;

typedef enum {
    COURSE_KIND_STANDARD,
    COURSE_KIND_BOSS,
    COURSE_KIND_SPEED_CROSS,
    COURSE_KIND_SHOT_CROSS,
    COURSE_KIND_X_CROSS,
    COURSE_KIND_TRAINING,
} CourseKind;

typedef struct {
    const void *romStart;
    const void *romEnd;
    s32 decompressedSize;
    CourseAssetCompression compression;
} CourseAssetRef;

typedef struct {
    CourseAssetRef displayLists;
    CourseAssetRef modelResources;
    CourseAssetRef trackMesh;
    CourseAssetRef textureTable;
    CourseAssetRef goldCoins;
    CourseAssetRef itemBoxes;
    CourseAssetRef sceneAnimation;
} CourseAssetBundle;

typedef struct {
    const DisplayLists *skyDisplayLists;
    const DisplayLists *fogDisplayLists;
    const LevelDisplayLists *displayListTable;
} CourseRenderDefinition;

typedef struct {
    u8 world;
    u16 duration;
    u8 startWaypoint;
} CoursePreviewDefinition;

typedef struct {
    u8 snowboardId;
    u8 colorSlot;
    u8 cpuDifficulty1P;
    u8 cpuDifficultyMP;
} CourseCpuSnowboardDefinition;

typedef struct {
    u8 cpuCharacters[4];
    u8 cpuBoardModel;
    u8 expertSnowboard;
    s32 rewards[3];
    CourseCpuSnowboardDefinition cpuSnowboards[6];
} CourseRaceDefinition;

/*
 * Logical recomp-facing view. The matching build deliberately does not
 * instantiate this structure; it continues to use the original projected
 * arrays so their symbols and ROM layout remain unchanged.
 */
typedef struct {
    u32 abiVersion;
    u32 structSize;
    const char *key;
    s32 legacyId;
    CourseKind kind;
    const char *behavior;
    CourseAssetBundle assets;
    LevelConfig environment;
    CourseRenderDefinition render;
    CoursePreviewDefinition preview;
    CourseRaceDefinition race;
    const char *overlay;
    u32 flags;
    u32 reserved[4];
} CourseDefinition;

/* Defined by the recomp-only source emitted by generate_course_definitions.py. */
extern const CourseDefinition gBuiltinCourseDefinitions[];
extern const s32 gBuiltinCourseDefinitionCount;
