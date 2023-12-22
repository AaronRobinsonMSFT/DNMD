#include "fixtures.h"
#include "baseline.h"
#include <pal.hpp>
#include <filesystem>
#include <algorithm>
#include <unordered_map>

#include <internal/dnmd_tools_platform.hpp>

#ifdef _WIN32
#define DNNE_API_OVERRIDE __declspec(dllimport)
#endif
#include <Regression.LocatorNE.h>

#define THROW_IF_FAILED(hr) if (FAILED(hr)) throw std::runtime_error(#hr)


namespace
{
    std::string baselinePath;
    std::string regressionAssemblyPath;

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

    malloc_span<uint8_t> ReadMetadataFromFile(std::filesystem::path path)
    {
        malloc_span<uint8_t> b;
        if (!pal::ReadFile(path, b)
            || !get_metadata_from_pe(b))
        {
            return {};
        }

        return b;
    }
    
    malloc_span<uint8_t> CreateImageWithAppliedDelta()
    {
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

        malloc_span<uint8_t> imageWithDelta;
        DWORD compositeImageSize;
        THROW_IF_FAILED(baseline->GetSaveSize(CorSaveSize::cssAccurate, &compositeImageSize));
        
        imageWithDelta = { (uint8_t*)malloc(compositeImageSize), compositeImageSize };

        THROW_IF_FAILED(baseline->SaveToMemory(imageWithDelta, compositeImageSize));

        return imageWithDelta;
    }

    malloc_span<uint8_t> GetMetadataFromKey(std::string key)
    {
        if (key == DeltaImageKey)
        {
            return CreateImageWithAppliedDelta();
        }
        return {};
    }
}

std::vector<MetadataFile> MetadataFilesInDirectory(std::string directory)
{
    std::vector<MetadataFile> scenarios;

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
                malloc_span<uint8_t> b = ReadMetadataFromFile(path);

                if (b.size() == 0)
                {
                    // Some DLLs don't have metadata, so skip them.
                    continue;
                }

                scenarios.emplace_back(MetadataFile::Kind::OnDisk, path.generic_string());
            }
        }
    }

    return scenarios;
}

std::vector<MetadataFile> CoreLibFiles()
{
    std::vector<MetadataFile> scenarios;

    scenarios.emplace_back(MetadataFile::Kind::OnDisk, (std::filesystem::path(baselinePath).parent_path() / "System.Private.CoreLib.dll").generic_string());
    scenarios.emplace_back(MetadataFile::Kind::OnDisk, (std::filesystem::path(FindFrameworkInstall("v4.0.30319")) / "mscorlib.dll").generic_string());

    auto fx2mscorlib = std::filesystem::path(FindFrameworkInstall("v2.0.50727")) / "mscorlib.dll";
    if (std::filesystem::exists(fx2mscorlib))
    {
        scenarios.emplace_back(MetadataFile::Kind::OnDisk, fx2mscorlib.generic_string());
    }
    return scenarios;
}

namespace
{
    std::mutex metadataCacheMutex;

    struct MetadataFileHash
    {
        size_t operator()(const MetadataFile& file) const
        {
            return std::hash<std::string>{}(file.pathOrKey);
        }
    };

    std::unordered_map<MetadataFile, malloc_span<uint8_t>, MetadataFileHash> metadataCache;
}

span<uint8_t> GetMetadataForFile(MetadataFile file)
{
    std::lock_guard<std::mutex> lock{ metadataCacheMutex };
    auto it = metadataCache.find(file);
    if (it != metadataCache.end())
    {
        return it->second;
    }

    malloc_span<uint8_t> b;
    if (file.kind == MetadataFile::Kind::OnDisk)
    {
        auto path = std::filesystem::path(baselinePath).parent_path() / file.pathOrKey;
        b = ReadMetadataFromFile(path);
    }
    else
    {
        b = GetMetadataFromKey(file.pathOrKey.c_str());
    }

    if (b.size() == 0)
    {
        return {};
    }

    span<uint8_t> spanToReturn = b;

    auto [_, inserted] = metadataCache.emplace(std::move(file), std::move(b));
    assert(inserted);
    return spanToReturn;
}

std::string PrintName(testing::TestParamInfo<MetadataFile> info)
{
    std::string name;
    if (info.param.kind == MetadataFile::Kind::OnDisk)
    {
        name = std::filesystem::path(info.param.pathOrKey).stem().generic_string();
        std::replace(name.begin(), name.end(), '.', '_');
    }
    else
    {
        name = info.param.pathOrKey + "_InMemory";
    }
    return name;
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