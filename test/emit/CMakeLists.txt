set(HEADER_TEST_SOURCES
	headers/columns.cpp
	headers/cursors.cpp
	headers/read.cpp
	headers/write.cpp
	headers/search.cpp
	headers/order.cpp)

set(SOURCES
	c_api_probes.c
	cursor.cpp
	cursorconversion.cpp
	rowreader.cpp
	rowwriter.cpp
	columnsearch.cpp
	rowsorting.cpp
	importhelpers.cpp
	emitteraccess.cpp
	hcorenum.cpp
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

set(HEADERS emit.hpp native.hpp c_api_probes.h)

add_executable(emit ${SOURCES} ${HEADERS})
target_include_directories(emit PRIVATE ../../src/interfaces)
target_link_libraries(emit PRIVATE dnmd::interfaces_static gtest_main gmock)

add_executable(rowreader_pdb rowreader.cpp c_api_probes.c)
target_link_libraries(rowreader_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(rowwriter_pdb rowwriter.cpp c_api_probes.c)
target_link_libraries(rowwriter_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(columnsearch_pdb columnsearch.cpp c_api_probes.c)
target_link_libraries(columnsearch_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(rowsorting_pdb rowsorting.cpp)
target_link_libraries(rowsorting_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(headers_pdb ${HEADER_TEST_SOURCES} c_api_probes.c)
target_link_libraries(headers_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(cursorconversion_pdb cursorconversion.cpp)
target_link_libraries(cursorconversion_pdb PRIVATE dnmd::pdb gtest_main)

add_executable(corealgorithms corealgorithms.cpp c_api_probes.c)
target_include_directories(corealgorithms PRIVATE ../../src/dnmd)
target_link_libraries(corealgorithms PRIVATE dnmd::dnmd gtest_main)
add_executable(corealgorithms_pdb corealgorithms.cpp c_api_probes.c)
target_include_directories(corealgorithms_pdb PRIVATE ../../src/dnmd)
target_link_libraries(corealgorithms_pdb PRIVATE dnmd::pdb gtest_main)
add_executable(pdbsequencepoints pdbsequencepoints.cpp)
target_link_libraries(pdbsequencepoints PRIVATE dnmd::pdb gtest_main)
add_executable(pdbblobs pdbblobs.cpp)
target_include_directories(pdbblobs PRIVATE ../../src/dnmd)
target_link_libraries(pdbblobs PRIVATE dnmd::pdb gtest_main)

add_executable(c_api_client c_api_client.c)
set_target_properties(c_api_client PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE CXX)
target_link_libraries(c_api_client PRIVATE dnmd::dnmd)
if (MSVC)
    target_compile_definitions(c_api_client PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME CApiClient COMMAND c_api_client)

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
    gtest_discover_tests(corealgorithms PROPERTIES ENVIRONMENT
        "ASAN_OPTIONS=detect_odr_violation=0;LSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/lsan.supp;UBSAN_OPTIONS=suppressions=${SANITIZER_SUPP_DIR}/ubsan.supp")
    gtest_discover_tests(corealgorithms_pdb TEST_PREFIX "Pdb." PROPERTIES ENVIRONMENT
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
    gtest_discover_tests(corealgorithms)
    gtest_discover_tests(corealgorithms_pdb TEST_PREFIX "Pdb.")
    gtest_discover_tests(pdbsequencepoints TEST_PREFIX "Pdb.")
    gtest_discover_tests(pdbblobs TEST_PREFIX "Pdb.")
endif()