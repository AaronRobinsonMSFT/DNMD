execute_process(
    COMMAND dotnet msbuild FindNetHostDir.proj -t:OutputNetHostDir -nologo
    OUTPUT_VARIABLE NET_HOST_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

string(STRIP ${NET_HOST_DIR} NET_HOST_DIR)

add_library(nethost IMPORTED STATIC)
target_compile_definitions(nethost INTERFACE NETHOST_USE_AS_STATIC)
target_include_directories(nethost INTERFACE ${NET_HOST_DIR})

if (WIN32)
    set_target_properties(nethost PROPERTIES
        IMPORTED_LOCATION ${NET_HOST_DIR}/libnethost.lib)
else()
    set_target_properties(nethost PROPERTIES
        IMPORTED_LOCATION ${NET_HOST_DIR}/libnethost.a)
endif()
