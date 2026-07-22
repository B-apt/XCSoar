// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#ifdef USE_GLX

/**
 * Saves the thread's current GLX context on construction, releases it
 * (glXMakeContextCurrent() with no context), and re-asserts it again
 * on destruction.
 *
 * MapLibre's HeadlessFrontend makes its own offscreen EGL context
 * current on this thread in order to render.  On Mesa/libglvnd, EGL's
 * eglMakeCurrent() refuses to activate a context while a GLX context
 * is still current on the same thread -- it fails outright (throwing
 * inside MapLibre) rather than silently taking over -- so XCSoar's
 * GLX context must be explicitly released first, then restored once
 * MapLibre is done.
 *
 * Deliberately kept in its own translation unit with no MapLibre (or
 * even XCSoar GL) includes: <GL/glx.h> drags in <X11/Xlib.h>, whose
 * macros (e.g. "None") collide with identically-named enum members in
 * MapLibre Native's own headers and silently break them if both are
 * included from the same file.
 */
class ScopeRestoreGLXContext final {
  void *display;
  unsigned long drawable;
  void *context;

public:
  [[nodiscard]]
  ScopeRestoreGLXContext() noexcept;

  ~ScopeRestoreGLXContext() noexcept;

  ScopeRestoreGLXContext(const ScopeRestoreGLXContext &) = delete;
  ScopeRestoreGLXContext &operator=(const ScopeRestoreGLXContext &) = delete;
};

#endif
