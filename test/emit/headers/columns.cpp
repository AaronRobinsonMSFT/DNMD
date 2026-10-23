#include <dnmd_columns.hpp>
#include <dnmd_columns.hpp>
#include <gtest/gtest.h>

TEST(CppColumnHeader, StandaloneColumns)
{
    static_assert(std::is_same_v<decltype(dnmd::columns::TypeDef_Flags)::value_type, dnmd::column_type_constant>);
    static_assert(std::is_same_v<decltype(dnmd::columns::ClassLayout_Parent)::value_type, dnmd::raw_cursor<mdtid_TypeDef>>);
    constexpr auto keys = dnmd::detail::table_sort_keys<mdtid_GenericParam>();
    static_assert(std::tuple_size_v<decltype(keys)> == 2);
    EXPECT_EQ(mdtGenericParam_Owner, std::get<0>(keys).column.column_index);
    EXPECT_EQ(mdtGenericParam_Number, std::get<1>(keys).column.column_index);
}
