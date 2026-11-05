/*
 * Wide view and Full View ("Weitsicht") for the PMD port.
 *
 * The game keeps running for its 240x160 GBA screen: camera, scrolling,
 * logic and the HUD are untouched. The port always draws a frame of the
 * chosen size (400x240 in Full View, 282x160 in the wide view), so the zoom
 * never changes. The GBA screen sits at (dx, dy) inside it. In dungeon,
 * ground (town, base, cutscene) and map scenes the margins show more of the
 * same map; on every other screen (title, full-screen menus, pictures) the
 * GBA picture is centred and the margins stay black:
 *
 *  - Map layers (BG3, BG2 on two-layer maps): the game's VRAM tilemaps are
 *    only 32x32 tiles and only filled around the GBA screen, so the port
 *    builds its own tilemap rows for the whole window from the game's map
 *    data (dungeon cels, ground chunk mappings) and hands them to the
 *    renderer as "shadow" tilemaps.
 *  - UI layers (BG0 text/HUD, BG1 window fill and darkness): shown at the
 *    GBA screen position; outside it BG0 is blank and BG1 continues the
 *    darkness fill tile.
 *  - Objects: every OAM slot knows the true screen position of the sprite
 *    it holds (Port_SpriteStaged / Port_OamSlot), so coordinates beyond
 *    the 9/8-bit OAM range are recovered; sprites are shifted by (dx, dy).
 *    The game's culling is widened by gPortViewMargin*.
 *  - Windows (text-box shapes, darkness) and the HBlank table are shifted
 *    by (dx, dy); edges at the GBA screen border extend to the window border.
 *
 * Ground maps smaller than the window get a smaller, centred frame (same
 * zoom); the dungeon shows its out-of-bounds tile beyond the map, like the
 * game itself does.
 */
#include <stdlib.h>
#include <string.h>

#include "port/port.h"
#include "port/port_view.h"
#include "port/port_game.h"
#include "port/ppu/include/cpu/mode1.h"

#include "global.h"
#include "run_dungeon.h"
#include "dungeon_map_access.h"
#include "dungeon_util.h"
#include "structs/str_dungeon.h"
#include "structs/dungeon_entity.h"
#include "structs/map.h"
#include "structs/str_ground_bg.h"
#include "code_809D148.h"
#include "text_1.h"
#include "world_map.h"
#include "friend_areas_map.h"

struct WorldMap *Port_WorldMap(void);
extern GroundBg *gUnknown_3001B78; /* ground_weather.c: the weather's ground map */
#define PICTURE_W 480
#define PICTURE_H 320

int gPortViewMode = PORT_VIEW_FULL;
int gPortViewMarginLeft, gPortViewMarginRight, gPortViewMarginTop, gPortViewMarginBottom;

extern const Tile *gCurTilesetOobTile;

/* View of the frame being shown (latched at VBlank). */
static int sActive;
static int sWidth = PORT_GBA_W, sHeight = PORT_GBA_H;
static int sDx, sDy;
static int sContentW = PORT_GBA_W, sContentH = PORT_GBA_H;
static int sPictureMap; /* the current ground map is a GBA-screen picture (S02 ...) */ /* the part of the frame that shows the map */
static int sScene, sSceneLatched;
static uint32_t sSceneFrame;

/* Port-side tilemaps handed to the renderer: 32 ring rows of
 * MODE1_WS_SHADOW_COLS columns per BG. */
#define SHADOW_COLS MODE1_WS_SHADOW_COLS
static uint16_t sShadow[4][32 * SHADOW_COLS];
#define BLANK_ENTRY 0xFFFFu /* renderer: fully transparent */
#define DARK_ENTRY 0xF27Au  /* BG1 fill tile 0x27A, palette 15 (dark in dungeons, blank elsewhere) */

/* Per staged sprite / OAM slot: true GBA-screen position. */
static short sStagedX[128], sStagedY[128];
static short sSlotX[128], sSlotY[128];

#define IO16(off) (*(volatile u16 *)(gHostIo + (off)))

/* ------------------------------------------------------------ hooks */

void Port_ViewSceneFrame(int scene)
{
    if (scene != sScene)
        Port_Crumb("scene %d", scene);
    sScene = scene;
    sSceneFrame = Port_FrameCounter();
}

int Port_ViewScene(void)
{
    return sScene;
}

void Port_SpriteStaged(int index, int x, int y)
{
    if (index < 0 || index >= 128)
        return;
    sStagedX[index] = (short)x;
    sStagedY[index] = (short)y;
}

static uint32_t sSlotFrame[128]; /* the frame a slot was last staged in */

void Port_OamSlot(int slot, int index)
{
    if (slot < 0 || slot >= 128)
        return;
    if (index < 0 || index >= 128) {
        sSlotX[slot] = PORT_GBA_W / 2;
        sSlotY[slot] = PORT_GBA_H / 2;
        return;
    }
    sSlotX[slot] = sStagedX[index];
    sSlotY[slot] = sStagedY[index];
    sSlotFrame[slot] = Port_FrameCounter() + 1;
}

int Port_ViewWidth(void)
{
    return sActive ? sWidth : PORT_GBA_W;
}

int Port_ViewHeight(void)
{
    return sActive ? sHeight : PORT_GBA_H;
}

int Port_ViewZoomHeight(void)
{
    return Port_ViewHeight();
}

/* ------------------------------------------------------------ sizing */

static void SizeForMode(int mode, int *w, int *h)
{
    switch (mode) {
        case PORT_VIEW_WIDE:
            *w = PORT_VIEW_WIDE_W;
            *h = PORT_GBA_H;
            break;
        case PORT_VIEW_FULL:
            *w = PORT_VIEW_FULL_W;
            *h = PORT_VIEW_FULL_H;
            break;
        default:
            *w = PORT_GBA_W;
            *h = PORT_GBA_H;
            break;
    }
}

/* Offset of the window start before the GBA screen along one axis (>= 0),
 * for a window that must stay inside a map of size `room`. */
static int WindowOffset(int cam, int screen, int window, int room)
{
    int extra = window - screen;
    int start = cam - extra / 2;
    int off;
    if (room > 0) {
        if (start > room - window)
            start = room - window;
        if (start < 0)
            start = 0;
    }
    off = cam - start;
    if (off < 0)
        off = 0;
    if (off > extra)
        off = extra;
    return off;
}

static int ClampToRoom(int size, int room, int gba)
{
    if (room > 0 && size > room)
        size = room;
    if (size < gba)
        size = gba;
    return size;
}

/* Is a DMA channel set to run at every HBlank (a per-line effect)? */
static int HBlankDmaActive(void)
{
    static const unsigned cnt[3] = {0x0C6, 0x0D2, 0x0DE}; /* DMA1..3 CNT_H */
    unsigned k;
    for (k = 0; k < 3; k++) {
        uint16_t v;
        memcpy(&v, gHostIo + cnt[k], 2);
        if ((v & 0x8000) && ((v >> 12) & 3) == 2)
            return 1;
    }
    return 0;
}

/* Is the dungeon darkness in effect? Then the game fills its BG1 tilemap
 * with the dark tile 0x27A (the window around the player cuts a hole);
 * on a lit floor BG1 holds other entries. The four corners are sampled. */
static int DarknessActive(void)
{
    uint16_t cnt, entry, dispcnt, winout;
    unsigned base, dark = 0, k;
    static const unsigned char corners[4][2] = {{0, 0}, {29, 0}, {0, 19}, {29, 19}};
    /* with windows on, the margins count as "outside": BG1 shows there only
     * if WINOUT lets it (dark floors: yes, the hole around the player is the
     * window; lit floors: empty windows and WINOUT hides BG1) */
    memcpy(&dispcnt, gHostIo + 0x00, 2);
    memcpy(&winout, gHostIo + 0x4A, 2);
    if (dispcnt & 0x6000)
        return (winout & 2) != 0;
    memcpy(&cnt, gHostIo + 0x0A, 2); /* BG1CNT */
    base = ((cnt >> 8) & 31) * 0x800;
    for (k = 0; k < 4; k++) {
        memcpy(&entry, gHostVram + base + (corners[k][1] * 32 + corners[k][0]) * 2, 2);
        if ((entry & 0x3FF) == 0x27A)
            dark++;
    }
#ifndef __vita__
    if (getenv("PMD_DARK_DEBUG") && Port_FrameCounter() % 300 == 0) {
        uint16_t io[8];
        memcpy(io, gHostIo, 16);
        Platform_Log("dark: frame %u dispcnt %04x bg1cnt %04x base %x corners dark %u win0h %04x win0v %04x win1h %04x win1v %04x winin %04x winout %04x\n",
                     Port_FrameCounter(), io[0], cnt, base, dark, *(uint16_t *)(gHostIo + 0x40), *(uint16_t *)(gHostIo + 0x44),
                     *(uint16_t *)(gHostIo + 0x42), *(uint16_t *)(gHostIo + 0x46), *(uint16_t *)(gHostIo + 0x48), *(uint16_t *)(gHostIo + 0x4A));
    }
#endif
    return dark >= 3;
}

/* A map of one flat chunk (the black dream scenes): nothing to extend,
 * the GBA screen stays centred so that the game's centred text is. */
static int UniformMap(const MapRender *mr)
{
    static const u16 *lastChunks;
    static int lastW, lastH, lastResult;
    const u16 *chunks = mr->chunkMappings[0], *tiles = mr->tileMappings;
    int dim = mr->chunkDimensions == CHUNK_DIMENSIONS_2x2 ? 2 : 3;
    int cx, cy, k, w = mr->widthChunks, h = mr->heightChunks;
    if (chunks == NULL || tiles == NULL || w <= 0 || h <= 0 || w > 64)
        return 0;
    if (chunks == lastChunks && w == lastW && h == lastH && (Port_FrameCounter() & 31) != 0)
        return lastResult;
    lastChunks = chunks;
    lastW = w;
    lastH = h;
    lastResult = 0;
    for (cy = 0; cy < h; cy++)
        for (cx = 0; cx < w; cx++)
            if (chunks[cy * 64 + cx] != chunks[0])
                return 0;
    for (k = 1; k < dim * dim; k++)
        if (tiles[chunks[0] * 9 + k] != tiles[chunks[0] * 9])
            return 0;
    lastResult = 1;
    return 1;
}

/* The title screen (S02), the health warning (S05) and S06 are ground maps
 * made for the 240x160 screen, wider than it (the title pans) and with
 * stray rows below: shown at the map's width and the GBA screen's rows. */
static int PictureMapName(const char *name)
{
    size_t n = strlen(name);
    return n >= 4 && name[0] == 'S' && name[1] >= '0' && name[1] <= '9' && name[2] >= '0' && name[2] <= '9' && name[n - 1] == 'c';
}

/* The ground map's camera (top-left of the GBA screen) and map size. */
static int GroundInfo(int *camX, int *camY, int *mapW, int *mapH, int *wrap)
{
    GroundBg *g = gGroundMapDungeon_3001B70;
    PixelPos cam;
    sPictureMap = 0;
    if (g == NULL || g->unk474 <= 0 || g->mapRender[0].chunkDimensions == 0)
        return 0;
    if (!sub_809D248(&cam))
        return 0;
    if (UniformMap(&g->mapRender[0]))
        return 0;
    if (g->bpcFile != NULL && g->bpcFile->file != NULL && g->bpcFile->file->name != NULL &&
        PictureMapName((const char *)g->bpcFile->file->name))
        sPictureMap = 1;
    *camX = cam.x;
    *camY = cam.y;
    *mapW = g->mapRender[0].mapSizePixels.x;
    *mapH = g->mapRender[0].mapSizePixels.y;
    *wrap = g->mapRender[0].wrapAround;
    return 1;
}

void Port_ViewBeforeLogicEnd(void)
{
}

/* Called after the game's VBlank handler: the registers and OAM it has just
 * written are what the next rendered frame shows. */
void Port_ViewOnVBlank(void)
{
    int w, h, cw, ch;
    int camX = 0, camY = 0, mapW = 0, mapH = 0, wrap = 0;

    /* A scene stays current while its frame function keeps reporting;
     * after two frames without a report (title screen, menus) the frame
     * is a picture without a map again. */
    sSceneLatched = (Port_FrameCounter() - sSceneFrame <= 2) ? sScene : PORT_SCENE_NONE;
    SizeForMode(gPortViewMode, &w, &h);
    sActive = (w != PORT_GBA_W || h != PORT_GBA_H);
    sWidth = w;
    sHeight = h;

    /* The map's extent decides how much of the frame shows the world;
     * the rest of the frame stays black and the content is centred. */
    cw = PORT_GBA_W;
    ch = PORT_GBA_H;
    if (sActive) {
        switch (sSceneLatched) {
            case PORT_SCENE_DUNGEON:
                if (gDungeon != NULL) {
                    cw = w;
                    ch = h;
                }
                break;
            case PORT_SCENE_GROUND:
                if (GroundInfo(&camX, &camY, &mapW, &mapH, &wrap)) {
                    if (sPictureMap) {
                        cw = ClampToRoom(w, mapW, PORT_GBA_W);
                        ch = PORT_GBA_H;
                    } else if (wrap) {
                        cw = w;
                        ch = h;
                    } else if (mapW > PORT_GBA_W && mapH > PORT_GBA_H) {
                        /* a map made for the GBA screen in one direction
                         * (logos, fixed pictures) stays at the GBA size */
                        cw = ClampToRoom(w, mapW, PORT_GBA_W);
                        ch = ClampToRoom(h, mapH, PORT_GBA_H);
                    }
                }
                break;
            case PORT_SCENE_WORLDMAP:
            case PORT_SCENE_FRIENDMAP: {
                struct WorldMap *m = Port_WorldMap();
                if (sSceneLatched == PORT_SCENE_WORLDMAP && m != NULL) {
                    camX = m->bgPos.x;
                    camY = m->bgPos.y;
                } else if (sSceneLatched == PORT_SCENE_FRIENDMAP && gFriendAreasMapPtr != NULL) {
                    camX = gFriendAreasMapPtr->bgPos.x;
                    camY = gFriendAreasMapPtr->bgPos.y;
                } else {
                    break;
                }
                mapW = PICTURE_W;
                mapH = PICTURE_H;
                cw = ClampToRoom(w, mapW, PORT_GBA_W);
                ch = ClampToRoom(h, mapH, PORT_GBA_H);
                break;
            }
            default:
                break;
        }
    }
    sContentW = cw;
    sContentH = ch;
    if (!sActive) {
        sDx = sDy = 0;
    } else if (mapW > 0 && !wrap && cw > PORT_GBA_W) {
        /* the window follows the camera and stops at the map's edges */
        sDx = (w - cw) / 2 + WindowOffset(camX, PORT_GBA_W, cw, mapW);
        sDy = (h - ch) / 2 + WindowOffset(camY, PORT_GBA_H, ch, mapH);
    } else {
        sDx = (w - cw) / 2 + (cw - PORT_GBA_W) / 2;
        sDy = (h - ch) / 2 + (ch - PORT_GBA_H) / 2;
    }

    /* The logic frame that starts now checks visibility against the
     * content window. */
    gPortViewMarginLeft = sActive ? sDx - (w - cw) / 2 : 0;
    gPortViewMarginRight = sActive ? cw - PORT_GBA_W - gPortViewMarginLeft : 0;
    gPortViewMarginTop = sActive ? sDy - (h - ch) / 2 : 0;
    gPortViewMarginBottom = sActive ? ch - PORT_GBA_H - gPortViewMarginTop : 0;
}

/* ------------------------------------------------------- map layers */

static int FloorDiv(int v, int d)
{
    return v >= 0 ? v / d : -((d - 1 - v) / d);
}

/* The 3x3 tilemap entries of a dungeon cell, as the game chooses them
 * (dungeon_map_access.c, UpdateTrapsVisibility). */
static const u16 *DungeonCel(const Dungeon *dungeon, int cx, int cy)
{
    const Tile *tile = GetTile(cx, cy);
    if (tile->terrainFlags & TERRAIN_TYPE_UNK_x1000)
        return dungeon->unk12A18[26];
    if (tile->terrainFlags & TERRAIN_TYPE_SHOP)
        return dungeon->unk12A18[25];
    if (dungeon->unk181e8.hallucinating)
        return dungeon->unk11884[tile->unk8];
    if (tile->spawnOrVisibilityFlags.visibility & 4)
        return dungeon->unk12A18[27];
    if (tile->object != NULL) {
        Entity *obj = tile->object;
        if (GetEntityType(obj) == ENTITY_TRAP && (obj->isVisible || dungeon->unk181e8.showInvisibleTrapsMonsters)) {
            u8 trapType = GetEntInfo(obj)->flags;
            return dungeon->unk12A18[trapType];
        }
        return dungeon->unk11884[tile->unk8];
    }
    if (tile->terrainFlags & TERRAIN_TYPE_STAIRS)
        return dungeon->unk12A18[dungeon->unk3A12];
    return dungeon->unk11884[tile->unk8];
}

/* Fills the ring rows of BG3 for a dungeon window whose top-left is at
 * world pixel (x0, y0). */
static void FillDungeon(int bg, int x0, int y0, int cols)
{
    const Dungeon *dungeon = gDungeon;
    int tx0 = x0 >> 3, ty0 = y0 >> 3;
    int r, c;
    uint16_t *shadow = sShadow[bg];

    for (r = 0; r < 32; r++) {
        int ty = ty0 + r;
        int cy = FloorDiv(ty, 3), sy = ty - cy * 3;
        uint16_t *row = shadow + (ty & 31) * SHADOW_COLS;
        int lastCx = -100000;
        const u16 *cel = NULL;
        for (c = 0; c < cols; c++) {
            int tx = tx0 + c;
            int cx = FloorDiv(tx, 3), sx = tx - cx * 3;
            if (cx != lastCx) {
                cel = DungeonCel(dungeon, cx, cy);
                lastCx = cx;
            }
            row[c] = cel[sy * 3 + sx];
        }
    }
}

/* Fills the ring rows of one ground map layer. */
static void FillGround(int bg, const MapRender *mr, int layer, int x0, int y0, int cols)
{
    int dim = mr->chunkDimensions == CHUNK_DIMENSIONS_2x2 ? 2 : 3;
    int tx0 = x0 >> 3, ty0 = y0 >> 3;
    int widthTiles = mr->mapSizePixels.x / 8, heightTiles = mr->mapSizePixels.y / 8;
    const u16 *chunks = mr->chunkMappings[layer];
    const u16 *tiles = mr->tileMappings;
    uint16_t *shadow = sShadow[bg];
    int r, c;

    if (chunks == NULL || tiles == NULL || widthTiles <= 0 || heightTiles <= 0) {
        for (r = 0; r < 32 * SHADOW_COLS; r++)
            shadow[r] = BLANK_ENTRY;
        return;
    }
    for (r = 0; r < 32; r++) {
        int ty = ty0 + r;
        uint16_t *row = shadow + (ty & 31) * SHADOW_COLS;
        int wy = ty;
        int cy, sy;
        if (mr->wrapAround) {
            wy %= heightTiles;
            if (wy < 0)
                wy += heightTiles;
        }
        cy = FloorDiv(wy, dim);
        sy = wy - cy * dim;
        for (c = 0; c < cols; c++) {
            int tx = tx0 + c, wx = tx, cx, sx;
            if (mr->wrapAround) {
                wx %= widthTiles;
                if (wx < 0)
                    wx += widthTiles;
            }
            if (wx < 0 || wy < 0 || wx >= widthTiles || wy >= heightTiles || cy >= mr->heightChunks ||
                (sPictureMap && (ty * 8 < mr->cameraPos.y || ty * 8 >= mr->cameraPos.y + PORT_GBA_H))) {
                row[c] = BLANK_ENTRY;
                continue;
            }
            cx = FloorDiv(wx, dim);
            sx = wx - cx * dim;
            if (cx >= mr->widthChunks || cx >= 64) {
                row[c] = BLANK_ENTRY;
                continue;
            }
            row[c] = tiles[chunks[cy * 64 + cx] * 9 + sy * dim + sx];
        }
    }
}

/* A 64x64-tile picture kept in RAM (world map, friend areas map). */
static void FillPicture(int bg, const u16 (*map)[64], int x0, int y0, int cols)
{
    int tx0 = x0 >> 3, ty0 = y0 >> 3;
    uint16_t *shadow = sShadow[bg];
    int r, c;
    for (r = 0; r < 32; r++) {
        int ty = ty0 + r;
        uint16_t *row = shadow + (ty & 31) * SHADOW_COLS;
        for (c = 0; c < cols; c++) {
            int tx = tx0 + c;
            row[c] = (tx >= 0 && ty >= 0 && tx < PICTURE_W / 8 && ty < PICTURE_H / 8) ? map[ty][tx] : BLANK_ENTRY;
        }
    }
}

/* UI layer (BG0 / BG1): the VRAM tilemap inside the GBA screen rectangle,
 * `outside` beyond it. */
static void FillUi(int bg, uint16_t outside, int cols)
{
    const uint16_t *vram = (const uint16_t *)(gHostVram + 0x6000 + bg * 0x800);
    int hofs = IO16(0x10 + bg * 4) & 0x1FF, vofs = IO16(0x12 + bg * 4) & 0x1FF;
    int fine = (hofs - sDx) & 7;
    uint16_t *shadow = sShadow[bg];
    int r, c;

    for (r = 0; r < 32; r++) {
        /* ring row r is VRAM tile row r; its top GBA line is 8r - vofs */
        uint16_t *row = shadow + r * SHADOW_COLS;
        int gy = ((8 * r - vofs) % 256 + 256) % 256;
        int rowVisible = gy < PORT_GBA_H || gy > 248;
        for (c = 0; c < cols; c++) {
            int gx = 8 * c - fine - sDx; /* GBA x of the column's first pixel */
            int vcol = ((gx + hofs) >> 3) & 31;
            /* BG0 column 0 holds the game's black edge tile (0x279), a
             * mask for the GBA screen border: not part of the picture. */
            if (!rowVisible || gx <= -8 || gx >= PORT_GBA_W || (bg == 0 && hofs == 0 && vcol == 0))
                row[c] = outside;
            else
                row[c] = vram[r * 32 + vcol];
        }
    }
}

static void ShadowSet(int bg, int on)
{
    virtuappu_mode1_ws_shadow[bg] = on ? sShadow[bg] : NULL;
    virtuappu_mode1_ws_shadow_cols[bg] = (uint8_t)SHADOW_COLS;
    virtuappu_mode1_ws_shadow_stride[bg] = (uint8_t)SHADOW_COLS;
    virtuappu_mode1_ws_shadow_base_tile[bg] = 0;
}

/* Sets the renderer's scroll of `bg` so that the window's left/top is at
 * world pixel (x0, y0): the game's register plus the port's shift. */
static void WorldScroll(int bg, int x0, int y0)
{
    int hofs = IO16(0x10 + bg * 4) & 0x1FF;
    int vofs = IO16(0x12 + bg * 4) & 0x1FF;
    virtuappu_mode1_bg_dx[bg] = ((x0 - hofs) & 0x1FF);
    virtuappu_mode1_bg_dy[bg] = ((y0 - vofs) & 0x1FF);
}

static void DumpState(void)
{
#ifndef __vita__
    static int dumpFrame = -2;
    int i;
    if (dumpFrame == -2) {
        const char *e = getenv("PMD_VIEW_DUMP");
        dumpFrame = e ? atoi(e) : -1;
    }
    if ((int)Port_FrameCounter() != dumpFrame)
        return;
    Platform_Log("view: scene=%d size=%dx%d content=%dx%d dx=%d dy=%d\n", sSceneLatched, sWidth, sHeight, sContentW, sContentH, sDx, sDy);
    for (i = 0; i < 128; i++) {
        u16 a0 = *(u16 *)(gHostOam + i * 8), a1 = *(u16 *)(gHostOam + i * 8 + 2);
        if ((a0 & 0x300) == 0x200)
            continue;
        Platform_Log("oam %3d y=%3d x=%3d base=%d,%d\n", i, a0 & 0xFF, a1 & 0x1FF, sSlotX[i], sSlotY[i]);
    }
#endif
}

/* Called right before a frame is rendered. */
/* Scenes without a map (title, menus): the game's tilemaps hold rows
 * beyond the 160 GBA lines (scroll effects such as the title screen's
 * sea), which must not show above or below the GBA picture. */
void Port_ViewBlankRows(int *top, int *bottom)
{
#ifndef __vita__
    if (getenv("PMD_DARK_DEBUG") && Port_FrameCounter() % 300 == 0)
    {
        uint16_t d1, d2, d3, dispstat;
        memcpy(&d1, gHostIo + 0x0C6, 2); memcpy(&d2, gHostIo + 0x0D2, 2); memcpy(&d3, gHostIo + 0x0DE, 2); memcpy(&dispstat, gHostIo + 4, 2);
        if (gGroundMapDungeon_3001B70 != NULL) {
            const MapRender *mr = &gGroundMapDungeon_3001B70->mapRender[0];
            Platform_Log("blank: dispcnt %04x map %dx%d chunks %dx%d dim %d wrap %d cam %d,%d ctx %d bgs %d first %d\n", IO16(0),
                         mr->mapSizePixels.x, mr->mapSizePixels.y, mr->widthChunks, mr->heightChunks, mr->chunkDimensions, mr->wrapAround,
                         mr->cameraPos.x, mr->cameraPos.y, gGroundMapDungeon_3001B70->unk474, mr->numBgs, mr->unk2);
        }
        Platform_Log("blank: active %d scene %d content %dx%d dx %d dy %d dma %04x %04x %04x dispstat %04x bpc %s\n", sActive, sSceneLatched, sContentW, sContentH, sDx, sDy, d1, d2, d3, dispstat,
                     (gGroundMapDungeon_3001B70 && gGroundMapDungeon_3001B70->bpcFile && gGroundMapDungeon_3001B70->bpcFile->file) ? (const char *)gGroundMapDungeon_3001B70->bpcFile->file->name : "-");
    }
#endif
    /* a scene without a map keeps the frame as content (the picture is
     * centred, the renderer wraps the tilemaps outside): only the 160 GBA
     * rows may show */
    if (!sActive) {
        *top = 0;
        *bottom = 0;
        return;
    }
    /* per-line effects (HBlank DMA: the title screen's sea) exist for the
     * 160 GBA lines only: the rows outside would show the map without them */
    if (sSceneLatched != PORT_SCENE_NONE && sContentH > PORT_GBA_H && !HBlankDmaActive()) {
        *top = 0;
        *bottom = 0;
        return;
    }
    *top = sDy;
    *bottom = sDy + PORT_GBA_H;
}

void Port_ViewBeforeRender(int *width, int *height)
{
    int i, cols;

    virtuappu_mode1_ws_full_view = 0;
    for (i = 0; i < 4; i++)
        ShadowSet(i, 0);
    if (!sActive) {
        virtuappu_mode1_obj_port = 0;
        virtuappu_mode1_win_dx = virtuappu_mode1_win_dy = 0;
        virtuappu_mode1_line_dy = 0;
        for (i = 0; i < 4; i++)
            virtuappu_mode1_bg_dx[i] = virtuappu_mode1_bg_dy[i] = 0;
        *width = PORT_GBA_W;
        *height = PORT_GBA_H;
        return;
    }

    cols = sWidth / 8 + 2;
    if (cols > SHADOW_COLS)
        cols = SHADOW_COLS;
    virtuappu_mode1_ws_full_view = 1;

    /* UI layers at the GBA screen position */
    for (i = 0; i < 2; i++) {
        virtuappu_mode1_bg_dx[i] = (-sDx) & 0x1FF;
        virtuappu_mode1_bg_dy[i] = (-sDy) & 0x1FF;
    }
    FillUi(0, BLANK_ENTRY, cols);
    /* BG1 carries the darkness: the game hides it inside its window. A
     * window over the whole GBA screen (a lit floor, a ground scene) means
     * nothing is dark, so the margins get no dark tile either. */
    FillUi(1, DarknessActive() ? DARK_ENTRY : BLANK_ENTRY, cols);
    ShadowSet(0, 1);
    ShadowSet(1, 1);

    if (sContentW == PORT_GBA_W && sContentH == PORT_GBA_H) {
        /* no map around the GBA screen: the whole picture is UI */
        for (i = 2; i < 4; i++) {
            virtuappu_mode1_bg_dx[i] = (-sDx) & 0x1FF;
            virtuappu_mode1_bg_dy[i] = (-sDy) & 0x1FF;
            FillUi(i, BLANK_ENTRY, cols);
            ShadowSet(i, 1);
        }
    } else if (sSceneLatched == PORT_SCENE_DUNGEON && gDungeon != NULL) {
        const Dungeon *dungeon = gDungeon;
        int x0 = dungeon->unk181e8.cameraPixelPos.x - sDx;
        int y0 = dungeon->unk181e8.cameraPixelPos.y - (int)dungeon->unk181e8.unk181FC - sDy;
        FillDungeon(3, x0, y0, cols);
        ShadowSet(3, 1);
        WorldScroll(3, x0, y0);
        /* BG2: grid / effects made for the GBA screen: no shift, clipped
         * to the GBA rectangle by showing it as a UI layer */
        FillUi(2, BLANK_ENTRY, cols);
        ShadowSet(2, 1);
        virtuappu_mode1_bg_dx[2] = (-sDx) & 0x1FF;
        virtuappu_mode1_bg_dy[2] = (-sDy) & 0x1FF;
    } else if (sSceneLatched == PORT_SCENE_WORLDMAP || sSceneLatched == PORT_SCENE_FRIENDMAP) {
        const u16 (*bg2)[64], (*bg3)[64];
        int x0, y0;
        if (sSceneLatched == PORT_SCENE_WORLDMAP) {
            struct WorldMap *m = Port_WorldMap();
            bg2 = m->unk3114;
            bg3 = m->unk1114;
            x0 = m->bgPos.x - sDx;
            y0 = m->bgPos.y - sDy;
        } else {
            bg2 = gFriendAreasMapPtr->unk2014;
            bg3 = gFriendAreasMapPtr->unk14;
            x0 = gFriendAreasMapPtr->bgPos.x - sDx;
            y0 = gFriendAreasMapPtr->bgPos.y - sDy;
        }
        FillPicture(2, bg2, x0, y0, cols);
        FillPicture(3, bg3, x0, y0, cols);
        ShadowSet(2, 1);
        ShadowSet(3, 1);
        WorldScroll(2, x0, y0);
        WorldScroll(3, x0, y0);
    } else if (sSceneLatched == PORT_SCENE_GROUND && gGroundMapDungeon_3001B70 != NULL) {
        GroundBg *g = gGroundMapDungeon_3001B70;
        int ctx, k, bg2Done = 0;
        for (ctx = 0; ctx < g->unk474; ctx++) {
            const MapRender *mr = &g->mapRender[ctx];
            for (k = 0; k < mr->numBgs; k++) {
                int layer = mr->unk2 + k;
                int bg = 2 + g->unk52C.unkA + layer;
                int x0, y0;
                if (bg < 2 || bg > 3)
                    continue;
                x0 = mr->cameraPos.x - sDx;
                y0 = mr->cameraPos.y - sDy;
                FillGround(bg, mr, k, x0, y0, cols);
                ShadowSet(bg, 1);
                WorldScroll(bg, x0, y0);
                if (bg == 2)
                    bg2Done = 1;
            }
        }
        /* the weather (cloud shadows) is a second ground map with its own
         * camera, drawn by the game for the GBA screen only */
        if (!bg2Done && gUnknown_3001B78 != NULL) {
            GroundBg *wg = gUnknown_3001B78;
            for (ctx = 0; ctx < wg->unk474; ctx++) {
                const MapRender *mr = &wg->mapRender[ctx];
                for (k = 0; k < mr->numBgs; k++) {
                    int layer = mr->unk2 + k;
                    int bg = 2 + wg->unk52C.unkA + layer;
                    int x0, y0;
                    if (bg != 2)
                        continue;
                    x0 = mr->cameraPos.x - sDx;
                    y0 = mr->cameraPos.y - sDy;
                    FillGround(bg, mr, k, x0, y0, cols);
                    ShadowSet(bg, 1);
                    WorldScroll(bg, x0, y0);
                    bg2Done = 1;
                }
            }
        }
        if (!bg2Done) {
            /* other BG2 use: the VRAM tilemap, moved with the world */
            virtuappu_mode1_bg_dx[2] = (-sDx) & 0x1FF;
            virtuappu_mode1_bg_dy[2] = (-sDy) & 0x1FF;
        }
    }

    virtuappu_mode1_win_dx = sDx;
    virtuappu_mode1_win_dy = sDy;
    virtuappu_mode1_line_dy = sDy;
    virtuappu_mode1_obj_port = 1;
    for (i = 0; i < 128; i++) {
        virtuappu_mode1_obj_dx[i] = (int16_t)sDx;
        virtuappu_mode1_obj_dy[i] = (int16_t)sDy;
        virtuappu_mode1_obj_base_x[i] = sSlotX[i];
        virtuappu_mode1_obj_base_y[i] = sSlotY[i];
        /* a slot the game's sprite system did not fill this frame (or the
         * one before) keeps GBA rules: parked below line 160 = hidden */
        virtuappu_mode1_obj_staged[i] = sSlotFrame[i] + 2 > Port_FrameCounter() + 1 && sSlotFrame[i] != 0;
    }
    DumpState();
    *width = sWidth;
    *height = sHeight;
}
