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

extern int gPortViewMode;

/* Called by the frame loop. */
void Port_ViewBeforeLogicEnd(void); /* the game has finished its logic frame */
void Port_ViewOnVBlank(void);       /* after the game's VBlank handler */
void Port_ViewBeforeRender(int *width, int *height);
int Port_ViewWidth(void);
int Port_ViewHeight(void);
int Port_ViewZoomHeight(void);

#endif /* PORT_VIEW_H */
