/* Wide view / Full View of the PMD port (port/port_view.c). */
#ifndef PORT_VIEW_H
#define PORT_VIEW_H

enum {
    PORT_VIEW_GBA = 0,  /* 240x160 */
    PORT_VIEW_WIDE = 1, /* 16:9 at the GBA's 160 lines, same zoom */
    PORT_VIEW_FULL = 2  /* "Weitsicht": 400x240, more of the map in both directions */
};

#define PORT_VIEW_WIDE_W 282
#define PORT_VIEW_FULL_W 400
#define PORT_VIEW_FULL_H 240
#define PORT_VIEW_MAX_W 400
#define PORT_VIEW_MAX_H 240

/* What the game is showing (reported by the game's frame functions). */
enum {
    PORT_SCENE_NONE = 0, /* title, menus, map screens: the GBA picture */
    PORT_SCENE_DUNGEON = 1,
    PORT_SCENE_GROUND = 2
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
/* The staged sprite `index` was written to OAM slot `slot`. */
void Port_OamSlot(int slot, int index);

/* Called by the frame loop. */
void Port_ViewBeforeLogicEnd(void);
void Port_ViewOnVBlank(void);
void Port_ViewBeforeRender(int *width, int *height);
int Port_ViewWidth(void);
int Port_ViewHeight(void);
int Port_ViewZoomHeight(void);

#endif /* PORT_VIEW_H */
