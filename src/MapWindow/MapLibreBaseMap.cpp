// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "MapLibreBaseMap.hpp"

#ifdef ENABLE_MAPLIBRE_BASEMAP

#include "Projection/WindowProjection.hpp"
#include "Geo/GeoPoint.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Bitmap.hpp"
#include "ui/canvas/custom/UncompressedImage.hpp"
#include "io/MapFile.hpp"
#include "io/ZipArchive.hpp"
#include "io/ZipReader.hpp"
#include "io/FileReader.hxx"
#include "io/FileOutputStream.hxx"
#include "system/FileUtil.hpp"
#include "system/Path.hpp"
#include "LocalPath.hpp"
#include "LogFile.hpp"
#include "MapLibreGLXContext.hpp"

#include <mbgl/map/map.hpp>
#include <mbgl/map/map_options.hpp>
#include <mbgl/map/camera.hpp>
#include <mbgl/util/image.hpp>
#include <mbgl/util/run_loop.hpp>
#include <mbgl/util/geo.hpp>
#include <mbgl/gfx/headless_frontend.hpp>
#include <mbgl/style/style.hpp>
#include <mbgl/storage/resource_options.hpp>

#include <cmath>
#include <span>
#include <string>

namespace {

/**
 * Read a whole file into a std::string.  Throws on error.
 */
std::string
ReadWholeFile(Path path)
{
  FileReader reader(path);
  const std::size_t size = reader.GetSize();
  std::string result(size, '\0');
  reader.ReadFull(std::as_writable_bytes(std::span{result.data(), result.size()}));
  return result;
}

/**
 * Replace every occurrence of @p needle in @p json with @p
 * replacement.
 */
void
ReplaceAll(std::string &json, const std::string &needle,
          const std::string &replacement)
{
  std::string::size_type pos = 0;
  while ((pos = json.find(needle, pos)) != std::string::npos) {
    json.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

/**
 * Rewrite mapgen's relative bundle URLs to schemes MapLibre Native
 * can actually resolve without a network round trip:
 *
 * - "mbtiles://./foo.mbtiles" -> absolute "mbtiles://<cache_dir>/foo.mbtiles"
 *   (MBTilesFileSource, src/mbgl/storage/mbtiles_file_source.cpp,
 *   refuses anything but an absolute path)
 * - "./sprites/..." / "./glyphs/..." -> "asset://sprites/..." /
 *   "asset://glyphs/..." (AssetFileSource,
 *   src/mbgl/storage/asset_file_source.cpp, resolves "asset://X"
 *   against ResourceOptions::assetPath(), which LoadBundle() sets to
 *   cache_dir)
 *
 * A plain "./..." string with no recognised scheme matches none of
 * MapLibre's local file sources and falls through to the network
 * file source instead -- which is what silently sent glyph/sprite
 * requests out to the network before this rewrite existed.
 */
std::string
RewriteStyle(std::string json, Path cache_dir)
{
  ReplaceAll(json, "mbtiles://./", std::string("mbtiles://") + cache_dir.c_str() + "/");
  ReplaceAll(json, "\"./sprites/", "\"asset://sprites/");
  ReplaceAll(json, "\"./glyphs/", "\"asset://glyphs/");
  return json;
}

/**
 * Copy every "maplibre/..." entry (except the flat "maplibre/tiles/"
 * preview tree, which mapgen only produces for browser/debug preview
 * and which MBTilesFileSource never reads) out of the currently
 * configured map file into @p cache_dir, preserving the relative
 * directory structure.
 *
 * @return true if at least one file was extracted
 */
bool
ExtractBundle(Path cache_dir)
{
  auto archive = OpenMapFile();
  if (!archive)
    return false;

  static constexpr char prefix[] = "maplibre/";
  static constexpr char tiles_prefix[] = "maplibre/tiles/";
  constexpr std::size_t prefix_len = sizeof(prefix) - 1;
  constexpr std::size_t tiles_prefix_len = sizeof(tiles_prefix) - 1;

  bool found_any = false;

  for (std::string name = archive->NextName(); !name.empty();
       name = archive->NextName()) {
    if (name.compare(0, prefix_len, prefix) != 0)
      continue;

    if (name.compare(0, tiles_prefix_len, tiles_prefix) == 0)
      continue;

    const std::string relative = name.substr(prefix_len);
    if (relative.empty())
      continue;

    const auto dest = AllocatedPath::Build(cache_dir, relative.c_str());
    Directory::CreateRecursive(dest.GetParent());

    ZipReader reader(archive->get(), name.c_str());
    FileOutputStream output(dest);

    std::byte buffer[65536];
    for (;;) {
      const std::size_t n = reader.Read(buffer);
      if (n == 0)
        break;
      output.Write(std::span{buffer, n});
    }

    output.Commit();
    found_any = true;
  }

  return found_any;
}

} // namespace

struct MapLibreBaseMap::Impl {
  /* declaration order matters: members are destroyed in reverse
     order, and `map` (which references `frontend`, which needs
     `run_loop` to exist) must be torn down before either of them */
  mbgl::util::RunLoop run_loop;
  mbgl::HeadlessFrontend frontend;
  std::unique_ptr<mbgl::Map> map;

  Bitmap bitmap;

  bool style_loaded = false;
  bool dirty = true;

  GeoPoint last_center = GeoPoint::Invalid();
  double last_zoom = -1;
  double last_bearing_deg = 0;
  PixelSize last_size{0, 0};

  explicit Impl(PixelSize size)
    :frontend(mbgl::Size{(uint32_t)size.width, (uint32_t)size.height}, 1.0f) {}
};

MapLibreBaseMap::MapLibreBaseMap(PixelSize size) noexcept
{
  try {
    impl = std::make_unique<Impl>(size);
  } catch (...) {
    LogError(std::current_exception(), "Failed to initialise MapLibre");
    impl.reset();
  }
}

MapLibreBaseMap::~MapLibreBaseMap() noexcept
{
  /* tearing down mbgl::Map/HeadlessFrontend releases GL resources,
     which (like Render()) needs to activate MapLibre's own EGL
     context -- guard it the same way, and never let an exception
     escape a noexcept destructor */
  try {
#ifdef USE_GLX
    const ScopeRestoreGLXContext restore_glx;
#endif
    impl.reset();
  } catch (...) {
    LogError(std::current_exception(), "Failed to shut down MapLibre");
  }
}

bool
MapLibreBaseMap::LoadBundle() noexcept
try {
  if (!impl)
    return false;

  impl->style_loaded = false;

  const auto cache_dir = MakeCacheDirectory("maplibre");
  const auto style_path = AllocatedPath::Build(cache_dir, "style.json");

  if (!File::Exists(style_path) && !ExtractBundle(cache_dir)) {
    /* no "maplibre/" bundle in this map file -- not an error, this
       PoC feature is simply unavailable for this map */
    return false;
  }

  if (!File::Exists(style_path))
    return false;

  const std::string rewritten_json =
    RewriteStyle(ReadWholeFile(style_path), cache_dir);

  const auto rewritten_path = AllocatedPath::Build(cache_dir, "style.local.json");
  {
    FileOutputStream output(rewritten_path);
    output.Write(std::as_bytes(std::span{rewritten_json}));
    output.Commit();
  }

  if (!impl->map) {
    const auto cache_db_path = AllocatedPath::Build(cache_dir, "cache.sqlite");

    impl->map = std::make_unique<mbgl::Map>(
      impl->frontend, mbgl::MapObserver::nullObserver(),
      mbgl::MapOptions()
        .withMapMode(mbgl::MapMode::Static)
        .withSize(impl->frontend.getSize())
        .withPixelRatio(1.0f),
      mbgl::ResourceOptions()
        .withCachePath(cache_db_path.c_str())
        .withAssetPath(cache_dir.c_str()));
  }

  impl->map->getStyle().loadURL(std::string("file://") + rewritten_path.c_str());
  impl->style_loaded = true;
  impl->dirty = true;
  return true;
} catch (...) {
  LogError(std::current_exception(), "Failed to load MapLibre style");
  if (impl)
    impl->style_loaded = false;
  return false;
}

bool
MapLibreBaseMap::IsUsable() const noexcept
{
  return impl && impl->style_loaded;
}

void
MapLibreBaseMap::Resize(PixelSize size) noexcept
{
  if (!impl)
    return;

  const mbgl::Size mbgl_size{(uint32_t)size.width, (uint32_t)size.height};
  impl->frontend.setSize(mbgl_size);
  /* HeadlessFrontend::setSize() only resizes the render backend's
     framebuffer; mbgl::Map has its own, separate notion of viewport
     size (initially set from MapOptions::withSize() at construction)
     that drives its camera/tile-visibility transform, and it is never
     synced from the frontend automatically. */
  if (impl->map)
    impl->map->setSize(mbgl_size);
  impl->last_size = PixelSize{0, 0}; // force SetCamera() to re-jump next call
  impl->dirty = true;
}

void
MapLibreBaseMap::SetCamera(const WindowProjection &projection) noexcept
{
  if (!impl || !impl->map)
    return;

  const GeoPoint center = projection.GetGeoScreenCenter();
  const double lat_rad = center.latitude.Radians();
  const double scale = projection.GetScale(); // px/m
  if (scale <= 0)
    return;

  /* standard Web Mercator zoom<->resolution relationship: at zoom z,
     one screen pixel covers (78271.51696 * cos(lat) / 2^z) meters at
     the equator; 78271.51696 = Earth's equatorial circumference (m)
     / 512 -- MapLibre Native's tile size (mbgl::util::tileSize_D), not
     the classic 256px-tile constant; using 256 here under-zooms mbgl
     by exactly one level (2x too much ground shown), which desyncs the
     basemap from XCSoar's own overlay projection more and more with
     distance from the screen center -- most visible while panning far
     from the aircraft */
  const double zoom = std::log2(scale * 78271.51696 * std::cos(lat_rad));

  /* sign not yet verified empirically against mbgl's bearing
     convention -- if the rendered map turns out rotated the wrong
     way, flip this */
  const double bearing_deg = -projection.GetScreenAngle().Degrees();

  const PixelSize size = projection.GetScreenSize();
  if (size.width != impl->last_size.width ||
      size.height != impl->last_size.height) {
    const mbgl::Size mbgl_size{(uint32_t)size.width, (uint32_t)size.height};
    impl->frontend.setSize(mbgl_size);
    impl->map->setSize(mbgl_size); // see Resize() -- not synced automatically
    impl->last_size = size;
    impl->dirty = true;
  }

  if (center == impl->last_center && zoom == impl->last_zoom &&
      bearing_deg == impl->last_bearing_deg)
    return;

  impl->map->jumpTo(mbgl::CameraOptions()
                    .withCenter(mbgl::LatLng{center.latitude.Degrees(),
                                             center.longitude.Degrees()})
                    .withZoom(zoom)
                    .withBearing(bearing_deg)
                    .withPitch(0.0));

  impl->last_center = center;
  impl->last_zoom = zoom;
  impl->last_bearing_deg = bearing_deg;
  impl->dirty = true;
}

bool
MapLibreBaseMap::Render(Canvas &canvas, const PixelRect &rc) noexcept
{
  if (!impl || !impl->style_loaded || !impl->map)
    return false;

  if (impl->dirty) {
    try {
      mbgl::HeadlessFrontend::RenderResult result;

      {
#ifdef USE_GLX
        const ScopeRestoreGLXContext restore_glx;
#endif
        result = impl->frontend.render(*impl->map);
        /* restore_glx (if any) re-asserts XCSoar's own GLX context
           here, before any further GL call is made below */
      }

      auto &image = result.image;

      UncompressedImage uncompressed(UncompressedImage::Format::RGBA,
                                     image.stride(),
                                     image.size.width, image.size.height,
                                     std::move(image.data));
      if (!impl->bitmap.Load(std::move(uncompressed)))
        LogFormat("MapLibre: failed to import rendered frame as a texture");
    } catch (...) {
      LogError(std::current_exception(), "MapLibre render failed");
    }

    impl->dirty = false;
  }

  if (!impl->bitmap.IsDefined())
    return false;

  canvas.Stretch(rc.GetTopLeft(), rc.GetSize(), impl->bitmap);
  return true;
}

#endif
