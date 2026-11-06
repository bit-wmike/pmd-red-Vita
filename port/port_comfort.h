/* Comfort options of the PMD port (port/port_comfort.c): instant text,
 * auto-advancing text boxes, faster dungeon animations, running in the
 * town and a quick save on every new dungeon floor. All of them are off
 * by default and do nothing while their setting is 0. */
#ifndef PORT_COMFORT_H
#define PORT_COMFORT_H

/* Text boxes show their whole text at once (settings.ini text_instant). */
int Port_TextInstant(void);
/* A text box waiting for a button goes on by itself while A is held for
 * this many frames (settings.ini text_auto); 0 = never. */
int Port_TextAutoFrames(void);
/* Blinking cursors and text arrows: the game shows them while
 * (frame counter & 8), a 16-frame cycle; slow_blink (default on) doubles
 * it. Use as `counter & PORT_BLINK_MASK`. */
int Port_BlinkMask(void);
#define PORT_BLINK_MASK (Port_BlinkMask())
/* The player runs in ground scenes (settings.ini walk_fast). */
int Port_WalkFast(void);

/* Dungeon speed (settings.ini dungeon_speed): called by DungeonRunFrameActions
 * with the call-site id. Returns 1 when this logic frame must not wait for
 * VBlank, so that two (x2) or three (x1.5) logic frames of an animation run
 * per displayed frame. Frames that poll the player, show menus or text are
 * never skipped. */
int Port_DungeonSkipFrameWait(unsigned id);

/* Floor autosave (settings.ini floor_autosave): the dungeon code reports a
 * freshly entered floor; the port then takes a quick save at the first frame
 * in which the game waits for the player's input. */
void Port_FloorStarted(void);
void Port_FloorInputFrame(void);

/* Base autosave (settings.ini base_autosave): called at every ground frame
 * in which the player can move, with the ground map's id. The port takes a
 * quick save at the first such frame after the base (outside or inside the
 * house) was entered from another map, a dungeon or the title menu. */
void Port_GroundInputFrame(int groundMapId);

/* Faster dungeon messages (settings.ini msg_fast): 1 = the message log at
 * the bottom scrolls twice as fast and the game waits half as long after a
 * message before the next action. */
int Port_MsgFast(void);
/* sub_8052740: the game waits until its message timer (240 at the last
 * line) falls below this value, i.e. 60 frames (msg_fast: 30). */
int Port_MsgWaitThreshold(void);

#endif
