/*
 * Cheats of the PMD port: only ones that ease the mechanics without breaking
 * the game (experience/money/IQ multipliers, no hunger, recruitment bonus,
 * stairs on the floor map, team stat modifiers). They are applied where the
 * game computes the values (src/dungeon_leveling.c, src/dungeon_turn_effects.c,
 * src/dungeon_items.c, src/dungeon_ai_items.c, src/mission_reward.c,
 * src/dungeon_item_action.c, src/items.c, src/dungeon_mon_recruit.c,
 * src/dungeon_map.c, src/dungeon_damage.c, src/dungeon_logic.c).
 * Every helper returns its input unchanged at the default settings.
 *
 * Host: PMD_CHEAT_DEBUG=1 logs every hook that changes a value.
 */
#include <stdlib.h>
#include "port/port.h"
#include "port/port_config.h"
#include "port/port_view.h"
#include "port/port_cheats.h"

#ifndef __vita__
static int DebugOn(void)
{
    static int on = -1;
    if (on < 0)
        on = getenv("PMD_CHEAT_DEBUG") != NULL;
    return on;
}
#define CHEAT_LOG(...) do { if (DebugOn()) Platform_Log(__VA_ARGS__); } while (0)
#else
#define CHEAT_LOG(...) do { } while (0)
#endif

static int Clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int Port_InGameplay(void)
{
    return Port_ViewScene() != PORT_SCENE_NONE;
}

int Port_ExpMultiplier(void)
{
    return Clamp(gPortConfig.expMultiplier, 1, 4);
}

int Port_NoHunger(void)
{
    return gPortConfig.noHunger;
}

int Port_CheatMoney(int amount)
{
    int m = Clamp(gPortConfig.moneyMult, 1, 4);
    if (m == 1 || amount <= 0)
        return amount;
    CHEAT_LOG("cheat: money %d -> %d (x%d)\n", amount, amount * m, m);
    return amount * m;
}

int Port_CheatIq(int boost)
{
    int m = Clamp(gPortConfig.iqMult, 1, 4);
    if (m == 1 || boost <= 0)
        return boost;
    CHEAT_LOG("cheat: IQ gain %d -> %d (x%d)\n", boost, boost * m, m);
    return boost * m;
}

int Port_CheatRecruitRate(int rate, int rand)
{
    int bonus = 0;
    if (gPortConfig.recruitBoost == 1)
        bonus = 200;   /* +20 percentage points (rate is in 1/1000) */
    else if (gPortConfig.recruitBoost >= 2)
        bonus = 500;
    if (bonus == 0)
        return rate;
    {
        int boosted = rate + bonus;
        if (boosted > 1000)
            boosted = 1000;   /* never above 100 % */
        CHEAT_LOG("cheat: recruit rate %d -> %d /1000, roll %d -> %s\n", rate, boosted, rand,
                  rand < boosted ? "recruited" : "no");
        return boosted;
    }
}

int Port_StairsMarker(void)
{
    return gPortConfig.stairsMarker != 0;
}

void Port_CheatStairsDrawn(int x, int y, int stairSpotter)
{
    if (!stairSpotter)
        CHEAT_LOG("cheat: stairs marker drawn on the floor map at %d,%d\n", x, y);
    (void)x; (void)y;
}

static int StatSetting(int which)
{
    switch (which) {
    case PORT_STAT_ATK: return gPortConfig.statAtk;
    case PORT_STAT_DEF: return gPortConfig.statDef;
    case PORT_STAT_SPATK: return gPortConfig.statSpAtk;
    case PORT_STAT_SPDEF: return gPortConfig.statSpDef;
    }
    return 0;
}

int Port_CheatStat(int which, int value)
{
    static const char *const names[4] = {"atk", "def", "spatk", "spdef"};
    int step = Clamp(StatSetting(which), -3, 3);
    int scaled;
    if (step == 0 || value <= 0)
        return value;
    scaled = (value * (10 + step) + 5) / 10;
    CHEAT_LOG("cheat: team %s %d -> %d (%+d0 %%)\n", names[which & 3], value, scaled, step);
    return scaled;
}

/*
 * Speed: the game has no fractional speed, only speed stages (0 = every
 * second turn, 1 = normal, 2 = two actions per turn, ...). Each step of
 * stat_speed therefore gives a team member one extra speed stage on 10 % of
 * its turns (-10 %: one stage less on 20 % of the turns, since a turn at
 * stage 0 only loses half an action). A turn is four fractional ticks of
 * gDungeon->unk644.fractionalTurn; the ticks are counted here so that the
 * bonus is constant within a turn (the game compares CalcSpeedStage results
 * within one turn to print "speed rose/fell" messages).
 */
int Port_CheatSpeedStage(int fractionalTurn)
{
    static int lastTick = -1;
    static unsigned tick;
    int step = Clamp(gPortConfig.statSpeed, -3, 3);
    unsigned turn, share, bonus;

    if (step == 0)
        return 0;
    if (fractionalTurn != lastTick) {
        if (lastTick >= 0)
            tick += (unsigned)((fractionalTurn - lastTick + 24) % 24);
        else
            tick = (unsigned)fractionalTurn;   /* aligned with the game's turn windows */
        lastTick = fractionalTurn;
    }
    turn = tick / 4;
    share = step > 0 ? (unsigned)step : (unsigned)(-step) * 2;
    /* turns where floor((turn+1)*share/10) > floor(turn*share/10): share/10 of all turns */
    bonus = ((turn + 1) * share) / 10 != (turn * share) / 10;
    if (!bonus)
        return 0;
    CHEAT_LOG("cheat: speed stage %+d on turn %u\n", step > 0 ? 1 : -1, turn);
    return step > 0 ? 1 : -1;
}

void Port_ApplyCheats(void)
{
}
