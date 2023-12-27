#define DNCP_DEFINE_GUID
#include "pal.hpp"

#ifdef BUILD_WINDOWS
#define DNNE_API_OVERRIDE __declspec(dllimport)
#endif
#include <Regression.LocatorNE.h>

#ifndef BUILD_WINDOWS
#include <dlfcn.h>
#endif

#include <iostream>
#include <fstream>


namespace
{
    void* LoadModule(char const* path)
    {
#ifdef BUILD_WINDOWS
        return LoadLibraryA(path);
#else
        return dlopen(path, RTLD_LAZY);
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

    MetaDataGetDispenser LoadGetDispenser()
    {
        // TODO: Can we use nethost to discover hostfxr and use hostfxr APIs to discover the baseline?
        dncp::cotaskmem_ptr<char> coreClrPath{ (char*)GetCoreClrPath() };
        if (coreClrPath == nullptr)
        {
            std::cerr << "Failed to get coreclr path" << std::endl;
            return nullptr;
        }

        auto mod = LoadModule(coreClrPath.get());
        if (mod == nullptr)
        {
            std::cerr << "Failed to load metadata baseline module: " << coreClrPath.get() << std::endl;
            return nullptr;
        }

        auto getDispenser = (MetaDataGetDispenser)GetSymbol(mod, "MetaDataGetDispenser");
        if (getDispenser == nullptr)
        {
            std::cerr << "Failed to find MetaDataGetDispenser in module: " << coreClrPath.get() << std::endl;
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

bool pal::ReadFile(std::filesystem::path path, malloc_span<uint8_t>& b)
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
