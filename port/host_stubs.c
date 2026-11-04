/*
 * GBA-only pieces of the game that have no counterpart in the port: the link
 * cable routines (hand-written ARM code the game copies to EWRAM) and the
 * AGB debug print library.
 */
#include <stdarg.h>

#include "port/port.h"
#include "gba/types.h"

/* src/libisagbprn.c */
void AGBPrintInit(void) {}
void AGBPutc(const char c) { (void)c; }
void AGBPrint(const char *s) { Platform_Log("agbprint: %s\n", s); }
void AGBPrintf(const char *fmt, ...)
{
    (void)fmt;
}
void AGBPrintFlush1Block(void) {}
void AGBPrintFlush(void) {}
void AGBAssert(const char *file, int line, const char *expr, int stop)
{
    Platform_Log("assert: %s:%d %s%s\n", file, line, expr, stop ? " (stop)" : "");
}
