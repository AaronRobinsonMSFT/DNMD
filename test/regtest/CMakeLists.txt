set(HEADERS
    ./baseline.h
    ./fixtures.h
)
set(SOURCES
    ./main.cpp
    ./discovery.cpp
)


add_executable(regtest ${SOURCES} ${HEADERS})
target_link_libraries(regtest PRIVATE dnmd::interfaces gtest dncp::dncp)
set_target_properties(regtest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON) # Require C++17 for the tests so we can use std::filesystem.

if (NOT WIN32)
    target_link_libraries(regtest PRIVATE dncp::winhdrs)
endif()