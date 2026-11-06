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
#include "global.h"
#include "structs/str_dungeon.h"
#include "dungeon_map.h"
#include "dungeon_map_access.h"
#include "expose_trap.h"
#include "dungeon_util.h"

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

int Port_CheatRankPts(int points)
{
    int m = Clamp(gPortConfig.rankMult, 1, 4);
    if (m == 1 || points <= 0)
        return points;
    CHEAT_LOG("cheat: rescue points %d -> %d (x%d)\n", points, points * m, m);
    return points * m;
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

/* ---------------------------------------------------------- 0.1.20 */

int Port_CheatBellySlow(unsigned decrement)
{
    if (!gPortConfig.bellySlow)
        return 0;
    CHEAT_LOG("cheat: belly decrement halved (%u/65536 -> %u/65536)\n", decrement, decrement >> 1);
    return 1;
}

int Port_CheatPpFree(int teamIndex, int moveIndex, int pp)
{
    /* one parity bit per team slot and move: uses 1, 3, 5 ... cost PP,
     * uses 2, 4, 6 ... are free */
    static unsigned char parity[8][4];
    unsigned char *bit;
    if (!gPortConfig.ppSaver)
        return 0;
    bit = &parity[teamIndex & 7][moveIndex & 3];
    *bit ^= 1;
    if (*bit)
        return 0;
    CHEAT_LOG("cheat: team member %d move %d used without PP (PP %d)\n", teamIndex, moveIndex, pp);
    return 1;
}

int Port_CheatBuyPrice(int price)
{
    int half;
    if (!gPortConfig.shopBuyHalf || price <= 1)
        return price;
    half = (price + 1) / 2;
    CHEAT_LOG("cheat: buy price %d -> %d\n", price, half);
    return half;
}

int Port_CheatSellPrice(int price)
{
    if (!gPortConfig.shopSellDouble || price <= 0)
        return price;
    CHEAT_LOG("cheat: sell price %d -> %d\n", price, price * 2);
    return price * 2;
}

void Port_CheatFloorPrepared(void)
{
    s32 x, y;

    if (gPortConfig.trapsVisible) {
        int n = 0, all = 0;
        for (y = 0; y < DUNGEON_MAX_SIZE_Y; y++)
            for (x = 0; x < DUNGEON_MAX_SIZE_X; x++) {
                const Tile *tile = GetTile(x, y);
                if (tile->object != NULL && GetEntityType(tile->object) == ENTITY_TRAP)
                    all++;
                n += ExposeTrap(x, y) ? 1 : 0;
            }
        CHEAT_LOG("cheat: %d of %d traps exposed\n", n, all);
    }
    if (gPortConfig.floorReveal) {
        /* the map part of HandleLuminousOrbAction (src/dungeon_tilemap.c):
         * every tile REVEALED, so the floor map shows the whole layout; the
         * orb's allTilesRevealed (which also lights up dark floors and
         * widens the sight checks) stays off. The floor start draws the
         * map afterwards (UpdateMinimap in src/run_dungeon.c). */
        for (y = 0; y < DUNGEON_MAX_SIZE_Y; y++)
            for (x = 0; x < DUNGEON_MAX_SIZE_X; x++)
                GetTileMut(x, y)->spawnOrVisibilityFlags.visibility |= VISIBILITY_FLAG_REVEALED;
        CHEAT_LOG("cheat: floor map revealed\n");
    }
}

/* map_overlay --------------------------------------------------------
 *
 * The game draws the floor map into BG0 (palette 14, tilemap rows 1..16,
 * src/dungeon_map.c), above the status line in row 0. On the open field
 * BG0 becomes a first blend target from scanline 8 on (below the status
 * line, which stays opaque): the map lines are mixed with the field at the
 * game's own BLDALPHA (8/8 in dungeons). The register is changed at the
 * HBlank before line 8 (port/host_dma.c) and the game writes its own value
 * again at the next VBlank, so line 0 and every other scene are untouched. */

/* REG_BLDCNT of the dungeon (sBldCntTable[1], src/cpu.c): BG1 (darkness)
 * blended over BG2/BG3/OBJ/backdrop. */
#define DUNGEON_BLDCNT (BLDCNT_TGT1_BG1 | BLDCNT_EFFECT_BLEND | BLDCNT_TGT2_BG2 | BLDCNT_TGT2_BG3 | \
                        BLDCNT_TGT2_OBJ | BLDCNT_TGT2_BD)
#define MAP_FIRST_LINE 8

static int sMapOnField;
static uint32_t sMapFrame;

int Port_MapOverlay(void)
{
    return gPortConfig.mapOverlay != 0;
}

void Port_MapOverlayFrame(int mapOnField)
{
    sMapOnField = mapOnField && gPortConfig.mapOverlay;
    sMapFrame = Port_FrameCounter();
}

void Port_MapOverlayHBlank(void)
{
    /* called at the HBlank after line REG_VCOUNT; the dungeon reports every
     * shown frame, a stale report (other scene) does nothing */
    if (!sMapOnField || Port_FrameCounter() - sMapFrame > 2u)
        return;
    if (REG_VCOUNT + 1 != MAP_FIRST_LINE || REG_BLDCNT != DUNGEON_BLDCNT)
        return;
    REG_BLDCNT = DUNGEON_BLDCNT | BLDCNT_TGT1_BG0 | BLDCNT_TGT2_BG1;
}

void Port_ApplyCheats(void)
{
}
