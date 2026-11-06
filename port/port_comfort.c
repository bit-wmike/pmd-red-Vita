/* Comfort options of the PMD port; see port_comfort.h. */
#include "port/port.h"
#include "port/port_config.h"
#include "port/port_game.h"
#include "port/port_comfort.h"
#include "constants/ground_map.h"

int Port_TextInstant(void)
{
    return gPortConfig.textInstant != 0;
}

int Port_TextAutoFrames(void)
{
    return gPortConfig.textAuto ? 20 : 0;
}

int Port_BlinkMask(void)
{
    return gPortConfig.slowBlink ? 16 : 8;
}

int Port_WalkFast(void)
{
    return gPortConfig.walkFast != 0;
}

/* Call-site ids of DungeonRunFrameActions that wait for the player, run a
 * menu, print a text box, show the message log or play a cutscene. These
 * keep their normal pace; everything else (walking, attacks, items, orbs,
 * traps, fades, camera pans) is an animation that may run faster. */
static int IsPacedId(unsigned id)
{
    switch (id) {
        case 0x00: /* message log scrolling */
        case 0x09: /* dungeon text box */
        case 0x0B:
        case 0x0D: /* message log, recruit menu */
        case 0x0E: /* name entry */
        case 0x0F: /* main input loop */
        case 0x14: case 0x16: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
        case 0x23: /* held dpad */
        case 0x24: case 0x2D: case 0x2E:
        case 0x2F: /* map view */
        case 0x35: /* message log before the walking phase */
        case 0x37: case 0x3D: case 0x3E: case 0x45:
        case 0x46: /* cutscenes, recruit scene */
        case 0x47:
        case 0x4F: /* message log at the floor's end */
        case 0x54: /* R held */
        case 0x61:
            return 1;
        default:
            return 0;
    }
}

int Port_DungeonSkipFrameWait(unsigned id)
{
    static unsigned phase;
    int speed = gPortConfig.dungeonSpeed;

    if (speed <= 0 || IsPacedId(id)) {
        phase = 0;
        return 0;
    }
    phase++;
    if (speed == 1)
        return (phase % 3) == 0;   /* x1.5: three logic frames per two shown */
    return (phase & 1) != 0;       /* x2: two logic frames per shown frame */
}

static int sFloorSavePending;

static int sLastGroundMap = -1;

void Port_FloorStarted(void)
{
    sFloorSavePending = gPortConfig.floorAutosave != 0;
    sLastGroundMap = -1;   /* returning from the dungeon enters the base anew */
}

void Port_FloorInputFrame(void)
{
    if (!sFloorSavePending)
        return;
    sFloorSavePending = 0;
    if (!gPortConfig.floorAutosave)
        return;
    Platform_Log("[qsave] floor autosave\n");
    Port_RequestQuickSave();
}

/* Ground maps of the team base: outside and inside, one set of six maps
 * per partner species (include/constants/ground_map.h). */
#define BASE_MAP_FIRST MAP_TEAM_BASE_PIKACHU_BASIC
#define BASE_MAP_LAST MAP_TEAM_BASE_INSIDE_TREECKO_FINAL

static int IsBaseMap(int id)
{
    return id >= BASE_MAP_FIRST && id <= BASE_MAP_LAST;
}

void Port_GroundInputFrame(int groundMapId)
{
    int entered = IsBaseMap(groundMapId) && !IsBaseMap(sLastGroundMap);
    sLastGroundMap = groundMapId;
    if (!entered || !gPortConfig.baseAutosave)
        return;
    Platform_Log("[qsave] base autosave (map %d)\n", groundMapId);
    Port_RequestQuickSave();
}

int Port_MsgFast(void)
{
    return gPortConfig.msgFast != 0;
}

int Port_MsgWaitThreshold(void)
{
    return gPortConfig.msgFast ? 210 : 180;
}
