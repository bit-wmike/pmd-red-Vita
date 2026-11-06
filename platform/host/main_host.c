/*
 * Headless test runner for the port (Linux, 32-bit x86 or ARMv7 under qemu).
 *
 * Runs the game for a number of frames with scripted input and writes
 * screenshots and the audio output, for automated checks without a Vita.
 *
 *   pmd_host <rom.gba> [frames] [outdir] [script]
 *
 * The script has lines "<frame> <keymask-hex>" (GBA key bits: A=1, B=2,
 * Select=4, Start=8, Right=10, Left=20, Up=40, Down=80, R=100, L=200): from
 * that frame on the keys are held until the next line.
 *
 * Environment:
 *   PMD_SHOT=a,b,c       frames to save as outdir/frame_<n>.ppm
 *   PMD_SHOT_EVERY=k     additionally save every k-th frame
 *   PMD_DUMP=a,b         same as PMD_SHOT (Minish Cap port habit)
 *   PMD_SAVE=file        flash save file (default outdir/pmd_red.sav)
 *   PMD_VIEW=0|1|2       view mode (GBA / wide / full), default from settings.ini
 *   PMD_QSAVE_AT=n       quick save at frame n, PMD_QLOAD_AT=n quick load
 *   PMD_CHEATS=1         all cheats on;  PMD_30FPS=1  render every 2nd frame
 *   PMD_CFG=k=v,k=v      any settings.ini keys (e.g. PMD_CFG=hp_bars=2,text_instant=1)
 *   PMD_WAV=1            write outdir/audio.wav
 *   PMD_TIMING=1         print the render time every 300 frames
 *   PMD_TRACE=file       per-frame checksums of the game state
 *   PMD_PANEL_LOG=n      the frame panels (clock, dungeon, floor bar, jobs) as text every n frames
 *   PMD_PANEL_TESTJOBS=n six sample jobs written into the job list at frame n (panel tests only)
 *   PMD_STORAGE_TEST=f:op[:arg],...  Kangaskhan storage from the port menu (port/port_storage.c) at
 *                        frame f (or the next frame in which the game waits for input): op = list,
 *                        deposit:<bag slot>, withdraw:<item id>, give:<id>[:qty[:flags]] (test item into
 *                        the bag, flags 0x10 set, 0x08 sticky, 0x02 shop; flags -1: stored amount),
 *                        press:<keys hex>[:frames] (held instead of the script's keys, at an input frame),
 *                        key:<keys>[:frames] (same at any frame); in order, "+n" = n frames after the
 *                        previous operation; bag and storage are logged after each
 *   settings.ini, quicksave.bin, romindex.bin and textindex.bin live in the output directory.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "port/port.h"
#include "port/port_view.h"
#include "port/port_config.h"
#include "port/port_game.h"
#include "port/port_text.h"
#include "port/port_panel.h"
#include "port/port_storage.h"

static const char *sOutDir = "host_out";
static int sMaxFrames = 600;
static int sShotEvery;
static char sShotList[4096];
static FILE *sWav;
static long sWavFrames;
static char sSavePath[1024];
static int sQSaveAt = -1, sQLoadAt = -1;
static int sPanelLog; /* PMD_PANEL_LOG=n: the frame panels as text every n frames */

/* PMD_STORAGE_TEST: operations in order, each not before its frame ("+n":
 * n frames after the previous one ran) */
struct StorageOp { int frame, rel; char op[16]; int arg, arg2, arg3; int done; };
static struct StorageOp sStorageOps[64];
static int sStorageOpCount, sStorageNext;
static int sKeyOverride = -1, sKeyUntil; /* press/key: keys held instead of the script's */

static void ParseStorageTest(const char *e)
{
    while (*e && sStorageOpCount < 64) {
        struct StorageOp *o = &sStorageOps[sStorageOpCount];
        int n = 0;
        memset(o, 0, sizeof(*o));
        if (*e == '+') {
            o->rel = 1;
            e++;
        }
        if (sscanf(e, "%d:%15[a-z]%n", &o->frame, o->op, &n) < 2)
            break;
        e += n;
        if (*e == ':')
            o->arg = (int)strtol(e + 1, (char **)&e, 0);
        if (*e == ':')
            o->arg2 = (int)strtol(e + 1, (char **)&e, 0);
        if (*e == ':')
            o->arg3 = (int)strtol(e + 1, (char **)&e, 0);
        sStorageOpCount++;
        while (*e && *e != ',')
            e++;
        if (*e == ',')
            e++;
    }
}

static void StorageTest(int frame)
{
    static char text[16384];
    struct StorageOp *o;
    char msg[128] = "";
    int r = 0, isKey;
    if (sStorageNext >= sStorageOpCount || (sKeyOverride >= 0 && frame < sKeyUntil))
        return;
    o = &sStorageOps[sStorageNext];
    if (frame < o->frame)
        return;
    isKey = !strcmp(o->op, "key");
    /* everything but "key" waits for a frame in which the game waits for input */
    if (!isKey && !Port_StorageSafe()) {
        if (frame - o->frame > 3000) {
            Platform_Log("[storage] frame %d: %s gave up (the game never waited for input)\n", frame, o->op);
            o->done = 1;
            sStorageNext++;
        }
        return;
    }
    o->done = 1;
    sStorageNext++;
    if (sStorageNext < sStorageOpCount && sStorageOps[sStorageNext].rel)
        sStorageOps[sStorageNext].frame += frame;
    if (isKey || !strcmp(o->op, "press")) {
        sKeyOverride = o->arg;
        sKeyUntil = frame + (o->arg2 > 0 ? o->arg2 : 4);
        Platform_Log("[storage] frame %d: %s %x for %d frames\n", frame, o->op, o->arg, sKeyUntil - frame);
        return;
    }
    if (!strcmp(o->op, "deposit"))
        r = Port_StorageDeposit(o->arg, msg, sizeof(msg));
    else if (!strcmp(o->op, "withdraw"))
        r = Port_StorageWithdraw(o->arg, msg, sizeof(msg));
    else if (!strcmp(o->op, "give"))
        r = Port_StorageTestGive(o->arg, o->arg2, o->arg3);
    Platform_Log("[storage] frame %d (asked %d): %s %d -> %d \"%s\"\n", frame, o->frame, o->op, o->arg, r, msg);
    Port_StorageText(text, sizeof(text));
    Platform_Log("%s", text);
}

struct InputEvent { int frame; unsigned keys; };
static struct InputEvent sInput[8192];
static int sInputCount;

uint64_t Platform_TimeUs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

void Platform_Log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static int ShotWanted(int frame)
{
    char buf[32];
    if (sShotEvery && frame % sShotEvery == 0)
        return 1;
    snprintf(buf, sizeof(buf), ",%d,", frame);
    return strstr(sShotList, buf) != NULL;
}

void Platform_Present(const uint32_t *pixels, int width, int height, int pitch, int zoomHeight)
{
    int frame = (int)Port_FrameCounter();
    (void)zoomHeight;
    if (getenv("PMD_TIMING") && frame % 300 == 299)
        printf("host: frame %d render avg %u us\n", frame, Port_ProfileAverageUs(PORT_PROF_RENDER));
    if (ShotWanted(frame)) {
        char path[1100];
        FILE *f;
        int x, y;
        snprintf(path, sizeof(path), "%s/frame_%05d.ppm", sOutDir, frame);
        f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", width, height);
            for (y = 0; y < height; y++)
                for (x = 0; x < width; x++) {
                    uint32_t p = pixels[y * pitch + x];
                    unsigned char rgb[3] = {(unsigned char)p, (unsigned char)(p >> 8), (unsigned char)(p >> 16)};
                    fwrite(rgb, 1, 3, f);
                }
            fclose(f);
        }
    }
}

static void Finish(void)
{
    if (sWav) {
        long bytes = sWavFrames * 4;
        fseek(sWav, 4, SEEK_SET);
        { unsigned v = (unsigned)(36 + bytes); fwrite(&v, 4, 1, sWav); }
        fseek(sWav, 40, SEEK_SET);
        { unsigned v = (unsigned)bytes; fwrite(&v, 4, 1, sWav); }
        fclose(sWav);
    }
}

uint16_t Platform_PollKeys(void)
{
    int frame = (int)Port_FrameCounter() + 1;
    uint16_t keys = 0;
    int i;
    if (frame >= sMaxFrames) {
        Finish();
        Port_TextLogStats();
        printf("host: done after %d frames\n", frame);
        exit(0);
    }
    if (frame == sQSaveAt)
        Port_RequestQuickSave();
    if (frame == sQLoadAt)
        Port_RequestQuickLoad();
    if (sPanelLog > 0 && frame % sPanelLog == 0) {
        static char text[4096];
        Port_PanelRefresh();
        Port_PanelText(text, sizeof(text));
        Platform_Log("[panel] frame %d\n%s", frame, text);
    }
    if (sStorageOpCount)
        StorageTest(frame);
    for (i = 0; i < sInputCount; i++)
        if (frame >= sInput[i].frame)
            keys = (uint16_t)sInput[i].keys;
    if (sKeyOverride >= 0) {
        if (frame < sKeyUntil)
            return (uint16_t)sKeyOverride;
        if (frame < sKeyUntil + 2)
            return 0; /* released between two presses */
    }
    return keys;
}

void Platform_SubmitAudio(const int16_t *samples, int frames)
{
    if (sWav) {
        fwrite(samples, 4, (size_t)frames, sWav);
        sWavFrames += frames;
    }
}

int Platform_LoadFlash(void *data, size_t size)
{
    FILE *f = fopen(sSavePath, "rb");
    size_t n;
    if (!f)
        return 0;
    n = fread(data, 1, size, f);
    fclose(f);
    return n > 0;
}

void Platform_SaveFlash(const void *data, size_t size)
{
    FILE *f = fopen(sSavePath, "wb");
    if (!f)
        return;
    fwrite(data, 1, size, f);
    fclose(f);
}

long Platform_ReadFile(const char *name, void *data, size_t size)
{
    char path[1100];
    FILE *f;
    long n;
    snprintf(path, sizeof(path), "%s/%s", sOutDir, name);
    f = fopen(path, "rb");
    if (!f)
        return -1;
    n = (long)fread(data, 1, size, f);
    fclose(f);
    return n;
}

int Platform_WriteFile(const char *name, const void *data, size_t size)
{
    char path[1100];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", sOutDir, name);
    f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite(data, 1, size, f);
    fclose(f);
    return 0;
}

int Platform_PaceAllowsRender(void)
{
    return 1;
}

static void ReadScript(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    if (!f) {
        fprintf(stderr, "host: cannot read script %s\n", path);
        return;
    }
    while (fgets(line, sizeof(line), f) && sInputCount < 8192) {
        int frame;
        unsigned keys;
        if (sscanf(line, "%d %x", &frame, &keys) == 2) {
            sInput[sInputCount].frame = frame;
            sInput[sInputCount].keys = keys;
            sInputCount++;
        }
    }
    fclose(f);
}

static unsigned char *ReadRom(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *rom;
    long n;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return NULL;
    }
    rom = (unsigned char *)malloc((size_t)n);
    if (fread(rom, 1, (size_t)n, f) != (size_t)n) {
        free(rom);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return rom;
}

int main(int argc, char **argv)
{
    const char *e;
    unsigned char *rom;
    size_t romSize = 0;
    char cmd[1200];

    if (argc < 2) {
        fprintf(stderr, "usage: %s <rom.gba> [frames] [outdir] [script]\n", argv[0]);
        return 1;
    }
    if (argc > 2)
        sMaxFrames = atoi(argv[2]);
    if (argc > 3)
        sOutDir = argv[3];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", sOutDir);
    if (system(cmd) != 0)
        fprintf(stderr, "host: cannot create %s\n", sOutDir);
    if (argc > 4)
        ReadScript(argv[4]);

    e = getenv("PMD_SHOT");
    if (!e)
        e = getenv("PMD_DUMP");
    snprintf(sShotList, sizeof(sShotList), ",%s,", e ? e : "");
    if ((e = getenv("PMD_SHOT_EVERY")) != NULL)
        sShotEvery = atoi(e);
    if ((e = getenv("PMD_SAVE")) != NULL)
        snprintf(sSavePath, sizeof(sSavePath), "%s", e);
    else
        snprintf(sSavePath, sizeof(sSavePath), "%s/pmd_red.sav", sOutDir);
    if ((e = getenv("PMD_QSAVE_AT")) != NULL)
        sQSaveAt = atoi(e);
    if ((e = getenv("PMD_QLOAD_AT")) != NULL)
        sQLoadAt = atoi(e);
    if (getenv("PMD_WAV")) {
        char path[1100];
        static const unsigned char hdr[44] = {
            'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
            PORT_AUDIO_RATE & 0xFF, (PORT_AUDIO_RATE >> 8) & 0xFF, (PORT_AUDIO_RATE >> 16) & 0xFF, 0,
            (PORT_AUDIO_RATE * 4) & 0xFF, ((PORT_AUDIO_RATE * 4) >> 8) & 0xFF, ((PORT_AUDIO_RATE * 4) >> 16) & 0xFF, 0,
            4,0, 16,0, 'd','a','t','a', 0,0,0,0 };
        snprintf(path, sizeof(path), "%s/audio.wav", sOutDir);
        sWav = fopen(path, "wb");
        if (sWav)
            fwrite(hdr, 1, 44, sWav);
    }

    {
        uint64_t t0 = Platform_TimeUs(), t1;
        rom = ReadRom(argv[1], &romSize);
        if (!rom) {
            fprintf(stderr, "host: cannot read %s\n", argv[1]);
            return 1;
        }
        /* the game code only: a SHA-1 of the 32 MiB is no longer computed
         * at every start (romindex.bin knows it) */
        if (Port_RomVersion(rom, romSize) != 'E')
            fprintf(stderr, "host: note: %s is not the US ROM (game code B24%c); data is matched by content\n", argv[1],
                    Port_RomVersion(rom, romSize) ? Port_RomVersion(rom, romSize) : '?');
        t1 = Platform_TimeUs();
        if (Port_LoadRom(rom, romSize) != 0) {
            fprintf(stderr, "host: cannot load the game data\n");
            return 1;
        }
        fprintf(stderr, "[start] read rom %u ms, assets %u ms\n", (unsigned)((t1 - t0) / 1000),
                (unsigned)((Platform_TimeUs() - t1) / 1000));
    }

    Port_Init();
    if ((e = getenv("PMD_LANG")) != NULL)
        gPortConfig.language = atoi(e);
    if ((e = getenv("PMD_VIEW")) != NULL)
        gPortConfig.viewMode = gPortViewMode = atoi(e);
    if (getenv("PMD_30FPS"))
        gPortConfig.frameRate30 = 1;
    if (getenv("PMD_CHEATS")) {
        gPortConfig.expMultiplier = 4;
        gPortConfig.noHunger = 1;
    }
    /* PMD_CFG="key=value,key=value": any settings.ini keys (tests) */
    if ((e = getenv("PMD_CFG")) != NULL) {
        char buf[512];
        size_t i;
        strncpy(buf, e, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        for (i = 0; buf[i]; i++)
            if (buf[i] == ',')
                buf[i] = '\n';
        Port_ConfigParse(buf);
    }
    if ((e = getenv("PMD_STORAGE_TEST")) != NULL)
        ParseStorageTest(e);
    if ((e = getenv("PMD_PANEL_LOG")) != NULL && atoi(e) > 0) {
        sPanelLog = atoi(e);
        Port_PanelEnable(1);
    }
    Port_RunGame();
    return 0;
}
