#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "dnmd::dnmd" for configuration "Release"
set_property(TARGET dnmd::dnmd APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(dnmd::dnmd PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdnmd.a"
  )

list(APPEND _cmake_import_check_targets dnmd::dnmd )
list(APPEND _cmake_import_check_files_for_dnmd::dnmd "${_IMPORT_PREFIX}/lib/libdnmd.a" )

# Import target "dnmd::pdb" for configuration "Release"
set_property(TARGET dnmd::pdb APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(dnmd::pdb PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdnmd_pdb.a"
  )

list(APPEND _cmake_import_check_targets dnmd::pdb )
list(APPEND _cmake_import_check_files_for_dnmd::pdb "${_IMPORT_PREFIX}/lib/libdnmd_pdb.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
