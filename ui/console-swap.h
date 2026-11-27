/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef UI_CONSOLE_SWAP_H
#define UI_CONSOLE_SWAP_H

/* Called with the BQL held, like dpy_gfx_replace_surface. */
static bool console_swap_listeners_allow(QemuConsole *con,
                                         QemuConsole *active)
{
    DisplayChangeListener *dcl;
    bool listener = false;

    if (con->gl) {
        return false;
    }
    QLIST_FOREACH(dcl, &con->ds->listeners, next) {
        if (con != (dcl->con ? dcl->con : active)) {
            continue;
        }
        listener = true;
        if (!dcl->ops->dpy_gfx_can_swap || !dcl->ops->dpy_gfx_can_swap(dcl)) {
            return false;
        }
    }
    return listener;
}

#endif
