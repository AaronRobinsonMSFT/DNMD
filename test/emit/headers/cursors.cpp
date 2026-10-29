#include <dnmd_cursors.hpp>
#include <dnmd_cursors.hpp>
#include "fixture.hpp"

namespace
{
    using row = dnmd::cursor<mdtid_TypeDef>;
    using position = dnmd::raw_cursor<mdtid_TypeDef>;
    using indirect = dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>;
    using guard = dnmd::added_row<mdtid_TypeDef>;
    static_assert(std::is_same_v<decltype(dnmd::detail::cursor_access::make_cursor<mdtid_TypeDef>(
        std::declval<mdtable_t&>(), uint32_t{})), row>);
    static_assert(noexcept(dnmd::detail::cursor_access::make_cursor<mdtid_TypeDef>(
        std::declval<mdtable_t&>(), uint32_t{})));
    static_assert(std::is_same_v<decltype(std::declval<position const&>().is<mdtid_TypeDef>()), bool>);
    static_assert(noexcept(std::declval<position const&>().is<mdtid_TypeDef>()));
    static_assert(std::is_same_v<decltype(std::declval<position const&>().is_nil()), bool>);
    static_assert(noexcept(std::declval<position const&>().is_nil()));
    static_assert(dnmd::detail::first_concrete_table<mdtid_Field, mdtid_FieldPtr>::value == mdtid_Field);
    static_assert(dnmd::detail::first_concrete_table<mdtid_Unused, mdtid_Unused,
        mdtid_MethodDef, mdtid_MemberRef, mdtid_Unused>::value == mdtid_MethodDef);
    static_assert(!std::is_default_constructible_v<row>);
    static_assert(!std::is_default_constructible_v<position>);
    static_assert(!std::is_default_constructible_v<indirect>);
    static_assert(!std::is_default_constructible_v<guard>);
    static_assert(!std::is_default_constructible_v<dnmd::row_reader<mdtid_TypeDef>>);
    static_assert(!std::is_constructible_v<row, mdtable_t*, uint32_t>);
    static_assert(!std::is_constructible_v<row, mdtable_t&, uint32_t>);
    static_assert(!std::is_constructible_v<position, mdtable_t*, uint32_t>);
    static_assert(!std::is_constructible_v<position, mdcxt_t*, mdToken>);
    static_assert(!std::is_constructible_v<position, mdcxt_t&, mdToken>);
    static_assert(!std::is_constructible_v<position, mdtable_t&, uint32_t>);
    static_assert(!std::is_constructible_v<position, intptr_t, uint32_t>);
    static_assert(!std::is_constructible_v<indirect, mdtable_t*, mdToken>);
    static_assert(!std::is_constructible_v<indirect, mdcxt_t*, mdToken>);
    static_assert(std::is_constructible_v<position, mdhandle_t, mdToken>);
    static_assert(std::is_constructible_v<indirect, mdhandle_t, mdToken>);
    static_assert(!std::is_copy_constructible_v<guard>);
    static_assert(!std::is_copy_assignable_v<guard>);
    static_assert(std::is_move_constructible_v<guard>);
    static_assert(std::is_move_assignable_v<guard>);
    static_assert(std::is_trivially_copyable_v<row>);
    static_assert(std::is_trivially_copyable_v<position>);
    static_assert(std::is_trivially_copyable_v<indirect>);
    static_assert(sizeof(row) == 2 * sizeof(void*));
    static_assert(sizeof(position) == 2 * sizeof(void*));
    static_assert(std::is_constructible_v<dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef>, row>);
    static_assert(std::is_constructible_v<position, dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef>>);
    static_assert(std::is_constructible_v<position, guard const&>);
}

TEST_F(CppHeader, HeaderDereferenceability)
{
    (void)Row<mdtid_TypeDef>();
    (void)dnmd_test::AppendRow<mdtid_TypeDef>(handle);
    (void)dnmd_test::AppendRow<mdtid_TypeDef>(handle);
    for (uint32_t index : {0u, 1u, 3u, 4u})
    {
        auto value = position::at(handle, index);
        EXPECT_EQ(index != 0 && index != 4, static_cast<bool>(value));
        EXPECT_EQ(handle, value.get_handle());
        EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | index, dnmd_test::Token(value));
    }
    auto empty_nil = dnmd::raw_cursor<mdtid_TypeRef>::nil(handle);
    auto empty_end = dnmd::raw_cursor<mdtid_TypeRef>::end(handle);
    EXPECT_FALSE(empty_nil);
    EXPECT_FALSE(empty_end);
    EXPECT_EQ(CreateTokenType(mdtid_TypeRef), dnmd_test::Token(empty_nil));
    EXPECT_EQ(CreateTokenType(mdtid_TypeRef) | 1, dnmd_test::Token(empty_end));
    EXPECT_FALSE(dnmd::md_cursor_next(empty_end));
    auto invalid = position::at(handle, 5);
    EXPECT_FALSE(invalid);
    EXPECT_EQ(handle, invalid.get_handle());
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef), dnmd_test::Token(invalid));
    auto wrong_table = position::at(handle, 0, mdtid_Module);
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef), dnmd_test::Token(wrong_table));
}

TEST_F(CppHeader, NativeMovementPreservesFailures)
{
    (void)Row<mdtid_TypeDef>();
    (void)dnmd_test::AppendRow<mdtid_TypeDef>(handle);
    auto table = &dnmd::detail::context(handle).tables[mdtid_TypeDef];
    for (uint32_t index : {0u, 1u, table->row_count, table->row_count + 1})
    {
        for (int32_t delta : {INT32_MIN, -3, -1, 0, 1, 3, INT32_MAX})
        {
            auto value = position::at(handle, index);
            mdToken before = dnmd_test::Token(value);
            int64_t next = int64_t{index} + delta;
            bool expected_move = next > 0 && next <= int64_t{table->row_count} + 1;
            bool moved = dnmd::md_cursor_move(value, delta);
            EXPECT_EQ(expected_move, moved) << index << ":" << delta;
            mdToken expected = expected_move
                ? CreateTokenType(mdtid_TypeDef) | static_cast<uint32_t>(next) : before;
            EXPECT_EQ(expected, dnmd_test::Token(value));
            EXPECT_EQ(handle, value.get_handle());
            if (!moved)
            {
                EXPECT_EQ(before, dnmd_test::Token(value));
            }
        }
    }
}

TEST_F(CppHeader, TableIdentityIncludesNilAndEndPositions)
{
    (void)Row<mdtid_Module>();
    (void)Row<mdtid_TypeDef>();
    using union_position = dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef, mdtid_TypeRef>;
    auto& context = dnmd::detail::context(handle);
    for (mdtable_id_t table : {mdtid_Module, mdtid_TypeDef, mdtid_TypeRef})
    {
        uint32_t row_count = context.tables[table].row_count;
        for (uint32_t index : {0u, row_count, row_count + 1})
        {
            auto value = union_position::at(handle, index, table);
            EXPECT_EQ(table == mdtid_Module, value.is<mdtid_Module>());
            EXPECT_EQ(table == mdtid_TypeDef, value.is<mdtid_TypeDef>());
            EXPECT_EQ(table == mdtid_TypeRef, value.is<mdtid_TypeRef>());
            EXPECT_FALSE(value.is<mdtid_File>());
            EXPECT_FALSE(value.is<mdtid_Unused>());
            EXPECT_FALSE(value.is<mdtid_End>());
            EXPECT_EQ(index == 0, value.is_nil());
            EXPECT_EQ(index != 0 && index <= row_count, static_cast<bool>(value));
            EXPECT_EQ(CreateTokenType(table) | index, dnmd_test::Token(value));
            EXPECT_EQ(handle, value.get_handle());
        }
    }
    EXPECT_FALSE(context.tables[mdtid_TypeRef].is_initialized);
    auto single_table = position::nil(handle);
    EXPECT_TRUE(single_table.is<mdtid_TypeDef>());
    EXPECT_FALSE(single_table.is<mdtid_Module>());
    EXPECT_TRUE(single_table.is_nil());
    EXPECT_FALSE(position::end(handle).is_nil());
    dnmd::raw_cursor<mdtid_Unused, mdtid_TypeRef> invalid{handle, 0xff000001};
    EXPECT_TRUE(invalid.is<mdtid_TypeRef>());
    EXPECT_TRUE(invalid.is_nil());
    dnmd::any_cursor widened = invalid;
    EXPECT_TRUE(widened.is<mdtid_TypeRef>());
    EXPECT_TRUE(widened.is_nil());
}

TEST_F(CppHeader, NarrowingRebindsNilToTheSameImageIncludingEmptyTables)
{
    (void)Row<mdtid_Module>();
    (void)Row<mdtid_TypeDef>();
    using union_position = dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef, mdtid_TypeRef, mdtid_MethodDef>;
    auto source = union_position::at(handle, 0, mdtid_TypeRef);
    auto same = source.as<mdtid_TypeRef>();
    auto mismatch = source.as<mdtid_Module>();
    auto empty_mismatch = source.as<mdtid_MethodDef>();
    EXPECT_FALSE(same);
    EXPECT_FALSE(mismatch);
    EXPECT_FALSE(empty_mismatch);
    EXPECT_EQ(handle, same.get_handle());
    EXPECT_EQ(handle, mismatch.get_handle());
    EXPECT_EQ(handle, empty_mismatch.get_handle());
    EXPECT_EQ(CreateTokenType(mdtid_TypeRef), dnmd_test::Token(same));
    EXPECT_EQ(CreateTokenType(mdtid_Module), dnmd_test::Token(mismatch));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), dnmd_test::Token(empty_mismatch));
    auto end = union_position::at(handle, 2, mdtid_TypeDef);
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 2, dnmd_test::Token(end.as<mdtid_TypeDef>()));
    EXPECT_EQ(CreateTokenType(mdtid_Module), dnmd_test::Token(end.as<mdtid_Module>()));
    EXPECT_TRUE(dnmd::md_cursor_next(mismatch));
    EXPECT_TRUE(mismatch);
}

#if !defined(NDEBUG) && GTEST_HAS_DEATH_TEST
TEST(CppCursorDeathTest, RejectsInvalidHandles)
{
    mdToken token = CreateTokenType(mdtid_Module) | 1;
    mdcxt_t invalid{};
    EXPECT_DEATH((position{nullptr, token}), "value");
    EXPECT_DEATH((position{static_cast<mdhandle_t>(&invalid), token}), "value");
    EXPECT_DEATH((position::nil(nullptr)), "value");
    EXPECT_DEATH((position::end(nullptr)), "value");
    EXPECT_DEATH((dnmd::md_create_cursor<mdtid_TypeDef>(nullptr)), "value");
    EXPECT_DEATH((dnmd::md_token_to_cursor<mdtid_Module>(nullptr, token)), "value");
    EXPECT_DEATH((indirect{nullptr, token}), "value");
}

TEST_F(CppHeader, NilAndEndAsRequireLiveRows)
{
    EXPECT_DEATH((position::nil(handle).value()), "bool");
    EXPECT_DEATH((position::end(handle).value()), "bool");
    EXPECT_DEATH((indirect::nil(handle).value()), "row");
    EXPECT_DEATH((indirect::end(handle).value()), "row");
}
#endif

TEST_F(CppHeader, TokenConstructionAcceptsAllowedRows)
{
    using allowed = dnmd::raw_cursor<mdtid_TypeDef, mdtid_Module>;
    static_assert(std::is_nothrow_constructible_v<allowed, mdhandle_t, mdToken>);
    (void)Row<mdtid_Module>();
    (void)Row<mdtid_TypeDef>();
    for (uint32_t i = 0; i < 2; ++i)
        (void)dnmd_test::AppendRow<mdtid_TypeDef>(handle);
    for (mdToken token : {CreateTokenType(mdtid_Module) | 1, CreateTokenType(mdtid_TypeDef) | 1,
        CreateTokenType(mdtid_TypeDef) | 2, CreateTokenType(mdtid_TypeDef) | 3})
    {
        allowed actual{handle, token};
        ASSERT_TRUE(actual);
        EXPECT_EQ(token, dnmd_test::Token(actual));
        EXPECT_EQ(handle, actual.get_handle());
        EXPECT_TRUE(dnmd::md_is_valid_token(handle, token));
    }
}

TEST_F(CppHeader, InvalidTokensBecomeNilInTheFirstTable)
{
    auto first = Row<mdtid_TypeDef>();
    auto first_table = dnmd::detail::CursorTable(first);
    mdToken tokens[] = {
        0, CreateTokenType(mdtid_TypeDef), CreateTokenType(mdtid_TypeRef),
        CreateTokenType(mdtid_Module) | 1, CreateTokenType(mdtid_TypeDef) | (first_table->row_count + 1),
        CreateTokenType(mdtid_TypeDef) | 0x00ffffff, CreateTokenType(mdtid_TypeRef) | 1,
        CreateTokenType(mdtid_End) | 1, 0x30000001, 0x70000001, 0xff000001};
    for (mdToken token : tokens)
    {
        dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef> actual{handle, token};
        EXPECT_FALSE(actual);
        EXPECT_EQ(first_table, dnmd::detail::CursorTable(actual));
        EXPECT_EQ(CreateTokenType(mdtid_TypeDef), dnmd_test::Token(actual));
        auto factory = dnmd::md_token_to_cursor<mdtid_TypeDef, mdtid_TypeRef>(handle, token);
        EXPECT_FALSE(factory);
        EXPECT_EQ(first_table, dnmd::detail::CursorTable(factory));
        EXPECT_EQ(CreateTokenType(mdtid_TypeDef), dnmd_test::Token(factory));
        auto table = ExtractTokenType(token);
        auto rid = (dnmd::detail::RidFromToken)(token);
        bool valid = table < mdtid_End && rid != 0
            && rid <= dnmd::detail::context(handle).tables[table].row_count;
        EXPECT_EQ(valid, dnmd::md_is_valid_token(handle, token));
    }
}

TEST_F(CppHeader, TokenFallbackRespectsDeclaredTableOrder)
{
    (void)Row<mdtid_Module>();
    (void)Row<mdtid_TypeDef>();
    dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef> module_first{handle, 0xff000001};
    dnmd::raw_cursor<mdtid_TypeDef, mdtid_Module> type_first{handle, 0xff000001};
    EXPECT_FALSE(module_first);
    EXPECT_FALSE(type_first);
    EXPECT_EQ(CreateTokenType(mdtid_Module), dnmd_test::Token(module_first));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef), dnmd_test::Token(type_first));
    ASSERT_TRUE(dnmd::md_cursor_next(module_first));
    EXPECT_TRUE(module_first);
}

TEST_F(CppHeader, TokenValidityChecksEveryTableTag)
{
    using reserved = dnmd::raw_cursor<mdtid_Unused, mdtid_TypeDef, mdtid_Unused, mdtid_Module, mdtid_Unused>;
    (void)Row<mdtid_GenericParamConstraint>();
    for (uint32_t table = 0; table < 0x100; ++table)
    {
        for (uint32_t index : {0u, 1u, 0x00ffffffu})
        {
            mdToken token = CreateTokenType(table) | index;
            bool valid = table < mdtid_End && index != 0
                && index <= dnmd::detail::context(handle).tables[table].row_count;
            EXPECT_EQ(valid, dnmd::md_is_valid_token(handle, token)) << table << ":" << index;
            dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef> actual{handle, token};
            bool permitted = table == mdtid_Module || table == mdtid_TypeDef;
            EXPECT_EQ(valid && permitted, static_cast<bool>(actual)) << table << ":" << index;
            reserved with_reserved{handle, token};
            EXPECT_EQ(valid && permitted, static_cast<bool>(with_reserved)) << table << ":" << index;
            EXPECT_EQ(handle, with_reserved.get_handle());
            if (valid && permitted)
            {
                EXPECT_EQ(token, dnmd_test::Token(with_reserved));
            }
            else
            {
                EXPECT_TRUE(with_reserved.is_nil());
                EXPECT_TRUE(with_reserved.is<mdtid_TypeDef>());
            }
        }
    }
    EXPECT_TRUE((dnmd::raw_cursor<mdtid_GenericParamConstraint>{
        handle, CreateTokenType(mdtid_GenericParamConstraint) | 1}));
}

TEST_F(CppHeader, EmptyTokenFallbackDoesNotInitializeTableStorage)
{
    auto& context = dnmd::detail::context(handle);
    for (uint32_t index = 0; index < mdtid_End; ++index)
    {
        EXPECT_EQ(&context, context.tables[index].cxt);
        EXPECT_EQ(index, context.tables[index].table_id);
    }
    auto table = &context.tables[mdtid_TypeRef];
    ASSERT_EQ(0u, table->row_count);
    ASSERT_EQ(&context, table->cxt);
    ASSERT_FALSE(table->is_initialized);
    dnmd::raw_cursor<mdtid_TypeRef> actual{handle, CreateTokenType(mdtid_TypeRef) | 1};
    EXPECT_FALSE(actual);
    EXPECT_EQ(table, dnmd::detail::CursorTable(actual));
    EXPECT_EQ(CreateTokenType(mdtid_TypeRef), dnmd_test::Token(actual));
    EXPECT_EQ(&context, table->cxt);
    EXPECT_FALSE(table->is_initialized);
    (void)dnmd_test::AppendRow<mdtid_TypeRef>(handle);
    EXPECT_TRUE(table->is_initialized);
    ASSERT_TRUE(dnmd::md_cursor_next(actual));
    EXPECT_TRUE(actual);
}

TEST_F(CppHeader, TokenConstructionSkipsReservedFallbackTables)
{
    using reserved = dnmd::raw_cursor<mdtid_Unused, mdtid_Unused, mdtid_MethodDef, mdtid_MemberRef, mdtid_Unused>;
    (void)Row<mdtid_MemberRef>();
    reserved nil{handle, 0xff000001};
    EXPECT_FALSE(nil);
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), dnmd_test::Token(nil));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), dnmd_test::Token(reserved::at(handle, 0, mdtid_Unused)));
    reserved valid{handle, CreateTokenType(mdtid_MemberRef) | 1};
    ASSERT_TRUE(valid);
    EXPECT_TRUE(valid.as<mdtid_MemberRef>());
}

TEST(CppCursorValidation, BooleanValidationRejectsInvalidHandles)
{
    mdToken token = CreateTokenType(mdtid_Module) | 1;
    EXPECT_FALSE(dnmd::md_is_valid_token(nullptr, token));
    mdcxt_t invalid{};
    EXPECT_FALSE(dnmd::md_is_valid_token(&invalid, token));
}

#ifdef DNMD_PORTABLE_PDB
TEST_F(CppHeader, TokenConstructionAcceptsPortablePdbRows)
{
    (void)Row<mdtid_LocalScope>();
    (void)Row<mdtid_CustomDebugInformation>();
    mdToken token = CreateTokenType(mdtid_LocalScope) | 1;
    dnmd::raw_cursor<mdtid_LocalScope> scope{handle, token};
    ASSERT_TRUE(scope);
    EXPECT_TRUE(scope.is<mdtid_LocalScope>());
    EXPECT_FALSE(scope.is_nil());
    EXPECT_EQ(token, dnmd_test::Token(scope));
    EXPECT_TRUE(dnmd::md_is_valid_token(handle, token));
    token = CreateTokenType(mdtid_CustomDebugInformation) | 1;
    dnmd::raw_cursor<mdtid_CustomDebugInformation> debug{handle, token};
    ASSERT_TRUE(debug);
    EXPECT_TRUE(debug.is<mdtid_CustomDebugInformation>());
    EXPECT_FALSE(debug.is_nil());
    EXPECT_EQ(token, dnmd_test::Token(debug));
    EXPECT_TRUE(dnmd::md_is_valid_token(handle, token));
}
#endif

TEST_F(CppHeader, StandaloneCursors)
{
    auto type = dnmd::raw_cursor<mdtid_TypeDef>{Row<mdtid_TypeDef>()};
    mdToken token = dnmd_test::Token(type);
    EXPECT_TRUE(dnmd::md_is_valid_token(handle, token));
    EXPECT_TRUE(dnmd::md_token_to_cursor<mdtid_TypeDef>(handle, token));
    uint32_t flags;
    ASSERT_TRUE(type.value().get_column_value(dnmd::columns::TypeDef_Flags, &flags));
    auto fields = dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr>::nil(handle);
    ASSERT_TRUE(type.value().get_column_value(dnmd::columns::TypeDef_FieldList, fields));
    ASSERT_TRUE(fields.get_target_token(token));
    EXPECT_EQ(mdtid_Field, ExtractTokenType(token));
    auto next = dnmd::md_create_cursor<mdtid_TypeDef>(handle);
    ASSERT_TRUE(next);
    uint32_t count;
    auto counted = dnmd::md_create_cursor<mdtid_TypeDef>(handle, &count);
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 1, dnmd_test::Token(counted));
    EXPECT_EQ(dnmd::detail::context(handle).tables[mdtid_TypeDef].row_count, count);
    EXPECT_TRUE(dnmd::md_cursor_next(next));
}
