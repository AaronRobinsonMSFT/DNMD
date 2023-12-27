#ifndef _TEST_REGPAL_PAL_H_
#define _TEST_REGPAL_PAL_H_

#include <cstdint>
#include <cstddef>

#ifdef BUILD_WINDOWS
#define NOMINMAX
#include <Windows.h>
#else
#include <internal/dnmd_peimage.h>
#endif

#include <dncp.h>
#include <cor.h>
#include <filesystem>
#include <internal/span.hpp>

namespace pal
{
    HRESULT GetBaselineMetadataDispenser(IMetaDataDispenser** dispenser);
    bool ReadFile(std::filesystem::path path, malloc_span<uint8_t>& b);
}

#endif // !_TEST_REGPAL_PAL_H_