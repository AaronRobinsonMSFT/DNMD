#include <dnmd_search.hpp>
#include <dnmd_write.hpp>
#include <dnmd_read.hpp>
#include <dnmd_cursors.hpp>
#include <dnmd_columns.hpp>
#include <dnmd.hpp>
#include <dnmd_cursors.hpp>
#include <dnmd_write.hpp>
#include "fixture.hpp"

TEST_F(CppHeader, MixedIncludeOrderAndUmbrellaLifetimeWrappers)
{
    dnmd::handle_ptr owned{std::exchange(handle, nullptr)};
    {
        dnmd::added_row_t added;
        ASSERT_TRUE(md_append_row(owned.get(), mdtid_ClassLayout, &added));
        auto row = dnmd::raw_cursor<mdtid_ClassLayout>{added}.as();
        dnmd::row_writer writer{row};
        ASSERT_TRUE(writer.write(dnmd::columns::ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | 1));
        ASSERT_TRUE(row.set_column_value(dnmd::columns::ClassLayout_ClassSize, 32));
    }
    auto position = dnmd::md_create_cursor<mdtid_ClassLayout>(owned.get());
    ASSERT_TRUE(position);
    EXPECT_FALSE(dnmd::detail::CursorTable(position)->is_adding_new_row);
    EXPECT_TRUE(dnmd::md_find_row_from_cursor(position.as(), dnmd::columns::ClassLayout_Parent,
        CreateTokenType(mdtid_TypeDef) | 1));
}
