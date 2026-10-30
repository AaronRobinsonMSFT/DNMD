#include <dnmd.hpp>
#include "emit.hpp"
#include "importhelpers.hpp"

namespace
{
    using type_result = cursor_result<dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef>>;
    static_assert(!std::is_default_constructible_v<type_result>);
    static_assert(std::is_trivially_copyable_v<type_result>);
    static_assert(!std::is_convertible_v<type_result, HRESULT>);
    static_assert(!std::is_convertible_v<type_result, bool>);
    static_assert(!std::is_constructible_v<type_result, HRESULT>);
    static_assert(std::is_nothrow_constructible_v<type_result, import_helpers::unexpected<HRESULT>>);
    static_assert(std::is_same_v<type_result::error_type, HRESULT>);
    static_assert(std::is_same_v<type_result::value_type,
        dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef>>);
    static_assert(std::is_nothrow_constructible_v<type_result, dnmd::raw_cursor<mdtid_TypeDef>>);
    static_assert(!std::is_constructible_v<type_result, dnmd::raw_cursor<mdtid_Module>>);
    static_assert(!std::is_constructible_v<cursor_result<dnmd::raw_cursor<mdtid_TypeDef>>,
        dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef>>);
    static_assert(noexcept(static_cast<bool>(std::declval<type_result const&>())));
    static_assert(noexcept(std::declval<type_result const&>().has_value()));
    static_assert(noexcept(std::declval<type_result&>().error()));
    static_assert(noexcept(std::declval<type_result const&>().error()));
    static_assert(noexcept(*std::declval<type_result&>()));
    static_assert(noexcept(*std::declval<type_result const&>()));
    static_assert(noexcept(std::declval<type_result&>().operator->()));
    static_assert(noexcept(std::declval<type_result const&>().operator->()));

    class CppImportHelperReads : public testing::Test
    {
    protected:
        dnmd::handle_ptr source;
        dnmd::handle_ptr target;

        void SetUp() override
        {
            source.reset(md_create_new_handle());
            target.reset(md_create_new_handle());
            ASSERT_NE(nullptr, source.get());
            ASSERT_NE(nullptr, target.get());
        }

        void SetMvid(mdhandle_t handle, uint32_t value)
        {
            auto module = dnmd::md_create_cursor<mdtid_Module>(handle).value();
            ASSERT_TRUE(module.set_column_value(dnmd::columns::Module_Mvid,
                mdguid_t{value, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}}));
        }

        dnmd::cursor<mdtid_TypeDef> AddType(char const* name, char const* nspace)
        {
            auto added = dnmd::added_row<mdtid_TypeDef>::append(source.get());
            EXPECT_TRUE(added);
            auto row = added.value();
            dnmd::row_writer writer{row};
            EXPECT_TRUE(writer.write(dnmd::columns::TypeDef_TypeName, name));
            EXPECT_TRUE(writer.write(dnmd::columns::TypeDef_TypeNamespace, nspace));
            return row;
        }
    };
}

TEST_F(CppImportHelperReads, ImportsNestedTypeNamesAndEnclosingReference)
{
    using namespace dnmd::columns;
    ASSERT_NO_FATAL_FAILURE(SetMvid(source.get(), 1));
    ASSERT_NO_FATAL_FAILURE(SetMvid(target.get(), 1));
    auto outer = AddType("Outer", "Tests");
    auto inner = AddType("Inner", "");
    {
        auto added = dnmd::added_row<mdtid_NestedClass>::append(source.get());
        ASSERT_TRUE(added);
        dnmd::row_writer writer{added.value()};
        ASSERT_TRUE(writer.write(NestedClass_NestedClass, inner));
        ASSERT_TRUE(writer.write(NestedClass_EnclosingClass, outer));
    }

    auto imported = ImportReferenceToTypeDef(inner, source.get(), {},
        target.get(), target.get(), true, [](dnmd::any_cursor) {});
    ASSERT_TRUE(imported);
    EXPECT_TRUE(imported.has_value());
    auto imported_inner = imported->as<mdtid_TypeRef>();
    ASSERT_TRUE(imported_inner);
    char const* name;
    char const* nspace;
    dnmd::row_reader inner_reader{imported_inner.value()};
    ASSERT_TRUE(inner_reader.read(TypeRef_TypeName, &name));
    ASSERT_TRUE(inner_reader.read(TypeRef_TypeNamespace, &nspace));
    EXPECT_STREQ("Inner", name);
    EXPECT_STREQ("", nspace);
    mdToken scope;
    ASSERT_TRUE(inner_reader.read(TypeRef_ResolutionScope, scope));
    EXPECT_EQ(CreateTokenType(mdtid_TypeRef) | 1, scope);
    auto imported_outer = dnmd::raw_cursor<mdtid_TypeRef>{target.get(), scope};
    ASSERT_TRUE(imported_outer);
    dnmd::row_reader outer_reader{imported_outer.value()};
    ASSERT_TRUE(outer_reader.read(TypeRef_TypeName, &name));
    ASSERT_TRUE(outer_reader.read(TypeRef_TypeNamespace, &nspace));
    EXPECT_STREQ("Outer", name);
    EXPECT_STREQ("Tests", nspace);

    auto missing = dnmd::raw_cursor<mdtid_TypeDef>::nil(source.get());
    auto output = ImportReferenceToTypeDef(missing, source.get(), {},
        target.get(), target.get(), true, [](dnmd::any_cursor) {});
    ASSERT_FALSE(output);
    EXPECT_FALSE(output.has_value());
    EXPECT_EQ(E_FAIL, output.error());
    uint32_t count;
    ASSERT_TRUE(dnmd::md_create_cursor<mdtid_TypeRef>(target.get(), &count));
    EXPECT_EQ(3u, count);
}

TEST_F(CppImportHelperReads, ImportsTypeNamesWithinTheSourceImage)
{
    using namespace dnmd::columns;
    auto type = AddType("InPlace", "Tests");
    auto imported = ImportReferenceToTypeDef(type, source.get(), {},
        source.get(), source.get(), true, [](dnmd::any_cursor) {});
    ASSERT_TRUE(imported);
    EXPECT_TRUE(imported.has_value());
    auto reference = imported->as<mdtid_TypeRef>();
    ASSERT_TRUE(reference);
    dnmd::row_reader reader{reference.value()};
    char const* name;
    char const* nspace;
    mdToken scope;
    ASSERT_TRUE(reader.read(TypeRef_TypeName, &name));
    ASSERT_TRUE(reader.read(TypeRef_TypeNamespace, &nspace));
    ASSERT_TRUE(reader.read(TypeRef_ResolutionScope, scope));
    EXPECT_STREQ("InPlace", name);
    EXPECT_STREQ("Tests", nspace);
    EXPECT_EQ(CreateTokenType(mdtid_Module) | 1, scope);
}

TEST_F(CppImportHelperReads, ImportsTypeReferenceWithinTheSourceImage)
{
    using namespace dnmd::columns;
    mdToken token;
    {
        auto reference = dnmd::added_row<mdtid_TypeRef>::append(source.get());
        ASSERT_TRUE(reference);
        dnmd::row_writer writer{reference.value()};
        ASSERT_TRUE(writer.write(TypeRef_ResolutionScope, dnmd::md_create_cursor<mdtid_Module>(source.get())));
        ASSERT_TRUE(writer.write(TypeRef_TypeName, "Existing"));
        ASSERT_TRUE(writer.write(TypeRef_TypeNamespace, "Tests"));
        ASSERT_TRUE(reference.get_token(token));
    }
    mdToken original = token;
    uint32_t added = 0;
    ASSERT_EQ(S_OK, ImportReferenceToTypeDefOrRefOrSpec(source.get(), source.get(), {},
        source.get(), source.get(), [&](dnmd::any_cursor) { ++added; }, &token));
    EXPECT_EQ(original, token);
    EXPECT_EQ(0u, added);
}

TEST_F(CppImportHelperReads, RejectsEndOfTableResolutionScopes)
{
    using namespace dnmd::columns;
    ASSERT_NO_FATAL_FAILURE(SetMvid(source.get(), 1));
    ASSERT_NO_FATAL_FAILURE(SetMvid(target.get(), 2));
    auto reference = dnmd::added_row<mdtid_TypeRef>::append(source.get());
    ASSERT_TRUE(reference);
    auto row = reference.value();
    dnmd::row_writer writer{row};
    ASSERT_TRUE(writer.write(TypeRef_TypeName, "Invalid"));
    mdToken original;
    ASSERT_TRUE(reference.get_token(original));
    reference.commit();
    struct
    {
        mdToken scope;
        HRESULT result;
    } const cases[] = {
        {CreateTokenType(mdtid_Module) | 2, CLDB_E_FILE_CORRUPT},
        {CreateTokenType(mdtid_ModuleRef) | 1, CLDB_E_FILE_CORRUPT},
        {CreateTokenType(mdtid_AssemblyRef) | 1, E_FAIL}
    };
    for (auto const& test : cases)
    {
        SCOPED_TRACE(test.scope);
        ASSERT_TRUE(writer.write(TypeRef_ResolutionScope, test.scope));
        mdToken token = original;
        uint32_t added = 0;
        EXPECT_EQ(test.result, ImportReferenceToTypeDefOrRefOrSpec(target.get(), source.get(), {},
            target.get(), target.get(), [&](dnmd::any_cursor) { ++added; }, &token));
        EXPECT_EQ(original, token);
        EXPECT_EQ(0u, added);
    }
}

TEST_F(CppImportHelperReads, FindsMatchingAssemblyReferenceWhileImportingType)
{
    using namespace dnmd::columns;
    ASSERT_NO_FATAL_FAILURE(SetMvid(source.get(), 1));
    ASSERT_NO_FATAL_FAILURE(SetMvid(target.get(), 2));
    {
        auto assembly = dnmd::added_row<mdtid_Assembly>::append(source.get());
        ASSERT_TRUE(assembly);
        dnmd::row_writer writer{assembly.value()};
        ASSERT_TRUE(writer.write(Assembly_Name, "SourceAssembly"));
        ASSERT_TRUE(writer.write(Assembly_Culture, ""));
        ASSERT_TRUE(writer.write(Assembly_MajorVersion, 1));
        ASSERT_TRUE(writer.write(Assembly_MinorVersion, 2));
        ASSERT_TRUE(writer.write(Assembly_BuildNumber, 3));
        ASSERT_TRUE(writer.write(Assembly_RevisionNumber, 4));
    }
    struct
    {
        uint32_t major;
        uint32_t minor;
        uint32_t build;
        uint32_t revision;
        char const* name;
        char const* culture;
    } const candidates[] = {
        {9, 2, 3, 4, "SourceAssembly", ""},
        {1, 9, 3, 4, "SourceAssembly", ""},
        {1, 2, 9, 4, "SourceAssembly", ""},
        {1, 2, 3, 9, "SourceAssembly", ""},
        {1, 2, 3, 4, "OtherAssembly", ""},
        {1, 2, 3, 4, "SourceAssembly", "en-US"},
        {1, 2, 3, 4, "SourceAssembly", ""}
    };
    for (auto const& candidate : candidates)
    {
        auto reference = dnmd::added_row<mdtid_AssemblyRef>::append(target.get());
        ASSERT_TRUE(reference);
        dnmd::row_writer writer{reference.value()};
        ASSERT_TRUE(writer.write(AssemblyRef_Name, candidate.name));
        ASSERT_TRUE(writer.write(AssemblyRef_Culture, candidate.culture));
        ASSERT_TRUE(writer.write(AssemblyRef_MajorVersion, candidate.major));
        ASSERT_TRUE(writer.write(AssemblyRef_MinorVersion, candidate.minor));
        ASSERT_TRUE(writer.write(AssemblyRef_BuildNumber, candidate.build));
        ASSERT_TRUE(writer.write(AssemblyRef_RevisionNumber, candidate.revision));
    }
    auto type = AddType("Imported", "Tests");
    auto imported = ImportReferenceToTypeDef(type, source.get(), {},
        target.get(), target.get(), true, [](dnmd::any_cursor) {});
    ASSERT_TRUE(imported);
    EXPECT_TRUE(imported.has_value());
    auto row = imported->as<mdtid_TypeRef>();
    ASSERT_TRUE(row);
    dnmd::row_reader reader{row.value()};
    char const* name;
    ASSERT_TRUE(reader.read(TypeRef_TypeName, &name));
    EXPECT_STREQ("Imported", name);
    mdToken scope;
    ASSERT_TRUE(reader.read(TypeRef_ResolutionScope, scope));
    EXPECT_EQ(CreateTokenType(mdtid_AssemblyRef) | ARRAY_SIZE(candidates), scope);
    uint32_t count;
    ASSERT_TRUE(dnmd::md_create_cursor<mdtid_AssemblyRef>(target.get(), &count));
    EXPECT_EQ(ARRAY_SIZE(candidates), count);
}

TEST_F(CppImportHelperReads, ImportsTypeSpecSignature)
{
    using namespace dnmd::columns;
    mdToken token;
    uint8_t signature[] = {ELEMENT_TYPE_I4};
    {
        auto added = dnmd::added_row<mdtid_TypeSpec>::append(source.get());
        ASSERT_TRUE(added);
        ASSERT_TRUE(added.value().set_column_value(TypeSpec_Signature, signature, sizeof(signature)));
        ASSERT_TRUE(added.get_token(token));
    }
    ASSERT_EQ(S_OK, ImportReferenceToTypeDefOrRefOrSpec(source.get(), source.get(), {},
        target.get(), target.get(), [](dnmd::any_cursor) {}, &token));
    auto imported = dnmd::raw_cursor<mdtid_TypeSpec>{target.get(), token};
    ASSERT_TRUE(imported);
    uint8_t const* bytes;
    uint32_t length;
    ASSERT_TRUE(imported.value().get_column_value(TypeSpec_Signature, &bytes, &length));
    ASSERT_EQ(sizeof(signature), length);
    EXPECT_EQ(signature[0], bytes[0]);
}

TEST(ImportCursorResult, PreservesCursorlessStatuses)
{
    for (HRESULT status : {S_FALSE, E_FAIL, E_OUTOFMEMORY, CLDB_E_FILE_CORRUPT})
    {
        type_result result{import_helpers::unexpected{status}};
        ASSERT_FALSE(result);
        EXPECT_FALSE(result.has_value());
        EXPECT_EQ(status, result.error());
        auto const copy = result;
        ASSERT_FALSE(copy);
        EXPECT_EQ(status, copy.error());
    }
}

TEST_F(CppImportHelperReads, CursorResultsPreserveNilAndEndPositions)
{
    using position = dnmd::raw_cursor<mdtid_Module>;
    for (auto value : {position::nil(source.get()), position::end(source.get())})
    {
        cursor_result<dnmd::raw_cursor<mdtid_Module, mdtid_ModuleRef>> result{value};
        ASSERT_TRUE(result);
        EXPECT_TRUE(result.has_value());
        EXPECT_FALSE(*result);
        EXPECT_EQ(value.is_nil(), result->is_nil());
        EXPECT_TRUE(result->is<mdtid_Module>());
        EXPECT_EQ(source.get(), result->get_handle());
        mdToken expected;
        mdToken actual;
        ASSERT_TRUE(value.get_token(expected));
        ASSERT_TRUE(result->get_token(actual));
        EXPECT_EQ(expected, actual);
    }
}

TEST_F(CppImportHelperReads, CursorResultsWidenAndChangeAlternatives)
{
    auto module = dnmd::md_create_cursor<mdtid_Module>(source.get());
    ASSERT_TRUE(module);
    cursor_result<dnmd::raw_cursor<mdtid_Module, mdtid_ModuleRef>> result{import_helpers::unexpected{E_FAIL}};
    result = module.value();
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.has_value());
    auto const copy = result;
    EXPECT_TRUE(copy);
    EXPECT_TRUE(copy->is<mdtid_Module>());
    EXPECT_EQ(source.get(), (*copy).get_handle());
    EXPECT_EQ(source.get(), result->get_handle());

    result = import_helpers::unexpected{S_FALSE};
    ASSERT_FALSE(result);
    EXPECT_EQ(S_FALSE, result.error());
    EXPECT_TRUE(copy);
    result = *copy;
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.has_value());
    result = import_helpers::unexpected{E_FAIL};
    ASSERT_FALSE(result);
    EXPECT_EQ(E_FAIL, result.error());
}

TEST_F(CppImportHelperReads, ReusesTypeDefinitionInMatchingTargetImage)
{
    ASSERT_NO_FATAL_FAILURE(SetMvid(source.get(), 1));
    ASSERT_NO_FATAL_FAILURE(SetMvid(target.get(), 1));
    auto type = AddType("Existing", "Tests");
    auto targetType = dnmd::added_row<mdtid_TypeDef>::append(target.get());
    ASSERT_TRUE(targetType);
    mdToken expected;
    ASSERT_TRUE(targetType.get_token(expected));
    targetType.commit();
    uint32_t added = 0;

    auto imported = ImportReferenceToTypeDef(type, source.get(), {},
        target.get(), target.get(), false, [&](dnmd::any_cursor) { ++added; });
    ASSERT_TRUE(imported);
    EXPECT_TRUE(imported.has_value());
    EXPECT_TRUE(imported->is<mdtid_TypeDef>());
    EXPECT_EQ(target.get(), imported->get_handle());
    mdToken actual;
    ASSERT_TRUE(imported->get_token(actual));
    EXPECT_EQ(expected, actual);
    EXPECT_EQ(0u, added);
}

TEST_F(CppImportHelperReads, RejectsMissingTypeDefinitionInMatchingTargetImage)
{
    ASSERT_NO_FATAL_FAILURE(SetMvid(source.get(), 1));
    ASSERT_NO_FATAL_FAILURE(SetMvid(target.get(), 1));
    auto type = AddType("Missing", "Tests");
    uint32_t added = 0;
    auto imported = ImportReferenceToTypeDef(type, source.get(), {},
        target.get(), target.get(), false, [&](dnmd::any_cursor) { ++added; });
    ASSERT_FALSE(imported);
    EXPECT_FALSE(imported.has_value());
    EXPECT_EQ(CLDB_E_FILE_CORRUPT, imported.error());
    EXPECT_EQ(0u, added);
}
