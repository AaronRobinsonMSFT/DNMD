#include "emit.hpp"
#include <array>
#include <gmock/gmock.h>

TEST(MethodDef, Define)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdMethodDef methodDef;

    // TypeDef,1 is the <Module> type.
    std::array sig = { (uint8_t)IMAGE_CEE_CS_CALLCONV_DEFAULT, (uint8_t)0, (uint8_t)ELEMENT_TYPE_VOID };
    ULONG rva = 0x424242;
    ASSERT_EQ(S_OK, emit->DefineMethod(TokenFromRid(1, mdtTypeDef), W("Foo"), mdStatic, sig.data(), (ULONG)sig.size(), rva, 0, &methodDef));
    ASSERT_EQ(1, RidFromToken(methodDef));
    ASSERT_EQ(mdtMethodDef, TypeFromToken(methodDef));
    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    
    mdTypeDef type;
    WSTR_string readName;
    readName.resize(3);
    ULONG readNameLength;

    DWORD attr;
    PCCOR_SIGNATURE sigBlob;
    ULONG sigBlobLength;
    ULONG codeRVA;
    DWORD implFlags;
    ASSERT_EQ(S_OK, import->GetMethodProps(methodDef, &type, readName.data(), (ULONG)readName.capacity(), &readNameLength, &attr, &sigBlob, &sigBlobLength, &codeRVA, &implFlags));
    EXPECT_EQ(W("Foo"), readName.substr(0, readNameLength - 1));
    EXPECT_EQ(mdStatic, attr);
    EXPECT_EQ(rva, codeRVA);
    EXPECT_EQ(0, implFlags);
    EXPECT_THAT(std::vector(sigBlob, sigBlob + sigBlobLength), testing::ContainerEq(std::vector(sig.begin(), sig.end())));
}