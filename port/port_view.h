/* Wide view / Full View of the PMD port (port/port_view.c). */
#ifndef PORT_VIEW_H
#define PORT_VIEW_H

enum {
    PORT_VIEW_GBA = 0,  /* 240x160 */
    PORT_VIEW_WIDE = 1, /* 16:9 at the GBA's 160 lines, same zoom */
    PORT_VIEW_FULL = 2  /* "Weitsicht": 424x240 (exactly 16:9), more of the map in both directions */
};

#define PORT_VIEW_WIDE_W 282
#define PORT_VIEW_FULL_W 424
#define PORT_VIEW_FULL_H 240
#define PORT_VIEW_MAX_W 424
#define PORT_VIEW_MAX_H 240

/* What the game is showing (reported by the game's frame functions). */
enum {
    PORT_SCENE_NONE = 0, /* title, menus, map screens: the GBA picture */
    PORT_SCENE_DUNGEON = 1,
    PORT_SCENE_GROUND = 2,
    PORT_SCENE_WORLDMAP = 3,  /* 480x320 picture in RAM */
    PORT_SCENE_FRIENDMAP = 4  /* friend areas map, same layout */
};

extern int gPortViewMode;
/* Margins (pixels) of the view around the GBA screen during the logic frame
 * in progress: the game's on-screen checks are widened by them. */
extern int gPortViewMarginLeft, gPortViewMarginRight, gPortViewMarginTop, gPortViewMarginBottom;

/* Hooks for the game code. */
void Port_ViewSceneFrame(int scene);
int Port_ViewScene(void); /* the current PORT_SCENE_* */
/* A sprite was staged at index `index` with the true GBA-screen position. */
void Port_SpriteStaged(int index, int x, int y);
/* 1 while AddSprite() places a world sprite (shadows, traps, floor
 * markers); everything else through AddSprite() is screen UI (menu
 * cursors, portraits, text icons) and keeps the GBA screen's place in the
 * frame instead of following the world window. */
extern int gPortSpriteWorld;
void Port_SpriteStagedUi(int index, int ui);
/* The staged sprite `index` was written to OAM slot `slot`. */
void Port_OamSlot(int slot, int index);

/* Called by the frame loop. */
void Port_ViewBeforeLogicEnd(void);
void Port_ViewOnVBlank(void);
void Port_ViewBeforeRender(int *width, int *height);
/* Rows of the frame that must stay black (no map there): [0,top) and [bottom,height). */
void Port_ViewBlankRows(int *top, int *bottom);
/* Columns beside a picture narrower than the frame are blanked: [left, right) stays. */
void Port_ViewBlankCols(int *left, int *right);
/* After the frame is drawn: undoes the port's temporary OAM copies. */
void Port_ViewAfterRender(void);
int Port_ViewWidth(void);
int Port_ViewHeight(void);
int Port_ViewZoomHeight(void);
/* The world's shift in the frame being rendered (where GBA-screen pixel
 * (0,0) of the map and the world sprites lands) and the scene that frame
 * shows (latched at VBlank; PORT_SCENE_NONE once a scene stops reporting). */
void Port_ViewWorldShift(int *dx, int *dy);
int Port_ViewSceneShown(void);

#endif /* PORT_VIEW_H */
