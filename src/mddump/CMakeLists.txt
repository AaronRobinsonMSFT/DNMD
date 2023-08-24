set(SOURCES
  main.cpp
)

add_executable(mddump
  ${SOURCES}
)

target_link_libraries(mddump
  dnmd::pdb
  dncp::dncp)

target_compile_definitions(mddump PRIVATE DNMD_PORTABLE_PDB)

if(NOT MSVC)
  target_link_libraries(mddump dncp::winhdrs)
endif()

install(TARGETS mddump)