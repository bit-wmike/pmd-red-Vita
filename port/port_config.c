/*
 * Settings of the PMD port, stored as a small ini file by the platform
 * (ux0:data/pmd/settings.ini on the Vita).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/port_config.h"
#include "port/port_view.h"

struct PortConfig gPortConfig;

struct Field {
    const char *key;
    int *value;
    int min, max;
};

static struct Field sFields[] = {
    {"config_version", &gPortConfig.configVersion, 0, 1000},
    {"view_mode", &gPortConfig.viewMode, 0, 2},
    {"display_style", &gPortConfig.displayStyle, 0, 2},
    {"frame_rate_30", &gPortConfig.frameRate30, 0, 1},
    {"rear_touch", &gPortConfig.rearTouch, 0, 1},
    {"turbo", &gPortConfig.turbo, 0, 1},
    {"cheat_hp", &gPortConfig.cheatHp, 0, 1},
    {"cheat_belly", &gPortConfig.cheatBelly, 0, 1},
    {"cheat_money", &gPortConfig.cheatMoney, 0, 1},
};

void Port_ConfigDefaults(void)
{
    memset(&gPortConfig, 0, sizeof(gPortConfig));
    gPortConfig.configVersion = PORT_CONFIG_VERSION;
    gPortConfig.viewMode = PORT_VIEW_FULL;
    gPortConfig.displayStyle = PORT_STYLE_SHARP;
    gPortConfig.rearTouch = 1;
}

void Port_ConfigParse(const char *text)
{
    char line[128];
    while (*text) {
        const char *end = strchr(text, '\n');
        size_t len = end ? (size_t)(end - text) : strlen(text);
        char *eq;
        size_t i;
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, text, len);
        line[len] = 0;
        text += len + (end ? 1 : 0);
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        for (i = 0; i < sizeof(sFields) / sizeof(sFields[0]); i++) {
            if (strcmp(line, sFields[i].key) == 0) {
                int v = atoi(eq + 1);
                if (v < sFields[i].min) v = sFields[i].min;
                if (v > sFields[i].max) v = sFields[i].max;
                *sFields[i].value = v;
            }
        }
    }
}

void Port_ConfigMigrate(void)
{
    gPortConfig.configVersion = PORT_CONFIG_VERSION;
}

int Port_ConfigWrite(char *buf, int size)
{
    int n = 0;
    size_t i;
    for (i = 0; i < sizeof(sFields) / sizeof(sFields[0]); i++)
        n += snprintf(buf + n, size > n ? size - n : 0, "%s=%d\n", sFields[i].key, *sFields[i].value);
    return n;
}
