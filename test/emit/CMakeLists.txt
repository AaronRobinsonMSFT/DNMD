set(SOURCES
	typeref.cpp
	module.cpp
	typedef.cpp
	moduleref.cpp
	methoddef.cpp
	standalonesig.cpp
)

set(HEADERS emit.hpp)

add_executable(emit ${SOURCES} ${HEADERS})
target_link_libraries(emit PRIVATE dnmd::interfaces_static gtest_main gmock)

gtest_discover_tests(emit)