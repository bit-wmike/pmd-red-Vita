/*
 * Port overlay: HP bars over enemy monsters in dungeons and a small clock
 * (time of day, play time) in the frame's margin.
 *
 * The frame loop calls Port_OverlayOnVBlank() right after the game's VBlank
 * handler - the OAM and view shift written then are what the next rendered
 * frame shows - and Port_OverlayDraw() after the PPU has rendered the frame,
 * before it is handed to the platform. Drawing writes a few dozen pixels per
 * bar straight into the ABGR8888 frame; nothing is allocated.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/port.h"
#include "port/port_view.h"
#include "port/port_config.h"
#include "port/port_overlay.h"
#include "port/port_panel.h"

#include "global.h"
#include "run_dungeon.h"
#include "dungeon_util.h"
#include "structs/str_dungeon.h"
#include "structs/dungeon_entity.h"
#include "play_time.h"
#include "pokemon.h"

#define ABGR(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* ---- HP bars ---------------------------------------------------------- */

#define BAR_W 16 /* inner width (pixels of fill at full HP) */
#define BAR_H 3
#define BAR_ABOVE 22 /* rows above the monster's centre (a tile is 24 px, sprites ~24 px tall) */

struct Bar {
    short x, y; /* frame position of the bar's top-left inner pixel */
    unsigned char fill; /* 0..BAR_W */
    unsigned char level; /* 0 green, 1 yellow, 2 red */
};

static struct Bar sBars[DUNGEON_MAX_WILD_POKEMON];
static int sBarCount;

static int BarWanted(Entity *ent)
{
    EntityInfo *info;
    if (ent == NULL || ent->type != ENTITY_MONSTER || !ent->isVisible)
        return 0;
    info = GetEntInfo(ent);
    if (!info->isNotTeamMember)
        return 0;
    /* a shopkeeper minding its shop is not an enemy */
    if (info->shopkeeper == SHOPKEEPER_MODE_SHOPKEEPER)
        return 0;
    if (gPortConfig.hpBars == 1 && !info->bossFlag)
        return 0;
    /* darkness, invisibility: the same rule as the sprite */
    return Port_ShouldDrawEntity(ent) != 0;
}

static void CollectBars(void)
{
    const Dungeon *d = gDungeon;
    int dx, dy, i;

    sBarCount = 0;
    if (gPortConfig.hpBars == 0 || d == NULL || Port_ViewSceneShown() != PORT_SCENE_DUNGEON)
        return;
    Port_ViewWorldShift(&dx, &dy);
    for (i = 0; i < DUNGEON_MAX_WILD_POKEMON; i++) {
        Entity *ent = d->wildPokemon[i];
        EntityInfo *info;
        struct Bar *b;
        int hp, max, x, y;

        if (!BarWanted(ent))
            continue;
        info = GetEntInfo(ent);
        hp = info->HP;
        max = info->maxHPStat;
        if (max <= 0 || hp <= 0) /* defeated: no bar during the faint animation */
            continue;
        if (hp > max)
            hp = max;
        x = (ent->pixelPos.x / 256) - d->unk181e8.cameraPixelPos.x + dx;
        y = ((ent->pixelPos.y - ent->elevation) / 256) - d->unk181e8.cameraPixelPos.y + dy;
        b = &sBars[sBarCount++];
        b->x = (short)(x - BAR_W / 2);
        b->y = (short)(y - BAR_ABOVE);
        b->fill = (unsigned char)((hp * BAR_W + max / 2) / max);
        if (hp > 0 && b->fill == 0)
            b->fill = 1;
        b->level = (hp * 4 < max) ? 2 : (hp * 2 < max) ? 1 : 0;
    }
}

static void PutPixel(uint32_t *frame, int width, int height, int pitch, int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < width && y < height)
        frame[y * pitch + x] = color;
}

static void FillRect(uint32_t *frame, int width, int height, int pitch, int x, int y, int w, int h, uint32_t color)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > width ? width : x + w, y1 = y + h > height ? height : y + h;
    int r, c;
    for (r = y0; r < y1; r++) {
        uint32_t *row = frame + r * pitch;
        for (c = x0; c < x1; c++)
            row[c] = color;
    }
}

static void DrawBars(uint32_t *frame, int width, int height, int pitch)
{
    static const uint32_t fillColor[3] = {ABGR(0x40, 0xD8, 0x40), ABGR(0xF0, 0xD0, 0x20), ABGR(0xF0, 0x30, 0x30)};
    const uint32_t outline = ABGR(0x10, 0x10, 0x10), empty = ABGR(0x38, 0x38, 0x38);
    int i;
    for (i = 0; i < sBarCount; i++) {
        const struct Bar *b = &sBars[i];
        FillRect(frame, width, height, pitch, b->x - 1, b->y - 1, BAR_W + 2, BAR_H + 2, outline);
        FillRect(frame, width, height, pitch, b->x, b->y, BAR_W, BAR_H, empty);
        if (b->fill > 0)
            FillRect(frame, width, height, pitch, b->x, b->y, b->fill, BAR_H, fillColor[b->level]);
    }
}

/* ---- clock ------------------------------------------------------------ */

/* 3x5 pixel digits (bit 2 = left column), ':' is one column wide. */
static const unsigned char sDigits[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};
#define GLYPH_H 5

static int GlyphWidth(char c)
{
    return (c >= '0' && c <= '9') ? 3 : 1;
}

static int GlyphRow(char c, int row)
{
    if (c >= '0' && c <= '9')
        return sDigits[c - '0'][row];
    if (c == ':')
        return (row == 1 || row == 3) ? 1 : 0;
    return 0;
}

static int TextWidth(const char *s)
{
    int w = 0;
    for (; *s; s++)
        w += GlyphWidth(*s) + 1;
    return w > 0 ? w - 1 : 0;
}

/* White text with a one pixel dark outline, top-left at (x, y). */
static void DrawText(uint32_t *frame, int width, int height, int pitch, int x, int y, const char *s)
{
    const uint32_t fg = ABGR(0xF8, 0xF8, 0xF8), outline = ABGR(0x10, 0x10, 0x10);
    int pass;
    for (pass = 0; pass < 2; pass++) {
        const char *p;
        int cx = x;
        for (p = s; *p; p++) {
            int gw = GlyphWidth(*p), row, col;
            for (row = 0; row < GLYPH_H; row++) {
                int bits = GlyphRow(*p, row);
                for (col = 0; col < gw; col++) {
                    if (!(bits & (1 << (gw - 1 - col))))
                        continue;
                    if (pass == 0) {
                        int ox, oy;
                        for (oy = -1; oy <= 1; oy++)
                            for (ox = -1; ox <= 1; ox++)
                                PutPixel(frame, width, height, pitch, cx + col + ox, y + row + oy, outline);
                    } else {
                        PutPixel(frame, width, height, pitch, cx + col, y + row, fg);
                    }
                }
            }
            cx += gw + 1;
        }
    }
}

static void TimeOfDay(char *out, size_t size)
{
    int hour, minute;
    Port_PanelTimeOfDay(&hour, &minute);
    snprintf(out, size, "%02d:%02d", hour, minute);
}

static void DrawClock(uint32_t *frame, int width, int height, int pitch)
{
    char line[24];
    int x, y = 2;

    TimeOfDay(line, sizeof(line));
    x = width - TextWidth(line) - 2;
    DrawText(frame, width, height, pitch, x, y, line);

    if (gPlayTimeRef != NULL) {
        const struct PlayTimeStruct *pt = gPlayTimeRef;
        int hours = pt->hours < 0 ? 0 : pt->hours;
        snprintf(line, sizeof(line), "%d:%02d:%02d", hours, pt->minutes % 60, pt->seconds % 60);
        y += GLYPH_H + 2;
        x = width - TextWidth(line) - 2;
        DrawText(frame, width, height, pitch, x, y, line);
    }
}

/* ---- partners' HP beside the status line ------------------------------ */

/* 3x5 capitals for names (bit 2 = left column); umlauts get two dots above. */
static const unsigned char sLetters[26][5] = {
    {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4},
    {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7}, {1, 1, 1, 5, 2}, {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7},
    {5, 7, 7, 5, 5}, {6, 5, 5, 5, 5}, {2, 5, 5, 5, 2}, {6, 5, 6, 4, 4}, {2, 5, 5, 6, 3}, {6, 5, 6, 5, 5},
    {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2}, {5, 5, 5, 5, 7}, {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5},
    {5, 5, 2, 2, 2}, {7, 1, 2, 4, 7},
};

#define MAX_PARTNERS 3
struct Partner {
    unsigned char name[12]; /* the game's Latin-1 name */
    short hp, max;
};
static struct Partner sPartners[MAX_PARTNERS];
static int sPartnerCount;

static void CollectPartners(void)
{
    const Dungeon *d = gDungeon;
    int i;

    sPartnerCount = 0;
    if (gPortConfig.hpBars == 0 || d == NULL || Port_ViewSceneShown() != PORT_SCENE_DUNGEON)
        return;
    for (i = 0; i < MAX_TEAM_MEMBERS && sPartnerCount < MAX_PARTNERS; i++) {
        Entity *ent = d->teamPokemon[i];
        EntityInfo *info;
        struct Partner *p;
        if (ent == NULL || !EntityIsValid(ent))
            continue;
        info = GetEntInfo(ent);
        if (info->isTeamLeader || info->isNotTeamMember)
            continue;
        p = &sPartners[sPartnerCount++];
        memset(p->name, 0, sizeof(p->name));
        if (gRecruitedPokemonRef != NULL && info->teamIndex < MAX_TEAM_MEMBERS)
            memcpy(p->name, gRecruitedPokemonRef->dungeonTeam[info->teamIndex].name, POKEMON_NAME_LENGTH);
        p->hp = info->HP < 0 ? 0 : info->HP;
        p->max = info->maxHPStat;
    }
}

/* A Latin-1 letter as a capital A-Z (0 = none); *dots for an umlaut. */
static int Capital(unsigned char c, int *dots)
{
    *dots = 0;
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 'A';
    if (c >= 'A' && c <= 'Z')
        return c;
    switch (c) {
        case 0xC4: case 0xE4: *dots = 1; return 'A';
        case 0xD6: case 0xF6: *dots = 1; return 'O';
        case 0xDC: case 0xFC: *dots = 1; return 'U';
        case 0xDF: return 'S';
        case 0xC0: case 0xC1: case 0xC2: case 0xE0: case 0xE1: case 0xE2: return 'A';
        case 0xC8: case 0xC9: case 0xCA: case 0xE8: case 0xE9: case 0xEA: return 'E';
        case 0xCC: case 0xCD: case 0xCE: case 0xEC: case 0xED: case 0xEE: return 'I';
        case 0xD2: case 0xD3: case 0xD4: case 0xF2: case 0xF3: case 0xF4: return 'O';
        case 0xD9: case 0xDA: case 0xDB: case 0xF9: case 0xFA: case 0xFB: return 'U';
        case 0xC7: case 0xE7: return 'C';
        case 0xD1: case 0xF1: return 'N';
    }
    return 0;
}

/* The name in capitals with a dark outline; returns its width. */
static int DrawName(uint32_t *frame, int width, int height, int pitch, int x, int y, const unsigned char *s, int n, uint32_t fg)
{
    const uint32_t outline = ABGR(0x10, 0x10, 0x10);
    int pass, w = 0;
    for (pass = 0; pass < 2; pass++) {
        int cx = x, k;
        for (k = 0; k < n && s[k]; k++) {
            int dots, c = Capital(s[k], &dots), row, col;
            if (c == 0) {
                cx += (s[k] == ' ') ? 3 : 0;
                continue;
            }
            for (row = -2; row < GLYPH_H; row++) {
                int bits = row < 0 ? (dots && row == -2 ? 5 : 0) : sLetters[c - 'A'][row];
                for (col = 0; col < 3; col++) {
                    if (!(bits & (4 >> col)))
                        continue;
                    if (pass == 0) {
                        int ox, oy;
                        for (oy = -1; oy <= 1; oy++)
                            for (ox = -1; ox <= 1; ox++)
                                PutPixel(frame, width, height, pitch, cx + col + ox, y + row + oy, outline);
                    } else {
                        PutPixel(frame, width, height, pitch, cx + col, y + row, fg);
                    }
                }
            }
            cx += 4;
        }
        w = cx - x;
    }
    return w;
}

int Port_ViewHudStatus(int *x0, int *y0, int *x1); /* port/port_view.c */

#define PARTNER_BAR_W 20

static void DrawPartners(uint32_t *frame, int width, int height, int pitch)
{
    static const uint32_t fillColor[3] = {ABGR(0x40, 0xD8, 0x40), ABGR(0xF0, 0xD0, 0x20), ABGR(0xF0, 0x30, 0x30)};
    const uint32_t outline = ABGR(0x10, 0x10, 0x10), empty = ABGR(0x38, 0x38, 0x38), nameColor = ABGR(0xF8, 0xE0, 0x48);
    int sx0, sy0, sx1, x, y, i, limit = width - 4;

    if (!Port_ViewHudStatus(&sx0, &sy0, &sx1))
        return;
    if (gPortConfig.clockOverlay && !gPortPanelsVisible)
        limit = width - 40; /* the clock's corner */
    x = sx1 + 6;
    y = sy0 + 2;
    for (i = 0; i < sPartnerCount; i++) {
        const struct Partner *p = &sPartners[i];
        char num[16];
        int n, w, fill, level, hp = p->hp > p->max ? p->max : p->hp;
        snprintf(num, sizeof(num), "%d/%d", hp, p->max);
        for (n = 0; n < POKEMON_NAME_LENGTH && p->name[n]; n++)
            ;
        w = 4 * n + 3 + TextWidth(num) + 3 + PARTNER_BAR_W + 2;
        if (x + w > limit && x > sx1 + 6) {
            x = sx1 + 6;
            y += GLYPH_H + 4;
        }
        x += DrawName(frame, width, height, pitch, x, y, p->name, POKEMON_NAME_LENGTH, nameColor) + 3;
        DrawText(frame, width, height, pitch, x, y, num);
        x += TextWidth(num) + 4;
        fill = p->max > 0 ? (hp * PARTNER_BAR_W + p->max / 2) / p->max : 0;
        if (hp > 0 && fill == 0)
            fill = 1;
        level = (hp * 4 < p->max) ? 2 : (hp * 2 < p->max) ? 1 : 0;
        FillRect(frame, width, height, pitch, x - 1, y, PARTNER_BAR_W + 2, 5, outline);
        FillRect(frame, width, height, pitch, x, y + 1, PARTNER_BAR_W, 3, empty);
        if (fill > 0)
            FillRect(frame, width, height, pitch, x, y + 1, fill, 3, fillColor[level]);
        x += PARTNER_BAR_W + 8;
    }
}

/* ---- hooks ------------------------------------------------------------ */

void Port_OverlayOnVBlank(void)
{
    CollectBars();
    CollectPartners();
}

void Port_OverlayDraw(uint32_t *frame, int width, int height, int pitch)
{
    if (sBarCount > 0 && gPortConfig.hpBars != 0)
        DrawBars(frame, width, height, pitch);
    if (sPartnerCount > 0 && gPortConfig.hpBars != 0)
        DrawPartners(frame, width, height, pitch);
    /* the Vita shows the clock beside the image when its panels are visible */
    if (gPortConfig.clockOverlay && !gPortPanelsVisible)
        DrawClock(frame, width, height, pitch);
}
