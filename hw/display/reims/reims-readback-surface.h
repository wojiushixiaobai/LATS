/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef REIMS_READBACK_SURFACE_H
#define REIMS_READBACK_SURFACE_H

/* The pixman image, not the device or DisplaySurface, owns these pixels. */
static void reims_graphics_pci_free_pixels(pixman_image_t *image, void *opaque)
{
    (void)image;
    g_free(opaque);
}

static DisplaySurface *reims_graphics_surface_from_pixels(uint32_t width,
                                                          uint32_t height,
                                                          uint8_t *pixels)
{
    DisplaySurface *surface = qemu_create_displaysurface_from(
        width, height, PIXMAN_x8r8g8b8, (size_t)width * 4, pixels);

    pixman_image_set_destroy_function(surface->image,
                                      reims_graphics_pci_free_pixels, pixels);
    return surface;
}

#endif
