set(SOURCES
	typeref.cpp
	module.cpp
	typedef.cpp)

set(HEADERS emit.hpp)

add_executable(emit ${SOURCES} ${HEADERS})
target_link_libraries(emit PRIVATE dnmd::interfaces_static gtest_main)

gtest_discover_tests(emit)