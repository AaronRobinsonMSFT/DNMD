#include <dnmd.hpp>
#include "native.hpp"
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
            return position.value();
        }

        template<mdtable_id_t TableId>
        bool Sorted()
        {
            return dnmd::detail::CursorTable(Row<TableId>())->is_sorted;
        }

        void AppendParameter(uint32_t owner, uint32_t number)
        {
            auto added = dnmd::added_row<mdtid_GenericParam>::append(handle.get());
            ASSERT_TRUE(added);
            dnmd::row_writer writer{dnmd::raw_cursor<mdtid_GenericParam>{added}.value()};
            ASSERT_TRUE(writer.write(dnmd::columns::GenericParam_Owner, CreateTokenType(mdtid_TypeDef) | owner));
            ASSERT_TRUE(writer.write(dnmd::columns::GenericParam_Number, number));
        }

        void FillLayouts(std::initializer_list<uint32_t> parents)
        {
            for (uint32_t parent : parents)
            {
                auto added = dnmd::added_row<mdtid_ClassLayout>::append(handle.get());
                ASSERT_TRUE(added);
                auto row = dnmd::raw_cursor<mdtid_ClassLayout>{added}.value();
                ASSERT_TRUE(row.set_column_value(dnmd::columns::ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | parent));
            }
        }

#ifdef DNMD_PORTABLE_PDB
        void AppendScope(uint32_t method, uint32_t start, uint32_t length)
        {
            auto added = dnmd::added_row<mdtid_LocalScope>::append(handle.get());
            ASSERT_TRUE(added);
            dnmd::row_writer writer{dnmd::raw_cursor<mdtid_LocalScope>{added}.value()};
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
        auto added = dnmd::added_row<mdtid_GenericParam>::append(handle.get());
        ASSERT_TRUE(added);
        auto row = dnmd::raw_cursor<mdtid_GenericParam>{added}.value();
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
            auto added = dnmd::added_row<mdtid_ClassLayout>::insert(Row<mdtid_ClassLayout>(2), false);
            ASSERT_TRUE(added);
            auto row = dnmd::raw_cursor<mdtid_ClassLayout>{added}.value();
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
        auto added = append
            ? dnmd::added_row<mdtid_ClassLayout>::append(handle.get())
            : dnmd::added_row<mdtid_ClassLayout>::insert(Row<mdtid_ClassLayout>(), false);
        ASSERT_TRUE(added);
        auto row = added.value();
        ASSERT_TRUE(row.set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | (append ? 2 : 6)));
        EXPECT_TRUE(Sorted<mdtid_ClassLayout>());
        added.commit();
        EXPECT_FALSE(added);
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
        auto added = dnmd::added_row<mdtid_CustomAttribute>::append(handle.get());
        ASSERT_TRUE(added);
        auto row = dnmd::raw_cursor<mdtid_CustomAttribute>{added}.value();
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
    mdtable_t* table;
    {
        auto added = dnmd::added_row<mdtid_ENCLog>::append(handle.get());
        ASSERT_TRUE(added);
        auto row = dnmd::raw_cursor<mdtid_ENCLog>{added}.value();
        table = dnmd::detail::CursorTable(row);
        EXPECT_FALSE(table->is_sorted);
        ASSERT_TRUE(row.set_column_value(ENCLog_Token, 0xffffffff));
        auto moved = std::move(added);
        EXPECT_FALSE(added);
        EXPECT_EQ(handle.get(), added.get_handle());
        EXPECT_EQ(CreateTokenType(mdtid_ENCLog), dnmd_test::Token(added.position()));
        EXPECT_TRUE(moved);
        EXPECT_TRUE(table->is_adding_new_row);
    }
    EXPECT_FALSE(table->is_adding_new_row);
    EXPECT_FALSE(table->is_sorted);
}

TEST_F(CppRowSorting, ConsumingCommitDoesNotFinishANewerPendingRow)
{
    auto first = dnmd::added_row<mdtid_GenericParam>::append(handle.get());
    ASSERT_TRUE(first);
    auto row = first.value();
    first.commit();
    EXPECT_FALSE(first);
    EXPECT_EQ(handle.get(), first.get_handle());
    EXPECT_EQ(CreateTokenType(mdtid_GenericParam), dnmd_test::Token(first.position()));
    auto second = dnmd::added_row<mdtid_GenericParam>::append(handle.get());
    ASSERT_TRUE(second);
    first.commit();
    EXPECT_TRUE(dnmd::detail::CursorTable(row)->is_adding_new_row);
    second.commit();
    EXPECT_FALSE(dnmd::detail::CursorTable(row)->is_adding_new_row);
}

TEST_F(CppRowSorting, FailedSecondAcquisitionDoesNotCommitTheFirst)
{
    auto first = dnmd::added_row<mdtid_GenericParam>::append(handle.get());
    ASSERT_TRUE(first);
    auto table = dnmd::detail::CursorTable(first.value());
    {
        auto failed = dnmd::added_row<mdtid_GenericParam>::append(handle.get());
        EXPECT_FALSE(failed);
        EXPECT_EQ(handle.get(), failed.get_handle());
        EXPECT_EQ(CreateTokenType(mdtid_GenericParam), dnmd_test::Token(failed.position()));
        EXPECT_EQ(1u, table->row_count);
        EXPECT_TRUE(table->is_adding_new_row);
    }
    EXPECT_TRUE(table->is_adding_new_row);
    first.commit();
    EXPECT_FALSE(table->is_adding_new_row);
}

TEST_F(CppRowSorting, SelfMoveRetainsOwnershipAndMoveAssignmentCommitsTheDestination)
{
#ifdef DNMD_PORTABLE_PDB
    dnmd::handle_ptr other{md_create_new_pdb_handle()};
#else
    dnmd::handle_ptr other{md_create_new_handle()};
#endif
    ASSERT_NE(nullptr, other.get());
    ASSERT_NO_FATAL_FAILURE(FillLayouts({4}));
    auto destination = dnmd::added_row<mdtid_ClassLayout>::append(handle.get());
    ASSERT_TRUE(destination);
    ASSERT_TRUE(destination.value().set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | 2));
    auto destination_table = dnmd::detail::CursorTable(destination.value());
    auto& alias = destination;
    destination = std::move(alias);
    EXPECT_TRUE(destination);
    EXPECT_TRUE(destination_table->is_adding_new_row);
    EXPECT_TRUE(destination_table->is_sorted);
    auto source = dnmd::added_row<mdtid_ClassLayout>::append(other.get());
    ASSERT_TRUE(source);
    auto source_table = dnmd::detail::CursorTable(source.value());
    auto token = dnmd_test::Token(source.position());
    destination = std::move(source);
    EXPECT_FALSE(source);
    EXPECT_EQ(other.get(), source.get_handle());
    EXPECT_EQ(CreateTokenType(mdtid_ClassLayout), dnmd_test::Token(source.position()));
    EXPECT_EQ(other.get(), destination.get_handle());
    EXPECT_EQ(token, dnmd_test::Token(destination.position()));
    EXPECT_FALSE(destination_table->is_adding_new_row);
    EXPECT_FALSE(destination_table->is_sorted);
    EXPECT_TRUE(source_table->is_adding_new_row);
    destination.commit();
    EXPECT_FALSE(source_table->is_adding_new_row);
}

TEST_F(CppRowSorting, SortedListFailureCommitsThePartiallyAcquiredRow)
{
    auto owner = dnmd_test::AppendRow<mdtid_TypeDef>(handle.get());
    auto first_method = dnmd_test::AddListRow(owner, TypeDef_MethodList);
    auto first_parameter = dnmd_test::AddListRow(first_method, MethodDef_ParamList);
    auto second_method = dnmd_test::AddListRow(owner, TypeDef_MethodList);
    auto table = dnmd::detail::CursorTable(first_parameter);
    auto& details = table->column_details[static_cast<uint8_t>(Param_Sequence.column_index)];
    auto saved = details;
    details = static_cast<mdtcol_t>(details & ~mdtc_constant);
    auto failed = dnmd::md_add_new_row_to_sorted_list(second_method, MethodDef_ParamList, Param_Sequence, 7);
    details = saved;
    EXPECT_FALSE(failed);
    EXPECT_EQ(handle.get(), failed.get_handle());
    EXPECT_EQ(CreateTokenType(mdtid_Param), dnmd_test::Token(failed.position()));
    EXPECT_EQ(2u, table->row_count);
    EXPECT_FALSE(table->is_adding_new_row);
}

TEST_F(CppRowSorting, SortedListFactoryInfersTheChildAndInitializesItsSortColumn)
{
    auto owner = dnmd_test::AppendRow<mdtid_TypeDef>(handle.get());
    auto method = dnmd_test::AddListRow(owner, TypeDef_MethodList);
    for (uint32_t sequence : {3u, 2u, 1u, 4u, 3u, 3u})
    {
        auto added = dnmd::md_add_new_row_to_sorted_list(method, MethodDef_ParamList, Param_Sequence, sequence);
        static_assert(std::is_same_v<decltype(added), dnmd::added_row<mdtid_Param>>);
        ASSERT_TRUE(added);
        uint32_t actual;
        ASSERT_TRUE(added.value().get_column_value(Param_Sequence, &actual));
        EXPECT_EQ(sequence, actual);
    }
    auto parameters = dnmd::indirect_cursor<mdtid_Param, mdtid_ParamPtr>::nil(handle.get());
    uint32_t count;
    ASSERT_TRUE(method.get_column_value_as_range(MethodDef_ParamList, parameters, &count));
    ASSERT_EQ(6u, count);
    for (uint32_t expected : {1u, 2u, 3u, 3u, 3u, 4u})
    {
        ASSERT_TRUE(parameters);
        uint32_t actual;
        ASSERT_TRUE(parameters.value().get_column_value(Param_Sequence, &actual));
        EXPECT_EQ(expected, actual);
        ASSERT_TRUE(dnmd::md_cursor_next(parameters));
    }
    EXPECT_FALSE(parameters);
}

TEST_F(CppRowSorting, PointerInsertionFailureFinishesOnlyItsOwnPartialRow)
{
    auto first = dnmd_test::AppendRow<mdtid_TypeDef>(handle.get());
    (void)dnmd_test::AddListRow(first, TypeDef_MethodList);
    auto second = dnmd_test::AppendRow<mdtid_TypeDef>(handle.get());
    (void)dnmd_test::AddListRow(second, TypeDef_MethodList);
    (void)dnmd_test::AddListRow(first, TypeDef_MethodList);
    auto pending_pointer = dnmd::added_row<mdtid_MethodPtr>::append(handle.get());
    ASSERT_TRUE(pending_pointer);
    auto direct_table = &dnmd::detail::context(handle.get()).tables[mdtid_MethodDef];
    auto pointer_table = dnmd::detail::CursorTable(pending_pointer.value());
    auto before = direct_table->row_count;
    {
        auto failed = dnmd::md_add_new_row_to_list(second, TypeDef_MethodList);
        EXPECT_FALSE(failed);
        EXPECT_EQ(before + 1, direct_table->row_count);
        EXPECT_FALSE(direct_table->is_adding_new_row);
        EXPECT_TRUE(pointer_table->is_adding_new_row);
    }
    EXPECT_TRUE(pointer_table->is_adding_new_row);
    pending_pointer.commit();
    EXPECT_FALSE(pointer_table->is_adding_new_row);
}

TEST_F(CppRowSorting, InsertAfterFactoryOwnsAndCommitsItsRow)
{
    ASSERT_NO_FATAL_FAILURE(FillLayouts({2, 6}));
    auto added = dnmd::added_row<mdtid_ClassLayout>::insert(Row<mdtid_ClassLayout>(), true);
    ASSERT_TRUE(added);
    EXPECT_EQ(CreateTokenType(mdtid_ClassLayout) | 2, dnmd_test::Token(added.position()));
    ASSERT_TRUE(added.value().set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | 4));
    added.commit();
    EXPECT_TRUE(Sorted<mdtid_ClassLayout>());
    EXPECT_EQ(3u, dnmd::detail::CursorTable(Row<mdtid_ClassLayout>())->row_count);
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
    dnmd::raw_cursor<mdtid_GenericParam> start = dnmd::raw_cursor<mdtid_GenericParam>::nil(loaded.get());
    uint32_t count;
    ASSERT_EQ(dnmd::range_result::found, dnmd::md_find_range_from_cursor(first.value(), GenericParam_Owner,
        CreateTokenType(mdtid_TypeDef) | 1, start, &count));
    EXPECT_EQ(2u, count);
    EXPECT_EQ(1u, dnmd::detail::CursorRow(start));
    EXPECT_EQ(dnmd::range_result::not_supported, dnmd::md_find_range_from_cursor(first.value(), GenericParam_Number,
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
            auto added = dnmd::added_row<mdtid_LocalScope>::insert(Row<mdtid_LocalScope>(2), false);
            ASSERT_TRUE(added);
            dnmd::row_writer writer{dnmd::raw_cursor<mdtid_LocalScope>{added}.value()};
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
