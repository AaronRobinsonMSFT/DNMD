#include <dnmd_search.hpp>
#include <dnmd_write.hpp>
#include <dnmd_read.hpp>
#include <dnmd_cursors.hpp>
#include <dnmd_schema.hpp>
#include <dnmd.hpp>
#include <dnmd_cursors.hpp>
#include <dnmd_write.hpp>
#include "fixture.hpp"

#if !defined(NDEBUG) && GTEST_HAS_DEATH_TEST
TEST(CppGuardDeathTest, RejectsInvalidHandles)
{
    EXPECT_DEATH((dnmd::added_row<mdtid_ClassLayout>::append(nullptr)), "value");
}
#endif

TEST_F(CppHeader, ExplicitRowGuardCommits)
{
    auto added = dnmd::added_row<mdtid_ClassLayout>::append(handle);
    ASSERT_TRUE(added);
    auto row = added.value();
    dnmd::raw_cursor<mdtid_ClassLayout> live{added};
    dnmd::raw_cursor<mdtid_Module> mismatch{added};
    EXPECT_TRUE(live);
    EXPECT_EQ(dnmd_test::Token(row), dnmd_test::Token(live));
    EXPECT_FALSE(mismatch);
    EXPECT_EQ(handle, mismatch.get_handle());
    EXPECT_EQ(CreateTokenType(mdtid_Module), dnmd_test::Token(mismatch));
    ASSERT_TRUE(row.set_column_value(dnmd::columns::ClassLayout_ClassSize, 32));
    added.commit();
    EXPECT_FALSE(added);
    EXPECT_EQ(handle, added.get_handle());
    EXPECT_EQ(CreateTokenType(mdtid_ClassLayout), dnmd_test::Token(added));
    EXPECT_FALSE(dnmd::detail::CursorTable(row)->is_adding_new_row);
    added.commit();
    EXPECT_FALSE(dnmd::detail::CursorTable(row)->is_adding_new_row);
}

TEST_F(CppHeader, MixedIncludeOrderAndUmbrellaLifetimeWrappers)
{
    dnmd::handle_ptr owned{std::exchange(handle, nullptr)};
    {
        auto added = dnmd::added_row<mdtid_ClassLayout>::append(owned.get());
        ASSERT_TRUE(added);
        auto row = added.value();
        dnmd::row_writer writer{row};
        ASSERT_TRUE(writer.write(dnmd::columns::ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | 1));
        ASSERT_TRUE(row.set_column_value(dnmd::columns::ClassLayout_ClassSize, 32));
    }
    auto position = dnmd::md_create_cursor<mdtid_ClassLayout>(owned.get());
    ASSERT_TRUE(position);
    EXPECT_FALSE(dnmd::detail::CursorTable(position)->is_adding_new_row);
    EXPECT_TRUE(dnmd::md_find_row_from_cursor(position.value(), dnmd::columns::ClassLayout_Parent,
        CreateTokenType(mdtid_TypeDef) | 1));
}
