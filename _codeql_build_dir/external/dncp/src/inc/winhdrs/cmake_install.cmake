# Install script for directory: /home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "./")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/winhdrs" TYPE FILE FILES
    "/home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs/oaidl.h"
    "/home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs/objidl.h"
    "/home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs/ole2.h"
    "/home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs/poppack.h"
    "/home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs/pshpack1.h"
    "/home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs/rpc.h"
    "/home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs/rpcndr.h"
    "/home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs/specstrings.h"
    "/home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs/unknwn.h"
    "/home/runner/work/DNMD/DNMD/external/dncp/src/inc/winhdrs/winerror.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/dncp/winhdrs.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/dncp/winhdrs.cmake"
         "/home/runner/work/DNMD/DNMD/_codeql_build_dir/external/dncp/src/inc/winhdrs/CMakeFiles/Export/a19871413648b358cd375ba787f557e2/winhdrs.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/dncp/winhdrs-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/dncp/winhdrs.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/dncp" TYPE FILE FILES "/home/runner/work/DNMD/DNMD/_codeql_build_dir/external/dncp/src/inc/winhdrs/CMakeFiles/Export/a19871413648b358cd375ba787f557e2/winhdrs.cmake")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/DNMD/DNMD/_codeql_build_dir/external/dncp/src/inc/winhdrs/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
