#include "data/course_data.h"
#include "assets.h"
#include "common.h"
#include "data/asset_metadata.h"
#include "generated/course_asset_sizes.h"
#include "system/rom_loader.h"
#include "system/task_scheduler.h"

USE_ASSET(SUNNY_MOUNTAIN_COURSE_DISPLAY_LISTS);
USE_ASSET(SUNNY_MOUNTAIN_COURSE_MODEL_RESOURCES);

LevelConfig gLevelConfigs[] = {
#include "generated/course_definitions/level_configs.inc"
};

CompressedAssetMeta gCourseDataAssets[] = {
#include "generated/course_definitions/track_mesh_assets.inc"
};

CompressedAssetMeta gSpriteAssets[] = {
#include "generated/course_definitions/texture_table_assets.inc"
};

AssetMeta gUncompressedAssets[] = {
#include "generated/course_definitions/display_list_assets.inc"
};

CompressedAssetMeta gCompressedSegment2Assets[] = {
#include "generated/course_definitions/model_resource_assets.inc"
};

s32 gSkyDisplayLists1[] = {
#include "generated/course_definitions/sky_display_lists_1.inc"
};

s32 gSkyDisplayLists2[] = {
#include "generated/course_definitions/sky_display_lists_2.inc"
};

LevelDisplayLists *gSkyDisplayLists3[] = {
#include "generated/course_definitions/sky_display_lists_3.inc"
};

LevelConfig *getLevelConfig(s32 index) {
    return &gLevelConfigs[index];
}

void *loadCourseDataByIndex(s32 index) {
    return loadCompressedData(
        gCourseDataAssets[index].start,
        gCourseDataAssets[index].end,
        gCourseDataAssets[index].uncompressedSize
    );
}

void *loadSpriteAssetByIndex(s32 index) {
    return loadCompressedData(
        gSpriteAssets[index].start,
        gSpriteAssets[index].end,
        gSpriteAssets[index].uncompressedSize
    );
}

void *loadUncompressedAssetByIndex(s32 index) {
    return loadUncompressedData(gUncompressedAssets[index].start, gUncompressedAssets[index].end);
}

void *loadCompressedSegment2AssetByIndex(s32 index) {
    return loadCompressedData(
        gCompressedSegment2Assets[index].start,
        gCompressedSegment2Assets[index].end,
        gCompressedSegment2Assets[index].uncompressedSize
    );
}

s32 getSkyDisplayLists1ByIndex(s32 arg0) {
    return gSkyDisplayLists1[arg0];
}

s32 getSkyDisplayLists2ByIndex(s32 arg0) {
    return gSkyDisplayLists2[arg0];
}

LevelDisplayLists *getSkyDisplayLists3ByIndex(s32 index) {
    return gSkyDisplayLists3[index];
}

void *loadAsset_B7E70(void) {
    return loadUncompressedData(
        &PARTICLE_EFFECT_UNCOMPRESSED_GRAPHICS_ROM_START,
        &PARTICLE_EFFECT_UNCOMPRESSED_GRAPHICS_ROM_END
    );
}

void *loadAsset_216290(void) {
    return loadCompressedData(
        &PARTICLE_EFFECT_COMPRESSED_GRAPHICS_ROM_START,
        &PARTICLE_EFFECT_COMPRESSED_GRAPHICS_ROM_END,
        0x5740
    );
}

void *loadAsset_34CB50(void) {
    return loadCompressedData(&hudSpriteAsset_ROM_START, &hudSpriteAsset_ROM_END, 0x5E28);
}

void *loadShootCrossSprites(void) {
    return loadCompressedData(&shootCrossSprites_ROM_START, &shootCrossSprites_ROM_END, 0xE08);
}

void *loadSpeedCrossSprites(void) {
    return loadCompressedData(&speedCrossSprites_ROM_START, &speedCrossSprites_ROM_END, 0x868);
}

void *loadTrickCrossSprites(void) {
    return loadCompressedData(&trickCrossSprites_ROM_START, &trickCrossSprites_ROM_END, 0xA88);
}

void *loadAsset_34F7E0(void) {
    return loadCompressedData(&UI_MENUS_GRAPHICS_ROM_START, &UI_MENUS_GRAPHICS_ROM_END, 0x438);
}
