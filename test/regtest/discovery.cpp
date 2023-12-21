#include "fixtures.h"
#include "baseline.h"
#include <filesystem>
#include <map>

#include <internal/dnmd_tools_platform.hpp>

#ifdef _WIN32
#define DNNE_API_OVERRIDE __declspec(dllimport)
#endif
#include <Regression.LocatorNE.h>

namespace
{
    malloc_span<uint8_t> ReadMetadataFromFile(std::filesystem::path path)
    {
        malloc_span<uint8_t> b;
        if (!read_in_file(path.generic_string().c_str(), b))
        {
            std::cerr << "Failed to read in '" << path << "'\n";
            return {};
        }

        if (!get_metadata_from_pe(b))
        {
            std::cerr << "Failed to read '" << path << "' as PE\n";
            return {};
        }

        return b;
    }
}

std::vector<FileBlob> MetadataInDirectory(std::string directory)
{
    std::vector<FileBlob> scenarios;
    
    if (!std::filesystem::exists(directory))
    {
        std::cerr << "Directory '" << directory << "' does not exist\n";
        return scenarios;
    }

    for (auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file())
        {
            auto path = entry.path();
            auto ext = path.extension();
            if (ext == ".dll")
            {
                FileBlob scenario;
                scenario.path = path.filename().generic_string();

                malloc_span<uint8_t> b = ReadMetadataFromFile(path);

                if (b.size() == 0)
                {
                    // Some DLLs don't have metadata, so skip them.
                    continue;
                }

                scenario.blob = std::vector<uint8_t>{ (uint8_t*)b, b + b.size() };
                scenarios.push_back(std::move(scenario));
            }
        }
    }

    return scenarios;
}

namespace
{
    std::string baselinePath;
    std::string regressionAssemblyPath;
}

std::string GetBaselineDirectory()
{
    return std::filesystem::path(baselinePath).parent_path().string();
}

void SetBaselineModulePath(std::string path)
{
    baselinePath = path;
}

void SetRegressionAssemblyPath(std::string path)
{
    regressionAssemblyPath = path;
}

malloc_span<uint8_t> GetRegressionAssemblyMetadata()
{
    return ReadMetadataFromFile(regressionAssemblyPath);
}

std::string FindFrameworkInstall(std::string version)
{
#ifdef _WIN32
    dncp::cotaskmem_ptr<char> installPath { (char*)GetFrameworkPath(version.c_str()) };
    if (installPath == nullptr)
    {
        std::cerr << "Failed to find framework install for version: " << version << "\n";
        return {};
    }
    return installPath.get();
#else
    return {};
#endif
}