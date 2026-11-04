/* Gameplay additions of the PMD port (port/port_game.c). */
#ifndef PORT_GAME_H
#define PORT_GAME_H

#include <stdint.h>

/* Extra key bits the platform may report besides the GBA keys. */
#define PORT_KEY_TURBO 0x8000

uint16_t Port_TranslateKeys(uint16_t keys);
void Port_GameFrame(int mainLoop);
void Port_RequestQuickSave(void);
void Port_RequestQuickLoad(void);
void Port_ShowMessage(const char *text);
const char *Port_CurrentMessage(void);
int Port_TurboActive(void);

#endif
