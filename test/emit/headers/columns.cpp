#include <dnmd_columns.hpp>
#include <dnmd_columns.hpp>
#include <gtest/gtest.h>

TEST(CppColumnHeader, StandaloneColumns)
{
    static_assert(std::is_same_v<decltype(dnmd::columns::TypeDef_Flags)::value_type, dnmd::column_type_constant>);
    static_assert(std::is_same_v<decltype(dnmd::columns::ClassLayout_Parent)::value_type, dnmd::raw_cursor<mdtid_TypeDef>>);
}

TEST(CppColumnHeader, ColumnOffsetEncoding)
{
    static_assert([] {
        for (uint32_t offset = 0; offset <= UINT8_MAX; ++offset)
        {
            if (static_cast<uint32_t>((dnmd::detail::InsertOffset)(offset)) != (offset << 8)
                || (dnmd::detail::ExtractOffset)((dnmd::detail::InsertOffset)(offset)) != offset)
                return false;
        }
        return true;
    }());
    static_assert((dnmd::detail::InsertOffset)(0x100) == mdtc_none);
    static_assert((dnmd::detail::InsertOffset)(UINT32_MAX) == mdtc_comask);
    static_assert((dnmd::detail::ExtractOffset)(UINT32_MAX) == UINT8_MAX);
}
