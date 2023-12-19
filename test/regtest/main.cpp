#include "baseline.h"
#include <gtest/gtest.h>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace TestBaseline
{
    dncp::com_ptr<IMetaDataDispenser> Metadata = nullptr;
    dncp::com_ptr<ISymUnmanagedBinder> Symbol = nullptr;
}

namespace
{
    void* LoadModule(char const* path)
    {
#ifdef _WIN32
        return LoadLibraryA(path);
#else
        return dlopen(path, RTLD_LAZY);
#endif
    }

    void* GetSymbol(void* module, char const* name)
    {
#ifdef _WIN32
        return GetProcAddress((HMODULE)module, name);
#else
        return dlsym(module, name);
#endif
    }

    using MetaDataGetDispenser = HRESULT(__stdcall*)(REFCLSID, REFIID, LPVOID*);
}

#define RETURN_IF_FAILED(x) { auto hr = x; if (FAILED(hr)) return hr; }

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    for (int i = 0; i < argc; ++i) {
        if (std::string(argv[i]) == "--md-baseline" && i + 1 < argc)
        {
            auto mod = LoadModule(argv[++i]);
            if (TestBaseline::Metadata == nullptr)
            {
                printf("Failed to load metadata baseline module: %s\n", argv[i]);
                return -1;
            }

            auto getDispenser = (MetaDataGetDispenser)GetSymbol(mod, "MetaDataGetDispenser");
            if (getDispenser == nullptr)
            {
                printf("Failed to find MetaDataGetDispenser in module: %s\n", argv[i]);
                return -1;
            }

            RETURN_IF_FAILED(getDispenser(CLSID_CorMetaDataDispenser, IID_IMetaDataDispenser, (void**)&TestBaseline::Metadata));
        }
        else if (std::string(argv[i]) == "--sym-baseline" && i + 1 < argc)
        {
            auto mod = LoadModule(argv[i + 1]);
            if (TestBaseline::Metadata == nullptr)
            {
                printf("Failed to load symbol baseline module: %s\n", argv[i]);
                return -1;
            }
        }
    }

    return RUN_ALL_TESTS();
}
