// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "MapLibreGLXContext.hpp"

#ifdef USE_GLX

#include <GL/glx.h>

ScopeRestoreGLXContext::ScopeRestoreGLXContext() noexcept
  :display(glXGetCurrentDisplay()),
   drawable(glXGetCurrentDrawable()),
   context(glXGetCurrentContext())
{
  /* EGL refuses to activate a context on a thread that still has a
     GLX context current (eglMakeCurrent() fails outright rather than
     silently taking over) -- release it explicitly so MapLibre's own
     eglMakeCurrent() call can succeed. */
  if (context != nullptr && display != nullptr)
    glXMakeContextCurrent(static_cast<Display *>(display), None, None, nullptr);
}

ScopeRestoreGLXContext::~ScopeRestoreGLXContext() noexcept
{
  if (context != nullptr)
    glXMakeContextCurrent(static_cast<Display *>(display),
                          static_cast<GLXDrawable>(drawable),
                          static_cast<GLXDrawable>(drawable),
                          static_cast<GLXContext>(context));
}

#endif
