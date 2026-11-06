/*
 * Gameplay additions of the PMD port: control extras, cheats and quick
 * save / quick load (a snapshot of the whole game state).
 */
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "port/port.h"
#include "port/port_config.h"
#include "port/port_game.h"
#include "port/port_view.h"

#ifndef PMD_PORT_VERSION
#define PMD_PORT_VERSION "dev"
#endif

/* ---------------------------------------------------------------- input */

static int sTurbo;

uint16_t Port_TranslateKeys(uint16_t keys)
{
    sTurbo = gPortConfig.turbo && (keys & PORT_KEY_TURBO);
    return keys & 0x3FF;
}

static int sSkipScene;

/* A scene the port runs through at once (the health and safety screen on
 * the Vita; on the host only with PMD_SKIP_WARNING, so that the test
 * scripts stay in step). */
void Port_SetSkipScene(int on)
{
#ifndef __vita__
    if (on && !getenv("PMD_SKIP_WARNING"))
        on = 0;
#endif
    if (on != sSkipScene)
        Platform_Log("skip scene: %s\n", on ? "on" : "off");
    sSkipScene = on;
}

int Port_SkipSceneActive(void)
{
    return sSkipScene;
}

int Port_TurboActive(void)
{
    return sTurbo || sSkipScene;
}

/* --------------------------------------------------------------- cheats */

void Port_ApplyCheats(void); /* port/port_cheats.c */

/* ------------------------------------------------------------- crumbs */

/* The last few things the game did (files opened, text looked up, scene
 * changes): the Vita writes them to crumbs.txt twice a second, so that a
 * crash without a usable core dump still leaves a trace. */
#define CRUMB_COUNT 24
#define CRUMB_LEN 72
static char sCrumbs[CRUMB_COUNT][CRUMB_LEN];
static unsigned sCrumbNext, sCrumbTotal;

void Port_Crumb(const char *fmt, ...)
{
    va_list ap;
    char *line = sCrumbs[sCrumbNext % CRUMB_COUNT];
    int n = snprintf(line, CRUMB_LEN, "%u ", Port_FrameCounter());
    va_start(ap, fmt);
    vsnprintf(line + n, CRUMB_LEN - n, fmt, ap);
    va_end(ap);
    sCrumbNext++;
    sCrumbTotal++;
}

/* The crumbs, oldest first, as text. */
int Port_CrumbText(char *buf, int size)
{
    unsigned i, from = sCrumbTotal > CRUMB_COUNT ? sCrumbNext - CRUMB_COUNT : 0;
    int n = 0;
    for (i = from; i < sCrumbNext && n < size - 1; i++)
        n += snprintf(buf + n, size - n, "%s\n", sCrumbs[i % CRUMB_COUNT]);
    return n < size ? n : size - 1; /* snprintf counts what did not fit */
}

/* ------------------------------------------------------ quick save/load */

#define STATE_MAGIC 0x53444D50u /* "PMDS" */

struct StateHeader {
    uint32_t magic;
    char build[48];
    uint32_t ramSize;
};

static int sRequest; /* 1 = save, 2 = load */
static char sStateMessage[96];
static int sStateMessageFrames;

/* Quick save slots 1..3 (settings.ini save_slot): quicksave1.bin ..
 * quicksave3.bin. Up to 0.1.19 there was one file, quicksave.bin; slot 1
 * still loads it until slot 1 is saved for the first time. */
static int SaveSlot(void)
{
    int slot = gPortConfig.saveSlot;
    return slot < 1 ? 1 : (slot > 3 ? 3 : slot);
}

static void SlotFile(char *out, size_t size, int slot)
{
    snprintf(out, size, "quicksave%d.bin", slot);
}

static void BuildId(char out[48])
{
    memset(out, 0, 48);
    strncpy(out, PMD_PORT_VERSION " " __DATE__ " " __TIME__, 47);
}

static void DoSave(void)
{
    int slot = SaveSlot();
    char name[32], msg[64];
    unsigned char *ptr[16];
    size_t size[16], total = sizeof(struct StateHeader), n, i, off;
    unsigned char *buf;
    struct StateHeader *h;

    n = Port_GameRamRegions(ptr, size, 16);
    for (i = 0; i < n; i++)
        total += size[i];
    buf = (unsigned char *)malloc(total);
    if (!buf)
        return;
    h = (struct StateHeader *)buf;
    h->magic = STATE_MAGIC;
    BuildId(h->build);
    h->ramSize = (uint32_t)(total - sizeof(*h));
    off = sizeof(*h);
    for (i = 0; i < n; i++) {
        memcpy(buf + off, ptr[i], size[i]);
        off += size[i];
    }
    SlotFile(name, sizeof(name), slot);
    if (Platform_WriteFile(name, buf, total) == 0)
        snprintf(msg, sizeof(msg), "Schnellspeicherung gesichert (Platz %d)", slot);
    else
        snprintf(msg, sizeof(msg), "Schnellspeicherung fehlgeschlagen (Platz %d)", slot);
    Port_ShowMessage(msg);
    free(buf);
}

static void DoLoad(void)
{
    int slot = SaveSlot();
    char name[32], msg[80];
    unsigned char *ptr[16];
    size_t size[16], total = sizeof(struct StateHeader), n, i, off;
    unsigned char *buf;
    struct StateHeader *h;
    char build[48];

    n = Port_GameRamRegions(ptr, size, 16);
    for (i = 0; i < n; i++)
        total += size[i];
    buf = (unsigned char *)malloc(total);
    if (!buf)
        return;
    SlotFile(name, sizeof(name), slot);
    if (Platform_ReadFile(name, buf, total) != (long)total
        && (slot != 1 || Platform_ReadFile("quicksave.bin", buf, total) != (long)total)) {
        snprintf(msg, sizeof(msg), "Keine Schnellspeicherung vorhanden (Platz %d)", slot);
        Port_ShowMessage(msg);
        free(buf);
        return;
    }
    h = (struct StateHeader *)buf;
    BuildId(build);
    if (h->magic != STATE_MAGIC || memcmp(h->build, build, sizeof(build)) != 0 || h->ramSize != total - sizeof(*h)) {
        snprintf(msg, sizeof(msg), "Schnellspeicherung (Platz %d) stammt von einer anderen Version", slot);
        Port_ShowMessage(msg);
        free(buf);
        return;
    }
    off = sizeof(*h);
    for (i = 0; i < n; i++) {
        memcpy(ptr[i], buf + off, size[i]);
        off += size[i];
    }
    free(buf);
    HostDma_Reset();
    snprintf(msg, sizeof(msg), "Schnellspeicherung geladen (Platz %d)", slot);
    Port_ShowMessage(msg);
}

void Port_RequestQuickSave(void)
{
    sRequest = 1;
}

void Port_RequestQuickLoad(void)
{
    sRequest = 2;
}

void Port_ShowMessage(const char *text)
{
    strncpy(sStateMessage, text, sizeof(sStateMessage) - 1);
    sStateMessage[sizeof(sStateMessage) - 1] = 0;
    sStateMessageFrames = 120;
    Platform_Log("message: %s\n", text);
}

const char *Port_CurrentMessage(void)
{
    if (sStateMessageFrames <= 0)
        return NULL;
    sStateMessageFrames--;
    return sStateMessage;
}

int Port_InGameplay(void); /* port/port_cheats.c */

/* Called once per frame at the frame boundary (the game is halted). */
void Port_GameFrame(int mainLoop)
{
    Port_ApplyCheats();

    if (sRequest) {
        /* The whole game state is in memory when the game waits for VBlank
         * in its main loop; the C stack holds nothing then. */
        (void)mainLoop;
        if (Port_InGameplay()) {
            if (sRequest == 1)
                DoSave();
            else
                DoLoad();
        } else {
            Port_ShowMessage("Schnellspeichern geht nur im laufenden Spiel");
        }
        sRequest = 0;
    }
}
