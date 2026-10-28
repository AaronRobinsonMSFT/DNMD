#include <dnmd_schema.hpp>
#include <dnmd_schema.hpp>
#include <gtest/gtest.h>

TEST(CppSchemaHeader, StandaloneSchema)
{
    using namespace dnmd;
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
