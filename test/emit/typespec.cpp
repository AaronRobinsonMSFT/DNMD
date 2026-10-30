#include "emit.hpp"
#include <array>
#include <vector>
#include <gmock/gmock.h>

TEST(TypeSpec, Define)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdTypeSpec spec;
    std::array<uint8_t, 3> signature = {0x01, 0x02, 0x03};
    ASSERT_EQ(S_OK, emit->GetTokenFromTypeSpec(signature.data(), (ULONG)signature.size(), &spec));
    ASSERT_EQ(1, RidFromToken(spec));
    ASSERT_EQ(mdtTypeSpec, TypeFromToken(spec));

    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));

    PCCOR_SIGNATURE sigBlob;
    ULONG sigBlobLength;
    ASSERT_EQ(S_OK, import->GetTypeSpecFromToken(spec, &sigBlob, &sigBlobLength));
    EXPECT_THAT(std::vector(sigBlob, sigBlob + sigBlobLength), testing::ContainerEq(std::vector(signature.begin(), signature.end())));
}

TEST(TypeSpec, InvalidTokensPreserveOutputs)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    std::array<uint8_t, 3> signature = {0x01, 0x02, 0x03};
    mdTypeSpec spec;
    ASSERT_EQ(S_OK, emit->GetTokenFromTypeSpec(signature.data(), (ULONG)signature.size(), &spec));
    ASSERT_EQ(TokenFromRid(1, mdtTypeSpec), spec);
    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    struct
    {
        mdToken token;
        HRESULT result;
    } const cases[] = {
        {mdTypeSpecNil, CLDB_E_RECORD_NOTFOUND},
        {TokenFromRid(2, mdtTypeSpec), CLDB_E_RECORD_NOTFOUND},
        {TokenFromRid(0x00ffffff, mdtTypeSpec), CLDB_E_RECORD_NOTFOUND},
        {TokenFromRid(1, mdtTypeDef), E_INVALIDARG},
        {TokenFromRid(1, mdtModule), E_INVALIDARG},
        {0xff000001u, E_INVALIDARG}
    };
    for (auto const& test : cases)
    {
        SCOPED_TRACE(test.token);
        PCCOR_SIGNATURE blob = nullptr;
        ULONG length = 123;
        EXPECT_EQ(test.result, import->GetTypeSpecFromToken(test.token, &blob, &length));
        EXPECT_EQ(nullptr, blob);
        EXPECT_EQ(123u, length);
    }
}