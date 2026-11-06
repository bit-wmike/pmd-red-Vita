/*
 * Kangaskhan storage from the port menu (port/port_storage.h).
 *
 * The rules are those of Kangaskhan's storage in the town
 * (src/kangaskhan_storage1.c, src/kecleon_bros4.c, src/code_801C8C4.c):
 *   store   not Poké / a used TM (IsNotMoneyOrUsedTMItem), at most 999 of an
 *           item in the storage (sub_801ADA0: thrown items count their stack);
 *           MoveToStorage + ShiftItemsDownFrom + FillInventoryGaps (sub_80178D0)
 *   take    a free bag slot; a thrown item comes out as one stack of up to
 *           99, any other item one at a time (sub_8017A1C);
 *           teamStorage[id] -= n, AddHeldItemToInventory (sub_8017928)
 *   list    the storage sorted by GetItemOrder (sub_801CE58)
 * In addition, for the dungeon: Kecleon's unpaid goods and sticky items
 * stay in the bag (the town never sees them: leaving a dungeon clears both,
 * ClearAllItems_8091FB4 / ClearAllItemsWithFlag).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/port.h"
#include "port/port_view.h"
#include "port/port_panel.h"
#include "port/port_storage.h"

#include "global.h"
#include "constants/item.h"
#include "items.h"
#include "event_flag.h"
#include "constants/event_flag.h"
#include "structs/str_dungeon.h"
#include "structs/str_items.h"

static uint32_t sInputFrame = 0xFFFFFFFFu;

void Port_StorageInputFrame(void)
{
    sInputFrame = Port_FrameCounter();
#ifndef __vita__
    {
        static int debug = -1; /* PMD_STORAGE_DEBUG=1: log every input frame */
        if (debug < 0)
            debug = getenv("PMD_STORAGE_DEBUG") != NULL;
        if (debug)
            Platform_Log("[storage] input frame %u\n", (unsigned)sInputFrame);
    }
#endif
}

int Port_StorageSafe(void)
{
    return gTeamInventoryRef != NULL && sInputFrame == Port_FrameCounter();
}

const char *Port_StorageReason(int err)
{
    switch (err) {
        case PORT_STORAGE_OK: return "";
        case PORT_STORAGE_ERR_STATE: return "Nur m\xc3\xb6glich, wenn das Spiel auf Eingabe wartet (kein Men\xc3\xbc/Text offen)";
        case PORT_STORAGE_ERR_EMPTY: return "Kein Item";
        case PORT_STORAGE_ERR_SHOP: return "Ladenware (nicht bezahlt)";
        case PORT_STORAGE_ERR_STICKY: return "Klebrig";
        case PORT_STORAGE_ERR_MONEY: return "Kann nicht eingelagert werden";
        case PORT_STORAGE_ERR_FULL: return "Lager voll (999)";
        case PORT_STORAGE_ERR_BAGFULL: return "Beutel voll";
        case PORT_STORAGE_ERR_NOBAG: return "Hier gibt es keinen Beutel (noch kein Rettungsteam / Story-Dungeon)";
        default: return "?";
    }
}

/* The team's bag exists: a rescue team has been founded (the game's own
 * condition for the bag in dungeons, main_loops.c), and in a dungeon the
 * dungeon uses it (gDungeon->unk644.hasInventory; the menu shows "no items"
 * otherwise). */
static int HasBag(void)
{
    if (!CheckQuest(QUEST_SET_TEAM_NAME))
        return 0;
    if (Port_ViewSceneShown() == PORT_SCENE_DUNGEON && gDungeon != NULL && !gDungeon->unk644.hasInventory)
        return 0;
    return 1;
}

static int StorageKind(int id)
{
    return id > ITEM_NOTHING && id < NUMBER_OF_ITEM_IDS && IsNotMoneyOrUsedTMItem((u8)id);
}

/* Why bag slot `slot` cannot go into the storage. */
static int DepositCheck(int slot)
{
    const Item *it;
    int sum;
    if (gTeamInventoryRef == NULL)
        return PORT_STORAGE_ERR_STATE;
    if (slot < 0 || slot >= INVENTORY_SIZE)
        return PORT_STORAGE_ERR_EMPTY;
    it = &gTeamInventoryRef->teamItems[slot];
    if (!(it->flags & ITEM_FLAG_EXISTS) || it->id == ITEM_NOTHING || it->id >= NUMBER_OF_ITEM_IDS)
        return PORT_STORAGE_ERR_EMPTY;
    /* (ITEM_FLAG_UNPAID is only a cursor mark of the dungeon item menu) */
    if (it->flags & ITEM_FLAG_IN_SHOP)
        return PORT_STORAGE_ERR_SHOP;
    if (it->flags & ITEM_FLAG_STICKY)
        return PORT_STORAGE_ERR_STICKY;
    if (!IsNotMoneyOrUsedTMItem(it->id))
        return PORT_STORAGE_ERR_MONEY;
    /* sub_801ADA0: thrown items up to 999 with the stack, others up to 998 before */
    sum = gTeamInventoryRef->teamStorage[it->id];
    if (IsThrownItem(it->id) ? sum + it->quantity > 999 : sum + 1 > 999)
        return PORT_STORAGE_ERR_FULL;
    return PORT_STORAGE_OK;
}

static void EntryName(struct PortStorageEntry *e)
{
    Port_PanelItemName(e->name, sizeof(e->name), e->id);
    if (e->name[0] == 0)
        snprintf(e->name, sizeof(e->name), "Item %d", e->id);
}

void Port_StorageList(struct PortStorageView *v)
{
    int i, j, n = 0;
    memset(v, 0, sizeof(*v));
    v->valid = gTeamInventoryRef != NULL && gItemParametersData != NULL;
    v->safe = Port_StorageSafe();
    v->inDungeon = Port_ViewSceneShown() == PORT_SCENE_DUNGEON;
    if (!v->valid)
        return;
    v->noBag = !HasBag();
    for (i = 0; i < INVENTORY_SIZE; i++) {
        const Item *it = &gTeamInventoryRef->teamItems[i];
        struct PortStorageEntry *e;
        if (!(it->flags & ITEM_FLAG_EXISTS))
            continue;
        e = &v->bag[v->bagCount++];
        e->id = it->id;
        e->slot = i;
        e->flags = it->flags;
        e->set = (it->flags & ITEM_FLAG_SET) != 0;
        e->thrown = it->id < NUMBER_OF_ITEM_IDS && IsThrownItem(it->id);
        e->count = e->thrown ? it->quantity : 1;
        e->refuse = v->noBag ? PORT_STORAGE_ERR_NOBAG : DepositCheck(i);
        if (it->id == ITEM_POKE)
            snprintf(e->name, sizeof(e->name), "%d Pok\xc3\xa9", (int)GetMoneyValue((Item *)it));
        else
            EntryName(e);
    }
    for (i = 1; i < NUMBER_OF_ITEM_IDS; i++) {
        if (gTeamInventoryRef->teamStorage[i] != 0 && StorageKind(i))
            v->store[n++].id = i;
    }
    /* the game's order (sub_801CE58: exchange sort by GetItemOrder) */
    for (i = 0; i < n - 1; i++)
        for (j = i + 1; j < n; j++)
            if (GetItemOrder((u8)v->store[i].id) > GetItemOrder((u8)v->store[j].id)) {
                int t = v->store[i].id;
                v->store[i].id = v->store[j].id;
                v->store[j].id = t;
            }
    for (i = 0; i < n; i++) {
        struct PortStorageEntry *e = &v->store[i];
        e->count = gTeamInventoryRef->teamStorage[e->id];
        e->thrown = IsThrownItem((u8)e->id);
        e->slot = -1;
        e->refuse = v->noBag ? PORT_STORAGE_ERR_NOBAG
                  : GetNumberOfFilledInventorySlots() >= INVENTORY_SIZE ? PORT_STORAGE_ERR_BAGFULL : PORT_STORAGE_OK;
        EntryName(e);
    }
    v->storeCount = n;
}

static void Msg(char *msg, int size, const char *fmt, const char *name, int count, int err)
{
    if (msg == NULL || size <= 0)
        return;
    if (err != PORT_STORAGE_OK)
        snprintf(msg, size, "%s: %s", name[0] ? name : "Item", Port_StorageReason(err));
    else if (count > 1)
        snprintf(msg, (size_t)size, "%s x%d %s", name, count, fmt);
    else
        snprintf(msg, (size_t)size, "%s %s", name, fmt);
}

int Port_StorageDeposit(int slot, char *msg, int msgSize)
{
    char name[40] = "";
    Item item;
    int err, count;

    if (!Port_StorageSafe()) {
        Msg(msg, msgSize, "", "", 0, PORT_STORAGE_ERR_STATE);
        return PORT_STORAGE_ERR_STATE;
    }
    if (slot >= 0 && slot < INVENTORY_SIZE)
        Port_PanelItemName(name, sizeof(name), gTeamInventoryRef->teamItems[slot].id);
    err = HasBag() ? DepositCheck(slot) : PORT_STORAGE_ERR_NOBAG;
    if (err != PORT_STORAGE_OK) {
        Msg(msg, msgSize, "", name, 0, err);
        Platform_Log("[storage] deposit slot %d refused: %s\n", slot, Port_StorageReason(err));
        return err;
    }
    /* sub_80178D0 */
    item = gTeamInventoryRef->teamItems[slot];
    count = IsThrownItem(item.id) ? item.quantity : 1;
    MoveToStorage(&item);
    ShiftItemsDownFrom(slot);
    FillInventoryGaps();
    Msg(msg, msgSize, "eingelagert", name, count, PORT_STORAGE_OK);
    Platform_Log("[storage] deposit slot %d: item %d x%d -> storage %d\n", slot, item.id, count,
                 gTeamInventoryRef->teamStorage[item.id]);
    Port_PanelRefresh();
    return PORT_STORAGE_OK;
}

int Port_StorageWithdraw(int id, char *msg, int msgSize)
{
    char name[40] = "";
    BulkItem item;
    int stored;

    if (!Port_StorageSafe()) {
        Msg(msg, msgSize, "", "", 0, PORT_STORAGE_ERR_STATE);
        return PORT_STORAGE_ERR_STATE;
    }
    Port_PanelItemName(name, sizeof(name), id);
    if (!HasBag()) {
        Msg(msg, msgSize, "", "", 0, PORT_STORAGE_ERR_NOBAG);
        return PORT_STORAGE_ERR_NOBAG;
    }
    if (!StorageKind(id) || gTeamInventoryRef->teamStorage[id] == 0) {
        Msg(msg, msgSize, "", name, 0, PORT_STORAGE_ERR_EMPTY);
        return PORT_STORAGE_ERR_EMPTY;
    }
    if (GetNumberOfFilledInventorySlots() >= INVENTORY_SIZE) {
        Msg(msg, msgSize, "", name, 0, PORT_STORAGE_ERR_BAGFULL);
        Platform_Log("[storage] withdraw item %d refused: bag full\n", id);
        return PORT_STORAGE_ERR_BAGFULL;
    }
    /* sub_8017A1C / sub_8017928 */
    stored = gTeamInventoryRef->teamStorage[id];
    item.id = (u8)id;
    item.quantity = IsThrownItem((u8)id) ? (stored > 99 ? 99 : stored) : 1;
    gTeamInventoryRef->teamStorage[id] -= item.quantity;
    AddHeldItemToInventory(&item);
    FillInventoryGaps();
    Msg(msg, msgSize, "in den Beutel genommen", name, item.quantity, PORT_STORAGE_OK);
    Platform_Log("[storage] withdraw item %d x%d -> storage %d, bag %d/%d\n", id, item.quantity,
                 gTeamInventoryRef->teamStorage[id], (int)GetNumberOfFilledInventorySlots(), INVENTORY_SIZE);
    Port_PanelRefresh();
    return PORT_STORAGE_OK;
}

int Port_StorageText(char *buf, int size)
{
    static struct PortStorageView v;
    int n = 0, i;
#define OUT(...) (n += snprintf(buf + n, size > n ? (size_t)(size - n) : 0, __VA_ARGS__))
    Port_StorageList(&v);
    if (!v.valid) {
        OUT("[storage] no game data\n");
        return n < size ? n : size - 1;
    }
    OUT("[storage] Beutel %d/%d (%s%s%s):", v.bagCount, PORT_STORAGE_BAG, v.inDungeon ? "Dungeon" : "au\xc3\x9f" "erhalb",
        v.safe ? "" : ", Spiel nicht in der Eingabe", v.noBag ? ", kein Beutel" : "");
    for (i = 0; i < v.bagCount; i++) {
        const struct PortStorageEntry *e = &v.bag[i];
        OUT(" [%d]%s", e->slot, e->name);
        if (e->thrown)
            OUT(" x%d", e->count);
        if (e->set)
            OUT("(gesetzt)");
        if (e->refuse != PORT_STORAGE_OK)
            OUT("{%s}", Port_StorageReason(e->refuse));
    }
    OUT("\n[storage] Lager %d Sorten:", v.storeCount);
    for (i = 0; i < v.storeCount; i++)
        OUT(" %s(%d)=%d", v.store[i].name, v.store[i].id, v.store[i].count);
    OUT("\n");
#undef OUT
    return n < size ? n : size - 1;
}

#ifndef __vita__
int Port_StorageTestGive(int id, int quantity, int flags)
{
    Item item;
    if (gTeamInventoryRef == NULL || id <= ITEM_NOTHING || id >= NUMBER_OF_ITEM_IDS)
        return -1;
    if (flags < 0) {
        /* into the storage instead: the stored amount becomes `quantity` */
        gTeamInventoryRef->teamStorage[id] = (u16)quantity;
        Platform_Log("[storage] test: storage of item %d set to %d\n", id, quantity);
        return 0;
    }
    item.id = (u8)id;
    item.flags = (u8)(ITEM_FLAG_EXISTS | flags);
    item.quantity = (u8)(IsThrownItem((u8)id) ? (quantity > 0 ? quantity : 1) : 0);
    if (flags & ITEM_FLAG_SET) {
        int i;
        for (i = 0; i < INVENTORY_SIZE; i++)
            gTeamInventoryRef->teamItems[i].flags &= ~ITEM_FLAG_SET;
    }
    if (AddItemToInventory(&item))
        return -1;
    Platform_Log("[storage] test: item %d x%d flags %02x put into the bag\n", id, item.quantity, item.flags);
    return 0;
}
#endif
