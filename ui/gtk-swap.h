/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef UI_GTK_SWAP_H
#define UI_GTK_SWAP_H

static bool gd_can_swap(DisplayChangeListener *dcl)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    GtkWidget *area = vc->gfx.drawing_area;

    /* gd_switch's redraw uses the window, which is absent while unrealized. */
    return gtk_widget_get_realized(area) && gtk_widget_get_window(area);
}

#endif
