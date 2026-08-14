function(flux_read_version version_file output_variable)
  if(NOT EXISTS "${version_file}")
    message(FATAL_ERROR "Missing component version file: ${version_file}")
  endif()

  file(STRINGS "${version_file}" version_label LIMIT_COUNT 1)
  string(STRIP "${version_label}" version_label)
  if(version_label STREQUAL "")
    message(FATAL_ERROR "Component version file is empty: ${version_file}")
  endif()

  set(${output_variable} "${version_label}" PARENT_SCOPE)
endfunction()

function(flux_extract_semver version_label output_variable)
  string(REGEX MATCH
      "v([0-9]+\\.[0-9]+\\.[0-9]+(-[A-Za-z0-9.-]+)?)"
      version_match "${version_label}")
  if(NOT version_match)
    message(FATAL_ERROR "Version label has no semantic version: ${version_label}")
  endif()
  set(${output_variable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()
