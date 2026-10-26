#include "emit.hpp"
#include <dnmd.hpp>
#include <vector>

TEST(CppEmitterAccess, DeleteClassLayoutResolvesDirectAndIndirectFields)
{
    for (bool indirect : {false, true})
    {
        dncp::com_ptr<IMetaDataEmit> emit;
        ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
        mdTypeDef type;
        mdTypeDef other;
        ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Layout"), tdPublic | tdExplicitLayout,
            mdTypeDefNil, nullptr, &type));
        ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Other"), tdPublic | tdExplicitLayout,
            mdTypeDefNil, nullptr, &other));
        uint8_t signature[] = {IMAGE_CEE_CS_CALLCONV_FIELD, ELEMENT_TYPE_I4};
        mdFieldDef first;
        mdFieldDef second;
        mdFieldDef other_field = mdFieldDefNil;
        ASSERT_EQ(S_OK, emit->DefineField(type, W("First"), fdPublic, signature,
            sizeof(signature), ELEMENT_TYPE_VOID, nullptr, 0, &first));
        if (indirect)
        {
            ASSERT_EQ(S_OK, emit->DefineField(other, W("Other"), fdPublic, signature,
                sizeof(signature), ELEMENT_TYPE_VOID, nullptr, 0, &other_field));
        }
        ASSERT_EQ(S_OK, emit->DefineField(type, W("Second"), fdPublic, signature,
            sizeof(signature), ELEMENT_TYPE_VOID, nullptr, 0, &second));
        COR_FIELD_OFFSET offsets[] = {{first, 0}, {second, 4}, {mdFieldDefNil, 0}};
        ASSERT_EQ(S_OK, emit->SetClassLayout(type, 4, offsets, 8));
        if (indirect)
        {
            COR_FIELD_OFFSET other_offsets[] = {{other_field, 8}, {mdFieldDefNil, 0}};
            ASSERT_EQ(S_OK, emit->SetClassLayout(other, 4, other_offsets, 12));
        }
        dncp::com_ptr<IMetaDataImport> import;
        ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
        DWORD packing;
        COR_FIELD_OFFSET actual[2]{};
        ULONG count;
        ULONG size;
        ASSERT_EQ(S_OK, import->GetClassLayout(type, &packing, actual, 2, &count, &size));
        ASSERT_EQ(2u, count);
        ASSERT_EQ(S_OK, emit->DeleteClassLayout(type));
        EXPECT_EQ(CLDB_E_RECORD_NOTFOUND, import->GetClassLayout(type, &packing, actual, 2, &count, &size));
        ULONG image_size;
        ASSERT_EQ(S_OK, emit->GetSaveSize(cssAccurate, &image_size));
        std::vector<uint8_t> image(image_size);
        ASSERT_EQ(S_OK, emit->SaveToMemory(image.data(), image_size));
        mdhandle_t raw;
        ASSERT_TRUE(md_create_handle(image.data(), image.size(), &raw));
        dnmd::handle_ptr saved{raw};
        uint32_t layout_count;
        auto layouts = dnmd::md_create_cursor<mdtid_FieldLayout>(saved.get(), &layout_count);
        ASSERT_TRUE(layouts);
        uint32_t deleted = 0;
        for (uint32_t i = 0; i < layout_count; ++i, dnmd::md_cursor_next(layouts))
        {
            mdToken parent;
            ASSERT_TRUE(layouts.value().get_column_value(dnmd::columns::FieldLayout_Field, parent));
            EXPECT_NE(first, parent);
            EXPECT_NE(second, parent);
            if (parent == mdFieldDefNil)
                ++deleted;
        }
        EXPECT_EQ(2u, deleted);
        if (indirect)
        {
            ASSERT_EQ(S_OK, import->GetClassLayout(other, &packing, actual, 2, &count, &size));
            ASSERT_EQ(1u, count);
            EXPECT_EQ(other_field, actual[0].ridOfField);
            EXPECT_EQ(8u, actual[0].ulOffset);
        }
    }
}

TEST(CppEmitterAccess, DefinesEmptyTypeAfterFieldAndMethodIndirection)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdTypeDef first;
    mdTypeDef second;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("First"), tdPublic, mdTypeDefNil, nullptr, &first));
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Second"), tdPublic, mdTypeDefNil, nullptr, &second));
    uint8_t field_signature[] = {IMAGE_CEE_CS_CALLCONV_FIELD, ELEMENT_TYPE_I4};
    uint8_t method_signature[] = {IMAGE_CEE_CS_CALLCONV_DEFAULT, 0, ELEMENT_TYPE_VOID};
    mdTypeDef owners[] = {first, second, first};
    for (mdTypeDef owner : owners)
    {
        mdFieldDef field;
        mdMethodDef method;
        ASSERT_EQ(S_OK, emit->DefineField(owner, W("Field"), fdPublic, field_signature,
            sizeof(field_signature), ELEMENT_TYPE_VOID, nullptr, 0, &field));
        ASSERT_EQ(S_OK, emit->DefineMethod(owner, W("Method"), mdPublic | mdStatic,
            method_signature, sizeof(method_signature), 0, 0, &method));
    }
    mdTypeDef empty;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Empty"), tdPublic, mdTypeDefNil, nullptr, &empty));
    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    HCORENUM fields{};
    HCORENUM methods{};
    mdFieldDef field;
    mdMethodDef method;
    ULONG count;
    EXPECT_EQ(S_FALSE, import->EnumFields(&fields, empty, &field, 1, &count));
    EXPECT_EQ(0u, count);
    EXPECT_EQ(S_FALSE, import->EnumMethods(&methods, empty, &method, 1, &count));
    EXPECT_EQ(0u, count);
    import->CloseEnum(fields);
    import->CloseEnum(methods);
}

TEST(CppEmitterAccess, TokenClassificationKeepsNamesAndUserStringsDistinct)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    ASSERT_EQ(S_OK, emit->SetModuleProps(W("Classification")));
    mdTypeDef type;
    mdTypeRef reference;
    mdModuleRef module;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Tests.Type"), tdPublic, mdTypeDefNil, nullptr, &type));
    ASSERT_EQ(S_OK, emit->DefineTypeRefByName(mdTokenNil, W("Tests.External"), &reference));
    ASSERT_EQ(S_OK, emit->DefineModuleRef(W("Reference"), &module));
    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    struct
    {
        mdToken token;
        char const* name;
    } const names[] = {
        {TokenFromRid(1, mdtModule), "Classification"},
        {type, "Type"},
        {reference, "External"},
        {module, "Reference"}
    };
    for (auto const& entry : names)
    {
        MDUTF8CSTR name = nullptr;
        ASSERT_EQ(S_OK, import->GetNameFromToken(entry.token, &name));
        EXPECT_STREQ(entry.name, name);
    }

    mdString text;
    ASSERT_EQ(S_OK, emit->DefineUserString(W("Hello"), 5, &text));
    WCHAR value[8]{};
    ULONG length;
    ASSERT_EQ(S_OK, import->GetUserString(text, value, 8, &length));
    EXPECT_EQ(WSTR_string(W("Hello")), WSTR_string(value, length));
    EXPECT_EQ(5u, length);
    EXPECT_EQ(E_INVALIDARG, import->GetUserString(type, value, 8, &length));
    MDUTF8CSTR name = nullptr;
    EXPECT_EQ(E_INVALIDARG, import->GetNameFromToken(text, &name));
}

TEST(CppEmitterAccess, PermissionSetEnumerationClassifiesOwnerTables)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    struct
    {
        mdToken token;
        HRESULT result;
    } const cases[] = {
        {mdTypeDefNil, CLDB_E_RECORD_NOTFOUND},
        {mdMethodDefNil, CLDB_E_RECORD_NOTFOUND},
        {mdAssemblyNil, CLDB_E_RECORD_NOTFOUND},
        {mdModuleNil, S_FALSE},
        {mdTypeRefNil, S_FALSE},
        {0xff000001u, S_FALSE}
    };
    for (auto const& test : cases)
    {
        SCOPED_TRACE(test.token);
        HCORENUM enumeration{};
        mdPermission permission;
        ULONG count = 123;
        EXPECT_EQ(test.result, import->EnumPermissionSets(&enumeration, test.token, 0, &permission, 1, &count));
        if (test.result == S_FALSE)
            EXPECT_EQ(0u, count);
        EXPECT_EQ(nullptr, enumeration);
        import->CloseEnum(enumeration);
    }
}
