set(SOURCES
  bytes.c
  entry.c
  query.c
  streams.c
  tables.c
  sig.c
)

set(HEADERS
  ../inc/dnmd.h
  ./internal.h
)

add_library(dnmd
  STATIC
  ${SOURCES}
  ${HEADERS}
)

add_library(dnmd::dnmd ALIAS dnmd)

target_include_directories(dnmd PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../inc>)
target_include_directories(dnmd PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../inc/external>) # Hiding the "external" subdirectory due to uses of <...> in cor.h.
target_include_directories(dnmd INTERFACE $<INSTALL_INTERFACE:include>)

if (NOT WIN32)
  target_link_libraries(dnmd PUBLIC dncp::dncp dncp::winhdrs)
endif()

set_target_properties(dnmd PROPERTIES
  PUBLIC_HEADER "../inc/dnmd.h;../inc/dnmd.hpp")

install(TARGETS dnmd EXPORT dnmd
  PUBLIC_HEADER DESTINATION include
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

install(EXPORT dnmd NAMESPACE dnmd:: FILE dnmdlib.cmake DESTINATION lib/cmake/dnmd)
  