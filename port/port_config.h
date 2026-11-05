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
};

extern struct PortConfig gPortConfig;

void Port_ConfigDefaults(void);
void Port_ConfigParse(const char *text);
int Port_ConfigWrite(char *buf, int size);
void Port_ConfigMigrate(void);
void Port_ConfigLoad(void);
void Port_ConfigSave(void);

#endif /* PORT_CONFIG_H */
