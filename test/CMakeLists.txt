# Configure the compiler
include(../configure.cmake)

add_subdirectory(regnative/)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW) # Set timestamps in downloaded archives to the time of download.
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(ImportManagedComponents.cmake)

include(GoogleTest)

add_subdirectory(regtest)