cmake_minimum_required(VERSION 3.24)

project(flux-suite-workspace LANGUAGES CXX)

include(CTest)

set(FLUX_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH
    "Flux Suite monorepo root")

option(FLUX_BUILD_MOTION "Build Flux Motion and its OBS plugin" OFF)
option(FLUX_BUILD_ENCODER "Build Flux Encoder" OFF)
option(FLUX_BUILD_INSTALLER "Build Flux Suite Installer" OFF)

add_subdirectory(packages/flux-common)
add_subdirectory(plugins/obs/flux-motion)

if(FLUX_BUILD_MOTION)
  add_subdirectory(apps/flux-motion)
endif()

if(FLUX_BUILD_ENCODER)
  add_subdirectory(apps/flux-encoder)
endif()

if(FLUX_BUILD_INSTALLER)
  add_subdirectory(apps/flux-suite-installer)
endif()

if(NOT FLUX_BUILD_MOTION AND NOT FLUX_BUILD_ENCODER AND NOT FLUX_BUILD_INSTALLER)
  message(STATUS "Flux Suite workspace configured without an application target")
  message(STATUS "Select a component preset or enable a FLUX_BUILD_* option")
endif()
