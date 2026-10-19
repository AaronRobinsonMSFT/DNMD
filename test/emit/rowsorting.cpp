#include <dnmd.hpp>
#include <gtest/gtest.h>
#include <array>
#include <vector>

namespace
{
    using expected_keys = std::vector<std::pair<col_index_t, bool>>;

    template<mdtable_id_t TableId>
    void CheckKeys(expected_keys const& expected)
    {
        expected_keys actual;
        std::apply([&](auto... keys) {
            (actual.emplace_back(keys.column.column_index, keys.descending), ...);
        }, dnmd::detail::table_sort_keys<TableId>());
        EXPECT_EQ(expected, actual) << "Table " << TableId;
    }

    template<size_t... TableIds>
    void CheckAllKeys(std::array<expected_keys, mdtid_End> const& expected, std::index_sequence<TableIds...>)
    {
        (CheckKeys<static_cast<mdtable_id_t>(TableIds)>(expected[TableIds]), ...);
    }

    class CppRowSorting : public testing::Test
    {
    protected:
        dnmd::handle_ptr handle;

        void SetUp() override { Reset(); }

        void Reset()
        {
#ifdef DNMD_PORTABLE_PDB
            handle.reset(md_create_new_pdb_handle());
#else
            handle.reset(md_create_new_handle());
#endif
            ASSERT_NE(nullptr, handle.get());
        }

        template<mdtable_id_t TableId>
        dnmd::cursor<TableId> Row(uint32_t index = 1)
        {
            auto position = dnmd::md_create_cursor<TableId>(handle.get());
            EXPECT_TRUE(dnmd::md_cursor_move(position, index - 1));
            return position.as();
        }

        template<mdtable_id_t TableId>
        bool Sorted()
        {
            return dnmd::detail::CursorTable(Row<TableId>())->is_sorted;
        }

        void AppendParameter(uint32_t owner, uint32_t number)
        {
            dnmd::added_row_t added;
            ASSERT_TRUE(md_append_row(handle.get(), mdtid_GenericParam, &added));
            dnmd::row_writer writer{dnmd::raw_cursor<mdtid_GenericParam>{added}.as()};
            ASSERT_TRUE(writer.write(dnmd::columns::GenericParam_Owner, CreateTokenType(mdtid_TypeDef) | owner));
            ASSERT_TRUE(writer.write(dnmd::columns::GenericParam_Number, number));
        }

        void FillLayouts(std::initializer_list<uint32_t> parents)
        {
            for (uint32_t parent : parents)
            {
                dnmd::added_row_t added;
                ASSERT_TRUE(md_append_row(handle.get(), mdtid_ClassLayout, &added));
                auto row = dnmd::raw_cursor<mdtid_ClassLayout>{added}.as();
                ASSERT_TRUE(row.set_column_value(dnmd::columns::ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | parent));
            }
        }

#ifdef DNMD_PORTABLE_PDB
        void AppendScope(uint32_t method, uint32_t start, uint32_t length)
        {
            dnmd::added_row_t added;
            ASSERT_TRUE(md_append_row(handle.get(), mdtid_LocalScope, &added));
            dnmd::row_writer writer{dnmd::raw_cursor<mdtid_LocalScope>{added}.as()};
            ASSERT_TRUE(writer.write(dnmd::columns::LocalScope_Method, CreateTokenType(mdtid_MethodDef) | method));
            ASSERT_TRUE(writer.write(dnmd::columns::LocalScope_StartOffset, start));
            ASSERT_TRUE(writer.write(dnmd::columns::LocalScope_Length, length));
        }
#endif
    };
}

using namespace dnmd::columns;

TEST(CppSortKeys, CompleteOrderedSchema)
{
    std::array<expected_keys, mdtid_End> expected;
    expected[mdtid_ClassLayout] = {{mdtClassLayout_Parent, false}};
    expected[mdtid_Constant] = {{mdtConstant_Parent, false}};
    expected[mdtid_CustomAttribute] = {{mdtCustomAttribute_Parent, false}};
    expected[mdtid_DeclSecurity] = {{mdtDeclSecurity_Parent, false}};
    expected[mdtid_FieldLayout] = {{mdtFieldLayout_Field, false}};
    expected[mdtid_FieldMarshal] = {{mdtFieldMarshal_Parent, false}};
    expected[mdtid_FieldRva] = {{mdtFieldRva_Field, false}};
    expected[mdtid_GenericParam] = {{mdtGenericParam_Owner, false}, {mdtGenericParam_Number, false}};
    expected[mdtid_GenericParamConstraint] = {{mdtGenericParamConstraint_Owner, false}};
    expected[mdtid_ImplMap] = {{mdtImplMap_MemberForwarded, false}};
    expected[mdtid_InterfaceImpl] = {{mdtInterfaceImpl_Class, false}};
    expected[mdtid_MethodImpl] = {{mdtMethodImpl_Class, false}};
    expected[mdtid_MethodSemantics] = {{mdtMethodSemantics_Association, false}};
    expected[mdtid_NestedClass] = {{mdtNestedClass_NestedClass, false}};
#ifdef DNMD_PORTABLE_PDB
    expected[mdtid_LocalScope] = {{mdtLocalScope_Method, false}, {mdtLocalScope_StartOffset, false}, {mdtLocalScope_Length, true}};
    expected[mdtid_StateMachineMethod] = {{mdtStateMachineMethod_MoveNextMethod, false}};
    expected[mdtid_CustomDebugInformation] = {{mdtCustomDebugInformation_Parent, false}};
#endif
    CheckAllKeys(expected, std::make_index_sequence<mdtid_End>{});
}

TEST_F(CppRowSorting, KeyWritesCheckActualNeighborsAtBothEdgesAndInTheMiddle)
{
    for (auto change : {std::pair{1u, 5u}, std::pair{2u, 1u}, std::pair{2u, 7u}, std::pair{3u, 3u}})
    {
        ASSERT_NO_FATAL_FAILURE(Reset());
        ASSERT_NO_FATAL_FAILURE(FillLayouts({2, 4, 6}));
        ASSERT_TRUE(Sorted<mdtid_ClassLayout>());
        auto row = Row<mdtid_ClassLayout>(change.first);
        ASSERT_TRUE(row.set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | (change.first * 2)));
        EXPECT_TRUE(Sorted<mdtid_ClassLayout>());
        ASSERT_TRUE(row.set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | change.second));
        EXPECT_FALSE(Sorted<mdtid_ClassLayout>());
    }
}

TEST_F(CppRowSorting, SingleRowAndEqualKeysStaySorted)
{
    ASSERT_NO_FATAL_FAILURE(FillLayouts({3}));
    auto row = Row<mdtid_ClassLayout>();
    ASSERT_TRUE(row.set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | 100));
    EXPECT_TRUE(Sorted<mdtid_ClassLayout>());
    ASSERT_TRUE(row.set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | 3));
    ASSERT_NO_FATAL_FAILURE(FillLayouts({3, 3}));
    EXPECT_TRUE(Sorted<mdtid_ClassLayout>());
    ASSERT_TRUE(Row<mdtid_ClassLayout>(2).set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | 3));
    EXPECT_TRUE(Sorted<mdtid_ClassLayout>());
}

TEST_F(CppRowSorting, CompositeWritesStopAtTheFirstUnequalKey)
{
    ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 9));
    ASSERT_NO_FATAL_FAILURE(AppendParameter(2, 0));
    ASSERT_NO_FATAL_FAILURE(AppendParameter(2, 1));
    ASSERT_NO_FATAL_FAILURE(AppendParameter(3, 0));
    ASSERT_TRUE(Sorted<mdtid_GenericParam>());
    ASSERT_TRUE(Row<mdtid_GenericParam>(2).set_column_value(GenericParam_Number, 0));
    EXPECT_TRUE(Sorted<mdtid_GenericParam>());
    ASSERT_TRUE(Row<mdtid_GenericParam>(3).set_column_value(GenericParam_Number, 7));
    EXPECT_TRUE(Sorted<mdtid_GenericParam>());
    ASSERT_TRUE(Row<mdtid_GenericParam>(1).set_column_value(GenericParam_Number, 0xffff));
    EXPECT_TRUE(Sorted<mdtid_GenericParam>());
    ASSERT_TRUE(Row<mdtid_GenericParam>(4).set_column_value(GenericParam_Number, 0));
    EXPECT_TRUE(Sorted<mdtid_GenericParam>());
    ASSERT_TRUE(Row<mdtid_GenericParam>(2).set_column_value(GenericParam_Number, 8));
    EXPECT_FALSE(Sorted<mdtid_GenericParam>());
}

TEST_F(CppRowSorting, PrimaryWritesStillValidateEqualPrefixSecondaryKeys)
{
    for (uint32_t index : {1u, 2u})
    {
        ASSERT_NO_FATAL_FAILURE(Reset());
        ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 9));
        ASSERT_NO_FATAL_FAILURE(AppendParameter(2, 0));
        ASSERT_TRUE(Sorted<mdtid_GenericParam>());
        uint32_t owner = index == 1 ? 2 : 1;
        ASSERT_TRUE(Row<mdtid_GenericParam>(index).set_column_value(GenericParam_Owner, CreateTokenType(mdtid_TypeDef) | owner));
        EXPECT_FALSE(Sorted<mdtid_GenericParam>());
    }
}

TEST_F(CppRowSorting, NarrowKeyWritesValidateTheStoredValue)
{
    ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 0));
    ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 2));
    ASSERT_TRUE(Row<mdtid_GenericParam>().set_column_value(GenericParam_Number, 0x10001));
    EXPECT_TRUE(Sorted<mdtid_GenericParam>());
    uint32_t number;
    ASSERT_TRUE(Row<mdtid_GenericParam>().get_column_value(GenericParam_Number, &number));
    EXPECT_EQ(1u, number);
}

TEST_F(CppRowSorting, NewRowValidationIsDeferredUntilAllKeysAreInitialized)
{
    ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 5));
    {
        dnmd::added_row_t added;
        ASSERT_TRUE(md_append_row(handle.get(), mdtid_GenericParam, &added));
        auto row = dnmd::raw_cursor<mdtid_GenericParam>{added}.as();
        dnmd::row_writer writer{row};
        auto table = dnmd::detail::CursorTable(row);
        ASSERT_TRUE(writer.write(GenericParam_Owner, CreateTokenType(mdtid_TypeDef) | 1));
        EXPECT_TRUE(table->is_adding_new_row);
        EXPECT_TRUE(table->is_sorted);
        ASSERT_TRUE(writer.write(GenericParam_Number, 9));
        EXPECT_TRUE(table->is_sorted);
    }
    EXPECT_TRUE(Sorted<mdtid_GenericParam>());
    EXPECT_FALSE(dnmd::detail::CursorTable(Row<mdtid_GenericParam>())->is_adding_new_row);
    ASSERT_NO_FATAL_FAILURE(AppendParameter(2, 0));
    EXPECT_TRUE(Sorted<mdtid_GenericParam>());
}

TEST_F(CppRowSorting, InsertedRowCommitChecksBothNeighbors)
{
    for (uint32_t parent : {1u, 4u, 8u})
    {
        ASSERT_NO_FATAL_FAILURE(Reset());
        ASSERT_NO_FATAL_FAILURE(FillLayouts({2, 6}));
        {
            dnmd::added_row_t added;
            ASSERT_TRUE(md_insert_row_before(Row<mdtid_ClassLayout>(2), &added));
            auto row = dnmd::raw_cursor<mdtid_ClassLayout>{added}.as();
            ASSERT_TRUE(row.set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | parent));
            EXPECT_TRUE(Sorted<mdtid_ClassLayout>());
        }
        EXPECT_EQ(parent == 4, Sorted<mdtid_ClassLayout>());
        EXPECT_FALSE(dnmd::detail::CursorTable(Row<mdtid_ClassLayout>())->is_adding_new_row);
    }
}

TEST_F(CppRowSorting, TypedCommitChecksAppendAndPrependEdges)
{
    for (bool append : {false, true})
    {
        ASSERT_NO_FATAL_FAILURE(Reset());
        ASSERT_NO_FATAL_FAILURE(FillLayouts({4}));
        mdcursor_t added;
        if (append)
            ASSERT_TRUE(md_append_row(handle.get(), mdtid_ClassLayout, &added));
        else
            ASSERT_TRUE(md_insert_row_before(Row<mdtid_ClassLayout>(), &added));
        auto row = dnmd::raw_cursor<mdtid_ClassLayout>{added}.as();
        ASSERT_TRUE(row.set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | (append ? 2 : 6)));
        EXPECT_TRUE(Sorted<mdtid_ClassLayout>());
        dnmd::md_commit_row_add(row);
        EXPECT_FALSE(Sorted<mdtid_ClassLayout>());
        EXPECT_FALSE(dnmd::detail::CursorTable(row)->is_adding_new_row);
    }
}

TEST_F(CppRowSorting, CompositeCommitRejectsReversedSecondaryKeys)
{
    ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 5));
    ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 4));
    EXPECT_FALSE(Sorted<mdtid_GenericParam>());
}

TEST_F(CppRowSorting, NonKeyWritesPreserveSortednessAndUnsortedTablesStayUnsorted)
{
    ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 5));
    ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 9));
    dnmd::row_writer writer{Row<mdtid_GenericParam>()};
    ASSERT_TRUE(writer.write(GenericParam_Flags, 0xffff));
    ASSERT_TRUE(writer.write(GenericParam_Name, "Changed"));
    EXPECT_TRUE(Sorted<mdtid_GenericParam>());
    ASSERT_TRUE(writer.write(GenericParam_Number, 10));
    EXPECT_FALSE(Sorted<mdtid_GenericParam>());
    ASSERT_TRUE(writer.write(GenericParam_Number, 5));
    EXPECT_FALSE(Sorted<mdtid_GenericParam>());
    ASSERT_NO_FATAL_FAILURE(AppendParameter(2, 0));
    EXPECT_FALSE(Sorted<mdtid_GenericParam>());
}

TEST_F(CppRowSorting, CodedIndexSortOrderUsesStoredRidAndTagValues)
{
    for (auto token : {CreateTokenType(mdtid_Field) | 1, CreateTokenType(mdtid_TypeDef) | 1, CreateTokenType(mdtid_MethodDef) | 2})
    {
        dnmd::added_row_t added;
        ASSERT_TRUE(md_append_row(handle.get(), mdtid_CustomAttribute, &added));
        auto row = dnmd::raw_cursor<mdtid_CustomAttribute>{added}.as();
        ASSERT_TRUE(row.set_column_value(CustomAttribute_Parent, token));
    }
    ASSERT_TRUE(Sorted<mdtid_CustomAttribute>());
    auto row = Row<mdtid_CustomAttribute>(2);
    ASSERT_TRUE(row.set_column_value(CustomAttribute_Parent, CreateTokenType(mdtid_MethodDef) | 2));
    EXPECT_TRUE(Sorted<mdtid_CustomAttribute>());
    ASSERT_TRUE(row.set_column_value(CustomAttribute_Parent, CreateTokenType(mdtid_MethodDef) | 1));
    EXPECT_FALSE(Sorted<mdtid_CustomAttribute>());
}

TEST_F(CppRowSorting, EmptyMovedAndUnkeyedGuards)
{
    dnmd::added_row_t empty;
    mdtable_t* table;
    {
        dnmd::added_row_t added;
        ASSERT_TRUE(md_append_row(handle.get(), mdtid_ENCLog, &added));
        auto row = dnmd::raw_cursor<mdtid_ENCLog>{added}.as();
        table = dnmd::detail::CursorTable(row);
        EXPECT_FALSE(table->is_sorted);
        ASSERT_TRUE(row.set_column_value(ENCLog_Token, 0xffffffff));
        dnmd::added_row_t moved{std::move(added)};
        EXPECT_TRUE(table->is_adding_new_row);
    }
    EXPECT_FALSE(table->is_adding_new_row);
    EXPECT_FALSE(table->is_sorted);
}

#ifndef DNMD_PORTABLE_PDB
TEST_F(CppRowSorting, CompositeSortedStateRoundTripsAndDrivesPrimaryRanges)
{
    ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 5));
    ASSERT_NO_FATAL_FAILURE(AppendParameter(1, 9));
    ASSERT_NO_FATAL_FAILURE(AppendParameter(2, 0));
    ASSERT_TRUE(Row<mdtid_GenericParam>(2).set_column_value(GenericParam_Number, 10));
    ASSERT_TRUE(Sorted<mdtid_GenericParam>());
    size_t size = 0;
    EXPECT_FALSE(md_write_to_buffer(handle.get(), nullptr, &size));
    ASSERT_GT(size, 0u);
    std::vector<uint8_t> image(size);
    ASSERT_TRUE(md_write_to_buffer(handle.get(), image.data(), &size));
    mdhandle_t raw;
    ASSERT_TRUE(md_create_handle(image.data(), size, &raw));
    dnmd::handle_ptr loaded{raw};
    auto first = dnmd::md_create_cursor<mdtid_GenericParam>(loaded.get());
    ASSERT_TRUE(first);
    EXPECT_TRUE(dnmd::detail::CursorTable(first)->is_sorted);
    dnmd::raw_cursor<mdtid_GenericParam> start;
    uint32_t count;
    ASSERT_EQ(MD_RANGE_FOUND, dnmd::md_find_range_from_cursor(first.as(), GenericParam_Owner,
        CreateTokenType(mdtid_TypeDef) | 1, start, &count));
    EXPECT_EQ(2u, count);
    EXPECT_EQ(1u, dnmd::detail::CursorRow(start));
    EXPECT_EQ(MD_RANGE_NOT_SUPPORTED, dnmd::md_find_range_from_cursor(first.as(), GenericParam_Number,
        10, start, &count));
    EXPECT_EQ(2u, count);
}
#endif

#ifdef DNMD_PORTABLE_PDB
TEST_F(CppRowSorting, DescendingScopeLengthIsOnlyATieBreaker)
{
    ASSERT_NO_FATAL_FAILURE(AppendScope(1, 10, 100));
    ASSERT_NO_FATAL_FAILURE(AppendScope(1, 10, 50));
    ASSERT_NO_FATAL_FAILURE(AppendScope(1, 20, 200));
    ASSERT_NO_FATAL_FAILURE(AppendScope(2, 0, 300));
    ASSERT_TRUE(Sorted<mdtid_LocalScope>());
    ASSERT_TRUE(Row<mdtid_LocalScope>(2).set_column_value(LocalScope_Length, 60));
    EXPECT_TRUE(Sorted<mdtid_LocalScope>());
    ASSERT_TRUE(Row<mdtid_LocalScope>(3).set_column_value(LocalScope_Length, 0xffffffff));
    EXPECT_TRUE(Sorted<mdtid_LocalScope>());
    ASSERT_TRUE(Row<mdtid_LocalScope>(2).set_column_value(LocalScope_Length, 101));
    EXPECT_FALSE(Sorted<mdtid_LocalScope>());
}

TEST_F(CppRowSorting, DescendingScopeCommitChecksBothNeighbors)
{
    for (uint32_t length : {10u, 60u, 200u})
    {
        ASSERT_NO_FATAL_FAILURE(Reset());
        ASSERT_NO_FATAL_FAILURE(AppendScope(1, 10, 100));
        ASSERT_NO_FATAL_FAILURE(AppendScope(1, 10, 20));
        {
            dnmd::added_row_t added;
            ASSERT_TRUE(md_insert_row_before(Row<mdtid_LocalScope>(2), &added));
            dnmd::row_writer writer{dnmd::raw_cursor<mdtid_LocalScope>{added}.as()};
            ASSERT_TRUE(writer.write(LocalScope_Method, CreateTokenType(mdtid_MethodDef) | 1));
            ASSERT_TRUE(writer.write(LocalScope_StartOffset, 10));
            ASSERT_TRUE(writer.write(LocalScope_Length, length));
            EXPECT_TRUE(Sorted<mdtid_LocalScope>());
        }
        EXPECT_EQ(length == 60, Sorted<mdtid_LocalScope>());
    }
}

TEST_F(CppRowSorting, ScopeStartOffsetIsComparedOnlyWithinAMethod)
{
    ASSERT_NO_FATAL_FAILURE(AppendScope(1, 10, 1));
    ASSERT_NO_FATAL_FAILURE(AppendScope(1, 20, 100));
    ASSERT_NO_FATAL_FAILURE(AppendScope(2, 0, 200));
    ASSERT_TRUE(Sorted<mdtid_LocalScope>());
    ASSERT_TRUE(Row<mdtid_LocalScope>(2).set_column_value(LocalScope_StartOffset, 30));
    EXPECT_TRUE(Sorted<mdtid_LocalScope>());
    ASSERT_TRUE(Row<mdtid_LocalScope>(3).set_column_value(LocalScope_Method, CreateTokenType(mdtid_MethodDef) | 3));
    EXPECT_TRUE(Sorted<mdtid_LocalScope>());
    ASSERT_TRUE(Row<mdtid_LocalScope>(2).set_column_value(LocalScope_StartOffset, 5));
    EXPECT_FALSE(Sorted<mdtid_LocalScope>());
}

TEST_F(CppRowSorting, ScopeConstantsAreComparedAsUnsigned)
{
    ASSERT_NO_FATAL_FAILURE(AppendScope(1, 0, 0xffffffff));
    ASSERT_NO_FATAL_FAILURE(AppendScope(1, 0, 0x80000000));
    ASSERT_NO_FATAL_FAILURE(AppendScope(1, 0, 1));
    ASSERT_NO_FATAL_FAILURE(AppendScope(1, 0xffffffff, 0xffffffff));
    ASSERT_TRUE(Sorted<mdtid_LocalScope>());
    ASSERT_TRUE(Row<mdtid_LocalScope>(3).set_column_value(LocalScope_Length, 0x90000000));
    EXPECT_FALSE(Sorted<mdtid_LocalScope>());
}
#endif
