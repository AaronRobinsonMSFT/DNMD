#include <dnmd_columns.hpp>
#include <dnmd_columns.hpp>
#include <gtest/gtest.h>

TEST(CppColumnHeader, StandaloneColumns)
{
    static_assert(std::is_same_v<decltype(dnmd::columns::TypeDef_Flags)::value_type, dnmd::column_type_constant>);
    static_assert(std::is_same_v<decltype(dnmd::columns::ClassLayout_Parent)::value_type, dnmd::raw_cursor<mdtid_TypeDef>>);
}
