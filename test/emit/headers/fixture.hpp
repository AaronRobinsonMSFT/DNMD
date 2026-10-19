#ifndef DNMD_TEST_EMIT_HEADERS_FIXTURE_HPP
#define DNMD_TEST_EMIT_HEADERS_FIXTURE_HPP

#include <dnmd.h>
#include <gtest/gtest.h>

class CppHeader : public testing::Test
{
protected:
    mdhandle_t handle{};

    void SetUp() override
    {
#ifdef DNMD_PORTABLE_PDB
        handle = md_create_new_pdb_handle();
#else
        handle = md_create_new_handle();
#endif
        ASSERT_NE(nullptr, handle);
    }

    void TearDown() override
    {
        if (handle != nullptr)
            md_destroy_handle(handle);
    }

    template<mdtable_id_t TableId>
    mdcursor_t Row()
    {
        mdcursor_t row{};
        uint32_t count;
        if (!md_create_cursor(handle, TableId, &row, &count))
        {
            EXPECT_TRUE(md_append_row(handle, TableId, &row));
            md_commit_row_add(row);
        }
        return row;
    }
};

#endif // DNMD_TEST_EMIT_HEADERS_FIXTURE_HPP
