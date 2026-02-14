#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "dnmd::interfaces" for configuration "Release"
set_property(TARGET dnmd::interfaces APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(dnmd::interfaces PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdnmd_interfaces.so"
  IMPORTED_SONAME_RELEASE "libdnmd_interfaces.so"
  )

list(APPEND _cmake_import_check_targets dnmd::interfaces )
list(APPEND _cmake_import_check_files_for_dnmd::interfaces "${_IMPORT_PREFIX}/lib/libdnmd_interfaces.so" )

# Import target "dnmd::interfaces_static" for configuration "Release"
set_property(TARGET dnmd::interfaces_static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(dnmd::interfaces_static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdnmd_interfaces_static.a"
  )

list(APPEND _cmake_import_check_targets dnmd::interfaces_static )
list(APPEND _cmake_import_check_files_for_dnmd::interfaces_static "${_IMPORT_PREFIX}/lib/libdnmd_interfaces_static.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
