set(SOURCES
	cursor.cpp
	rowreader.cpp
	typeref.cpp
	module.cpp
	typedef.cpp
	moduleref.cpp
	methoddef.cpp
	standalonesig.cpp
	memberref.cpp
	typespec.cpp
	assembly.cpp
	assemblyref.cpp
	param.cpp
	fieldmarshal.cpp
	fieldrva.cpp)

set(HEADERS emit.hpp)

add_executable(emit ${SOURCES} ${HEADERS})
target_link_libraries(emit PRIVATE dnmd::interfaces_static gtest_main gmock)

add_executable(rowreader_pdb rowreader.cpp)
target_link_libraries(rowreader_pdb PRIVATE dnmd::pdb gtest_main)

if (DNMD_ENABLE_SANITIZERS)
    set(SANITIZER_SUPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sanitizers")
    gtest_discover_tests(emit PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
    gtest_discover_tests(rowreader_pdb TEST_PREFIX "Pdb." PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
else()
    gtest_discover_tests(emit)
    gtest_discover_tests(rowreader_pdb TEST_PREFIX "Pdb.")
endif()