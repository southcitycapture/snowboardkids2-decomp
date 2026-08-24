#include "race/ai_pathfinding.h"
#include "common.h"
#include "data/course_data.h"
#include "gamestate.h"
#include "math/geometry.h"
#include "math/rand.h"
#include "race/race_session.h"
#include "system/task_scheduler.h"

// Macro definitions
#define SEC3(gs) ((gs)->gameData.sectors)
#define SEC1(gs) ((gs)->gameData.vertices)

// AI path choice values
#define PATH_CHOICE_MAIN 0
#define PATH_CHOICE_SHORTCUT 8

// AI path flags
#define PATH_FLAG_SHORTCUT_AVAILABLE 8

// Random thresholds for AI decision making
#define SHORTCUT_SKIP_CHANCE 0xC0 // 192/256 = 75% take rate in special mode

// Maximum look-ahead distance for AI target calculation
#define AI_MAX_LOOKAHEAD_DISTANCE 0xA00000

// AI lateral offset scaling factors
#define LATERAL_OFFSET_SCALE 0x2000
#define LANE_WIDTH_MULTIPLIER 6

#define LERP_X(out, wpArr, idx, posPtr, endField, startField, f)                                           \
    (out)->x = (((((posPtr)[(wpArr)[idx].endField].x - (posPtr)[(wpArr)[idx].startField].x) * (f)) / 32) + \
                (posPtr)[(wpArr)[idx].startField].x)                                                       \
               << 16

#define LERP_Z(out, wpArr, idx, posPtr, endField, startField, f)                                           \
    (out)->z = (((((posPtr)[(wpArr)[idx].endField].z - (posPtr)[(wpArr)[idx].startField].z) * (f)) / 32) + \
                (posPtr)[(wpArr)[idx].startField].z)                                                       \
               << 16

typedef struct {
    /* 0x00 */ s8 pathChoice;
    /* 0x01 */ u8 lateralFactor;
    /* 0x02 */ u8 factor;
    /* 0x03 */ s8 pathPreference;
} AIPathPreference;

typedef struct {
    s32 dirX;
    s32 _pad1;
    s32 dirZ;
    s32 _pad2;
} StackSpillVars;

// Global variables
extern u8 gShortcutChanceByMemoryPool[];

// Function declarations
void computeAIWaypointLateralPosition(Player *, TrackData *, s16, Vec3i *);
void computeAIWaypointPosition(Player *, TrackData *, s16, Vec3i *);

void calculateAITargetPosition(Player *player) {
    Vec3i finalWaypointPos;
    Vec3i currentWaypointPos;
    Vec3i nextWaypointPos;
    Vec3i rotatedPos;
    Vec3i projectedPlayerPos;
    TrackData *trackData;
    LevelConfig *levelConfig;
    s32 *pathChoiceData;
    s32 currentSectorIndex;
    int new_var;
    s32 pathAngle;
    s32 distanceToWaypoint;
    s32 maxDistance;
    GameState *gs;
    gs = getCurrentAllocation();
    trackData = &gs->gameData;
    currentSectorIndex = player->sectorIndex;
    if (trackData->sectors[currentSectorIndex].nextSectorIndex < 0) {
        levelConfig = getLevelConfig(gs->memoryPoolId);
        player->aiTarget.x = levelConfig->liftEntryPosX;
        player->aiTarget.z = levelConfig->liftEntryPosZ;
        return;
    }
    computeAIWaypointLateralPosition(player, trackData, (s16)currentSectorIndex, &currentWaypointPos);
    computeAIWaypointPosition(player, trackData, (s16)currentSectorIndex, &nextWaypointPos);
    projectedPlayerPos.x = player->worldPos.x - currentWaypointPos.x;
    projectedPlayerPos.z = player->worldPos.z - currentWaypointPos.z;
    pathAngle =
        computeAngleToPosition(nextWaypointPos.x, nextWaypointPos.z, currentWaypointPos.x, currentWaypointPos.z);
    rotateVectorY(&projectedPlayerPos, -pathAngle, &rotatedPos);
    rotatedPos.x = 0;
    rotateVectorY(&rotatedPos, pathAngle, &projectedPlayerPos);
    projectedPlayerPos.x += currentWaypointPos.x;
    projectedPlayerPos.z += currentWaypointPos.z;
    while (1) {
        computeAIWaypointPosition(player, trackData, (s16)currentSectorIndex, &nextWaypointPos);
        new_var = nextWaypointPos.z - projectedPlayerPos.z;
        finalWaypointPos.x = nextWaypointPos.x - projectedPlayerPos.x;
        finalWaypointPos.z = new_var;
        distanceToWaypoint = distance_2d(finalWaypointPos.x, finalWaypointPos.z);
        if (distanceToWaypoint > 0xA00000) {
            maxDistance = 0xA00000;
            finalWaypointPos.x = (((s64)finalWaypointPos.x) * maxDistance) / distanceToWaypoint;
            finalWaypointPos.z = (((s64)finalWaypointPos.z) * maxDistance) / distanceToWaypoint;
            break;
        }
        if (trackData->sectors[currentSectorIndex].nextSectorIndex < 0) {
            break;
        }
        pathChoiceData = (s32 *)player->aiPathData;
        if (pathChoiceData != 0) {
            if ((*((s8 *)(&pathChoiceData[currentSectorIndex]))) == (-1)) {
                currentSectorIndex = trackData->sectors[currentSectorIndex].leftSectorIndex;
            }
            if ((*((s8 *)(&pathChoiceData[currentSectorIndex]))) == 0) {
                currentSectorIndex = trackData->sectors[currentSectorIndex].nextSectorIndex;
            }
            if ((*((s8 *)(&pathChoiceData[currentSectorIndex]))) == 1) {
                currentSectorIndex = trackData->sectors[currentSectorIndex].rightSectorIndex;
            }
        } else {
            currentSectorIndex = trackData->sectors[currentSectorIndex].nextSectorIndex;
        }
    }

    finalWaypointPos.x += projectedPlayerPos.x;
    finalWaypointPos.z += projectedPlayerPos.z;
    computeAIWaypointLateralPosition(player, trackData, (s16)currentSectorIndex, &currentWaypointPos);
    finalWaypointPos.x -= currentWaypointPos.x;
    finalWaypointPos.z -= currentWaypointPos.z;
    pathAngle =
        computeAngleToPosition(nextWaypointPos.x, nextWaypointPos.z, currentWaypointPos.x, currentWaypointPos.z);
    rotateVectorY(&finalWaypointPos, -pathAngle, &rotatedPos);
    rotatedPos.x = 0;
    rotateVectorY(&rotatedPos, pathAngle, &finalWaypointPos);
    finalWaypointPos.x += currentWaypointPos.x;
    finalWaypointPos.z += currentWaypointPos.z;
    player->aiTarget.x = finalWaypointPos.x;
    player->aiTarget.z = finalWaypointPos.z;
}

void computeAIWaypointPosition(Player *player, TrackData *trackData, s16 sectorIdx, Vec3i *result) {
    AIPathPreference *pathData;
    s16 waypointIdx;
    s8 factor;
    s32 factorRaw = 0;

    pathData = (AIPathPreference *)player->aiPathData;
    if (pathData != NULL) {
        s8 pathChoice = pathData[sectorIdx].pathChoice;
        factorRaw = pathData[sectorIdx].factor;

        switch (pathChoice) {
            case -1:
                waypointIdx = trackData->sectors[sectorIdx].leftSectorIndex;
                if ((s8)factorRaw >= 0) {
                    factor = (s8)factorRaw;
                    LERP_X(
                        result,
                        trackData->sectors,
                        waypointIdx,
                        trackData->vertices,
                        endRightVertexIndex,
                        endCenterVertexIndex,
                        factor
                    );
                    LERP_Z(
                        result,
                        trackData->sectors,
                        waypointIdx,
                        trackData->vertices,
                        endRightVertexIndex,
                        endCenterVertexIndex,
                        factor
                    );
                } else {
                    factor = (s8)(-factorRaw);
                    LERP_X(
                        result,
                        trackData->sectors,
                        waypointIdx,
                        trackData->vertices,
                        endLeftVertexIndex,
                        endCenterVertexIndex,
                        factor
                    );
                    LERP_Z(
                        result,
                        trackData->sectors,
                        waypointIdx,
                        trackData->vertices,
                        endLeftVertexIndex,
                        endCenterVertexIndex,
                        factor
                    );
                }
                break;
            case 0:
                if ((s8)factorRaw >= 0) {
                    factor = (s8)factorRaw;
                    LERP_X(
                        result,
                        trackData->sectors,
                        sectorIdx,
                        trackData->vertices,
                        startRightVertexIndex,
                        startCenterVertexIndex,
                        factor
                    );
                    LERP_Z(
                        result,
                        trackData->sectors,
                        sectorIdx,
                        trackData->vertices,
                        startRightVertexIndex,
                        startCenterVertexIndex,
                        factor
                    );
                } else {
                    factor = (s8)(-factorRaw);
                    LERP_X(
                        result,
                        trackData->sectors,
                        sectorIdx,
                        trackData->vertices,
                        startLeftVertexIndex,
                        startCenterVertexIndex,
                        factor
                    );
                    LERP_Z(
                        result,
                        trackData->sectors,
                        sectorIdx,
                        trackData->vertices,
                        startLeftVertexIndex,
                        startCenterVertexIndex,
                        factor
                    );
                }
                break;
            case 1:
                waypointIdx = trackData->sectors[sectorIdx].rightSectorIndex;
                if ((s8)factorRaw >= 0) {
                    factor = (s8)factorRaw;
                    LERP_X(
                        result,
                        trackData->sectors,
                        waypointIdx,
                        trackData->vertices,
                        endRightVertexIndex,
                        endCenterVertexIndex,
                        factor
                    );
                    LERP_Z(
                        result,
                        trackData->sectors,
                        waypointIdx,
                        trackData->vertices,
                        endRightVertexIndex,
                        endCenterVertexIndex,
                        factor
                    );
                } else {
                    factor = (s8)(-factorRaw);
                    LERP_X(
                        result,
                        trackData->sectors,
                        waypointIdx,
                        trackData->vertices,
                        endLeftVertexIndex,
                        endCenterVertexIndex,
                        factor
                    );
                    LERP_Z(
                        result,
                        trackData->sectors,
                        waypointIdx,
                        trackData->vertices,
                        endLeftVertexIndex,
                        endCenterVertexIndex,
                        factor
                    );
                }
                break;
            default:
                return;
        }
    } else {
        if (player->playerIndex == 1) {
            factorRaw = 3;
        }
        if (player->playerIndex == 2) {
            factorRaw = -10;
        }
        if (player->playerIndex == 3) {
            factorRaw = 10;
        }

        if ((s8)factorRaw >= 0) {
            factor = (s8)factorRaw;
            LERP_X(
                result,
                trackData->sectors,
                sectorIdx,
                trackData->vertices,
                startRightVertexIndex,
                startCenterVertexIndex,
                factor
            );
            LERP_Z(
                result,
                trackData->sectors,
                sectorIdx,
                trackData->vertices,
                startRightVertexIndex,
                startCenterVertexIndex,
                factor
            );
        } else {
            factor = (s8)(-factorRaw);
            LERP_X(
                result,
                trackData->sectors,
                sectorIdx,
                trackData->vertices,
                startLeftVertexIndex,
                startCenterVertexIndex,
                factor
            );
            LERP_Z(
                result,
                trackData->sectors,
                sectorIdx,
                trackData->vertices,
                startLeftVertexIndex,
                startCenterVertexIndex,
                factor
            );
        }
    }
}

void computeAIWaypointLateralPosition(Player *player, TrackData *trackData, s16 sectorIdx, Vec3i *result) {
    AIPathPreference *pathData;
    s8 factor;
    s32 factorRaw = 0;

    pathData = (AIPathPreference *)player->aiPathData;
    if (pathData != NULL) {
        factorRaw = pathData[sectorIdx].lateralFactor;
    } else {
        if (player->playerIndex == 1) {
            factorRaw = 3;
        }
        if (player->playerIndex == 2) {
            factorRaw = -10;
        }
        if (player->playerIndex == 3) {
            factorRaw = 10;
        }
    }

    if ((s8)factorRaw >= 0) {
        factor = (s8)factorRaw;
        LERP_X(
            result,
            trackData->sectors,
            sectorIdx,
            trackData->vertices,
            endRightVertexIndex,
            endCenterVertexIndex,
            factor
        );
        LERP_Z(
            result,
            trackData->sectors,
            sectorIdx,
            trackData->vertices,
            endRightVertexIndex,
            endCenterVertexIndex,
            factor
        );
    } else {
        factor = (s8)(-factorRaw);
        LERP_X(
            result,
            trackData->sectors,
            sectorIdx,
            trackData->vertices,
            endLeftVertexIndex,
            endCenterVertexIndex,
            factor
        );
        LERP_Z(
            result,
            trackData->sectors,
            sectorIdx,
            trackData->vertices,
            endLeftVertexIndex,
            endCenterVertexIndex,
            factor
        );
    }
}

s8 determineAIPathChoice(Player *player) {
    GameState *gs;
    volatile StackSpillVars spill;
    s32 trackDirX;
    s32 trackDirZ;
    s32 trackLengthSq;
    s32 trackLength;
    s32 normalizedDirX;
    s32 normalizedDirZ;
    s32 playerToStartX;
    s32 playerToStartZ;
    s64 lateralDistance;

    gs = getCurrentAllocation();

    if (player->aiPathData != NULL &&
        ((AIPathPreference *)player->aiPathData)[player->sectorIndex].pathPreference != 0) {
        trackDirX = SEC1(gs)[SEC3(gs)[player->sectorIndex].startLeftVertexIndex].x -
                    SEC1(gs)[SEC3(gs)[player->sectorIndex].startRightVertexIndex].x;
        spill.dirX = trackDirX;

        trackLengthSq = trackDirX * trackDirX;
        trackDirZ = SEC1(gs)[SEC3(gs)[player->sectorIndex].startLeftVertexIndex].z -
                    SEC1(gs)[SEC3(gs)[player->sectorIndex].startRightVertexIndex].z;
        spill.dirZ = trackDirZ;
        trackLengthSq += trackDirZ * trackDirZ;

        trackLength = isqrt64(trackLengthSq);
        normalizedDirX = (spill.dirX << 13) / trackLength;
        normalizedDirZ = (spill.dirZ << 13) / trackLength;

        playerToStartX = player->worldPos.x - (SEC1(gs)[SEC3(gs)[player->sectorIndex].startLeftVertexIndex].x << 16);
        spill.dirX = playerToStartX;

        playerToStartZ = player->worldPos.z - (SEC1(gs)[SEC3(gs)[player->sectorIndex].startLeftVertexIndex].z << 16);
        spill.dirZ = playerToStartZ;

        // Perpendicular distance from track center line
        lateralDistance =
            ((s64)(-((s16)normalizedDirZ)) * playerToStartX) + ((s64)((s16)normalizedDirX) * playerToStartZ);
        trackLength = -((s32)(lateralDistance / LATERAL_OFFSET_SCALE));

        if (trackLength < (player->smoothedSpeedCap * LANE_WIDTH_MULTIPLIER)) {
            return ((AIPathPreference *)player->aiPathData)[player->sectorIndex].pathPreference;
        }
    }

    // Reset shortcut choice if no shortcut available
    if (!(player->aiPathFlags & PATH_FLAG_SHORTCUT_AVAILABLE)) {
        player->aiShortcutDecisionMade = 0;
    }

    // Special mode: unk86 is set (possibly time attack or special mode)
    if (gs->unk86 != 0) {
        if (player->aiShortcutDecisionMade == 0 && (player->aiPathFlags & PATH_FLAG_SHORTCUT_AVAILABLE)) {
            // 25% chance to skip shortcut
            if ((randA() & 0xFF) >= SHORTCUT_SKIP_CHANCE) {
                return PATH_CHOICE_MAIN;
            }
            player->aiShortcutDecisionMade = 1;
            return PATH_CHOICE_SHORTCUT;
        }
        return PATH_CHOICE_MAIN;
    }

    // Normal race mode (not race type 9)
    if (gs->raceType != RACE_TYPE_TRAINING) {
        if (player->aiShortcutDecisionMade == 0 && (player->aiPathFlags & PATH_FLAG_SHORTCUT_AVAILABLE)) {
            // Check random shortcut chance based on memory pool
            if ((randA() & 0xFF) < gShortcutChanceByMemoryPool[gs->memoryPoolId]) {
                player->aiShortcutDecisionMade = 1;
                return PATH_CHOICE_SHORTCUT;
            }
            // Boss characters (ID >= 6) always take shortcuts
            if (player->characterId >= 6) {
                player->aiShortcutDecisionMade = 1;
                return PATH_CHOICE_SHORTCUT;
            }
        }
    }

    return PATH_CHOICE_MAIN;
}
