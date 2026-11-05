/* Overlay drawn by the port into the finished frame (port/port_overlay.c):
 * HP bars over enemy monsters in dungeons and a clock in the frame's
 * margin. Platform independent: the host's PPM frames and the Vita show
 * the same pixels. */
#ifndef PORT_OVERLAY_H
#define PORT_OVERLAY_H

#include <stdint.h>

/* After the game's VBlank handler: takes the positions and HP of the
 * monsters for the frame rendered next. */
void Port_OverlayOnVBlank(void);
/* Draws the overlay into the rendered frame (ABGR8888, `pitch` pixels per
 * row). Does nothing when both overlays are off. */
void Port_OverlayDraw(uint32_t *frame, int width, int height, int pitch);

#endif /* PORT_OVERLAY_H */
