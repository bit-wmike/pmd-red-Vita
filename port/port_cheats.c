/*
 * Cheats of the PMD port: only ones that ease the mechanics without breaking
 * the game (experience multiplier, no hunger). They are applied where the
 * game computes the values (src/dungeon_leveling.c, src/dungeon_turn_effects.c).
 */
#include "port/port.h"
#include "port/port_config.h"
#include "port/port_view.h"

int Port_InGameplay(void)
{
    return Port_ViewScene() != PORT_SCENE_NONE;
}

int Port_ExpMultiplier(void)
{
    int m = gPortConfig.expMultiplier;
    return m < 1 ? 1 : (m > 4 ? 4 : m);
}

int Port_NoHunger(void)
{
    return gPortConfig.noHunger;
}

void Port_ApplyCheats(void)
{
}
