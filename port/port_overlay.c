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
#include <time.h>

#include "port/port.h"
#include "port/port_view.h"
#include "port/port_config.h"
#include "port/port_overlay.h"

#include "global.h"
#include "run_dungeon.h"
#include "dungeon_util.h"
#include "structs/str_dungeon.h"
#include "structs/dungeon_entity.h"
#include "play_time.h"

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
    return ShouldDisplayEntity(ent) != 0;
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
    static uint32_t lastFrame = 0xFFFFFFFFu;
    static int hour, minute;
    uint32_t frame = Port_FrameCounter();

    if (lastFrame == 0xFFFFFFFFu || frame - lastFrame >= 60) {
        time_t now;
        struct tm *t;
#ifndef __vita__
        /* host tests: a fixed time keeps the frames reproducible */
        static const char *fixed;
        static int fixedChecked;
        if (!fixedChecked) {
            fixed = getenv("PMD_CLOCK");
            fixedChecked = 1;
        }
        if (fixed != NULL && sscanf(fixed, "%d:%d", &hour, &minute) == 2) {
            lastFrame = frame;
            goto done;
        }
#endif
        now = time(NULL);
        t = localtime(&now);
        if (t != NULL) {
            hour = t->tm_hour;
            minute = t->tm_min;
        }
        lastFrame = frame;
    }
#ifndef __vita__
done:
#endif
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

/* ---- hooks ------------------------------------------------------------ */

void Port_OverlayOnVBlank(void)
{
    CollectBars();
}

void Port_OverlayDraw(uint32_t *frame, int width, int height, int pitch)
{
    if (sBarCount > 0 && gPortConfig.hpBars != 0)
        DrawBars(frame, width, height, pitch);
    if (gPortConfig.clockOverlay)
        DrawClock(frame, width, height, pitch);
}
