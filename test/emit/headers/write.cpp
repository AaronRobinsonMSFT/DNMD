#include <dnmd_write.hpp>
#include <dnmd_write.hpp>
#include "fixture.hpp"

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

    mdcursor_t added;
    ASSERT_TRUE(md_append_row(handle, mdtid_GenericParam, &added));
    auto parameter = dnmd::raw_cursor<mdtid_GenericParam>{added}.as();
    ASSERT_TRUE(parameter.set_column_value(dnmd::columns::GenericParam_Owner, type));
    ASSERT_TRUE(parameter.set_column_value(dnmd::columns::GenericParam_Number, 0));
    dnmd::md_commit_row_add(parameter);
    EXPECT_FALSE(dnmd::detail::CursorTable(parameter)->is_adding_new_row);
}
