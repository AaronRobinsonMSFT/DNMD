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
    bool read_in_file(std::filesystem::path path, malloc_span<uint8_t>& b)
    {
        // Read in the entire file
        std::ifstream fd{ path, std::ios::binary };
        if (!fd)
            return false;

        size_t size = std::filesystem::file_size(path);
        if (size == 0)
            return false;

        b = { (uint8_t*)std::malloc(size), size };

        fd.read((char*)(uint8_t*)b, b.size());
        return true;
    }

    malloc_span<uint8_t> ReadMetadataFromFile(std::filesystem::path path)
    {
        malloc_span<uint8_t> b;
        if (!read_in_file(path, b))
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

std::vector<FileBlob> CoreLibs()
{
    std::vector<FileBlob> scenarios;

    auto coreclrSystemPrivateCoreLib = std::filesystem::path(baselinePath).parent_path() / "System.Private.CoreLib.dll";
    auto b = ReadMetadataFromFile(coreclrSystemPrivateCoreLib);
    if (b.size() == 0)
    {
        std::cerr << "Failed to read in '" << coreclrSystemPrivateCoreLib << "'\n";
        return scenarios;
    }

    scenarios.push_back({ "netcoreapp", std::vector<uint8_t>{ (uint8_t*)b, b + b.size() } });

    auto fx4mscorlib = std::filesystem::path(FindFrameworkInstall("v4.0.30319")) / "mscorlib.dll";
    b = ReadMetadataFromFile(coreclrSystemPrivateCoreLib);
    if (b.size() == 0)
    {
        std::cerr << "Failed to read in '" << coreclrSystemPrivateCoreLib << "'\n";
        return scenarios;
    }

    scenarios.push_back({ "fx4", std::vector<uint8_t>{ (uint8_t*)b, b + b.size() } });
    auto fx2mscorlib = std::filesystem::path(FindFrameworkInstall("v2.0.50727")) / "mscorlib.dll";
    if (std::filesystem::exists(fx2mscorlib))
    {
        b = ReadMetadataFromFile(coreclrSystemPrivateCoreLib);
        if (b.size() == 0)
        {
            std::cerr << "Failed to read in '" << coreclrSystemPrivateCoreLib << "'\n";
            return scenarios;
        }

        scenarios.push_back({ "fx2", std::vector<uint8_t>{ (uint8_t*)b, b + b.size() } });
    }

    return scenarios;
}

namespace
{
    template<typename T>
    struct OnExit
    {
        T callback;
        ~OnExit()
        {
            callback();
        }
    };

    template<typename T>
    [[nodiscard]] OnExit<T> on_scope_exit(T callback)
    {
        return { callback };
    }
}

#define THROW_IF_FAILED(hr) if (FAILED(hr)) throw std::runtime_error(#hr)

FileBlob ImageWithDelta()
{
    FileBlob imageWithDelta = { "imageWithDelta", {} };
    uint8_t* baseImage = nullptr;
    uint32_t baseImageSize = 0;
    uint8_t** deltas = nullptr;
    uint32_t* deltaSizes = nullptr;
    uint32_t numDeltas = 0;

    GetImageAndDeltas(&baseImage, &baseImageSize, &numDeltas, &deltas, &deltaSizes);

    auto _ = on_scope_exit([&]() { FreeImageAndDeltas(baseImage, numDeltas, deltas, deltaSizes); });

    dncp::com_ptr<IMetaDataEmit> baseline;
    THROW_IF_FAILED(TestBaseline::DeltaMetadataBuilder->OpenScopeOnMemory(baseImage, baseImageSize, 0, IID_IMetaDataEmit, (IUnknown**)&baseline));
    
    for (uint32_t i = 0; i < numDeltas; i++)
    {
        dncp::com_ptr<IMetaDataImport> delta;
        THROW_IF_FAILED(TestBaseline::DeltaMetadataBuilder->OpenScopeOnMemory(deltas[i], deltaSizes[i], 0, IID_IMetaDataImport, (IUnknown**)&delta));

        THROW_IF_FAILED(baseline->ApplyEditAndContinue(delta));
    }

    DWORD compositeImageSize;
    THROW_IF_FAILED(baseline->GetSaveSize(CorSaveSize::cssAccurate, &compositeImageSize));

    imageWithDelta.blob.resize(compositeImageSize);
    THROW_IF_FAILED(baseline->SaveToMemory(imageWithDelta.blob.data(), compositeImageSize));

    return imageWithDelta;
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