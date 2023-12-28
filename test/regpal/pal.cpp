#define DNCP_DEFINE_GUID
#include "pal.hpp"

#include <nethost.h>
#include <hostfxr.h>

#ifndef BUILD_WINDOWS
#include <dlfcn.h>
#endif

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string_view>

using std::filesystem::path;

#ifdef BUILD_WINDOWS
#define X(str) std::wstring_view{L##str}
#else
#define X(str) std::string_view{str}
#endif

namespace
{
    void* LoadModule(path path)
    {
#ifdef BUILD_WINDOWS
        return LoadLibraryW(path.c_str());
#else
        return dlopen(path.c_str(), RTLD_LAZY);
#endif
    }

    void* GetSymbol(void* module, char const* name)
    {
#ifdef BUILD_WINDOWS
        return GetProcAddress((HMODULE)module, name);
#else
        return dlsym(module, name);
#endif
    }

    using MetaDataGetDispenser = HRESULT(STDMETHODCALLTYPE*)(REFCLSID, REFIID, LPVOID*);

    using CoreCLRInitialize = int(STDMETHODCALLTYPE*)(
            const char* exePath,
            const char* appDomainFriendlyName,
            int propertyCount,
            const char** propertyKeys,
            const char** propertyValues,
            void** hostHandle,
            unsigned int* domainId);

    MetaDataGetDispenser LoadGetDispenser()
    {
        auto coreClrPath = pal::GetCoreClrPath();
        if (coreClrPath.empty())
        {
            std::cerr << "Failed to get coreclr path" << std::endl;
            return nullptr;
        }

        auto mod = LoadModule(coreClrPath);
        if (mod == nullptr)
        {
            std::cerr << "Failed to load metadata baseline module: " << coreClrPath << std::endl;
            return nullptr;
        }
#ifndef BUILD_WINDOWS
        // On non-Windows, the metadata APIs in CoreCLR don't work until the PAL is initialized.
        // Initialize the runtime just enough to load the PAL.
        auto init = (CoreCLRInitialize)GetSymbol(mod, "coreclr_initialize");
        if (init == nullptr)
        {
            std::cerr << "Failed to find coreclr_initialize in module: " << coreClrPath << std::endl;
            return nullptr;
        }

        const char* propertyKeys[] = { "TRUSTED_PLATFORM_ASSEMBLIES" };
        const char* propertyValues[] = { coreClrPath.c_str() };
        init("regpal", "regpal", 1, propertyKeys, propertyValues, nullptr, nullptr);
#endif

        auto getDispenser = (MetaDataGetDispenser)GetSymbol(mod, "MetaDataGetDispenser");
        if (getDispenser == nullptr)
        {
            std::cerr << "Failed to find MetaDataGetDispenser in module: " << coreClrPath << std::endl;
            return nullptr;
        }

        return getDispenser;
    }

    MetaDataGetDispenser GetDispenser = LoadGetDispenser();
}

HRESULT pal::GetBaselineMetadataDispenser(IMetaDataDispenser** dispenser)
{
    if (GetDispenser == nullptr)
    {
        return E_FAIL;
    }

    return GetDispenser(CLSID_CorMetaDataDispenser, IID_IMetaDataDispenser, (void**)dispenser);
}

bool pal::ReadFile(path path, malloc_span<uint8_t>& b)
{
    // Read in the entire file
    std::ifstream fd{ path, std::ios::binary | std::ios::in };
    if (!fd)
        return false;

    size_t size = std::filesystem::file_size(path);
    if (size == 0)
        return false;

    b = { (uint8_t*)std::malloc(size), size };

    fd.read((char*)(uint8_t*)b, b.size());
    return true;
}

path pal::GetCoreClrPath()
{
    int result = 0;
    size_t bufferSize = 4096;
    std::unique_ptr<char_t[]> hostfxr_path{nullptr};
    do
    {
        hostfxr_path.reset(new char_t[bufferSize]);
        result = get_hostfxr_path(hostfxr_path.get(), &bufferSize, nullptr);
    } while (result != 0);
    
    void* hostfxrModule = LoadModule(hostfxr_path.get());
    if (hostfxrModule == nullptr)
    {
        std::cerr << "Failed to load hostfxr module: " << hostfxr_path.get() << std::endl;
        return {};
    }

    path coreClrPath = {};
    auto getDotnetEnvironmentInfo = (hostfxr_get_dotnet_environment_info_fn)GetSymbol(hostfxrModule, "hostfxr_get_dotnet_environment_info");
    if (getDotnetEnvironmentInfo(
            nullptr,
            nullptr,
            [](const hostfxr_dotnet_environment_info* info, void* result_context)
            {
                path& coreClrPath = *(path*)result_context;
                for (size_t i = 0; i < info->framework_count; ++i)
                {
                    if (info->frameworks[i].name == X("Microsoft.NETCore.App"))
                    {
                        std::cout << "Found Microsoft.NETCore.App (" << info->frameworks[i].version << "): " << info->frameworks[i].path << std::endl;
                        coreClrPath = info->frameworks[i].path;
                        coreClrPath /= info->frameworks[i].version;
#ifdef BUILD_WINDOWS
                        coreClrPath /= "coreclr.dll";
#elif BUILD_MACOS
                        coreClrPath /= "libcoreclr.dylib";
#elif BUILD_UNIX
                        coreClrPath /= "libcoreclr.so";
#else
#error "Unknown platform, cannot determine name for CoreCLR executable"
#endif
                    }
                }
            },
            &coreClrPath
        ) != 0)
    {
        std::cout << "Failed to get dotnet environment info" << std::endl;
        return {};
    }

    return coreClrPath;
}
