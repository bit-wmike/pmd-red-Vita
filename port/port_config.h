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
    int rearTouch;       /* rear touch: left = quick save, right = quick load */
    int turbo;           /* R trigger held = fast forward (host: TMC_FAST) */
    /* cheats */
    int cheatHp;         /* leader's HP stays full */
    int cheatBelly;      /* belly stays full */
    int cheatMoney;      /* 99999 Poké */
    int cheatNoHunger;   /* same as belly, kept for the ini */
};

extern struct PortConfig gPortConfig;

void Port_ConfigDefaults(void);
void Port_ConfigParse(const char *text);
int Port_ConfigWrite(char *buf, int size);
void Port_ConfigMigrate(void);
void Port_ConfigLoad(void);
void Port_ConfigSave(void);

#endif /* PORT_CONFIG_H */
