#pragma once

#include "common.h"
#include "data/asset_metadata.h"
#include "graphics/displaylist.h"
#include "graphics/graphics.h"
#include "math/geometry.h"

typedef struct {
    /* 0x0 */ s32 liftEntryPosX;
    /* 0x4 */ s32 liftEntryPosZ;
    /* 0x8 */ u16 liftEntryYawOffset;
    /* 0xA */ s16 padding;
    /* 0xC */ Vec3i courseStartPos;
    /* 0x18 */ DirectionalLightData lightColors;
    /* 0x20 */ EnvironmentColorData environmentColors;
    /* 0x28 */ s16 musicTrack;
    /* 0x2A */ u8 padding2[0x2];
} LevelConfig;

typedef struct {
    u8 padding[0x40];
    DisplayLists finalLapDisplayLists;
    u8 padding2[0x50 - 0x10];
    DisplayLists sceneryDisplayLists1;
    DisplayLists sceneryDisplayLists2;
    DisplayLists sceneryDisplayLists3;
    DisplayLists sceneryDisplayLists4;
} LevelDisplayLists;

extern DisplayLists gSunnyMountainSkyDisplayLists[];
extern DisplayLists gSunnyMountainFogDisplayLists[];
extern DisplayLists gSunnyMountainDisplayListTable[];
extern DisplayLists gSunnyMountainScrollingSceneryDisplayLists[];
extern DisplayLists gTurtleIslandSkyDisplayLists[];
extern DisplayLists gTurtleIslandFogDisplayLists[];
extern DisplayLists gTurtleIslandDefaultFogDisplayLists[];
extern DisplayLists gTurtleIslandScrollingSceneryDisplayLists1[];
extern DisplayLists gTurtleIslandScrollingSceneryDisplayLists2[];
extern DisplayLists gTurtleIslandScrollingSceneryDisplayLists3[];
extern DisplayLists gTurtleIslandScrollingSceneryDisplayLists4[];
extern DisplayLists gTurtleIslandDisplayListTable[];
extern DisplayLists gJingleTownSkyDisplayLists[];
extern DisplayLists gJingleTownFogDisplayLists[];
extern DisplayLists gJingleTownScrollingSceneryDisplayLists1[];
extern DisplayLists gJingleTownScrollingSceneryDisplayLists2[];
extern DisplayLists gJingleTownDisplayListTable[];
extern DisplayLists gJingleTownBossSkyDisplayLists[];
extern DisplayLists gJingleTownBossFogDisplayLists[];
extern DisplayLists gJingleTownBossScrollingSceneryDisplayLists1[];
extern DisplayLists gJingleTownBossScrollingSceneryDisplayLists2[];
extern DisplayLists gJingleTownBossDisplayListTable[];
extern DisplayLists gWendysHouseSkyDisplayLists[];
extern DisplayLists gWendysHouseFogDisplayLists[];
extern DisplayLists gWendysHouseScrollingSceneryDisplayLists[];
extern DisplayLists gWendysHouseDisplayListTable[];
extern DisplayLists gLindasCastleSkyDisplayLists[];
extern DisplayLists gLindasCastleFogDisplayLists[];
extern DisplayLists gLindasCastleDisplayListTable[];
extern DisplayLists gCrazyJungleSkyDisplayLists[];
extern DisplayLists gCrazyJungleFogDisplayLists[];
extern DisplayLists gCrazyJungleScrollingSceneryDisplayLists1[];
extern DisplayLists gCrazyJungleScrollingSceneryDisplayLists2[];
extern DisplayLists gCrazyJungleScrollingSceneryDisplayLists3[];
extern DisplayLists gCrazyJungleDisplayListTable[];
extern DisplayLists gCrazyJungleBossSkyDisplayLists[];
extern DisplayLists gCrazyJungleBossFogDisplayLists[];
extern DisplayLists gCrazyJungleBossScrollingSceneryDisplayLists1[];
extern DisplayLists gCrazyJungleBossScrollingSceneryDisplayLists2[];
extern DisplayLists gCrazyJungleBossScrollingSceneryDisplayLists3[];
extern DisplayLists gCrazyJungleBossDisplayListTable[];
extern DisplayLists gStarlightHighwaySkyDisplayLists[];
extern DisplayLists gStarlightHighwayFogDisplayLists[];
extern DisplayLists gStarlightHighwayScrollingSceneryDisplayLists1[];
extern DisplayLists gStarlightHighwayScrollingSceneryDisplayLists2[];
extern DisplayLists gStarlightHighwayScrollingSceneryDisplayLists3[];
extern DisplayLists gStarlightHighwayDisplayListTable[];
extern DisplayLists gHauntedHouseSkyDisplayLists[];
extern DisplayLists gHauntedHouseFogDisplayLists[];
extern DisplayLists gHauntedHouseDisplayListTable[];
extern DisplayLists gIceLandSkyDisplayLists[];
extern DisplayLists gIceLandFogDisplayLists[];
extern DisplayLists gIceLandDisplayListTable[];
extern DisplayLists gIceLandBossSkyDisplayLists[];
extern DisplayLists gIceLandBossFogDisplayLists[];
extern DisplayLists gIceLandBossDisplayListTable[];
extern DisplayLists gSnowboardStreetSpeedCrossSkyDisplayLists[];
extern DisplayLists gSnowboardStreetSpeedCrossFogDisplayLists[];
extern DisplayLists gSnowboardStreetSpeedCrossScrollingSceneryDisplayLists[];
extern DisplayLists gSnowboardStreetSpeedCrossDisplayListTable[];
extern DisplayLists gSnowboardStreetShotCrossSkyDisplayLists[];
extern DisplayLists gSnowboardStreetShotCrossFogDisplayLists[];
extern DisplayLists gSnowboardStreetShotCrossScrollingSceneryDisplayLists[];
extern DisplayLists gSnowboardStreetShotCrossDisplayListTable[];
extern DisplayLists gXCrossSkyDisplayLists[];
extern DisplayLists gXCrossFogDisplayLists[];
extern DisplayLists gXCrossDisplayListTable[];

DisplayLists *getSkyDisplayListsForCourse(s32 courseId);
DisplayLists *getFogDisplayListsForCourse(s32 courseId);
LevelDisplayLists *getDisplayListTableForCourse(s32 courseId);

void *loadCourseDataByIndex(s32 index);
void *loadSpriteAssetByIndex(s32 index);
void *loadCompressedSegment2AssetByIndex(s32 index);
void *loadUncompressedAssetByIndex(s32 index);
LevelConfig *getLevelConfig(s32 index);

void *loadAsset_B7E70(void);
void *loadAsset_216290(void);
void *loadAsset_34CB50(void);
void *loadShootCrossSprites(void);
void *loadSpeedCrossSprites(void);
void *loadTrickCrossSprites(void);
void *loadAsset_34F7E0(void);
