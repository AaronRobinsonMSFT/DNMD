set(SOURCES typeref.cpp)

add_executable(emit ${SOURCES})
target_link_libraries(emit PRIVATE dnmd::interfaces_static gtest_main)

gtest_discover_tests(emit)