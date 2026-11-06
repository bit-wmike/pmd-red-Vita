/*
 * PS Vita platform layer of the Pokémon Mystery Dungeon: Red Rescue Team port.
 *
 * Video: vita2d, the frame scaled to the full screen height; styles smooth,
 *        sharp (integer pre-scale, then smooth down-scale) and pixel (integer).
 *        Around a game image with borders (pixel style, GBA view) either the
 *        darkened LiveArea background or a bezel graphic is drawn ("Rahmen").
 * Menu:  START + SELECT (view, display, frame, frame rate, controls, quick
 *        save, cheats), saved to settings.ini.
 * Audio: a thread feeding sceAudioOut from a ring buffer (48 kHz stereo).
 * Input: buttons and left stick mapped to the GBA keys.
 * Files: ux0:data/pmd/ - the ROM (any *.gba of the US version), the save
 *        (pmd_red.sav, 128 KiB, compatible with GBA emulators), settings,
 *        quick save, the ROM index and log.txt.
 */
#include <psp2/apputil.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/touch.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/system_param.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

#include <pthread.h>
#include <sched.h>

#include "port/port.h"
#include "port/port_text.h"
#include "port/port_assets.h"
#include "port/port_view.h"
#include "port/port_config.h"
#include "port/port_game.h"
#include "port/port_panel.h"
#include "cpu/mode1.h"

#define DATA_DIR "ux0:data/pmd"
#define SAVE_PATH DATA_DIR "/pmd_red.sav"
#define LOG_PATH DATA_DIR "/log.txt"

#define KEY_A 1
#define KEY_B 2
#define KEY_SELECT 4
#define KEY_START 8
#define KEY_RIGHT 0x10
#define KEY_LEFT 0x20
#define KEY_UP 0x40
#define KEY_DOWN 0x80
#define KEY_R 0x100
#define KEY_L 0x200

unsigned int sceUserMainThreadStackSize = 1 * 1024 * 1024;
int _newlib_heap_size_user = 160 * 1024 * 1024;

/* ---------------------------------------------------------------- log */

static FILE *sLog;

uint64_t Platform_TimeUs(void)
{
    return sceKernelGetProcessTimeWide();
}

void Platform_Log(const char *fmt, ...)
{
    va_list ap;
    if (!sLog)
        return;
    va_start(ap, fmt);
    vfprintf(sLog, fmt, ap);
    va_end(ap);
    fflush(sLog);
}

/* Watchdog (see WatchdogThread): a game frame beat, the port menus pause it,
 * and the last platform call entered by the game thread. */
static volatile unsigned sWdBeat;
static volatile int sWdPause;
static const char *volatile sWdWhere = "start";

/* -------------------------------------------------------------- video */

static vita2d_texture *sTex;
static vita2d_texture *sSharp;
static vita2d_pgf *sFont;
/* The frame around a game image with borders: the LiveArea background of
 * the VPK (drawn darkened) and the 9-slice bezel (platform/vita/assets/
 * bezel.png, made by tools/make_bezel.py). The VPK build converts both to
 * raw RGBA files (tools/png2raw.py: the SDK's vita2d has no PNG loader).
 * Either may be NULL when its file is missing. */
static vita2d_texture *sArtwork;
static vita2d_texture *sBezel;

#define SHARP_K 3

#define ARTWORK_PATH "app0:assets/bg.raw"
#define BEZEL_PATH "app0:assets/bezel.raw"
#define ARTWORK_TINT RGBA8(100, 100, 100, 255) /* ~40 % brightness */
#define BEZEL_BORDER 48  /* width of the bezel ring (corner size in bezel.png) */
#define BEZEL_CENTER 16  /* the opening in the middle of bezel.png */
#define BEZEL_SHELL RGBA8(40, 41, 46, 255) /* the flat colour the ring fades into */

/* Loads a raw RGBA texture written by png2raw.py ('PRAW', u32 w, u32 h,
 * then R,G,B,A bytes per pixel) straight into a vita2d texture. */
static vita2d_texture *LoadRawTexture(const char *path)
{
    unsigned char head[12];
    unsigned w, h, y;
    vita2d_texture *tex;
    unsigned char *dst;
    FILE *f = fopen(path, "rb");
    if (!f) {
        Platform_Log("video: %s not found\n", path);
        return NULL;
    }
    if (fread(head, 1, sizeof(head), f) != sizeof(head) || memcmp(head, "PRAW", 4) != 0) {
        Platform_Log("video: %s is not a raw texture\n", path);
        fclose(f);
        return NULL;
    }
    w = head[4] | head[5] << 8 | head[6] << 16 | (unsigned)head[7] << 24;
    h = head[8] | head[9] << 8 | head[10] << 16 | (unsigned)head[11] << 24;
    if (w == 0 || h == 0 || w > 2048 || h > 2048 ||
        !(tex = vita2d_create_empty_texture_format(w, h, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR))) {
        Platform_Log("video: %s: bad size %ux%u\n", path, w, h);
        fclose(f);
        return NULL;
    }
    dst = (unsigned char *)vita2d_texture_get_datap(tex);
    for (y = 0; y < h; y++) {
        if (fread(dst + y * vita2d_texture_get_stride(tex), 1, (size_t)w * 4, f) != (size_t)w * 4) {
            Platform_Log("video: %s is truncated\n", path);
            vita2d_free_texture(tex);
            fclose(f);
            return NULL;
        }
    }
    fclose(f);
    return tex;
}

static void VideoInit(void)
{
    vita2d_init();
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
    vita2d_set_vblank_wait(0); /* paced by WaitDue() */
    sTex = vita2d_create_empty_texture_format(PORT_VIEW_MAX_W, PORT_VIEW_MAX_H, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    sSharp = vita2d_create_empty_texture_rendertarget(PORT_VIEW_MAX_W * SHARP_K, PORT_VIEW_MAX_H * SHARP_K,
                                                       SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
    sFont = vita2d_load_default_pgf();
    sArtwork = LoadRawTexture(ARTWORK_PATH);
    if (sArtwork)
        vita2d_texture_set_filters(sArtwork, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    sBezel = LoadRawTexture(BEZEL_PATH);
    if (sBezel && (vita2d_texture_get_width(sBezel) != 2 * BEZEL_BORDER + BEZEL_CENTER ||
                   vita2d_texture_get_height(sBezel) != 2 * BEZEL_BORDER + BEZEL_CENTER)) {
        Platform_Log("video: bezel.raw has the wrong size %ux%u\n", vita2d_texture_get_width(sBezel),
                     vita2d_texture_get_height(sBezel));
        vita2d_free_texture(sBezel);
        sBezel = NULL;
    }
    if (sBezel)
        vita2d_texture_set_filters(sBezel, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
    Platform_Log("video: artwork %s, bezel %s\n", sArtwork ? "loaded" : "missing (black)", sBezel ? "loaded" : "missing (plain)");
}

/* Fills the screen around the game rectangle (gx, gy, gw, gh) according to
 * gPortConfig.frame. Called right after the clear, before the game is drawn;
 * nothing is drawn over the game image itself. One texture draw for the
 * artwork, nine for the bezel - no per-pixel work. */
static void DrawFrame(float gx, float gy, float gw, float gh)
{
    if (gw >= 959.0f && gh >= 543.0f)
        return; /* the game fills the screen */
    if (gPortConfig.frame == PORT_FRAME_ARTWORK) {
        if (sArtwork)
            vita2d_draw_texture_tint_scale(sArtwork, 0, 0, 960.0f / vita2d_texture_get_width(sArtwork),
                                           544.0f / vita2d_texture_get_height(sArtwork), ARTWORK_TINT);
    } else if (gPortConfig.frame == PORT_FRAME_BEZEL) {
        const float b = BEZEL_BORDER, c = BEZEL_CENTER;
        vita2d_draw_rectangle(0, 0, 960, 544, BEZEL_SHELL);
        if (!sBezel)
            return;
        /* corners at 1:1 ... */
        vita2d_draw_texture_part(sBezel, gx - b, gy - b, 0, 0, b, b);
        vita2d_draw_texture_part(sBezel, gx + gw, gy - b, b + c, 0, b, b);
        vita2d_draw_texture_part(sBezel, gx - b, gy + gh, 0, b + c, b, b);
        vita2d_draw_texture_part(sBezel, gx + gw, gy + gh, b + c, b + c, b, b);
        /* ... and the edges stretched along the game rectangle */
        vita2d_draw_texture_part_scale(sBezel, gx, gy - b, b, 0, c, b, gw / c, 1.0f);
        vita2d_draw_texture_part_scale(sBezel, gx, gy + gh, b, b + c, c, b, gw / c, 1.0f);
        vita2d_draw_texture_part_scale(sBezel, gx - b, gy, 0, b, b, c, 1.0f, gh / c);
        vita2d_draw_texture_part_scale(sBezel, gx + gw, gy, b + c, b, b, c, 1.0f, gh / c);
    }
}

static int sCatchup; /* frames computed but not drawn (pacing) */

/* Frame timing for the log: work time = game logic + rendering of a frame. */
static SceUInt64 sLastPresentEnd;
static SceUInt64 sWorkSum, sWorkMax;
static int sWorkFrames, sSlowFrames;

static int FreeMemKiB(void)
{
    SceKernelFreeMemorySizeInfo info;
    info.size = sizeof(info);
    if (sceKernelGetFreeMemorySize(&info) < 0)
        return -1;
    return info.size_user / 1024;
}

static void PerfFrame(SceUInt64 start)
{
    if (sLastPresentEnd) {
        SceUInt64 work = start - sLastPresentEnd;
        sWorkSum += work;
        if (work > sWorkMax)
            sWorkMax = work;
        if (work > 16000)
            sSlowFrames++;
        if (++sWorkFrames == 300) {
            Platform_Log("[perf] frame work avg %.2f ms (render %.2f ms), max %.2f ms, >16 ms: %d of 300, "
                         "view %dx%d, catchup %d, render threads %d, free mem %d KiB\n",
                         sWorkSum / 300.0 / 1000.0, Port_ProfileAverageUs(PORT_PROF_RENDER) / 1000.0,
                         sWorkMax / 1000.0, sSlowFrames, Port_ViewWidth(), Port_ViewHeight(), sCatchup,
                         1 + virtuappu_mode1_worker_count(), FreeMemKiB());
            sCatchup = 0;
            sWorkSum = sWorkMax = 0;
            sWorkFrames = sSlowFrames = 0;
        }
    }
}

/* Frame pacing: logic frame n is due at display vblank sAnchorVcount + n.
 * When the game runs late, frames are computed without drawing (at most two
 * in a row) so the game keeps its speed; after a long stall (loading, menu)
 * the schedule is re-anchored instead of catching up. */
static unsigned sAnchorVcount, sAnchorFrame;
static int sAnchored, sSkipRun;

static int FrameLate(void)
{
    unsigned due = sAnchorVcount + (Port_FrameCounter() - sAnchorFrame);
    return (int)(sceDisplayGetVcount() - due);
}

static void Reanchor(void)
{
    sAnchorVcount = sceDisplayGetVcount();
    sAnchorFrame = Port_FrameCounter();
    sAnchored = 1;
}

int Platform_PaceAllowsRender(void)
{
    int late;
    if (Port_TurboActive()) {
        /* fast forward: draw every fourth frame, never wait */
        sAnchored = 0;
        return (Port_FrameCounter() & 3) == 0;
    }
    if (!sAnchored)
        Reanchor();
    late = FrameLate();
    if (late > 8) {
        Reanchor();
        late = 0;
    }
    if (late >= (gPortConfig.frameRate30 ? 3 : 2) && sSkipRun < 2) {
        sSkipRun++;
        sCatchup++;
        return 0;
    }
    sSkipRun = 0;
    return 1;
}

static void WaitDue(void)
{
    if (Port_TurboActive())
        return;
    while (FrameLate() < 0)
        sceDisplayWaitVblankStart();
}


/* ------------------------------------------------------------ panels */

/* Panels in the screen's margins beside the game image (settings
 * "side_panels"): right the clock, play time and the dungeon's status,
 * left the floor bar of the dungeon with the floors of accepted jobs. The
 * data comes from port/port_panel.c (filled at VBlank on this thread). A
 * short tap on the left margin opens the job overview (RunJobOverview). */
#define PANEL_MIN_W 44 /* narrower margins: no panels (smooth/sharp fill the width) */
#define PANEL_BG RGBA8(0, 0, 0, 150)
#define PANEL_TEXT RGBA8(235, 235, 235, 255)
#define PANEL_DIM RGBA8(150, 150, 150, 255)
#define PANEL_HEAD RGBA8(255, 205, 90, 255)
#define PANEL_OPEN RGBA8(255, 140, 40, 255)
#define PANEL_DONE RGBA8(90, 220, 90, 255)
#define PANEL_ITEM RGBA8(250, 220, 70, 255)

static float sGameX, sGameY, sGameW = 960, sGameH = 544; /* the game image on screen (last DrawGame) */

/* Text centred in [x, x+w) with its baseline at y, scaled down to fit. */
static void PanelText(float x, float w, int y, unsigned color, float scale, const char *text)
{
    int tw = vita2d_pgf_text_width(sFont, scale, text);
    if (tw > w - 2 && tw > 0) {
        scale *= (w - 2) / tw;
        tw = vita2d_pgf_text_width(sFont, scale, text);
    }
    vita2d_pgf_draw_text(sFont, (int)(x + (w - tw) / 2), y, color, scale, text);
}

/* Words wrapped to lines of width w, centred; returns the y after the text. */
static int PanelWrap(float x, float w, int y, unsigned color, float scale, int lineH, const char *text, int maxLines)
{
    char line[64];
    const char *p = text;
    int lines = 0;
    while (*p && lines < maxLines) {
        int len = 0, best = 0;
        /* longest run of whole words that fits; at least one word */
        for (;;) {
            int end = len;
            while (p[end] == ' ')
                end++;
            while (p[end] && p[end] != ' ')
                end++;
            if (end == len || end >= (int)sizeof(line))
                break;
            memcpy(line, p, (size_t)end);
            line[end] = 0;
            if (best > 0 && vita2d_pgf_text_width(sFont, scale, line) > w - 2)
                break;
            best = len = end;
            if (!p[end])
                break;
        }
        if (best == 0)
            break;
        memcpy(line, p, (size_t)best);
        line[best] = 0;
        PanelText(x, w, y, color, scale, line);
        y += lineH;
        lines++;
        p += best;
        while (*p == ' ')
            p++;
    }
    return y;
}

static void DrawCheck(float x, float y, float s, unsigned color)
{
    /* a tick in the box (x, y, s, s), two pixels thick */
    float i;
    for (i = 0; i < 2; i++) {
        vita2d_draw_line(x + s * 0.12f, y + s * 0.55f + i, x + s * 0.4f, y + s * 0.85f + i, color);
        vita2d_draw_line(x + s * 0.4f, y + s * 0.85f + i, x + s * 0.9f, y + s * 0.15f + i, color);
    }
}

static void DrawRightPanel(const struct PortPanel *p, float x, float w)
{
    char buf[48];
    int y = 30;
    vita2d_draw_rectangle(x + 3, 4, w - 6, 536, PANEL_BG);
    x += 3;
    w -= 6;
    PanelText(x, w, y, PANEL_TEXT, 1.05f, p->clock);
    y += 18;
    PanelText(x, w, y, PANEL_DIM, 0.7f, p->playTime);
    y += 24;
    if (p->inDungeon) {
        /* the dungeon's name is in the frame above the game, the floor in
         * the floor bar on the left (Mike) */
        vita2d_draw_rectangle(x + 4, y - 10, w - 8, 1, PANEL_DIM);
        y += 8;
        if (p->maxBelly > 0) {
            PanelText(x, w, y, PANEL_DIM, 0.6f, "Bauch");
            y += 17;
            snprintf(buf, sizeof(buf), "%d/%d", p->belly, p->maxBelly);
            PanelText(x, w, y, p->belly * 5 <= p->maxBelly ? PANEL_OPEN : PANEL_TEXT, 0.8f, buf);
            y += 26;
        }
        if (p->weather[0]) {
            PanelText(x, w, y, PANEL_DIM, 0.6f, "Wetter");
            y += 17;
            PanelText(x, w, y, PANEL_TEXT, 0.7f, p->weather);
            y += 26;
        }
    } else {
        y += 8;
    }
    /* the bag instead of the money (Mike: the bag is what needs managing) */
    PanelText(x, w, y, PANEL_DIM, 0.6f, "Beutel");
    y += 17;
    snprintf(buf, sizeof(buf), "%d/%d", p->bagCount, p->bagMax);
    PanelText(x, w, y, p->bagCount >= p->bagMax ? PANEL_OPEN : p->bagCount >= p->bagMax - 2 ? PANEL_HEAD : PANEL_TEXT,
              0.8f, buf);
}

static void DrawFloorBar(const struct PortPanel *p, float x, float w)
{
    char buf[16];
    int n = p->floorCount, f, top = 46, bottom = 482;
    float rowH, avail = (float)(bottom - top);
    PanelText(x, w, 30, PANEL_HEAD, 0.6f, "Etagen");
    if (p->floorMarks[0]) {
        /* an item to find: anywhere in the dungeon */
        vita2d_pgf_draw_text(sFont, (int)x + 6, 518 - 22, PANEL_TEXT, 0.55f, "Item");
        if (p->floorMarks[0] & PORT_FLOOR_OPEN) {
            vita2d_draw_fill_circle(x + w - 9, 518 - 27, 5, PANEL_OPEN);
        } else if (p->floorMarks[0] & PORT_FLOOR_DONE) {
            DrawCheck(x + w - 15, 518 - 33, 11, PANEL_DONE);
        } else {
            vita2d_draw_fill_circle(x + w - 9, 518 - 27, 5, PANEL_DIM);
            vita2d_draw_fill_circle(x + w - 9, 518 - 27, 3, RGBA8(20, 20, 20, 255));
        }
    }
    if (n < 1)
        return;
    rowH = avail / n;
    if (rowH > 36)
        rowH = 36;
    for (f = 1; f <= n; f++) {
        /* deeper is lower: floor 1 at the top going down, at the bottom going up */
        int slot = p->stairsUp ? n - f : f - 1;
        float ry = top + slot * rowH, rh = rowH > 3 ? rowH - 1 : rowH;
        unsigned fill = f == p->floor ? RGBA8(250, 200, 70, 230) : f < p->floor ? RGBA8(90, 90, 100, 200) : RGBA8(45, 45, 55, 200);
        float mx = x + w - 13;
        vita2d_draw_rectangle(x + 4, ry, w - 8, rh, fill);
        if (rowH >= 11 && (n <= 30 || f == 1 || f == p->floor || f % 5 == 0)) {
            snprintf(buf, sizeof(buf), "%d", f);
            vita2d_pgf_draw_text(sFont, (int)x + 7, (int)(ry + rh / 2 + 5 * (rowH >= 16 ? 1.0f : 0.7f)),
                                 f == p->floor ? RGBA8(20, 20, 20, 255) : PANEL_TEXT, rowH >= 16 ? 0.62f : 0.45f, buf);
        }
        if (p->floorMarks[f] & PORT_FLOOR_OPEN) {
            if (rowH >= 8)
                vita2d_draw_fill_circle(mx + 4, ry + rh / 2, rowH >= 14 ? 5.0f : 3.0f, PANEL_OPEN);
            else
                vita2d_draw_rectangle(x + w - 10, ry, 6, rh, PANEL_OPEN);
        } else if (p->floorMarks[f] & PORT_FLOOR_DONE) {
            if (rowH >= 10)
                DrawCheck(mx - 1, ry + rh / 2 - 5, 11, PANEL_DONE);
            else
                vita2d_draw_rectangle(x + w - 10, ry, 6, rh, PANEL_DONE);
        } else if (p->floorMarks[f] & PORT_FLOOR_INACTIVE) {
            /* a job here that is not taken in the job list: a grey ring */
            if (rowH >= 8) {
                vita2d_draw_fill_circle(mx + 4, ry + rh / 2, rowH >= 14 ? 5.0f : 3.0f, PANEL_DIM);
                vita2d_draw_fill_circle(mx + 4, ry + rh / 2, rowH >= 14 ? 3.0f : 1.5f, fill | 0xFF000000u);
            }
        }
    }
    /* the current floor: a marker beside the bar */
    if (p->floor >= 1 && p->floor <= n) {
        int slot = p->stairsUp ? n - p->floor : p->floor - 1;
        float cy = top + slot * rowH + (rowH - 1) / 2;
        vita2d_draw_rectangle(x, cy - 3, 3, 6, RGBA8(250, 200, 70, 255));
    }
}

static void DrawLeftPanel(const struct PortPanel *p, float x, float w)
{
    char buf[32];
    vita2d_draw_rectangle(x + 3, 4, w - 6, 536, PANEL_BG);
    x += 3;
    w -= 6;
    if (p->inDungeon) {
        DrawFloorBar(p, x, w);
    } else {
        int i, open = 0, done = 0, y = 30;
        for (i = 0; i < p->jobCount; i++) {
            if (p->jobs[i].status == PORT_JOBST_OPEN || p->jobs[i].status == PORT_JOBST_ITEM)
                open++;
            else if (p->jobs[i].status != PORT_JOBST_SUSPENDED)
                done++;
        }
        PanelText(x, w, y, PANEL_HEAD, 0.6f, "Jobs");
        y += 30;
        snprintf(buf, sizeof(buf), "%d/8", p->jobCount);
        PanelText(x, w, y, PANEL_TEXT, 0.9f, buf);
        y += 30;
        if (open) {
            vita2d_draw_fill_circle(x + 12, y - 5, 5, PANEL_OPEN);
            snprintf(buf, sizeof(buf), "%d", open);
            vita2d_pgf_draw_text(sFont, (int)x + 24, y, PANEL_TEXT, 0.75f, buf);
            y += 24;
        }
        if (done) {
            DrawCheck(x + 6, y - 11, 12, PANEL_DONE);
            snprintf(buf, sizeof(buf), "%d", done);
            vita2d_pgf_draw_text(sFont, (int)x + 24, y, PANEL_TEXT, 0.75f, buf);
        }
    }
    /* the tap target's label at the bottom */
    PanelText(x, w, 516, PANEL_DIM, 0.5f, "Tippen:");
    PanelText(x, w, 530, PANEL_DIM, 0.5f, "Jobs");
}

/* After the game image: the panels when both margins are wide enough. */
static void DrawPanels(void)
{
    const struct PortPanel *p = Port_PanelGet();
    float left = sGameX, right = 960.0f - (sGameX + sGameW);
    int on = gPortConfig.sidePanels && left >= PANEL_MIN_W && right >= PANEL_MIN_W;
    Port_PanelEnable(on);
    gPortPanelsVisible = on;
    if (!on || p->serial == 0)
        return;
    DrawLeftPanel(p, 0, left);
    DrawRightPanel(p, sGameX + sGameW, right);
    if (p->inDungeon && p->dungeonName[0] && sGameY >= 20) {
        /* the dungeon's name centred in the frame above the game (room for
         * the long names of later dungeons) */
        float scale = sGameY >= 30 ? 0.95f : 0.75f;
        int tw = vita2d_pgf_text_width(sFont, scale, p->dungeonName);
        int tx = (int)(sGameX + (sGameW - tw) / 2);
        vita2d_draw_rectangle((float)tx - 10, 3, (float)tw + 20, sGameY - 6, PANEL_BG);
        vita2d_pgf_draw_text(sFont, tx, (int)(sGameY / 2 + 8 * scale), PANEL_HEAD, scale, p->dungeonName);
    }
}

static int sLastW = 240, sLastH = 160, sLastZoom = 160;

static void DrawGame(void)
{
    int width = sLastW, height = sLastH;
    float scale = 544.0f / sLastZoom;
    float dx, dy;

    if (gPortConfig.displayStyle == PORT_STYLE_PIXEL) {
        int k = 544 / sLastZoom;
        scale = (float)(k < 1 ? 1 : k);
    }
    dx = (float)(int)((960.0f - width * scale) / 2.0f);
    dy = (float)(int)((544.0f - height * scale) / 2.0f);
    sGameX = dx;
    sGameY = dy;
    sGameW = width * scale;
    sGameH = height * scale;

    if (gPortConfig.displayStyle == PORT_STYLE_SHARP) {
        /* nearest-neighbour to 3x, then a smooth scale to the screen */
        vita2d_start_drawing_advanced(sSharp, SCE_GXM_SCENE_FRAGMENT_SET_DEPENDENCY);
        vita2d_texture_set_filters(sTex, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
        vita2d_draw_texture_part_scale(sTex, 0, 0, 0, 0, (float)width, (float)height, (float)SHARP_K, (float)SHARP_K);
        vita2d_end_drawing();
        vita2d_start_drawing_advanced(NULL, SCE_GXM_SCENE_VERTEX_WAIT_FOR_DEPENDENCY);
        vita2d_clear_screen();
        DrawFrame(dx, dy, width * scale, height * scale);
        vita2d_texture_set_filters(sSharp, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
        vita2d_draw_texture_part_scale(sSharp, dx, dy, 0, 0, (float)(width * SHARP_K), (float)(height * SHARP_K),
                                       scale / SHARP_K, scale / SHARP_K);
    } else {
        SceGxmTextureFilter f = gPortConfig.displayStyle == PORT_STYLE_SMOOTH ? SCE_GXM_TEXTURE_FILTER_LINEAR
                                                                               : SCE_GXM_TEXTURE_FILTER_POINT;
        vita2d_start_drawing();
        vita2d_clear_screen();
        DrawFrame(dx, dy, width * scale, height * scale);
        vita2d_texture_set_filters(sTex, f, f);
        vita2d_draw_texture_part_scale(sTex, dx, dy, 0, 0, (float)width, (float)height, scale, scale);
    }
    DrawPanels();
}

static void DrawToast(void)
{
    const char *msg = Port_CurrentMessage();
    if (!msg)
        return;
    vita2d_draw_rectangle(0, 500, 960, 44, RGBA8(0, 0, 0, 170));
    vita2d_pgf_draw_text(sFont, 20, 530, RGBA8(255, 255, 255, 255), 1.0f, msg);
}

void Platform_Present(const uint32_t *pixels, int width, int height, int pitch, int zoomHeight)
{
    uint8_t *dst;
    unsigned stride;
    int y;

    sWdWhere = "Platform_Present (vita2d)";
    PerfFrame(sceKernelGetProcessTimeWide());
    vita2d_wait_rendering_done();
    dst = (uint8_t *)vita2d_texture_get_datap(sTex);
    stride = vita2d_texture_get_stride(sTex);
    for (y = 0; y < height; y++)
        memcpy(dst + y * stride, pixels + y * pitch, (size_t)width * 4);
    sLastW = width;
    sLastH = height;
    sLastZoom = zoomHeight;

    vita2d_pool_reset();
    DrawGame();
    DrawToast();
    vita2d_end_drawing();
    WaitDue();
    vita2d_swap_buffers();
    sLastPresentEnd = sceKernelGetProcessTimeWide();
    sWdWhere = "game logic (after present)";
}

static void ShowMessage(const char *title, const char *text)
{
    for (;;) {
        SceCtrlData pad;
        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_pgf_draw_text(sFont, 40, 80, RGBA8(255, 120, 60, 255), 1.4f, title);
        vita2d_pgf_draw_text(sFont, 40, 140, RGBA8(230, 230, 230, 255), 1.0f, text);
        vita2d_pgf_draw_text(sFont, 40, 500, RGBA8(150, 150, 150, 255), 0.9f, "START: beenden / quit");
        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START)
            break;
    }
}

static void ShowProgress(const char *text)
{
    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_pgf_draw_text(sFont, 40, 80, RGBA8(255, 200, 80, 255), 1.4f, "Mystery Dungeon Vita " PMD_PORT_VERSION);
    vita2d_pgf_draw_text(sFont, 40, 140, RGBA8(230, 230, 230, 255), 1.0f, text);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

/* -------------------------------------------------------------- audio */

#define AUDIO_GRAIN 256
#define RING_FRAMES 8192 /* stereo frames, power of two */

static int16_t sRing[RING_FRAMES * 2];
static volatile uint32_t sRingWrite, sRingRead;
static int sAudioPort = -1;
static int16_t sHoldL, sHoldR;

void Platform_SubmitAudio(const int16_t *samples, int frames)
{
    int i;
    uint32_t w = sRingWrite;
    if (Port_TurboActive())
        return; /* fast forward: no sound */
    for (i = 0; i < frames; i++) {
        if (w - sRingRead >= RING_FRAMES - 1)
            break; /* full: drop the rest */
        sRing[(w & (RING_FRAMES - 1)) * 2] = samples[i * 2];
        sRing[(w & (RING_FRAMES - 1)) * 2 + 1] = samples[i * 2 + 1];
        w++;
    }
    sRingWrite = w;
}

static int AudioThread(SceSize args, void *argp)
{
    static int16_t buf[AUDIO_GRAIN * 2];
    (void)args; (void)argp;
    for (;;) {
        uint32_t r = sRingRead;
        int i;
        for (i = 0; i < AUDIO_GRAIN; i++) {
            if (r == sRingWrite) {
                /* underrun: hold the last sample (no click) */
                buf[i * 2] = i ? buf[i * 2 - 2] : sHoldL;
                buf[i * 2 + 1] = i ? buf[i * 2 - 1] : sHoldR;
            } else {
                buf[i * 2] = sRing[(r & (RING_FRAMES - 1)) * 2];
                buf[i * 2 + 1] = sRing[(r & (RING_FRAMES - 1)) * 2 + 1];
                r++;
            }
        }
        sRingRead = r;
        sHoldL = buf[AUDIO_GRAIN * 2 - 2];
        sHoldR = buf[AUDIO_GRAIN * 2 - 1];
        sceAudioOutOutput(sAudioPort, buf);
    }
    return 0;
}

static void AudioInit(void)
{
    SceUID th;
    sAudioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_GRAIN, PORT_AUDIO_RATE,
                                     SCE_AUDIO_OUT_MODE_STEREO);
    if (sAudioPort < 0) {
        Platform_Log("audio: open port failed 0x%08x\n", sAudioPort);
        return;
    }
    {
        int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
        sceAudioOutSetVolume(sAudioPort, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
    }
    th = sceKernelCreateThread("pmd_audio", AudioThread, 0x10000100, 0x10000, 0, 0, NULL);
    if (th >= 0)
        sceKernelStartThread(th, 0, NULL);
}

/* -------------------------------------------------------------- input */

/* ---- menu (START + SELECT) ---- */

static int Wrap(int v, int d, int n);

static const char *const sLangNames[PORT_LANG_COUNT] = {"Englisch", "Deutsch", "Franzoesisch", "Italienisch", "Spanisch"};

/* First start with a European ROM: the language is asked once; later it is
 * changed in the port menu only, so that it never gets in the way. */
static void ChooseLanguage(void)
{
    static const char *const names[PORT_LANG_COUNT] = {"English", "Deutsch", "Fran\xc3\xa7" "ais", "Italiano", "Espa\xc3\xb1ol"};
    int sel = PORT_LANG_DE;
    unsigned prev = ~0u;
    Platform_Log("[ui] language chooser\n");
    for (;;) {
        SceCtrlData pad;
        unsigned pressed;
        int i, done = 0;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        pressed = pad.buttons & ~prev;
        prev = pad.buttons;
        if (pressed & SCE_CTRL_UP) sel = Wrap(sel, -1, PORT_LANG_COUNT);
        if (pressed & SCE_CTRL_DOWN) sel = Wrap(sel, 1, PORT_LANG_COUNT);
        if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_START)) done = 1;
        vita2d_pool_reset();
        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_pgf_draw_text(sFont, 60, 60, RGBA8(255, 200, 80, 255), 1.3f,
                             "Pokemon Mystery Dungeon: Red Rescue Team - Vita " PMD_PORT_VERSION);
        vita2d_pgf_draw_text(sFont, 80, 130, RGBA8(220, 220, 220, 255), 1.0f, "Sprache / Language / Langue / Lingua / Idioma");
        for (i = 0; i < PORT_LANG_COUNT; i++)
            vita2d_pgf_draw_text(sFont, 100, 190 + i * 40, i == sel ? RGBA8(255, 230, 80, 255) : RGBA8(200, 200, 200, 255), 1.1f, names[i]);
        vita2d_pgf_draw_text(sFont, 80, 190 + sel * 40, RGBA8(255, 230, 80, 255), 1.1f, ">");
        vita2d_pgf_draw_text(sFont, 60, 500, RGBA8(150, 150, 150, 255), 0.85f,
                             "Spaeter im Port-Menue (START + SELECT) aenderbar  /  can be changed in the port menu later");
        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
        sceDisplayWaitVblankStart();
        if (done)
            break;
    }
    gPortConfig.language = sel;
    Port_ConfigSave();
    Platform_Log("[ui] language chosen: %d\n", sel);
}

/* The port menu: sections with a heading, scrolled when longer than the
 * screen. The view is always the Full View (424x240); only the display
 * style can be chosen. */
enum {
    MI_H_DISPLAY, MI_STYLE, MI_FRAME, MI_FPS, MI_LANG,
    MI_H_CONTROLS, MI_SQUARE, MI_TRIANGLE, MI_RSTICK, MI_TURBO,
    MI_H_COMFORT, MI_TEXT_INSTANT, MI_TEXT_AUTO, MI_BLINK, MI_MSGFAST, MI_DSPEED, MI_WALK, MI_AUTOSAVE, MI_BASESAVE,
    MI_HPBARS, MI_CLOCK, MI_PANELS, MI_MAPOVL,
    MI_H_SAVE, MI_SLOT, MI_QSAVE, MI_QLOAD,
    MI_H_CHEATS, MI_CH_EXP, MI_CH_MONEY, MI_CH_IQ, MI_CH_RECRUIT, MI_CH_STAIRS, MI_CH_HUNGER, MI_CH_BELLY,
    MI_CH_TRAPS, MI_CH_PP, MI_CH_BUY, MI_CH_SELL, MI_CH_REVEAL,
    MI_CH_ATK, MI_CH_DEF, MI_CH_SPATK, MI_CH_SPDEF, MI_CH_SPEED,
    MI_BACK, MI_COUNT
};
#define MENU_ROWS 13 /* items drawn at once */

static int IsHeading(int item)
{
    return item == MI_H_DISPLAY || item == MI_H_CONTROLS || item == MI_H_COMFORT || item == MI_H_SAVE || item == MI_H_CHEATS;
}

static const char *OnOff(int v)
{
    return v ? "an" : "aus";
}

static const char *Stat(int v)
{
    static char buf[16];
    if (v == 0)
        return "normal";
    snprintf(buf, sizeof(buf), "%+d0 %%", v);
    return buf;
}

static void MenuLine(int item, char *buf, size_t size)
{
    static const char *styles[] = {"Weich", "Scharf", "Pixelgenau"};
    static const char *frames[] = {"Aus (schwarz)", "Artwork (LiveArea-Bild, abgedunkelt)", "Bezel (Rahmengrafik)"};
    static const char *speeds[] = {"normal", "x1,5", "x2"};
    static const char *bars[] = {"aus", "nur Bosse", "alle Gegner"};
    static const char *recruit[] = {"normal", "+20 %", "+50 %"};
    switch (item) {
        case MI_H_DISPLAY: snprintf(buf, size, "-- Bild (Ansicht immer Weitsicht 424x240) --"); break;
        case MI_STYLE: snprintf(buf, size, "Bilddarstellung: %s", styles[gPortConfig.displayStyle]); break;
        case MI_FRAME: snprintf(buf, size, "Rahmen um das Spielbild: %s", frames[gPortConfig.frame]); break;
        case MI_FPS: snprintf(buf, size, "Bildrate: %s", gPortConfig.frameRate30 ? "30 (Spieltempo bleibt)" : "60"); break;
        case MI_LANG:
            if (Port_TextMultilingual())
                snprintf(buf, size, "Sprache: %s", sLangNames[gPortConfig.language < 0 ? 0 : gPortConfig.language]);
            else
                snprintf(buf, size, "Sprache: Englisch (die ROM hat nur eine Sprache)");
            break;
        case MI_H_CONTROLS: snprintf(buf, size, "-- Steuerung --"); break;
        case MI_SQUARE: snprintf(buf, size, "Viereck: %s", gPortConfig.squareThrow ? "Gegenstand werfen (L+R)" : "B"); break;
        case MI_TRIANGLE: snprintf(buf, size, "Dreieck: %s", gPortConfig.triangleMove ? "Attacke einsetzen (L+A)" : "B"); break;
        case MI_RSTICK: snprintf(buf, size, "Rechter Stick = diagonal gehen (R+Richtung): %s", OnOff(gPortConfig.rightStickDiag)); break;
        case MI_TURBO: snprintf(buf, size, "Touchscreen halten = Schnellvorlauf: %s", OnOff(gPortConfig.turbo)); break;
        case MI_H_COMFORT: snprintf(buf, size, "-- Komfort --"); break;
        case MI_TEXT_INSTANT: snprintf(buf, size, "Text sofort komplett anzeigen: %s", OnOff(gPortConfig.textInstant)); break;
        case MI_TEXT_AUTO: snprintf(buf, size, "A halten blaettert Text weiter: %s", OnOff(gPortConfig.textAuto)); break;
        case MI_BLINK: snprintf(buf, size, "Cursor blinken langsamer: %s", OnOff(gPortConfig.slowBlink)); break;
        case MI_DSPEED: snprintf(buf, size, "Dungeon-Tempo (Bewegung, Gegnerzuege): %s", speeds[gPortConfig.dungeonSpeed]); break;
        case MI_WALK: snprintf(buf, size, "Schneller laufen in Basis und Stadt: %s", OnOff(gPortConfig.walkFast)); break;
        case MI_AUTOSAVE: snprintf(buf, size, "Schnellspeichern bei jedem Etagenwechsel: %s", OnOff(gPortConfig.floorAutosave)); break;
        case MI_HPBARS: snprintf(buf, size, "HP-Balken ueber Gegnern: %s", bars[gPortConfig.hpBars]); break;
        case MI_CLOCK: snprintf(buf, size, "Uhrzeit und Spielzeit im Spielbild: %s", OnOff(gPortConfig.clockOverlay)); break;
        case MI_MSGFAST: snprintf(buf, size, "Dungeon-Meldungen schneller: %s", OnOff(gPortConfig.msgFast)); break;
        case MI_BASESAVE: snprintf(buf, size, "Schnellspeichern beim Betreten der Basis: %s", OnOff(gPortConfig.baseAutosave)); break;
        case MI_PANELS: snprintf(buf, size, "Anzeigen im Rahmen (Uhr, Status, Etagen, Missionen): %s", OnOff(gPortConfig.sidePanels)); break;
        case MI_MAPOVL: snprintf(buf, size, "Karte halbtransparent ueber dem Dungeon: %s", OnOff(gPortConfig.mapOverlay)); break;
        case MI_SLOT: snprintf(buf, size, "Schnellspeicher-Platz: %d", gPortConfig.saveSlot); break;
        case MI_CH_BELLY: snprintf(buf, size, "Bauch sinkt halb so schnell: %s", OnOff(gPortConfig.bellySlow)); break;
        case MI_CH_TRAPS: snprintf(buf, size, "Fallen von Anfang an sichtbar: %s", OnOff(gPortConfig.trapsVisible)); break;
        case MI_CH_PP: snprintf(buf, size, "AP sparen (jeder 2. Einsatz kostenlos): %s", OnOff(gPortConfig.ppSaver)); break;
        case MI_CH_BUY: snprintf(buf, size, "Laden: Kaufpreise halbiert: %s", OnOff(gPortConfig.shopBuyHalf)); break;
        case MI_CH_SELL: snprintf(buf, size, "Laden: Verkaufspreise doppelt: %s", OnOff(gPortConfig.shopSellDouble)); break;
        case MI_CH_REVEAL: snprintf(buf, size, "Etage beim Betreten aufgedeckt: %s", OnOff(gPortConfig.floorReveal)); break;
        case MI_H_SAVE: snprintf(buf, size, "-- Schnellspeicher --"); break;
        case MI_QSAVE: snprintf(buf, size, "Schnellspeichern"); break;
        case MI_QLOAD: snprintf(buf, size, "Schnellladen"); break;
        case MI_H_CHEATS: snprintf(buf, size, "-- Sanfte Cheats --"); break;
        case MI_CH_EXP: snprintf(buf, size, "Erfahrung x%d", gPortConfig.expMultiplier); break;
        case MI_CH_MONEY: snprintf(buf, size, "Geld x%d", gPortConfig.moneyMult); break;
        case MI_CH_IQ: snprintf(buf, size, "IQ x%d", gPortConfig.iqMult); break;
        case MI_CH_RECRUIT: snprintf(buf, size, "Rekrutierungschance: %s", recruit[gPortConfig.recruitBoost]); break;
        case MI_CH_STAIRS: snprintf(buf, size, "Treppe auf der Karte markiert: %s", OnOff(gPortConfig.stairsMarker)); break;
        case MI_CH_HUNGER: snprintf(buf, size, "Kein Hunger (Bauch sinkt nicht): %s", OnOff(gPortConfig.noHunger)); break;
        case MI_CH_ATK: snprintf(buf, size, "Team-Angriff: %s", Stat(gPortConfig.statAtk)); break;
        case MI_CH_DEF: snprintf(buf, size, "Team-Verteidigung: %s", Stat(gPortConfig.statDef)); break;
        case MI_CH_SPATK: snprintf(buf, size, "Team-Spezial-Angriff: %s", Stat(gPortConfig.statSpAtk)); break;
        case MI_CH_SPDEF: snprintf(buf, size, "Team-Spezial-Verteidigung: %s", Stat(gPortConfig.statSpDef)); break;
        case MI_CH_SPEED: snprintf(buf, size, "Team-Initiative: %s", Stat(gPortConfig.statSpeed)); break;
        default: snprintf(buf, size, "Zurueck zum Spiel"); break;
    }
}

static int Wrap(int v, int d, int n)
{
    return (v + d + n) % n;
}

static int Clamp(int v, int d, int lo, int hi)
{
    v += d;
    return v < lo ? lo : v > hi ? hi : v;
}

/* Changes an item; returns 1 when the menu should close. */
static int MenuChange(int item, int d)
{
    int step = d == 0 ? 1 : d; /* X on a value: next */
    switch (item) {
        case MI_LANG:
            if (Port_TextMultilingual())
                gPortConfig.language = Wrap(gPortConfig.language < 0 ? 0 : gPortConfig.language, step, PORT_LANG_COUNT);
            break;
        case MI_STYLE: gPortConfig.displayStyle = Wrap(gPortConfig.displayStyle, step, 3); break;
        case MI_FRAME: gPortConfig.frame = Wrap(gPortConfig.frame, step, 3); break;
        case MI_FPS: gPortConfig.frameRate30 ^= 1; break;
        case MI_SQUARE: gPortConfig.squareThrow ^= 1; break;
        case MI_TRIANGLE: gPortConfig.triangleMove ^= 1; break;
        case MI_RSTICK: gPortConfig.rightStickDiag ^= 1; break;
        case MI_TURBO: gPortConfig.turbo ^= 1; break;
        case MI_TEXT_INSTANT: gPortConfig.textInstant ^= 1; break;
        case MI_TEXT_AUTO: gPortConfig.textAuto ^= 1; break;
        case MI_BLINK: gPortConfig.slowBlink ^= 1; break;
        case MI_DSPEED: gPortConfig.dungeonSpeed = Wrap(gPortConfig.dungeonSpeed, step, 3); break;
        case MI_WALK: gPortConfig.walkFast ^= 1; break;
        case MI_AUTOSAVE: gPortConfig.floorAutosave ^= 1; break;
        case MI_HPBARS: gPortConfig.hpBars = Wrap(gPortConfig.hpBars, step, 3); break;
        case MI_CLOCK: gPortConfig.clockOverlay ^= 1; break;
        case MI_MSGFAST: gPortConfig.msgFast ^= 1; break;
        case MI_BASESAVE: gPortConfig.baseAutosave ^= 1; break;
        case MI_PANELS: gPortConfig.sidePanels ^= 1; break;
        case MI_MAPOVL: gPortConfig.mapOverlay ^= 1; break;
        case MI_SLOT: gPortConfig.saveSlot = 1 + Wrap(gPortConfig.saveSlot - 1, step, 3); break;
        case MI_CH_BELLY: gPortConfig.bellySlow ^= 1; break;
        case MI_CH_TRAPS: gPortConfig.trapsVisible ^= 1; break;
        case MI_CH_PP: gPortConfig.ppSaver ^= 1; break;
        case MI_CH_BUY: gPortConfig.shopBuyHalf ^= 1; break;
        case MI_CH_SELL: gPortConfig.shopSellDouble ^= 1; break;
        case MI_CH_REVEAL: gPortConfig.floorReveal ^= 1; break;
        case MI_CH_EXP: gPortConfig.expMultiplier = 1 + Wrap(gPortConfig.expMultiplier - 1, step, 4); break;
        case MI_CH_MONEY: gPortConfig.moneyMult = 1 + Wrap(gPortConfig.moneyMult - 1, step, 4); break;
        case MI_CH_IQ: gPortConfig.iqMult = 1 + Wrap(gPortConfig.iqMult - 1, step, 4); break;
        case MI_CH_RECRUIT: gPortConfig.recruitBoost = Wrap(gPortConfig.recruitBoost, step, 3); break;
        case MI_CH_STAIRS: gPortConfig.stairsMarker ^= 1; break;
        case MI_CH_HUNGER: gPortConfig.noHunger ^= 1; break;
        case MI_CH_ATK: gPortConfig.statAtk = Clamp(gPortConfig.statAtk, step, -3, 3); break;
        case MI_CH_DEF: gPortConfig.statDef = Clamp(gPortConfig.statDef, step, -3, 3); break;
        case MI_CH_SPATK: gPortConfig.statSpAtk = Clamp(gPortConfig.statSpAtk, step, -3, 3); break;
        case MI_CH_SPDEF: gPortConfig.statSpDef = Clamp(gPortConfig.statSpDef, step, -3, 3); break;
        case MI_CH_SPEED: gPortConfig.statSpeed = Clamp(gPortConfig.statSpeed, step, -3, 3); break;
        case MI_QSAVE:
            if (d == 0) { Port_RequestQuickSave(); return 1; }
            break;
        case MI_QLOAD:
            if (d == 0) { Port_RequestQuickLoad(); return 1; }
            break;
        case MI_BACK:
            if (d == 0) return 1;
            break;
    }
    return 0;
}

static int NextItem(int sel, int d)
{
    do {
        sel = Wrap(sel, d, MI_COUNT);
    } while (IsHeading(sel));
    return sel;
}

static void RunMenu(void)
{
    static int sel = MI_STYLE, top;
    unsigned prev = ~0u;
    char line[160];

    Platform_Log("[ui] menu open\n");
    sWdPause = 1;
    for (;;) {
        SceCtrlData pad;
        unsigned pressed;
        int i, close = 0;

        sceCtrlPeekBufferPositive(0, &pad, 1);
        pressed = pad.buttons & ~prev;
        prev = pad.buttons;
        if (pressed & SCE_CTRL_UP) sel = NextItem(sel, -1);
        if (pressed & SCE_CTRL_DOWN) sel = NextItem(sel, 1);
        if (pressed & SCE_CTRL_LEFT) close = MenuChange(sel, -1);
        if (pressed & SCE_CTRL_RIGHT) close = MenuChange(sel, 1);
        if (pressed & SCE_CTRL_CROSS) close = MenuChange(sel, 0) || close;
        if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START))
            close = 1;
        /* keep the selection (and its heading) on screen */
        if (sel < top + 1)
            top = sel > 0 && IsHeading(sel - 1) ? sel - 1 : sel;
        if (sel >= top + MENU_ROWS)
            top = sel - MENU_ROWS + 1;
        if (top > MI_COUNT - MENU_ROWS)
            top = MI_COUNT - MENU_ROWS;
        if (top < 0)
            top = 0;

        vita2d_pool_reset();
        DrawGame();
        vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(0, 0, 0, 190));
        vita2d_pgf_draw_text(sFont, 60, 50, RGBA8(255, 200, 80, 255), 1.3f,
                             "Pokemon Mystery Dungeon: Red Rescue Team - Vita " PMD_PORT_VERSION);
        for (i = 0; i < MENU_ROWS && top + i < MI_COUNT; i++) {
            int item = top + i;
            MenuLine(item, line, sizeof(line));
            vita2d_pgf_draw_text(sFont, IsHeading(item) ? 64 : 80, 92 + i * 31,
                                 item == sel ? RGBA8(255, 230, 80, 255) : IsHeading(item) ? RGBA8(140, 190, 255, 255) : RGBA8(220, 220, 220, 255),
                                 1.0f, line);
            if (item == sel)
                vita2d_pgf_draw_text(sFont, 60, 92 + i * 31, RGBA8(255, 230, 80, 255), 1.0f, ">");
        }
        if (top > 0)
            vita2d_pgf_draw_text(sFont, 900, 92, RGBA8(150, 150, 150, 255), 1.0f, "^");
        if (top + MENU_ROWS < MI_COUNT)
            vita2d_pgf_draw_text(sFont, 900, 92 + (MENU_ROWS - 1) * 31, RGBA8(150, 150, 150, 255), 1.0f, "v");
        vita2d_pgf_draw_text(sFont, 60, 525, RGBA8(150, 150, 150, 255), 0.85f,
                             "Steuerkreuz: waehlen/aendern   X: ausfuehren   O/START: zurueck");
        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
        sceDisplayWaitVblankStart();
        if (close)
            break;
    }
    Port_ConfigSave();
    Platform_Log("[ui] menu closed: view=%d style=%d frame=%d fps30=%d square=%d triangle=%d rstick=%d turbo=%d exp=x%d hunger=%d\n",
                 gPortConfig.viewMode, gPortConfig.displayStyle, gPortConfig.frame, gPortConfig.frameRate30, gPortConfig.squareThrow,
                 gPortConfig.triangleMove, gPortConfig.rightStickDiag, gPortConfig.turbo,
                 gPortConfig.expMultiplier, gPortConfig.noHunger);
    sAnchored = 0; /* the menu stalled the game: re-anchor the pacing */
    sWdPause = 0;
}


/* ---- job overview (short tap on the left margin) ---- */

static unsigned JobStatusColor(int status)
{
    switch (status) {
        case PORT_JOBST_OPEN: return PANEL_OPEN;
        case PORT_JOBST_ITEM: return PANEL_ITEM;
        case PORT_JOBST_DONE:
        case PORT_JOBST_REWARD: return PANEL_DONE;
        default: return PANEL_DIM;
    }
}

static int FrontTouched(void)
{
    SceTouchData t;
    return sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) >= 1 && t.reportNum > 0;
}

/* The accepted jobs over the paused game: floor, dungeon, what to do, the
 * client, target and item, and how far the job is. Jobs in the dungeon
 * being explored come first. Closed by a tap or any button. */
static void RunJobOverview(void)
{
    const struct PortPanel *p;
    unsigned prev = ~0u;
    int touchPrev = 1; /* the opening tap may still be on the panel */
    char line[200];

    Port_PanelRefresh();
    p = Port_PanelGet();
    Platform_Log("[ui] job overview open (%d jobs)\n", p->jobCount);
    sWdPause = 1;
    for (;;) {
        SceCtrlData pad;
        unsigned pressed;
        int touch, close = 0, i, pass, y;

        sceCtrlPeekBufferPositive(0, &pad, 1);
        pressed = pad.buttons & ~prev;
        prev = pad.buttons;
        touch = FrontTouched();
        if (touch && !touchPrev)
            close = 1;
        touchPrev = touch;
        if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START | SCE_CTRL_TRIANGLE | SCE_CTRL_SQUARE | SCE_CTRL_SELECT))
            close = 1;

        vita2d_pool_reset();
        DrawGame();
        vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(0, 0, 0, 205));
        if (p->inDungeon)
            snprintf(line, sizeof(line), "Jobs (%d/8)  -  jetzt: %s %s", p->jobCount, p->dungeonName, p->floorText);
        else
            snprintf(line, sizeof(line), "Jobs (%d/8)", p->jobCount);
        vita2d_pgf_draw_text(sFont, 40, 44, PANEL_HEAD, 1.2f, line);
        y = 92;
        if (p->jobCount == 0)
            vita2d_pgf_draw_text(sFont, 60, y, PANEL_TEXT, 1.0f, "Keine Jobs in der Jobliste.");
        /* pass 0: this dungeon, pass 1: the others */
        for (pass = 0; pass < 2; pass++) {
            for (i = 0; i < p->jobCount; i++) {
                const struct PortPanelJob *j = &p->jobs[i];
                unsigned col = JobStatusColor(j->status);
                int n;
                if ((pass == 0) != (j->here != 0))
                    continue;
                if (j->status == PORT_JOBST_OPEN)
                    vita2d_draw_fill_circle(52, y - 6, 6, col);
                else if (j->status == PORT_JOBST_SUSPENDED)
                    vita2d_draw_rectangle(46, y - 7, 12, 3, col);
                else
                    DrawCheck(45, y - 13, 14, col);
                vita2d_pgf_draw_text(sFont, 70, y, j->here ? PANEL_ITEM : PANEL_TEXT, j->floorText[0] ? 1.0f : 0.7f,
                                     j->floorText[0] ? j->floorText : "\xc3\xbc" "berall");
                snprintf(line, sizeof(line), "%s:  %s%s", j->dungeonName, j->what, j->special ? "  (Sonderauftrag)" : "");
                vita2d_pgf_draw_text(sFont, 140, y, j->here ? PANEL_TEXT : RGBA8(200, 200, 200, 255), 1.0f, line);
                n = snprintf(line, sizeof(line), "Kunde: %s", j->client[0] ? j->client : "-");
                if (j->target[0] && j->kind != PORT_JOB_RESCUE_CLIENT && j->kind != PORT_JOB_FIND_ITEM)
                    n += snprintf(line + n, sizeof(line) - n, "   Ziel: %s", j->target);
                if (j->item[0])
                    snprintf(line + n, sizeof(line) - n, "   Item: %s", j->item);
                vita2d_pgf_draw_text(sFont, 140, y + 22, PANEL_DIM, 0.8f, line);
                vita2d_pgf_draw_text(sFont, 640, y + 22, col, 0.8f, j->statusText);
                y += 52;
            }
        }
        vita2d_pgf_draw_text(sFont, 40, 528, PANEL_DIM, 0.8f, "Tippen oder Taste: zur\xc3\xbc" "ck zum Spiel");
        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
        sceDisplayWaitVblankStart();
        if (close)
            break;
    }
    Platform_Log("[ui] job overview closed\n");
    sAnchored = 0; /* the overview stalled the game: re-anchor the pacing */
    sWdPause = 0;
}

/* ---- input ---- */

static int sWaitRelease; /* after the menu: ignore buttons until released */
#define TAP_FRAMES 15  /* a touch released within this many frames is a tap */
#define TAP_ZONE_W 64  /* without panels: the tap zone at the screen's left edge */
static int sTouchFrames, sTouchLeft, sTouchX, sTouchY;
static int sTouchIgnore; /* the tap that closed the overview: ignored until released */
/* The rear touch panel is deliberately unused: it is too easy to touch by accident. */

/* crumbs.txt: the last actions of the game, rewritten six times a second (a
 * crash without a usable core dump still leaves this trace). The file
 * itself is written by a low-priority thread: open/write/rename on the
 * memory card take milliseconds and stalled the game loop (catch-up frames). */
static char sCrumbBuf[2048];
static volatile int sCrumbLen;  /* > 0: sCrumbBuf waits for the writer */
static SceUID sCrumbSema = -1;

static int CrumbThread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        sceKernelWaitSema(sCrumbSema, 1, NULL);
        __sync_synchronize();
        if (sCrumbLen > 0)
            Platform_WriteFile("crumbs.txt", sCrumbBuf, (size_t)sCrumbLen);
        __sync_synchronize();
        sCrumbLen = 0;
    }
    return 0;
}

/* Watchdog: run8 froze in a dungeon without a crash (no frames, not even the
 * system overlay) and the host could not reproduce it. When the game thread
 * has not polled the input for 30 s outside the port menus (longer than
 * the first text index build, ~10 s), this thread
 * writes watchdog.txt (frame, last platform call, crumbs; with sceIo, as the
 * stuck thread may hold the stdio lock of log.txt) and then crashes on
 * purpose: the core dump (ux0:data/ *.psp2dmp) holds the registers and the
 * stack of every thread, so it shows where the game thread hangs. */
static int WatchdogThread(SceSize args, void *argp)
{
    unsigned last = sWdBeat, still = 0;
    (void)args; (void)argp;
    for (;;) {
        sceKernelDelayThread(500 * 1000);
        if (sWdPause || sWdBeat != last) {
            last = sWdBeat;
            still = 0;
            continue;
        }
        if (++still < 60)
            continue;
        {
            static char buf[2600];
            int n = snprintf(buf, sizeof(buf), "pmd %s watchdog: no game frame for 30 s, frame %u, in %s\n",
                             PMD_PORT_VERSION, (unsigned)Port_FrameCounter(), sWdWhere);
            n += Port_CrumbText(buf + n, (int)sizeof(buf) - n);
            Platform_WriteFile("watchdog.txt", buf, (size_t)n);
            sceKernelDelayThread(200 * 1000);
            *(volatile int *)0 = 0x57444f47; /* "WDOG": core dump with all threads */
        }
    }
    return 0;
}

static void WriteCrumbs(void)
{
    static unsigned sCount;
    static int sThreadState; /* 0 = not started, 1 = running, -1 = failed */
    static int sWdStarted;
    int n;
    sWdBeat++;
    if (!sWdStarted) {
        SceUID wd = sceKernelCreateThread("pmd_watchdog", WatchdogThread, 0x10000100 - 10, 0x2000, 0, 0, NULL);
        sWdStarted = 1;
        if (wd >= 0)
            sceKernelStartThread(wd, 0, NULL);
    }
    if (++sCount % 10)
        return;
    if (sThreadState == 0) {
        SceUID th;
        sThreadState = -1;
        sCrumbSema = sceKernelCreateSema("pmd_crumbs", 0, 0, 1, NULL);
        if (sCrumbSema >= 0) {
            th = sceKernelCreateThread("pmd_crumbs", CrumbThread, 0x10000100 + 20, 0x4000, 0, 0, NULL);
            if (th >= 0 && sceKernelStartThread(th, 0, NULL) >= 0)
                sThreadState = 1;
        }
    }
    if (sThreadState > 0 && sCrumbLen != 0)
        return; /* the writer is still busy: next time */
    n = snprintf(sCrumbBuf, sizeof(sCrumbBuf), "pmd %s crumbs (newest last)\n", PMD_PORT_VERSION);
    n += Port_CrumbText(sCrumbBuf + n, (int)sizeof(sCrumbBuf) - n);
    if (sThreadState > 0) {
        __sync_synchronize();
        sCrumbLen = n;
        sceKernelSignalSema(sCrumbSema, 1);
    } else {
        Platform_WriteFile("crumbs.txt", sCrumbBuf, (size_t)n);
    }
}

uint16_t Platform_PollKeys(void)
{
    SceCtrlData pad;
    uint16_t keys = 0;

    sWdWhere = "game logic/rendering (after input)";
    WriteCrumbs();

    sceCtrlPeekBufferPositive(0, &pad, 1);
    if ((pad.buttons & (SCE_CTRL_START | SCE_CTRL_SELECT)) == (SCE_CTRL_START | SCE_CTRL_SELECT)) {
        RunMenu();
        sWaitRelease = 1;
        return 0;
    }
    if (sWaitRelease) {
        if (pad.buttons & (SCE_CTRL_START | SCE_CTRL_SELECT | SCE_CTRL_CROSS | SCE_CTRL_CIRCLE))
            return 0;
        sWaitRelease = 0;
    }

    if (pad.buttons & SCE_CTRL_CROSS) keys |= KEY_A;    /* confirm / attack */
    if (pad.buttons & SCE_CTRL_CIRCLE) keys |= KEY_B;   /* cancel / run */
    /* Square: throw the set item (the game's L+R), Triangle: use the set
     * move (L+A); each can be turned back into a plain B in the menu. */
    if (pad.buttons & SCE_CTRL_SQUARE) keys |= gPortConfig.squareThrow ? (KEY_L | KEY_R) : KEY_B;
    if (pad.buttons & SCE_CTRL_TRIANGLE) keys |= gPortConfig.triangleMove ? (KEY_L | KEY_A) : KEY_B;
    if (pad.buttons & SCE_CTRL_LTRIGGER) keys |= KEY_L;
    if (pad.buttons & SCE_CTRL_RTRIGGER) keys |= KEY_R;
    if (pad.buttons & SCE_CTRL_START) keys |= KEY_START;
    if (pad.buttons & SCE_CTRL_SELECT) keys |= KEY_SELECT;
    if ((pad.buttons & SCE_CTRL_UP) || pad.ly < 128 - 64) keys |= KEY_UP;
    if ((pad.buttons & SCE_CTRL_DOWN) || pad.ly > 128 + 64) keys |= KEY_DOWN;
    if ((pad.buttons & SCE_CTRL_LEFT) || pad.lx < 128 - 64) keys |= KEY_LEFT;
    if ((pad.buttons & SCE_CTRL_RIGHT) || pad.lx > 128 + 64) keys |= KEY_RIGHT;
    /* Right stick: R + direction = diagonal moves / turning in place. */
    if (gPortConfig.rightStickDiag) {
        int dx = (int)pad.rx - 128, dy = (int)pad.ry - 128;
        if (dx * dx + dy * dy > 64 * 64) {
            keys |= KEY_R;
            if (dy < -40) keys |= KEY_UP;
            if (dy > 40) keys |= KEY_DOWN;
            if (dx < -40) keys |= KEY_LEFT;
            if (dx > 40) keys |= KEY_RIGHT;
        }
    }
    /* Front touch held: fast forward. A short tap that starts on the left
     * margin (the floor bar; the left edge when there are no panels) opens
     * the job overview instead: there fast forward starts only once the
     * touch is held longer than a tap. */
    {
        SceTouchData t;
        int touched = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) >= 1 && t.reportNum > 0;
        if (touched && sTouchIgnore) {
            /* still the touch that closed the overview */
        } else if (touched) {
            int x = t.report[0].x / 2, y = t.report[0].y / 2; /* panel: 1920x1088 */
            if (sTouchFrames == 0) {
                float zone = sGameX >= PANEL_MIN_W ? sGameX : TAP_ZONE_W;
                sTouchLeft = x < zone;
                sTouchX = x;
                sTouchY = y;
            } else if (abs(x - sTouchX) > 30 || abs(y - sTouchY) > 30) {
                sTouchLeft = 0; /* a swipe, not a tap */
            }
            if (sTouchFrames < 1000)
                sTouchFrames++;
            if (gPortConfig.turbo && (!sTouchLeft || sTouchFrames > TAP_FRAMES))
                keys |= PORT_KEY_TURBO;
        } else {
            int tap = sTouchFrames > 0 && sTouchFrames <= TAP_FRAMES && sTouchLeft;
            sTouchFrames = 0;
            sTouchIgnore = 0;
            if (tap) {
                RunJobOverview();
                sWaitRelease = 1;
                sTouchIgnore = 1;
                return 0;
            }
        }
    }
    return keys;
}

/* -------------------------------------------------------------- files */

int Platform_LoadFlash(void *data, size_t size)
{
    SceUID fd = sceIoOpen(SAVE_PATH, SCE_O_RDONLY, 0);
    int n;
    if (fd < 0)
        return 0;
    memset(data, 0xFF, size);
    n = sceIoRead(fd, data, size);
    sceIoClose(fd);
    return n > 0;
}

void Platform_SaveFlash(const void *data, size_t size)
{
    SceUID fd = sceIoOpen(SAVE_PATH ".tmp", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        Platform_Log("save: cannot write (0x%08x)\n", fd);
        return;
    }
    sceIoWrite(fd, data, size);
    sceIoClose(fd);
    sceIoRemove(SAVE_PATH);
    sceIoRename(SAVE_PATH ".tmp", SAVE_PATH);
    Platform_Log("save: written\n");
}

long Platform_ReadFile(const char *name, void *data, size_t size)
{
    char path[256];
    SceUID fd;
    int n;
    snprintf(path, sizeof(path), DATA_DIR "/%s", name);
    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return -1;
    n = sceIoRead(fd, data, size);
    sceIoClose(fd);
    return n < 0 ? -1 : n;
}

int Platform_WriteFile(const char *name, const void *data, size_t size)
{
    char path[256], tmp[260];
    SceUID fd;
    int n;
    snprintf(path, sizeof(path), DATA_DIR "/%s", name);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    fd = sceIoOpen(tmp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0)
        return -1;
    n = sceIoWrite(fd, data, size);
    sceIoClose(fd);
    if (n != (int)size) {
        sceIoRemove(tmp);
        return -1;
    }
    sceIoRemove(path);
    return sceIoRename(tmp, path) < 0 ? -1 : 0;
}

/* Finds the US ROM among the .gba files in ux0:data/pmd/. */
static unsigned char *LoadRom(size_t *outSize, int *foundAny)
{
    SceUID dir = sceIoDopen(DATA_DIR);
    SceIoDirent ent;
    unsigned char *rom = NULL;

    *foundAny = 0;
    if (dir < 0)
        return NULL;
    while (sceIoDread(dir, &ent) > 0) {
        size_t len = strlen(ent.d_name);
        char path[512];
        SceUID fd;
        if (len < 4 || strcasecmp(ent.d_name + len - 4, ".gba") != 0)
            continue;
        *foundAny = 1;
        snprintf(path, sizeof(path), DATA_DIR "/%s", ent.d_name);
        if (ent.d_stat.st_size != PORT_ROM_SIZE) {
            Platform_Log("rom: %s has the wrong size\n", ent.d_name);
            continue;
        }
        rom = (unsigned char *)malloc(PORT_ROM_SIZE);
        fd = sceIoOpen(path, SCE_O_RDONLY, 0);
        if (fd >= 0 && rom) {
            /* in 4 MiB pieces with a progress line (the memory card reads
             * ~10-30 MB/s: 32 MiB take seconds) */
            uint64_t t0 = sceKernelGetProcessTimeWide(), us;
            int n = 0, got;
            while (n < (int)PORT_ROM_SIZE) {
                char msg[96];
                snprintf(msg, sizeof(msg), "ROM wird gelesen ... / reading the ROM ... %d %%", n / (int)(PORT_ROM_SIZE / 100));
                ShowProgress(msg);
                got = sceIoRead(fd, rom + n, (PORT_ROM_SIZE - n) < (4u << 20) ? PORT_ROM_SIZE - n : (4u << 20));
                if (got <= 0)
                    break;
                n += got;
            }
            sceIoClose(fd);
            us = sceKernelGetProcessTimeWide() - t0;
            /* the game code only: the SHA-1 of the whole ROM took ~4 s here
             * (romindex.bin keeps it) */
            if (n == (int)PORT_ROM_SIZE && Port_RomVersion(rom, (size_t)n) != 0) {
                Platform_Log("rom: using %s (game code B24%c, %s)\n", ent.d_name, Port_RomVersion(rom, (size_t)n),
                             Port_RomVersion(rom, (size_t)n) == 'E' ? "USA, the version this port is made from"
                                                                     : "another version: data is matched by content");
                Platform_Log("[start] rom read: %u ms (%u KB/s)\n", (unsigned)(us / 1000),
                             (unsigned)((uint64_t)n * 1000000u / 1024u / (us ? us : 1)));
                *outSize = (size_t)n;
                break;
            }
            Platform_Log("rom: %s is not Pokemon Mystery Dungeon - Red Rescue Team\n", ent.d_name);
        }
        free(rom);
        rom = NULL;
    }
    sceIoDclose(dir);
    return rom;
}

/* --------------------------------------------------------------- main */

/* libgcc reaches pthreads only through *weak* references, which never pull
 * an object out of a static archive: on the Vita they all resolved to NULL,
 * so libgcc took the program for single-threaded and the emulated TLS behind
 * the renderer's __thread scanline buffers would be shared by all render
 * threads. Taking the address of every gthreads entry point forces them in
 * (same fix as in the Minish Cap and Zero Mission ports). */
void *const gPmdGthreadsLink[] = {
    (void *)&pthread_attr_destroy,   (void *)&pthread_attr_init,         (void *)&pthread_attr_setdetachstate,
    (void *)&pthread_cancel,         (void *)&pthread_cond_broadcast,    (void *)&pthread_cond_destroy,
    (void *)&pthread_cond_init,      (void *)&pthread_cond_signal,       (void *)&pthread_cond_timedwait,
    (void *)&pthread_cond_wait,      (void *)&pthread_create,            (void *)&pthread_detach,
    (void *)&pthread_equal,          (void *)&pthread_exit,              (void *)&pthread_getschedparam,
    (void *)&pthread_getspecific,    (void *)&pthread_join,              (void *)&pthread_key_create,
    (void *)&pthread_key_delete,     (void *)&pthread_mutex_destroy,     (void *)&pthread_mutex_init,
    (void *)&pthread_mutex_lock,     (void *)&pthread_mutex_timedlock,   (void *)&pthread_mutex_trylock,
    (void *)&pthread_mutex_unlock,   (void *)&pthread_mutexattr_destroy, (void *)&pthread_mutexattr_init,
    (void *)&pthread_mutexattr_settype, (void *)&pthread_once,           (void *)&pthread_rwlock_rdlock,
    (void *)&pthread_rwlock_tryrdlock, (void *)&pthread_rwlock_trywrlock, (void *)&pthread_rwlock_unlock,
    (void *)&pthread_rwlock_wrlock,  (void *)&pthread_self,              (void *)&pthread_setschedparam,
    (void *)&pthread_setspecific,    (void *)&sched_get_priority_max,    (void *)&sched_get_priority_min,
    (void *)&sched_yield,
};

static void CheckGthreads(void)
{
    unsigned i, missing = 0;
    for (i = 0; i < sizeof(gPmdGthreadsLink) / sizeof(gPmdGthreadsLink[0]); i++)
        if (gPmdGthreadsLink[i] == NULL)
            missing++;
    Platform_Log("[thread] gthreads entry points missing=%u\n", missing);
}

int main(int argc, char **argv)
{
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    unsigned char *rom;
    size_t romSize = 0;
    int foundAny;

    uint64_t tMain = sceKernelGetProcessTimeWide(), tPhase;

    (void)argc; (void)argv;
    memset(&init, 0, sizeof(init));
    memset(&boot, 0, sizeof(boot));
    sceAppUtilInit(&init, &boot);

    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    sceIoMkdir(DATA_DIR, 0777);
    sceIoRemove(DATA_DIR "/log_prev.txt");
    sceIoRename(LOG_PATH, DATA_DIR "/log_prev.txt");
    sLog = fopen(LOG_PATH, "w");
    Platform_Log("Pokemon Mystery Dungeon Red Vita %s\n", PMD_PORT_VERSION);
    /* process time: from the start of the process (loading the 37 MiB of
     * the eboot's segments happens before main) */
    Platform_Log("[start] main reached after %u ms\n", (unsigned)(tMain / 1000));
    CheckGthreads();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    tPhase = sceKernelGetProcessTimeWide();
    VideoInit();
    Platform_Log("[start] video init: %u ms\n", (unsigned)((sceKernelGetProcessTimeWide() - tPhase) / 1000));
    ShowProgress("ROM wird gelesen ... / reading the ROM ...");

    rom = LoadRom(&romSize, &foundAny);
    if (!rom) {
        ShowMessage("ROM nicht gefunden / ROM not found",
                    foundAny ? "Die .gba-Datei in ux0:data/pmd/ ist nicht Pokemon Mystery Dungeon - Red Rescue Team (32 MiB).\n"
                               "The .gba file in ux0:data/pmd/ is not Pokemon Mystery Dungeon - Red Rescue Team (32 MiB)."
                             : "Kopiere deine ROM (Pokemon Mystery Dungeon - Red Rescue Team, USA oder Europa) nach ux0:data/pmd/\n"
                               "Copy your ROM (Pokemon Mystery Dungeon - Red Rescue Team, USA or Europe) to ux0:data/pmd/");
        sceKernelExitProcess(0);
        return 0;
    }
    ShowProgress("Spieldaten werden geladen (beim ersten Start einige Sekunden) ...\nloading the game data (a few seconds on the first start) ...");
    tPhase = sceKernelGetProcessTimeWide();
    if (Port_LoadRom(rom, romSize) != 0) {
        ShowMessage("Fehler / Error", "Die Spieldaten konnten nicht geladen werden.\nGame data could not be loaded.");
        sceKernelExitProcess(0);
        return 0;
    }
    Platform_Log("[start] game data: %u ms\n", (unsigned)((sceKernelGetProcessTimeWide() - tPhase) / 1000));
    /* the note on another ROM version: only when the ROM was searched (first
     * start), not at every start with the same ROM (it held 4 s each time) */
    if (gPortAssetsCacheBuilt && (gPortAssetsMissing || gPortAssetsPartial)) {
        char buf[300];
        snprintf(buf, sizeof(buf),
                 "Andere ROM-Version: %u Datenbloecke nur teilweise, %u nicht gefunden (siehe log.txt).\n"
                 "Other ROM version: %u data blocks matched partly, %u not found (see log.txt).",
                 gPortAssetsPartial, gPortAssetsMissing, gPortAssetsPartial, gPortAssetsMissing);
        ShowProgress(buf);
        sceKernelDelayThread(4 * 1000 * 1000);
    }

    tPhase = sceKernelGetProcessTimeWide();
    AudioInit();
    Port_Init(); /* loads settings.ini */
    Platform_Log("[start] audio + settings: %u ms\n", (unsigned)((sceKernelGetProcessTimeWide() - tPhase) / 1000));
    gPortConfig.viewMode = gPortViewMode = PORT_VIEW_FULL; /* the Vita port always shows the Full View */
    Platform_Log("settings: view=%d style=%d frame=%d fps30=%d turbo=%d\n", gPortConfig.viewMode,
                 gPortConfig.displayStyle, gPortConfig.frame, gPortConfig.frameRate30, gPortConfig.turbo);
    if (Port_TextMultilingual() && gPortConfig.language < 0)
        ChooseLanguage();
    Platform_Log("settings: language=%d (multilingual ROM: %d)\n", gPortConfig.language, Port_TextMultilingual());
    Platform_Log("[start] game starts after %u ms\n", (unsigned)(sceKernelGetProcessTimeWide() / 1000));
    Port_RunGame();

    sceKernelExitProcess(0);
    return 0;
}
