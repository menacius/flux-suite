if(NOT DEFINED FLUX_REPOSITORY_ROOT)
  get_filename_component(FLUX_REPOSITORY_ROOT
      "${FLUX_SUITE_COMMON_DIR}/../.." ABSOLUTE)
endif()
include("${FLUX_REPOSITORY_ROOT}/cmake/FluxVersion.cmake")
flux_read_version("${FLUX_SUITE_COMMON_DIR}/VERSION.txt"
                  FLUX_COMMON_VERSION_LABEL)
