#include "fixtures.h"
#include "baseline.h"
#include <filesystem>
#include <map>

#include <internal/dnmd_tools_platform.hpp>

#define THROW_IF_FAILED(hr) if (FAILED(hr)) throw std::runtime_error("HRESULT: " #hr)

std::vector<FileBlob> MetadataInDirectory(std::string directory)
{
    std::vector<FileBlob> scenarios;
    for (auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file())
        {
            auto path = entry.path();
            auto ext = path.extension();
            if (ext == ".dll")
            {
                FileBlob scenario;
                scenario.path = path.string();

                malloc_span<uint8_t> b;
                if (!read_in_file(scenario.path.c_str(), b))
                {
                    std::cerr << "Failed to read in '" << scenario.path << "'\n";
                    continue;
                }

                if (!get_metadata_from_pe(b))
                {
                    std::cerr << "Failed to '" << scenario.path << "' << as PE\n";
                    continue;
                }

                scenario.blob = std::vector<uint8_t>{ (uint8_t*)b, b + b.size() };
            }
        }
    }

    return scenarios;
}

namespace
{
    std::string baselinePath;
}

std::string GetBaselineDirectory()
{
    return std::filesystem::path(baselinePath).parent_path().string();
}

void SetBaselineModulePath(std::string path)
{
    baselinePath = path;
}