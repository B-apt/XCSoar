MapLibre basemap proof of concept
==================================

This note describes the Linux/OpenGL proof of concept that renders the
visual basemap with `MapLibre Native
<https://github.com/maplibre/maplibre-native>`_ instead of XCSoar's own
``RasterTerrain``/topography renderers, while leaving every calculation
and every other overlay renderer untouched.

Scope
-----

- OpenGL builds only (``ENABLE_MAPLIBRE_BASEMAP``), Linux/PC target only
  for now.
- Offline only: the MapLibre style, vector tiles, raster hillshade,
  sprites and glyphs all come from an optional ``maplibre/`` bundle
  inside the ``.xcm`` map file (produced by mapgen's ``--maplibre``
  option); no network access at runtime.
- Visual only: replaces the *pixels* drawn for terrain shading and
  topology, nothing else.

Code entry points:

- :file:`src/MapWindow/MapLibreBaseMap.hpp` / :file:`.cpp`
- :file:`src/MapWindow/MapWindowRender.cpp`
  (:cpp:`MapWindow::RenderMapLibreBase`, called from
  :cpp:`MapWindow::Render`)
- :file:`src/Startup.cpp` (:cpp:`MapWindow::SetMapLibreBundle` wiring)
- :file:`build/maplibre.mk`

Native ``.xcm`` rendering vs. this MapLibre path
-------------------------------------------------

- **Terrain shading**: normally :cpp:`BackgroundRenderer` reads
  ``terrain.jp2`` directly. With MapLibre active, replaced by MapLibre's
  hillshade layer.
- **Topology** (roads/water/etc.): normally :cpp:`CachedTopographyRenderer`
  reads the shapefiles directly. With MapLibre active, replaced by
  MapLibre's vector-tile layers.
- **RasterTerrain data**: always loaded, always serviced
  (:cpp:`MapWindow::UpdateTerrain`). **Unchanged** either way -- still the
  only source of truth for elevation.
- **GlideComputer / airspace ground levels / route+reach /
  final-glide-through-terrain**: all read :cpp:`RasterTerrain` directly.
  **Unchanged** either way.
- **Airspace/waypoint/task/traffic/aircraft renderers**: native
  :cpp:`Canvas` drawing. **Unchanged** either way, drawn on top exactly
  as before.
- **Weather overlay** (RASP, :cpp:`MapOverlay`/``SetOverlay()``): drawn
  after terrain+topography. **Unchanged position** either way -- still
  drawn right after the (now possibly MapLibre) background.

In other words: :cpp:`MapWindow::Render()` still computes
``render_projection`` the same way and still calls every renderer in the
same order; only the two calls that used to paint the background pixels
(:cpp:`RenderTerrain` / :cpp:`RenderTopography`) are skipped for that
frame when :cpp:`MapWindow::RenderMapLibreBase` succeeds.

Data flow: headless render + CPU readback
------------------------------------------

MapLibre Native needs its own GL context to render (vertex/fragment
shaders, its own FBOs, its own texture atlas for vector tiles). Rather
than trying to share XCSoar's live GLX context with it, this PoC keeps
the two fully decoupled:

1. ``mbgl::HeadlessFrontend`` renders into its **own offscreen EGL
   context** (confirmed via ``ldd`` on the MapLibre Native build: it
   links ``libEGL``/``libGLESv2``, never ``libGLX``/``libX11``).
2. :cpp:`MapLibreBaseMap::Render()` reads back the resulting
   ``PremultipliedImage`` (tightly packed RGBA8) and wraps it in an
   XCSoar :cpp:`Bitmap` via :cpp:`UncompressedImage` -- the exact same
   path a decoded PNG/JPEG goes through elsewhere in XCSoar.
3. :cpp:`Canvas::Stretch()` draws that :cpp:`Bitmap` as a full-viewport
   quad, using XCSoar's normal texture shader.

Because step 1 happens on the same UI thread as step 3 (there is no
``DrawThread`` under ``ENABLE_OPENGL`` --
:file:`src/ui/window/DoubleBufferWindow.hpp`), and because EGL and GLX
share the same per-thread "current context" slot on Mesa/libglvnd,
``frontend.render()`` making its own EGL context current leaves
XCSoar's GLX context no longer current when it returns.
:cpp:`MapLibreBaseMap::Render()` therefore saves
``glXGetCurrentDisplay/Drawable/Context()`` before calling into
MapLibre and restores them with ``glXMakeContextCurrent()`` immediately
after, before any further GL call is made. This is gated on ``USE_GLX``
and is the one piece of this integration that is genuinely
platform/backend-specific.

Camera mapping
---------------

:cpp:`MapLibreBaseMap::SetCamera()` converts the current
:cpp:`WindowProjection` to an ``mbgl::CameraOptions`` every frame (cheap;
only re-renders if something actually changed):

- center: :cpp:`WindowProjection::GetGeoScreenCenter()`
- zoom: ``log2(scale_px_per_m * 156543.03392 * cos(latitude))`` -- the
  standard Web Mercator resolution/zoom relationship (156543.03392 =
  Earth's equatorial circumference in meters / 256)
- bearing: ``-GetScreenAngle().Degrees()``
- pitch: fixed at 0 (flat 2D map, matching XCSoar's own projection)

Offline bundle loading
------------------------

``.xcm`` files are read in place via zzip (:file:`src/io/MapFile.hpp`);
they are never extracted to disk for native rendering. MapLibre
Native's built-in ``MBTilesFileSource``
(``src/mbgl/storage/mbtiles_file_source.cpp`` in the MapLibre Native
tree) only accepts **absolute filesystem paths**, and mapgen's generated
``style.json`` uses relative ``mbtiles://./basemap.mbtiles`` URLs. So
:cpp:`MapLibreBaseMap::LoadBundle()`:

1. Extracts every ``maplibre/...`` entry (except the flat
   ``maplibre/tiles/`` preview tree, which is browser/debug-only, see
   mapgen's ``GENERATE_TEST_BUNDLE.md``) to
   :cpp:`GetCachePath()`\ ``/maplibre/``, skipped on subsequent runs if
   ``style.json`` is already there.
2. Rewrites ``mbtiles://./`` to an absolute ``mbtiles://<cache dir>/``
   and writes the result to ``style.local.json`` in the same directory.
3. Loads that via ``mbgl::Style::loadURL("file://...")`` (sprite/glyph
   relative paths then resolve normally against that ``file://`` base).

Any failure at any of these steps (no map file configured, no
``maplibre/`` folder in this particular ``.xcm``, I/O error, MapLibre
exception) leaves :cpp:`MapLibreBaseMap::IsUsable()` false, which is the
*entire* fallback mechanism: :cpp:`MapWindow::RenderMapLibreBase()`
simply returns ``false`` and the native
:cpp:`RenderTerrain`/:cpp:`RenderTopography` path runs exactly as it did
before this PoC existed.

Building
--------

Not vendored, not built via ``thirdparty.py``. Point the build at an
existing MapLibre Native checkout + CMake build via
:file:`build/local-config.mk`::

   MAPLIBRE_NATIVE_SRC = $(HOME)/00_DEV/projets/0_perso/maplibre-native
   MAPLIBRE_NATIVE_BUILD = $(MAPLIBRE_NATIVE_SRC)/build-linux-opengl

``MAPLIBRE_BASEMAP`` defaults to on whenever ``OPENGL=y`` (off on
Android). See :file:`build/maplibre.mk` for the exact static archive
list and system libraries linked in.

Known limitations
-------------------

- **Bearing sign** between :cpp:`Projection::GetScreenAngle()` and
  ``mbgl``'s bearing convention has not been verified empirically yet.
  First manual check: force north-up and confirm north renders up.
- ``frontend.render()`` is synchronous and runs on the UI thread; a full
  MapLibre re-render on every pan/zoom/rotate tick could cause a visible
  frame hitch. Mitigated (not eliminated) by only re-rendering when the
  camera actually changed.
- :cpp:`MapWindow::RenderTopographyLabels` (drawn later, over airspace)
  is untouched and keeps drawing XCSoar's own topography labels
  regardless of whether MapLibre is active; MapLibre's own style also
  renders labels via its glyph layers, so duplicate labels are possible.
- First load extracts the bundle's MBTiles files (tens of MB) into the
  cache directory; one-time cost per map file, skipped on subsequent
  loads.

Android follow-up work
-------------------------

Not implemented in this PoC; noted for a future pass:

- MapLibre Native's Android artifact is a separate build (AAR via
  Gradle), not the CMake preset used here -- ``build/maplibre.mk``'s
  static-archive approach does not carry over as-is.
- XCSoar's Android target uses a JNI/Java ``GLSurfaceView`` GL context
  rather than GLX. The headless-render-and-readback design should still
  decouple cleanly (MapLibre's own EGL context vs. Android's EGL
  context), but the GLX-specific context-restore workaround in
  :cpp:`MapLibreBaseMap::Render()` needs an EGL equivalent
  (``eglGetCurrentContext``/``eglMakeCurrent``) verified against
  Android's GL thread model.
- The bundle-extraction step (:cpp:`ExtractBundle`) uses desktop
  filesystem APIs already available on Android via XCSoar's existing
  :file:`system/FileUtil.hpp`; should work unchanged, but not tested.
