/*
 * Wide view / Full View for the PMD port (placeholder: the GBA picture).
 */
#include "port/port.h"
#include "port/port_view.h"
#include "port/ppu/include/cpu/mode1.h"

int gPortViewMode = PORT_VIEW_FULL;

void Port_ViewBeforeLogicEnd(void)
{
}

void Port_ViewOnVBlank(void)
{
}

int Port_ViewWidth(void)
{
    return PORT_GBA_W;
}

int Port_ViewHeight(void)
{
    return PORT_GBA_H;
}

int Port_ViewZoomHeight(void)
{
    return PORT_GBA_H;
}

void Port_ViewBeforeRender(int *width, int *height)
{
    int i;
    virtuappu_mode1_obj_port = 0;
    virtuappu_mode1_win_dx = virtuappu_mode1_win_dy = 0;
    virtuappu_mode1_line_dy = 0;
    for (i = 0; i < 4; i++)
        virtuappu_mode1_bg_dx[i] = virtuappu_mode1_bg_dy[i] = 0;
    *width = PORT_GBA_W;
    *height = PORT_GBA_H;
}
