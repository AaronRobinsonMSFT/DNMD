set(HEADERS
    ./baseline.h
    ./fixtures.h
    ./asserts.h
    ./pal.hpp
)
set(SOURCES
    ./main.cpp
    ./discovery.cpp
    ./pal.cpp
    ./metadata.cpp
)

add_executable(regtest ${SOURCES} ${HEADERS})
target_link_libraries(regtest PRIVATE dnmd::interfaces gtest gmock dncp::dncp) # Reference gmock for better collection assertions
set_target_properties(regtest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON) # Require C++17 for the tests so we can use std::filesystem.

if (NOT WIN32)
    target_link_libraries(regtest PRIVATE dncp::winhdrs)
endif()

target_link_libraries(regtest PRIVATE Regression.Locator)

add_custom_command(TARGET regtest POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dnmd::interfaces> $<TARGET_FILE_DIR:regtest>)

add_custom_command(TARGET regtest POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory ${RegressionLocatorDirectory} $<TARGET_FILE_DIR:regtest>)

gtest_discover_tests(regtest DISCOVERY_TIMEOUT 100 DISCOVERY_MODE PRE_TEST)