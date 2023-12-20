#include <gtest/gtest.h>
#include <string>
#include "baseline.h"
#include "fixtures.h"
#include "pal.hpp"

namespace TestBaseline
{
    dncp::com_ptr<IMetaDataDispenser> Metadata = nullptr;
    dncp::com_ptr<ISymUnmanagedBinder> Symbol = nullptr;
}

namespace
{
    using MetaDataGetDispenser = HRESULT(__stdcall*)(REFCLSID, REFIID, LPVOID*);
}

#define RETURN_IF_FAILED(x) { auto hr = x; if (FAILED(hr)) return hr; }

class ThrowListener final : public testing::EmptyTestEventListener {
  void OnTestPartResult(const testing::TestPartResult& result) override {
    if (result.failed()) {
      throw testing::AssertionException(result);
    }
  }
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    for (int i = 0; i < argc; ++i) {
        if (std::string(argv[i]) == "--md-baseline" && i + 1 < argc)
        {
            std::string path { argv[++i] };
            SetBaselineModulePath(path);
            auto mod = LoadModule(path.c_str());
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
    }

    testing::UnitTest::GetInstance()->listeners().Append(new ThrowListener);

    return RUN_ALL_TESTS();
}
