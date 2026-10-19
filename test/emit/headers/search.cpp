#include <dnmd_search.hpp>
#include <dnmd_search.hpp>
#include "fixture.hpp"

TEST_F(CppHeader, StandaloneSearch)
{
    auto type = dnmd::raw_cursor<mdtid_TypeDef>{Row<mdtid_TypeDef>()}.as();
    mdcursor_t added;
    ASSERT_TRUE(md_append_row(handle, mdtid_GenericParam, &added));
    mdToken owner;
    ASSERT_TRUE(type.get_token(owner));
    ASSERT_TRUE(md_set_column_value_as_token(added, mdtGenericParam_Owner, owner));
    md_commit_row_add(added);
    auto first = dnmd::md_create_cursor<mdtid_GenericParam>(handle).as();
    EXPECT_TRUE(dnmd::md_find_row_from_cursor(first, dnmd::columns::GenericParam_Owner, type));
    dnmd::raw_cursor<mdtid_GenericParam> start;
    uint32_t count;
    ASSERT_EQ(MD_RANGE_FOUND, dnmd::md_find_range_from_cursor(first, dnmd::columns::GenericParam_Owner, type, start, &count));
    EXPECT_EQ(1u, count);

    ASSERT_TRUE(md_add_new_row_to_list(type, mdtTypeDef_FieldList, &added));
    md_commit_row_add(added);
    auto field = dnmd::raw_cursor<mdtid_Field>{added}.as();
    mdToken parent;
    ASSERT_TRUE(field.get_parent_token(parent));
    EXPECT_EQ(owner, parent);
}
