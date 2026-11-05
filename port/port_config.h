/* Settings of the PMD port (port/port_config.c). */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

enum {
    PORT_STYLE_SMOOTH = 0,
    PORT_STYLE_SHARP = 1,
    PORT_STYLE_PIXEL = 2
};

/* What fills the screen around the game image when it has borders
 * (pixel-perfect style, GBA view): nothing, the darkened LiveArea
 * background or a bezel graphic. */
enum {
    PORT_FRAME_OFF = 0,
    PORT_FRAME_ARTWORK = 1,
    PORT_FRAME_BEZEL = 2
};

#define PORT_CONFIG_VERSION 1

struct PortConfig {
    int configVersion;
    int viewMode;        /* PORT_VIEW_* */
    int displayStyle;    /* PORT_STYLE_* */
    int frame;           /* PORT_FRAME_*: what surrounds the game image (Vita) */
    int frameRate30;     /* render every second frame (game speed unchanged) */
    int turbo;           /* fast forward allowed (front touch held / host key) */
    int squareThrow;     /* Square = L+R (throw the set item) instead of B */
    int triangleMove;    /* Triangle = L+A (use the set move) instead of fast forward */
    int rightStickDiag;  /* right stick = R + direction (diagonal moves) */
    /* cheats that only ease the mechanics */
    int expMultiplier;   /* 1..4: experience gained in dungeons */
    int noHunger;        /* the belly does not go down */
    int textDebug;       /* log every text without a translation */
    int language;        /* PORT_LANG_*: -1 = not chosen yet (European ROM only) */
    /* comfort (all optional, port menu) */
    int textInstant;     /* 0/1: text boxes show their text at once */
    int textAuto;        /* 0/1: holding A advances text boxes */
    int dungeonSpeed;    /* 0 normal, 1 x1.5, 2 x2: moves and enemy turns in dungeons */
    int walkFast;        /* 0/1: faster walking in the town and base */
    int floorAutosave;   /* 0/1: quick save whenever a dungeon floor is entered */
    int clockOverlay;    /* 0/1: time of day and play time in the margin */
    int hpBars;          /* 0 off, 1 bosses, 2 all enemies: HP bars over enemies */
    /* soft cheats */
    int moneyMult;       /* 1..4: money found and rewards */
    int iqMult;          /* 1..4: IQ gained */
    int recruitBoost;    /* 0 off, 1 +20 %, 2 +50 %: recruitment chance */
    int stairsMarker;    /* 0/1: the stairs are shown on the floor map from the start */
    int statAtk, statDef, statSpAtk, statSpDef, statSpeed; /* -3..+3: team stat modifiers (steps of 10 %) */
};

extern struct PortConfig gPortConfig;

void Port_ConfigDefaults(void);
void Port_ConfigParse(const char *text);
int Port_ConfigWrite(char *buf, int size);
void Port_ConfigMigrate(void);
void Port_ConfigLoad(void);
void Port_ConfigSave(void);

#endif /* PORT_CONFIG_H */
