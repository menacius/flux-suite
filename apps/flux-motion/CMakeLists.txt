cmake_minimum_required(VERSION 3.16)
project(flux-motion VERSION 0.8.19)

# Resolve shared packages and host adapters from the monorepo. Cache entries
# keep embedded/non-standard checkouts possible.
if(NOT DEFINED FLUX_REPOSITORY_ROOT)
  get_filename_component(FLUX_REPOSITORY_ROOT
      "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
set(FLUX_SUITE_COMMON_DIR
    "${FLUX_REPOSITORY_ROOT}/packages/flux-common"
    CACHE PATH "Flux Suite shared sources and resources")
set(OBS_FXM_OBS_PLUGIN_ROOT
    "${FLUX_REPOSITORY_ROOT}/plugins/obs/flux-motion"
    CACHE PATH "Flux Motion OBS integration component root")
set(OBS_FXM_OBS_PLUGIN_DIR
    "${OBS_FXM_OBS_PLUGIN_ROOT}/src"
    CACHE PATH "Flux Motion OBS integration source root")

foreach(required_root IN ITEMS
    FLUX_SUITE_COMMON_DIR OBS_FXM_OBS_PLUGIN_ROOT OBS_FXM_OBS_PLUGIN_DIR)
  if(NOT IS_DIRECTORY "${${required_root}}")
    message(FATAL_ERROR "${required_root} does not exist: ${${required_root}}")
  endif()
endforeach()

# Public development version shown in the About dialog and diagnostic logs.
# Keep PROJECT_VERSION numeric for CMake/package compatibility and append the
# prerelease channel separately.
include("${FLUX_REPOSITORY_ROOT}/cmake/FluxVersion.cmake")
flux_read_version("${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt"
                  FLUX_MOTION_VERSION_LABEL)
flux_extract_semver("${FLUX_MOTION_VERSION_LABEL}" OBS_FXM_VERSION)
flux_read_version("${OBS_FXM_OBS_PLUGIN_ROOT}/VERSION.txt"
                  FLUX_MOTION_OBS_VERSION_LABEL)
flux_extract_semver("${FLUX_MOTION_OBS_VERSION_LABEL}"
                    OBS_FXM_PLUGIN_VERSION)

# Manually assigned development delivery identifier. Update this once for each
# package handed to the user. It is intentionally source-controlled and does
# not change when the same checkout is rebuilt.
set(OBS_FXM_DEVELOPMENT_VERSION "420")

# ── C++ standard ────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/FS)
endif()

# ── OBS plugin packaging layout ─────────────────────────────────────
# OBS' modern manual-install layout is:
#   <plugins-root>/<plugin-name>/bin/<arch>/<plugin binary>
#   <plugins-root>/<plugin-name>/data/locale/*.ini
# Keeping build artifacts in this shape makes the build tree directly copyable
# into the OBS plugins directory and matches the Windows ProgramData layout.
set(OBS_TITLES_PLUGIN_NAME "flux-motion")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(OBS_TITLES_ARCH_DIR "64bit")
  set(OBS_TITLES_VCPKG_DEFAULT_TRIPLET "x64-windows")
else()
  set(OBS_TITLES_ARCH_DIR "32bit")
  set(OBS_TITLES_VCPKG_DEFAULT_TRIPLET "x86-windows")
endif()
set(OBS_TITLES_PACKAGE_DIR
    "${CMAKE_BINARY_DIR}/${OBS_TITLES_PLUGIN_NAME}"
    CACHE PATH "Staging directory for the OBS user-plugin folder layout")
set(OBS_TITLES_PACKAGE_BIN_DIR
    "${OBS_TITLES_PACKAGE_DIR}/bin/${OBS_TITLES_ARCH_DIR}")

# ── Qt auto-tools ───────────────────────────────────────────────────
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Find OBS ────────────────────────────────────────────────────────
# Try to find standard CMake config package first (e.g. from CMAKE_PREFIX_PATH)
find_package(libobs QUIET)
find_package(obs-frontend-api QUIET)

if(NOT libobs_FOUND OR NOT obs-frontend-api_FOUND)
  # Allow callers to point at an OBS SDK/install tree without baking in
  # machine-specific paths. CMake cache values take precedence over
  # environment variables so CI and developers can override discovery.
  set(OBS_SEARCH_ROOTS)
  if(OBS_SDK_DIR)
    list(APPEND OBS_SEARCH_ROOTS "${OBS_SDK_DIR}")
  endif()
  if(DEFINED ENV{OBS_SDK_DIR})
    list(APPEND OBS_SEARCH_ROOTS "$ENV{OBS_SDK_DIR}")
  endif()
  if(DEFINED ENV{OBS_STUDIO_DIR})
    list(APPEND OBS_SEARCH_ROOTS "$ENV{OBS_STUDIO_DIR}")
  endif()

  if(WIN32)
    foreach(base_dir
        "$ENV{USERPROFILE}/Desktop/obs-build-dependencies"
        "$ENV{USERPROFILE}/Downloads/obs-build-dependencies"
        "${CMAKE_CURRENT_SOURCE_DIR}/obs-build-dependencies")
      if(IS_DIRECTORY "${base_dir}")
        file(GLOB plugin_deps "${base_dir}/plugin-deps-*")
        list(SORT plugin_deps ORDER DESCENDING)
        list(APPEND OBS_SEARCH_ROOTS ${plugin_deps})
      endif()
    endforeach()

    if(DEFINED ENV{ProgramFiles})
      list(APPEND OBS_SEARCH_ROOTS "$ENV{ProgramFiles}/obs-studio")
    endif()
    if(DEFINED ENV{ProgramW6432})
      list(APPEND OBS_SEARCH_ROOTS "$ENV{ProgramW6432}/obs-studio")
    endif()
  endif()

  list(REMOVE_DUPLICATES OBS_SEARCH_ROOTS)

  foreach(candidate IN LISTS OBS_SEARCH_ROOTS)
    if(NOT IS_DIRECTORY "${candidate}")
      continue()
    endif()

    unset(OBS_LIB CACHE)
    unset(OBS_LIB)
    unset(OBS_FRONTEND_API_LIB CACHE)
    unset(OBS_FRONTEND_API_LIB)
    unset(OBS_INCLUDE_DIR CACHE)
    unset(OBS_INCLUDE_DIR)
    unset(OBS_DLL CACHE)
    unset(OBS_DLL)
    unset(OBS_FRONTEND_API_DLL CACHE)
    unset(OBS_FRONTEND_API_DLL)

    find_library(OBS_LIB NAMES obs
      HINTS "${candidate}/lib" "${candidate}/lib/obs"
      NO_DEFAULT_PATH)
    find_library(OBS_FRONTEND_API_LIB NAMES obs-frontend-api
      HINTS "${candidate}/lib" "${candidate}/lib/obs"
      NO_DEFAULT_PATH)
    find_path(OBS_INCLUDE_DIR NAMES obs.h
      HINTS "${candidate}/include" "${candidate}/include/obs"
      NO_DEFAULT_PATH)

    if(OBS_LIB AND OBS_FRONTEND_API_LIB AND OBS_INCLUDE_DIR)
      set(OBS_SDK_DIR "${candidate}" CACHE PATH "Path to OBS Studio SDK" FORCE)
      message(STATUS "Found OBS SDK at: ${OBS_SDK_DIR}")
      list(APPEND CMAKE_PREFIX_PATH "${OBS_SDK_DIR}")

      # Find DLLs to specify IMPORTED_LOCATION and prevent CMP0111 warnings.
      find_file(OBS_DLL NAMES obs.dll
        HINTS "${OBS_SDK_DIR}/bin/64bit" "${OBS_SDK_DIR}/bin"
        NO_DEFAULT_PATH)
      find_file(OBS_FRONTEND_API_DLL NAMES obs-frontend-api.dll
        HINTS "${OBS_SDK_DIR}/bin/64bit" "${OBS_SDK_DIR}/bin"
        NO_DEFAULT_PATH)

      if(NOT TARGET OBS::libobs)
        add_library(OBS::libobs SHARED IMPORTED GLOBAL)
        set_target_properties(OBS::libobs PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${OBS_INCLUDE_DIR}"
        )
        if(WIN32)
          set_target_properties(OBS::libobs PROPERTIES IMPORTED_IMPLIB "${OBS_LIB}")
          if(OBS_DLL)
            set_target_properties(OBS::libobs PROPERTIES IMPORTED_LOCATION "${OBS_DLL}")
          endif()
        else()
          set_target_properties(OBS::libobs PROPERTIES IMPORTED_LOCATION "${OBS_LIB}")
        endif()
      endif()
      if(NOT TARGET OBS::obs-frontend-api)
        add_library(OBS::obs-frontend-api SHARED IMPORTED GLOBAL)
        set_target_properties(OBS::obs-frontend-api PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${OBS_INCLUDE_DIR}"
        )
        if(WIN32)
          set_target_properties(OBS::obs-frontend-api PROPERTIES IMPORTED_IMPLIB "${OBS_FRONTEND_API_LIB}")
          if(OBS_FRONTEND_API_DLL)
            set_target_properties(OBS::obs-frontend-api PROPERTIES IMPORTED_LOCATION "${OBS_FRONTEND_API_DLL}")
          endif()
        else()
          set_target_properties(OBS::obs-frontend-api PROPERTIES IMPORTED_LOCATION "${OBS_FRONTEND_API_LIB}")
        endif()
      endif()
      set(libobs_FOUND TRUE)
      set(obs-frontend-api_FOUND TRUE)
      break()
    endif()

    unset(OBS_LIB CACHE)
    unset(OBS_LIB)
    unset(OBS_FRONTEND_API_LIB CACHE)
    unset(OBS_FRONTEND_API_LIB)
    unset(OBS_INCLUDE_DIR CACHE)
    unset(OBS_INCLUDE_DIR)
  endforeach()
endif()

if(NOT libobs_FOUND OR NOT obs-frontend-api_FOUND)
  # Fallback to finding them normally if we didn't resolve them manually
  find_package(libobs REQUIRED)
  find_package(obs-frontend-api REQUIRED)
endif()

# ── Find Qt ─────────────────────────────────────────────────────────
# OBS' Windows plugin dependency bundles are Qt-major-specific.  Recent
# bundles place Qt package configs under lib/cmake rather than directly under
# cmake/, so relying on the generic vcpkg toolchain search can incorrectly
# report Qt6 as missing and fall through to Qt5.
set(OBS_FXM_QT6_HINTS)
if(OBS_SDK_DIR)
  list(APPEND OBS_FXM_QT6_HINTS
    "${OBS_SDK_DIR}/lib/cmake/Qt6"
    "${OBS_SDK_DIR}/cmake/Qt6"
    "${OBS_SDK_DIR}/share/cmake/Qt6"
    "${OBS_SDK_DIR}/lib64/cmake/Qt6")

  # Do not retain this discovery result across an OBS SDK change. A stale
  # cached directory can mix two Qt minor-version ABIs in one translation unit.
  unset(OBS_FXM_DISCOVERED_QT6_DIR CACHE)
  find_path(OBS_FXM_DISCOVERED_QT6_DIR
    NAMES Qt6Config.cmake
    HINTS ${OBS_FXM_QT6_HINTS}
    NO_DEFAULT_PATH)
  if(OBS_FXM_DISCOVERED_QT6_DIR)
    set(Qt6_DIR "${OBS_FXM_DISCOVERED_QT6_DIR}" CACHE PATH
        "Qt6 package directory from the OBS dependency bundle" FORCE)
    get_filename_component(OBS_FXM_QT6_CMAKE_ROOT "${Qt6_DIR}/.." ABSOLUTE)
    list(PREPEND CMAKE_PREFIX_PATH "${OBS_FXM_QT6_CMAKE_ROOT}")
    message(STATUS "Found Qt6 package configuration at: ${Qt6_DIR}")
  endif()
endif()

# Resolve the base Qt modules first.  Keeping WebSockets in a separate lookup
# makes a missing optional SDK component produce an accurate Qt6 diagnostic
# instead of the misleading Qt5Config.cmake error.
find_package(Qt6 COMPONENTS Core Widgets Svg Network QUIET)
if(Qt6_FOUND)
  find_package(Qt6 COMPONENTS Multimedia REQUIRED)
  include(cmake/BootstrapQtWebSockets.cmake)
  obs_fxm_ensure_qt6_websockets()
  set(QT_VERSION_MAJOR 6)
elseif(OBS_SDK_DIR)
  message(FATAL_ERROR
    "The detected OBS SDK is expected to use Qt6, but Qt6Config.cmake or one "
    "of the required Qt6 modules could not be loaded. Checked: "
    "${OBS_FXM_QT6_HINTS}. Set Qt6_DIR to the directory containing "
    "Qt6Config.cmake if your dependency bundle uses a different layout.")
else()
  find_package(Qt5 5.15 COMPONENTS Core Widgets Svg Network WebSockets Multimedia REQUIRED)
  set(QT_VERSION_MAJOR 5)
endif()

set(OBS_FXM_QT_MULTIMEDIA_TARGET Qt${QT_VERSION_MAJOR}::Multimedia)

set(QT_LIBS
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Svg
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebSockets)

# ── Find Cairo + Pango ──────────────────────────────────────────────
find_package(PkgConfig QUIET)
set(OBS_FXM_TEXT_RENDER_TARGETS "")
if(PKG_CONFIG_FOUND)
  # IMPORTED_TARGET is important on Windows/MSVC: it preserves the vcpkg
  # library directories and transitive link dependencies. Linking the raw
  # *_LIBRARIES values can otherwise emit only `cairo.lib`, with no /LIBPATH.
  pkg_check_modules(CAIRO QUIET IMPORTED_TARGET cairo)
  pkg_check_modules(PANGO QUIET IMPORTED_TARGET pango)
  pkg_check_modules(PANGOCAIRO QUIET IMPORTED_TARGET pangocairo)
  if(TARGET PkgConfig::CAIRO AND TARGET PkgConfig::PANGO AND TARGET PkgConfig::PANGOCAIRO)
    list(APPEND OBS_FXM_TEXT_RENDER_TARGETS
      PkgConfig::CAIRO
      PkgConfig::PANGO
      PkgConfig::PANGOCAIRO)
  endif()
endif()

if(NOT CAIRO_FOUND OR NOT PANGO_FOUND OR NOT PANGOCAIRO_FOUND)
  message(STATUS "PkgConfig not found or missing components. Falling back to native search...")

  find_path(CAIRO_INCLUDE_DIR NAMES cairo.h PATH_SUFFIXES cairo)
  find_library(CAIRO_LIBRARY NAMES cairo)

  find_path(PANGO_INCLUDE_DIR NAMES pango/pango.h PATH_SUFFIXES pango-1.0)
  find_library(PANGO_LIBRARY NAMES pango-1.0)

  find_path(PANGOCAIRO_INCLUDE_DIR NAMES pango/pangocairo.h PATH_SUFFIXES pango-1.0)
  find_library(PANGOCAIRO_LIBRARY NAMES pangocairo-1.0)

  find_path(GLIB_INCLUDE_DIR NAMES glib.h PATH_SUFFIXES glib-2.0)
  if(GLIB_INCLUDE_DIR)
    get_filename_component(GLIB_PARENT_DIR "${GLIB_INCLUDE_DIR}" DIRECTORY)
    get_filename_component(GLIB_GRANDPARENT_DIR "${GLIB_PARENT_DIR}" DIRECTORY)
    find_path(GLIB_CONFIG_INCLUDE_DIR NAMES glibconfig.h
      HINTS "${GLIB_PARENT_DIR}/lib" "${GLIB_GRANDPARENT_DIR}/lib"
      PATH_SUFFIXES glib-2.0/include
    )
  else()
    find_path(GLIB_CONFIG_INCLUDE_DIR NAMES glibconfig.h PATH_SUFFIXES glib-2.0/include)
  endif()
  find_library(GLIB_LIBRARY NAMES glib-2.0)
  find_library(GOBJECT_LIBRARY NAMES gobject-2.0)
  find_path(HARFBUZZ_INCLUDE_DIR NAMES hb.h PATH_SUFFIXES harfbuzz)
  find_library(HARFBUZZ_LIBRARY NAMES harfbuzz)

  if(CAIRO_INCLUDE_DIR AND CAIRO_LIBRARY AND PANGO_INCLUDE_DIR AND PANGO_LIBRARY AND PANGOCAIRO_LIBRARY)
    set(CAIRO_FOUND TRUE)
    get_filename_component(CAIRO_PARENT_DIR "${CAIRO_INCLUDE_DIR}" DIRECTORY)
    set(CAIRO_INCLUDE_DIRS ${CAIRO_INCLUDE_DIR} ${CAIRO_PARENT_DIR})
    set(CAIRO_LIBRARIES ${CAIRO_LIBRARY})

    set(PANGO_FOUND TRUE)
    get_filename_component(PANGO_PARENT_DIR "${PANGO_INCLUDE_DIR}" DIRECTORY)
    set(PANGO_INCLUDE_DIRS ${PANGO_INCLUDE_DIR} ${PANGO_PARENT_DIR} ${GLIB_INCLUDE_DIR} ${GLIB_CONFIG_INCLUDE_DIR})
    if(HARFBUZZ_INCLUDE_DIR)
      list(APPEND PANGO_INCLUDE_DIRS ${HARFBUZZ_INCLUDE_DIR})
    endif()
    set(PANGO_LIBRARIES ${PANGO_LIBRARY} ${GLIB_LIBRARY} ${GOBJECT_LIBRARY})
    if(HARFBUZZ_LIBRARY)
      list(APPEND PANGO_LIBRARIES ${HARFBUZZ_LIBRARY})
    endif()

    set(PANGOCAIRO_FOUND TRUE)
    get_filename_component(PANGOCAIRO_PARENT_DIR "${PANGOCAIRO_INCLUDE_DIR}" DIRECTORY)
    set(PANGOCAIRO_INCLUDE_DIRS ${PANGOCAIRO_INCLUDE_DIR} ${PANGOCAIRO_PARENT_DIR})
    set(PANGOCAIRO_LIBRARIES ${PANGOCAIRO_LIBRARY})

    add_library(OBS_FXM_CAIRO UNKNOWN IMPORTED)
    set_target_properties(OBS_FXM_CAIRO PROPERTIES
      IMPORTED_LOCATION "${CAIRO_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${CAIRO_INCLUDE_DIRS}")
    add_library(OBS_FXM_PANGO UNKNOWN IMPORTED)
    set_target_properties(OBS_FXM_PANGO PROPERTIES
      IMPORTED_LOCATION "${PANGO_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${PANGO_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "${GLIB_LIBRARY};${GOBJECT_LIBRARY};${HARFBUZZ_LIBRARY}")
    add_library(OBS_FXM_PANGOCAIRO UNKNOWN IMPORTED)
    set_target_properties(OBS_FXM_PANGOCAIRO PROPERTIES
      IMPORTED_LOCATION "${PANGOCAIRO_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${PANGOCAIRO_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "OBS_FXM_CAIRO;OBS_FXM_PANGO")
    list(APPEND OBS_FXM_TEXT_RENDER_TARGETS
      OBS_FXM_CAIRO OBS_FXM_PANGO OBS_FXM_PANGOCAIRO)

    message(STATUS "Native Cairo found: ${CAIRO_LIBRARY}")
    message(STATUS "Native Pango found: ${PANGO_LIBRARY}")
    message(STATUS "Native PangoCairo found: ${PANGOCAIRO_LIBRARY}")
  else()
    message(FATAL_ERROR "Could not find Cairo, Pango, or PangoCairo. Please install dependencies.")
  endif()
endif()

# ── nlohmann/json ────────────────────────────────────────────────────
# Either install via package manager:  apt install nlohmann-json3-dev
# or rely on FetchContent:
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# QtColorWidgets provides the OBS-themed color wheel/dialog used by all editor
# color pickers. Keep the tag pinned so clean builds stay reproducible.
FetchContent_Declare(
  QtColorWidgets
  GIT_REPOSITORY https://gitlab.com/mattbas/Qt-Color-Widgets.git
  GIT_TAG 8491078434b24cba295b5e41cc0d2a94c7049a5b
  SOURCE_SUBDIR cmake-do-not-add
  GIT_PROGRESS FALSE
)
FetchContent_MakeAvailable(QtColorWidgets)

set(OBS_FXM_QTCOLORWIDGETS_ROOT "${qtcolorwidgets_SOURCE_DIR}")
set(OBS_FXM_QTCOLORWIDGETS_SOURCES
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/abstract_widget_list.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/bound_color_selector.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_2d_slider.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_delegate.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_dialog.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_dialog.ui"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_line_edit.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_list_widget.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_names.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_palette.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_palette_model.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_palette_widget.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_palette_widget.ui"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_preview.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_selector.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_utils.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/color_wheel.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/gradient_editor.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/gradient_list_model.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/gradient_slider.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/harmony_color_wheel.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/hue_slider.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/src/QtColorWidgets/swatch.cpp"
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/resources/QtColorWidgets/color_widgets.qrc"
)
file(GLOB OBS_FXM_QTCOLORWIDGETS_HEADERS CONFIGURE_DEPENDS
     "${OBS_FXM_QTCOLORWIDGETS_ROOT}/include/QtColorWidgets/*.hpp")

add_library(QtColorWidgets STATIC
    ${OBS_FXM_QTCOLORWIDGETS_SOURCES}
    ${OBS_FXM_QTCOLORWIDGETS_HEADERS}
)
target_compile_definitions(QtColorWidgets PUBLIC QTCOLORWIDGETS_STATICALLY_LINKED)
target_include_directories(QtColorWidgets PUBLIC
    "${OBS_FXM_QTCOLORWIDGETS_ROOT}/include")
target_link_libraries(QtColorWidgets PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
set_target_properties(QtColorWidgets PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)

# ── Sources ─────────────────────────────────────────────────────────
include("${FLUX_SUITE_COMMON_DIR}/cmake/FluxSuiteCommon.cmake")
include("${OBS_FXM_OBS_PLUGIN_ROOT}/cmake/FluxMotionObsPlugin.cmake")

set(OBS_FXM_CORE_MODEL_HEADERS
    Core/layer-model.h
    Core/title-data.h
    Core/title-snapshot.h
)

set(OBS_FXM_CORE_TIMELINE_HEADERS
    Core/animation.h
)

set(OBS_FXM_CORE_ANIMATION_SOURCES
    Core/animation.cpp
)

set(OBS_FXM_CORE_SERIALIZATION_SOURCES
    Core/packed-title-format.cpp
)
set(OBS_FXM_CORE_SERIALIZATION_HEADERS
    Core/packed-title-format.h
    Core/serialization-passthrough.h
    Core/title-serialization-schema.h
)

set(OBS_FXM_CORE_ASSET_SOURCES
    Core/asset-runtime.cpp
    Core/linked-asset.cpp
)
set(OBS_FXM_CORE_ASSET_HEADERS
    Core/asset-runtime.h
    Core/linked-asset.h
)
set(OBS_FXM_CORE_PLAYBACK_SOURCES
    Core/style-preset-runtime.cpp
    Core/title-video-runtime.cpp
)
set(OBS_FXM_CORE_PLAYBACK_HEADERS
    Core/playback-controller.h
    Core/project-loader.h
    Core/style-preset-runtime.h
    Core/title-video-runtime.h
    Core/audio-output-scheduler.h
    Core/audio-transport-direction.h
)

set(OBS_FXM_OBS_PLUGIN_LEGACY_RENDERING_SOURCES
    src/rendering/title-effect-registry.cpp
    src/rendering/title-gpu-text-renderer.cpp
)
set(OBS_FXM_OBS_PLUGIN_LEGACY_RENDERING_HEADERS
    src/rendering/title-effect-registry.h
    src/rendering/title-gpu-text-renderer.h
)
set(OBS_FXM_CORE_SOURCES
    src/core/chart-data.cpp
    src/core/title-data.cpp
    src/core/title-logger.cpp
    src/core/title-preferences.cpp
    src/core/ticker-runtime.cpp
    src/core/external-data.cpp
    src/core/external-data-log.cpp
    src/core/external-data-provider.cpp
)
set(OBS_FXM_CORE_HEADERS
    src/core/chart-data.h
    src/core/build-info.h
    src/core/live-text-cue-utils.h
    src/core/preset-category-path.h
    src/core/title-logger.h
    src/core/title-preferences.h
    src/core/ticker-runtime.h
    src/core/external-data-log.h
    src/core/external-data.h
    src/core/external-data-types.h
    src/core/external-data-provider.h
)

set(OBS_FXM_LAYER_HEADERS
    src/layers/image-layer-utils.h
    src/layers/path-geometry.h
    src/layers/stroke-path-geometry.h
    src/layers/layer-stack-widget.h
)

set(OBS_FXM_LAYER_SOURCES
    src/layers/image-layer-utils.cpp
    src/layers/path-geometry.cpp
    src/layers/stroke-path-geometry.cpp
    src/layers/layer-stack-widget.cpp
)

set(OBS_FXM_EXTENSION_HEADERS
    src/extensions/fxm-plugin-api.h
    src/extensions/effect-extension-catalog.h
)
set(OBS_FXM_EXTENSION_SOURCES
    src/extensions/effect-extension-catalog.cpp
)

set(OBS_FXM_EFFECT_HEADERS
    src/effects/layer-effects.h
    src/effects/effect-runtime.h
    src/effects/effect-preset-catalog.h
    src/effects/effects-panel.h
    src/effects/effects-presets-panel.h
)

set(OBS_FXM_EFFECT_SOURCES
    src/effects/effect-runtime.cpp
    src/effects/effect-preset-catalog.cpp
    src/effects/effects-panel.cpp
    src/effects/effects-presets-panel.cpp
)

set(OBS_FXM_TIMELINE_SOURCES
    src/timeline/timeline-widget.cpp
)
set(OBS_FXM_TIMELINE_HEADERS
    src/timeline/timeline-scroll-zoom-bar.h
    src/timeline/timeline-widget.h
    src/timeline/temporal-graph-editor.inc
)

set(OBS_FXM_TRANSITION_SOURCES
    src/transitions/transition-preset-catalog.cpp
    src/transitions/transition-editor-dialog.cpp
)
set(OBS_FXM_TRANSITION_HEADERS
    src/transitions/layer-transition.h
    src/transitions/transition-preset-catalog.h
    src/transitions/transition-editor-dialog.h
)

set(OBS_FXM_TEXT_SOURCES
    src/text/title-rich-text.cpp
    src/text/text-animator.cpp
    src/text/text-animator-presets.cpp
    src/text/text-animator-preset-io.cpp
    src/text/title-text-layout.cpp
    src/text/title-text-layout-qt.cpp
)
set(OBS_FXM_TEXT_HEADERS
    src/text/title-rich-text.h
    src/text/text-animator.h
    src/text/text-animator-presets.h
    src/text/text-animator-preset-io.h
    src/text/title-text-layout.h
    src/text/title-text-layout-qt-font-registry.h
    src/text/pattern-resource-cache.h
)

set(OBS_FXM_RENDERING_SOURCES
    src/rendering/chart-renderer.cpp
    src/rendering/layer-transform-3d.cpp
    src/rendering/title-gpu-text-sdf.cpp
)
set(OBS_FXM_RENDERING_HEADERS
    src/rendering/chart-renderer.h
    src/rendering/layer-transform-3d.h
    src/rendering/title-gpu-text-sdf.h
)

set(OBS_FXM_CACHE_SOURCES
    src/cache/cache-frame-key.cpp
    src/cache/cache-state-tracker.cpp
    src/cache/cache-manager.cpp
    src/cache/prerender-dock.cpp
    src/cache/ram-frame-cache.cpp
    src/cache/render-queue-manager.cpp
)
set(OBS_FXM_CACHE_HEADERS
    src/cache/cache-frame-payload.h
    src/cache/cache-tile-payload.h
    src/cache/cache-manager.h
    src/cache/cache-time.h
    src/cache/title-cache-policy.h
    src/cache/prerender-dock.h
)

set(OBS_FXM_CANVAS_SOURCES
    src/canvas/canvas-preview-inline-text.cpp
    src/canvas/canvas-preview.cpp
)
set(OBS_FXM_CANVAS_HEADERS
    src/canvas/canvas-preview.h
)

set(OBS_FXM_EDITOR_MODULE_HEADERS
    Editor/asset-library.h
    Editor/editor-theme.h
    Editor/editor-tool-icons.h
    Editor/external-data-settings-dialog.h
    Editor/external-data-binding-dialog.h
    Editor/external-data-table-mapping-dialog.h
    Editor/fxm-modern-controls.h
    Editor/hold-menu-tool-button.h
    Editor/long-press-tool-button.h
    Editor/open-color-palette.h
    Editor/timecode-spinbox.h
    Editor/title-animation-edit-utils.h
    Editor/title-assets.h
    src/editor/properties-panel.h
    src/editor/title-editor-internal.h
    src/editor/title-properties-panel.h
    src/editor/tools-sidebar.h
    src/editor/style-presets.h
    src/editor/title-dock.h
    src/editor/title-editor.h
)
set(OBS_FXM_EDITOR_MODULE_SOURCES
    Editor/asset-library.cpp
    Editor/editor-theme.cpp
    Editor/editor-tool-icons.cpp
    Editor/external-data-settings-dialog.cpp
    Editor/external-data-binding-dialog.cpp
    Editor/external-data-table-mapping-dialog.cpp
    Editor/fxm-modern-controls.cpp
    Editor/hold-menu-tool-button.cpp
    Editor/timecode-spinbox.cpp
    Editor/title-assets.cpp
    src/editor/properties-panel.cpp
    src/editor/title-properties-panel.cpp
    src/editor/tools-sidebar.cpp
    src/editor/style-presets.cpp
    src/editor/title-dock.cpp
    src/editor/title-editor.cpp
)

# The OBS deliverable owns the operational dock, but never the full Editor
# window. Keep this inventory narrow so adding a dock feature cannot silently
# pull TitleEditor, its panels, or its timeline UI back into the plugin.
set(OBS_FXM_OBS_DOCK_SOURCES
    Editor/external-data-settings-dialog.cpp
    Editor/external-data-binding-dialog.cpp
    Editor/external-data-table-mapping-dialog.cpp
    Editor/fxm-modern-controls.cpp
    Editor/hold-menu-tool-button.cpp
    Editor/timecode-spinbox.cpp
    Editor/title-assets.cpp
    src/editor/title-dock.cpp
)
set(OBS_FXM_OBS_DOCK_HEADERS
    Editor/external-data-settings-dialog.h
    Editor/external-data-binding-dialog.h
    Editor/external-data-table-mapping-dialog.h
    Editor/fxm-modern-controls.h
    Editor/hold-menu-tool-button.h
    Editor/long-press-tool-button.h
    Editor/timecode-spinbox.h
    Editor/title-assets.h
    src/editor/title-dock.h
)

include(cmake/FxmSourceModules.cmake)

set(PLUGIN_MODULES
    ${OBS_FXM_IMPLEMENTATION_MODULES}
)

set(PLUGIN_SOURCES
    ${OBS_FXM_OBS_PLUGIN_LEGACY_RENDERING_SOURCES}
    ${OBS_FXM_CORE_SOURCES}
    ${OBS_FXM_LAYER_SOURCES}
    ${OBS_FXM_EXTENSION_SOURCES}
    ${OBS_FXM_EFFECT_SOURCES}
    ${OBS_FXM_TIMELINE_SOURCES}
    ${OBS_FXM_TRANSITION_SOURCES}
    ${OBS_FXM_TEXT_SOURCES}
    ${OBS_FXM_RENDERING_SOURCES}
    ${OBS_FXM_CACHE_SOURCES}
    ${OBS_FXM_CANVAS_SOURCES}
)

set(PLUGIN_HEADERS
    ${OBS_FXM_OBS_PLUGIN_LEGACY_RENDERING_HEADERS}
    ${OBS_FXM_CORE_HEADERS}
    ${OBS_FXM_LAYER_HEADERS}
    ${OBS_FXM_EXTENSION_HEADERS}
    ${OBS_FXM_EFFECT_HEADERS}
    ${OBS_FXM_TIMELINE_HEADERS}
    ${OBS_FXM_TRANSITION_HEADERS}
    ${OBS_FXM_TEXT_HEADERS}
    ${OBS_FXM_RENDERING_HEADERS}
    ${OBS_FXM_CACHE_HEADERS}
    ${OBS_FXM_CANVAS_HEADERS}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${PLUGIN_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Header Files" FILES ${PLUGIN_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Implementation Modules" FILES ${PLUGIN_MODULES})
source_group(TREE "${FLUX_SUITE_COMMON_DIR}/Shared" PREFIX "Shared"
    FILES ${OBS_FXM_SHARED_SOURCES} ${OBS_FXM_SHARED_HEADERS}
          ${OBS_FXM_OBS_PLUGIN_SOURCE_SOURCES}
          ${OBS_FXM_OBS_PLUGIN_SOURCE_HEADERS}
          ${OBS_FXM_OBS_PLUGIN_SOURCE_MODULES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Core" PREFIX "Core"
    FILES ${OBS_FXM_CORE_MODEL_HEADERS} ${OBS_FXM_CORE_TIMELINE_HEADERS}
          ${OBS_FXM_CORE_ANIMATION_SOURCES}
          ${OBS_FXM_CORE_SERIALIZATION_SOURCES}
          ${OBS_FXM_CORE_SERIALIZATION_HEADERS}
          ${OBS_FXM_CORE_ASSET_SOURCES}
          ${OBS_FXM_CORE_ASSET_HEADERS}
          ${OBS_FXM_CORE_PLAYBACK_SOURCES}
          ${OBS_FXM_CORE_PLAYBACK_HEADERS})
source_group(TREE "${OBS_FXM_OBS_PLUGIN_DIR}" PREFIX "OBS Plugin"
    FILES ${OBS_FXM_OBS_PLUGIN_RENDERING_SOURCES}
          ${OBS_FXM_OBS_PLUGIN_RENDERING_HEADERS}
          ${OBS_FXM_OBS_PLUGIN_AUDIO_SOURCES}
          ${OBS_FXM_OBS_PLUGIN_AUDIO_HEADERS}
          ${OBS_FXM_OBS_PLUGIN_HOST_SOURCES}
          ${OBS_FXM_OBS_PLUGIN_HOST_HEADERS}
          ${OBS_FXM_OBS_PLUGIN_INTEGRATION_SOURCES}
          ${OBS_FXM_OBS_PLUGIN_INTEGRATION_HEADERS})
source_group("OBS Plugin\\Rendering"
    FILES ${OBS_FXM_OBS_PLUGIN_LEGACY_RENDERING_SOURCES}
          ${OBS_FXM_OBS_PLUGIN_LEGACY_RENDERING_HEADERS})
source_group("Editor"
    FILES ${OBS_FXM_EDITOR_MODULE_SOURCES}
          ${OBS_FXM_EDITOR_MODULE_HEADERS})

# ── LZ4 frame compression ───────────────────────────────────────────
# Prefer a package supplied by OBS/vcpkg/the host. When none is installed,
# build the pinned official LZ4 1.10.0 source directly so compressed disk
# caching does not silently degrade to raw BGRA on a clean build machine.
find_package(lz4 CONFIG QUIET)
set(OBS_FXM_LZ4_TARGET "")
if(TARGET lz4::lz4)
  set(OBS_FXM_LZ4_TARGET lz4::lz4)
elseif(TARGET LZ4::lz4)
  set(OBS_FXM_LZ4_TARGET LZ4::lz4)
else()
  find_path(OBS_FXM_LZ4_INCLUDE_DIR NAMES lz4.h)
  find_library(OBS_FXM_LZ4_LIBRARY NAMES lz4 liblz4)
  if(OBS_FXM_LZ4_INCLUDE_DIR AND OBS_FXM_LZ4_LIBRARY)
    add_library(OBS_FXM_LZ4 UNKNOWN IMPORTED)
    set_target_properties(OBS_FXM_LZ4 PROPERTIES
      IMPORTED_LOCATION "${OBS_FXM_LZ4_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${OBS_FXM_LZ4_INCLUDE_DIR}")
    set(OBS_FXM_LZ4_TARGET OBS_FXM_LZ4)
  else()
    FetchContent_Declare(
      obs_fxm_lz4_source
      GIT_REPOSITORY https://github.com/lz4/lz4.git
      GIT_TAG ebb370ca83af193212df4dcbadcc5d87bc0de2f0
      GIT_PROGRESS FALSE
    )
    FetchContent_GetProperties(obs_fxm_lz4_source)
    if(NOT obs_fxm_lz4_source_POPULATED)
      FetchContent_Populate(obs_fxm_lz4_source)
    endif()

    add_library(obs_fxm_lz4 STATIC
      "${obs_fxm_lz4_source_SOURCE_DIR}/lib/lz4.c")
    target_include_directories(obs_fxm_lz4 PUBLIC
      "${obs_fxm_lz4_source_SOURCE_DIR}/lib")
    set_target_properties(obs_fxm_lz4 PROPERTIES
      POSITION_INDEPENDENT_CODE ON)
    set(OBS_FXM_LZ4_TARGET obs_fxm_lz4)
  endif()
endif()

# ── Target ──────────────────────────────────────────────────────────
add_library(flux-motion MODULE
    ${OBS_FXM_SHARED_SOURCES}
    ${OBS_FXM_SHARED_HEADERS}
    ${OBS_FXM_CORE_MODEL_HEADERS}
    ${OBS_FXM_CORE_TIMELINE_HEADERS}
    ${OBS_FXM_CORE_ANIMATION_SOURCES}
    ${OBS_FXM_CORE_SERIALIZATION_SOURCES}
    ${OBS_FXM_CORE_SERIALIZATION_HEADERS}
    ${OBS_FXM_CORE_ASSET_SOURCES}
    ${OBS_FXM_CORE_ASSET_HEADERS}
    ${OBS_FXM_CORE_PLAYBACK_SOURCES}
    ${OBS_FXM_CORE_PLAYBACK_HEADERS}
    ${OBS_FXM_OBS_PLUGIN_RENDERING_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_RENDERING_HEADERS}
    ${OBS_FXM_OBS_PLUGIN_SOURCE_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_SOURCE_HEADERS}
    ${OBS_FXM_OBS_PLUGIN_SOURCE_MODULES}
    ${OBS_FXM_OBS_PLUGIN_AUDIO_SOURCES}
    "${OBS_FXM_OBS_PLUGIN_DIR}/obs-asset-path-provider.cpp"
    ${OBS_FXM_OBS_PLUGIN_AUDIO_HEADERS}
    ${OBS_FXM_OBS_PLUGIN_HOST_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_HOST_HEADERS}
    ${OBS_FXM_OBS_PLUGIN_INTEGRATION_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_INTEGRATION_HEADERS}
    ${OBS_FXM_OBS_DOCK_SOURCES}
    ${OBS_FXM_OBS_DOCK_HEADERS}
    ${PLUGIN_SOURCES}
    ${PLUGIN_HEADERS}
    ${PLUGIN_MODULES}
)

target_include_directories(flux-motion PRIVATE
    "${FLUX_SUITE_COMMON_DIR}/Shared"
    "${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine"
    Core
    "${OBS_FXM_OBS_PLUGIN_DIR}"
    src
    src/core
    src/layers
    src/effects
    src/timeline
    src/transitions
    src/text
    src/rendering
    src/cache
    src/canvas
    src/editor
    Editor
    src/extensions
    ${CAIRO_INCLUDE_DIRS}
    ${PANGO_INCLUDE_DIRS}
    ${PANGOCAIRO_INCLUDE_DIRS}
)

target_link_libraries(flux-motion PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    ${QT_LIBS}
    QtColorWidgets
    ${OBS_FXM_TEXT_RENDER_TARGETS}
    nlohmann_json::nlohmann_json
    ${OBS_FXM_LZ4_TARGET}
)

# Optional FFmpeg backend for MP3/FLAC/AAC/M4A and audio streams in video assets.
# A dependency-free PCM WAV decoder remains available when the development
# packages are not present, so source loading never becomes a hard failure.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(FXM_FFMPEG QUIET IMPORTED_TARGET
    libavformat libavcodec libavutil libswresample libswscale)
endif()
if(TARGET PkgConfig::FXM_FFMPEG)
  target_link_libraries(flux-motion PRIVATE PkgConfig::FXM_FFMPEG)
  target_compile_definitions(flux-motion PRIVATE FXM_HAVE_FFMPEG=1)
else()
  # Windows/vcpkg does not always expose FFmpeg through pkg-config.
  find_path(FXM_AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
  find_library(FXM_AVFORMAT_LIBRARY NAMES avformat)
  find_library(FXM_AVCODEC_LIBRARY NAMES avcodec)
  find_library(FXM_AVUTIL_LIBRARY NAMES avutil)
  find_library(FXM_SWRESAMPLE_LIBRARY NAMES swresample)
  find_library(FXM_SWSCALE_LIBRARY NAMES swscale)
  if(FXM_AVFORMAT_INCLUDE_DIR AND FXM_AVFORMAT_LIBRARY AND FXM_AVCODEC_LIBRARY
     AND FXM_AVUTIL_LIBRARY AND FXM_SWRESAMPLE_LIBRARY AND FXM_SWSCALE_LIBRARY)
    target_include_directories(flux-motion PRIVATE ${FXM_AVFORMAT_INCLUDE_DIR})
    target_link_libraries(flux-motion PRIVATE
      ${FXM_AVFORMAT_LIBRARY} ${FXM_AVCODEC_LIBRARY}
      ${FXM_AVUTIL_LIBRARY} ${FXM_SWRESAMPLE_LIBRARY} ${FXM_SWSCALE_LIBRARY})
    target_compile_definitions(flux-motion PRIVATE FXM_HAVE_FFMPEG=1)
  else()
    message(WARNING "FFmpeg development libraries were not found; only PCM WAV audio will decode and Video layers are unavailable")
  endif()
endif()

if(CAIRO_CFLAGS_OTHER)
  target_compile_options(flux-motion PRIVATE ${CAIRO_CFLAGS_OTHER})
endif()
if(PANGO_CFLAGS_OTHER)
  target_compile_options(flux-motion PRIVATE ${PANGO_CFLAGS_OTHER})
endif()
if(PANGOCAIRO_CFLAGS_OTHER)
  target_compile_options(flux-motion PRIVATE ${PANGOCAIRO_CFLAGS_OTHER})
endif()

# ── Locale / data files ──────────────────────────────────────────────
target_compile_definitions(flux-motion PRIVATE
    FXM_CANVAS_WITH_OBS=1
    PLUGIN_NAME="${OBS_TITLES_PLUGIN_NAME}"
    PLUGIN_VERSION="${OBS_FXM_VERSION}"
    FXM_VERSION_LABEL="${FLUX_MOTION_VERSION_LABEL}"
    FXM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FXM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FXM_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    FXM_DEVELOPMENT_VERSION="${OBS_FXM_DEVELOPMENT_VERSION}"
)
if(OBS_FXM_LZ4_TARGET)
    target_compile_definitions(flux-motion PRIVATE OBS_FXM_HAVE_LZ4=1)
endif()

# The production OBS target owns the dock and host/runtime adapters, but never
# the standalone Editor window or its editing panels.
set(OBS_FXM_LIGHTWEIGHT_PLUGIN_SOURCES
    ${OBS_FXM_SHARED_SOURCES}
    ${OBS_FXM_CORE_ANIMATION_SOURCES}
    ${OBS_FXM_CORE_SERIALIZATION_SOURCES}
    ${OBS_FXM_CORE_ASSET_SOURCES}
    ${OBS_FXM_CORE_PLAYBACK_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_LEGACY_RENDERING_SOURCES}
    "${OBS_FXM_OBS_PLUGIN_DIR}/obs-render-backend.cpp"
    "${OBS_FXM_OBS_PLUGIN_DIR}/obs-title-preview-renderer.cpp"
    ${OBS_FXM_OBS_PLUGIN_SOURCE_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_AUDIO_SOURCES}
    "${OBS_FXM_OBS_PLUGIN_DIR}/obs-asset-path-provider.cpp"
    "${OBS_FXM_OBS_PLUGIN_DIR}/obs-editor-host.cpp"
    "${OBS_FXM_OBS_PLUGIN_DIR}/obs-frame-rate-provider.cpp"
    "${OBS_FXM_OBS_PLUGIN_DIR}/obs-host-context-provider.cpp"
    "${OBS_FXM_OBS_PLUGIN_DIR}/obs-logger.cpp"
    "${OBS_FXM_OBS_PLUGIN_DIR}/obs-plugin-preferences.cpp"
    "${OBS_FXM_OBS_PLUGIN_DIR}/obs-translation-provider.cpp"
    ${OBS_FXM_OBS_PLUGIN_INTEGRATION_SOURCES}
    ${OBS_FXM_CORE_SOURCES}
    src/layers/image-layer-utils.cpp
    src/layers/path-geometry.cpp
    src/layers/stroke-path-geometry.cpp
    ${OBS_FXM_EXTENSION_SOURCES}
    src/effects/effect-runtime.cpp
    src/effects/effect-preset-catalog.cpp
    src/transitions/transition-preset-catalog.cpp
    ${OBS_FXM_TEXT_SOURCES}
    ${OBS_FXM_RENDERING_SOURCES}
    src/cache/cache-frame-key.cpp
    src/cache/cache-state-tracker.cpp
    src/cache/cache-manager.cpp
    src/cache/ram-frame-cache.cpp
    src/cache/render-queue-manager.cpp
    ${OBS_FXM_CANVAS_SOURCES}
    ${OBS_FXM_OBS_DOCK_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_SOURCE_MODULES}
)

add_library(flux-motion-obs-plugin MODULE
    ${OBS_FXM_LIGHTWEIGHT_PLUGIN_SOURCES})
target_include_directories(flux-motion-obs-plugin PRIVATE
    "${FLUX_SUITE_COMMON_DIR}/Shared" "${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine"
    Core "${OBS_FXM_OBS_PLUGIN_DIR}"
    src src/core src/layers src/effects src/timeline
    src/transitions src/text src/rendering src/cache src/canvas src/editor
    Editor src/extensions
    ${CAIRO_INCLUDE_DIRS} ${PANGO_INCLUDE_DIRS} ${PANGOCAIRO_INCLUDE_DIRS})
target_link_libraries(flux-motion-obs-plugin PRIVATE
    OBS::libobs OBS::obs-frontend-api
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Svg
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebSockets
    QtColorWidgets
    ${OBS_FXM_TEXT_RENDER_TARGETS}
    nlohmann_json::nlohmann_json
    ${OBS_FXM_LZ4_TARGET})
target_compile_definitions(flux-motion-obs-plugin PRIVATE
    FXM_OBS_PLUGIN_WITH_DOCK=1
    FXM_CANVAS_WITH_OBS=1
    PLUGIN_NAME="${OBS_TITLES_PLUGIN_NAME}"
    PLUGIN_VERSION="${OBS_FXM_PLUGIN_VERSION}"
    FXM_VERSION_LABEL="${FLUX_MOTION_OBS_VERSION_LABEL}"
    FXM_DEVELOPMENT_VERSION="${OBS_FXM_DEVELOPMENT_VERSION}")
if(OBS_FXM_LZ4_TARGET)
    target_compile_definitions(flux-motion-obs-plugin PRIVATE OBS_FXM_HAVE_LZ4=1)
endif()
if(TARGET PkgConfig::FXM_FFMPEG)
    target_link_libraries(flux-motion-obs-plugin PRIVATE PkgConfig::FXM_FFMPEG)
    target_compile_definitions(flux-motion-obs-plugin PRIVATE FXM_HAVE_FFMPEG=1)
elseif(FXM_AVFORMAT_INCLUDE_DIR AND FXM_AVFORMAT_LIBRARY AND FXM_AVCODEC_LIBRARY
       AND FXM_AVUTIL_LIBRARY AND FXM_SWRESAMPLE_LIBRARY AND FXM_SWSCALE_LIBRARY)
    target_include_directories(flux-motion-obs-plugin PRIVATE ${FXM_AVFORMAT_INCLUDE_DIR})
    target_link_libraries(flux-motion-obs-plugin PRIVATE
        ${FXM_AVFORMAT_LIBRARY} ${FXM_AVCODEC_LIBRARY}
        ${FXM_AVUTIL_LIBRARY} ${FXM_SWRESAMPLE_LIBRARY} ${FXM_SWSCALE_LIBRARY})
    target_compile_definitions(flux-motion-obs-plugin PRIVATE FXM_HAVE_FFMPEG=1)
endif()
if(CAIRO_CFLAGS_OTHER)
    target_compile_options(flux-motion-obs-plugin PRIVATE ${CAIRO_CFLAGS_OTHER})
endif()
if(PANGO_CFLAGS_OTHER)
    target_compile_options(flux-motion-obs-plugin PRIVATE ${PANGO_CFLAGS_OTHER})
endif()
if(PANGOCAIRO_CFLAGS_OTHER)
    target_compile_options(flux-motion-obs-plugin PRIVATE ${PANGOCAIRO_CFLAGS_OTHER})
endif()
set_target_properties(flux-motion-obs-plugin PROPERTIES
    OUTPUT_NAME "flux-motion"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/flux-motion-obs-plugin/bin/${OBS_TITLES_ARCH_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/flux-motion-obs-plugin/bin/${OBS_TITLES_ARCH_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/lightweight")
foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(flux-motion-obs-plugin PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/flux-motion-obs-plugin/bin/${OBS_TITLES_ARCH_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/flux-motion-obs-plugin/bin/${OBS_TITLES_ARCH_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib/lightweight/${config}")
endforeach()

set(OBS_FXM_STANDALONE_EDITOR_SOURCES
    ${OBS_FXM_SHARED_SOURCES}
    ${OBS_FXM_CORE_ANIMATION_SOURCES}
    ${OBS_FXM_CORE_SERIALIZATION_SOURCES}
    ${OBS_FXM_CORE_ASSET_SOURCES}
    ${OBS_FXM_CORE_PLAYBACK_SOURCES}
    ${OBS_FXM_CORE_SOURCES}
    ${OBS_FXM_LAYER_SOURCES}
    ${OBS_FXM_EXTENSION_SOURCES}
    ${OBS_FXM_EFFECT_SOURCES}
    ${OBS_FXM_TIMELINE_SOURCES}
    ${OBS_FXM_TRANSITION_SOURCES}
    ${OBS_FXM_TEXT_SOURCES}
    ${OBS_FXM_RENDERING_SOURCES}
    ${OBS_FXM_CACHE_SOURCES}
    ${OBS_FXM_CANVAS_SOURCES}
    ${OBS_FXM_EDITOR_MODULE_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_LEGACY_RENDERING_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_RENDERING_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_SOURCE_SOURCES}
    ${OBS_FXM_OBS_PLUGIN_AUDIO_SOURCES}
    Editor/standalone-obs-graphics-runtime.cpp
    Editor/standalone-editor-host.cpp
    Editor/standalone-welcome-screen.cpp
    Editor/project-panel.cpp
    Editor/project-loading-dialog.cpp
    Editor/startup-progress.cpp
    Editor/startup-progress-screen.cpp
    Editor/main.cpp
)
list(REMOVE_ITEM OBS_FXM_STANDALONE_EDITOR_SOURCES
    src/editor/title-dock.cpp)
set(OBS_FXM_STANDALONE_EDITOR_HEADERS ${OBS_FXM_EDITOR_MODULE_HEADERS})
list(REMOVE_ITEM OBS_FXM_STANDALONE_EDITOR_HEADERS
    src/editor/title-dock.h)

add_executable(flux-motion-editor WIN32
    ${OBS_FXM_STANDALONE_EDITOR_SOURCES}
    ${OBS_FXM_SHARED_HEADERS}
    ${OBS_FXM_CORE_HEADERS}
    ${OBS_FXM_LAYER_HEADERS}
    ${OBS_FXM_EXTENSION_HEADERS}
    ${OBS_FXM_EFFECT_HEADERS}
    ${OBS_FXM_TIMELINE_HEADERS}
    ${OBS_FXM_TRANSITION_HEADERS}
    ${OBS_FXM_TEXT_HEADERS}
    ${OBS_FXM_RENDERING_HEADERS}
    ${OBS_FXM_CACHE_HEADERS}
    ${OBS_FXM_CANVAS_HEADERS}
    ${OBS_FXM_STANDALONE_EDITOR_HEADERS}
    Editor/standalone-obs-graphics-runtime.h
    Editor/standalone-editor-host.h
    Editor/standalone-welcome-screen.h
    Editor/project-panel.h
    Editor/project-loading-dialog.h
    Editor/recent-projects.h
    Editor/startup-launch.h
    Editor/startup-progress.h
    Editor/startup-progress-screen.h
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/Editor/flux-motion-editor.rc>)
target_include_directories(flux-motion-editor PRIVATE
    ${FLUX_SUITE_COMMON_DIR}/Shared
    ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine
    Core src src/core src/layers src/effects src/timeline
    src/transitions src/text src/rendering src/cache src/canvas src/editor
    Editor ${OBS_FXM_OBS_PLUGIN_DIR} src/extensions
    ${CAIRO_INCLUDE_DIRS} ${PANGO_INCLUDE_DIRS} ${PANGOCAIRO_INCLUDE_DIRS})
target_link_libraries(flux-motion-editor PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    ${QT_LIBS}
    ${OBS_FXM_QT_MULTIMEDIA_TARGET}
    QtColorWidgets
    ${OBS_FXM_TEXT_RENDER_TARGETS}
    nlohmann_json::nlohmann_json
    ${OBS_FXM_LZ4_TARGET})
target_compile_definitions(flux-motion-editor PRIVATE
    FXM_CANVAS_WITH_OBS=1
    FXM_HAVE_QT_MULTIMEDIA=1
    PLUGIN_NAME="${OBS_TITLES_PLUGIN_NAME}"
    PLUGIN_VERSION="${OBS_FXM_VERSION}"
    FXM_VERSION_LABEL="${FLUX_MOTION_VERSION_LABEL}"
    FXM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FXM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FXM_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    FXM_DEVELOPMENT_VERSION="${OBS_FXM_DEVELOPMENT_VERSION}"
    FXM_STANDALONE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(MSVC)
    target_compile_options(flux-motion-editor PRIVATE /bigobj)
endif()
if(OBS_FXM_LZ4_TARGET)
    target_compile_definitions(flux-motion-editor PRIVATE OBS_FXM_HAVE_LZ4=1)
endif()
if(TARGET PkgConfig::FXM_FFMPEG)
    target_link_libraries(flux-motion-editor PRIVATE PkgConfig::FXM_FFMPEG)
    target_compile_definitions(flux-motion-editor PRIVATE FXM_HAVE_FFMPEG=1)
elseif(FXM_AVFORMAT_INCLUDE_DIR AND FXM_AVFORMAT_LIBRARY AND FXM_AVCODEC_LIBRARY
       AND FXM_AVUTIL_LIBRARY AND FXM_SWRESAMPLE_LIBRARY AND FXM_SWSCALE_LIBRARY)
    target_include_directories(flux-motion-editor PRIVATE ${FXM_AVFORMAT_INCLUDE_DIR})
    target_link_libraries(flux-motion-editor PRIVATE
        ${FXM_AVFORMAT_LIBRARY} ${FXM_AVCODEC_LIBRARY}
        ${FXM_AVUTIL_LIBRARY} ${FXM_SWRESAMPLE_LIBRARY} ${FXM_SWSCALE_LIBRARY})
    target_compile_definitions(flux-motion-editor PRIVATE FXM_HAVE_FFMPEG=1)
endif()
if(CAIRO_CFLAGS_OTHER)
    target_compile_options(flux-motion-editor PRIVATE ${CAIRO_CFLAGS_OTHER})
endif()
if(PANGO_CFLAGS_OTHER)
    target_compile_options(flux-motion-editor PRIVATE ${PANGO_CFLAGS_OTHER})
endif()
if(PANGOCAIRO_CFLAGS_OTHER)
    target_compile_options(flux-motion-editor PRIVATE ${PANGOCAIRO_CFLAGS_OTHER})
endif()
set_target_properties(flux-motion-editor PROPERTIES
    OUTPUT_NAME "Flux Motion"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/flux-motion-editor/bin")
foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(flux-motion-editor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper}
            "${CMAKE_BINARY_DIR}/flux-motion-editor/bin")
endforeach()

# The encoder renderer is deliberately a separate process and executable. It
# reuses the OBS-plugin compositor/audio implementation but never adds command
# line modes or export state to the interactive Flux Motion application.
set(OBS_FXM_STANDALONE_RENDERER_SOURCES ${OBS_FXM_STANDALONE_EDITOR_SOURCES})
list(REMOVE_ITEM OBS_FXM_STANDALONE_RENDERER_SOURCES Editor/main.cpp)
list(APPEND OBS_FXM_STANDALONE_RENDERER_SOURCES Renderer/main.cpp)
add_executable(flux-motion-renderer WIN32
    ${OBS_FXM_STANDALONE_RENDERER_SOURCES}
    ${OBS_FXM_SHARED_HEADERS}
    ${OBS_FXM_CORE_HEADERS}
    ${OBS_FXM_LAYER_HEADERS}
    ${OBS_FXM_EXTENSION_HEADERS}
    ${OBS_FXM_EFFECT_HEADERS}
    ${OBS_FXM_TIMELINE_HEADERS}
    ${OBS_FXM_TRANSITION_HEADERS}
    ${OBS_FXM_TEXT_HEADERS}
    ${OBS_FXM_RENDERING_HEADERS}
    ${OBS_FXM_CACHE_HEADERS}
    ${OBS_FXM_CANVAS_HEADERS}
    ${OBS_FXM_STANDALONE_EDITOR_HEADERS}
    Editor/standalone-obs-graphics-runtime.h
    Editor/standalone-editor-host.h)
target_include_directories(flux-motion-renderer PRIVATE
    ${FLUX_SUITE_COMMON_DIR}/Shared
    ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine
    Core src src/core src/layers src/effects src/timeline
    src/transitions src/text src/rendering src/cache src/canvas src/editor
    Editor ${OBS_FXM_OBS_PLUGIN_DIR} src/extensions
    ${CAIRO_INCLUDE_DIRS} ${PANGO_INCLUDE_DIRS} ${PANGOCAIRO_INCLUDE_DIRS})
target_link_libraries(flux-motion-renderer PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    ${QT_LIBS}
    ${OBS_FXM_QT_MULTIMEDIA_TARGET}
    QtColorWidgets
    ${OBS_FXM_TEXT_RENDER_TARGETS}
    nlohmann_json::nlohmann_json
    ${OBS_FXM_LZ4_TARGET})
target_compile_definitions(flux-motion-renderer PRIVATE
    FXM_CANVAS_WITH_OBS=1
    FXM_HAVE_QT_MULTIMEDIA=1
    PLUGIN_NAME="${OBS_TITLES_PLUGIN_NAME}"
    PLUGIN_VERSION="${OBS_FXM_VERSION}"
    FXM_VERSION_LABEL="${FLUX_MOTION_VERSION_LABEL}"
    FXM_DEVELOPMENT_VERSION="${OBS_FXM_DEVELOPMENT_VERSION}"
    FXM_STANDALONE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(MSVC)
    # The renderer deliberately reuses the full editor implementation and
    # therefore needs the same extended COFF section capacity as the editor.
    target_compile_options(flux-motion-renderer PRIVATE /bigobj)
endif()
if(OBS_FXM_LZ4_TARGET)
    target_compile_definitions(flux-motion-renderer PRIVATE OBS_FXM_HAVE_LZ4=1)
endif()
if(TARGET PkgConfig::FXM_FFMPEG)
    target_link_libraries(flux-motion-renderer PRIVATE PkgConfig::FXM_FFMPEG)
    target_compile_definitions(flux-motion-renderer PRIVATE FXM_HAVE_FFMPEG=1)
elseif(FXM_AVFORMAT_INCLUDE_DIR AND FXM_AVFORMAT_LIBRARY AND FXM_AVCODEC_LIBRARY
       AND FXM_AVUTIL_LIBRARY AND FXM_SWRESAMPLE_LIBRARY AND FXM_SWSCALE_LIBRARY)
    target_include_directories(flux-motion-renderer PRIVATE ${FXM_AVFORMAT_INCLUDE_DIR})
    target_link_libraries(flux-motion-renderer PRIVATE
        ${FXM_AVFORMAT_LIBRARY} ${FXM_AVCODEC_LIBRARY}
        ${FXM_AVUTIL_LIBRARY} ${FXM_SWRESAMPLE_LIBRARY} ${FXM_SWSCALE_LIBRARY})
    target_compile_definitions(flux-motion-renderer PRIVATE FXM_HAVE_FFMPEG=1)
endif()
if(CAIRO_CFLAGS_OTHER)
    target_compile_options(flux-motion-renderer PRIVATE ${CAIRO_CFLAGS_OTHER})
endif()
if(PANGO_CFLAGS_OTHER)
    target_compile_options(flux-motion-renderer PRIVATE ${PANGO_CFLAGS_OTHER})
endif()
if(PANGOCAIRO_CFLAGS_OTHER)
    target_compile_options(flux-motion-renderer PRIVATE ${PANGOCAIRO_CFLAGS_OTHER})
endif()
set_target_properties(flux-motion-renderer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/flux-motion-editor/bin")
foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(flux-motion-renderer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper}
            "${CMAKE_BINARY_DIR}/flux-motion-editor/bin")
endforeach()
add_dependencies(flux-motion-renderer flux-motion-editor)
if(WIN32)
    # CMake's TARGET_RUNTIME_DLLS sees obs.dll but not dependencies loaded by
    # that imported DLL nor the graphics backend passed to obs_reset_video().
    # Stage the small libobs runtime closure so the Editor also starts directly
    # (outside an OBS-launched process with OBS's bin directory on PATH).
    set(OBS_FXM_OBS_RUNTIME_SEARCH_DIRS
        "${OBS_SDK_DIR}/bin/64bit"
        "${OBS_SDK_DIR}/bin")
    if(DEFINED ENV{ProgramFiles})
        list(APPEND OBS_FXM_OBS_RUNTIME_SEARCH_DIRS
            "$ENV{ProgramFiles}/obs-studio/bin/64bit")
    endif()
    if(DEFINED ENV{ProgramW6432})
        list(APPEND OBS_FXM_OBS_RUNTIME_SEARCH_DIRS
            "$ENV{ProgramW6432}/obs-studio/bin/64bit")
    endif()
    set(OBS_FXM_STANDALONE_OBS_RUNTIME_DLLS)
    foreach(runtime_name zlib.dll w32-pthreads.dll libobs-d3d11.dll)
        unset(runtime_dll CACHE)
        unset(runtime_dll)
        find_file(runtime_dll NAMES "${runtime_name}"
            HINTS ${OBS_FXM_OBS_RUNTIME_SEARCH_DIRS}
            NO_DEFAULT_PATH)
        if(runtime_dll)
            list(APPEND OBS_FXM_STANDALONE_OBS_RUNTIME_DLLS "${runtime_dll}")
        endif()
    endforeach()
    find_path(OBS_FXM_LIBOBS_DATA_DIR NAMES default.effect
        HINTS
            "${OBS_SDK_DIR}/data/libobs"
            "$ENV{ProgramFiles}/obs-studio/data/libobs"
            "$ENV{ProgramW6432}/obs-studio/data/libobs"
        NO_DEFAULT_PATH)

    if(QT_VERSION_MAJOR EQUAL 6)
        set(OBS_FXM_QT_CMAKE_PACKAGE_DIR "${Qt6_DIR}")
    else()
        set(OBS_FXM_QT_CMAKE_PACKAGE_DIR "${Qt5_DIR}")
    endif()
    get_filename_component(OBS_FXM_QT_RUNTIME_PREFIX
        "${OBS_FXM_QT_CMAKE_PACKAGE_DIR}/../../.." ABSOLUTE)
    add_custom_command(TARGET flux-motion-editor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            $<TARGET_RUNTIME_DLLS:flux-motion-editor>
            $<TARGET_FILE_DIR:flux-motion-editor>
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:flux-motion-editor>/platforms"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${OBS_FXM_QT_RUNTIME_PREFIX}/plugins/platforms"
            "$<TARGET_FILE_DIR:flux-motion-editor>/platforms"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:flux-motion-editor>/multimedia"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${OBS_FXM_QT_RUNTIME_PREFIX}/plugins/multimedia"
            "$<TARGET_FILE_DIR:flux-motion-editor>/multimedia"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data"
            "$<TARGET_FILE_DIR:flux-motion-editor>/data"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${FLUX_SUITE_COMMON_DIR}/resources/fonts"
            "$<TARGET_FILE_DIR:flux-motion-editor>/data/fonts"
        COMMAND_EXPAND_LISTS
        COMMENT "Staging standalone Editor runtime dependencies")
    if(OBS_FXM_STANDALONE_OBS_RUNTIME_DLLS)
        add_custom_command(TARGET flux-motion-editor POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${OBS_FXM_STANDALONE_OBS_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:flux-motion-editor>
            COMMAND_EXPAND_LISTS
            COMMENT "Staging standalone libobs graphics runtime")
    endif()
    if(OBS_FXM_LIBOBS_DATA_DIR)
        add_custom_command(TARGET flux-motion-editor POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:flux-motion-editor>/data/libobs"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${OBS_FXM_LIBOBS_DATA_DIR}"
                "$<TARGET_FILE_DIR:flux-motion-editor>/data/libobs"
            COMMENT "Staging standalone libobs shader data")
    endif()
endif()

# ── OBS module helper (sets correct output name & install path) ──────
# This macro is provided by the OBS cmake helpers:
#   set_target_obs_bundle(flux-motion)
# If building standalone, stage artifacts in OBS' user-plugin folder layout.
set_target_properties(flux-motion PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${OBS_TITLES_PACKAGE_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${OBS_TITLES_PACKAGE_BIN_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
  string(TOUPPER "${config}" config_upper)
  set_target_properties(flux-motion PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${OBS_TITLES_PACKAGE_BIN_DIR}"
      RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${OBS_TITLES_PACKAGE_BIN_DIR}"
      ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib/${config}")
endforeach()

add_custom_command(TARGET flux-motion POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${OBS_TITLES_PACKAGE_DIR}/data"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data/locale"
            "${OBS_TITLES_PACKAGE_DIR}/data/locale"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data/icons"
            "${OBS_TITLES_PACKAGE_DIR}/data/icons"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${FLUX_SUITE_COMMON_DIR}/resources/fonts"
            "${OBS_TITLES_PACKAGE_DIR}/data/fonts"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${OBS_TITLES_PACKAGE_DIR}/data/effects"
            "${OBS_TITLES_PACKAGE_DIR}/data/effects-and-presets"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions"
            "${OBS_TITLES_PACKAGE_DIR}/data/effect-transitions"
    COMMENT "Staging OBS plugin data files")

if(WIN32)
  # OBS loads third-party DLL dependencies from the plugin binary directory.
  # Copy vcpkg-provided runtime DLLs into the staged folder so the plugin does
  # not compile successfully but fail LoadLibrary at OBS startup.
  set(OBS_TITLES_RUNTIME_DLL_DIRS)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND OBS_TITLES_RUNTIME_DLL_DIRS
         "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  endif()
  if(DEFINED _VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND OBS_TITLES_RUNTIME_DLL_DIRS
         "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  endif()
  if(DEFINED ENV{VCPKG_ROOT})
    if(DEFINED VCPKG_TARGET_TRIPLET)
      list(APPEND OBS_TITLES_RUNTIME_DLL_DIRS
           "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin")
    endif()
    list(APPEND OBS_TITLES_RUNTIME_DLL_DIRS
         "$ENV{VCPKG_ROOT}/installed/${OBS_TITLES_VCPKG_DEFAULT_TRIPLET}/bin")
  endif()

  set(OBS_TITLES_RUNTIME_DLLS)
  list(REMOVE_DUPLICATES OBS_TITLES_RUNTIME_DLL_DIRS)
  foreach(runtime_dir IN LISTS OBS_TITLES_RUNTIME_DLL_DIRS)
    if(IS_DIRECTORY "${runtime_dir}")
      file(GLOB runtime_dlls CONFIGURE_DEPENDS "${runtime_dir}/*.dll")
      list(APPEND OBS_TITLES_RUNTIME_DLLS ${runtime_dlls})
    endif()
  endforeach()

  if(OBS_TITLES_RUNTIME_DLLS)
    list(REMOVE_DUPLICATES OBS_TITLES_RUNTIME_DLLS)
    add_custom_command(TARGET flux-motion POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${OBS_TITLES_RUNTIME_DLLS}
                "$<TARGET_FILE_DIR:flux-motion>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying Windows runtime DLL dependencies")
    add_custom_command(TARGET flux-motion-obs-plugin POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${OBS_TITLES_RUNTIME_DLLS}
                "$<TARGET_FILE_DIR:flux-motion-obs-plugin>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying lightweight OBS plugin runtime DLL dependencies")
    install(FILES ${OBS_TITLES_RUNTIME_DLLS}
            DESTINATION ${OBS_TITLES_PLUGIN_NAME}/bin/${OBS_TITLES_ARCH_DIR})
  else()
    message(WARNING
      "No vcpkg runtime DLLs were found to bundle with flux-motion. "
      "If OBS reports that flux-motion failed to load, ensure Qt/Cairo/Pango "
      "runtime DLLs are next to the plugin binary or use build-windows.ps1.")
  endif()
endif()

# ── Install ──────────────────────────────────────────────────────────
# Install into the same user-plugin layout expected below the chosen prefix.
# Example Windows prefix: C:/ProgramData/obs-studio/plugins
# Example Linux prefix:   ~/.config/obs-studio/plugins
install(TARGETS flux-motion-obs-plugin
        RUNTIME DESTINATION ${OBS_TITLES_PLUGIN_NAME}/bin/${OBS_TITLES_ARCH_DIR}
        LIBRARY DESTINATION ${OBS_TITLES_PLUGIN_NAME}/bin/${OBS_TITLES_ARCH_DIR})
install(TARGETS flux-motion-editor
        RUNTIME DESTINATION flux-motion-editor/bin)

# ── Data files ──────────────────────────────────────────────────────
install(DIRECTORY data/locale
        DESTINATION ${OBS_TITLES_PLUGIN_NAME}/data)
install(DIRECTORY data/icons
        DESTINATION ${OBS_TITLES_PLUGIN_NAME}/data)
install(DIRECTORY "${FLUX_SUITE_COMMON_DIR}/resources/fonts/"
        DESTINATION ${OBS_TITLES_PLUGIN_NAME}/data/fonts)
install(DIRECTORY data/canned-templates
        DESTINATION ${OBS_TITLES_PLUGIN_NAME}/data)
install(DIRECTORY data/effect-transitions
        DESTINATION ${OBS_TITLES_PLUGIN_NAME}/data)

# ── Print summary ────────────────────────────────────────────────────
message(STATUS "Flux Motion: Qt${QT_VERSION_MAJOR}, Cairo ${CAIRO_VERSION}")

option(OBS_FXM_BUILD_TESTS "Build lightweight Flux Motion validation tests" OFF)
if(OBS_FXM_BUILD_TESTS)
  enable_testing()

  add_executable(motion_blur_sampling_test
      tests/motion_blur_sampling_test.cpp
      "${FLUX_SUITE_COMMON_DIR}/Shared/motion-blur-sampling.cpp")
  target_include_directories(motion_blur_sampling_test PRIVATE
      "${FLUX_SUITE_COMMON_DIR}/Shared")
  add_test(NAME motion_blur_sampling_test COMMAND motion_blur_sampling_test)

  add_executable(rendering_interfaces_contract_test
      tests/rendering_interfaces_contract_test.cpp)
  target_include_directories(rendering_interfaces_contract_test PRIVATE ${FLUX_SUITE_COMMON_DIR}/Shared)
  add_test(NAME rendering_interfaces_contract_test
      COMMAND rendering_interfaces_contract_test)

  add_executable(phase10_rendering_boundary_contract_test
      tests/phase10_rendering_boundary_contract_test.cpp)
  target_include_directories(phase10_rendering_boundary_contract_test
      PRIVATE tests)
  add_test(NAME phase10_rendering_boundary_contract_test
      COMMAND phase10_rendering_boundary_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-interfaces.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-render-backend.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-render-backend.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Core
      ${FLUX_SUITE_COMMON_DIR}/Shared
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor)

  add_executable(phase11_audio_boundary_contract_test
      tests/phase11_audio_boundary_contract_test.cpp)
  target_include_directories(phase11_audio_boundary_contract_test
      PRIVATE tests)
  add_test(NAME phase11_audio_boundary_contract_test
      COMMAND phase11_audio_boundary_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/audio-interfaces.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-audio-backend.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-audio-backend.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-audio-runtime.cpp)

  add_executable(phase12_editor_module_boundary_contract_test
      tests/phase12_editor_module_boundary_contract_test.cpp)
  target_include_directories(phase12_editor_module_boundary_contract_test
      PRIVATE tests)
  add_test(NAME phase12_editor_module_boundary_contract_test
      COMMAND phase12_editor_module_boundary_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/long-press-tool-button.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/open-color-palette.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/timecode-spinbox.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/timecode-spinbox.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/frame-rate-provider.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-frame-rate-provider.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/asset-path-provider.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-asset-path-provider.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/translation-provider.h
      ${FLUX_SUITE_COMMON_DIR}/Shared/title-localization.h
      ${FLUX_SUITE_COMMON_DIR}/Shared/title-localization.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-translation-provider.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/title-assets.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/title-assets.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/fxm-modern-controls.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/fxm-modern-controls.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-settings-dialog.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-settings-dialog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-binding-dialog.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-binding-dialog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-table-mapping-dialog.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-table-mapping-dialog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/asset-library.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/asset-library.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/frame-rate-provider.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/rich-text-model-adapters.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/dock-ui.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/template-library-helpers.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/window-session.inc
      ${OBS_FXM_OBS_PLUGIN_DIR}/plugin-main.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

  add_executable(phase12_editor_final_boundary_contract_test
      tests/phase12_editor_final_boundary_contract_test.cpp)
  target_include_directories(phase12_editor_final_boundary_contract_test
      PRIVATE tests)
  add_test(NAME phase12_editor_final_boundary_contract_test
      COMMAND phase12_editor_final_boundary_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/style-presets.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/tools-sidebar.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-properties-panel.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal.h)

  add_executable(phase13_obs_integration_boundary_contract_test
      tests/phase13_obs_integration_boundary_contract_test.cpp)
  target_include_directories(phase13_obs_integration_boundary_contract_test
      PRIVATE tests)
  add_test(NAME phase13_obs_integration_boundary_contract_test
      COMMAND phase13_obs_integration_boundary_contract_test
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-hotkeys.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-hotkeys.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/plugin-main.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/plugin-main.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/stinger-transition.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/stinger-transition.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-audio-runtime.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-audio-runtime.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.h
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-source
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/title-video-runtime.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/title-video-runtime.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/audio-output-scheduler.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/audio-transport-direction.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-editor-host.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor
      ${CMAKE_CURRENT_SOURCE_DIR}/src/obs)

  add_executable(editor_theme_boundary_contract_test
      tests/editor_theme_boundary_contract_test.cpp)
  target_include_directories(editor_theme_boundary_contract_test
      PRIVATE tests)
  add_test(NAME editor_theme_boundary_contract_test
      COMMAND editor_theme_boundary_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/editor-theme.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/editor-theme.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/main.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/signal-handlers.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/window-session.inc)

  add_executable(phase14_obsolete_dependencies_contract_test
      tests/phase14_obsolete_dependencies_contract_test.cpp)
  target_include_directories(phase14_obsolete_dependencies_contract_test
      PRIVATE tests)
  add_test(NAME phase14_obsolete_dependencies_contract_test
      COMMAND phase14_obsolete_dependencies_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/src/obs
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/fxm-modern-controls.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/fxm-modern-controls.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/widget-property-helpers.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-properties-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/title-animation-edit-utils.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/tools-sidebar.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/editor-tool-icons.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/editor-tool-icons.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/hold-menu-tool-button.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/text-layout-rendering.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/style-preset-runtime.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/style-presets.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal.h
      ${FLUX_SUITE_COMMON_DIR}/Shared/title-preview-renderer.h
      ${FLUX_SUITE_COMMON_DIR}/Shared/title-preview-renderer.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-title-preview-renderer.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/plugin-main.cpp)

  add_executable(phase14_shared_sdk_contract_test
      tests/phase14_shared_sdk_contract_test.cpp)
  target_include_directories(phase14_shared_sdk_contract_test PRIVATE
      Core ${FLUX_SUITE_COMMON_DIR}/Shared)
  add_test(NAME phase14_shared_sdk_contract_test
      COMMAND phase14_shared_sdk_contract_test)

  add_executable(phase14_core_independence_contract_test
      tests/phase14_core_independence_contract_test.cpp)
  target_include_directories(phase14_core_independence_contract_test
      PRIVATE tests)
  add_test(NAME phase14_core_independence_contract_test
      COMMAND phase14_core_independence_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-logger.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/host-context-provider.h
      ${FLUX_SUITE_COMMON_DIR}/Shared/logger.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-host-context-provider.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/obs-logger.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/visual-hash-keying.inc)

  add_executable(timecode_spinbox_contract_test
      tests/timecode_spinbox_contract_test.cpp
      Editor/timecode-spinbox.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/frame-rate-provider.cpp)
  target_include_directories(timecode_spinbox_contract_test PRIVATE
      Editor ${FLUX_SUITE_COMMON_DIR}/Shared)
  target_link_libraries(timecode_spinbox_contract_test PRIVATE
      Qt${QT_VERSION_MAJOR}::Widgets)
  if(WIN32)
    get_filename_component(OBS_FXM_QT_RUNTIME_ROOT
        "${Qt${QT_VERSION_MAJOR}_DIR}/../../.." ABSOLUTE)
    add_test(NAME timecode_spinbox_contract_test
        COMMAND ${CMAKE_COMMAND} -E env
        "PATH=${OBS_FXM_QT_RUNTIME_ROOT}/bin;$ENV{PATH}"
        $<TARGET_FILE:timecode_spinbox_contract_test>)
  else()
    add_test(NAME timecode_spinbox_contract_test
        COMMAND timecode_spinbox_contract_test)
  endif()

  add_executable(translation_provider_contract_test
      tests/translation_provider_contract_test.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/title-localization.cpp)
  target_include_directories(translation_provider_contract_test PRIVATE
      ${FLUX_SUITE_COMMON_DIR}/Shared)
  target_link_libraries(translation_provider_contract_test PRIVATE
      Qt${QT_VERSION_MAJOR}::Core)
  if(WIN32)
    add_test(NAME translation_provider_contract_test
        COMMAND ${CMAKE_COMMAND} -E env
        "PATH=${OBS_FXM_QT_RUNTIME_ROOT}/bin;$ENV{PATH}"
        $<TARGET_FILE:translation_provider_contract_test>)
  else()
    add_test(NAME translation_provider_contract_test
        COMMAND translation_provider_contract_test)
  endif()

  add_executable(frame_rate_provider_contract_test
      tests/frame_rate_provider_contract_test.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/frame-rate-provider.cpp)
  target_include_directories(frame_rate_provider_contract_test PRIVATE
      ${FLUX_SUITE_COMMON_DIR}/Shared)
  add_test(NAME frame_rate_provider_contract_test
      COMMAND frame_rate_provider_contract_test)

  add_executable(packed_title_format_test
      tests/packed_title_format_test.cpp
      Core/packed-title-format.cpp)
  target_include_directories(packed_title_format_test PRIVATE Core)
  target_link_libraries(packed_title_format_test PRIVATE
      Qt${QT_VERSION_MAJOR}::Core ${OBS_FXM_LZ4_TARGET})
  add_test(NAME packed_title_format_test COMMAND packed_title_format_test)

  add_executable(ram_frame_cache_membership_contract_test
      tests/ram_frame_cache_membership_contract_test.cpp)
  add_test(NAME ram_frame_cache_membership_contract_test
      COMMAND ram_frame_cache_membership_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/ram-frame-cache.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.cpp)

  add_executable(rich_text_model_test
      tests/rich_text_model_test.cpp
      src/text/title-rich-text.cpp)
  target_include_directories(rich_text_model_test PRIVATE Core ${FLUX_SUITE_COMMON_DIR}/Shared src src/core src/layers src/effects src/timeline src/text)
  target_compile_definitions(rich_text_model_test PRIVATE OBS_FXM_RICH_TEXT_STANDALONE_TEST)
  add_test(NAME rich_text_model_test COMMAND rich_text_model_test)

  add_executable(text_layout_contract_test
      tests/text_layout_contract_test.cpp
      src/text/title-rich-text.cpp
      src/text/title-text-layout.cpp)
  target_include_directories(text_layout_contract_test PRIVATE Core ${FLUX_SUITE_COMMON_DIR}/Shared src src/core src/layers src/effects src/timeline src/text)
  target_compile_definitions(text_layout_contract_test PRIVATE OBS_FXM_RICH_TEXT_STANDALONE_TEST)
  add_test(NAME text_layout_contract_test COMMAND text_layout_contract_test)

  add_executable(text_pipeline_performance_contract_test
      tests/text_pipeline_performance_contract_test.cpp)
  target_include_directories(text_pipeline_performance_contract_test PRIVATE tests)
  add_test(NAME text_pipeline_performance_contract_test
      COMMAND text_pipeline_performance_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/text/title-text-layout-qt.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/title-gpu-text-renderer.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview-inline-text.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/preview-cache-view.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/document-shape-editing.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/layer-evaluation-layout.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/rich-text-model-adapters.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/text/title-rich-text.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/rich-text-color-utils.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/compatibility-text-rendering.inc)

  add_executable(animation_model_test
      tests/animation_model_test.cpp
      Core/animation.cpp)
  target_include_directories(animation_model_test PRIVATE Core ${FLUX_SUITE_COMMON_DIR}/Shared src/timeline src/core)
  add_test(NAME animation_model_test COMMAND animation_model_test)

  add_executable(spatial_motion_path_3d_test
      tests/spatial_motion_path_3d_test.cpp
      Core/animation.cpp)
  target_include_directories(spatial_motion_path_3d_test PRIVATE Core ${FLUX_SUITE_COMMON_DIR}/Shared src src/timeline src/core)
  add_test(NAME spatial_motion_path_3d_test COMMAND spatial_motion_path_3d_test)

  add_executable(layer_3d_transform_contract_test
      tests/layer_3d_transform_contract_test.cpp)
  target_include_directories(layer_3d_transform_contract_test PRIVATE tests)
  add_test(NAME layer_3d_transform_contract_test
      COMMAND layer_3d_transform_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/layer-model.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/title-data.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/layer-transform-3d.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/layer-transform-3d.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/source-runtime.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/live-cue-state.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel/popup-state.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-properties-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/hierarchy-model.inc)

  add_executable(text_animator_system_test
      tests/text_animator_system_test.cpp
      Core/animation.cpp
      src/text/text-animator.cpp
      src/text/text-animator-presets.cpp)
  target_include_directories(text_animator_system_test PRIVATE
      Core ${FLUX_SUITE_COMMON_DIR}/Shared src src/timeline src/text src/transitions)
  target_compile_options(text_animator_system_test PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
  add_test(NAME text_animator_system_test COMMAND text_animator_system_test)

  add_executable(text_animator_preset_io_test
      tests/text_animator_preset_io_test.cpp
      Core/animation.cpp
      src/text/text-animator.cpp
      src/text/text-animator-preset-io.cpp)
  target_include_directories(text_animator_preset_io_test PRIVATE
      Core ${FLUX_SUITE_COMMON_DIR}/Shared src src/timeline src/text)
  target_link_libraries(text_animator_preset_io_test PRIVATE
      nlohmann_json::nlohmann_json)
  add_test(NAME text_animator_preset_io_test COMMAND text_animator_preset_io_test)

  add_executable(text_animator_timeline_contract_test
      tests/text_animator_timeline_contract_test.cpp)
  add_test(NAME text_animator_timeline_contract_test
      COMMAND text_animator_timeline_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/hierarchy-model.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.cpp)

  add_executable(text_animator_integration_contract_test
      tests/text_animator_integration_contract_test.cpp)
  add_test(NAME text_animator_integration_contract_test
      COMMAND text_animator_integration_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/compatibility-text-rendering.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/editor-events.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/source-lifecycle-playback.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/title-gpu-text-renderer.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/transitions/transition-editor-dialog.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/compatibility-layer-raster.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/layer-evaluation-layout.inc)

  add_executable(text_animator_build_integration_contract_test
      tests/text_animator_build_integration_contract_test.cpp)
  add_test(NAME text_animator_build_integration_contract_test
      COMMAND text_animator_build_integration_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel/text-animator-controls.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc)

  add_executable(spatial_bezier_keyframes_contract_test
      tests/spatial_bezier_keyframes_contract_test.cpp)
  target_include_directories(spatial_bezier_keyframes_contract_test PRIVATE tests)
  add_test(NAME spatial_bezier_keyframes_contract_test
      COMMAND spatial_bezier_keyframes_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/animation.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/animation.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/hierarchy-model.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/spatial-bezier-keyframes.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/canvas-overlay-paint.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/gpu-frame-rendering.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/disk-cache-storage.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/cache-policy-invalidation.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/commands-docks.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/window-session.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/worker-publication.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/docs/CHANGELOG.md)

  add_executable(temporal_graph_editor_contract_test
      tests/temporal_graph_editor_contract_test.cpp)
  target_include_directories(temporal_graph_editor_contract_test PRIVATE tests)
  add_test(NAME temporal_graph_editor_contract_test
      COMMAND temporal_graph_editor_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/animation.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/animation.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/hierarchy-model.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/temporal-graph-editor.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/commands-docks.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effect-animation-utils.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/disk-cache-storage.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/window-session.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/worker-publication.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/docs/CHANGELOG.md)

  add_executable(editor_ui_revision_140_contract_test
      tests/editor_ui_revision_140_contract_test.cpp)
  target_include_directories(editor_ui_revision_140_contract_test PRIVATE tests)
  add_test(NAME editor_ui_revision_140_contract_test
      COMMAND editor_ui_revision_140_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/commands-docks.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/temporal-graph-editor.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/text-layout-rendering.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/distribute-horizontal.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/distribute-vertical.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/flip-horizontal.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/flip-vertical.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/matte-alpha.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/matte-clipping.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/graph.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/matte-inverted.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/matte-luma.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/matte-normal.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/build-info.h)

  add_executable(editor_timeline_revision_141_contract_test
      tests/editor_timeline_revision_141_contract_test.cpp)
  target_include_directories(editor_timeline_revision_141_contract_test PRIVATE tests)
  add_test(NAME editor_timeline_revision_141_contract_test
      COMMAND editor_timeline_revision_141_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/editor-events.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/visibility-normal.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/no-visibility.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/visibility-matte.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/layer-lock.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/layer-unlock.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/build-info.h)

  add_executable(editor_keyframe_icon_revision_142_contract_test
      tests/editor_keyframe_icon_revision_142_contract_test.cpp)
  target_include_directories(editor_keyframe_icon_revision_142_contract_test PRIVATE tests)
  add_test(NAME editor_keyframe_icon_revision_142_contract_test
      COMMAND editor_keyframe_icon_revision_142_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/hierarchy-model.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/layer-lock.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/layer-unlock.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/no-visibility.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/visibility-matte.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/matte-source.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/matte-destination.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/build-info.h)

  add_executable(editor_keyframe_graph_matte_revision_143_contract_test
      tests/editor_keyframe_graph_matte_revision_143_contract_test.cpp)
  target_include_directories(editor_keyframe_graph_matte_revision_143_contract_test PRIVATE tests)
  add_test(NAME editor_keyframe_graph_matte_revision_143_contract_test
      COMMAND editor_keyframe_graph_matte_revision_143_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/temporal-graph-editor.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/commands-docks.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/editor-events.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/graph.svg
      ${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/build-info.h)

  add_executable(editor_character_version_revision_144_contract_test
      tests/editor_character_version_revision_144_contract_test.cpp)
  target_include_directories(editor_character_version_revision_144_contract_test PRIVATE tests)
  add_test(NAME editor_character_version_revision_144_contract_test
      COMMAND editor_character_version_revision_144_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel/construction-gradient-image-signals.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/build-info.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/plugin-main.h
      ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json
      ${CMAKE_CURRENT_SOURCE_DIR}/README.md
      ${CMAKE_CURRENT_SOURCE_DIR}/docs/README.md
      ${CMAKE_CURRENT_SOURCE_DIR}/docs/CHANGELOG.md)

  add_executable(transition_model_test
      tests/transition_model_test.cpp)
  target_include_directories(transition_model_test PRIVATE Core src/transitions src/timeline)
  add_test(NAME transition_model_test COMMAND transition_model_test)

  add_executable(cache_time_contract_test
      tests/cache_time_contract_test.cpp)
  target_include_directories(cache_time_contract_test PRIVATE src/cache)
  add_test(NAME cache_time_contract_test COMMAND cache_time_contract_test)

  add_executable(external_data_core_contract_test
      tests/external_data_core_contract_test.cpp)
  target_include_directories(external_data_core_contract_test PRIVATE tests)
  add_test(NAME external_data_core_contract_test
      COMMAND external_data_core_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data-types.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/layer-model.h
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/title-data.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/window-session.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

  add_executable(external_data_providers_contract_test
      tests/external_data_providers_contract_test.cpp)
  target_include_directories(external_data_providers_contract_test PRIVATE tests)
  add_test(NAME external_data_providers_contract_test
      COMMAND external_data_providers_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data-types.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data-provider.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data-provider.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/external-json-path.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-settings-dialog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/live-text-cache-playlist.inc
      ${OBS_FXM_OBS_PLUGIN_DIR}/plugin-main.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

  add_executable(external_data_ui_contract_test
      tests/external_data_ui_contract_test.cpp)
  target_include_directories(external_data_ui_contract_test PRIVATE tests)
  add_test(NAME external_data_ui_contract_test
      COMMAND external_data_ui_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data-types.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-settings-dialog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-binding-dialog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-table-mapping-dialog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/live-text-cache-playlist.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/list-selection-cues.inc
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-hotkeys.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

  add_executable(live_text_table_mapping_contract_test
      tests/live_text_table_mapping_contract_test.cpp)
  target_include_directories(live_text_table_mapping_contract_test PRIVATE tests)
  add_test(NAME live_text_table_mapping_contract_test
      COMMAND live_text_table_mapping_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data-types.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data-provider.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/external-data-table-mapping-dialog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/dock-lifecycle.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/live-text-cache-playlist.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

  add_executable(external_data_runtime_test
      tests/external_data_runtime_test.cpp
      src/core/external-data.cpp
      src/core/external-data-log.cpp)
  target_include_directories(external_data_runtime_test PRIVATE
      Core ${FLUX_SUITE_COMMON_DIR}/Shared src src/core src/layers src/effects src/timeline src/text src/transitions)
  add_test(NAME external_data_runtime_test COMMAND external_data_runtime_test)

  add_executable(chart_data_runtime_test
      tests/chart_data_runtime_test.cpp
      Core/animation.cpp
      src/core/chart-data.cpp
      src/core/external-data.cpp
      src/core/external-data-log.cpp)
  target_include_directories(chart_data_runtime_test PRIVATE
      Core ${FLUX_SUITE_COMMON_DIR}/Shared src src/core src/layers src/effects src/timeline src/text src/transitions)
  add_test(NAME chart_data_runtime_test COMMAND chart_data_runtime_test)
  set_tests_properties(chart_data_runtime_test PROPERTIES
      LABELS "unit;chart;external-data")

  add_executable(external_data_logging_test
      tests/external_data_logging_test.cpp
      src/core/external-data-log.cpp)
  target_include_directories(external_data_logging_test PRIVATE src src/core)
  add_test(NAME external_data_logging_test COMMAND external_data_logging_test)

  add_executable(external_data_live_cue_transition_contract_test
      tests/external_data_live_cue_transition_contract_test.cpp)
  target_include_directories(external_data_live_cue_transition_contract_test PRIVATE tests)
  add_test(NAME external_data_live_cue_transition_contract_test
      COMMAND external_data_live_cue_transition_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/source-runtime.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/gpu-effects-transitions.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/list-selection-cues.inc)

  add_executable(ticker_runtime_contract_test
      tests/ticker_runtime_contract_test.cpp
      src/core/ticker-runtime.cpp)
  target_include_directories(ticker_runtime_contract_test PRIVATE
      Core src src/core src/layers src/effects src/timeline src/text src/transitions)
  add_test(NAME ticker_runtime_contract_test COMMAND ticker_runtime_contract_test)

  add_executable(asset_runtime_contract_test
      tests/asset_runtime_contract_test.cpp
      Core/asset-runtime.cpp
      Core/linked-asset.cpp
      Core/animation.cpp)
  target_include_directories(asset_runtime_contract_test PRIVATE
      Core ${FLUX_SUITE_COMMON_DIR}/Shared src src/core src/layers src/effects src/timeline src/text src/transitions)
  add_test(NAME asset_runtime_contract_test COMMAND asset_runtime_contract_test)

  add_executable(ticker_controls_contract_test
      tests/ticker_controls_contract_test.cpp)
  add_test(NAME ticker_controls_contract_test
      COMMAND ticker_controls_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/layer-model.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-hotkeys.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini)

  add_executable(uncue_playback_snapshot_contract_test
      tests/uncue_playback_snapshot_contract_test.cpp)
  add_test(NAME uncue_playback_snapshot_contract_test
      COMMAND uncue_playback_snapshot_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/gpu-effects-transitions.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/list-selection-cues.inc
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-hotkeys.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/live-cue-queueing.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/title-data.h)

  add_executable(cue_ending_visual_contract_test
      tests/cue_ending_visual_contract_test.cpp)
  add_test(NAME cue_ending_visual_contract_test
      COMMAND cue_ending_visual_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/template-library-helpers.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/dock-lifecycle.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/live-text-cache-playlist.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/gpu-effects-transitions.inc)

  add_executable(live_cue_header_timer_contract_test
      tests/live_cue_header_timer_contract_test.cpp)
  add_test(NAME live_cue_header_timer_contract_test
      COMMAND live_cue_header_timer_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/title-data.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/source-runtime.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/gpu-effects-transitions.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/dock-lifecycle.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/live-text-cache-playlist.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

  add_executable(title_thumbnail_cue_state_contract_test
      tests/title_thumbnail_cue_state_contract_test.cpp)
  add_test(NAME title_thumbnail_cue_state_contract_test
      COMMAND title_thumbnail_cue_state_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/template-library-helpers.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/import-export-helpers.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/dock-lifecycle.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/list-selection-cues.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/title-actions.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

  add_executable(adjustment_effects_transitions_contract_test
      tests/adjustment_effects_transitions_contract_test.cpp)
  add_test(NAME adjustment_effects_transitions_contract_test
      COMMAND adjustment_effects_transitions_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/layer-model.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/layer-effects.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/transitions/layer-transition.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effects-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/transitions/transition-editor-dialog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effects-presets-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/title-effect-registry.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp)

  add_executable(effects_cleanup_four_color_gradient_contract_test
      tests/effects_cleanup_four_color_gradient_contract_test.cpp)
  add_test(NAME effects_cleanup_four_color_gradient_contract_test
      COMMAND effects_cleanup_four_color_gradient_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/layer-effects.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/title-effect-registry.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/extensions/effect-extension-catalog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effects-panel.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/gpu-presentation-readback.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/background-color/background-color.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/emboss/emboss.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/4-color-gradient/4-color-gradient.effect
      "${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/4-Color Gradient.fxmeffect"
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/asset-runtime.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/source-runtime.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effect-preset-catalog.cpp)

  add_executable(effects_stack_layer_space_contract_test
      tests/effects_stack_layer_space_contract_test.cpp)
  add_test(NAME effects_stack_layer_space_contract_test
      COMMAND effects_stack_layer_space_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/gpu-presentation-readback.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/source-lifecycle-playback.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/background-color/background-color.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/4-color-gradient/4-color-gradient.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/lens-flare/lens-flare.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/vignette/vignette.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/noise/noise.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/roughen-edges/roughen-edges.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/detail/detail.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/glare/glare.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/halation/halation.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/finishing/finishing.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/keying/keying.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/source-effects/source-effects.effect)

  add_executable(effect_keyframe_timeline_canvas_contract_test
      tests/effect_keyframe_timeline_canvas_contract_test.cpp)
  add_test(NAME effect_keyframe_timeline_canvas_contract_test
      COMMAND effect_keyframe_timeline_canvas_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effects-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effect-animation-utils.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal/hierarchy-model.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/canvas-overlay-paint.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/gpu-frame-rendering.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/keyboard-wheel-events.inc)

  add_executable(shutdown_performance_regression_contract_test
      tests/shutdown_performance_regression_contract_test.cpp)
  add_test(NAME shutdown_performance_regression_contract_test
      COMMAND shutdown_performance_regression_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effects-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effects-panel.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/preview-cache-view.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/keyboard-wheel-events.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/window-session.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/playback-cache-preferences.inc
      ${OBS_FXM_OBS_PLUGIN_DIR}/plugin-main.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/title-data.h)

  add_executable(canvas_refresh_pacing_contract_test
      tests/canvas_refresh_pacing_contract_test.cpp)
  add_test(NAME canvas_refresh_pacing_contract_test
      COMMAND canvas_refresh_pacing_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/preview-cache-view.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/keyboard-wheel-events.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/window-session.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/editor-events.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/signal-handlers.inc)

  add_executable(effects_interaction_regression_contract_test
      tests/effects_interaction_regression_contract_test.cpp)
  add_test(NAME effects_interaction_regression_contract_test
      COMMAND effects_interaction_regression_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effects-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/window-session.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/path-gradient-tools.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/fxm-modern-controls.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp)

  add_executable(effect_preview_shortcuts_contract_test
      tests/effect_preview_shortcuts_contract_test.cpp)
  add_test(NAME effect_preview_shortcuts_contract_test
      COMMAND effect_preview_shortcuts_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/transitions/transition-editor-dialog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/lens-flare/lens-flare.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/vignette/vignette.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/noise/noise.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/roughen-edges/roughen-edges.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/detail/detail.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/glare/glare.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/halation/halation.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/finishing/finishing.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/keying/keying.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/source-effects/source-effects.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.cpp)

  add_executable(procedural_effect_runtime_contract_test
      tests/procedural_effect_runtime_contract_test.cpp)
  add_test(NAME procedural_effect_runtime_contract_test
      COMMAND procedural_effect_runtime_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/title-effect-registry.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/lens-flare/lens-flare.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/vignette/vignette.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/noise/noise.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/roughen-edges/roughen-edges.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/detail/detail.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/glare/glare.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/halation/halation.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/finishing/finishing.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/keying/keying.effect
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/source-effects/source-effects.effect)

  add_executable(lens_flare_runtime_contract_test
      tests/lens_flare_runtime_contract_test.cpp)
  add_test(NAME lens_flare_runtime_contract_test
      COMMAND lens_flare_runtime_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/title-effect-registry.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/data/effect-transitions/shaders/lens-flare/lens-flare.effect)

  add_executable(system_memory_contract_test
      tests/system_memory_contract_test.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/system-memory.cpp)
  target_include_directories(system_memory_contract_test PRIVATE ${FLUX_SUITE_COMMON_DIR}/Shared)
  add_test(NAME system_memory_contract_test COMMAND system_memory_contract_test)

  add_executable(cache_frame_payload_test
      tests/cache_frame_payload_test.cpp)
  target_include_directories(cache_frame_payload_test PRIVATE src/cache)
  target_link_libraries(cache_frame_payload_test PRIVATE
      Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets)
  add_test(NAME cache_frame_payload_test COMMAND cache_frame_payload_test)

  add_executable(cache_tile_payload_test
      tests/cache_tile_payload_test.cpp)
  target_include_directories(cache_tile_payload_test PRIVATE src/cache)
  target_link_libraries(cache_tile_payload_test PRIVATE
      Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets)
  add_test(NAME cache_tile_payload_test COMMAND cache_tile_payload_test)

  add_executable(disk_cache_tiling_contract_test
      tests/disk_cache_tiling_contract_test.cpp)
  add_test(NAME disk_cache_tiling_contract_test
      COMMAND disk_cache_tiling_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-tile-payload.h)

  add_executable(live_text_cue_utils_test
      tests/live_text_cue_utils_test.cpp)
  target_include_directories(live_text_cue_utils_test PRIVATE
      Core src/core src/layers src/effects src/timeline src/text src/transitions)
  add_test(NAME live_text_cue_utils_test COMMAND live_text_cue_utils_test)

  add_executable(title_snapshot_test
      tests/title_snapshot_test.cpp)
  target_include_directories(title_snapshot_test PRIVATE
      Core src/core src/layers src/effects src/timeline src/text src/transitions)
  add_test(NAME title_snapshot_test COMMAND title_snapshot_test)

  add_executable(gpu_rounded_image_clip_test
      tests/gpu_rounded_image_clip_test.cpp)
  add_test(NAME gpu_rounded_image_clip_test COMMAND gpu_rounded_image_clip_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp)

  add_executable(gpu_mask_contract_test
      tests/gpu_mask_contract_test.cpp)
  add_test(NAME gpu_mask_contract_test COMMAND gpu_mask_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp)

  add_executable(gpu_ram_cache_tiling_contract_test
      tests/gpu_ram_cache_tiling_contract_test.cpp)
  add_test(NAME gpu_ram_cache_tiling_contract_test
      COMMAND gpu_ram_cache_tiling_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.h)

  add_executable(phase15_visibility_recovery_contract_test
      tests/phase15_visibility_recovery_contract_test.cpp)
  add_test(NAME phase15_visibility_recovery_contract_test
      COMMAND phase15_visibility_recovery_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/title-gpu-text-renderer.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-preferences.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

  add_executable(development_audit_contract_test
      tests/development_audit_contract_test.cpp)
  add_test(NAME development_audit_contract_test
      COMMAND development_audit_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/build-info.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/extensions/effect-extension-catalog.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/title-effect-registry.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/plugin-main.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/README.md
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/layer-effects.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/effects-panel.cpp)

  add_executable(group_container_contract_test
      tests/group_container_contract_test.cpp)
  add_test(NAME group_container_contract_test
      COMMAND group_container_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/layer-model.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp)

  add_executable(group_alt_drag_duplicate_contract_test
      tests/group_alt_drag_duplicate_contract_test.cpp)
  add_test(NAME group_alt_drag_duplicate_contract_test
      COMMAND group_alt_drag_duplicate_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp)

  add_executable(parenting_matte_expansion_contract_test
      tests/parenting_matte_expansion_contract_test.cpp)
  add_test(NAME parenting_matte_expansion_contract_test
      COMMAND parenting_matte_expansion_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/layer-model.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal.h
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

  add_executable(grouped_child_resize_canvas_menu_contract_test
      tests/grouped_child_resize_canvas_menu_contract_test.cpp)
  add_test(NAME grouped_child_resize_canvas_menu_contract_test
      COMMAND grouped_child_resize_canvas_menu_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor.cpp)

  add_executable(canvas_scaling_manipulation_contract_test
      tests/canvas_scaling_manipulation_contract_test.cpp)
  add_test(NAME canvas_scaling_manipulation_contract_test
      COMMAND canvas_scaling_manipulation_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp)

  add_executable(group_hierarchy_timeline_contract_test
      tests/group_hierarchy_timeline_contract_test.cpp)
  add_test(NAME group_hierarchy_timeline_contract_test
      COMMAND group_hierarchy_timeline_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/timeline/timeline-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini)

  add_executable(layer_color_visual_contract_test
      tests/layer_color_visual_contract_test.cpp)
  add_test(NAME layer_color_visual_contract_test
      COMMAND layer_color_visual_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-preferences.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-preferences.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor-internal.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp)

  add_executable(gpu_prerender_phase14_contract_test
      tests/gpu_prerender_phase14_contract_test.cpp)
  add_test(NAME gpu_prerender_phase14_contract_test COMMAND gpu_prerender_phase14_contract_test
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp)

  add_executable(asset_layer_contract_test
      tests/asset_layer_contract_test.cpp)
  add_test(NAME asset_layer_contract_test
      COMMAND asset_layer_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/layer-model.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Editor/asset-library.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/layers/layer-stack-widget.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/asset-runtime.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

  add_executable(gradient_preset_unification_contract_test
      tests/gradient_preset_unification_contract_test.cpp)
  add_test(NAME gradient_preset_unification_contract_test
      COMMAND gradient_preset_unification_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/style-presets.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/style-presets.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel/construction-type-live-shape.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel/color-gradient-editing.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/ui-construction.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini)

  add_executable(serialization_passthrough_hot_path_test
      tests/serialization_passthrough_hot_path_test.cpp)
  target_include_directories(serialization_passthrough_hot_path_test PRIVATE Core)
  add_test(NAME serialization_passthrough_hot_path_test
      COMMAND serialization_passthrough_hot_path_test)
  set_tests_properties(serialization_passthrough_hot_path_test PROPERTIES
      LABELS "unit;serialization;performance;render-hot-path")

  add_executable(serialization_migration_roundtrip_test
      tests/serialization_migration_roundtrip_test.cpp)
  target_include_directories(serialization_migration_roundtrip_test PRIVATE
      Core)
  target_link_libraries(serialization_migration_roundtrip_test PRIVATE
      nlohmann_json::nlohmann_json)
  add_test(NAME serialization_migration_roundtrip_test
      COMMAND serialization_migration_roundtrip_test)

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME python_contract_regression_suite
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_python_contract_tests.py)
    add_test(NAME automated_test_manifest_validation
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_automated_test_suite.py --validate-only)
    set_tests_properties(python_contract_regression_suite automated_test_manifest_validation PROPERTIES
        LABELS "source;regression")
  endif()

  add_executable(external_json_path_test
      tests/external_json_path_test.cpp)
  target_include_directories(external_json_path_test PRIVATE ${FLUX_SUITE_COMMON_DIR}/Shared)
  add_test(NAME external_json_path_test COMMAND external_json_path_test)

  add_executable(audio_output_scheduler_test
      tests/audio_output_scheduler_test.cpp)
  target_include_directories(audio_output_scheduler_test PRIVATE Core)
  add_test(NAME audio_output_scheduler_test COMMAND audio_output_scheduler_test)

  add_executable(audio_interfaces_contract_test
      tests/audio_interfaces_contract_test.cpp)
  target_include_directories(audio_interfaces_contract_test PRIVATE ${FLUX_SUITE_COMMON_DIR}/Shared)
  add_test(NAME audio_interfaces_contract_test
      COMMAND audio_interfaces_contract_test)

  add_executable(audio_reverse_transport_test
      tests/audio_reverse_transport_test.cpp)
  target_include_directories(audio_reverse_transport_test PRIVATE Core)
  add_test(NAME audio_reverse_transport_test COMMAND audio_reverse_transport_test)

  add_executable(pattern_resource_cache_test
      tests/pattern_resource_cache_test.cpp)
  target_include_directories(pattern_resource_cache_test PRIVATE
      ${FLUX_SUITE_COMMON_DIR}/Shared src/text src/core)
  add_test(NAME pattern_resource_cache_test COMMAND pattern_resource_cache_test)

  add_executable(performance_cache_threading_contract_test
      tests/performance_cache_threading_contract_test.cpp)
  target_include_directories(performance_cache_threading_contract_test PRIVATE tests)
  add_test(NAME performance_cache_threading_contract_test
      COMMAND performance_cache_threading_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data-provider.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/external-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/text/pattern-resource-cache.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/text/title-rich-text.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-audio-runtime.h
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-audio-runtime.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/animation.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/spatial-bezier-keyframes.inc
      # Contract argv[10] is content-hash (implemented in disk-cache-storage.inc);
      # argv[11] is disk-writer (implemented in visual-hash-keying.inc).
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/disk-cache-storage.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/visual-hash-keying.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/live-cue-state.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/worker-publication.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/render-queue-manager.cpp
      ${OBS_FXM_OBS_PLUGIN_DIR}/plugin-main.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-editor/window-session.inc
      ${OBS_FXM_OBS_PLUGIN_DIR}/stinger-transition.cpp
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/source-registration.inc
      ${FLUX_SUITE_COMMON_DIR}/Shared/rendering-engine/title-source/gpu-effects-transitions.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/cache-policy-invalidation.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/title-serialization-schema.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/canvas-preview/keyboard-wheel-events.inc)

  add_executable(unified_serialization_contract_test
      tests/unified_serialization_contract_test.cpp)
  target_include_directories(unified_serialization_contract_test PRIVATE tests)
  add_test(NAME unified_serialization_contract_test
      COMMAND unified_serialization_contract_test
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/title-data.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/Core/title-serialization-schema.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/style-presets.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cache/cache-manager/disk-cache-storage.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/dock-lifecycle.inc
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/title-dock/collapsible-titles.inc
      ${OBS_FXM_OBS_PLUGIN_DIR}/title-audio-runtime.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/properties-panel/property-synchronization.inc)
endif()
