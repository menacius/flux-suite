cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED FLUX_REPOSITORY_ROOT)
  get_filename_component(FLUX_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

include("${FLUX_REPOSITORY_ROOT}/cmake/FluxVersion.cmake")
flux_read_version("${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" FLUX_COMMON_VERSION_LABEL)

add_library(flux-common INTERFACE)
add_library(Flux::Common ALIAS flux-common)
target_include_directories(flux-common INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Shared")
set_property(TARGET flux-common PROPERTY FLUX_COMPONENT_VERSION
             "${FLUX_COMMON_VERSION_LABEL}")
