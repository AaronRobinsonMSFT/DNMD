set(SOURCES
  bytes.c
  deltas.cpp
  dump.cpp
  editor.c
  entry.c
  initialize.cpp
  row.cpp
  streams.c
  tables.c
  validate.cpp
)

set(PUBLIC_HEADERS
  ../inc/dnmd.h
  ../inc/dnmd.hpp
  ../inc/dnmd_columns.hpp
  ../inc/dnmd_schema.hpp
  ../inc/dnmd_cursors.hpp
  ../inc/dnmd_read.hpp
  ../inc/dnmd_write.hpp
  ../inc/dnmd_search.hpp
  ../inc/dnmd_result.hpp
)

set(HEADERS
  ${PUBLIC_HEADERS}
  ./internal.h
  ./internal.hpp
)

add_library(dnmd
  STATIC
  ${SOURCES}
  ${HEADERS}
)
add_library(dnmd_pdb
  STATIC
  ${SOURCES}
  ${HEADERS}
)

target_compile_definitions(dnmd_pdb PUBLIC DNMD_PORTABLE_PDB)
target_sources(dnmd_pdb PRIVATE ../inc/dnmd_pdb.hpp
  pdb_blobs.cpp pdb_dump.cpp pdb_sequence_points.cpp)
set_target_properties(dnmd_pdb PROPERTIES EXPORT_NAME pdb)

add_library(dnmd::dnmd ALIAS dnmd)
add_library(dnmd::pdb ALIAS dnmd_pdb)

target_include_directories(dnmd PUBLIC $<INSTALL_INTERFACE:include>)
target_include_directories(dnmd_pdb PUBLIC $<INSTALL_INTERFACE:include>)
target_compile_features(dnmd PUBLIC cxx_std_17)
target_compile_features(dnmd_pdb PUBLIC cxx_std_17)

set_target_properties(dnmd PROPERTIES
  PUBLIC_HEADER "${PUBLIC_HEADERS}"
  POSITION_INDEPENDENT_CODE ON)

set_target_properties(dnmd_pdb PROPERTIES
  PUBLIC_HEADER "${PUBLIC_HEADERS};../inc/dnmd_pdb.hpp"
  POSITION_INDEPENDENT_CODE ON)

install(TARGETS dnmd dnmd_pdb EXPORT dnmd
  PUBLIC_HEADER DESTINATION include
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

install(EXPORT dnmd NAMESPACE dnmd:: FILE dnmdlib.cmake DESTINATION lib/cmake/dnmd)
