/* Kangaskhan storage from the port menu (port/port_storage.c): the team's
 * bag and Kangaskhan's storage as lists, and moving items between them by
 * the rules of the storage in the town (src/kangaskhan_storage1.c).
 *
 * Platform independent. Changes to the game state only between two game
 * frames while the game is halted (from Platform_PollKeys, like the port
 * menu), and only when Port_StorageSafe() says that the game waits for the
 * player with no menu or text box of its own open. */
#ifndef PORT_STORAGE_H
#define PORT_STORAGE_H

#define PORT_STORAGE_BAG 20     /* INVENTORY_SIZE */
#define PORT_STORAGE_KINDS 240  /* NUMBER_OF_ITEM_IDS */

/* why an item cannot move (PortStorageEntry.refuse, return values) */
enum {
    PORT_STORAGE_OK = 0,
    PORT_STORAGE_ERR_STATE = -1,   /* no game data / the game is not waiting for the player */
    PORT_STORAGE_ERR_EMPTY = -2,   /* no item there */
    PORT_STORAGE_ERR_SHOP = -3,    /* Kecleon's goods, not paid */
    PORT_STORAGE_ERR_STICKY = -4,  /* sticky: the game lets it do nothing */
    PORT_STORAGE_ERR_MONEY = -5,   /* Poké or a used TM: the storage takes neither */
    PORT_STORAGE_ERR_FULL = -6,    /* the storage holds 999 of this item */
    PORT_STORAGE_ERR_BAGFULL = -7, /* the bag has no free slot */
    PORT_STORAGE_ERR_NOBAG = -8    /* no rescue team yet / a dungeon without the team's bag (the first one) */
};

struct PortStorageEntry {
    int id;         /* item id */
    int count;      /* bag: stack size of a thrown item, 1 otherwise; storage: the stored amount */
    int thrown;     /* a thrown item (stacks) */
    int slot;       /* bag slot (bag entries) */
    int flags;      /* bag: Item.flags (ITEM_FLAG_*) */
    int set;        /* bag: the item set for throwing (L+R) */
    int refuse;     /* PORT_STORAGE_OK or why it cannot move */
    char name[40];  /* UTF-8 (Port_Text: the European ROM's language) */
};

struct PortStorageView {
    int valid;      /* game data present */
    int safe;       /* Port_StorageSafe() */
    int noBag;      /* PORT_STORAGE_ERR_NOBAG applies: nothing can move */
    int inDungeon;
    int bagCount;
    struct PortStorageEntry bag[PORT_STORAGE_BAG];
    int storeCount; /* kinds of items in the storage, in the game's storage order */
    struct PortStorageEntry store[PORT_STORAGE_KINDS];
};

/* Game code (PORT hooks): the game waits for the player's input with no
 * menu of its own open - the dungeon's main input loop, a ground frame in
 * which the player can walk. */
void Port_StorageInputFrame(void);
/* 1 when the hook ran in the current frame (call from Platform_PollKeys). */
int Port_StorageSafe(void);

/* Fills the lists. */
void Port_StorageList(struct PortStorageView *v);
/* Moves the item of bag slot `slot` into the storage (the whole stack).
 * Returns PORT_STORAGE_OK or an error; msg gets a German line for the user. */
int Port_StorageDeposit(int slot, char *msg, int msgSize);
/* Takes item `id` out of the storage into the bag: one item, a thrown item
 * as one stack of up to 99. */
int Port_StorageWithdraw(int id, char *msg, int msgSize);
/* The reason as German text ("" for PORT_STORAGE_OK). */
const char *Port_StorageReason(int err);
/* Bag and storage as text (host log); returns the length. */
int Port_StorageText(char *buf, int size);

#ifndef __vita__
/* Host tests only (PMD_STORAGE_TEST give): puts an item into the bag with
 * the given Item.flags bits besides EXISTS (e.g. 0x10 set, 0x08 sticky,
 * 0x02 shop); flags < 0: sets the stored amount of the item instead.
 * Returns 0 or -1 when the bag is full. */
int Port_StorageTestGive(int id, int quantity, int flags);
#endif

#endif /* PORT_STORAGE_H */
