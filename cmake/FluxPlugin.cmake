function(flux_register_plugin target_name version_file)
  flux_read_version("${version_file}" component_version)
  set_property(TARGET "${target_name}" PROPERTY FLUX_COMPONENT_VERSION
               "${component_version}")
endfunction()
