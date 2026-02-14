#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "dncp::dncp" for configuration "Release"
set_property(TARGET dncp::dncp APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(dncp::dncp PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdncp.a"
  )

list(APPEND _cmake_import_check_targets dncp::dncp )
list(APPEND _cmake_import_check_files_for_dncp::dncp "${_IMPORT_PREFIX}/lib/libdncp.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
