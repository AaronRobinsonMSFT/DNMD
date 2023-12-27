#ifndef _TEST_REGPAL_PAL_H_
#define _TEST_REGPAL_PAL_H_

#include <cstdint>
#include <cstddef>
#include <dncp.h>

#ifndef BUILD_WINDOWS
#include <internal/dnmd_peimage.h>
#endif

#include <cor.h>
#include <filesystem>
#include <internal/span.hpp>

namespace pal
{
    HRESULT GetBaselineMetadataDispenser(IMetaDataDispenser** dispenser);
    bool ReadFile(std::filesystem::path path, malloc_span<uint8_t>& b);
}

#endif // !_TEST_REGPAL_PAL_H_