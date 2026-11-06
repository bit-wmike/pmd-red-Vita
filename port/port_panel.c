/*
 * Panels beside the game image: clock, play time, money, the dungeon's
 * name, floor, belly and weather, the floor bar with the floors of accepted
 * jobs, and the data of the job overview (port/port_panel.h).
 *
 * Sources in the game:
 *   jobs      gUnknown_203B490->jobSlots[8] (code_80958E8.c, the job list of
 *             the Pelipper post office): mailType 6 = taken ("Aktiv" in the
 *             job list), 5 = "Inaktiv" (jobs from the bulletin board start so),
 *             8 = done in this dungeon run (sub_80843BC, dungeon_jobs.c),
 *             9 = done and the run ended well (sub_8084424), 7 = done
 *             (friend rescue); missionType = WONDER_MAIL_MISSION_TYPE_*;
 *             find/deliver item jobs check the team's bag like sub_8096AF8
 *   dungeon   gDungeon->unk644.dungeonLocation (id, floor),
 *             GetDungeonFloorCount() - 1 floors, IsStairDirectionUp(),
 *             gDungeon->weather.weather, the leader in gDungeon->teamPokemon
 *   money     gTeamInventoryRef->teamMoney, play time gPlayTimeRef
 * Names: GetDungeonName1 / GetMonSpecies / the item table, through
 * Port_Text (European languages), converted from the game's character set
 * to UTF-8. Nothing here writes game state.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "port/port.h"
#include "port/port_view.h"
#include "port/port_config.h"
#include "port/port_text.h"
#include "port/port_panel.h"

#include "global.h"
#include "code_80958E8.h"
#include "constants/mailbox.h"
#include "constants/wonder_mail.h"
#include "constants/weather.h"
#include "constants/item.h"
#include "constants/dungeon.h"
#include "dungeon_info.h"
#include "dungeon_util.h"
#include "items.h"
#include "number_util.h"
#include "play_time.h"
#include "pokemon.h"
#include "structs/str_dungeon.h"
#include "structs/dungeon_entity.h"
#include "structs/str_items.h"

int gPortPanelsVisible;

static struct PortPanel sPanel;
static int sEnabled;

void Port_PanelEnable(int on)
{
    sEnabled = on;
}

const struct PortPanel *Port_PanelGet(void)
{
    return &sPanel;
}

/* ---- time ------------------------------------------------------------- */

void Port_PanelTimeOfDay(int *hour, int *minute)
{
    static uint32_t lastFrame = 0xFFFFFFFFu;
    static int h, m;
    uint32_t frame = Port_FrameCounter();

    if (lastFrame == 0xFFFFFFFFu || frame - lastFrame >= 60) {
        time_t now;
        struct tm *t;
#ifndef __vita__
        /* host tests: a fixed time keeps the frames reproducible */
        static const char *fixed;
        static int fixedChecked;
        if (!fixedChecked) {
            fixed = getenv("PMD_CLOCK");
            fixedChecked = 1;
        }
        if (fixed != NULL && sscanf(fixed, "%d:%d", &h, &m) == 2) {
            lastFrame = frame;
            *hour = h;
            *minute = m;
            return;
        }
#endif
        now = time(NULL);
        t = localtime(&now);
        if (t != NULL) {
            h = t->tm_hour;
            m = t->tm_min;
        }
        lastFrame = frame;
    }
    *hour = h;
    *minute = m;
}

/* ---- text ------------------------------------------------------------- */

static int PutUtf8(char *out, int pos, int size, unsigned cp)
{
    if (cp < 0x80) {
        if (pos + 1 >= size)
            return pos;
        out[pos++] = (char)cp;
    } else if (cp < 0x800) {
        if (pos + 2 >= size)
            return pos;
        out[pos++] = (char)(0xC0 | (cp >> 6));
        out[pos++] = (char)(0x80 | (cp & 0x3F));
    } else {
        if (pos + 3 >= size)
            return pos;
        out[pos++] = (char)(0xE0 | (cp >> 12));
        out[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[pos++] = (char)(0x80 | (cp & 0x3F));
    }
    return pos;
}

static int HexDigit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A game string (charmap.txt: Latin-1 with ~XX escapes, #C?/#R colour and
 * other # macros, two-byte icons from 0x81) as UTF-8 on one line. */
static void GameToUtf8(char *out, int size, const unsigned char *s)
{
    int pos = 0;
    if (size <= 0)
        return;
    if (s == NULL) {
        out[0] = 0;
        return;
    }
    s = Port_Text(s);
    while (*s && pos < size - 1) {
        unsigned char c = *s;
        if (c == '#') {
            unsigned char k = s[1];
            if (k == 0)
                break;
            if (k == 'C' || k == 'c')
                s += s[2] ? 3 : 2; /* colour + colour code */
            else if (k == 'n') {
                pos = PutUtf8(out, pos, size, ' ');
                s += 2;
            } else
                s += 2;
            continue;
        }
        if (c == '~' && HexDigit(s[1]) >= 0 && HexDigit(s[2]) >= 0) {
            pos = PutUtf8(out, pos, size, (unsigned)(HexDigit(s[1]) * 16 + HexDigit(s[2])));
            s += 3;
            continue;
        }
        if (c == '\n' || c == '\r') {
            pos = PutUtf8(out, pos, size, ' ');
            s++;
            continue;
        }
        if (c >= 0x81 && c <= 0x87 && s[1]) {
            /* two-byte icon; 81 89 / 81 8A are the gender signs */
            if (c == 0x81 && s[1] == 0x89)
                pos = PutUtf8(out, pos, size, 0x2642);
            else if (c == 0x81 && s[1] == 0x8A)
                pos = PutUtf8(out, pos, size, 0x2640);
            s += 2;
            continue;
        }
        if (c == 0xBD)
            pos = PutUtf8(out, pos, size, 0x2642); /* ♂ */
        else if (c == 0xBE)
            pos = PutUtf8(out, pos, size, 0x2640); /* ♀ */
        else if (c >= 0x20)
            pos = PutUtf8(out, pos, size, c); /* Latin-1 */
        s++;
    }
    out[pos] = 0;
}

static void MonName(char *out, int size, int species)
{
    if (species <= 0 || species >= MONSTER_MAX) {
        out[0] = 0;
        return;
    }
    GameToUtf8(out, size, (const unsigned char *)GetMonSpecies((s16)species));
}

static void ItemName(char *out, int size, int item)
{
    if (item <= 0 || item >= NUMBER_OF_ITEM_IDS || gItemParametersData == NULL) {
        out[0] = 0;
        return;
    }
    GameToUtf8(out, size, gItemParametersData[item].name);
}

void Port_PanelItemName(char *out, int size, int item)
{
    ItemName(out, size, item);
}

static void DungeonName(char *out, int size, int dungeon)
{
    if (dungeon < 0 || dungeon >= NUM_DUNGEONS) {
        out[0] = 0;
        return;
    }
    GameToUtf8(out, size, GetDungeonName1((u8)dungeon));
}

/* The floor as the European ROM writes it in each language (the block
 * "N -%d / P. -%d / E. -%d / U%d E%d / B%dF %dF" next to pksdir0). */
static void FloorText(char *out, int size, int dungeon, int floor)
{
    int up = (dungeon >= 0 && dungeon < NUM_DUNGEONS) ? IsStairDirectionUp((u8)dungeon) : 0;
    switch (Port_TextMultilingual() ? gPortConfig.language : PORT_LANG_EN) {
        case PORT_LANG_DE: snprintf(out, size, up ? "E%d" : "U%d", floor); break;
        case PORT_LANG_FR: snprintf(out, size, up ? "E. %d" : "E. -%d", floor); break;
        case PORT_LANG_IT: snprintf(out, size, up ? "P. %d" : "P. -%d", floor); break;
        case PORT_LANG_ES: snprintf(out, size, up ? "N %d" : "N -%d", floor); break;
        default: snprintf(out, size, up ? "%dF" : "B%dF", floor); break;
    }
}

/* ---- data ------------------------------------------------------------- */

/* The item in the team's bag. For a delivery the game skips shop items
 * and set sticky ones (sub_8084448); a found item only has to exist
 * (sub_8096AF8). */
static int ItemInBag(int item, int forDelivery)
{
    int i;
    if (gTeamInventoryRef == NULL || item <= 0)
        return 0;
    for (i = 0; i < INVENTORY_SIZE; i++) {
        const Item *it = &gTeamInventoryRef->teamItems[i];
        if (!(it->flags & ITEM_FLAG_EXISTS) || it->id != item)
            continue;
        if (forDelivery && ((it->flags & ITEM_FLAG_IN_SHOP) || ((it->flags & ITEM_FLAG_STICKY) && (it->flags & ITEM_FLAG_SET))))
            continue;
        return 1;
    }
    return 0;
}

/* The leader without GetLeader(): that one caches into gLeaderPointer. */
static EntityInfo *Leader(void)
{
    int i;
    for (i = 0; i < MAX_TEAM_MEMBERS; i++) {
        Entity *e = gDungeon->teamPokemon[i];
        if (e != NULL && EntityIsValid(e) && GetEntInfo(e)->isTeamLeader)
            return GetEntInfo(e);
    }
    return NULL;
}

/* The item set for throwing (L+R) and the leader's held item: in a dungeon
 * the leader's entity, elsewhere the leader in the recruited list. */
static void FillItems(struct PortPanel *p)
{
    int i;
    if (gTeamInventoryRef != NULL) {
        for (i = 0; i < INVENTORY_SIZE; i++) {
            const Item *it = &gTeamInventoryRef->teamItems[i];
            if ((it->flags & ITEM_FLAG_EXISTS) && (it->flags & ITEM_FLAG_SET) && it->id != ITEM_NOTHING &&
                it->id < NUMBER_OF_ITEM_IDS) {
                p->setItem = it->id;
                p->setSticky = (it->flags & ITEM_FLAG_STICKY) != 0;
                break;
            }
        }
        if (p->setItem) {
            int thrown = IsThrownItem((u8)p->setItem);
            for (i = 0; i < INVENTORY_SIZE; i++) {
                const Item *it = &gTeamInventoryRef->teamItems[i];
                if ((it->flags & ITEM_FLAG_EXISTS) && it->id == p->setItem)
                    p->setCount += thrown ? it->quantity : 1;
            }
            ItemName(p->setName, sizeof(p->setName), p->setItem);
        }
    }
    if (p->inDungeon) {
        EntityInfo *leader = Leader();
        if (leader != NULL && (leader->heldItem.flags & ITEM_FLAG_EXISTS) && leader->heldItem.id != ITEM_NOTHING &&
            leader->heldItem.id < NUMBER_OF_ITEM_IDS) {
            p->heldItem = leader->heldItem.id;
            p->heldSticky = (leader->heldItem.flags & ITEM_FLAG_STICKY) != 0;
            if (IsThrownItem(leader->heldItem.id))
                p->heldCount = leader->heldItem.quantity;
        }
    } else if (gRecruitedPokemonRef != NULL) {
        Pokemon *leader = GetLeaderMon1();
        if (leader != NULL && leader->heldItem.id != ITEM_NOTHING && leader->heldItem.id < NUMBER_OF_ITEM_IDS) {
            p->heldItem = leader->heldItem.id;
            if (IsThrownItem(leader->heldItem.id))
                p->heldCount = leader->heldItem.quantity;
        }
    }
    if (p->heldItem)
        ItemName(p->heldName, sizeof(p->heldName), p->heldItem);
}

static void FillJob(struct PortPanelJob *j, const WonderMail *m, int curDungeon)
{
    memset(j, 0, sizeof(*j));
    j->kind = m->missionType <= PORT_JOB_DELIVER_ITEM ? m->missionType : PORT_JOB_RESCUE_CLIENT;
    j->dungeon = m->dungeonSeed.location.id;
    j->floor = m->dungeonSeed.location.floor;
    j->here = curDungeon >= 0 && j->dungeon == curDungeon;
    j->special = m->unk2 != 0;
    /* a found item counts on any floor (the game's dungeon entry list
     * leaves the floor out too): no floor for those */
    if (j->kind == PORT_JOB_FIND_ITEM)
        j->floor = 0;
    else
        FloorText(j->floorText, sizeof(j->floorText), j->dungeon, j->floor);
    DungeonName(j->dungeonName, sizeof(j->dungeonName), j->dungeon);
    MonName(j->client, sizeof(j->client), m->clientSpecies);
    MonName(j->target, sizeof(j->target), m->targetSpecies);
    if (j->kind == PORT_JOB_FIND_ITEM || j->kind == PORT_JOB_DELIVER_ITEM)
        ItemName(j->item, sizeof(j->item), m->targetItem);

    switch (j->kind) {
        case PORT_JOB_RESCUE_CLIENT: /* the game names targetSpecies (= the client) */
        case PORT_JOB_RESCUE_TARGET:
            snprintf(j->what, sizeof(j->what), "%s retten", j->target[0] ? j->target : j->client);
            break;
        case PORT_JOB_ESCORT:
            snprintf(j->what, sizeof(j->what), "%s zu %s begleiten", j->client, j->target);
            break;
        case PORT_JOB_FIND_ITEM:
            snprintf(j->what, sizeof(j->what), "%s finden", j->item);
            break;
        case PORT_JOB_DELIVER_ITEM:
            snprintf(j->what, sizeof(j->what), "%s zu %s bringen", j->item, j->target[0] ? j->target : j->client);
            break;
    }

    switch (m->mailType) {
        case MAIL_TYPE_SUSPENDED_JOB:
            j->status = PORT_JOBST_SUSPENDED;
            snprintf(j->statusText, sizeof(j->statusText), "inaktiv (Jobliste: Aktiv)");
            break;
        case MAIL_TYPE_UNK8:
            j->status = PORT_JOBST_DONE;
            snprintf(j->statusText, sizeof(j->statusText), "erf\xc3\xbcllt");
            break;
        case MAIL_TYPE_UNK7:
        case MAIL_TYPE_UNK9:
            j->status = PORT_JOBST_REWARD;
            snprintf(j->statusText, sizeof(j->statusText), "erledigt - Belohnung in der Basis");
            break;
        default: /* MAIL_TYPE_TAKEN_JOB */
            j->status = PORT_JOBST_OPEN;
            if (j->kind == PORT_JOB_FIND_ITEM && ItemInBag(m->targetItem, 0)) {
                j->status = PORT_JOBST_ITEM;
                snprintf(j->statusText, sizeof(j->statusText), "Item im Beutel - Dungeon verlassen");
            } else if (j->kind == PORT_JOB_DELIVER_ITEM) {
                if (ItemInBag(m->targetItem, 1)) {
                    snprintf(j->statusText, sizeof(j->statusText), "offen - Item im Beutel");
                } else {
                    snprintf(j->statusText, sizeof(j->statusText), "offen - Item fehlt");
                }
            } else {
                snprintf(j->statusText, sizeof(j->statusText), "offen");
            }
            break;
    }
}

#ifndef __vita__
static void FakeFloors(struct PortPanel *p);
#endif

static void Fill(void)
{
    struct PortPanel *p = &sPanel;
    int hour, minute, i, curDungeon = -1;
    uint32_t serial = p->serial + 1;

    memset(p, 0, sizeof(*p));
    p->serial = serial;
    Port_PanelTimeOfDay(&hour, &minute);
    snprintf(p->clock, sizeof(p->clock), "%02d:%02d", hour % 100, minute % 100);
    if (gPlayTimeRef != NULL) {
        int h = gPlayTimeRef->hours < 0 ? 0 : gPlayTimeRef->hours;
        snprintf(p->playTime, sizeof(p->playTime), "%d:%02d:%02d", h % 10000, gPlayTimeRef->minutes % 60,
                 gPlayTimeRef->seconds % 60);
    }
    p->money = gTeamInventoryRef != NULL ? gTeamInventoryRef->teamMoney : 0;
    p->bagCount = gTeamInventoryRef != NULL ? GetNumberOfFilledInventorySlots() : 0;
    p->bagMax = INVENTORY_SIZE;

    if (gDungeon != NULL && Port_ViewSceneShown() == PORT_SCENE_DUNGEON) {
        static const char *const weather[WEATHER_COUNT] = {
            "", "Sonnig", "Sandsturm", "Bew\xc3\xb6lkt", "Regen", "Hagel", "Nebel", "Schnee",
        };
        const Dungeon *d = gDungeon;
        EntityInfo *leader;
        p->inDungeon = 1;
        p->dungeon = curDungeon = d->unk644.dungeonLocation.id;
        p->floor = d->unk644.dungeonLocation.floor;
        p->stairsUp = curDungeon < NUM_DUNGEONS ? IsStairDirectionUp((u8)curDungeon) : 0;
        p->floorCount = curDungeon < NUM_DUNGEONS ? GetDungeonFloorCount((u8)curDungeon) - 1 : 0;
        if (p->floorCount < p->floor)
            p->floorCount = p->floor;
        if (p->floorCount > PORT_PANEL_FLOORS)
            p->floorCount = PORT_PANEL_FLOORS;
        DungeonName(p->dungeonName, sizeof(p->dungeonName), curDungeon);
        FloorText(p->floorText, sizeof(p->floorText), curDungeon, p->floor);
        if (d->weather.weather < WEATHER_COUNT)
            snprintf(p->weather, sizeof(p->weather), "%s", weather[d->weather.weather]);
        leader = Leader();
        if (leader != NULL) {
            p->belly = FixedPointToInt(leader->belly);
            p->maxBelly = FixedPointToInt(leader->maxBelly);
            p->hp = leader->HP;
            p->maxHp = leader->maxHPStat;
        }
    }

    FillItems(p);

    if (gUnknown_203B490 != NULL) {
        for (i = 0; i < MAX_ACCEPTED_JOBS && p->jobCount < PORT_PANEL_JOBS; i++) {
            const WonderMail *m = &gUnknown_203B490->jobSlots[i];
            struct PortPanelJob *j;
            if (m->mailType < MAIL_TYPE_SUSPENDED_JOB || m->mailType > MAIL_TYPE_UNK9)
                continue;
            j = &p->jobs[p->jobCount++];
            FillJob(j, m, curDungeon);
            if (j->here && j->status == PORT_JOBST_SUSPENDED) {
                /* a hint: the job would count here once it is taken */
                p->floorMarks[j->floor <= PORT_PANEL_FLOORS ? j->floor : PORT_PANEL_FLOORS] |= PORT_FLOOR_INACTIVE;
            } else if (j->here) {
                int open = j->status == PORT_JOBST_OPEN;
                p->jobsHere++;
                if (j->floor == 0) {
                    /* find the item: the whole dungeon */
                    p->floorMarks[0] |= open ? PORT_FLOOR_OPEN : PORT_FLOOR_DONE;
                } else {
                    int f = j->floor > PORT_PANEL_FLOORS ? PORT_PANEL_FLOORS : j->floor;
                    p->floorMarks[f] |= open ? PORT_FLOOR_OPEN : PORT_FLOOR_DONE;
                    if (f > p->floorCount)
                        p->floorCount = f;
                }
            }
        }
    }
#ifndef __vita__
    FakeFloors(p);
#endif
}

#ifndef __vita__
/* Host test only: PMD_PANEL_FAKEFLOORS=floor:count[:stairsUp] pretends the
 * dungeon has `count` floors (up to 99, deep dungeons) and the team is on
 * `floor`; open jobs on floors 3 and count-4, done on count/2 - for checking
 * the floor bar, not the game (the panel data only, nothing is written). */
static void FakeFloors(struct PortPanel *p)
{
    static int checked, floor, count, up;
    if (!checked) {
        const char *e = getenv("PMD_PANEL_FAKEFLOORS");
        checked = 1;
        if (e == NULL || sscanf(e, "%d:%d:%d", &floor, &count, &up) < 2)
            count = 0;
    }
    if (count <= 0 || !p->inDungeon)
        return;
    if (count > PORT_PANEL_FLOORS)
        count = PORT_PANEL_FLOORS;
    p->floor = floor;
    p->floorCount = count;
    p->stairsUp = up;
    FloorText(p->floorText, sizeof(p->floorText), -1, floor);
    p->floorMarks[3 <= count ? 3 : count] |= PORT_FLOOR_OPEN;
    p->floorMarks[count > 4 ? count - 4 : count] |= PORT_FLOOR_OPEN;
    p->floorMarks[count / 2 > 0 ? count / 2 : 1] |= PORT_FLOOR_DONE;
}
#endif

void Port_PanelRefresh(void)
{
    Fill();
}

#ifndef __vita__
/* Host test only: PMD_PANEL_TESTJOBS=frame writes six sample jobs into the
 * job list at that frame (this changes the game state - for checking the
 * panels, not the game). */
static void TestJobs(void)
{
    static int at = -2;
    static const struct {
        u8 type, mission, dungeon, floor;
        s16 client, target;
        u8 item;
    } jobs[] = {
        {MAIL_TYPE_UNK9, WONDER_MAIL_MISSION_TYPE_RESCUE_CLIENT, DUNGEON_TINY_WOODS, 1, MONSTER_METAPOD, MONSTER_METAPOD, 0},
        {MAIL_TYPE_SUSPENDED_JOB, WONDER_MAIL_MISSION_TYPE_RESCUE_CLIENT, DUNGEON_TINY_WOODS, 2, MONSTER_CATERPIE, MONSTER_CATERPIE, 0},
        {MAIL_TYPE_TAKEN_JOB, WONDER_MAIL_MISSION_TYPE_DELIVER_ITEM, DUNGEON_TINY_WOODS, 3, MONSTER_PIDGEY, MONSTER_PIDGEY, ITEM_ORAN_BERRY},
        {MAIL_TYPE_TAKEN_JOB, WONDER_MAIL_MISSION_TYPE_FIND_ITEM, DUNGEON_TINY_WOODS, 2, MONSTER_PIDGEY, MONSTER_PIDGEY, ITEM_APPLE},
        {MAIL_TYPE_TAKEN_JOB, WONDER_MAIL_MISSION_TYPE_ESCORT_CLIENT, DUNGEON_THUNDERWAVE_CAVE, 4, MONSTER_CATERPIE, MONSTER_MAGNEMITE, 0},
        {MAIL_TYPE_SUSPENDED_JOB, WONDER_MAIL_MISSION_TYPE_RESCUE_TARGET, DUNGEON_MT_STEEL, 6, MONSTER_PIDGEY, MONSTER_SKARMORY, 0},
    };
    unsigned i;
    if (at == -2)
        at = getenv("PMD_PANEL_TESTJOBS") ? atoi(getenv("PMD_PANEL_TESTJOBS")) : -1;
    if (at < 0 || (int)Port_FrameCounter() != at || gUnknown_203B490 == NULL)
        return;
    for (i = 0; i < sizeof(jobs) / sizeof(jobs[0]) && i < MAX_ACCEPTED_JOBS; i++) {
        WonderMail *m = &gUnknown_203B490->jobSlots[i];
        memset(m, 0, sizeof(*m));
        m->mailType = jobs[i].type;
        m->missionType = jobs[i].mission;
        m->dungeonSeed.location.id = jobs[i].dungeon;
        m->dungeonSeed.location.floor = jobs[i].floor;
        m->clientSpecies = jobs[i].client;
        m->targetSpecies = jobs[i].target;
        m->targetItem = jobs[i].item;
    }
    Platform_Log("[panel] test jobs written at frame %d\n", at);
}
#endif

void Port_PanelOnVBlank(void)
{
#ifndef __vita__
    TestJobs();
#endif
    /* four times a second is plenty for a clock and a status line */
    if (sEnabled && Port_FrameCounter() % 15 == 0)
        Fill();
}

/* ---- text form (host log) --------------------------------------------- */

static const char *const sKindName[] = {"Kunde retten", "Ziel retten", "begleiten", "Item finden", "Item bringen"};
/* the words follow the German game: "Jobliste", "Aktiv"/"Inaktiv" */

int Port_PanelText(char *buf, int size)
{
    const struct PortPanel *p = &sPanel;
    int n = 0, i, f;
#define OUT(...) (n += snprintf(buf + n, size > n ? (size_t)(size - n) : 0, __VA_ARGS__))
    OUT("[panel] %s  Spielzeit %s  Geld %d P  Beutel %d/%d\n", p->clock, p->playTime, p->money, p->bagCount, p->bagMax);
    if (p->inDungeon) {
        OUT("[panel] Dungeon %d \"%s\" %s (Etage %d von %d)  Bauch %d/%d  KP %d/%d  Wetter %s\n", p->dungeon,
            p->dungeonName, p->floorText, p->floor, p->floorCount, p->belly, p->maxBelly, p->hp, p->maxHp,
            p->weather[0] ? p->weather : "-");
        OUT("[panel] Etagen%s:", p->stairsUp ? " (aufw\xc3\xa4rts)" : "");
        for (f = 1; f <= p->floorCount; f++)
            OUT(" %d%s%s%s%s", f, f == p->floor ? "*" : "", (p->floorMarks[f] & PORT_FLOOR_OPEN) ? "!" : "",
                (p->floorMarks[f] & PORT_FLOOR_DONE) ? "+" : "", (p->floorMarks[f] & PORT_FLOOR_INACTIVE) ? "?" : "");
        OUT("%s\n", (p->floorMarks[0] & PORT_FLOOR_OPEN) ? "  Item!" : (p->floorMarks[0] & PORT_FLOOR_DONE) ? "  Item+" :
            (p->floorMarks[0] & PORT_FLOOR_INACTIVE) ? "  Item?" : "");
    }
    OUT("[panel] Werfen: %s", p->setItem ? p->setName : "-");
    if (p->setItem)
        OUT(" x%d%s", p->setCount, p->setSticky ? " (klebrig)" : "");
    OUT("  Gehalten: %s", p->heldItem ? p->heldName : "-");
    if (p->heldCount)
        OUT(" x%d", p->heldCount);
    OUT("%s\n", p->heldSticky ? " (klebrig)" : "");
    OUT("[panel] Jobs: %d (hier aktiv %d)\n", p->jobCount, p->jobsHere);
    for (i = 0; i < p->jobCount; i++) {
        const struct PortPanelJob *j = &p->jobs[i];
        OUT("[panel]  %s%s %s: %s [%s] Kunde %s, Ziel %s, Item %s | %s%s\n", j->here ? "*" : " ", j->dungeonName,
            j->floorText, j->what, sKindName[j->kind], j->client[0] ? j->client : "-", j->target[0] ? j->target : "-",
            j->item[0] ? j->item : "-", j->statusText, j->special ? " (Sonderauftrag)" : "");
    }
#undef OUT
    return n < size ? n : size - 1;
}
