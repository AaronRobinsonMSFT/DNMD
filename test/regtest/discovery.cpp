#include "fixtures.h"
#include "baseline.h"
#include <filesystem>
#include <map>
#include "dnmd_interfaces.hpp"

#define THROW_IF_FAILED(hr) if (FAILED(hr)) throw std::runtime_error("HRESULT: " #hr)

namespace
{
    std::vector<MetadataRegresssion> MetadataInDirectory(std::string directory, CorOpenFlags flags)
    {
        std::vector<MetadataRegresssion> scenarios;
        for (auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.is_regular_file())
            {
                auto path = entry.path();
                auto ext = path.extension();
                if (ext == ".dll")
                {
                    MetadataRegresssion scenario;
                    scenario.modulePath = path.string();
                    scenario.baseline = nullptr;
                    scenario.test = nullptr;

                    std::basic_string<WCHAR> u16path; // use basic_string<WCHAR> instead of wstring to get the right type on all platforms
                    u16path.resize(scenario.modulePath.size());
                    // This is a hack to convert from char to WCHAR
                    // TODO: Refactor to use the PAL from dnmd::interfaces
                    std::transform(scenario.modulePath.begin(), scenario.modulePath.end(), u16path.begin(), [](char c) { return (WCHAR)c; });

                    THROW_IF_FAILED(TestBaseline::Metadata->OpenScope(u16path.c_str(), flags, IID_IMetaDataImport2, (IUnknown**)&scenario.baseline));
                    
                    dncp::com_ptr<IMetaDataDispenser> dnmdDispenser;
                    THROW_IF_FAILED(GetDispenser(IID_IMetaDataDispenser, (void**)&dnmdDispenser));

                    THROW_IF_FAILED(dnmdDispenser->OpenScope(u16path.c_str(), flags, IID_IMetaDataImport2, (IUnknown**)&scenario.test));

                    scenarios.push_back(scenario);
                }
            }
        }
    }
}

std::vector<MetadataRegresssion> ReadOnlyMetadataInDirectory(std::string directory)
{
    return MetadataInDirectory(directory, ofReadOnly);
}

std::vector<MetadataRegresssion> ReadWriteMetadataInDirectory(std::string directory)
{
    return MetadataInDirectory(directory, ofWrite);
}
