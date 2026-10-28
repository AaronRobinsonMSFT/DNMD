#include <dnmd_schema.hpp>
#include <dnmd_schema.hpp>
#include <gtest/gtest.h>
#include <array>

TEST(CppSchemaHeader, StandaloneSchema)
{
    using namespace dnmd;
    static_assert(detail::column_heap<column_type_constant>() == mdhid_None);
    static_assert(detail::column_heap<column_type_string>() == mdhid_String);
    static_assert(detail::column_heap<column_type_blob>() == mdhid_Blob);
    static_assert(detail::column_heap<column_type_guid>() == mdhid_Guid);
    static_assert(detail::column_heap<column_type_userstring>() == mdhid_UserString);
    static_assert(std::is_same_v<detail::all_tables::value_type, mdtable_id_t>);
    static_assert(detail::all_tables::size() == mdtid_End);
    static_assert(std::is_same_v<typename detail::table_range<mdtid_Module, mdtid_TypeDef>::type,
        detail::table_sequence<mdtid_Module, mdtid_TypeRef>>);
    static_assert(!std::is_convertible_v<std::integer_sequence<int, 0, 1>,
        detail::table_sequence<mdtid_Module, mdtid_TypeRef>>);
    constexpr auto keys = detail::table_sort_keys<mdtid_GenericParam>();
    static_assert(std::tuple_size_v<decltype(keys)> == 2);
    EXPECT_EQ(mdtGenericParam_Owner, std::get<0>(keys).column.column_index);
    EXPECT_EQ(mdtGenericParam_Number, std::get<1>(keys).column.column_index);
    static_assert(detail::for_each_column<mdtid_TypeDef>([](auto... columns) {
        return ((static_cast<uint8_t>(columns.column_index) < 6) && ...);
    }));
    static_assert(detail::is_list_column(columns::TypeDef_FieldList));
    static_assert(!detail::is_list_column(columns::TypeDef_Extends));
}

TEST(CppSchemaHeader, SelectedMetadataPreservesListAndKeyOrder)
{
    using namespace dnmd;
    int next_list = 4;
    EXPECT_TRUE(detail::for_each_column<mdtid_TypeDef>([&](auto... columns) {
        return ((static_cast<uint8_t>(columns.column_index) == next_list++) && ...);
    }, detail::list_columns<mdtid_TypeDef>()));
    EXPECT_EQ(6, next_list);

    auto keys = detail::table_sort_keys<mdtid_GenericParam>();
    int next_key = 0;
    auto check = [&](auto key) {
        auto expected = next_key++ == 0 ? mdtGenericParam_Owner : mdtGenericParam_Number;
        return key.column.column_index == expected && !key.descending;
    };
    EXPECT_TRUE(detail::for_each_column<mdtid_GenericParam>([&](auto... selected) {
        return (check(selected) && ...);
    }, keys));
    EXPECT_EQ(2, next_key);
}

TEST(CppSchemaHeader, StandaloneInitialLayout)
{
    using namespace dnmd;
    static_assert(detail::constant_column_width(columns::MethodDef_Rva) == 4);
    static_assert(detail::constant_column_width(columns::MethodDef_ImplFlags) == 2);
    static_assert(detail::constant_column_width(columns::ClassLayout_ClassSize) == 4);
    static_assert(detail::constant_column_width(columns::ClassLayout_PackingSize) == 2);
    constexpr auto counts = [] {
        std::array<uint32_t, mdtid_End> rows{};
        rows[mdtid_TypeDef] = 1;
        rows[mdtid_TypeSpec] = 0x4000;
        rows[mdtid_MemberRef] = 0x2000;
        return rows;
    }();
    static_assert((detail::initial_column_details(columns::CustomAttribute_Type, counts, mdc_none)
        & mdtc_widthmask) == mdtc_b4);
    std::array<mdtcol_t, 6> columns{};
    mdtable_t table{};
    table.column_details = columns.data();
    ASSERT_TRUE(detail::initialize_table_details<mdtid_TypeDef>(counts, mdc_none, false, table));
    EXPECT_EQ(16u, table.row_size_bytes);
    EXPECT_EQ(mdtc_b4, columns[3] & mdtc_widthmask);
    EXPECT_FALSE(table.is_initialized);
}
