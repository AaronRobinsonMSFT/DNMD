#include "emit.hpp"

TEST(Param, Define)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdParamDef param;
    mdMethodDef method;
    std::array<uint8_t, 4> signature = { IMAGE_CEE_CS_CALLCONV_DEFAULT_HASTHIS, 1, ELEMENT_TYPE_VOID, ELEMENT_TYPE_I4 };
    ASSERT_EQ(S_OK, emit->DefineMethod(TokenFromRid(1, mdtTypeDef), W("Method"), 0, signature.data(), (ULONG)signature.size(), 0, 0, &method));
    WSTR_string paramName{ W("Param") };
    ASSERT_EQ(S_OK, emit->DefineParam(method, 1, paramName.c_str(), pdIn, ELEMENT_TYPE_VOID, nullptr, 0, &param));
    ASSERT_EQ(1, RidFromToken(param));
    ASSERT_EQ(mdtParamDef, TypeFromToken(param));

    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));

    WSTR_string readName;
    readName.resize(paramName.size() + 1);
    mdMethodDef readMethod;
    ULONG readNameLength;
    ULONG flags;
    ULONG sequence;
    DWORD paramConstType;
    UVCP_CONSTANT constValue;
    ULONG constValueLength;
    ASSERT_EQ(S_OK, import->GetParamProps(param, & readMethod, & sequence, readName.data(), (ULONG)readName.capacity(), & readNameLength, & flags, & paramConstType, & constValue, & constValueLength));

    EXPECT_EQ(paramName, readName.substr(0, readNameLength - 1));
    EXPECT_EQ(method, readMethod);
    EXPECT_EQ(1, sequence);
    EXPECT_EQ(pdIn, flags);
    EXPECT_EQ(ELEMENT_TYPE_VOID, paramConstType);
    EXPECT_EQ(nullptr, constValue);
    EXPECT_EQ(0, constValueLength);
}

TEST(Param, DefineWithConstant)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdParamDef param;
    mdMethodDef method;
    std::array<uint8_t, 4> signature = { IMAGE_CEE_CS_CALLCONV_DEFAULT_HASTHIS, 1, ELEMENT_TYPE_VOID, ELEMENT_TYPE_I4 };
    ASSERT_EQ(S_OK, emit->DefineMethod(TokenFromRid(1, mdtTypeDef), W("Method"), 0, signature.data(), (ULONG)signature.size(), 0, 0, &method));
    WSTR_string paramName{ W("Param") };

    int value = 42;
    ASSERT_EQ(S_OK, emit->DefineParam(method, 1, paramName.c_str(), pdIn, ELEMENT_TYPE_I4, &value, sizeof(int), &param));
    ASSERT_EQ(1, RidFromToken(param));
    ASSERT_EQ(mdtParamDef, TypeFromToken(param));

    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));

    WSTR_string readName;
    readName.resize(paramName.size() + 1);
    mdMethodDef readMethod;
    ULONG readNameLength;
    ULONG flags;
    ULONG sequence;
    DWORD paramConstType;
    UVCP_CONSTANT constValue;
    ULONG constValueLength;
    ASSERT_EQ(S_OK, import->GetParamProps(param, & readMethod, & sequence, readName.data(), (ULONG)readName.capacity(), & readNameLength, & flags, & paramConstType, & constValue, & constValueLength));

    EXPECT_EQ(paramName, readName.substr(0, readNameLength - 1));
    EXPECT_EQ(method, readMethod);
    EXPECT_EQ(1, sequence);
    EXPECT_EQ(pdIn, flags);
    EXPECT_EQ(ELEMENT_TYPE_I4, paramConstType);

    // The constant value should be stored in the metadata in the #Blob heap, not just as a pointer to the passed-in value.
    EXPECT_NE(&value, constValue);
    EXPECT_EQ(value, *(int*)constValue);

    // Constant length only returned for string constants.
    EXPECT_EQ(0, constValueLength);
}

TEST(Param, DefineWithConstantString)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdParamDef param;
    mdMethodDef method;
    std::array<uint8_t, 4> signature = { IMAGE_CEE_CS_CALLCONV_DEFAULT_HASTHIS, 1, ELEMENT_TYPE_VOID, ELEMENT_TYPE_STRING };
    ASSERT_EQ(S_OK, emit->DefineMethod(TokenFromRid(1, mdtTypeDef), W("Method"), 0, signature.data(), (ULONG)signature.size(), 0, 0, &method));
    WSTR_string paramName{ W("Param") };

    WSTR_string value = { W("ConstantValue") };
    ASSERT_EQ(S_OK, emit->DefineParam(method, 1, paramName.c_str(), pdIn, ELEMENT_TYPE_STRING, value.c_str(), (ULONG)value.length(), &param));
    ASSERT_EQ(1, RidFromToken(param));
    ASSERT_EQ(mdtParamDef, TypeFromToken(param));

    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));

    WSTR_string readName;
    readName.resize(paramName.size() + 1);
    mdMethodDef readMethod;
    ULONG readNameLength;
    ULONG flags;
    ULONG sequence;
    DWORD paramConstType;
    UVCP_CONSTANT constValue;
    ULONG constValueLength;
    ASSERT_EQ(S_OK, import->GetParamProps(param, &readMethod, &sequence, readName.data(), (ULONG)readName.capacity(), &readNameLength, &flags, &paramConstType, &constValue, &constValueLength));

    EXPECT_EQ(paramName, readName.substr(0, readNameLength - 1));
    EXPECT_EQ(method, readMethod);
    EXPECT_EQ(1, sequence);
    EXPECT_EQ(pdIn, flags);
    EXPECT_EQ(ELEMENT_TYPE_STRING, paramConstType);

    // The constant value should be stored in the metadata in the #Blob heap, not just as a pointer to the passed-in value.
    EXPECT_NE(&value, constValue);
    WSTR_string retrievedConstant{ (WCHAR*)constValue, constValueLength };
    EXPECT_EQ(value, retrievedConstant);
}