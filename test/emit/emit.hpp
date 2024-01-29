#ifndef DNMD_TEST_EMIT_EMIT_HPP
#define DNMD_TEST_EMIT_EMIT_HPP
#include <cor.h>
#include <dncp.h>
#include <dnmd_interfaces.hpp>
#include <gtest/gtest.h>
#include <array>
#include <string>

using WSTR_string = std::basic_string<WCHAR>;

inline void CreateEmit(dncp::com_ptr<IMetaDataEmit>& emit)
{
    dncp::com_ptr<IMetaDataDispenser> dispenser;
    ASSERT_EQ(S_OK, GetDispenser(IID_IMetaDataDispenser, (void**)&dispenser));
    ASSERT_EQ(S_OK, dispenser->DefineScope(CLSID_CorMetaDataRuntime, 0, IID_IMetaDataEmit, (IUnknown**)&emit));
}
#endif // DNMD_TEST_EMIT_EMIT_HPP
