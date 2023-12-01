#ifndef _SRC_INTERFACES_IMPORTHELPERS_HPP
#define _SRC_INTERFACES_IMPORTHELPERS_HPP

#include <internal/dnmd_platform.hpp>
#include <internal/span.hpp>
#include <functional>

HRESULT ImportReferenceToTypeDef(
    mdcursor_t sourceTypeDef,
    mdhandle_t sourceAssembly,
    span<const uint8_t> sourceAssemblyHash,
    mdhandle_t targetAssembly,
    mdhandle_t targetModule,
    std::function<void(mdcursor_t row)> onRowEdited,
    mdcursor_t* targetTypeDef);

#endif // _SRC_INTERFACES_IMPORTHELPERS_HPP
