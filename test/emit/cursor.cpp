#include <dnmd.hpp>
#include "emit.hpp"
#include <algorithm>
#include <vector>

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
    using type_position = dnmd::raw_cursor<mdtid_TypeDef>;
    using type_reference = dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>;
    using method_position = dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>;

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
    using type_reader = dnmd::row_reader<mdtid_TypeDef>;
    static_assert(!std::is_default_constructible_v<type_reader>);
    static_assert(std::is_trivially_copyable_v<type_reader>);
    static_assert(std::is_constructible_v<type_reader, type_row const&>);
    static_assert(!std::is_constructible_v<type_reader, type_position>);
    static_assert(!std::is_constructible_v<type_reader, dnmd::cursor<mdtid_Module>>);
    static_assert(!std::is_constructible_v<type_reader, type_row, decltype(dnmd::columns::Module_Name)>);

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

    template<mdtable_id_t TableId>
    void ReadSavedRowCount(IMetaDataEmit* emit, uint32_t& count)
    {
        ULONG size;
        ASSERT_EQ(S_OK, emit->GetSaveSize(cssAccurate, &size));
        std::vector<uint8_t> bytes(size);
        ASSERT_EQ(S_OK, emit->SaveToMemory(bytes.data(), size));
        mdhandle_t raw;
        ASSERT_TRUE(md_create_handle(bytes.data(), bytes.size(), &raw));
        dnmd::handle_ptr saved{raw};
        ASSERT_TRUE(dnmd::md_create_cursor<TableId>(saved.get(), &count));
    }

    template<mdtable_id_t TableId, typename ColumnType>
    mdtcol_t ColumnDetails(dnmd::cursor<TableId> row, dnmd::column<TableId, ColumnType> column)
    {
        mdcursor_t raw = row;
        auto table = dnmd::detail::CursorTable(raw);
        return table->column_details[static_cast<uint8_t>(column.column_index)];
    }

    template<mdtable_id_t TableId, typename ColumnType>
    void CorruptColumn(dnmd::cursor<TableId> row, dnmd::column<TableId, ColumnType> column, uint32_t value)
    {
        mdcursor_t raw = row;
        auto table = dnmd::detail::CursorTable(raw);
        auto details = ColumnDetails(row, column);
        auto bytes = const_cast<uint8_t*>(table->data.ptr)
            + size_t{dnmd::detail::CursorRow(raw) - 1} * table->row_size_bytes
            + ((static_cast<uint32_t>(details) & mdtc_comask) >> 8);
        uint32_t width = (details & mdtc_b4) ? 4 : 2;
        for (uint32_t i = 0; i < width; ++i)
            bytes[i] = static_cast<uint8_t>(value >> (8 * i));
    }
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

TEST_F(CppCursor, ReadContextWidths)
{
    mdcursor_t type{};
    ASSERT_NO_FATAL_FAILURE(AddType("Reader", type));
    auto row = type_position{type}.as();
    ASSERT_TRUE(row.set_column_value(TypeDef_Flags, 0x89abcdef));
    dnmd::row_reader<mdtid_TypeDef> reader{row};
    uint32_t value;
    ASSERT_TRUE(reader.read(TypeDef_Flags, &value));
    EXPECT_EQ(0x89abcdefu, value);
    EXPECT_TRUE(ColumnDetails(row, TypeDef_Flags) & mdtc_b4);
    uint32_t c_value;
    ASSERT_TRUE(md_get_column_value_as_constant(type, mdtTypeDef_Flags, &c_value));
    ASSERT_TRUE(reader.read(TypeDef_Flags, &value));
    EXPECT_EQ(c_value, value);
    char const* name;
    ASSERT_TRUE(reader.read(TypeDef_TypeName, &name));
    EXPECT_STREQ("Reader", name);

    mdcursor_t field;
    ASSERT_TRUE(md_add_new_row_to_list(type, mdtTypeDef_FieldList, &field));
    md_commit_row_add(field);
    auto field_row = dnmd::raw_cursor<mdtid_Field>{field}.as();
    ASSERT_TRUE(field_row.set_column_value(Field_Flags, 0x1234));
    dnmd::row_reader<mdtid_Field> field_reader{field_row};
    EXPECT_TRUE(ColumnDetails(field_row, Field_Flags) & mdtc_b2);
    ASSERT_TRUE(field_reader.read(Field_Flags, &value));
    EXPECT_EQ(0x1234u, value);
}

TEST_F(CppCursor, ReadContextHeapBounds)
{
    auto module = dnmd::md_create_cursor<mdtid_Module>(handle.get()).as();
    ASSERT_TRUE(module.set_column_value(Module_Name, "Reader.dll"));
    dnmd::row_reader<mdtid_Module> reader{module};
    char const* name;
    ASSERT_TRUE(reader.read(Module_Name, &name));
    EXPECT_STREQ("Reader.dll", name);
    CorruptColumn(module, Module_Name, 0xffff);
    EXPECT_FALSE(module.get_column_value(Module_Name, &name));
    CorruptColumn(module, Module_Mvid, 0xffff);
    mdguid_t guid;
    EXPECT_FALSE(module.get_column_value(Module_Mvid, &guid));

    mdcursor_t raw;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_TypeSpec, &raw));
    md_commit_row_add(raw);
    auto spec = dnmd::raw_cursor<mdtid_TypeSpec>{raw}.as();
    uint8_t signature[] = {ELEMENT_TYPE_SZARRAY, ELEMENT_TYPE_I4};
    ASSERT_TRUE(spec.set_column_value(TypeSpec_Signature, signature, sizeof(signature)));
    uint8_t const* blob;
    uint32_t length;
    ASSERT_TRUE(spec.get_column_value(TypeSpec_Signature, &blob, &length));
    ASSERT_EQ(sizeof(signature), length);
    const_cast<uint8_t*>(blob)[-1] = 0x7f;
    EXPECT_FALSE(spec.get_column_value(TypeSpec_Signature, &blob, &length));
    EXPECT_FALSE(md_get_column_value_as_blob(raw, mdtTypeSpec_Signature, &blob, &length));
    CorruptColumn(spec, TypeSpec_Signature, 0xffff);
    EXPECT_FALSE(spec.get_column_value(TypeSpec_Signature, &blob, &length));
}

TEST_F(CppCursor, ReadContextReferenceValidation)
{
    mdcursor_t raw;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_CustomAttribute, &raw));
    md_commit_row_add(raw);
    auto attribute = dnmd::raw_cursor<mdtid_CustomAttribute>{raw}.as();
    dnmd::row_reader<mdtid_CustomAttribute> reader{attribute};
    mdToken token = 0x12345678;
    EXPECT_FALSE(reader.read(CustomAttribute_Type, token)); // Reserved CustomAttributeType tag zero.
    EXPECT_EQ(0x12345678u, token);
    CorruptColumn(attribute, CustomAttribute_Type, 2);
    decltype(CustomAttribute_Type)::value_type constructor;
    ASSERT_TRUE(attribute.get_column_value(CustomAttribute_Type, constructor));
    EXPECT_FALSE(constructor);
    ASSERT_TRUE(constructor.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), token);

    ASSERT_TRUE(md_append_row(handle.get(), mdtid_ClassLayout, &raw));
    md_commit_row_add(raw);
    auto layout = dnmd::raw_cursor<mdtid_ClassLayout>{raw}.as();
    CorruptColumn(layout, ClassLayout_Parent, 3); // Beyond the TypeDef table's end position.
    EXPECT_TRUE(layout.get_column_value(ClassLayout_Parent, token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 3, token);
    type_position parent;
    EXPECT_FALSE(layout.get_column_value(ClassLayout_Parent, parent));
}

TEST_F(CppCursor, ReadContextLargeHeapIndices)
{
    auto module = dnmd::md_create_cursor<mdtid_Module>(handle.get()).as();
    std::string large_name(70000, 'x');
    ASSERT_TRUE(module.set_column_value(Module_Name, large_name.c_str()));
    ASSERT_TRUE(module.set_column_value(Module_Name, "AfterLargeString"));
    dnmd::row_reader<mdtid_Module> reader{module};
    ASSERT_TRUE(ColumnDetails(module, Module_Name) & mdtc_b4);
    char const* name;
    ASSERT_TRUE(reader.read(Module_Name, &name));
    EXPECT_STREQ("AfterLargeString", name);

    mdcursor_t raw;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_TypeSpec, &raw));
    md_commit_row_add(raw);
    auto spec = dnmd::raw_cursor<mdtid_TypeSpec>{raw}.as();
    std::vector<uint8_t> large_blob(70000, 0x5a);
    ASSERT_TRUE(spec.set_column_value(TypeSpec_Signature, large_blob.data(), static_cast<uint32_t>(large_blob.size())));
    uint8_t signature[] = {ELEMENT_TYPE_I4};
    ASSERT_TRUE(spec.set_column_value(TypeSpec_Signature, signature, sizeof(signature)));
    dnmd::row_reader<mdtid_TypeSpec> spec_reader{spec};
    ASSERT_TRUE(ColumnDetails(spec, TypeSpec_Signature) & mdtc_b4);
    uint8_t const* blob;
    uint32_t length;
    ASSERT_TRUE(spec_reader.read(TypeSpec_Signature, &blob, &length));
    ASSERT_EQ(1u, length);
    EXPECT_EQ(signature[0], blob[0]);
}

TEST_F(CppCursor, ReadContextRangeGapsAndDescendingBounds)
{
    mdcursor_t first{};
    ASSERT_NO_FATAL_FAILURE(AddIndirectMethods(first));
    mdcursor_t last{};
    ASSERT_NO_FATAL_FAILURE(AddType("Last", last));
    auto first_row = type_position{first}.as();
    auto middle = dnmd::md_token_to_cursor<mdtid_TypeDef>(handle.get(), CreateTokenType(mdtid_TypeDef) | 3).as();
    CorruptColumn(middle, TypeDef_MethodList, 0);
    method_position range;
    uint32_t count;
    ASSERT_TRUE(first_row.get_column_value_as_range(TypeDef_MethodList, range, &count));
    EXPECT_EQ(3u, count);
    CorruptColumn(first_row, TypeDef_MethodList, 2);
    CorruptColumn(type_position{last}.as(), TypeDef_MethodList, 1);
    EXPECT_FALSE(first_row.get_column_value_as_range(TypeDef_MethodList, range, &count));
}

TEST_F(CppCursor, ReadContextBulkLimits)
{
    auto owner = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get()).as();
    for (uint32_t i = 0; i < 130; ++i)
    {
        mdcursor_t raw;
        ASSERT_TRUE(md_append_row(handle.get(), mdtid_MemberRef, &raw));
        ASSERT_TRUE((dnmd::raw_cursor<mdtid_MemberRef>{raw}.as().set_column_value(MemberRef_Class, owner)));
        md_commit_row_add(raw);
    }
    auto rows = dnmd::md_create_cursor<mdtid_MemberRef>(handle.get());
    ASSERT_TRUE(rows);
    std::array<mdToken, 66> values;
    values.fill(0x12345678);
    ASSERT_EQ(64, rows.as().get_column_values(MemberRef_Class, 64, values.data()));
    EXPECT_TRUE(std::all_of(values.begin(), values.begin() + 64,
        [](mdToken value) { return value == (CreateTokenType(mdtid_TypeDef) | 1); }));
    EXPECT_EQ(0x12345678u, values[64]);
    ASSERT_TRUE(dnmd::md_cursor_move(rows, 129));
    values.fill(0x12345678);
    ASSERT_EQ(1, rows.as().get_column_values(MemberRef_Class, 64, values.data()));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 1, values[0]);
    EXPECT_EQ(0x12345678u, values[1]);
    EXPECT_EQ(0, rows.as().get_column_values(MemberRef_Class, 0, nullptr));
}

TEST_F(CppCursor, WriteScalarColumns)
{
    auto const module = dnmd::md_create_cursor<mdtid_Module>(handle.get()).as();
    ASSERT_TRUE(module.set_column_value(Module_Name, "Written.dll"));
    char const* name;
    ASSERT_TRUE(module.get_column_value(Module_Name, &name));
    EXPECT_STREQ("Written.dll", name);

    mdguid_t expected{0x12345678, 0x2345, 0x3456, {1, 2, 3, 4, 5, 6, 7, 8}};
    ASSERT_TRUE(module.set_column_value(Module_Mvid, expected));
    mdguid_t actual;
    ASSERT_TRUE(module.get_column_value(Module_Mvid, &actual));
    EXPECT_EQ(expected.data1, actual.data1);
    EXPECT_EQ(expected.data2, actual.data2);
    EXPECT_EQ(expected.data3, actual.data3);
    EXPECT_TRUE(std::equal(expected.data4, expected.data4 + 8, actual.data4));

    mdcursor_t type{};
    ASSERT_NO_FATAL_FAILURE(AddType("WrittenType", type));
    auto row = type_position{type}.as();
    ASSERT_TRUE(row.set_column_value(TypeDef_Flags, tdPublic));
    uint32_t flags;
    ASSERT_TRUE(row.get_column_value(TypeDef_Flags, &flags));
    EXPECT_EQ(static_cast<uint32_t>(tdPublic), flags);
    static_assert(noexcept(row.set_column_value(TypeDef_Flags, 0)));
    static_assert(noexcept(module.set_column_value(Module_Mvid, expected)));
    static_assert(noexcept(module.set_column_value(Module_Name, "")));
    static_assert(noexcept(module.set_column_value(
        std::declval<dnmd::column<mdtid_Module, dnmd::column_type_userstring>>(), u"")));

    mdcursor_t specification;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_TypeSpec, &specification));
    md_commit_row_add(specification);
    auto spec = dnmd::raw_cursor<mdtid_TypeSpec>{specification}.as();
    uint8_t signature[] = {ELEMENT_TYPE_SZARRAY, ELEMENT_TYPE_I4};
    ASSERT_TRUE(spec.set_column_value(TypeSpec_Signature, signature, sizeof(signature)));
    uint8_t const* blob;
    uint32_t size;
    ASSERT_TRUE(spec.get_column_value(TypeSpec_Signature, &blob, &size));
    ASSERT_EQ(sizeof(signature), size);
    EXPECT_TRUE(std::equal(signature, signature + size, blob));
    ASSERT_TRUE(spec.set_column_value(TypeSpec_Signature, nullptr, 0));
    ASSERT_TRUE(spec.get_column_value(TypeSpec_Signature, &blob, &size));
    EXPECT_EQ(0u, size);
}

TEST_F(CppCursor, WriteReferenceColumns)
{
    mdcursor_t base{};
    mdcursor_t derived{};
    ASSERT_NO_FATAL_FAILURE(AddType("Base", base));
    ASSERT_NO_FATAL_FAILURE(AddType("Derived", derived));
    auto row = type_position{derived}.as();
    auto base_position = type_position{base};
    mdToken base_token;
    ASSERT_TRUE(base_position.get_token(base_token));
    ASSERT_TRUE(row.set_column_value(TypeDef_Extends, base_position.as()));
    mdToken actual;
    ASSERT_TRUE(row.get_column_value(TypeDef_Extends, actual));
    EXPECT_EQ(base_token, actual);
    ASSERT_TRUE(row.set_column_value(TypeDef_Extends, base_position));
    dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef> base_reference{base};
    ASSERT_TRUE(row.set_column_value(TypeDef_Extends, base_reference));
    EXPECT_FALSE(row.set_column_value(TypeDef_Extends, CreateTokenType(mdtid_Field) | 1));
    ASSERT_TRUE(row.get_column_value(TypeDef_Extends, actual));
    EXPECT_EQ(base_token, actual);

    ASSERT_TRUE(row.set_column_value(TypeDef_Extends, CreateTokenType(mdtid_TypeDef)));
    type_reference nil;
    ASSERT_TRUE(row.get_column_value(TypeDef_Extends, nil));
    EXPECT_FALSE(nil);
    ASSERT_TRUE(row.set_column_value(TypeDef_Extends, nil.get_as<mdtid_TypeDef>()));
    ASSERT_TRUE(row.get_column_value(TypeDef_Extends, actual));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef), actual);
    EXPECT_FALSE(row.set_column_value(TypeDef_Extends, dnmd::raw_cursor<mdtid_TypeRef>{}));
}

TEST_F(CppCursor, WriteDirectListPositions)
{
    mdcursor_t owner{};
    mdcursor_t empty{};
    ASSERT_NO_FATAL_FAILURE(AddType("Owner", owner));
    ASSERT_NO_FATAL_FAILURE(AddMethod(owner, "Method"));
    ASSERT_NO_FATAL_FAILURE(AddType("Empty", empty));
    auto row = type_position{owner}.as();
    auto empty_row = type_position{empty}.as();
    method_position first;
    method_position end;
    ASSERT_TRUE(row.get_column_value(TypeDef_MethodList, first));
    ASSERT_TRUE(empty_row.get_column_value(TypeDef_MethodList, end));
    ASSERT_TRUE(first);
    EXPECT_FALSE(end);
    ASSERT_TRUE(row.set_column_value(TypeDef_MethodList, first.as()));
    ASSERT_TRUE(row.set_column_value(TypeDef_MethodList,
        dnmd::raw_cursor<mdtid_MethodDef>{static_cast<mdcursor_t>(first)}));
    ASSERT_TRUE(empty_row.set_column_value(TypeDef_MethodList, end));
    ASSERT_TRUE(empty_row.set_column_value(TypeDef_MethodList,
        dnmd::raw_cursor<mdtid_MethodDef>{static_cast<mdcursor_t>(end)}));

    ASSERT_TRUE(empty_row.set_column_value(TypeDef_MethodList, CreateTokenType(mdtid_MethodDef)));
    method_position nil;
    ASSERT_TRUE(empty_row.get_column_value(TypeDef_MethodList, nil));
    EXPECT_FALSE(nil);
    ASSERT_TRUE(empty_row.set_column_value(TypeDef_MethodList, nil));
    ASSERT_TRUE(empty_row.set_column_value(TypeDef_MethodList,
        dnmd::raw_cursor<mdtid_MethodDef>{static_cast<mdcursor_t>(nil)}));
    mdToken token;
    ASSERT_TRUE(nil.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), token);
    ASSERT_TRUE(empty_row.set_column_value(TypeDef_MethodList, end));
    method_position read;
    ASSERT_TRUE(empty_row.get_column_value(TypeDef_MethodList, read));
    EXPECT_FALSE(read);
    ASSERT_TRUE(read.get_target_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 2, token);
}

TEST_F(CppCursor, WriteIndirectSourcesAndTargets)
{
    mdcursor_t owner{};
    ASSERT_NO_FATAL_FAILURE(AddIndirectMethods(owner));
    auto row = type_position{owner}.as();
    method_position methods;
    ASSERT_TRUE(row.get_column_value(TypeDef_MethodList, methods));
    ASSERT_TRUE(methods);
    mdToken source;
    mdToken target;
    ASSERT_TRUE(methods.get_token(source));
    ASSERT_TRUE(methods.get_target_token(target));
    ASSERT_EQ(mdtid_MethodPtr, ExtractTokenType(source));
    ASSERT_TRUE(row.set_column_value(TypeDef_MethodList, methods));
    ASSERT_TRUE(row.set_column_value(TypeDef_MethodList,
        dnmd::raw_cursor<mdtid_MethodDef, mdtid_MethodPtr>{static_cast<mdcursor_t>(methods)}));
    ASSERT_TRUE(row.set_column_value(TypeDef_MethodList,
        dnmd::raw_cursor<mdtid_MethodPtr>{static_cast<mdcursor_t>(methods)}.as()));
    method_position read;
    ASSERT_TRUE(row.get_column_value(TypeDef_MethodList, read));
    mdToken token;
    ASSERT_TRUE(read.get_token(token));
    EXPECT_EQ(source, token);

    mdcursor_t implementation;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_MethodImpl, &implementation));
    auto impl = dnmd::raw_cursor<mdtid_MethodImpl>{implementation}.as();
    ASSERT_TRUE(impl.set_column_value(MethodImpl_Class, row));
    ASSERT_TRUE(impl.set_column_value(MethodImpl_MethodBody, methods));
    ASSERT_TRUE(impl.set_column_value(MethodImpl_MethodDeclaration, methods.as()));
    md_commit_row_add(implementation);
    ASSERT_TRUE(impl.get_column_value(MethodImpl_MethodBody, token));
    EXPECT_EQ(target, token);
    EXPECT_FALSE(impl.set_column_value(MethodImpl_MethodBody, method_position{}));
    ASSERT_TRUE(impl.get_column_value(MethodImpl_MethodBody, token));
    EXPECT_EQ(target, token);

    auto end = methods;
    ASSERT_TRUE(dnmd::md_cursor_move(end, 3));
    EXPECT_FALSE(end);
    ASSERT_TRUE(impl.set_column_value(MethodImpl_MethodBody, end));
    ASSERT_TRUE(impl.get_column_value(MethodImpl_MethodBody, token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 4, token);
    ASSERT_TRUE(row.set_column_value(TypeDef_MethodList, CreateTokenType(mdtid_MethodPtr)));
    method_position nil;
    ASSERT_TRUE(row.get_column_value(TypeDef_MethodList, nil));
    EXPECT_FALSE(nil);
    ASSERT_TRUE(impl.set_column_value(MethodImpl_MethodBody, nil));
    ASSERT_TRUE(impl.get_column_value(MethodImpl_MethodBody, token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), token);
    ASSERT_TRUE(row.set_column_value(TypeDef_MethodList, methods));
    ASSERT_TRUE(impl.set_column_value(MethodImpl_MethodBody, methods));
}

TEST_F(CppCursor, WriteEmptyAddedRowGuard)
{
    {
        dnmd::added_row_t empty;
    }
    auto position = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get());
    EXPECT_TRUE(position);
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
    static_assert(std::is_same_v<decltype(match), dnmd::raw_cursor<mdtid_NestedClass>>);
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

    dnmd::raw_cursor<mdtid_NestedClass> range;
    ASSERT_EQ(dnmd::range_result::found, dnmd::md_find_range_from_cursor(rows.as(), NestedClass_NestedClass, child_token, range, &count));
    EXPECT_EQ(1u, count);
    ASSERT_TRUE(range);
    EXPECT_EQ(dnmd::range_result::not_found, dnmd::md_find_range_from_cursor(rows.as(), NestedClass_NestedClass,
        CreateTokenType(mdtid_TypeDef) | 100, range, &count));
    EXPECT_EQ(dnmd::range_result::not_found, dnmd::md_find_range_from_cursor(rows.as(), NestedClass_NestedClass,
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
    static_assert(std::is_same_v<decltype(match), dnmd::raw_cursor<mdtid_TypeRef>>);
    EXPECT_TRUE(match);
    EXPECT_TRUE(dnmd::md_find_row_from_cursor(rows.as(), TypeRef_ResolutionScope, CreateTokenType(mdtid_Module) | 1));
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(rows.as(), TypeRef_ResolutionScope, CreateTokenType(mdtid_AssemblyRef) | 1));
    dnmd::raw_cursor<mdtid_TypeRef> range;
    EXPECT_EQ(dnmd::range_result::not_supported, dnmd::md_find_range_from_cursor(rows.as(), TypeRef_ResolutionScope,
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

TEST(CppCursorEmitWrites, ParentedRowsUpdateInPlace)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdTypeDef type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Layout"), tdSequentialLayout, mdTypeDefNil, nullptr, &type));
    uint8_t signature[] = {IMAGE_CEE_CS_CALLCONV_FIELD, ELEMENT_TYPE_I4};
    mdFieldDef field;
    ASSERT_EQ(S_OK, emit->DefineField(type, W("Field"), fdPublic, signature,
        sizeof(signature), ELEMENT_TYPE_VOID, nullptr, 0, &field));
    ASSERT_EQ(S_OK, emit->SetFieldRVA(field, 0x1000));
    ASSERT_EQ(S_OK, emit->SetFieldRVA(field, 0x2000));
    ASSERT_EQ(S_OK, emit->SetClassLayout(type, 8, nullptr, 32));
    ASSERT_EQ(S_OK, emit->SetClassLayout(type, 4, nullptr, 64));

    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    ULONG rva;
    ASSERT_EQ(S_OK, import->GetRVA(field, &rva, nullptr));
    EXPECT_EQ(0x2000u, rva);
    DWORD packing;
    ULONG size;
    ULONG fields;
    ASSERT_EQ(S_OK, import->GetClassLayout(type, &packing, nullptr, 0, &fields, &size));
    EXPECT_EQ(4u, packing);
    EXPECT_EQ(64u, size);
    uint32_t count = 0;
    ASSERT_NO_FATAL_FAILURE(ReadSavedRowCount<mdtid_FieldRva>(emit, count));
    EXPECT_EQ(1u, count);
    ASSERT_NO_FATAL_FAILURE(ReadSavedRowCount<mdtid_ClassLayout>(emit, count));
    EXPECT_EQ(1u, count);
    ASSERT_EQ(S_OK, emit->DeleteClassLayout(type));
    EXPECT_EQ(CLDB_E_RECORD_NOTFOUND, import->GetClassLayout(type, &packing, nullptr, 0, &fields, &size));
}

TEST(CppCursorEmitWrites, ParentedRowsCanBeUpdatedAndDeleted)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    uint8_t signature[] = {IMAGE_CEE_CS_CALLCONV_FIELD, ELEMENT_TYPE_I4};
    mdFieldDef field;
    ASSERT_EQ(S_OK, emit->DefineField(TokenFromRid(1, mdtTypeDef), W("Field"), fdPublic, signature,
        sizeof(signature), ELEMENT_TYPE_VOID, nullptr, 0, &field));
    uint8_t first[] = {NATIVE_TYPE_I4};
    uint8_t second[] = {NATIVE_TYPE_I8};
    ASSERT_EQ(S_OK, emit->SetFieldMarshal(field, first, sizeof(first)));
    ASSERT_EQ(S_OK, emit->SetFieldMarshal(field, second, sizeof(second)));
    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    PCCOR_SIGNATURE read;
    ULONG size;
    ASSERT_EQ(S_OK, import->GetFieldMarshal(field, &read, &size));
    ASSERT_EQ(sizeof(second), size);
    EXPECT_EQ(second[0], read[0]);
    uint32_t count = 0;
    ASSERT_NO_FATAL_FAILURE(ReadSavedRowCount<mdtid_FieldMarshal>(emit, count));
    EXPECT_EQ(1u, count);
    ASSERT_EQ(S_OK, emit->DeleteFieldMarshal(field));
    EXPECT_EQ(CLDB_E_RECORD_NOTFOUND, import->GetFieldMarshal(field, &read, &size));

    mdModuleRef module;
    ASSERT_EQ(S_OK, emit->DefineModuleRef(W("Native"), &module));
    ASSERT_EQ(S_OK, emit->DefinePinvokeMap(field, 0, W("Entry"), module));
    ASSERT_EQ(S_OK, emit->DeletePinvokeMap(field));
    DWORD flags;
    WCHAR name[16];
    ULONG length;
    mdModuleRef imported_module;
    EXPECT_EQ(CLDB_E_RECORD_NOTFOUND, import->GetPinvokeMap(field, &flags, name, 16, &length, &imported_module));
}

TEST(CppCursorEmitWrites, GenericConstraintsAreWrittenAndReplaced)
{
    dncp::com_ptr<IMetaDataEmit2> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdTypeDef owner;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Generic`1"), 0, mdTypeDefNil, nullptr, &owner));
    mdTypeRef first;
    mdTypeRef second;
    ASSERT_EQ(S_OK, emit->DefineTypeRefByName(TokenFromRid(1, mdtModule), W("First"), &first));
    ASSERT_EQ(S_OK, emit->DefineTypeRefByName(TokenFromRid(1, mdtModule), W("Second"), &second));
    mdToken constraints[] = {first, mdTokenNil};
    mdGenericParam parameter;
    ASSERT_EQ(S_OK, emit->DefineGenericParam(owner, 0, 0, W("T"), 0, constraints, &parameter));
    dncp::com_ptr<IMetaDataImport2> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport2, (void**)&import));
    auto check_constraint = [&](mdToken expected) {
        HCORENUM enumeration = nullptr;
        mdGenericParamConstraint values[2]{};
        ULONG count;
        HRESULT hr = import->EnumGenericParamConstraints(&enumeration, parameter, values, 2, &count);
        import->CloseEnum(enumeration);
        ASSERT_EQ(S_OK, hr);
        ASSERT_EQ(1u, count);
        mdGenericParam read_owner;
        mdToken constraint;
        ASSERT_EQ(S_OK, import->GetGenericParamConstraintProps(values[0], &read_owner, &constraint));
        EXPECT_EQ(parameter, read_owner);
        EXPECT_EQ(expected, constraint);
    };
    ASSERT_NO_FATAL_FAILURE(check_constraint(first));
    constraints[0] = second;
    ASSERT_EQ(S_OK, emit->SetGenericParamProps(parameter, 0, W("T"), 0, constraints));
    ASSERT_NO_FATAL_FAILURE(check_constraint(second));
    constraints[0] = mdTokenNil;
    ASSERT_EQ(S_OK, emit->SetGenericParamProps(parameter, 0, W("T"), 0, constraints));
    HCORENUM enumeration = nullptr;
    mdGenericParamConstraint value;
    ULONG count;
    HRESULT hr = import->EnumGenericParamConstraints(&enumeration, parameter, &value, 1, &count);
    import->CloseEnum(enumeration);
    EXPECT_EQ(S_FALSE, hr);
    EXPECT_EQ(0u, count);
}

TEST(CppCursorEmitWrites, ExportedTypeIdIsWrittenAsAConstant)
{
    dncp::com_ptr<IMetaDataAssemblyEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdFile file;
    ASSERT_EQ(S_OK, emit->DefineFile(W("Other.netmodule"), nullptr, 0, 0, &file));
    mdExportedType type;
    ASSERT_EQ(S_OK, emit->DefineExportedType(W("Exported"), file, TokenFromRid(1, mdtTypeDef), tdPublic, &type));
    mdTypeDef expected = TokenFromRid(2, mdtTypeDef);
    ASSERT_EQ(S_OK, emit->SetExportedTypeProps(type, file, expected, tdPublic));
    dncp::com_ptr<IMetaDataAssemblyImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataAssemblyImport, (void**)&import));
    mdToken implementation;
    mdTypeDef actual;
    DWORD flags;
    ASSERT_EQ(S_OK, import->GetExportedTypeProps(type, nullptr, 0, nullptr, &implementation, &actual, &flags));
    EXPECT_EQ(expected, actual);
    EXPECT_EQ(file, implementation);
}

TEST(CppCursorImport, MultiColumnLookupsAdvanceReadersWithTheirRows)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdTypeDef first_type;
    mdTypeDef second_type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("First"), tdPublic, mdTypeDefNil, nullptr, &first_type));
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Second"), tdPublic, mdTypeDefNil, nullptr, &second_type));
    uint8_t signature[] = {IMAGE_CEE_CS_CALLCONV_DEFAULT, 0, ELEMENT_TYPE_VOID};
    mdMethodDef other_method;
    mdMethodDef target_method;
    ASSERT_EQ(S_OK, emit->DefineMethod(first_type, W("Other"), mdPublic | mdStatic,
        signature, sizeof(signature), 0, 0, &other_method));
    ASSERT_EQ(S_OK, emit->DefineMethod(first_type, W("Target"), mdPublic | mdStatic,
        signature, sizeof(signature), 0, 0, &target_method));
    uint8_t field_signature[] = {IMAGE_CEE_CS_CALLCONV_FIELD, ELEMENT_TYPE_I4};
    mdFieldDef other_field;
    mdFieldDef target_field;
    ASSERT_EQ(S_OK, emit->DefineField(first_type, W("Other"), fdPublic,
        field_signature, sizeof(field_signature), ELEMENT_TYPE_VOID, nullptr, 0, &other_field));
    ASSERT_EQ(S_OK, emit->DefineField(first_type, W("Target"), fdPublic,
        field_signature, sizeof(field_signature), ELEMENT_TYPE_VOID, nullptr, 0, &target_field));
    mdMemberRef other_ref;
    mdMemberRef target_ref;
    ASSERT_EQ(S_OK, emit->DefineMemberRef(first_type, W("Other"),
        signature, sizeof(signature), &other_ref));
    ASSERT_EQ(S_OK, emit->DefineMemberRef(first_type, W("Target"),
        signature, sizeof(signature), &target_ref));
    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    mdTypeDef found_type;
    ASSERT_EQ(S_OK, import->FindTypeDefByName(W("Second"), mdTokenNil, &found_type));
    EXPECT_EQ(second_type, found_type);
    mdMethodDef found_method;
    ASSERT_EQ(S_OK, import->FindMethod(first_type, W("Target"), signature, sizeof(signature), &found_method));
    EXPECT_EQ(target_method, found_method);
    mdFieldDef found_field;
    ASSERT_EQ(S_OK, import->FindField(first_type, W("Target"),
        field_signature, sizeof(field_signature), &found_field));
    EXPECT_EQ(target_field, found_field);
    mdMemberRef found_ref;
    ASSERT_EQ(S_OK, import->FindMemberRef(first_type, W("Target"), signature, sizeof(signature), &found_ref));
    EXPECT_EQ(target_ref, found_ref);
}

TEST(CppCursorImport, ReusesAccessorsAcrossLargeStringProperties)
{
    dncp::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    WSTR_string name(4000, static_cast<WCHAR>('N'));
    name += W(".LongType");
    mdTypeDef type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(name.c_str(), tdPublic | tdAbstract, mdTypeDefNil, nullptr, &type));
    dncp::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    WSTR_string actual(name.size() + 1, 0);
    ULONG length;
    DWORD flags;
    mdToken extends;
    ASSERT_EQ(S_OK, import->GetTypeDefProps(type, actual.data(), static_cast<ULONG>(actual.size()),
        &length, &flags, &extends));
    ASSERT_EQ(name.size() + 1, length);
    EXPECT_EQ(0, std::memcmp(name.data(), actual.data(), name.size() * sizeof(WCHAR)));
    EXPECT_EQ(static_cast<DWORD>(tdPublic | tdAbstract), flags);
    EXPECT_EQ(mdTypeRefNil, extends);
    mdTypeDef found;
    ASSERT_EQ(S_OK, import->FindTypeDefByName(name.c_str(), mdTokenNil, &found));
    EXPECT_EQ(type, found);
}
