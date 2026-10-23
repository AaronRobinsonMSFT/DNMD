#include <dnmd_write.hpp>
#include <dnmd_write.hpp>
#include "fixture.hpp"

namespace
{
    template<typename Owner, typename List, typename = void>
    struct can_add_list : std::false_type {};

    template<typename Owner, typename List>
    struct can_add_list<Owner, List, std::void_t<decltype(dnmd::md_add_new_row_to_list(
        std::declval<Owner>(), std::declval<List>()))>> : std::true_type {};

    template<typename Owner, typename List, typename Sort, typename = void>
    struct can_add_sorted_list : std::false_type {};

    template<typename Owner, typename List, typename Sort>
    struct can_add_sorted_list<Owner, List, Sort, std::void_t<decltype(dnmd::md_add_new_row_to_sorted_list(
        std::declval<Owner>(), std::declval<List>(), std::declval<Sort>(), uint32_t{}))>> : std::true_type {};

    using owner = dnmd::cursor<mdtid_TypeDef>;
    using list = decltype(dnmd::columns::TypeDef_MethodList);
    static_assert(can_add_list<owner, list>::value);
    static_assert(!can_add_list<owner, decltype(dnmd::columns::MethodDef_ParamList)>::value);
    static_assert(!can_add_list<owner, decltype(dnmd::columns::TypeDef_Flags)>::value);
    static_assert(!can_add_list<dnmd::raw_cursor<mdtid_TypeDef>, list>::value);
    static_assert(can_add_sorted_list<owner, list, decltype(dnmd::columns::MethodDef_Rva)>::value);
    static_assert(!can_add_sorted_list<owner, list, decltype(dnmd::columns::Param_Sequence)>::value);
    static_assert(!can_add_sorted_list<owner, list, decltype(dnmd::columns::MethodDef_Name)>::value);
    static_assert(!std::is_constructible_v<dnmd::added_row<mdtid_TypeDef>, mdtable_t&, uint32_t>);
    static_assert(!std::is_constructible_v<dnmd::added_row<mdtid_TypeDef>, owner>);
}

TEST_F(CppHeader, StandaloneWrite)
{
    auto type = dnmd::raw_cursor<mdtid_TypeDef>{Row<mdtid_TypeDef>()}.as();
    dnmd::row_writer writer{type};
    ASSERT_TRUE(writer.write(dnmd::columns::TypeDef_Flags, 0x1234));
    ASSERT_TRUE(type.set_column_value(dnmd::columns::TypeDef_TypeName, "Standalone"));
    uint32_t flags;
    ASSERT_TRUE(type.get_column_value(dnmd::columns::TypeDef_Flags, &flags));
    EXPECT_EQ(0x1234u, flags);
    char const* name;
    ASSERT_TRUE(type.get_column_value(dnmd::columns::TypeDef_TypeName, &name));
    EXPECT_STREQ("Standalone", name);

    auto added = dnmd::added_row<mdtid_GenericParam>::append(handle);
    ASSERT_TRUE(added);
    auto parameter = added.as();
    ASSERT_TRUE(parameter.set_column_value(dnmd::columns::GenericParam_Owner, type));
    ASSERT_TRUE(parameter.set_column_value(dnmd::columns::GenericParam_Number, 0));
    added.commit();
    EXPECT_FALSE(dnmd::detail::CursorTable(parameter)->is_adding_new_row);
}
