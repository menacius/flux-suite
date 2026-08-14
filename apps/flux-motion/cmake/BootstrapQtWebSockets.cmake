include_guard(GLOBAL)

include(ExternalProject)

option(OBS_FXM_BOOTSTRAP_QT_WEBSOCKETS
  "Download and build Qt WebSockets when it is missing from the selected Qt SDK"
  ON)
set(OBS_FXM_QT_WEBSOCKETS_GIT_REPOSITORY
  "https://github.com/qt/qtwebsockets.git"
  CACHE STRING "Official Qt WebSockets Git repository")
set(OBS_FXM_QT_WEBSOCKETS_GIT_TAG ""
  CACHE STRING "Qt WebSockets Git tag; empty selects v<Qt6_VERSION>")

function(obs_fxm_ensure_qt6_websockets)
  if(TARGET Qt6::WebSockets)
    return()
  endif()

  find_package(Qt6WebSockets CONFIG QUIET)
  if(TARGET Qt6::WebSockets)
    message(STATUS "Found Qt6 WebSockets at: ${Qt6WebSockets_DIR}")
    return()
  endif()

  if(NOT OBS_FXM_BOOTSTRAP_QT_WEBSOCKETS)
    message(FATAL_ERROR
      "Qt6 WebSockets is missing and OBS_FXM_BOOTSTRAP_QT_WEBSOCKETS=OFF. "
      "Install the module for Qt ${Qt6_VERSION} or enable automatic bootstrap.")
  endif()
  if(NOT Qt6_VERSION)
    message(FATAL_ERROR "Cannot bootstrap Qt WebSockets before the base Qt6 version is known.")
  endif()

  if(OBS_FXM_QT_WEBSOCKETS_GIT_TAG)
    set(_qtws_tag "${OBS_FXM_QT_WEBSOCKETS_GIT_TAG}")
  else()
    set(_qtws_tag "v${Qt6_VERSION}")
  endif()

  # Build the Qt module in a completely separate CMake process. Adding the
  # official repository with add_subdirectory/FetchContent makes its second
  # find_package(Qt6) run inside the plugin directory scope, where Qt attempts
  # to promote vcpkg's non-global Threads::Threads imported target and fails.
  set(_qtws_root "${CMAKE_BINARY_DIR}/_deps/qtwebsockets-${Qt6_VERSION}")
  set(_qtws_source "${_qtws_root}/src")
  set(_qtws_build "${_qtws_root}/build")
  set(_qtws_install "${_qtws_root}/install")

  get_filename_component(_qt6_cmake_parent "${Qt6_DIR}/.." ABSOLUTE)
  get_filename_component(_qt6_prefix "${_qt6_cmake_parent}/../.." ABSOLUTE)

  set(_qtws_cmake_args
    "-DCMAKE_INSTALL_PREFIX=${_qtws_install}"
    "-DCMAKE_PREFIX_PATH=${_qt6_prefix}"
    "-DQt6_DIR=${Qt6_DIR}"
    "-DQT_BUILD_EXAMPLES=OFF"
    "-DQT_BUILD_TESTS=OFF"
    "-DQT_BUILD_BENCHMARKS=OFF"
    "-DQT_BUILD_MANUAL_TESTS=OFF"
    "-DQT_BUILD_STANDALONE_TESTS=OFF"
    "-DQT_BUILD_EXAMPLES_BY_DEFAULT=OFF"
    "-DBUILD_SHARED_LIBS=OFF"
    "-DQT_BUILD_SHARED_LIBS=OFF")

  if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND _qtws_cmake_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  endif()
  if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND _qtws_cmake_args "-DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}")
  endif()
  if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND _qtws_cmake_args "-DCMAKE_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}")
  endif()
  if(CMAKE_MSVC_RUNTIME_LIBRARY)
    list(APPEND _qtws_cmake_args "-DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}")
  endif()

  if(WIN32)
    set(_qtws_library "${_qtws_install}/lib/Qt6WebSockets.lib")
  else()
    set(_qtws_library "${_qtws_install}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}Qt6WebSockets${CMAKE_STATIC_LIBRARY_SUFFIX}")
  endif()

  message(STATUS
    "Qt6 WebSockets was not found in the OBS SDK; bootstrapping official "
    "qtwebsockets ${_qtws_tag} for Qt ${Qt6_VERSION} in an isolated build")

  ExternalProject_Add(obs_fxm_qtwebsockets_external
    PREFIX "${_qtws_root}/ep"
    GIT_REPOSITORY "${OBS_FXM_QT_WEBSOCKETS_GIT_REPOSITORY}"
    GIT_TAG "${_qtws_tag}"
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    SOURCE_DIR "${_qtws_source}"
    BINARY_DIR "${_qtws_build}"
    INSTALL_DIR "${_qtws_install}"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS ${_qtws_cmake_args}
    BUILD_BYPRODUCTS "${_qtws_library}"
    UPDATE_DISCONNECTED TRUE
    USES_TERMINAL_DOWNLOAD TRUE
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE
    USES_TERMINAL_INSTALL TRUE)

  file(MAKE_DIRECTORY "${_qtws_install}/include")
  add_library(obs_fxm_qt6_websockets STATIC IMPORTED GLOBAL)
  set_target_properties(obs_fxm_qt6_websockets PROPERTIES
    IMPORTED_LOCATION "${_qtws_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${_qtws_install}/include"
    INTERFACE_LINK_LIBRARIES "Qt6::Core;Qt6::Network")
  add_dependencies(obs_fxm_qt6_websockets obs_fxm_qtwebsockets_external)
  add_library(Qt6::WebSockets ALIAS obs_fxm_qt6_websockets)

  message(STATUS "Using isolated bootstrapped Qt6::WebSockets target (${_qtws_tag})")
endfunction()
