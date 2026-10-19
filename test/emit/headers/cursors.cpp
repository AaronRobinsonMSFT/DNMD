#include <dnmd_cursors.hpp>
#include <dnmd_cursors.hpp>
#include "fixture.hpp"

TEST_F(CppHeader, StandaloneCursors)
{
    auto type = dnmd::raw_cursor<mdtid_TypeDef>{Row<mdtid_TypeDef>()};
    mdToken token;
    ASSERT_TRUE(type.get_token(token));
    EXPECT_TRUE(dnmd::md_is_valid_token(handle, token));
    EXPECT_TRUE(dnmd::md_token_to_cursor<mdtid_TypeDef>(handle, token));
    uint32_t flags;
    ASSERT_TRUE(type.as().get_column_value(dnmd::columns::TypeDef_Flags, &flags));
    dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr> fields;
    ASSERT_TRUE(type.as().get_column_value(dnmd::columns::TypeDef_FieldList, fields));
    ASSERT_TRUE(fields.get_target_token(token));
    EXPECT_EQ(mdtid_Field, ExtractTokenType(token));
    auto next = dnmd::md_create_cursor<mdtid_TypeDef>(handle);
    ASSERT_TRUE(next);
    EXPECT_TRUE(dnmd::md_cursor_next(next));
}
