/*
 * Cheats of the PMD port, applied once per frame to the game's structures.
 * (Filled in once the game runs on the host; see port_game.c.)
 */
#include "port/port.h"
#include "port/port_config.h"

int Port_InGameplay(void)
{
    return 1;
}

void Port_ApplyCheats(void)
{
}
