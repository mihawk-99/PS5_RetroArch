/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2026 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

/* PS5 OpenGL context driver.
 *
 * The console's GL runtime (the bundled ps5-opengl SDK: Mesa gallium over the
 * Agc command processor) exposes a fullscreen EGL: there is no window system
 * to describe a surface to, so eglCreateWindowSurface takes a null native
 * window on EGL_DEFAULT_DISPLAY and the presenter's size is fixed at 1080p.
 * The structure follows gfx/drivers_context/xegl_ctx.c - the API bind records
 * the requested version, which set_video_mode then turns into the
 * EGL_CONTEXT_*_VERSION_KHR / core-profile attributes gl3's context wants.
 */

#include <stdlib.h>
#include <string/stdstring.h>
#include <compat/strl.h>
#include <EGL/egl.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_EGL
#include <gfx/common/egl_common.h>
#endif

#include <frontend/frontend_driver.h>
#include <gfx/video_driver.h>

#define ATTR_PS5GL_WIDTH 1920
#define ATTR_PS5GL_HEIGHT 1080

typedef struct
{
#ifdef HAVE_EGL
   egl_ctx_data_t egl;
#endif
   unsigned width, height;
   float refresh_rate;
} ps5_ctx_data_t;

static enum gfx_ctx_api ps5_ctx_api = GFX_CTX_OPENGL_API;

static void ps5_ctx_destroy(void *data)
{
   ps5_ctx_data_t *ps5 = (ps5_ctx_data_t *)data;

   if (ps5)
   {
#ifdef HAVE_EGL
      egl_destroy(&ps5->egl);
#endif
      free(ps5);
   }
}

static void ps5_ctx_get_video_size(void *data,
      unsigned *width, unsigned *height)
{
   /* The EGL presenter owns the whole output; the size it was opened with is
    * the size it has. */
   *width  = ATTR_PS5GL_WIDTH;
   *height = ATTR_PS5GL_HEIGHT;
}

static void *ps5_ctx_init(void *video_driver)
{
#ifdef HAVE_EGL
   EGLint n;
   EGLint major, minor;
   static const EGLint attribs[] = {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 24,
      EGL_STENCIL_SIZE, 8,
      EGL_SAMPLE_BUFFERS, 0,
      EGL_SAMPLES, 0,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE};
#endif
   ps5_ctx_data_t *ps5 = (ps5_ctx_data_t *)calloc(1, sizeof(*ps5));

   if (!ps5)
      return NULL;

#ifdef HAVE_EGL
   if (!egl_init_context(&ps5->egl, EGL_NONE, EGL_DEFAULT_DISPLAY,
            &major, &minor, &n, attribs, NULL))
   {
      egl_report_error();
      goto error;
   }
#endif

   return ps5;

error:
   ps5_ctx_destroy(ps5);
   return NULL;
}

static void ps5_ctx_check_window(void *data, bool *quit,
      bool *resize, unsigned *width, unsigned *height)
{
   *width  = ATTR_PS5GL_WIDTH;
   *height = ATTR_PS5GL_HEIGHT;
   *resize = false;
   *quit   = false;
}

static bool ps5_ctx_set_video_mode(void *data,
      unsigned width, unsigned height,
      bool fullscreen)
{
#ifdef HAVE_EGL
   EGLint attribs[16];
   EGLint *attr = attribs;
#endif
   ps5_ctx_data_t *ps5 = (ps5_ctx_data_t *)data;

   ps5->width        = ATTR_PS5GL_WIDTH;
   ps5->height       = ATTR_PS5GL_HEIGHT;
   ps5->refresh_rate = 60.0f;

#ifdef HAVE_EGL
   {
      unsigned version = ps5->egl.major * 1000 + ps5->egl.minor;

      if (version >= 3001)
      {
         *attr++ = EGL_CONTEXT_MAJOR_VERSION_KHR;
         *attr++ = (EGLint)ps5->egl.major;
         *attr++ = EGL_CONTEXT_MINOR_VERSION_KHR;
         *attr++ = (EGLint)ps5->egl.minor;

         /* Technically, we don't have core/compat until 3.2. */
         if (version >= 3002)
         {
            *attr++ = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
            *attr++ = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR;
         }
      }
   }

   *attr = EGL_NONE;

   if (!egl_create_context(&ps5->egl, attribs))
      goto error;
   if (!egl_create_surface(&ps5->egl, 0))
      goto error;
#endif

   return true;

error:
#ifdef HAVE_EGL
   egl_report_error();
#endif
   ps5_ctx_destroy(data);

   return false;
}

static void ps5_ctx_input_driver(void *data,
      const char *name,
      input_driver_t **input, void **input_data)
{
   *input      = NULL;
   *input_data = NULL;
}

static enum gfx_ctx_api ps5_ctx_get_api(void *data) { return ps5_ctx_api; }

static bool ps5_ctx_bind_api(void *data,
      enum gfx_ctx_api api, unsigned major, unsigned minor)
{
   g_egl_major = major;
   g_egl_minor = minor;
   ps5_ctx_api = api;

   switch (api)
   {
      case GFX_CTX_OPENGL_API:
         return egl_bind_api(EGL_OPENGL_API);
      case GFX_CTX_OPENGL_ES_API:
         return egl_bind_api(EGL_OPENGL_ES_API);
      default:
         break;
   }

   return false;
}

static bool ps5_ctx_has_focus(void *data) { return true; }

static bool ps5_ctx_suppress_screensaver(void *data, bool enable) { return false; }

static void ps5_ctx_set_swap_interval(void *data, int swap_interval)
{
#ifdef HAVE_EGL
   ps5_ctx_data_t *ps5 = (ps5_ctx_data_t *)data;
   egl_set_swap_interval(&ps5->egl, swap_interval);
#endif
}

static void ps5_ctx_swap_buffers(void *data)
{
#ifdef HAVE_EGL
   ps5_ctx_data_t *ps5 = (ps5_ctx_data_t *)data;
   egl_swap_buffers(&ps5->egl);
#endif
}

static void ps5_ctx_bind_hw_render(void *data, bool enable)
{
#ifdef HAVE_EGL
   ps5_ctx_data_t *ps5 = (ps5_ctx_data_t *)data;
   egl_bind_hw_render(&ps5->egl, enable);
#endif
}

static uint32_t ps5_ctx_get_flags(void *data)
{
   uint32_t flags = 0;

   if (string_is_equal(video_driver_get_ident(), "glcore"))
   {
#if defined(HAVE_SLANG) && defined(HAVE_SPIRV_CROSS)
      BIT32_SET(flags, GFX_CTX_FLAGS_SHADERS_SLANG);
#endif
   }
#ifdef HAVE_GLSL
   BIT32_SET(flags, GFX_CTX_FLAGS_SHADERS_GLSL);
#endif

   return flags;
}

static void ps5_ctx_set_flags(void *data, uint32_t flags) { }

static float ps5_ctx_get_refresh_rate(void *data)
{
   ps5_ctx_data_t *ps5 = (ps5_ctx_data_t *)data;
   return ps5->refresh_rate;
}

const gfx_ctx_driver_t ps5_egl_ctx = {
    ps5_ctx_init,
    ps5_ctx_destroy,
    ps5_ctx_get_api,
    ps5_ctx_bind_api,
    ps5_ctx_set_swap_interval,
    ps5_ctx_set_video_mode,
    ps5_ctx_get_video_size,
    ps5_ctx_get_refresh_rate,
    NULL, /* get_video_output_size */
    NULL, /* get_video_output_prev */
    NULL, /* get_video_output_next */
    NULL, /* get_metrics */
    NULL,
    NULL, /* update_title */
    ps5_ctx_check_window,
    NULL, /* set_resize */
    ps5_ctx_has_focus,
    ps5_ctx_suppress_screensaver,
    false, /* has_windowed */
    ps5_ctx_swap_buffers,
    ps5_ctx_input_driver,
#ifdef HAVE_EGL
    egl_get_proc_address,
#else
    NULL,
#endif
    NULL,
    NULL,
    NULL,
    "egl_ps5",
    ps5_ctx_get_flags,
    ps5_ctx_set_flags,
    ps5_ctx_bind_hw_render,
    NULL,
    NULL};
