#include <dnmd.hpp>
#include "emit.hpp"
#include <algorithm>

namespace
{
    template<typename T, typename = void>
    struct has_as : std::false_type {};

    template<typename T>
    struct has_as<T, std::void_t<decltype(std::declval<T const&>().as())>> : std::true_type {};

    template<typename T, typename = void>
    struct can_advance : std::false_type {};

    template<typename T>
    struct can_advance<T, std::void_t<decltype(dnmd::md_cursor_next(std::declval<T&>()))>> : std::true_type {};

    using type_row = dnmd::cursor<mdtid_TypeDef>;
    using type_position = dnmd::cursor_raw<mdtid_TypeDef>;
    using type_reference = dnmd::cursor_raw<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>;
    using method_position = dnmd::cursor_indirect<mdtid_MethodDef, mdtid_MethodPtr>;

    static_assert(std::is_same_v<dnmd::handle_ptr::deleter_type, dnmd::handle_deleter_t>);
    static_assert(std::is_same_v<decltype(&::md_create_cursor),
        bool (*)(mdhandle_t, mdtable_id_t, mdcursor_t*, uint32_t*)>);
    static_assert(std::is_same_v<decltype(&::md_token_to_cursor),
        bool (*)(mdhandle_t, mdToken, mdcursor_t*)>);
    static_assert(dnmd::cursor_tableid_v<type_row> == mdtid_TypeDef);
    static_assert(std::is_same_v<std::remove_cv_t<decltype(dnmd::columns::TypeDef_TypeName)>,
        dnmd::column<mdtid_TypeDef, dnmd::column_type_string>>);
    static_assert(!std::is_default_constructible_v<type_row>);
    static_assert(!std::is_constructible_v<type_row, mdcursor_t>);
    static_assert(std::is_trivially_copyable_v<type_row>);
    static_assert(std::is_trivially_copyable_v<type_position>);
    static_assert(std::is_trivially_copyable_v<method_position>);
    static_assert(sizeof(type_position) == sizeof(mdcursor_t));
    static_assert(sizeof(type_reference) == sizeof(mdcursor_t));
    static_assert(has_as<type_position>::value);
    static_assert(has_as<method_position>::value);
    static_assert(!has_as<type_reference>::value);
    static_assert(!can_advance<type_row>::value);
    static_assert(can_advance<type_position>::value);
    static_assert(can_advance<method_position>::value);
    static_assert(std::is_same_v<decltype(std::declval<type_position>().as()), type_row>);
    static_assert(std::is_same_v<decltype(std::declval<method_position>().as()), dnmd::cursor<mdtid_MethodDef>>);

    class CppCursor : public testing::Test
    {
    protected:
        dnmd::handle_ptr handle;

        void SetUp() override
        {
            handle.reset(md_create_new_handle());
            ASSERT_NE(nullptr, handle.get());
        }

        void AddType(char const* name, mdcursor_t& row)
        {
            ASSERT_TRUE(md_append_row(handle.get(), mdtid_TypeDef, &row));
            ASSERT_TRUE(md_set_column_value_as_utf8(row, mdtTypeDef_TypeName, name));
            md_commit_row_add(row);
        }

        void AddMethod(mdcursor_t owner, char const* name)
        {
            mdcursor_t row;
            ASSERT_TRUE(md_add_new_row_to_list(owner, mdtTypeDef_MethodList, &row));
            ASSERT_TRUE(md_set_column_value_as_utf8(row, mdtMethodDef_Name, name));
            md_commit_row_add(row);
        }

        void AddIndirectMethods(mdcursor_t& first_type)
        {
            mdcursor_t second_type{};
            ASSERT_NO_FATAL_FAILURE(AddType("FirstType", first_type));
            ASSERT_NO_FATAL_FAILURE(AddMethod(first_type, "FirstMethod"));
            ASSERT_NO_FATAL_FAILURE(AddType("SecondType", second_type));
            ASSERT_NO_FATAL_FAILURE(AddMethod(second_type, "OtherMethod"));
            ASSERT_NO_FATAL_FAILURE(AddMethod(first_type, "SecondMethod"));
        }
    };
}

using namespace dnmd::columns;

TEST_F(CppCursor, EmptyPositions)
{
    type_position position;
    method_position indirect;
    EXPECT_FALSE(position);
    EXPECT_FALSE(indirect);
    EXPECT_FALSE(dnmd::md_cursor_next(position));
    EXPECT_FALSE(dnmd::md_cursor_next(indirect));
    EXPECT_FALSE(md_cursor_is_dereferenceable(mdcursor_t{}));

    mdToken token = 0x12345678;
    EXPECT_FALSE(indirect.get_target_token(token));
    EXPECT_EQ(0x12345678u, token);

    int visits = 0;
    EXPECT_FALSE(position.visit([&](auto) { ++visits; }));
    EXPECT_EQ(0, visits);

    uint32_t count;
    EXPECT_FALSE(dnmd::md_create_cursor<mdtid_MethodDef>(handle.get(), &count));
    EXPECT_FALSE(dnmd::md_create_cursor<mdtid_TypeDef>(nullptr, &count));
    EXPECT_FALSE(dnmd::md_token_to_cursor<mdtid_TypeDef>(handle.get(), CreateTokenType(mdtid_MethodDef) | 1));
    EXPECT_FALSE(dnmd::md_token_to_cursor<mdtid_TypeDef>(handle.get(), CreateTokenType(mdtid_TypeDef)));
}

TEST_F(CppCursor, CreateCursorWithoutCount)
{
    auto position = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get());
    static_assert(std::is_same_v<decltype(position), type_position>);
    static_assert(noexcept(dnmd::md_create_cursor<mdtid_TypeDef>(nullptr)));
    ASSERT_TRUE(position);
    char const* name;
    ASSERT_TRUE(position.as().get_column_value(TypeDef_TypeName, &name));
    EXPECT_STREQ("<Module>", name);

    EXPECT_FALSE(dnmd::md_create_cursor<mdtid_MethodDef>(handle.get()));
    EXPECT_FALSE(dnmd::md_create_cursor<mdtid_TypeDef>(nullptr));
}

TEST_F(CppCursor, RawRowAndEndPositions)
{
    uint32_t count;
    auto position = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get(), &count);
    ASSERT_TRUE(position);
    ASSERT_EQ(1u, count);
    auto row = position.as();

    char const* name;
    ASSERT_TRUE(row.get_column_value(TypeDef_TypeName, &name));
    EXPECT_STREQ("<Module>", name);
    uint32_t flags;
    ASSERT_TRUE(row.get_column_value(TypeDef_Flags, &flags));
    EXPECT_EQ(0u, flags);

    ASSERT_TRUE(dnmd::md_cursor_next(position));
    EXPECT_FALSE(position);
    mdToken token;
    ASSERT_TRUE(position.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 2, token);
    EXPECT_FALSE(dnmd::md_cursor_next(position));

    int visits = 0;
    EXPECT_FALSE(position.visit([&](auto) { ++visits; }));
    EXPECT_EQ(0, visits);
    auto narrowed_end = position.get_as<mdtid_TypeDef>();
    EXPECT_FALSE(narrowed_end);
    ASSERT_TRUE(narrowed_end.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 2, token);

    ASSERT_TRUE(md_cursor_to_token(row, &token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 1, token);
    ASSERT_TRUE(dnmd::md_cursor_move(position, -1));
    EXPECT_TRUE(position);
}

TEST_F(CppCursor, RawNilAndEndReferences)
{
    uint32_t count;
    auto position = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get(), &count);
    ASSERT_TRUE(position);
    type_reference reference;
    ASSERT_TRUE(position.as().get_column_value(TypeDef_Extends, reference));
    EXPECT_FALSE(reference);

    mdToken token;
    ASSERT_TRUE(reference.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef), token);
    auto nil = reference.get_as<mdtid_TypeDef>();
    EXPECT_FALSE(nil);
    ASSERT_TRUE(nil.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef), token);
    ASSERT_TRUE(dnmd::md_cursor_next(nil));
    EXPECT_TRUE(nil);
    ASSERT_TRUE(nil.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 1, token);

    int visits = 0;
    EXPECT_FALSE(reference.visit([&](auto) { ++visits; }));
    EXPECT_EQ(0, visits);

    mdToken end_token = CreateTokenType(mdtid_TypeDef) | (count + 1);
    ASSERT_TRUE(md_set_column_value_as_token(position, mdtTypeDef_Extends, end_token));
    ASSERT_TRUE(position.as().get_column_value(TypeDef_Extends, reference));
    EXPECT_FALSE(reference);
    ASSERT_TRUE(reference.get_token(token));
    EXPECT_EQ(end_token, token);
    EXPECT_FALSE(reference.visit([&](auto) { ++visits; }));
    EXPECT_EQ(0, visits);
}

TEST_F(CppCursor, NarrowingAndVisitingRows)
{
    uint32_t count;
    auto position = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get(), &count);
    ASSERT_TRUE(position);
    type_reference reference{position};

    EXPECT_FALSE(reference.get_as<mdtid_TypeRef>());
    auto narrowed = reference.get_as<mdtid_TypeDef>();
    ASSERT_TRUE(narrowed);
    char const* name;
    ASSERT_TRUE(narrowed.as().get_column_value(TypeDef_TypeName, &name));
    EXPECT_STREQ("<Module>", name);

    int visits = 0;
    ASSERT_TRUE(reference.visit([&](auto row) {
        ++visits;
        EXPECT_EQ(mdtid_TypeDef, dnmd::cursor_tableid_v<decltype(row)>);
    }));
    EXPECT_EQ(1, visits);
}

TEST_F(CppCursor, TokenFactoriesAndValidation)
{
    mdToken token = CreateTokenType(mdtid_TypeDef) | 1;
    auto position = dnmd::md_token_to_cursor<mdtid_TypeDef, mdtid_TypeRef>(handle.get(), token);
    ASSERT_TRUE(position);
    EXPECT_TRUE(position.get_as<mdtid_TypeDef>());
    EXPECT_FALSE(position.get_as<mdtid_TypeRef>());
    EXPECT_TRUE(dnmd::md_is_valid_token(handle.get(), token));
    EXPECT_FALSE(dnmd::md_is_valid_token(handle.get(), CreateTokenType(mdtid_TypeDef)));
    EXPECT_FALSE(dnmd::md_is_valid_token(nullptr, token));
    EXPECT_FALSE((dnmd::md_token_to_cursor<mdtid_TypeDef, mdtid_TypeRef>(handle.get(), CreateTokenType(mdtid_Module) | 1)));
    EXPECT_TRUE(dnmd::md_is_valid_token(handle.get(), CreateTokenType(mdtid_Module) | 1));
}

TEST_F(CppCursor, DirectListPositions)
{
    uint32_t count;
    auto type = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get(), &count);
    ASSERT_TRUE(type);
    method_position methods;
    ASSERT_TRUE(type.as().get_column_value_as_range(TypeDef_MethodList, methods, &count));
    EXPECT_EQ(0u, count);
    EXPECT_FALSE(methods);

    mdToken source_token;
    mdToken target_token;
    ASSERT_TRUE(methods.get_token(source_token));
    ASSERT_TRUE(methods.get_target_token(target_token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), source_token);
    EXPECT_EQ(source_token, target_token);

    ASSERT_NO_FATAL_FAILURE(AddMethod(type, "Method"));
    ASSERT_TRUE(type.as().get_column_value_as_range(TypeDef_MethodList, methods, &count));
    ASSERT_EQ(1u, count);
    ASSERT_TRUE(methods);
    char const* name;
    ASSERT_TRUE(methods.as().get_column_value(MethodDef_Name, &name));
    EXPECT_STREQ("Method", name);
    mdToken owner;
    ASSERT_TRUE(methods.as().get_parent_token(owner));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 1, owner);

    ASSERT_TRUE(dnmd::md_cursor_next(methods));
    EXPECT_FALSE(methods);
    ASSERT_TRUE(methods.get_token(source_token));
    ASSERT_TRUE(methods.get_target_token(target_token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 2, source_token);
    EXPECT_EQ(source_token, target_token);
    EXPECT_FALSE(dnmd::md_cursor_next(methods));
    ASSERT_TRUE(dnmd::md_cursor_move(methods, -1));
    EXPECT_TRUE(methods);
}

TEST_F(CppCursor, IndirectTargetsAreCachedAndRefreshedOnMovement)
{
    mdcursor_t first_type{};
    ASSERT_NO_FATAL_FAILURE(AddIndirectMethods(first_type));
    auto type = type_position{first_type}.as();
    method_position methods;
    uint32_t count;
    ASSERT_TRUE(type.get_column_value_as_range(TypeDef_MethodList, methods, &count));
    ASSERT_EQ(2u, count);
    ASSERT_TRUE(dnmd::md_cursor_next(methods));
    ASSERT_TRUE(methods);

    mdToken source_token;
    mdToken target_token;
    ASSERT_TRUE(methods.get_token(source_token));
    ASSERT_TRUE(methods.get_target_token(target_token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodPtr) | 2, source_token);
    EXPECT_EQ(mdtid_MethodDef, ExtractTokenType(target_token));
    ASSERT_NE(CreateTokenType(mdtid_MethodDef) | 2, target_token);
    mdToken cached_token = target_token;

    ASSERT_TRUE(md_set_column_value_as_token(methods, mdtMethodPtr_Method, CreateTokenType(mdtid_MethodDef) | 2));
    ASSERT_TRUE(methods.get_target_token(target_token));
    EXPECT_EQ(cached_token, target_token);
    ASSERT_TRUE(md_cursor_to_token(methods.as(), &target_token));
    EXPECT_EQ(cached_token, target_token);

    method_position refreshed{methods};
    ASSERT_TRUE(refreshed.get_target_token(target_token));
    EXPECT_EQ(cached_token, target_token);
    ASSERT_TRUE(dnmd::md_cursor_move(refreshed, 0));
    ASSERT_TRUE(refreshed.get_target_token(target_token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 2, target_token);

    ASSERT_TRUE(dnmd::md_cursor_next(methods));
    ASSERT_TRUE(methods.get_target_token(target_token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 2, target_token);
    ASSERT_TRUE(dnmd::md_cursor_next(methods));
    EXPECT_FALSE(methods);
    ASSERT_TRUE(methods.get_token(source_token));
    ASSERT_TRUE(methods.get_target_token(target_token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodPtr) | 4, source_token);
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 4, target_token);
}

TEST_F(CppCursor, NilIndirectionPosition)
{
    mdcursor_t first_type{};
    ASSERT_NO_FATAL_FAILURE(AddIndirectMethods(first_type));
    ASSERT_TRUE(md_set_column_value_as_token(first_type, mdtTypeDef_MethodList, CreateTokenType(mdtid_MethodPtr)));
    method_position methods;
    ASSERT_TRUE(type_position{first_type}.as().get_column_value(TypeDef_MethodList, methods));
    EXPECT_FALSE(methods);

    mdToken token;
    ASSERT_TRUE(methods.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodPtr), token);
    ASSERT_TRUE(methods.get_target_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), token);
    ASSERT_TRUE(dnmd::md_cursor_next(methods));
    EXPECT_TRUE(methods);
    ASSERT_TRUE(methods.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodPtr) | 1, token);
}

TEST_F(CppCursor, IndirectionToNilEndAndInvalidRows)
{
    mdcursor_t first_type{};
    ASSERT_NO_FATAL_FAILURE(AddIndirectMethods(first_type));
    method_position methods;
    ASSERT_TRUE(type_position{first_type}.as().get_column_value(TypeDef_MethodList, methods));
    ASSERT_TRUE(methods);
    mdcursor_t source = methods;

    ASSERT_TRUE(md_set_column_value_as_token(source, mdtMethodPtr_Method, CreateTokenType(mdtid_MethodDef)));
    methods = method_position{source};
    EXPECT_FALSE(methods);
    mdToken token;
    ASSERT_TRUE(methods.get_target_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), token);

    ASSERT_TRUE(md_set_column_value_as_token(source, mdtMethodPtr_Method, CreateTokenType(mdtid_MethodDef) | 4));
    methods = method_position{source};
    EXPECT_FALSE(methods);
    ASSERT_TRUE(methods.get_target_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 4, token);

    ASSERT_TRUE(md_set_column_value_as_token(source, mdtMethodPtr_Method, CreateTokenType(mdtid_MethodDef) | 100));
    methods = method_position{source};
    EXPECT_FALSE(methods);
    token = 0x12345678;
    EXPECT_FALSE(methods.get_target_token(token));
    EXPECT_EQ(0x12345678u, token);
    ASSERT_TRUE(methods.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodPtr) | 1, token);
}

TEST_F(CppCursor, FindRowsReturnsTheSearchedTable)
{
    uint32_t count;
    auto types = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get(), &count);
    ASSERT_TRUE(types);
    auto constant_match = dnmd::md_find_row_from_cursor(types.as(), TypeDef_Flags, 0);
    EXPECT_TRUE(constant_match);
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(types.as(), TypeDef_Flags, 0x12345678));

    mdcursor_t child{};
    ASSERT_NO_FATAL_FAILURE(AddType("Child", child));
    mdcursor_t nested;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_NestedClass, &nested));
    ASSERT_TRUE(md_set_column_value_as_cursor(nested, mdtNestedClass_NestedClass, child));
    ASSERT_TRUE(md_set_column_value_as_cursor(nested, mdtNestedClass_EnclosingClass, types));
    md_commit_row_add(nested);

    auto rows = dnmd::md_create_cursor<mdtid_NestedClass>(handle.get(), &count);
    ASSERT_TRUE(rows);
    mdToken child_token;
    ASSERT_TRUE(md_cursor_to_token(child, &child_token));
    auto match = dnmd::md_find_row_from_cursor(rows.as(), NestedClass_NestedClass, child_token);
    static_assert(std::is_same_v<decltype(match), dnmd::cursor_raw<mdtid_NestedClass>>);
    ASSERT_TRUE(match);
    type_position enclosing;
    ASSERT_TRUE(match.as().get_column_value(NestedClass_EnclosingClass, enclosing));
    ASSERT_TRUE(enclosing);
    mdToken token;
    ASSERT_TRUE(enclosing.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 1, token);
    EXPECT_TRUE(dnmd::md_find_row_from_cursor(rows.as(), NestedClass_NestedClass, type_position{child}.as()));
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(rows.as(), NestedClass_NestedClass, CreateTokenType(mdtid_TypeDef) | 100));
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(rows.as(), NestedClass_NestedClass, CreateTokenType(mdtid_MethodDef) | 2));

    dnmd::cursor_raw<mdtid_NestedClass> range;
    ASSERT_EQ(MD_RANGE_FOUND, dnmd::md_find_range_from_cursor(rows.as(), NestedClass_NestedClass, child_token, range, &count));
    EXPECT_EQ(1u, count);
    ASSERT_TRUE(range);
    EXPECT_EQ(MD_RANGE_NOT_FOUND, dnmd::md_find_range_from_cursor(rows.as(), NestedClass_NestedClass,
        CreateTokenType(mdtid_TypeDef) | 100, range, &count));
    EXPECT_EQ(MD_RANGE_NOT_FOUND, dnmd::md_find_range_from_cursor(rows.as(), NestedClass_NestedClass,
        CreateTokenType(mdtid_MethodDef) | 2, range, &count));

    mdToken tokens[2] = {mdTokenNil, 0x12345678};
    ASSERT_EQ(1, rows.as().get_column_values(NestedClass_NestedClass, 2, tokens));
    EXPECT_EQ(child_token, tokens[0]);
    EXPECT_EQ(0x12345678u, tokens[1]);
}

TEST_F(CppCursor, FindCodedIndexRows)
{
    mdcursor_t reference;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_TypeRef, &reference));
    ASSERT_TRUE(md_set_column_value_as_token(reference, mdtTypeRef_ResolutionScope, CreateTokenType(mdtid_Module) | 1));
    md_commit_row_add(reference);

    uint32_t count;
    auto rows = dnmd::md_create_cursor<mdtid_TypeRef>(handle.get(), &count);
    auto modules = dnmd::md_create_cursor<mdtid_Module>(handle.get(), &count);
    ASSERT_TRUE(rows);
    ASSERT_TRUE(modules);
    auto match = dnmd::md_find_row_from_cursor(rows.as(), TypeRef_ResolutionScope, modules.as());
    static_assert(std::is_same_v<decltype(match), dnmd::cursor_raw<mdtid_TypeRef>>);
    EXPECT_TRUE(match);
    EXPECT_TRUE(dnmd::md_find_row_from_cursor(rows.as(), TypeRef_ResolutionScope, CreateTokenType(mdtid_Module) | 1));
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(rows.as(), TypeRef_ResolutionScope, CreateTokenType(mdtid_AssemblyRef) | 1));
    dnmd::cursor_raw<mdtid_TypeRef> range;
    EXPECT_EQ(MD_RANGE_NOT_SUPPORTED, dnmd::md_find_range_from_cursor(rows.as(), TypeRef_ResolutionScope,
        CreateTokenType(mdtid_Module) | 1, range, &count));
}

TEST_F(CppCursor, FindIndirectListRowsUsesTheSourcePosition)
{
    mdcursor_t first_type{};
    ASSERT_NO_FATAL_FAILURE(AddIndirectMethods(first_type));
    method_position methods;
    ASSERT_TRUE(type_position{first_type}.as().get_column_value(TypeDef_MethodList, methods));
    uint32_t count;
    auto types = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get(), &count);
    ASSERT_TRUE(types);
    auto match = dnmd::md_find_row_from_cursor(types.as(), TypeDef_MethodList, methods);
    ASSERT_TRUE(match);
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(types.as(), TypeDef_MethodList, method_position{}));
    char const* name;
    ASSERT_TRUE(match.as().get_column_value(TypeDef_TypeName, &name));
    EXPECT_STREQ("FirstType", name);
}

TEST(CppCursorImport, EnumMembersWithName)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdToken implements = mdTokenNil;
    mdTypeDef first_type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("FirstType"), 0, mdTypeDefNil, &implements, &first_type));

    uint8_t method_signature[] = {IMAGE_CEE_CS_CALLCONV_DEFAULT, 0, ELEMENT_TYPE_VOID};
    mdMethodDef matched_method;
    ASSERT_EQ(S_OK, emit->DefineMethod(first_type, W("Match"), mdStatic, method_signature,
        sizeof(method_signature), 0, 0, &matched_method));
    mdTypeDef second_type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("SecondType"), 0, mdTypeDefNil, &implements, &second_type));
    mdMethodDef other_method;
    ASSERT_EQ(S_OK, emit->DefineMethod(second_type, W("Other"), mdStatic, method_signature,
        sizeof(method_signature), 0, 0, &other_method));
    ASSERT_EQ(S_OK, emit->DefineMethod(first_type, W("Other"), mdStatic, method_signature,
        sizeof(method_signature), 0, 0, &other_method));

    uint8_t field_signature[] = {IMAGE_CEE_CS_CALLCONV_FIELD, ELEMENT_TYPE_I4};
    mdFieldDef matched_field;
    ASSERT_EQ(S_OK, emit->DefineField(first_type, W("Match"), 0, field_signature,
        sizeof(field_signature), ELEMENT_TYPE_VOID, nullptr, 0, &matched_field));

    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    HCORENUM enumeration = nullptr;
    mdToken members[2]{};
    ULONG count = 0;
    HRESULT hr = import->EnumMembersWithName(&enumeration, first_type, W("Match"), members, 2, &count);
    import->CloseEnum(enumeration);
    ASSERT_EQ(S_OK, hr);
    ASSERT_EQ(2u, count);
    EXPECT_EQ(matched_method, members[0]);
    EXPECT_EQ(matched_field, members[1]);

    enumeration = nullptr;
    std::array<mdToken, 3> all_members{};
    hr = import->EnumMembers(&enumeration, first_type, all_members.data(),
        static_cast<ULONG>(all_members.size()), &count);
    import->CloseEnum(enumeration);
    ASSERT_EQ(S_OK, hr);
    ASSERT_EQ(all_members.size(), count);
    std::array<mdToken, 3> expected = {matched_method, other_method, matched_field};
    std::sort(expected.begin(), expected.end());
    std::sort(all_members.begin(), all_members.end());
    EXPECT_EQ(expected, all_members);

    MDUTF8CSTR name;
    ASSERT_EQ(S_OK, import->GetNameFromToken(matched_method, &name));
    EXPECT_STREQ("Match", name);
    ASSERT_EQ(S_OK, import->GetNameFromToken(matched_field, &name));
    EXPECT_STREQ("Match", name);
    EXPECT_EQ(E_INVALIDARG, import->GetNameFromToken(TokenFromRid(1, mdtTypeSpec), &name));
    int is_global;
    ASSERT_EQ(S_OK, import->IsGlobal(matched_method, &is_global));
    EXPECT_FALSE(is_global);
    ASSERT_EQ(S_OK, import->IsGlobal(TokenFromRid(1, mdtTypeDef), &is_global));
    EXPECT_TRUE(is_global);
}

TEST(CppCursorImport, GetCustomAttributeByName)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdToken implements = mdTokenNil;
    mdTypeDef attribute_type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Example.Attribute"), 0, mdTypeDefNil, &implements, &attribute_type));
    uint8_t signature[] = {IMAGE_CEE_CS_CALLCONV_HASTHIS, 0, ELEMENT_TYPE_VOID};
    mdMethodDef constructor;
    ASSERT_EQ(S_OK, emit->DefineMethod(attribute_type, W(".ctor"), mdSpecialName | mdRTSpecialName,
        signature, sizeof(signature), 0, 0, &constructor));
    mdTypeDef owner;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Owner"), 0, mdTypeDefNil, &implements, &owner));
    uint8_t value[] = {1, 0, 0, 0};
    mdCustomAttribute attribute;
    ASSERT_EQ(S_OK, emit->DefineCustomAttribute(owner, constructor, value, sizeof(value), &attribute));

    mdTypeRef reference;
    ASSERT_EQ(S_OK, emit->DefineTypeRefByName(TokenFromRid(1, mdtModule), W("Example.OtherAttribute"), &reference));
    mdMemberRef referenced_constructor;
    ASSERT_EQ(S_OK, emit->DefineMemberRef(reference, W(".ctor"), signature, sizeof(signature), &referenced_constructor));
    ASSERT_EQ(S_OK, emit->DefineCustomAttribute(owner, referenced_constructor, value, sizeof(value), &attribute));

    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    auto check_attribute = [&](LPCWSTR name) {
        void const* data;
        ULONG size;
        ASSERT_EQ(S_OK, import->GetCustomAttributeByName(owner, name, &data, &size));
        ASSERT_NE(nullptr, data);
        ASSERT_EQ(sizeof(value), size);
        for (ULONG i = 0; i < size; ++i)
            EXPECT_EQ(value[i], static_cast<uint8_t const*>(data)[i]);
    };
    ASSERT_NO_FATAL_FAILURE(check_attribute(W("Example.Attribute")));
    ASSERT_NO_FATAL_FAILURE(check_attribute(W("Example.OtherAttribute")));
}

TEST(CppCursorImport, ClassLayoutWithoutFieldOffsets)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdToken implements = mdTokenNil;
    mdTypeDef type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Layout"), tdSequentialLayout, mdTypeDefNil, &implements, &type));
    uint8_t signature[] = {IMAGE_CEE_CS_CALLCONV_FIELD, ELEMENT_TYPE_I4};
    mdFieldDef field;
    ASSERT_EQ(S_OK, emit->DefineField(type, W("Field"), fdPublic, signature,
        sizeof(signature), ELEMENT_TYPE_VOID, nullptr, 0, &field));
    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));

    COR_FIELD_OFFSET offset{};
    DWORD packing;
    ULONG size;
    ULONG count;
    EXPECT_EQ(CLDB_E_RECORD_NOTFOUND, import->GetClassLayout(type, &packing, &offset, 1, &count, &size));
    ASSERT_EQ(1u, count);
    EXPECT_EQ(field, offset.ridOfField);
    EXPECT_EQ(0xffffffffu, offset.ulOffset);

    ASSERT_EQ(S_OK, emit->SetClassLayout(type, 8, nullptr, 32));
    ASSERT_EQ(S_OK, import->GetClassLayout(type, &packing, &offset, 1, &count, &size));
    EXPECT_EQ(8u, packing);
    EXPECT_EQ(32u, size);
    EXPECT_EQ(field, offset.ridOfField);
    EXPECT_EQ(0xffffffffu, offset.ulOffset);
}

TEST(CppCursorImport, GenericParameterEnumerationAcrossBatches)
{
    dncp::com_ptr<IMetaDataEmit2> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdToken implements = mdTokenNil;
    mdTypeDef first_type;
    mdTypeDef second_type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("First`65"), 0, mdTypeDefNil, &implements, &first_type));
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Second`65"), 0, mdTypeDefNil, &implements, &second_type));
    std::array<mdGenericParam, 65> expected{};
    for (ULONG i = 0; i < 2 * expected.size(); ++i)
    {
        mdGenericParam parameter;
        ASSERT_EQ(S_OK, emit->DefineGenericParam(i % 2 == 0 ? first_type : second_type,
            i / 2, 0, W("T"), 0, nullptr, &parameter));
        if (i % 2 == 0)
            expected[i / 2] = parameter;
    }

    dncp::com_ptr<IMetaDataImport2> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport2, (void**)&import));
    HCORENUM enumeration = nullptr;
    std::array<mdGenericParam, 65> actual{};
    ULONG count;
    HRESULT hr = import->EnumGenericParams(&enumeration, first_type, actual.data(),
        static_cast<ULONG>(actual.size()), &count);
    import->CloseEnum(enumeration);
    ASSERT_EQ(S_OK, hr);
    ASSERT_EQ(actual.size(), count);
    EXPECT_EQ(expected, actual);
    for (ULONG i = 0; i < count; ++i)
    {
        ULONG sequence;
        DWORD flags;
        mdToken owner;
        WCHAR name[4];
        ULONG name_length;
        ASSERT_EQ(S_OK, import->GetGenericParamProps(actual[i], &sequence, &flags, &owner,
            nullptr, name, 4, &name_length));
        EXPECT_EQ(i, sequence);
        EXPECT_EQ(0u, flags);
        EXPECT_EQ(first_type, owner);
        EXPECT_EQ(static_cast<WCHAR>('T'), name[0]);
    }
}

TEST(CppCursorImport, NativeCallingConventionsFromTypedNames)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdToken implements = mdTokenNil;
    mdTypeDef cdecl_type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("System.Runtime.CompilerServices.CallConvCdecl"),
        0, mdTypeDefNil, &implements, &cdecl_type));
    mdTypeRef stdcall_type;
    ASSERT_EQ(S_OK, emit->DefineTypeRefByName(TokenFromRid(1, mdtModule),
        W("System.Runtime.CompilerServices.CallConvStdcall"), &stdcall_type));
    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    auto check_calling_convention = [&](mdToken modifier, ULONG expected) {
        uint8_t signature[8] = {IMAGE_CEE_CS_CALLCONV_DEFAULT, 0, ELEMENT_TYPE_CMOD_OPT};
        ULONG compressed_size = CorSigCompressToken(modifier, signature + 3);
        ASSERT_GT(compressed_size, 0u);
        ASSERT_LE(compressed_size, 4u);
        signature[compressed_size + 3] = ELEMENT_TYPE_VOID;
        ULONG actual;
        ASSERT_EQ(S_OK, import->GetNativeCallConvFromSig(signature, compressed_size + 4, &actual));
        EXPECT_EQ(expected, actual);
    };
    ASSERT_NO_FATAL_FAILURE(check_calling_convention(cdecl_type, pmCallConvCdecl));
    ASSERT_NO_FATAL_FAILURE(check_calling_convention(stdcall_type, pmCallConvStdcall));
}
