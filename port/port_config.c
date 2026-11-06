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
    {"frame", &gPortConfig.frame, 0, 2},
    {"frame_rate_30", &gPortConfig.frameRate30, 0, 1},
    {"turbo", &gPortConfig.turbo, 0, 1},
    {"square_throw", &gPortConfig.squareThrow, 0, 1},
    {"triangle_move", &gPortConfig.triangleMove, 0, 1},
    {"right_stick_diagonal", &gPortConfig.rightStickDiag, 0, 1},
    {"exp_multiplier", &gPortConfig.expMultiplier, 1, 4},
    {"no_hunger", &gPortConfig.noHunger, 0, 1},
    {"language", &gPortConfig.language, -1, 4},
    {"text_debug", &gPortConfig.textDebug, 0, 1},
    {"text_instant", &gPortConfig.textInstant, 0, 1},
    {"slow_blink", &gPortConfig.slowBlink, 0, 1},
    {"text_auto", &gPortConfig.textAuto, 0, 1},
    {"dungeon_speed", &gPortConfig.dungeonSpeed, 0, 2},
    {"walk_fast", &gPortConfig.walkFast, 0, 1},
    {"floor_autosave", &gPortConfig.floorAutosave, 0, 1},
    {"clock_overlay", &gPortConfig.clockOverlay, 0, 1},
    {"hp_bars", &gPortConfig.hpBars, 0, 2},
    {"money_multiplier", &gPortConfig.moneyMult, 1, 4},
    {"iq_multiplier", &gPortConfig.iqMult, 1, 4},
    {"recruit_boost", &gPortConfig.recruitBoost, 0, 2},
    {"stairs_marker", &gPortConfig.stairsMarker, 0, 1},
    {"stat_atk", &gPortConfig.statAtk, -3, 3},
    {"stat_def", &gPortConfig.statDef, -3, 3},
    {"stat_spatk", &gPortConfig.statSpAtk, -3, 3},
    {"stat_spdef", &gPortConfig.statSpDef, -3, 3},
    {"stat_speed", &gPortConfig.statSpeed, -3, 3},
    {"side_panels", &gPortConfig.sidePanels, 0, 1},
    {"msg_fast", &gPortConfig.msgFast, 0, 1},
    {"save_slot", &gPortConfig.saveSlot, 1, 3},
    {"base_autosave", &gPortConfig.baseAutosave, 0, 1},
    {"map_overlay", &gPortConfig.mapOverlay, 0, 1},
    {"belly_slow", &gPortConfig.bellySlow, 0, 1},
    {"traps_visible", &gPortConfig.trapsVisible, 0, 1},
    {"pp_saver", &gPortConfig.ppSaver, 0, 1},
    {"shop_buy_half", &gPortConfig.shopBuyHalf, 0, 1},
    {"shop_sell_double", &gPortConfig.shopSellDouble, 0, 1},
    {"floor_reveal", &gPortConfig.floorReveal, 0, 1},
};

void Port_ConfigDefaults(void)
{
    memset(&gPortConfig, 0, sizeof(gPortConfig));
    gPortConfig.configVersion = PORT_CONFIG_VERSION;
    gPortConfig.viewMode = PORT_VIEW_FULL;
    gPortConfig.displayStyle = PORT_STYLE_SHARP;
    gPortConfig.frame = PORT_FRAME_ARTWORK;
    gPortConfig.turbo = 1;
    gPortConfig.language = -1;
    gPortConfig.squareThrow = 1;
    gPortConfig.triangleMove = 1;
    gPortConfig.rightStickDiag = 1;
    gPortConfig.expMultiplier = 1;
    gPortConfig.moneyMult = 1;
    gPortConfig.iqMult = 1;
    gPortConfig.hpBars = 2;
    gPortConfig.slowBlink = 1;
    gPortConfig.sidePanels = 1;
    gPortConfig.saveSlot = 1;
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
