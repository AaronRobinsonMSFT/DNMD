set(SOURCES
  ./perf.cpp
)

add_executable(regperf
  ${SOURCES}
)

target_link_libraries(regperf
  dnmd::interfaces
  dncp::dncp
  Regression.Locator)

if(NOT MSVC)
  target_link_libraries(regperf dncp::winhdrs)
endif()

include(FetchContent)
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY
  https://github.com/google/benchmark.git
  GIT_TAG
  v1.8.3
)

set(BENCHMARK_ENABLE_TESTING OFF)
FetchContent_MakeAvailable(benchmark)

target_link_libraries(regperf benchmark::benchmark)

install(DIRECTORY ${RegressionLocatorDirectory} DESTINATION bin)
install(TARGETS regperf DESTINATION bin)