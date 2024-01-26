#include <cor.h>
#include <dncp.h>
#include <dnmd_interfaces.hpp>
#include <gtest/gtest.h>
#include <array>

#ifdef BUILD_WINDOWS
using WSTR_string = std::wstring;
#else
#define W(x) u##x
using WSTR_string = std::u16string;
#endif

template<typename T>
using static_char_buffer = std::array<T, 64>;

TEST(TypeRef, DefineTypeRefWithScopeAndName)
{
    dncp::com_ptr<IMetaDataDispenser> dispenser;
    ASSERT_EQ(S_OK, GetDispenser(IID_IMetaDataDispenser, (void**)&dispenser));
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_EQ(S_OK, dispenser->DefineScope(CLSID_CorMetaDataRuntime, 0, IID_IMetaDataEmit, (IUnknown**)&emit));
    mdTypeRef typeRef;
    WSTR_string name = W("System.Object");
    ASSERT_EQ(S_OK, emit->DefineTypeRefByName(TokenFromRid(1, mdtModule), W("System.Object"), &typeRef));
    ASSERT_EQ(1, RidFromToken(typeRef));
    ASSERT_EQ(mdtTypeRef, TypeFromToken(typeRef));

    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    mdToken resolutionScope;
    WSTR_string readName;
    readName.resize(name.capacity() + 1);
    ULONG readNameLength;
    ASSERT_EQ(S_OK, import->GetTypeRefProps(typeRef, &resolutionScope, readName.data(), (ULONG) readName.size(), &readNameLength));
    EXPECT_EQ(TokenFromRid(1, mdtModule), resolutionScope);
    EXPECT_EQ(readNameLength, name.size() + 1);
    EXPECT_EQ(name, readName.substr(0, readNameLength - 1));

}