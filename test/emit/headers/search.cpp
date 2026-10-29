#include <dnmd_search.hpp>
#include "fixture.hpp"

namespace
{
    static_assert(std::is_enum_v<dnmd::range_result>);
    static_assert(!std::is_convertible_v<dnmd::range_result, int>);
    static_assert(!std::is_convertible_v<int, dnmd::range_result>);
    static_assert(static_cast<int>(dnmd::range_result::found) == 0);
    static_assert(static_cast<int>(dnmd::range_result::not_found) == 1);
    static_assert(static_cast<int>(dnmd::range_result::not_supported) == 2);

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
    auto type = dnmd::raw_cursor<mdtid_TypeDef>{Row<mdtid_TypeDef>()}.value();
    auto added = dnmd::added_row<mdtid_GenericParam>::append(handle);
    ASSERT_TRUE(added);
    mdToken owner;
    ASSERT_TRUE(type.get_token(owner));
    ASSERT_TRUE(added.value().set_column_value(dnmd::columns::GenericParam_Owner, owner));
    added.commit();
    auto first = dnmd::md_create_cursor<mdtid_GenericParam>(handle).value();
    EXPECT_TRUE(dnmd::md_find_row_from_cursor(first, dnmd::columns::GenericParam_Owner, type));
    dnmd::raw_cursor<mdtid_GenericParam> start = dnmd::raw_cursor<mdtid_GenericParam>::nil(handle);
    uint32_t count;
    ASSERT_EQ(dnmd::range_result::found, dnmd::md_find_range_from_cursor(first, dnmd::columns::GenericParam_Owner, type, start, &count));
    EXPECT_EQ(1u, count);

    auto field = dnmd_test::AddListRow(type, dnmd::columns::TypeDef_FieldList);
    mdToken parent;
    ASSERT_TRUE(field.get_parent_token(parent));
    EXPECT_EQ(owner, parent);
}
