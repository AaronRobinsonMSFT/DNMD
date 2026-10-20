#include <dnmd_cursors.hpp>
#include <dnmd_cursors.hpp>
#include "fixture.hpp"

TEST(CppCursorBounds, HeaderDereferenceability)
{
    static_assert(noexcept(dnmd::detail::is_dereferenceable(mdcursor_t{})));
    EXPECT_FALSE(dnmd::detail::is_dereferenceable(mdcursor_t{}));

    mdtable_t table{};
    table.table_id = mdtid_TypeDef;
    table.row_count = 3;
    auto check = [&](uint32_t row, bool expected) {
        mdcursor_t position{reinterpret_cast<intptr_t>(&table), static_cast<intptr_t>(row)};
        EXPECT_EQ(expected, dnmd::detail::is_dereferenceable(position)) << "Row " << row;
    };
    check(0, false);
    check(1, true);
    check(3, true);
    check(4, false);
    check(5, false);
    check(0x01000001, true);

    table.row_count = 0;
    check(0, false);
    check(1, false);
}

TEST(CppCursorBounds, NarrowingPreservesEmptyAndTableBoundNilPositions)
{
    dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef> empty;
    auto narrowed = empty.get_as<mdtid_Module>();
    EXPECT_FALSE(narrowed);
    mdToken token;
    ASSERT_TRUE(narrowed.get_token(token));
    EXPECT_EQ(mdToken{0}, token);
    EXPECT_FALSE(dnmd::md_cursor_next(narrowed));

    mdtable_t table{};
    table.table_id = mdtid_Module;
    table.row_count = 1;
    mdcursor_t nil{reinterpret_cast<intptr_t>(&table), 0};
    dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef> bound{nil};
    auto bound_nil = bound.get_as<mdtid_Module>();
    EXPECT_FALSE(bound_nil);
    EXPECT_EQ(&table, dnmd::detail::CursorTable(bound_nil));
    ASSERT_TRUE(dnmd::md_cursor_next(bound_nil));
    EXPECT_TRUE(bound_nil);
}

#if !defined(NDEBUG) && GTEST_HAS_DEATH_TEST
TEST(CppCursorDeathTest, RejectsNullTableConstruction)
{
    mdcursor_t empty{};
    EXPECT_DEATH((dnmd::raw_cursor<mdtid_Module>{empty}), "CursorTable");
    EXPECT_DEATH((dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>{empty}), "CursorTable");
    EXPECT_DEATH((dnmd::raw_cursor<mdtid_Module>{}.as()), "CursorTable");
}

TEST(CppCursorDeathTest, RejectsInvalidHandlesInTokenConstruction)
{
    using position = dnmd::raw_cursor<mdtid_Module>;
    mdToken token = CreateTokenType(mdtid_Module) | 1;
    EXPECT_DEATH((position{nullptr, token}), "context");
    mdcxt_t invalid{};
    EXPECT_DEATH((position{&invalid, token}), "context");
}
#endif

TEST_F(CppHeader, TokenConstructionMatchesCForAllowedRows)
{
    using position = dnmd::raw_cursor<mdtid_TypeDef, mdtid_Module>;
    static_assert(std::is_nothrow_constructible_v<position, mdhandle_t, mdToken>);
    static_assert(sizeof(position) == sizeof(mdcursor_t));
    (void)Row<mdtid_Module>();
    (void)Row<mdtid_TypeDef>();
    for (uint32_t i = 0; i < 2; ++i)
    {
        mdcursor_t added;
        ASSERT_TRUE(md_append_row(handle, mdtid_TypeDef, &added));
        md_commit_row_add(added);
    }
    mdToken tokens[] = {
        CreateTokenType(mdtid_Module) | 1,
        CreateTokenType(mdtid_TypeDef) | 1,
        CreateTokenType(mdtid_TypeDef) | 2,
        CreateTokenType(mdtid_TypeDef) | 3,
    };
    for (mdToken token : tokens)
    {
        mdcursor_t expected;
        ASSERT_TRUE(md_token_to_cursor(handle, token, &expected));
        position actual{handle, token};
        ASSERT_TRUE(actual);
        mdToken round_trip;
        ASSERT_TRUE(actual.get_token(round_trip));
        EXPECT_EQ(token, round_trip);
        mdcursor_t raw = actual;
        EXPECT_EQ(expected._reserved1, raw._reserved1);
        EXPECT_EQ(expected._reserved2, raw._reserved2);
        EXPECT_TRUE(dnmd::md_is_valid_token(handle, token));
    }
    dnmd::raw_cursor<mdtid_TypeDef> single{handle, tokens[1]};
    ASSERT_TRUE(single);
    EXPECT_EQ(1u, dnmd::detail::CursorRow(single.as()));
}

TEST_F(CppHeader, InvalidTokensBecomeNilInTheFirstTable)
{
    auto first = Row<mdtid_TypeDef>();
    (void)Row<mdtid_Module>();
    auto first_table = dnmd::detail::CursorTable(first);
    mdToken tokens[] = {
        mdToken{0},
        CreateTokenType(mdtid_TypeDef),
        CreateTokenType(mdtid_TypeRef),
        CreateTokenType(mdtid_Module) | 1,
        CreateTokenType(mdtid_TypeDef) | (first_table->row_count + 1),
        CreateTokenType(mdtid_TypeDef) | 0x00ffffff,
        CreateTokenType(mdtid_TypeRef) | 1,
        CreateTokenType(mdtid_End) | 1,
        0x30000001,
        0x70000001,
        0xff000001,
    };
    for (mdToken token : tokens)
    {
        dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef> actual{handle, token};
        EXPECT_FALSE(actual);
        EXPECT_EQ(first_table, dnmd::detail::CursorTable(actual));
        EXPECT_EQ(0u, dnmd::detail::CursorRow(actual));
        mdToken nil;
        ASSERT_TRUE(actual.get_token(nil));
        EXPECT_EQ(CreateTokenType(mdtid_TypeDef), nil);

        auto factory = dnmd::md_token_to_cursor<mdtid_TypeDef, mdtid_TypeRef>(handle, token);
        EXPECT_FALSE(factory);
        EXPECT_EQ(first_table, dnmd::detail::CursorTable(factory));
        ASSERT_TRUE(factory.get_token(nil));
        EXPECT_EQ(CreateTokenType(mdtid_TypeDef), nil);

        mdcursor_t c_position;
        EXPECT_EQ(md_token_to_cursor(handle, token, &c_position), dnmd::md_is_valid_token(handle, token));
    }
}

TEST_F(CppHeader, TokenFallbackRespectsDeclaredTableOrder)
{
    auto module = Row<mdtid_Module>();
    auto type = Row<mdtid_TypeDef>();
    dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef> module_first{handle, 0xff000001};
    dnmd::raw_cursor<mdtid_TypeDef, mdtid_Module> type_first{handle, 0xff000001};
    EXPECT_FALSE(module_first);
    EXPECT_FALSE(type_first);
    EXPECT_EQ(dnmd::detail::CursorTable(module), dnmd::detail::CursorTable(module_first));
    EXPECT_EQ(dnmd::detail::CursorTable(type), dnmd::detail::CursorTable(type_first));
    mdToken token;
    ASSERT_TRUE(module_first.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_Module), token);
    ASSERT_TRUE(type_first.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef), token);
    ASSERT_TRUE(dnmd::md_cursor_next(module_first));
    EXPECT_TRUE(module_first);
}

TEST_F(CppHeader, TokenValidityMatchesCForEveryTableTag)
{
    (void)Row<mdtid_Module>();
    (void)Row<mdtid_TypeDef>();
    (void)Row<mdtid_GenericParamConstraint>();
    dnmd::raw_cursor<mdtid_GenericParamConstraint> last_type_system_table{
        handle, CreateTokenType(mdtid_GenericParamConstraint) | 1};
    EXPECT_TRUE(last_type_system_table);
    uint32_t rows[] = {0, 1, 0x00ffffff};
    for (uint32_t table = 0; table < 0x100; ++table)
    {
        for (uint32_t row : rows)
        {
            mdToken token = CreateTokenType(table) | row;
            mdcursor_t expected;
            bool valid = md_token_to_cursor(handle, token, &expected);
            EXPECT_EQ(valid, dnmd::md_is_valid_token(handle, token)) << "Table " << table << ", row " << row;
            dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef> position{handle, token};
            bool permitted = table == static_cast<uint32_t>(mdtid_Module)
                || table == static_cast<uint32_t>(mdtid_TypeDef);
            EXPECT_EQ(valid && permitted, static_cast<bool>(position)) << "Table " << table << ", row " << row;
        }
    }
}

TEST_F(CppHeader, EmptyTokenFallbackDoesNotInitializeTableStorage)
{
    auto context = dnmd::detail::extract_mdcxt(handle);
    auto table = &context->tables[mdtid_TypeRef];
    ASSERT_EQ(0u, table->row_count);
    ASSERT_EQ(nullptr, table->cxt);
    dnmd::raw_cursor<mdtid_TypeRef> position{handle, CreateTokenType(mdtid_TypeRef) | 1};
    EXPECT_FALSE(position);
    EXPECT_EQ(table, dnmd::detail::CursorTable(position));
    mdToken token;
    ASSERT_TRUE(position.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeRef), token);
    EXPECT_EQ(nullptr, table->cxt);

    mdcursor_t added;
    ASSERT_TRUE(md_append_row(handle, mdtid_TypeRef, &added));
    md_commit_row_add(added);
    EXPECT_EQ(context, table->cxt);
    ASSERT_TRUE(dnmd::md_cursor_next(position));
    EXPECT_TRUE(position);
}

TEST_F(CppHeader, TokenConstructionSkipsReservedFallbackTables)
{
    using position = dnmd::raw_cursor<mdtid_Unused, mdtid_Unused, mdtid_MethodDef, mdtid_MemberRef, mdtid_Unused>;
    (void)Row<mdtid_MemberRef>();
    position nil{handle, 0xff000001};
    EXPECT_FALSE(nil);
    mdToken token;
    ASSERT_TRUE(nil.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), token);
    EXPECT_FALSE(nil.get_as<mdtid_MethodDef>());
    position valid{handle, CreateTokenType(mdtid_MemberRef) | 1};
    ASSERT_TRUE(valid);
    EXPECT_TRUE(valid.get_as<mdtid_MemberRef>());
}

TEST_F(CppHeader, TokenHelpersPreserveInvalidHandleFailures)
{
    mdToken token = CreateTokenType(mdtid_Module) | 1;
    auto missing = dnmd::md_token_to_cursor<mdtid_Module>(nullptr, token);
    EXPECT_FALSE(missing);
    EXPECT_EQ(nullptr, dnmd::detail::CursorTable(missing));
    EXPECT_FALSE(dnmd::md_is_valid_token(nullptr, token));
    mdcxt_t invalid{};
    auto bad = dnmd::md_token_to_cursor<mdtid_Module>(&invalid, token);
    EXPECT_FALSE(bad);
    EXPECT_EQ(nullptr, dnmd::detail::CursorTable(bad));
    EXPECT_FALSE(dnmd::md_is_valid_token(&invalid, token));
}

#ifdef DNMD_PORTABLE_PDB
TEST_F(CppHeader, TokenConstructionAcceptsPortablePdbRows)
{
    (void)Row<mdtid_LocalScope>();
    (void)Row<mdtid_CustomDebugInformation>();
    mdToken token = CreateTokenType(mdtid_LocalScope) | 1;
    dnmd::raw_cursor<mdtid_LocalScope> position{handle, token};
    ASSERT_TRUE(position);
    mdToken actual;
    ASSERT_TRUE(position.get_token(actual));
    EXPECT_EQ(token, actual);
    EXPECT_TRUE(dnmd::md_is_valid_token(handle, token));
    token = CreateTokenType(mdtid_CustomDebugInformation) | 1;
    dnmd::raw_cursor<mdtid_CustomDebugInformation> last_table{handle, token};
    ASSERT_TRUE(last_table);
    ASSERT_TRUE(last_table.get_token(actual));
    EXPECT_EQ(token, actual);
    EXPECT_TRUE(dnmd::md_is_valid_token(handle, token));
}
#endif

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
