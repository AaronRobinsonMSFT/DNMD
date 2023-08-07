# Configure the compiler
include(../configure.cmake)

include_directories(BEFORE ./inc)

configure_file(inc/internal/configure.h.in ${CMAKE_CURRENT_BINARY_DIR}/inc/configure.h)
include_directories($<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/inc>)

add_subdirectory(dnmd/)
add_subdirectory(interfaces/)
add_subdirectory(mddump/)

include(CMakePackageConfigHelpers)
configure_package_config_file(config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/dnmd-config.cmake
  INSTALL_DESTINATION lib/cmake/dnmd)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/dnmd-config-version.cmake
  VERSION 1.0.0
  COMPATIBILITY SameMajorVersion )
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dnmd-config.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/dnmd-config-version.cmake
        DESTINATION lib/cmake/dnmd)