/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the production listener and GTK guards with lightweight stubs. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct QemuConsole QemuConsole;
typedef struct DisplayChangeListener DisplayChangeListener;
typedef struct DisplayChangeListenerOps {
    bool (*dpy_gfx_can_swap)(DisplayChangeListener *);
} DisplayChangeListenerOps;
struct DisplayChangeListener {
    const DisplayChangeListenerOps *ops;
    QemuConsole *con;
    DisplayChangeListener *next;
};
typedef struct { DisplayChangeListener *first; } ListenerList;
typedef struct { ListenerList listeners; } DisplayState;
struct QemuConsole { void *gl; DisplayState *ds; };
#define QLIST_FOREACH(it, head, field) \
    for ((it) = (head)->first; (it); (it) = (it)->field)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

typedef struct { bool realized; void *window; } GtkWidget;
static bool gtk_widget_get_realized(GtkWidget *w) { return w->realized; }
static void *gtk_widget_get_window(GtkWidget *w) { return w->window; }
typedef struct {
    struct { DisplayChangeListener dcl; GtkWidget *drawing_area; } gfx;
} VirtualConsole;

#include "../../ui/console-swap.h"
#include "../../ui/gtk-swap.h"

int main(void)
{
    const DisplayChangeListenerOps gtk = { .dpy_gfx_can_swap = gd_can_swap };
    const DisplayChangeListenerOps vnc = { .dpy_gfx_can_swap = NULL };
    GtkWidget area = { .realized = true, .window = &area };
    DisplayState ds = {0};
    QemuConsole a = { .ds = &ds }, b = { .ds = &ds };
    VirtualConsole gui = { .gfx = { .dcl = { .ops = &gtk, .con = &a },
                                     .drawing_area = &area } };
    DisplayChangeListener remote = { .ops = &vnc, .con = &a };

    assert(!console_swap_listeners_allow(&a, &a)); /* no listener */
    ds.listeners.first = &gui.gfx.dcl;
    assert(console_swap_listeners_allow(&a, &a));
    area.realized = false;
    assert(!console_swap_listeners_allow(&a, &a));
    area.realized = true; area.window = NULL;
    assert(!console_swap_listeners_allow(&a, &a));
    area.window = &area;
    a.gl = &a;
    assert(!console_swap_listeners_allow(&a, &a)); /* GL always falls back */
    a.gl = NULL;
    gui.gfx.dcl.next = &remote;
    assert(!console_swap_listeners_allow(&a, &a)); /* GTK + VNC */
    ds.listeners.first = &remote; remote.next = &gui.gfx.dcl;
    gui.gfx.dcl.next = NULL;
    assert(!console_swap_listeners_allow(&a, &a)); /* reverse order */
    remote.con = &b;
    assert(console_swap_listeners_allow(&a, &a)); /* other console ignored */
    remote.con = NULL;
    assert(!console_swap_listeners_allow(&a, &a)); /* active-console listener */
    assert(console_swap_listeners_allow(&a, &b)); /* only explicit GTK matches */
    assert(!console_swap_listeners_allow(&b, &b)); /* VNC alone */
    return 0;
}
