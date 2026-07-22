# MapLibre Native offscreen basemap PoC (Linux/OpenGL only).
#
# MapLibre Native is not vendored and not built via thirdparty.py; it is
# expected to already be checked out and built (CMake) on the host, with
# MAPLIBRE_NATIVE_SRC/MAPLIBRE_NATIVE_BUILD pointing at it.  Set these in
# build/local-config.mk, e.g.:
#
#   MAPLIBRE_NATIVE_SRC = $(HOME)/00_DEV/projets/0_perso/maplibre-native
#   MAPLIBRE_NATIVE_BUILD = $(MAPLIBRE_NATIVE_SRC)/build-linux-opengl

MAPLIBRE_BASEMAP ?= $(OPENGL)

ifeq ($(TARGET_IS_ANDROID),y)
# Phase 2, not this PoC: MapLibre Native's Android artifact is a
# separate AAR build, not this CMake preset.
MAPLIBRE_BASEMAP = n
endif

ifeq ($(MAPLIBRE_BASEMAP),y)

ifeq ($(MAPLIBRE_NATIVE_SRC),)
$(error MAPLIBRE_BASEMAP=y requires MAPLIBRE_NATIVE_SRC (and MAPLIBRE_NATIVE_BUILD) to be set, e.g. in build/local-config.mk)
endif

ifeq ($(MAPLIBRE_NATIVE_BUILD),)
$(error MAPLIBRE_BASEMAP=y requires MAPLIBRE_NATIVE_BUILD to be set, e.g. in build/local-config.mk)
endif

# Defines and vendored header-only dependency include paths, copied
# verbatim from build-linux-opengl/compile_commands.json (the flags
# MapLibre Native's own CMake build uses to compile bin/harness.cpp
# against these same headers).
MAPLIBRE_CPPFLAGS = \
	-DENABLE_MAPLIBRE_BASEMAP \
	-DEGL_NO_X11 -DMESA_EGL_NO_X11_HEADERS \
	-DMLN_RENDER_BACKEND_OPENGL=1 -DMLN_USE_UNORDERED_DENSE=1 \
	-DRAPIDJSON_HAS_STDSTRING=1 -DWL_EGL_PLATFORM \
	-isystem $(MAPLIBRE_NATIVE_SRC)/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/platform/default/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/args \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/freetype/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/harfbuzz/src \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/maplibre-native-base/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/maplibre-native-base/deps/cheap-ruler-cpp/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/maplibre-native-base/deps/geojson-vt-cpp/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/maplibre-native-base/deps/geojson.hpp/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/maplibre-native-base/deps/geometry.hpp/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/maplibre-native-base/deps/jni.hpp/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/maplibre-native-base/deps/pixelmatch-cpp/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/maplibre-native-base/deps/shelf-pack-cpp/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/maplibre-native-base/deps/variant/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/expected-lite/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/rapidjson/include \
	-isystem $(MAPLIBRE_NATIVE_SRC)/vendor/unordered_dense/include

# Static archives, exactly as linked by MapLibre Native's own CMake/ninja
# build (verified against build-linux-opengl/build.ninja's mbgl-harness
# link line) plus the system libraries it links dynamically.
MAPLIBRE_STATIC_LIBS = \
	$(MAPLIBRE_NATIVE_BUILD)/libmbgl-core.a \
	$(MAPLIBRE_NATIVE_BUILD)/libmbgl-vendor-parsedate.a \
	$(MAPLIBRE_NATIVE_BUILD)/vendor/maplibre-tile-spec/cpp/libmlt-cpp.a \
	$(MAPLIBRE_NATIVE_BUILD)/vendor/maplibre-tile-spec/cpp/libfastpfor-lib.a \
	$(MAPLIBRE_NATIVE_BUILD)/libmbgl-vendor-csscolorparser.a \
	$(MAPLIBRE_NATIVE_BUILD)/libmbgl-harfbuzz.a \
	$(MAPLIBRE_NATIVE_BUILD)/libmbgl-freetype.a \
	$(MAPLIBRE_NATIVE_BUILD)/libmbgl-vendor-nunicode.a \
	$(MAPLIBRE_NATIVE_BUILD)/libmbgl-vendor-sqlite.a

MAPLIBRE_LDLIBS = \
	$(MAPLIBRE_STATIC_LIBS) \
	-lEGL -lGLESv2 -lcurl -ljpeg -luv -lpthread -ldl -lrt -lwebp \
	-licuuc -licudata -licui18n -lpng -lz

endif
