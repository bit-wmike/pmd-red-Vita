/*
 * Gameplay additions of the PMD port: control extras, cheats and quick
 * save / quick load (a snapshot of the whole game state).
 */
#include <stdlib.h>
#include <string.h>

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

int Port_TurboActive(void)
{
    return sTurbo;
}

/* --------------------------------------------------------------- cheats */

void Port_ApplyCheats(void); /* port/port_cheats.c */

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

static void BuildId(char out[48])
{
    memset(out, 0, 48);
    strncpy(out, PMD_PORT_VERSION " " __DATE__ " " __TIME__, 47);
}

static void DoSave(void)
{
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
    if (Platform_WriteFile("quicksave.bin", buf, total) == 0)
        Port_ShowMessage("Schnellspeicherung gesichert");
    else
        Port_ShowMessage("Schnellspeicherung fehlgeschlagen");
    free(buf);
}

static void DoLoad(void)
{
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
    if (Platform_ReadFile("quicksave.bin", buf, total) != (long)total) {
        Port_ShowMessage("Keine Schnellspeicherung vorhanden");
        free(buf);
        return;
    }
    h = (struct StateHeader *)buf;
    BuildId(build);
    if (h->magic != STATE_MAGIC || memcmp(h->build, build, sizeof(build)) != 0 || h->ramSize != total - sizeof(*h)) {
        Port_ShowMessage("Schnellspeicherung stammt von einer anderen Version");
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
    Port_ShowMessage("Schnellspeicherung geladen");
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
