function(flux_component_package_name output_variable component_name version_label platform_name)
  string(REGEX REPLACE "[^A-Za-z0-9._-]+" "_" safe_version "${version_label}")
  set(${output_variable} "${component_name}_${safe_version}_${platform_name}" PARENT_SCOPE)
endfunction()
