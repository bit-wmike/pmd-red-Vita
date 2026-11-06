/* Panels beside the game image (port/port_panel.c): clock, play time,
 * dungeon status, the floor bar of the current dungeon with the floors of
 * accepted jobs, and the job overview.
 *
 * Platform independent: the frame loop fills the data at VBlank (game
 * thread, game state consistent), the platform draws it (the Vita in the
 * screen's margins, the host as text in the log). All strings are UTF-8,
 * game names come from Port_Text (the European ROM's languages). */
#ifndef PORT_PANEL_H
#define PORT_PANEL_H

#include <stdint.h>

#define PORT_PANEL_JOBS 8      /* MAX_ACCEPTED_JOBS */
#define PORT_PANEL_FLOORS 100  /* the longest dungeons have 99 floors */

enum {
    PORT_JOB_RESCUE_CLIENT = 0, /* WONDER_MAIL_MISSION_TYPE_* */
    PORT_JOB_RESCUE_TARGET = 1,
    PORT_JOB_ESCORT = 2,
    PORT_JOB_FIND_ITEM = 3,
    PORT_JOB_DELIVER_ITEM = 4
};

enum {
    PORT_JOBST_OPEN = 0,      /* accepted, not done yet */
    PORT_JOBST_ITEM = 1,      /* item job: the item is in the bag (find: done once the dungeon is left) */
    PORT_JOBST_DONE = 2,      /* done in this dungeon run (rescued, delivered, escorted) */
    PORT_JOBST_REWARD = 3,    /* done, the reward waits at the base */
    PORT_JOBST_SUSPENDED = 4  /* "Inaktiv" in the job list: not active in dungeons; jobs from the
                               * bulletin board start this way until set to "Aktiv" */
};

/* bits of PortPanel.floorMarks[] */
#define PORT_FLOOR_OPEN 1
#define PORT_FLOOR_DONE 2
#define PORT_FLOOR_INACTIVE 4 /* a job here that is not taken in the job list */

struct PortPanelJob {
    int kind;      /* PORT_JOB_* */
    int status;    /* PORT_JOBST_* */
    int dungeon;   /* dungeon id */
    int floor;           /* 0 for a found item: any floor of the dungeon counts */
    int here;      /* in the dungeon being explored */
    int special;   /* a special job (story, news) */
    char floorText[8];   /* "B3F" / "3F" (German "U3" / "E3"), "" without a floor */
    char dungeonName[40];
    char client[24], target[24], item[32];
    char what[96];       /* "Raupy retten", "Sinelbeere an Taubsi bringen" */
    char statusText[48]; /* "offen", "Item im Beutel" ... */
};

struct PortPanel {
    uint32_t serial;        /* changes with every refresh */
    char clock[8];          /* "09:24" */
    char playTime[16];      /* "12:34:56" */
    int money;              /* team money (Poké) */
    int bagCount, bagMax;   /* items in the bag / room in it */
    /* dungeon */
    int inDungeon;
    int dungeon, floor, floorCount, stairsUp;
    char dungeonName[40];
    char floorText[8];
    int belly, maxBelly;    /* of the leader; maxBelly 0 = unknown */
    int hp, maxHp;          /* of the leader */
    char weather[20];       /* "" when clear */
    /* jobs */
    int jobCount;           /* accepted job slots in use */
    int jobsHere;           /* active jobs in this dungeon */
    struct PortPanelJob jobs[PORT_PANEL_JOBS];
    unsigned char floorMarks[PORT_PANEL_FLOORS + 1]; /* index = floor; [0] = item to find (any floor) */
    /* items (also outside dungeons) */
    int setItem;            /* the item set for throwing (L+R, ITEM_FLAG_SET in the bag); 0 = none */
    int setCount;           /* of that item in the bag: stack sizes summed (thrown items) or slots */
    int setSticky;
    char setName[40];
    int heldItem;           /* the leader's held item; 0 = none */
    int heldCount;          /* stack size of a held thrown item, 0 otherwise */
    int heldSticky;
    char heldName[40];
};

/* Platform: 1 = keep the data current (panels shown, logging). */
void Port_PanelEnable(int on);
/* Frame loop, after the game's VBlank handler: refreshes the data every
 * few frames while enabled. */
void Port_PanelOnVBlank(void);
/* Refreshes now (the job overview opens). Game thread only. */
void Port_PanelRefresh(void);
const struct PortPanel *Port_PanelGet(void);
/* The panel as plain text (the host's PMD_PANEL_LOG); returns the length. */
int Port_PanelText(char *buf, int size);
/* An item's name as UTF-8 (Port_Text, the European ROM's language); "" for none. */
void Port_PanelItemName(char *out, int size, int item);
/* Time of day (PMD_CLOCK=HH:MM fixes it on the host). */
void Port_PanelTimeOfDay(int *hour, int *minute);

/* Set by the platform: 1 while the clock is shown beside the game image,
 * so the overlay leaves its small clock out of the image. */
extern int gPortPanelsVisible;

/* The floor as the European ROM writes it in the chosen language
 * ("U3" in German, "B3F" in English). */
void Port_FloorText(char *out, int size, int dungeon, int floor);

#endif /* PORT_PANEL_H */
