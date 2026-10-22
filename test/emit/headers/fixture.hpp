#ifndef DNMD_TEST_EMIT_HEADERS_FIXTURE_HPP
#define DNMD_TEST_EMIT_HEADERS_FIXTURE_HPP

#include <dnmd_write.hpp>
#include <gtest/gtest.h>
#include "../native.hpp"

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
    dnmd::cursor<TableId> Row()
    {
        auto row = dnmd::md_create_cursor<TableId>(handle);
        return row ? row.as() : dnmd_test::AppendRow<TableId>(handle);
    }
};

#endif // DNMD_TEST_EMIT_HEADERS_FIXTURE_HPP
