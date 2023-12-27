execute_process(
    COMMAND dotnet build -c Debug
    COMMAND dotnet build -c Release
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(Regression.Locator IMPORTED SHARED)

if (WIN32)
    set_target_properties(Regression.Locator PROPERTIES
        IMPORTED_LOCATION_DEBUG ${CMAKE_BINARY_DIR}/managed/bin/Regression.Locator/debug/Regression.LocatorNE.dll
        IMPORTED_LOCATION_RELEASE ${CMAKE_BINARY_DIR}/managed/bin/Regression.Locator/release/Regression.LocatorNE.dll
        IMPORTED_IMPLIB_DEBUG ${CMAKE_BINARY_DIR}/managed/bin/Regression.Locator/debug/Regression.LocatorNE.lib
        IMPORTED_IMPLIB_RELEASE ${CMAKE_BINARY_DIR}/managed/bin/Regression.Locator/release/Regression.LocatorNE.lib)
elseif (APPLE)
    set_target_properties(Regression.Locator PROPERTIES
        IMPORTED_LOCATION_DEBUG ${CMAKE_BINARY_DIR}/managed/bin/Regression.Locator/debug/Regression.LocatorNE.dylib
        IMPORTED_LOCATION_RELEASE ${CMAKE_BINARY_DIR}/managed/bin/Regression.Locator/release/Regression.LocatorNE.dylib)
else()
    set_target_properties(Regression.Locator PROPERTIES
        IMPORTED_LOCATION_DEBUG ${CMAKE_BINARY_DIR}/managed/bin/Regression.Locator/debug/Regression.LocatorNE.so
        IMPORTED_LOCATION_RELEASE ${CMAKE_BINARY_DIR}/managed/bin/Regression.Locator/release/Regression.LocatorNE.so)
endif()

set_target_properties(Regression.Locator PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/managed/bin/Regression.Locator/$<LOWER_CASE:$<CONFIG>>/)

set(RegressionLocatorDirectory ${CMAKE_BINARY_DIR}/managed/bin/Regression.Locator/$<CONFIG>/)
