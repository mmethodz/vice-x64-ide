/**
 * \file video.c
 * \brief Headless UI video stuff
 *
 * \author Marco van den Heuvel <blackystardust68@yahoo.com>
 * \author Michael C. Martin <mcmartin@gmail.com>
 */

/* This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmdline.h"
#include "machine.h"
#include "palette.h"
#include "resources.h"
#include "videoarch.h"
#include "video.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define VICE_WEB_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define VICE_WEB_KEEPALIVE
#endif

#define WEB_FRAME_DIAGNOSTIC_INTERVAL 50

static uint8_t *web_frame_pixels = NULL;
static unsigned int web_frame_width = 0;
static unsigned int web_frame_height = 0;
static unsigned int web_frame_number = 0;
static unsigned int web_frame_refresh_count = 0;
static unsigned int web_frame_source_nonzero = 0;
static unsigned int web_frame_render_nonblack = 0;
static int web_frame_rendermode = VIDEO_RENDER_NULL;
static int web_frame_palette_entries = 0;
static int web_frame_ready = 0;

VICE_WEB_KEEPALIVE uint8_t *vice_web_frame_pixels(void);
VICE_WEB_KEEPALIVE int vice_web_frame_width(void);
VICE_WEB_KEEPALIVE int vice_web_frame_height(void);
VICE_WEB_KEEPALIVE int vice_web_frame_number(void);
VICE_WEB_KEEPALIVE int vice_web_frame_refresh_count(void);
VICE_WEB_KEEPALIVE int vice_web_frame_source_nonzero(void);
VICE_WEB_KEEPALIVE int vice_web_frame_render_nonblack(void);
VICE_WEB_KEEPALIVE int vice_web_frame_rendermode(void);
VICE_WEB_KEEPALIVE int vice_web_frame_palette_entries(void);
VICE_WEB_KEEPALIVE int vice_web_frame_ready(void);

static void web_frame_clear(void)
{
    unsigned int i;
    unsigned int pixels = web_frame_width * web_frame_height;

    if (web_frame_pixels == NULL) {
        return;
    }

    for (i = 0; i < pixels; i++) {
        web_frame_pixels[i * 4] = 0;
        web_frame_pixels[i * 4 + 1] = 0;
        web_frame_pixels[i * 4 + 2] = 0;
        web_frame_pixels[i * 4 + 3] = 255;
    }
}

static int web_frame_ensure_size(unsigned int width, unsigned int height)
{
    uint8_t *pixels;
    size_t byte_size;

    if (width == 0 || height == 0) {
        return -1;
    }
    if (width == web_frame_width && height == web_frame_height && web_frame_pixels != NULL) {
        return 0;
    }

    byte_size = (size_t)width * (size_t)height * 4u;
    pixels = (uint8_t *)realloc(web_frame_pixels, byte_size);
    if (pixels == NULL) {
        return -1;
    }

    web_frame_pixels = pixels;
    web_frame_width = width;
    web_frame_height = height;
    web_frame_ready = 0;
    web_frame_clear();
    return 0;
}

static unsigned int web_frame_count_nonblack(uint8_t *pixels, unsigned int width, unsigned int height)
{
    unsigned int i;
    unsigned int count = 0;
    unsigned int pixels_count = width * height;

    if (pixels == NULL) {
        return 0;
    }

    for (i = 0; i < pixels_count; i++) {
        if (pixels[i * 4] || pixels[i * 4 + 1] || pixels[i * 4 + 2]) {
            count++;
        }
    }
    return count;
}

static unsigned int web_frame_count_source_nonzero(video_canvas_t *canvas,
                                                   unsigned int xs,
                                                   unsigned int ys,
                                                   unsigned int w,
                                                   unsigned int h)
{
    unsigned int x;
    unsigned int y;
    unsigned int count = 0;
    unsigned int source_width;
    unsigned int source_height;
    unsigned int source_pitch;

    if (canvas == NULL || canvas->draw_buffer == NULL || canvas->draw_buffer->draw_buffer == NULL) {
        return 0;
    }

    source_width = canvas->draw_buffer->draw_buffer_width;
    source_height = canvas->draw_buffer->draw_buffer_height;
    source_pitch = canvas->draw_buffer->draw_buffer_pitch ? canvas->draw_buffer->draw_buffer_pitch : source_width;

    if (source_width == 0 || source_height == 0 || xs >= source_width || ys >= source_height) {
        return 0;
    }
    if (xs + w > source_width) {
        w = source_width - xs;
    }
    if (ys + h > source_height) {
        h = source_height - ys;
    }

    for (y = 0; y < h; y++) {
        const uint8_t *src = canvas->draw_buffer->draw_buffer + ((ys + y) * source_pitch) + xs;
        for (x = 0; x < w; x++) {
            if (src[x] != 0) {
                count++;
            }
        }
    }
    return count;
}

static void web_frame_set_palette(video_canvas_t *canvas, palette_t *palette)
{
    unsigned int i;
    video_render_color_tables_t *color_tables;

    if (canvas == NULL || canvas->videoconfig == NULL || palette == NULL) {
        return;
    }

    canvas->palette = palette;
    color_tables = &canvas->videoconfig->color_tables;
    web_frame_palette_entries = (int)palette->num_entries;

    for (i = 0; i < palette->num_entries; i++) {
#ifdef WORDS_BIGENDIAN
        uint32_t color = ((uint32_t)palette->entries[i].red << 24)
            | ((uint32_t)palette->entries[i].green << 16)
            | ((uint32_t)palette->entries[i].blue << 8)
            | 0xffU;
#else
        uint32_t color = (uint32_t)palette->entries[i].red
            | ((uint32_t)palette->entries[i].green << 8)
            | ((uint32_t)palette->entries[i].blue << 16)
            | (0xffU << 24);
#endif
        video_render_setphysicalcolor(canvas->videoconfig, (int)i, color, 32);
    }

#ifdef WORDS_BIGENDIAN
    for (i = 0; i < 256; i++) {
        video_render_setrawrgb(color_tables, i, i << 24, i << 16, i << 8);
    }
    video_render_setrawalpha(color_tables, 0xffU);
#else
    for (i = 0; i < 256; i++) {
        video_render_setrawrgb(color_tables, i, i, i << 8, i << 16);
    }
    video_render_setrawalpha(color_tables, 0xffU << 24);
#endif
    video_render_initraw(canvas->videoconfig);
}

VICE_WEB_KEEPALIVE uint8_t *vice_web_frame_pixels(void)
{
    return web_frame_pixels;
}

VICE_WEB_KEEPALIVE int vice_web_frame_width(void)
{
    return (int)web_frame_width;
}

VICE_WEB_KEEPALIVE int vice_web_frame_height(void)
{
    return (int)web_frame_height;
}

VICE_WEB_KEEPALIVE int vice_web_frame_number(void)
{
    return (int)web_frame_number;
}

VICE_WEB_KEEPALIVE int vice_web_frame_refresh_count(void)
{
    return (int)web_frame_refresh_count;
}

VICE_WEB_KEEPALIVE int vice_web_frame_source_nonzero(void)
{
    return (int)web_frame_source_nonzero;
}

VICE_WEB_KEEPALIVE int vice_web_frame_render_nonblack(void)
{
    return (int)web_frame_render_nonblack;
}

VICE_WEB_KEEPALIVE int vice_web_frame_rendermode(void)
{
    return web_frame_rendermode;
}

VICE_WEB_KEEPALIVE int vice_web_frame_palette_entries(void)
{
    return web_frame_palette_entries;
}

VICE_WEB_KEEPALIVE int vice_web_frame_ready(void)
{
    return web_frame_ready;
}


/** \brief  Command line options related to generic video output
 */
static const cmdline_option_t cmdline_options[] =
{
    CMDLINE_LIST_END
};


/** \brief  Integer/boolean resources related to video output
 */
static const resource_int_t resources_int[] =
{
    RESOURCE_INT_LIST_END
};

/** \brief  Arch-sepcific function to check which chip is
 *          currently "active", or has the focus of the user.
 *
 * Note: this version always returns VIDEO_CHIP_VICII since the headless build
 * has no concept of a canvas or even an active window, and this function only
 * makes sense in a "headful" build.
 */
int video_arch_get_active_chip(void)
{
    return VIDEO_CHIP_VICII;
}

/** \brief  Arch-specific initialization for a video canvas
 *  \param[inout] canvas The canvas being initialized
 *  \sa video_canvas_create
 */
void video_arch_canvas_init(struct video_canvas_s *canvas)
{
    /* printf("%s\n", __func__); */
}


/** \brief  Initialize command line options for generic video resouces
 *
 * \return  0 on success, < 0 on failure
 */
int video_arch_cmdline_options_init(void)
{
    /* printf("%s\n", __func__); */

    if (machine_class != VICE_MACHINE_VSID) {
        return cmdline_register_options(cmdline_options);
    }
    return 0;
}


/** \brief  Initialize video-related resources
 *
 * \return  0 on success, < on failure
 */
int video_arch_resources_init(void)
{
    /* printf("%s\n", __func__); */

    if (machine_class != VICE_MACHINE_VSID) {
        return resources_register_int(resources_int);
    }
    return 0;
}

/** \brief Clean up any memory held by arch-specific video resources. */
void video_arch_resources_shutdown(void)
{
    /* printf("%s\n", __func__); */
}

/** \brief Query whether a canvas is resizable.
 *  \param canvas The canvas to query
 *  \return TRUE if the canvas can be resized.
 */
char video_canvas_can_resize(video_canvas_t *canvas)
{
    /* printf("%s\n", __func__); */

    return 1;
}

/** \brief Create a new video_canvas_s.
 *  \param[inout] canvas A freshly allocated canvas object.
 *  \param[in]    width  Pointer to a width value. May be NULL if canvas
 *                       size is not yet known.
 *  \param[in]    height Pointer to a height value. May be NULL if canvas
 *                       size is not yet known.
 *  \param        mapped Unused.
 *  \return The completely initialized canvas. The window that holds
 *          it will be visible in the UI at time of return.
 */
video_canvas_t *video_canvas_create(video_canvas_t *canvas,
                                    unsigned int *width, unsigned int *height,
                                    int mapped)
{
    /* printf("%s\n", __func__); */

    canvas->created = 1;

    return canvas;
}

/** \brief Free a previously created video canvas and all its
 *         components.
 *  \param[in] canvas The canvas to destroy.
 */
void video_canvas_destroy(struct video_canvas_s *canvas)
{
    /* printf("%s\n", __func__); */
}

/** \brief Update the display on a video canvas to reflect the machine
 *         state.
 * \param canvas The canvas to update.
 * \param xs     A parameter to forward to video_canvas_render()
 * \param ys     A parameter to forward to video_canvas_render()
 * \param xi     X coordinate of the leftmost pixel to update
 * \param yi     Y coordinate of the topmost pixel to update
 * \param w      Width of the rectangle to update
 * \param h      Height of the rectangle to update
 */
void video_canvas_refresh(struct video_canvas_s *canvas,
                          unsigned int xs, unsigned int ys,
                          unsigned int xi, unsigned int yi,
                          unsigned int w, unsigned int h)
{
    unsigned int target_width;
    unsigned int target_height;
    unsigned int source_w = w;
    unsigned int source_h = h;
    int update_diagnostics;

    if (canvas == NULL || canvas->draw_buffer == NULL || canvas->draw_buffer->draw_buffer == NULL) {
        return;
    }

    if (canvas->videoconfig != NULL) {
        web_frame_rendermode = canvas->videoconfig->rendermode;
    }
    update_diagnostics = !web_frame_ready || (web_frame_refresh_count % WEB_FRAME_DIAGNOSTIC_INTERVAL) == 0;
    if (update_diagnostics) {
        web_frame_source_nonzero = web_frame_count_source_nonzero(canvas, xs, ys, source_w, source_h);
    }

    target_width = canvas->draw_buffer->canvas_physical_width
        ? canvas->draw_buffer->canvas_physical_width
        : (canvas->draw_buffer->canvas_width ? canvas->draw_buffer->canvas_width : canvas->draw_buffer->visible_width);
    target_height = canvas->draw_buffer->canvas_physical_height
        ? canvas->draw_buffer->canvas_physical_height
        : (canvas->draw_buffer->canvas_height ? canvas->draw_buffer->canvas_height : canvas->draw_buffer->visible_height);

    if (web_frame_ensure_size(target_width, target_height) < 0) {
        return;
    }
    if (xi >= canvas->draw_buffer->canvas_width || yi >= canvas->draw_buffer->canvas_height) {
        return;
    }

    if (xi + w > canvas->draw_buffer->canvas_width) {
        w = canvas->draw_buffer->canvas_width - xi;
    }
    if (yi + h > canvas->draw_buffer->canvas_height) {
        h = canvas->draw_buffer->canvas_height - yi;
    }

    video_canvas_render(canvas, web_frame_pixels, (int)w, (int)h, (int)xs, (int)ys, (int)xi, (int)yi, (int)(target_width * 4));
    if (update_diagnostics) {
        web_frame_render_nonblack = web_frame_count_nonblack(web_frame_pixels, target_width, target_height);
    }
    web_frame_refresh_count++;
    web_frame_number++;
    web_frame_ready = 1;
}

/** \brief Update canvas size to match the draw buffer size requested
 *         by the emulation core.
 * \param canvas The video canvas to update.
 * \param resize_canvas Ignored - the canvas will always resize.
 */

void video_canvas_resize(struct video_canvas_s *canvas, char resize_canvas)
{
    unsigned int width;
    unsigned int height;

    if (canvas == NULL || canvas->draw_buffer == NULL) {
        return;
    }

    width = canvas->draw_buffer->canvas_physical_width
        ? canvas->draw_buffer->canvas_physical_width
        : (canvas->draw_buffer->canvas_width ? canvas->draw_buffer->canvas_width : canvas->draw_buffer->visible_width);
    height = canvas->draw_buffer->canvas_physical_height
        ? canvas->draw_buffer->canvas_physical_height
        : (canvas->draw_buffer->canvas_height ? canvas->draw_buffer->canvas_height : canvas->draw_buffer->visible_height);
    (void)web_frame_ensure_size(width, height);
}

/** \brief Assign a palette to the canvas.
 * \param canvas The canvas to update the palette
 * \param palette The new palette to assign
 * \return Zero on success, nonzero on failure
 */
int video_canvas_set_palette(struct video_canvas_s *canvas,
                             struct palette_s *palette)
{
    /* printf("%s\n", __func__); */

    web_frame_set_palette(canvas, palette);

    return 0;
}

/** \brief Perform any frontend-specific initialization.
 *  \return 0 on success, nonzero on failure
 */
int video_init(void)
{
    /* printf("%s\n", __func__); */

    return 0;
}

/** \brief Perform any frontend-specific uninitialization. */
void video_shutdown(void)
{
    /* printf("%s\n", __func__); */
}
