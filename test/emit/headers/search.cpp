#include <dnmd_search.hpp>
#include <dnmd_search.hpp>
#include "fixture.hpp"

namespace
{
    static_assert(std::is_enum_v<dnmd::range_result>);
    static_assert(!std::is_convertible_v<dnmd::range_result, int>);
    static_assert(!std::is_convertible_v<dnmd::range_result, md_range_result_t>);
    static_assert(!std::is_convertible_v<md_range_result_t, dnmd::range_result>);
    static_assert(static_cast<int>(dnmd::range_result::found) == MD_RANGE_FOUND);
    static_assert(static_cast<int>(dnmd::range_result::not_found) == MD_RANGE_NOT_FOUND);
    static_assert(static_cast<int>(dnmd::range_result::not_supported) == MD_RANGE_NOT_SUPPORTED);

    template<typename Value>
    using range_return_type = decltype(dnmd::md_find_range_from_cursor(
        std::declval<dnmd::cursor<mdtid_ClassLayout>>(), dnmd::columns::ClassLayout_Parent,
        std::declval<Value>(), std::declval<dnmd::raw_cursor<mdtid_ClassLayout>&>(), std::declval<uint32_t*>()));

    static_assert(std::is_same_v<dnmd::range_result, range_return_type<mdToken>>);
    static_assert(std::is_same_v<dnmd::range_result, range_return_type<dnmd::cursor<mdtid_TypeDef>>>);
    static_assert(std::is_same_v<dnmd::range_result, range_return_type<dnmd::raw_cursor<mdtid_TypeDef>>>);
}

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
    ASSERT_EQ(dnmd::range_result::found, dnmd::md_find_range_from_cursor(first, dnmd::columns::GenericParam_Owner, type, start, &count));
    EXPECT_EQ(1u, count);

    ASSERT_TRUE(md_add_new_row_to_list(type, mdtTypeDef_FieldList, &added));
    md_commit_row_add(added);
    auto field = dnmd::raw_cursor<mdtid_Field>{added}.as();
    mdToken parent;
    ASSERT_TRUE(field.get_parent_token(parent));
    EXPECT_EQ(owner, parent);
}
