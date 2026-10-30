#ifndef DNMD_TEST_EMIT_NATIVE_HPP
#define DNMD_TEST_EMIT_NATIVE_HPP

#include <dnmd_write.hpp>
#include <gtest/gtest.h>

namespace dnmd_test
{
    inline void AssertTableOwnership(mdhandle_t handle)
    {
        auto& context = dnmd::detail::context(handle);
        for (uint32_t table = 0; table < mdtid_End; ++table)
        {
            SCOPED_TRACE(table);
            ASSERT_EQ(&context, context.tables[table].cxt);
            ASSERT_EQ(table, context.tables[table].table_id);
        }
    }

    template<typename Position>
    mdToken Token(Position const& position)
    {
        mdToken token = 0;
        EXPECT_TRUE(position.get_token(token));
        return token;
    }

    template<mdtable_id_t TableId>
    dnmd::cursor<TableId> AppendRow(mdhandle_t handle)
    {
        auto added = dnmd::added_row<TableId>::append(handle);
        EXPECT_TRUE(added);
        auto row = added.value();
        added.commit();
        return row;
    }

    template<mdtable_id_t Owner, mdtable_id_t Direct, mdtable_id_t Pointer>
    dnmd::cursor<Direct> AddListRow(dnmd::cursor<Owner> owner,
        dnmd::column<Owner, dnmd::indirect_cursor<Direct, Pointer>> column)
    {
        auto added = dnmd::md_add_new_row_to_list(owner, column);
        EXPECT_TRUE(added);
        auto row = added.value();
        added.commit();
        return row;
    }

    template<mdtable_id_t Owner, mdtable_id_t Direct>
    dnmd::cursor<Direct> AddListRow(dnmd::cursor<Owner> owner,
        dnmd::column<Owner, dnmd::raw_cursor<Direct>> column)
    {
        auto added = dnmd::added_row<Direct>::in_list(owner, column);
        EXPECT_TRUE(added);
        auto row = added.value();
        added.commit();
        return row;
    }
}

#endif
