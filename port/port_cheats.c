/*
 * Cheats of the PMD port, applied once per frame to the game's structures.
 */
#include "port/port.h"
#include "port/port_config.h"
#include "port/port_view.h"

#include "global.h"
#include "run_dungeon.h"
#include "dungeon_range.h"
#include "dungeon_util.h"
#include "items.h"
#include "structs/str_dungeon.h"
#include "structs/dungeon_entity.h"
#include "structs/str_items.h"

int Port_InGameplay(void)
{
    return Port_ViewScene() != PORT_SCENE_NONE;
}

void Port_ApplyCheats(void)
{
    if (Port_ViewScene() == PORT_SCENE_DUNGEON && gDungeon != NULL) {
        Entity *leader = GetLeader();
        if (leader != NULL && EntityIsValid(leader)) {
            EntityInfo *info = GetEntInfo(leader);
            if (gPortConfig.cheatHp && info->HP < info->maxHPStat)
                info->HP = info->maxHPStat;
            if (gPortConfig.cheatBelly && (info->belly.unk0 < info->maxBelly.unk0 || info->belly.unk2 < 0)) {
                info->belly = info->maxBelly;
                info->bellyEmpty = FALSE;
            }
        }
    }
    if (gPortConfig.cheatMoney && gTeamInventoryRef != NULL && gTeamInventoryRef->teamMoney < 99999)
        gTeamInventoryRef->teamMoney = 99999;
}
