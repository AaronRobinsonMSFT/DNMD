#ifndef _SRC_INTERFACES_IMPORTHELPERS_HPP
#define _SRC_INTERFACES_IMPORTHELPERS_HPP

#include <internal/dnmd_platform.hpp>
#include <internal/span.hpp>
#include <functional>

HRESULT ImportReferenceToTypeDef(
    mdcursor_t sourceTypeDef,
    mdhandle_t sourceAssembly,
    span<uint8_t const> sourceAssemblyHash,
    mdhandle_t targetAssembly,
    mdhandle_t targetModule,
    bool alwaysImport,
    std::function<void(mdcursor_t row)> onRowEdited,
    mdcursor_t* targetTypeDef);

HRESULT ImportReferenceToTypeDefOrRefOrSpec(
    mdhandle_t sourceAssembly,
    mdhandle_t sourceModule,
    span<uint8_t const> sourceAssemblyHash,
    mdhandle_t targetAssembly,
    mdhandle_t targetModule,
    std::function<void(mdcursor_t)> onRowAdded,
    mdToken* importedToken);

#endif // _SRC_INTERFACES_IMPORTHELPERS_HPP
