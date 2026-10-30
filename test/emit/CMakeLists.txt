set(HEADER_TEST_SOURCES
	headers/columns.cpp
	headers/schema.cpp
	headers/cursors.cpp
	headers/read.cpp
	headers/write.cpp
	headers/search.cpp
	headers/order.cpp)

set(SOURCES
	cursor.cpp
	cursorconversion.cpp
	rowreader.cpp
	rowwriter.cpp
	columnsearch.cpp
	rowsorting.cpp
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
	fieldrva.cpp
	${HEADER_TEST_SOURCES})

set(HEADERS emit.hpp native.hpp)

add_executable(emit ${SOURCES} ${HEADERS})
target_link_libraries(emit PRIVATE dnmd::interfaces_static gtest_main gmock)

add_executable(rowreader_pdb rowreader.cpp)
target_link_libraries(rowreader_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(rowwriter_pdb rowwriter.cpp)
target_link_libraries(rowwriter_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(columnsearch_pdb columnsearch.cpp)
target_link_libraries(columnsearch_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(rowsorting_pdb rowsorting.cpp)
target_link_libraries(rowsorting_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(headers_pdb ${HEADER_TEST_SOURCES})
target_link_libraries(headers_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(cursorconversion_pdb cursorconversion.cpp)
target_link_libraries(cursorconversion_pdb PRIVATE dnmd::pdb gtest_main)

add_executable(pdbsequencepoints pdbsequencepoints.cpp)
target_link_libraries(pdbsequencepoints PRIVATE dnmd::pdb gtest_main)
add_executable(pdbblobs pdbblobs.cpp)
target_include_directories(pdbblobs PRIVATE ../../src/dnmd)
target_link_libraries(pdbblobs PRIVATE dnmd::pdb gtest_main)

if (DNMD_ENABLE_SANITIZERS)
    set(SANITIZER_SUPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sanitizers")
    gtest_discover_tests(emit PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
    gtest_discover_tests(rowreader_pdb TEST_PREFIX "Pdb." PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
    gtest_discover_tests(rowwriter_pdb TEST_PREFIX "Pdb." PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
    gtest_discover_tests(columnsearch_pdb TEST_PREFIX "Pdb." PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
    gtest_discover_tests(rowsorting_pdb TEST_PREFIX "Pdb." PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
    gtest_discover_tests(headers_pdb TEST_PREFIX "Pdb." PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
    gtest_discover_tests(cursorconversion_pdb TEST_PREFIX "Pdb." PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
    gtest_discover_tests(pdbsequencepoints TEST_PREFIX "Pdb." PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
    gtest_discover_tests(pdbblobs TEST_PREFIX "Pdb." PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
else()
    gtest_discover_tests(emit)
    gtest_discover_tests(rowreader_pdb TEST_PREFIX "Pdb.")
    gtest_discover_tests(rowwriter_pdb TEST_PREFIX "Pdb.")
    gtest_discover_tests(columnsearch_pdb TEST_PREFIX "Pdb.")
    gtest_discover_tests(rowsorting_pdb TEST_PREFIX "Pdb.")
    gtest_discover_tests(headers_pdb TEST_PREFIX "Pdb.")
    gtest_discover_tests(cursorconversion_pdb TEST_PREFIX "Pdb.")
    gtest_discover_tests(pdbsequencepoints TEST_PREFIX "Pdb.")
    gtest_discover_tests(pdbblobs TEST_PREFIX "Pdb.")
endif()
