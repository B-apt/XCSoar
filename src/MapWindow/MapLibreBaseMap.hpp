// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#ifdef ENABLE_MAPLIBRE_BASEMAP

#include "ui/dim/Rect.hpp"

#include <memory>

class Canvas;
class WindowProjection;

/**
 * Offline MapLibre Native basemap, used as an OpenGL-only PoC
 * replacement for the #RasterTerrain / topography *background*
 * rendering in #MapWindow.  Every other subsystem (RasterTerrain
 * itself, GlideComputer, airspace ground levels, route/reach,
 * final-glide-through-terrain, and all native overlay renderers)
 * keeps operating on the real data, unaffected by this class.
 *
 * MapLibre renders into its own offscreen EGL context
 * (mbgl::HeadlessFrontend); the resulting RGBA image is read back and
 * uploaded as a plain #Bitmap/GL texture into XCSoar's own (GLX)
 * context and drawn as a full-viewport quad.  The two GL contexts
 * never share state; see MapLibreBaseMap.cpp for why re-asserting
 * XCSoar's GLX context after each MapLibre render is still necessary.
 *
 * All `<mbgl/...>` includes are confined to MapLibreBaseMap.cpp; this
 * header (and everyone who only calls these methods) never needs the
 * MapLibre Native include paths.
 */
class MapLibreBaseMap final {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  /**
   * Constructs the (offscreen) MapLibre renderer at the given frame
   * size.  No style is loaded yet; the object is not usable until
   * LoadBundle() succeeds.  Never throws: internal failures (e.g. no
   * offscreen GL available) just leave the object permanently
   * unusable.
   */
  explicit MapLibreBaseMap(PixelSize size) noexcept;

  ~MapLibreBaseMap() noexcept;

  MapLibreBaseMap(const MapLibreBaseMap &) = delete;
  MapLibreBaseMap &operator=(const MapLibreBaseMap &) = delete;

  /**
   * Look for a "maplibre/" bundle (style.json + offline MBTiles +
   * sprites/glyphs) inside the currently configured map file
   * (OpenMapFile()), extract it to the cache directory if not already
   * extracted, rewrite its "mbtiles://" source URLs to absolute
   * paths, and load the resulting style.
   *
   * @return true if a usable style was loaded; false (leaving the
   * object unusable, so the caller keeps using native
   * terrain/topography rendering) on any missing map file, missing
   * "maplibre/" bundle, I/O error, or MapLibre exception.
   */
  bool LoadBundle() noexcept;

  /**
   * Is a style loaded and ready to render?  If false, callers must
   * fall back to native terrain/topography rendering.
   */
  [[gnu::pure]]
  bool IsUsable() const noexcept;

  void Resize(PixelSize size) noexcept;

  /**
   * Point the MapLibre camera at the given XCSoar projection.  Cheap
   * to call every frame: only marks the internal frame dirty if the
   * camera actually changed since the last call.
   */
  void SetCamera(const WindowProjection &projection) noexcept;

  /**
   * Re-render (only if dirty) and draw the current frame as a
   * full-viewport quad into @p canvas, in place of the native
   * terrain/topography background.
   *
   * @return true if a valid frame was actually drawn (whether
   * rendered just now or reused from an earlier successful render).
   * Callers must fall back to native terrain/topography rendering
   * when this returns false -- e.g. right after LoadBundle()
   * succeeded but before the first frame has finished rendering, or
   * if a style asset failed to load -- otherwise nothing paints the
   * background that frame and old contents show through.
   */
  bool Render(Canvas &canvas, const PixelRect &rc) noexcept;
};

#endif
