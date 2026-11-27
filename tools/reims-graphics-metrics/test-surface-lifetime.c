/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Standalone pixman lifetime regression for the actual surface helper. */
#include <glib.h>
#include <pixman.h>
#include <stdint.h>

typedef struct DisplaySurface {
    pixman_image_t *image;
} DisplaySurface;

static DisplaySurface *qemu_create_displaysurface_from(int width, int height,
                                                       pixman_format_code_t fmt,
                                                       int stride, uint8_t *data)
{
    DisplaySurface *s = g_new(DisplaySurface, 1);
    s->image = pixman_image_create_bits(fmt, width, height,
                                        (uint32_t *)data, stride);
    g_assert_nonnull(s->image);
    return s;
}

static unsigned int pixels_freed;
static void test_pixels_free(void *p)
{
    pixels_freed++;
    g_free(p);
}

#define g_free test_pixels_free
#include "../../hw/display/reims/reims-readback-surface.h"
#undef g_free

int main(void)
{
    uint8_t *a = g_malloc0(4 * 8 * 8);
    uint8_t *b = g_malloc0(4 * 16 * 8);
    DisplaySurface *first = reims_graphics_surface_from_pixels(8, 8, a);
    pixman_image_t *retained = pixman_image_ref(first->image);
    DisplaySurface *resized;

    a[0] = 0x31;
    pixman_image_unref(first->image); /* surface replacement */
    g_free(first);
    g_assert_cmpuint(pixels_freed, ==, 0);
    resized = reims_graphics_surface_from_pixels(16, 8, b);
    g_assert_cmpuint(((uint8_t *)pixman_image_get_data(retained))[0], ==, 0x31);
    pixman_image_unref(resized->image); /* console teardown */
    g_free(resized);
    g_assert_cmpuint(pixels_freed, ==, 1);
    pixman_image_unref(retained); /* screendump/listener releases old frame */
    g_assert_cmpuint(pixels_freed, ==, 2);
    return 0;
}
