/*
 * Mesa 3-D graphics library
 * Version:  7.9
 *
 * Copyright (C) 2009-2010 Chia-I Wu <olv@0xlab.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "egldriver.h"
#include "eglcurrent.h"
#include "egllog.h"

#include "pipe/p_screen.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"

#include "egl_g3d.h"
#include "egl_g3d_api.h"
#include "egl_g3d_image.h"
#include "egl_g3d_st.h"
#include "native.h"

/**
 * Return the state tracker for the given context.
 */
static struct st_api *
egl_g3d_choose_st(_EGLDriver *drv, _EGLContext *ctx)
{
   struct egl_g3d_driver *gdrv = egl_g3d_driver(drv);
   struct st_api *stapi;
   EGLint idx = -1;

   switch (ctx->ClientAPI) {
   case EGL_OPENGL_ES_API:
      switch (ctx->ClientVersion) {
      case 1:
         idx = ST_API_OPENGL_ES1;
         break;
      case 2:
         idx = ST_API_OPENGL_ES2;
         break;
      default:
         _eglLog(_EGL_WARNING, "unknown client version %d",
               ctx->ClientVersion);
         break;
      }
      break;
   case EGL_OPENVG_API:
      idx = ST_API_OPENVG;
      break;
   case EGL_OPENGL_API:
      idx = ST_API_OPENGL;
      break;
   default:
      _eglLog(_EGL_WARNING, "unknown client API 0x%04x", ctx->ClientAPI);
      break;
   }

   stapi = (idx >= 0) ? gdrv->stapis[idx] : NULL;
   return stapi;
}

static _EGLContext *
egl_g3d_create_context(_EGLDriver *drv, _EGLDisplay *dpy, _EGLConfig *conf,
                       _EGLContext *share, const EGLint *attribs)
{
   struct egl_g3d_display *gdpy = egl_g3d_display(dpy);
   struct egl_g3d_context *gshare = egl_g3d_context(share);
   struct egl_g3d_config *gconf = egl_g3d_config(conf);
   struct egl_g3d_context *gctx;

   gctx = CALLOC_STRUCT(egl_g3d_context);
   if (!gctx) {
      _eglError(EGL_BAD_ALLOC, "eglCreateContext");
      return NULL;
   }

   if (!_eglInitContext(&gctx->base, dpy, conf, attribs)) {
      free(gctx);
      return NULL;
   }

   gctx->stapi = egl_g3d_choose_st(drv, &gctx->base);
   if (!gctx->stapi) {
      free(gctx);
      return NULL;
   }

   gctx->stctxi = gctx->stapi->create_context(gctx->stapi, gdpy->smapi,
         &gconf->stvis, (gshare) ? gshare->stctxi : NULL);
   if (!gctx->stctxi) {
      free(gctx);
      return NULL;
   }

   gctx->stctxi->st_manager_private = (void *) &gctx->base;

   return &gctx->base;
}

/**
 * Destroy a context.
 */
static void
destroy_context(_EGLDisplay *dpy, _EGLContext *ctx)
{
   struct egl_g3d_context *gctx = egl_g3d_context(ctx);

   /* FIXME a context might live longer than its display */
   if (!dpy->Initialized)
      _eglLog(_EGL_FATAL, "destroy a context with an unitialized display");

   gctx->stctxi->destroy(gctx->stctxi);

   free(gctx);
}

static EGLBoolean
egl_g3d_destroy_context(_EGLDriver *drv, _EGLDisplay *dpy, _EGLContext *ctx)
{
   if (!_eglIsContextBound(ctx))
      destroy_context(dpy, ctx);
   return EGL_TRUE;
}

struct egl_g3d_create_surface_arg {
   EGLint type;
   union {
      EGLNativeWindowType win;
      EGLNativePixmapType pix;
   } u;
};

static _EGLSurface *
egl_g3d_create_surface(_EGLDriver *drv, _EGLDisplay *dpy, _EGLConfig *conf,
                       struct egl_g3d_create_surface_arg *arg,
                       const EGLint *attribs)
{
   struct egl_g3d_display *gdpy = egl_g3d_display(dpy);
   struct egl_g3d_config *gconf = egl_g3d_config(conf);
   struct egl_g3d_surface *gsurf;
   struct native_surface *nsurf;
   const char *err;

   switch (arg->type) {
   case EGL_WINDOW_BIT:
      err = "eglCreateWindowSurface";
      break;
   case EGL_PIXMAP_BIT:
      err = "eglCreatePixmapSurface";
      break;
#ifdef EGL_MESA_screen_surface
   case EGL_SCREEN_BIT_MESA:
      err = "eglCreateScreenSurface";
      break;
#endif
   default:
      err = "eglCreateUnknownSurface";
      break;
   }

   gsurf = CALLOC_STRUCT(egl_g3d_surface);
   if (!gsurf) {
      _eglError(EGL_BAD_ALLOC, err);
      return NULL;
   }

   if (!_eglInitSurface(&gsurf->base, dpy, arg->type, conf, attribs)) {
      free(gsurf);
      return NULL;
   }

   /* create the native surface */
   switch (arg->type) {
   case EGL_WINDOW_BIT:
      nsurf = gdpy->native->create_window_surface(gdpy->native,
            arg->u.win, gconf->native);
      break;
   case EGL_PIXMAP_BIT:
      nsurf = gdpy->native->create_pixmap_surface(gdpy->native,
            arg->u.pix, gconf->native);
      break;
#ifdef EGL_MESA_screen_surface
   case EGL_SCREEN_BIT_MESA:
      /* prefer back buffer (move to _eglInitSurface?) */
      gsurf->base.RenderBuffer = EGL_BACK_BUFFER;
      nsurf = gdpy->native->modeset->create_scanout_surface(gdpy->native,
            gconf->native, gsurf->base.Width, gsurf->base.Height);
      break;
#endif
   default:
      nsurf = NULL;
      break;
   }

   if (!nsurf) {
      free(gsurf);
      return NULL;
   }
   /* initialize the geometry */
   if (!nsurf->validate(nsurf, 0x0, &gsurf->sequence_number, NULL,
            &gsurf->base.Width, &gsurf->base.Height)) {
      nsurf->destroy(nsurf);
      free(gsurf);
      return NULL;
   }

   gsurf->stvis = gconf->stvis;
   if (gsurf->base.RenderBuffer == EGL_SINGLE_BUFFER)
      gsurf->stvis.render_buffer = ST_ATTACHMENT_FRONT_LEFT;

   gsurf->stfbi = egl_g3d_create_st_framebuffer(&gsurf->base);
   if (!gsurf->stfbi) {
      nsurf->destroy(nsurf);
      free(gsurf);
      return NULL;
   }

   nsurf->user_data = &gsurf->base;
   gsurf->native = nsurf;

   return &gsurf->base;
}

static _EGLSurface *
egl_g3d_create_window_surface(_EGLDriver *drv, _EGLDisplay *dpy,
                              _EGLConfig *conf, EGLNativeWindowType win,
                              const EGLint *attribs)
{
   struct egl_g3d_create_surface_arg arg;

   memset(&arg, 0, sizeof(arg));
   arg.type = EGL_WINDOW_BIT;
   arg.u.win = win;

   return egl_g3d_create_surface(drv, dpy, conf, &arg, attribs);
}

static _EGLSurface *
egl_g3d_create_pixmap_surface(_EGLDriver *drv, _EGLDisplay *dpy,
                              _EGLConfig *conf, EGLNativePixmapType pix,
                              const EGLint *attribs)
{
   struct egl_g3d_create_surface_arg arg;

   memset(&arg, 0, sizeof(arg));
   arg.type = EGL_PIXMAP_BIT;
   arg.u.pix = pix;

   return egl_g3d_create_surface(drv, dpy, conf, &arg, attribs);
}

static _EGLSurface *
egl_g3d_create_pbuffer_surface(_EGLDriver *drv, _EGLDisplay *dpy,
                               _EGLConfig *conf, const EGLint *attribs)
{
   struct egl_g3d_config *gconf = egl_g3d_config(conf);
   struct egl_g3d_surface *gsurf;

   gsurf = CALLOC_STRUCT(egl_g3d_surface);
   if (!gsurf) {
      _eglError(EGL_BAD_ALLOC, "eglCreatePbufferSurface");
      return NULL;
   }

   if (!_eglInitSurface(&gsurf->base, dpy, EGL_PBUFFER_BIT, conf, attribs)) {
      free(gsurf);
      return NULL;
   }

   gsurf->stvis = gconf->stvis;

   gsurf->stfbi = egl_g3d_create_st_framebuffer(&gsurf->base);
   if (!gsurf->stfbi) {
      free(gsurf);
      return NULL;
   }

   return &gsurf->base;
}

/**
 * Destroy a surface.
 */
static void
destroy_surface(_EGLDisplay *dpy, _EGLSurface *surf)
{
   struct egl_g3d_surface *gsurf = egl_g3d_surface(surf);

   /* FIXME a surface might live longer than its display */
   if (!dpy->Initialized)
      _eglLog(_EGL_FATAL, "destroy a surface with an unitialized display");

   pipe_resource_reference(&gsurf->render_texture, NULL);
   egl_g3d_destroy_st_framebuffer(gsurf->stfbi);
   if (gsurf->native)
      gsurf->native->destroy(gsurf->native);
   free(gsurf);
}

static EGLBoolean
egl_g3d_destroy_surface(_EGLDriver *drv, _EGLDisplay *dpy, _EGLSurface *surf)
{
   if (!_eglIsSurfaceBound(surf))
      destroy_surface(dpy, surf);
   return EGL_TRUE;
}

static EGLBoolean
egl_g3d_make_current(_EGLDriver *drv, _EGLDisplay *dpy,
                     _EGLSurface *draw, _EGLSurface *read, _EGLContext *ctx)
{
   struct egl_g3d_context *gctx = egl_g3d_context(ctx);
   struct egl_g3d_surface *gdraw = egl_g3d_surface(draw);
   struct egl_g3d_surface *gread = egl_g3d_surface(read);
   struct egl_g3d_context *old_gctx;
   EGLBoolean ok = EGL_TRUE;

   /* bind the new context and return the "orphaned" one */
   if (!_eglBindContext(&ctx, &draw, &read))
      return EGL_FALSE;
   old_gctx = egl_g3d_context(ctx);

   if (old_gctx) {
      /* flush old context */
      old_gctx->stctxi->flush(old_gctx->stctxi,
            PIPE_FLUSH_RENDER_CACHE | PIPE_FLUSH_FRAME, NULL);
   }

   if (gctx) {
      ok = gctx->stapi->make_current(gctx->stapi, gctx->stctxi,
            (gdraw) ? gdraw->stfbi : NULL, (gread) ? gread->stfbi : NULL);
      if (ok) {
         gctx->stctxi->notify_invalid_framebuffer(gctx->stctxi, gdraw->stfbi);
         if (gread != gdraw) {
            gctx->stctxi->notify_invalid_framebuffer(gctx->stctxi,
                  gread->stfbi);
         }

         if (gdraw->base.Type == EGL_WINDOW_BIT) {
            gctx->base.WindowRenderBuffer =
               (gdraw->stvis.render_buffer == ST_ATTACHMENT_FRONT_LEFT) ?
               EGL_SINGLE_BUFFER : EGL_BACK_BUFFER;
         }
      }
   }
   else if (old_gctx) {
      ok = old_gctx->stapi->make_current(old_gctx->stapi, NULL, NULL, NULL);
      old_gctx->base.WindowRenderBuffer = EGL_NONE;
   }

   if (ctx && !_eglIsContextLinked(ctx))
      destroy_context(dpy, ctx);
   if (draw && !_eglIsSurfaceLinked(draw))
      destroy_surface(dpy, draw);
   if (read && read != draw && !_eglIsSurfaceLinked(read))
      destroy_surface(dpy, read);

   return ok;
}

static EGLBoolean
egl_g3d_swap_buffers(_EGLDriver *drv, _EGLDisplay *dpy, _EGLSurface *surf)
{
   struct egl_g3d_surface *gsurf = egl_g3d_surface(surf);
   _EGLContext *ctx = _eglGetCurrentContext();
   struct egl_g3d_context *gctx = NULL;

   /* no-op for pixmap or pbuffer surface */
   if (gsurf->base.Type == EGL_PIXMAP_BIT ||
       gsurf->base.Type == EGL_PBUFFER_BIT)
      return EGL_TRUE;

   /* or when the surface is single-buffered */
   if (gsurf->stvis.render_buffer == ST_ATTACHMENT_FRONT_LEFT)
      return EGL_TRUE;

   if (ctx && ctx->DrawSurface == surf)
      gctx = egl_g3d_context(ctx);

   /* flush if the surface is current */
   if (gctx) {
      gctx->stctxi->flush(gctx->stctxi,
            PIPE_FLUSH_RENDER_CACHE | PIPE_FLUSH_FRAME, NULL);
   }

   return gsurf->native->swap_buffers(gsurf->native);
}

/**
 * Get the pipe surface of the given attachment of the native surface.
 */
static struct pipe_surface *
get_pipe_surface(struct native_display *ndpy, struct native_surface *nsurf,
                 enum native_attachment natt,
		 unsigned bind)
{
   struct pipe_resource *textures[NUM_NATIVE_ATTACHMENTS];
   struct pipe_surface *psurf;

   textures[natt] = NULL;
   nsurf->validate(nsurf, 1 << natt, NULL, textures, NULL, NULL);
   if (!textures[natt])
      return NULL;

   psurf = ndpy->screen->get_tex_surface(ndpy->screen, textures[natt],
         0, 0, 0, bind);
   pipe_resource_reference(&textures[natt], NULL);

   return psurf;
}

static EGLBoolean
egl_g3d_copy_buffers(_EGLDriver *drv, _EGLDisplay *dpy, _EGLSurface *surf,
                     EGLNativePixmapType target)
{
   struct egl_g3d_display *gdpy = egl_g3d_display(dpy);
   struct egl_g3d_surface *gsurf = egl_g3d_surface(surf);
   _EGLContext *ctx = _eglGetCurrentContext();
   struct egl_g3d_config *gconf;
   struct native_surface *nsurf;
   struct pipe_screen *screen = gdpy->native->screen;
   struct pipe_surface *psurf;

   if (!gsurf->render_texture)
      return EGL_TRUE;

   gconf = egl_g3d_config(egl_g3d_find_pixmap_config(dpy, target));
   if (!gconf)
      return _eglError(EGL_BAD_NATIVE_PIXMAP, "eglCopyBuffers");

   nsurf = gdpy->native->create_pixmap_surface(gdpy->native,
         target, gconf->native);
   if (!nsurf)
      return _eglError(EGL_BAD_NATIVE_PIXMAP, "eglCopyBuffers");

   /* flush if the surface is current */
   if (ctx && ctx->DrawSurface == &gsurf->base) {
      struct egl_g3d_context *gctx = egl_g3d_context(ctx);
      gctx->stctxi->flush(gctx->stctxi,
            PIPE_FLUSH_RENDER_CACHE | PIPE_FLUSH_FRAME, NULL);
   }

   /* create a pipe context to copy surfaces */
   if (!gdpy->pipe) {
      gdpy->pipe =
         gdpy->native->screen->context_create(gdpy->native->screen, NULL);
      if (!gdpy->pipe)
         return EGL_FALSE;
   }

   psurf = get_pipe_surface(gdpy->native, nsurf, NATIVE_ATTACHMENT_FRONT_LEFT,
			    PIPE_BIND_BLIT_DESTINATION);
   if (psurf) {
      struct pipe_surface *psrc;

      psrc = screen->get_tex_surface(screen, gsurf->render_texture,
            0, 0, 0, PIPE_BIND_BLIT_SOURCE);
      if (psrc) {
         gdpy->pipe->surface_copy(gdpy->pipe, psurf, 0, 0,
               psrc, 0, 0, psurf->width, psurf->height);
         pipe_surface_reference(&psrc, NULL);

         nsurf->flush_frontbuffer(nsurf);
      }

      pipe_surface_reference(&psurf, NULL);
   }

   nsurf->destroy(nsurf);

   return EGL_TRUE;
}

static EGLBoolean
egl_g3d_wait_client(_EGLDriver *drv, _EGLDisplay *dpy, _EGLContext *ctx)
{
   struct egl_g3d_display *gdpy = egl_g3d_display(dpy);
   struct egl_g3d_context *gctx = egl_g3d_context(ctx);
   struct pipe_screen *screen = gdpy->native->screen;
   struct pipe_fence_handle *fence = NULL;

   gctx->stctxi->flush(gctx->stctxi,
         PIPE_FLUSH_RENDER_CACHE | PIPE_FLUSH_FRAME, &fence);
   screen->fence_finish(screen, fence, 0);
   screen->fence_reference(screen, &fence, NULL);

   return EGL_TRUE;
}

static EGLBoolean
egl_g3d_wait_native(_EGLDriver *drv, _EGLDisplay *dpy, EGLint engine)
{
   _EGLContext *ctx = _eglGetCurrentContext();

   if (engine != EGL_CORE_NATIVE_ENGINE)
      return _eglError(EGL_BAD_PARAMETER, "eglWaitNative");

   if (ctx && ctx->DrawSurface) {
      struct egl_g3d_surface *gsurf = egl_g3d_surface(ctx->DrawSurface);

      if (gsurf->native)
         gsurf->native->wait(gsurf->native);
   }

   return EGL_TRUE;
}

static EGLBoolean
egl_g3d_bind_tex_image(_EGLDriver *drv, _EGLDisplay *dpy,
                       _EGLSurface *surf, EGLint buffer)
{
   struct egl_g3d_surface *gsurf = egl_g3d_surface(surf);
   _EGLContext *es1 = _eglGetAPIContext(EGL_OPENGL_ES_API);
   struct egl_g3d_context *gctx;
   enum pipe_format internal_format;
   enum st_texture_type target;

   if (!gsurf || gsurf->base.Type != EGL_PBUFFER_BIT)
      return _eglError(EGL_BAD_SURFACE, "eglBindTexImage");
   if (buffer != EGL_BACK_BUFFER)
      return _eglError(EGL_BAD_PARAMETER, "eglBindTexImage");
   if (gsurf->base.BoundToTexture)
      return _eglError(EGL_BAD_ACCESS, "eglBindTexImage");

   switch (gsurf->base.TextureFormat) {
   case EGL_TEXTURE_RGB:
      internal_format = PIPE_FORMAT_R8G8B8_UNORM;
      break;
   case EGL_TEXTURE_RGBA:
      internal_format = PIPE_FORMAT_B8G8R8A8_UNORM;
      break;
   default:
      return _eglError(EGL_BAD_MATCH, "eglBindTexImage");
   }

   switch (gsurf->base.TextureTarget) {
   case EGL_TEXTURE_2D:
      target = ST_TEXTURE_2D;
      break;
   default:
      return _eglError(EGL_BAD_MATCH, "eglBindTexImage");
   }

   if (!es1)
      return EGL_TRUE;
   if (!gsurf->render_texture)
      return EGL_FALSE;

   /* flush properly if the surface is bound */
   if (gsurf->base.CurrentContext) {
      gctx = egl_g3d_context(gsurf->base.CurrentContext);
      gctx->stctxi->flush(gctx->stctxi,
            PIPE_FLUSH_RENDER_CACHE | PIPE_FLUSH_FRAME, NULL);
   }

   gctx = egl_g3d_context(es1);
   if (gctx->stctxi->teximage) {
      if (!gctx->stctxi->teximage(gctx->stctxi, target,
               gsurf->base.MipmapLevel, internal_format,
               gsurf->render_texture, gsurf->base.MipmapTexture))
         return EGL_FALSE;
      gsurf->base.BoundToTexture = EGL_TRUE;
   }

   return EGL_TRUE;
}

static EGLBoolean
egl_g3d_release_tex_image(_EGLDriver *drv, _EGLDisplay *dpy,
                          _EGLSurface *surf, EGLint buffer)
{
   struct egl_g3d_surface *gsurf = egl_g3d_surface(surf);

   if (!gsurf || gsurf->base.Type != EGL_PBUFFER_BIT ||
       !gsurf->base.BoundToTexture)
      return _eglError(EGL_BAD_SURFACE, "eglReleaseTexImage");
   if (buffer != EGL_BACK_BUFFER)
      return _eglError(EGL_BAD_PARAMETER, "eglReleaseTexImage");

   if (gsurf->render_texture) {
      _EGLContext *ctx = _eglGetAPIContext(EGL_OPENGL_ES_API);
      struct egl_g3d_context *gctx = egl_g3d_context(ctx);

      /* what if the context the surface binds to is no longer current? */
      if (gctx) {
         gctx->stctxi->teximage(gctx->stctxi, ST_TEXTURE_2D,
               gsurf->base.MipmapLevel, PIPE_FORMAT_NONE, NULL, FALSE);
      }
   }

   gsurf->base.BoundToTexture = EGL_FALSE;

   return EGL_TRUE;
}

#ifdef EGL_MESA_screen_surface

static _EGLSurface *
egl_g3d_create_screen_surface(_EGLDriver *drv, _EGLDisplay *dpy,
                              _EGLConfig *conf, const EGLint *attribs)
{
   struct egl_g3d_create_surface_arg arg;

   memset(&arg, 0, sizeof(arg));
   arg.type = EGL_SCREEN_BIT_MESA;

   return egl_g3d_create_surface(drv, dpy, conf, &arg, attribs);
}

static EGLBoolean
egl_g3d_show_screen_surface(_EGLDriver *drv, _EGLDisplay *dpy,
                            _EGLScreen *scr, _EGLSurface *surf,
                            _EGLMode *mode)
{
   struct egl_g3d_display *gdpy = egl_g3d_display(dpy);
   struct egl_g3d_screen *gscr = egl_g3d_screen(scr);
   struct egl_g3d_surface *gsurf = egl_g3d_surface(surf);
   struct native_surface *nsurf;
   const struct native_mode *nmode;
   EGLBoolean changed;

   if (gsurf) {
      EGLint idx;

      if (!mode)
         return _eglError(EGL_BAD_MATCH, "eglShowSurfaceMESA");
      if (gsurf->base.Type != EGL_SCREEN_BIT_MESA)
         return _eglError(EGL_BAD_SURFACE, "eglShowScreenSurfaceMESA");
      if (gsurf->base.Width < mode->Width || gsurf->base.Height < mode->Height)
         return _eglError(EGL_BAD_MATCH,
               "eglShowSurfaceMESA(surface smaller than mode size)");

      /* find the index of the mode */
      for (idx = 0; idx < gscr->base.NumModes; idx++)
         if (mode == &gscr->base.Modes[idx])
            break;
      if (idx >= gscr->base.NumModes) {
         return _eglError(EGL_BAD_MODE_MESA,
               "eglShowSurfaceMESA(unknown mode)");
      }

      nsurf = gsurf->native;
      nmode = gscr->native_modes[idx];
   }
   else {
      if (mode)
         return _eglError(EGL_BAD_MATCH, "eglShowSurfaceMESA");

      /* disable the screen */
      nsurf = NULL;
      nmode = NULL;
   }

   /* TODO surface panning by CRTC choosing */
   changed = gdpy->native->modeset->program(gdpy->native, 0, nsurf,
         gscr->base.OriginX, gscr->base.OriginY, &gscr->native, 1, nmode);
   if (changed) {
      gscr->base.CurrentSurface = &gsurf->base;
      gscr->base.CurrentMode = mode;
   }

   return changed;
}

#endif /* EGL_MESA_screen_surface */

/**
 * Find a config that supports the pixmap.
 */
_EGLConfig *
egl_g3d_find_pixmap_config(_EGLDisplay *dpy, EGLNativePixmapType pix)
{
   struct egl_g3d_display *gdpy = egl_g3d_display(dpy);
   struct egl_g3d_config *gconf;
   EGLint i;

   for (i = 0; i < dpy->NumConfigs; i++) {
      gconf = egl_g3d_config(dpy->Configs[i]);
      if (gdpy->native->is_pixmap_supported(gdpy->native, pix, gconf->native))
         break;
   }

   return (i < dpy->NumConfigs) ? &gconf->base : NULL;
}

void
egl_g3d_init_driver_api(_EGLDriver *drv)
{
   _eglInitDriverFallbacks(drv);

   drv->API.CreateContext = egl_g3d_create_context;
   drv->API.DestroyContext = egl_g3d_destroy_context;
   drv->API.CreateWindowSurface = egl_g3d_create_window_surface;
   drv->API.CreatePixmapSurface = egl_g3d_create_pixmap_surface;
   drv->API.CreatePbufferSurface = egl_g3d_create_pbuffer_surface;
   drv->API.DestroySurface = egl_g3d_destroy_surface;
   drv->API.MakeCurrent = egl_g3d_make_current;
   drv->API.SwapBuffers = egl_g3d_swap_buffers;
   drv->API.CopyBuffers = egl_g3d_copy_buffers;
   drv->API.WaitClient = egl_g3d_wait_client;
   drv->API.WaitNative = egl_g3d_wait_native;

   drv->API.BindTexImage = egl_g3d_bind_tex_image;
   drv->API.ReleaseTexImage = egl_g3d_release_tex_image;

   drv->API.CreateImageKHR = egl_g3d_create_image;
   drv->API.DestroyImageKHR = egl_g3d_destroy_image;

#ifdef EGL_MESA_screen_surface
   drv->API.CreateScreenSurfaceMESA = egl_g3d_create_screen_surface;
   drv->API.ShowScreenSurfaceMESA = egl_g3d_show_screen_surface;
#endif
}