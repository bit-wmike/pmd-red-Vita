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

#endif
