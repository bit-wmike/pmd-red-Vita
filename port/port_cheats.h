/* Soft cheats of the PMD port (port/port_cheats.c): every function is a
 * no-op at the default settings (multipliers 1, everything else 0). */
#ifndef PORT_CHEATS_H
#define PORT_CHEATS_H

enum {
    PORT_STAT_ATK = 0,
    PORT_STAT_DEF = 1,
    PORT_STAT_SPATK = 2,
    PORT_STAT_SPDEF = 3
};

int Port_ExpMultiplier(void);
int Port_NoHunger(void);

/* Money found in dungeons / mission rewards (money_multiplier 1..4). */
int Port_CheatMoney(int amount);
/* IQ gained from a gummi (iq_multiplier 1..4); the caller caps at 999. */
int Port_CheatIq(int boost);
/* Recruitment rate in 1/1000 after the game's own bonuses (recruit_boost). */
int Port_CheatRecruitRate(int rate, int rand);
/* 1 = the stairs are drawn on the floor map as with Stair Spotter. */
int Port_StairsMarker(void);
/* Debug (host): an unknown stairs tile was drawn on the floor map. */
void Port_CheatStairsDrawn(int x, int y, int stairSpotter);
/* Scales a team member's effective stat by (1 + 0.1 * stat_xxx). */
int Port_CheatStat(int which, int value);
/* Speed stage bonus (-1/0/+1) for a team member on this turn (stat_speed). */
int Port_CheatSpeedStage(int fractionalTurn);

/* 1 = the leader's belly goes down at half the rate (belly_slow); the
 * caller halves the decrement (low word of the 48.16 value, for the log). */
int Port_CheatBellySlow(unsigned decrement);
/* 1 = this use of move moveIndex by team member teamIndex costs no PP
 * (pp_saver: every second use of each move is free). */
int Port_CheatPpFree(int teamIndex, int moveIndex, int pp);
/* Shop prices (shop_buy_half / shop_sell_double): every buy/sell price the
 * shops show or charge passes through these (src/items.c). */
int Port_CheatBuyPrice(int price);
int Port_CheatSellPrice(int price);
/* A new dungeon floor was generated or loaded: traps_visible exposes all
 * traps, floor_reveal reveals the floor map (the map part of the Luminous
 * Orb; dark floors stay dark)
 * (src/run_dungeon.c, before the game updates the trap tiles). */
void Port_CheatFloorPrepared(void);

/* map_overlay: the floor map is shown even when the game's map option is
 * Off, and while it is shown on the open field (no message log, no text
 * box) it is alpha-blended over the field below the status line.
 * Port_MapOverlayFrame is called by every shown dungeon frame with the
 * game's state, Port_MapOverlayHBlank by the HBlank emulation. */
int Port_MapOverlay(void);
void Port_MapOverlayFrame(int mapOnField);
void Port_MapOverlayHBlank(void);

#endif
