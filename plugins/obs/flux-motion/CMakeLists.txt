cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED FLUX_REPOSITORY_ROOT)
  get_filename_component(FLUX_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()

include("${FLUX_REPOSITORY_ROOT}/cmake/FluxVersion.cmake")
flux_read_version("${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt"
                  FLUX_MOTION_OBS_VERSION_LABEL)

# The production binary is assembled by apps/flux-motion because it shares the
# Motion compositor and runtime. This target makes the host-adapter boundary
# visible to IDEs and other CMake consumers without duplicating that code.
add_library(flux-motion-obs-sources INTERFACE)
file(GLOB FLUX_MOTION_OBS_HEADERS CONFIGURE_DEPENDS "src/*.h")
target_sources(flux-motion-obs-sources INTERFACE ${FLUX_MOTION_OBS_HEADERS})
target_include_directories(flux-motion-obs-sources INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_property(TARGET flux-motion-obs-sources PROPERTY FLUX_COMPONENT_VERSION
             "${FLUX_MOTION_OBS_VERSION_LABEL}")
