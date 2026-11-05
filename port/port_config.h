/* Settings of the PMD port (port/port_config.c). */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

enum {
    PORT_STYLE_SMOOTH = 0,
    PORT_STYLE_SHARP = 1,
    PORT_STYLE_PIXEL = 2
};

#define PORT_CONFIG_VERSION 1

struct PortConfig {
    int configVersion;
    int viewMode;        /* PORT_VIEW_* */
    int displayStyle;    /* PORT_STYLE_* */
    int frameRate30;     /* render every second frame (game speed unchanged) */
    int turbo;           /* fast forward allowed (front touch held / host key) */
    int squareThrow;     /* Square = L+R (throw the set item) instead of B */
    int triangleMove;    /* Triangle = L+A (use the set move) instead of fast forward */
    int rightStickDiag;  /* right stick = R + direction (diagonal moves) */
    /* cheats that only ease the mechanics */
    int expMultiplier;   /* 1..4: experience gained in dungeons */
    int noHunger;
    int language;        /* PORT_LANG_*: -1 = not chosen yet (European ROM only) */        /* the belly does not go down */
};

extern struct PortConfig gPortConfig;

void Port_ConfigDefaults(void);
void Port_ConfigParse(const char *text);
int Port_ConfigWrite(char *buf, int size);
void Port_ConfigMigrate(void);
void Port_ConfigLoad(void);
void Port_ConfigSave(void);

#endif /* PORT_CONFIG_H */
