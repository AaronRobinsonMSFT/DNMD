#include <gtest/gtest.h>
#include <string>
#include "baseline.h"
#include "fixtures.h"
#include "pal.hpp"

#ifdef _WIN32
#define DNNE_API_OVERRIDE __declspec(dllimport)
#endif
#include <Regression.LocatorNE.h>

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
    if (result.fatally_failed()) {
      throw testing::AssertionException(result);
    }
  }
};

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    dncp::cotaskmem_ptr<char> coreClrPath{ (char*)GetCoreClrPath() };
    if (coreClrPath == nullptr)
    {
        std::cout << "Failed to get coreclr path" << std::endl;
        return -1;
    }

    auto mod = LoadModule(coreClrPath.get());
    if (mod == nullptr)
    {
        std::cout << "Failed to load metadata baseline module: " << coreClrPath.get() << std::endl;
        return -1;
    }

    auto getDispenser = (MetaDataGetDispenser)GetSymbol(mod, "MetaDataGetDispenser");
    if (getDispenser == nullptr)
    {
        std::cout << "Failed to find MetaDataGetDispenser in module: " << coreClrPath.get() << std::endl;
        return -1;
    }

    RETURN_IF_FAILED(getDispenser(CLSID_CorMetaDataDispenser, IID_IMetaDataDispenser, (void**)&TestBaseline::Metadata));

    std::cout << "Loaded metadata baseline module: " << coreClrPath.get() << std::endl;

    dncp::cotaskmem_ptr<char> regressionAssemblyPath{ (char*)GetRegressionTargetAssemblyPath() };
    if (regressionAssemblyPath == nullptr)
    {
        std::cout << "Failed to get regression assembly path" << std::endl;
        return -1;
    }

    SetRegressionAssemblyPath(regressionAssemblyPath.get());

    std::cout << "Regression assembly path: " << regressionAssemblyPath.get() << std::endl;

    testing::UnitTest::GetInstance()->listeners().Append(new ThrowListener);

    return RUN_ALL_TESTS();
}
