#include <dnmd.hpp>
#include "internal.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

extern "C" void* alloc_mdmem(mdcxt_t* context, size_t length);

namespace
{
    using namespace dnmd;
    using namespace dnmd::columns;

    static_assert(detail::for_each_column<mdtid_TypeRef>([](auto... columns) {
        (static_cast<void>(columns), ...);
        return sizeof...(columns);
    }) == 3);
    static_assert(detail::for_each_column<mdtid_Unused>([](auto... columns) {
        return ((columns.column_index == 0) && ...);
    }));
    static_assert(!detail::for_each_column<mdtid_Unused>([](auto... columns) {
        return ((columns.column_index == 0) || ...);
    }));

    static_assert(std::is_same_v<detail::all_tables::value_type, mdtable_id_t>);
    static_assert(detail::all_tables::size() == mdtid_End);
    static_assert(std::is_same_v<typename detail::table_range<mdtid_TypeRef, mdtid_TypeRef>::type,
        detail::table_sequence<>>);
    static_assert(std::is_same_v<typename detail::table_range<mdtid_TypeRef, mdtid_FieldPtr>::type,
        detail::table_sequence<mdtid_TypeRef, mdtid_TypeDef>>);
#ifdef DNMD_PORTABLE_PDB
    static_assert(detail::non_pdb_tables::size() == mdtid_FirstPdb);
    static_assert(std::is_same_v<detail::pdb_tables, detail::table_sequence<
        mdtid_Document, mdtid_MethodDebugInformation, mdtid_LocalScope, mdtid_LocalVariable,
        mdtid_LocalConstant, mdtid_ImportScope, mdtid_StateMachineMethod, mdtid_CustomDebugInformation>>);
#else
    static_assert(std::is_same_v<detail::non_pdb_tables, detail::all_tables>);
#endif

    template<typename Owner, typename List, typename... Sort>
    auto SupportsListAdd(int) -> decltype(detail::add_row_to_list(
        std::declval<Owner>(), std::declval<List>(), std::declval<Sort>()...,
        std::declval<mdtable_t**>(), std::declval<uint32_t*>()), std::true_type{});

    template<typename Owner, typename List, typename... Sort>
    auto SupportsListAdd(...) -> std::false_type;

    template<typename Owner, typename List, typename... Sort>
    inline constexpr bool can_add_list_v = decltype(SupportsListAdd<Owner, List, Sort...>(0))::value;

    static_assert(can_add_list_v<cursor<mdtid_MethodDef>, decltype(MethodDef_ParamList)>);
    static_assert(can_add_list_v<cursor<mdtid_MethodDef>, decltype(MethodDef_ParamList),
        decltype(Param_Sequence), uint32_t>);
    static_assert(!can_add_list_v<cursor<mdtid_MethodDef>, col_index_t>);
    static_assert(!can_add_list_v<cursor<mdtid_TypeDef>, decltype(MethodDef_ParamList)>);
    static_assert(!can_add_list_v<cursor<mdtid_MethodDef>, decltype(MethodDef_ParamList),
        decltype(Field_Flags), uint32_t>);
    static_assert(!can_add_list_v<cursor<mdtid_MethodDef>, decltype(MethodDef_ParamList),
        decltype(Param_Name), uint32_t>);

    class CoreAlgorithms : public testing::Test
    {
    protected:
        handle_ptr base;
        handle_ptr delta;

        void SetUp() override
        {
            base.reset(md_create_new_handle());
            delta.reset(md_create_new_handle());
            ASSERT_NE(nullptr, base.get());
            ASSERT_NE(nullptr, delta.get());
        }

#ifdef DNMD_PORTABLE_PDB
        std::array<uint8_t, 116> pdb_image{};

        handle_ptr CreatePdb()
        {
            std::memcpy(pdb_image.data(), "BSJB", 4);
            pdb_image[4] = 1;
            pdb_image[6] = 1;
            pdb_image[12] = 12;
            std::memcpy(pdb_image.data() + 16, "PDB v1.0", 8);
            pdb_image[30] = 2;
            pdb_image[32] = 60;
            pdb_image[36] = 32;
            std::memcpy(pdb_image.data() + 40, "#Pdb", 4);
            pdb_image[48] = 92;
            pdb_image[52] = 24;
            std::memcpy(pdb_image.data() + 56, "#~", 2);
            pdb_image[96] = 2;
            pdb_image[99] = 1;
            mdhandle_t raw = nullptr;
            EXPECT_TRUE(md_create_handle(pdb_image.data(), pdb_image.size(), &raw));
            return handle_ptr{raw};
        }
#endif

        template<mdtable_id_t Table>
        raw_cursor<Table> AddRow(mdhandle_t handle)
        {
            auto added = added_row<Table>::append(handle);
            EXPECT_TRUE(added);
            return added.position();
        }

        raw_cursor<mdtid_MethodDef> AddMethod(mdhandle_t handle)
        {
            auto owner = md_create_cursor<mdtid_TypeDef>(handle);
            auto added = md_add_new_row_to_list(owner.value(), TypeDef_MethodList);
            EXPECT_TRUE(added);
            return added.position();
        }

        raw_cursor<mdtid_Param> AddParameter(raw_cursor<mdtid_MethodDef> method, uint32_t sequence)
        {
            auto added = md_add_new_row_to_list(method.value(), MethodDef_ParamList);
            EXPECT_TRUE(added);
            EXPECT_TRUE(added.value().set_column_value(Param_Sequence, sequence));
            return added.position();
        }

        bool ShiftReferences(mdtable_id_t table, uint32_t first, int64_t shift)
        {
            return update_table_references_for_shifted_rows(&detail::context(base.get()), table, first, shift);
        }

        void Log(mdToken token, uint32_t operation = 0)
        {
            auto row = AddRow<mdtid_ENCLog>(delta.get());
            ASSERT_TRUE(row);
            row_writer writer{row.value()};
            ASSERT_TRUE(writer.write(ENCLog_Token, token));
            ASSERT_TRUE(writer.write(ENCLog_Op, operation));
        }

        void Map(mdToken token)
        {
            auto row = AddRow<mdtid_ENCMap>(delta.get());
            ASSERT_TRUE(row);
            ASSERT_TRUE(row.value().set_column_value(ENCMap_Token, token));
        }

        void Minimal()
        {
            auto& flags = detail::context(delta.get()).context_flags;
            flags = static_cast<mdcxt_flag_t>(flags | mdc_minimal_delta);
        }

        std::vector<uint8_t> Save(mdhandle_t handle)
        {
            size_t size = 0;
            EXPECT_FALSE(md_write_to_buffer(handle, nullptr, &size));
            std::vector<uint8_t> bytes(size);
            EXPECT_TRUE(md_write_to_buffer(handle, bytes.data(), &size));
            return bytes;
        }

        template<mdtable_id_t Table, typename Heap>
        void Rebase(cursor<Table> row, column<Table, Heap> col, size_t base_size)
        {
            auto offset = detail::row_storage_access::read(row_reader{row}, col);
            ASSERT_LE(base_size, UINT32_MAX - offset);
            if (offset != 0)
                ASSERT_TRUE(detail::row_storage_access::write_heap_index(
                    row_writer{row}, col, offset + static_cast<uint32_t>(base_size)));
        }

        template<mdtable_id_t Child>
        static constexpr auto NameColumn()
        {
            if constexpr (Child == mdtid_MethodDef) return MethodDef_Name;
            else if constexpr (Child == mdtid_Field) return Field_Name;
            else if constexpr (Child == mdtid_Param) return Param_Name;
            else if constexpr (Child == mdtid_Property) return Property_Name;
            else return Event_Name;
        }

        template<mdtable_id_t Owner, mdtable_id_t Child, mdtable_id_t Ptr>
        void CheckCreation(raw_cursor<Owner> parent, raw_cursor<Owner> source_parent,
            column<Owner, indirect_cursor<Child, Ptr>> list, uint32_t operation)
        {
            if constexpr (Owner == mdtid_EventMap)
            {
                ASSERT_TRUE(parent.value().set_column_value(EventMap_Parent, CreateTokenType(mdtid_TypeDef) | 1));
                ASSERT_TRUE(source_parent.value().set_column_value(EventMap_Parent, CreateTokenType(mdtid_TypeDef) | 1));
            }
            else if constexpr (Owner == mdtid_PropertyMap)
            {
                ASSERT_TRUE(parent.value().set_column_value(PropertyMap_Parent, CreateTokenType(mdtid_TypeDef) | 1));
                ASSERT_TRUE(source_parent.value().set_column_value(PropertyMap_Parent, CreateTokenType(mdtid_TypeDef) | 1));
            }
            auto added = md_add_new_row_to_list(source_parent.value(), list);
            ASSERT_TRUE(added);
            ASSERT_TRUE(added.value().set_column_value(NameColumn<Child>(), "Added"));
            ASSERT_NO_FATAL_FAILURE(Rebase(added.value(), NameColumn<Child>(),
                detail::context(base.get()).strings_heap.size));
            mdToken token;
            mdToken parent_token;
            ASSERT_TRUE(added.get_token(token));
            ASSERT_TRUE(parent.get_token(parent_token));
            added.commit();
            ASSERT_NO_FATAL_FAILURE(Log(parent_token, operation));
            ASSERT_NO_FATAL_FAILURE(Log(token));
            Minimal();
            ASSERT_TRUE(md_apply_delta(base.get(), delta.get()));

            auto result = raw_cursor<Child>{base.get(), token};
            ASSERT_TRUE(result);
            char const* name;
            ASSERT_TRUE(result.value().get_column_value(NameColumn<Child>(), &name));
            EXPECT_STREQ("Added", name);
            auto range = indirect_cursor<Child, Ptr>::nil(base.get());
            uint32_t count;
            ASSERT_TRUE(row_reader{parent.value()}.read(list, range, &count));
            EXPECT_EQ(1u, count);
            ASSERT_TRUE(range);
            mdToken actual;
            ASSERT_TRUE(range.get_target_token(actual));
            EXPECT_EQ(token, actual);
            EXPECT_FALSE(detail::CursorTable(result)->is_adding_new_row);
        }

        void CheckParameterSort(bool existing_pointer, bool duplicate)
        {
            auto method = AddMethod(base.get());
            ASSERT_TRUE(method);
            auto first = AddParameter(method, duplicate ? 1u : 2u);
            auto second = AddParameter(method, 1);
            ASSERT_TRUE(first);
            ASSERT_TRUE(second);
            auto& context = detail::context(base.get());
            if (existing_pointer)
            {
                ASSERT_TRUE((detail::create_and_fill_indirect_table<mdtid_Param, mdtid_ParamPtr>(&context)));
                auto& details = context.tables[mdtid_MethodDef].column_details[
                    static_cast<uint8_t>(MethodDef_ParamList.column_index)];
                details = static_cast<mdtcol_t>((static_cast<uint32_t>(details) & ~mdtc_timask)
                    | (uint32_t{mdtid_ParamPtr} << 16));
            }
            auto source_method = AddMethod(delta.get());
            ASSERT_TRUE(source_method);
            ASSERT_TRUE(AddParameter(source_method, 0));
            auto token = CreateTokenType(mdtid_Param) | 3;
            ASSERT_NO_FATAL_FAILURE(Map(token));
            ASSERT_NO_FATAL_FAILURE(Log(CreateTokenType(mdtid_MethodDef) | 1, 3));
            ASSERT_NO_FATAL_FAILURE(Log(token));
            Minimal();
            ASSERT_TRUE(md_apply_delta(base.get(), delta.get()));
            EXPECT_TRUE(context.tables[mdtid_Param].has_indirect_table);
            auto range = indirect_cursor<mdtid_Param, mdtid_ParamPtr>::nil(base.get());
            uint32_t count;
            ASSERT_TRUE(row_reader{method.value()}.read(MethodDef_ParamList, range, &count));
            ASSERT_EQ(3u, count);
            std::vector<mdToken> tokens;
            std::vector<uint32_t> sequences;
            for (uint32_t i = 0; i < count; ++i, range.move(1))
            {
                ASSERT_TRUE(range);
                uint32_t value;
                mdToken current;
                ASSERT_TRUE(range.value().get_column_value(Param_Sequence, &value));
                ASSERT_TRUE(range.get_target_token(current));
                tokens.push_back(current);
                sequences.push_back(value);
            }
            EXPECT_TRUE(std::is_sorted(sequences.begin(), sequences.end()));
            std::vector<mdToken> expected{token,
                CreateTokenType(mdtid_Param) | (duplicate ? 1u : 2u),
                CreateTokenType(mdtid_Param) | (duplicate ? 2u : 1u)};
            EXPECT_EQ(expected, tokens);
        }
    };

    template<mdtable_id_t... Tables>
    void CheckColumnOrder(detail::table_sequence<Tables...>)
    {
        auto check = [](auto table) {
            constexpr auto id = decltype(table)::value;
            uint8_t index = 0;
            auto check_index = [&](auto col) {
                EXPECT_EQ(index++, static_cast<uint8_t>(col.column_index));
            };
            (void)check_index;
            detail::for_each_column<id>([&](auto... cols) {
                (check_index(cols), ...);
            });
        };
        (check(std::integral_constant<mdtable_id_t, Tables>{}), ...);
    }
}

TEST(CoreSchema, ForEachColumnPreservesOrderAndShortCircuiting)
{
    using namespace dnmd;
    std::vector<uint8_t> visited;
    auto visit = [&](auto col) { visited.push_back(static_cast<uint8_t>(col.column_index)); };
    detail::for_each_column<mdtid_TypeRef>([&](auto... columns) { (visit(columns), ...); });
    EXPECT_EQ((std::vector<uint8_t>{0, 1, 2}), visited);

    visited.clear();
    auto until_second = [&](auto col) {
        visit(col);
        return static_cast<uint8_t>(col.column_index) != 1;
    };
    EXPECT_FALSE(detail::for_each_column<mdtid_TypeRef>([&](auto... columns) {
        return (until_second(columns) && ...);
    }));
    EXPECT_EQ((std::vector<uint8_t>{0, 1}), visited);

    visited.clear();
    auto find_second = [&](auto col) {
        visit(col);
        return static_cast<uint8_t>(col.column_index) == 1;
    };
    EXPECT_TRUE(detail::for_each_column<mdtid_TypeRef>([&](auto... columns) {
        return (find_second(columns) || ...);
    }));
    EXPECT_EQ((std::vector<uint8_t>{0, 1}), visited);
}

TEST(CoreSchema, ForEachColumnForwardsCallableAndResult)
{
    auto move_only = [value = std::make_unique<int>(7)](auto... columns) mutable {
        (static_cast<void>(columns), ...);
        return std::pair{std::move(value), sizeof...(columns)};
    };
    auto result = dnmd::detail::for_each_column<mdtid_TypeRef>(std::move(move_only));
    ASSERT_NE(nullptr, result.first);
    EXPECT_EQ(7, *result.first);
    EXPECT_EQ(3u, result.second);

    int calls = 0;
    auto reference = [&](auto...) -> int& { return ++calls; };
    static_assert(std::is_same_v<decltype(dnmd::detail::for_each_column<mdtid_TypeRef>(reference)), int&>);
    EXPECT_EQ(&calls, &dnmd::detail::for_each_column<mdtid_TypeRef>(reference));
    EXPECT_EQ(1, calls);
}

TEST(CoreSchema, ForEachColumnInvokesEmptySchemasWithoutArguments)
{
    int calls = 0;
    EXPECT_EQ(17, dnmd::detail::for_each_column<mdtid_Unused>([&] { ++calls; return 17; }));
    EXPECT_EQ(1, calls);
#ifdef DNMD_PORTABLE_PDB
    EXPECT_EQ(0u, dnmd::detail::for_each_column<static_cast<mdtable_id_t>(0x2d)>([](auto... columns) {
        (static_cast<void>(columns), ...);
        return sizeof...(columns);
    }));
#endif
}

TEST_F(CoreAlgorithms, TypedSchemasPreserveColumnOrder)
{
    CheckColumnOrder(detail::all_tables{});
}

TEST_F(CoreAlgorithms, InitializesMinimalRowsWithoutGuidAllocation)
{
    auto module = md_create_cursor<mdtid_Module>(base.get());
    auto global = md_create_cursor<mdtid_TypeDef>(base.get());
    ASSERT_TRUE(module);
    ASSERT_TRUE(global);
    auto& context = detail::context(base.get());
    EXPECT_EQ(1u, context.tables[mdtid_Module].row_count);
    EXPECT_EQ(1u, context.tables[mdtid_TypeDef].row_count);
    EXPECT_EQ(0u, context.guid_heap.size);
    EXPECT_EQ(0u, detail::row_storage_access::read(row_reader{module.value()}, Module_Mvid));
    char const* name;
    ASSERT_TRUE(global.value().get_column_value(TypeDef_TypeName, &name));
    EXPECT_STREQ("<Module>", name);
    EXPECT_FALSE(context.tables[mdtid_Module].is_adding_new_row);
    EXPECT_FALSE(context.tables[mdtid_TypeDef].is_adding_new_row);
    EXPECT_TRUE(md_validate(base.get()));
}

TEST_F(CoreAlgorithms, RawHeapIndexWritesPreserveHeapsAndCheckWidth)
{
    auto module = md_create_cursor<mdtid_Module>(base.get()).value();
    row_writer writer{module};
    EXPECT_FALSE(detail::row_storage_access::write_heap_index(writer, Module_Mvid, UINT16_MAX + 1u));
    EXPECT_EQ(0u, detail::context(base.get()).guid_heap.size);
    std::string large(65536, 'x');
    ASSERT_TRUE(writer.write(Module_Name, large.c_str()));
    ASSERT_TRUE(writer.write(Module_Name, "AfterGrowth"));
    auto offset = detail::row_storage_access::read(row_reader{module}, Module_Name);
    ASSERT_GT(offset, UINT16_MAX);
    auto size = detail::context(base.get()).strings_heap.size;
    ASSERT_TRUE(detail::row_storage_access::write_heap_index(writer, Module_Name, offset));
    EXPECT_EQ(size, detail::context(base.get()).strings_heap.size);
    char const* name;
    ASSERT_TRUE(module.get_column_value(Module_Name, &name));
    EXPECT_STREQ("AfterGrowth", name);
}

TEST_F(CoreAlgorithms, AppendAndInsertPreserveExistingRows)
{
    for (uint32_t row = 1; row <= 3; ++row)
    {
        auto added = added_row<mdtid_ModuleRef>::append(base.get());
        ASSERT_TRUE(added);
        mdToken token;
        ASSERT_TRUE(added.get_token(token));
        EXPECT_EQ(CreateTokenType(mdtid_ModuleRef) | row, token);
        ASSERT_TRUE(added.value().set_column_value(ModuleRef_Name, row == 1 ? "First" : "Next"));
    }
    for (auto edit : {std::pair{1u, false}, std::pair{2u, true}})
    {
        auto relative = raw_cursor<mdtid_ModuleRef>::at(base.get(), edit.first);
        ASSERT_TRUE(relative);
        auto added = added_row<mdtid_ModuleRef>::insert(relative.value(), edit.second);
        ASSERT_TRUE(added);
        EXPECT_EQ(edit.first + (edit.second ? 1u : 0u), detail::CursorRow(added.position()));
    }
    auto last = added_row<mdtid_ModuleRef>::append(base.get());
    ASSERT_TRUE(last);
    last.commit();
    auto bytes = Save(base.get());
    mdhandle_t raw;
    ASSERT_TRUE(md_create_handle(bytes.data(), bytes.size(), &raw));
    handle_ptr loaded{raw};
    for (auto handle : {base.get(), loaded.get()})
    {
        EXPECT_EQ(6u, detail::context(handle).tables[mdtid_ModuleRef].row_count);
        auto position = md_create_cursor<mdtid_ModuleRef>(handle);
        for (auto expected : {"", "First", "", "Next", "Next", ""})
        {
            ASSERT_TRUE(position);
            char const* name;
            ASSERT_TRUE(position.value().get_column_value(ModuleRef_Name, &name));
            EXPECT_STREQ(expected, name);
            ASSERT_TRUE(position.move(1));
        }
        EXPECT_FALSE(position);
    }
}

TEST_F(CoreAlgorithms, SortedListInsertionPreservesOwnerRangesAndKeys)
{
    auto first = AddMethod(base.get());
    ASSERT_TRUE(first);
    ASSERT_TRUE(AddParameter(first, 5));
    auto second = AddMethod(base.get());
    ASSERT_TRUE(second);
    ASSERT_TRUE(AddParameter(second, 4));
    for (auto item : {std::pair{1u, 1u},
        std::pair{1u, 3u}, std::pair{2u, 0u}, std::pair{1u, 3u}})
    {
        SCOPED_TRACE(testing::Message() << "parent=" << item.first << " sequence=" << item.second);
        auto parent = CreateTokenType(mdtid_MethodDef) | item.first;
        auto native_parent = raw_cursor<mdtid_MethodDef>{base.get(), parent};
        ASSERT_TRUE(native_parent);
        auto added = md_add_new_row_to_sorted_list(native_parent.value(), MethodDef_ParamList,
            Param_Sequence, item.second);
        ASSERT_TRUE(added);
        uint32_t sequence;
        mdToken owner;
        ASSERT_TRUE(added.value().get_column_value(Param_Sequence, &sequence));
        EXPECT_EQ(item.second, sequence);
        ASSERT_TRUE(added.value().get_parent_token(owner));
        EXPECT_EQ(parent, owner);
    }
    for (uint32_t owner : {1u, 2u})
    {
        auto parent = raw_cursor<mdtid_MethodDef>::at(base.get(), owner).value();
        auto range = indirect_cursor<mdtid_Param, mdtid_ParamPtr>::nil(base.get());
        uint32_t count;
        ASSERT_TRUE(row_reader{parent}.read(MethodDef_ParamList, range, &count));
        std::vector<uint32_t> sequences;
        for (uint32_t index = 0; index < count; ++index, range.move(1))
        {
            ASSERT_TRUE(range);
            uint32_t sequence;
            ASSERT_TRUE(range.value().get_column_value(Param_Sequence, &sequence));
            sequences.push_back(sequence);
        }
        std::sort(sequences.begin(), sequences.end());
        EXPECT_EQ(owner == 1 ? (std::vector<uint32_t>{1, 3, 3, 5}) : (std::vector<uint32_t>{0, 4}), sequences);
    }
    EXPECT_TRUE(detail::context(base.get()).tables[mdtid_Param].has_indirect_table);
}

TEST_F(CoreAlgorithms, UnsortedListInsertionPreservesOwners)
{
    auto first_owner = md_create_cursor<mdtid_TypeDef>(base.get());
    ASSERT_TRUE(first_owner);
    auto field = md_add_new_row_to_list(first_owner.value(), TypeDef_FieldList);
    ASSERT_TRUE(field);
    field.commit();
    ASSERT_TRUE(AddRow<mdtid_TypeDef>(base.get()));
    for (uint32_t owner : {2u, 1u, 1u, 2u})
    {
        auto native_parent = raw_cursor<mdtid_TypeDef>::at(base.get(), owner);
        ASSERT_TRUE(native_parent);
        auto added = md_add_new_row_to_list(native_parent.value(), TypeDef_FieldList);
        ASSERT_TRUE(added);
        mdToken parent;
        ASSERT_TRUE(added.value().get_parent_token(parent));
        EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | owner, parent);
    }
    EXPECT_EQ(5u, detail::context(base.get()).tables[mdtid_Field].row_count);
    EXPECT_TRUE(detail::context(base.get()).tables[mdtid_Field].has_indirect_table);
}

TEST_F(CoreAlgorithms, FailedAcquisitionsFinishTheirRowsWithoutCommittingAnotherGuard)
{
    auto method = AddMethod(base.get());
    ASSERT_TRUE(method);
    ASSERT_TRUE(AddParameter(method, 0));
    ASSERT_TRUE(AddMethod(base.get()));
    auto invalid_sort = column<mdtid_Param, column_type_constant>{Param_Name.column_index};
    auto native_parent = raw_cursor<mdtid_MethodDef>::at(base.get(), 2);
    ASSERT_TRUE(native_parent);
    EXPECT_FALSE(added_row<mdtid_Param>::in_list(native_parent.value(), MethodDef_ParamList, invalid_sort, 7));
    auto& context = detail::context(base.get());
    EXPECT_EQ(2u, context.tables[mdtid_Param].row_count);
    EXPECT_FALSE(context.tables[mdtid_Param].is_adding_new_row);
    auto pending = added_row<mdtid_ModuleRef>::append(base.get());
    ASSERT_TRUE(pending);
    EXPECT_FALSE(added_row<mdtid_ModuleRef>::append(base.get()));
    uint32_t added = 99;
    EXPECT_FALSE(detail::append_row_to_table<mdtid_ModuleRef>(&context, &added));
    EXPECT_EQ(0u, added);
    EXPECT_TRUE(context.tables[mdtid_ModuleRef].is_adding_new_row);
    pending.commit();
    EXPECT_FALSE(context.tables[mdtid_ModuleRef].is_adding_new_row);
}

TEST_F(CoreAlgorithms, IndirectionFailureFinishesItsChildAndReturnsNoOwnedRow)
{
    auto first = md_create_cursor<mdtid_TypeDef>(base.get());
    ASSERT_TRUE(first);
    auto first_method = md_add_new_row_to_list(first.value(), TypeDef_MethodList);
    ASSERT_TRUE(first_method);
    first_method.commit();
    auto second = AddRow<mdtid_TypeDef>(base.get());
    ASSERT_TRUE(second);
    auto second_method = md_add_new_row_to_list(second.value(), TypeDef_MethodList);
    ASSERT_TRUE(second_method);
    second_method.commit();
    auto another = md_add_new_row_to_list(first.value(), TypeDef_MethodList);
    ASSERT_TRUE(another);
    another.commit();
    auto pending = added_row<mdtid_MethodPtr>::append(base.get());
    ASSERT_TRUE(pending);
    mdtable_t* added_table = nullptr;
    uint32_t added_row = 0;
    auto native_parent = raw_cursor<mdtid_TypeDef>::at(base.get(), 2);
    ASSERT_TRUE(native_parent);
    EXPECT_FALSE(detail::add_row_to_list(native_parent.value(), TypeDef_MethodList, &added_table, &added_row));
    EXPECT_EQ(nullptr, added_table);
    EXPECT_EQ(0u, added_row);
    auto& context = detail::context(base.get());
    EXPECT_EQ(4u, context.tables[mdtid_MethodDef].row_count);
    EXPECT_FALSE(context.tables[mdtid_MethodDef].is_adding_new_row);
    EXPECT_EQ(detail::CursorRow(pending.position()), context.tables[mdtid_MethodPtr].row_count);
    EXPECT_TRUE(context.tables[mdtid_MethodPtr].is_adding_new_row);
    pending.commit();
    EXPECT_FALSE(context.tables[mdtid_MethodPtr].is_adding_new_row);
}

TEST_F(CoreAlgorithms, RowCommitUsesCompositeKeyOrdering)
{
    ASSERT_TRUE(AddRow<mdtid_TypeDef>(base.get()));
    auto append = [&](uint32_t owner, uint32_t number) {
        auto added = added_row<mdtid_GenericParam>::append(base.get());
        ASSERT_TRUE(added);
        row_writer writer{added.value()};
        ASSERT_TRUE(writer.write(GenericParam_Owner, CreateTokenType(mdtid_TypeDef) | owner));
        ASSERT_TRUE(writer.write(GenericParam_Number, number));
        added.commit();
    };
    ASSERT_NO_FATAL_FAILURE(append(1, 9));
    ASSERT_NO_FATAL_FAILURE(append(2, 0));
    auto& table = detail::context(base.get()).tables[mdtid_GenericParam];
    EXPECT_TRUE(table.is_sorted);
    ASSERT_NO_FATAL_FAILURE(append(2, 1));
    EXPECT_TRUE(table.is_sorted);
    ASSERT_NO_FATAL_FAILURE(append(2, 0));
    EXPECT_FALSE(table.is_sorted);
}

TEST_F(CoreAlgorithms, CoordinateEditValidationClearsOnlyItsOutputs)
{
    uint32_t row = 99;
    EXPECT_FALSE(detail::append_row_to_table<mdtid_TypeDef>(nullptr, &row));
    EXPECT_EQ(0u, row);
    EXPECT_FALSE(added_row<mdtid_Field>::append(base.get()));
    EXPECT_FALSE(added_row<mdtid_FieldPtr>::append(base.get()));
    EXPECT_FALSE(detail::append_row_to_table<mdtid_TypeRef>(&detail::context(base.get()), nullptr));
    auto table = &detail::context(base.get()).tables[mdtid_TypeDef];
    for (uint32_t position : {0u, 3u, UINT32_MAX})
    {
        row = 99;
        auto relative = raw_cursor<mdtid_TypeDef>::at(base.get(), position);
        EXPECT_FALSE(detail::insert_row_relative(relative, false, &row));
        EXPECT_EQ(0u, row);
    }
    mdtable_t* target = &detail::context(base.get()).tables[mdtid_Field];
    row = 99;
    auto invalid_list = column<mdtid_TypeDef, indirect_cursor<mdtid_Field, mdtid_FieldPtr>>{
        TypeDef_Flags.column_index};
    auto native_parent = md_create_cursor<mdtid_TypeDef>(base.get());
    ASSERT_TRUE(native_parent);
    EXPECT_FALSE(detail::add_row_to_list(native_parent.value(), invalid_list, &target, &row));
    EXPECT_EQ(nullptr, target);
    EXPECT_EQ(0u, row);
    EXPECT_EQ(1u, table->row_count);
    EXPECT_FALSE(table->is_adding_new_row);
#ifdef DNMD_PORTABLE_PDB
    EXPECT_FALSE(added_row<static_cast<mdtable_id_t>(0x2d)>::append(base.get()));
#endif
}

TEST_F(CoreAlgorithms, ShiftedDirectReferencesCoverEveryRowAndSupportBackwardShifts)
{
    for (uint32_t index = 1; index < 4; ++index)
        ASSERT_TRUE(AddRow<mdtid_TypeDef>(base.get()));
    for (uint32_t rid : {0u, 1u, 2u, 4u})
    {
        auto row = AddRow<mdtid_ClassLayout>(base.get());
        ASSERT_TRUE(row);
        ASSERT_TRUE(row.value().set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | rid));
        ASSERT_TRUE(row.value().set_column_value(ClassLayout_ClassSize, rid + 100));
    }
    ASSERT_TRUE(ShiftReferences(mdtid_TypeDef, 2, 2));
    uint32_t index = 1;
    for (uint32_t expected : {0u, 1u, 4u, 6u})
    {
        auto row = raw_cursor<mdtid_ClassLayout>::at(base.get(), index++);
        mdToken token;
        ASSERT_TRUE(row.value().get_column_value(ClassLayout_Parent, token));
        EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | expected, token);
    }
    ASSERT_TRUE(ShiftReferences(mdtid_TypeDef, 4, -2));
    index = 1;
    for (uint32_t expected : {0u, 1u, 2u, 4u})
    {
        auto row = raw_cursor<mdtid_ClassLayout>::at(base.get(), index++);
        mdToken token;
        uint32_t size;
        ASSERT_TRUE(row.value().get_column_value(ClassLayout_Parent, token));
        ASSERT_TRUE(row.value().get_column_value(ClassLayout_ClassSize, &size));
        EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | expected, token);
        EXPECT_EQ(expected + 100, size);
    }
}

TEST_F(CoreAlgorithms, ShiftedCodedReferencesPreserveOtherTagsAndNilValues)
{
    std::vector<mdToken> tokens{
        CreateTokenType(mdtid_TypeDef) | 1,
        CreateTokenType(mdtid_TypeRef) | 2,
        CreateTokenType(mdtid_TypeDef) | 2,
        CreateTokenType(mdtid_TypeDef),
        CreateTokenType(mdtid_TypeRef),
        CreateTokenType(mdtid_TypeDef) | 3};
    for (auto token : tokens)
    {
        auto row = AddRow<mdtid_MemberRef>(base.get());
        ASSERT_TRUE(row);
        ASSERT_TRUE(row.value().set_column_value(MemberRef_Class, token));
    }
    ASSERT_TRUE(ShiftReferences(mdtid_TypeDef, 2, 1));
    tokens[2] = CreateTokenType(mdtid_TypeDef) | 3;
    tokens[5] = CreateTokenType(mdtid_TypeDef) | 4;
    uint32_t index = 1;
    for (auto expected : tokens)
    {
        mdToken token;
        auto row = raw_cursor<mdtid_MemberRef>::at(base.get(), index++);
        ASSERT_TRUE(row.value().get_column_value(MemberRef_Class, token));
        EXPECT_EQ(expected, token);
    }
}

TEST_F(CoreAlgorithms, ShiftedListReferencesUseTheActiveDirectOrPointerTable)
{
    ASSERT_TRUE(AddMethod(base.get()));
    ASSERT_TRUE(AddMethod(base.get()));
    auto second = AddRow<mdtid_TypeDef>(base.get());
    ASSERT_TRUE(second);
    auto first = md_create_cursor<mdtid_TypeDef>(base.get());
    ASSERT_TRUE(first);
    auto& context = detail::context(base.get());
    auto table = &context.tables[mdtid_TypeDef];
    auto column = static_cast<uint8_t>(TypeDef_MethodList.column_index);
    auto expect = [&](mdtable_id_t target, uint32_t first_rid, uint32_t second_rid) {
        mdToken token;
        ASSERT_TRUE(first.value().get_column_value(TypeDef_MethodList, token));
        EXPECT_EQ(CreateTokenType(target) | first_rid, token);
        ASSERT_TRUE(second.value().get_column_value(TypeDef_MethodList, token));
        EXPECT_EQ(CreateTokenType(target) | second_rid, token);
    };
    ASSERT_TRUE(ShiftReferences(mdtid_MethodDef, 1, 1));
    ASSERT_NO_FATAL_FAILURE(expect(mdtid_MethodDef, 2, 4));
    ASSERT_TRUE(ShiftReferences(mdtid_MethodDef, 2, -1));
    ASSERT_NO_FATAL_FAILURE(expect(mdtid_MethodDef, 1, 3));

    ASSERT_TRUE((detail::create_and_fill_indirect_table<mdtid_MethodDef, mdtid_MethodPtr>(&context)));
    auto& details = table->column_details[column];
    details = static_cast<mdtcol_t>((static_cast<uint32_t>(details) & ~mdtc_timask)
        | (uint32_t{mdtid_MethodPtr} << 16));
    ASSERT_TRUE(ShiftReferences(mdtid_MethodDef, 1, 1));
    ASSERT_NO_FATAL_FAILURE(expect(mdtid_MethodPtr, 1, 3));
    auto pointer_column = detail::pointer_column<mdtid_MethodPtr>();
    for (uint32_t index : {1u, 2u})
    {
        mdToken token;
        auto row = raw_cursor<mdtid_MethodPtr>::at(base.get(), index);
        ASSERT_TRUE(row.value().get_column_value(pointer_column, token));
        EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | (index + 1), token);
    }
    ASSERT_TRUE(ShiftReferences(mdtid_MethodDef, 2, -1));
    ASSERT_TRUE(ShiftReferences(mdtid_MethodPtr, 2, 1));
    ASSERT_NO_FATAL_FAILURE(expect(mdtid_MethodPtr, 1, 4));
    ASSERT_TRUE(ShiftReferences(mdtid_MethodPtr, 3, -1));
    for (uint32_t index : {1u, 2u})
    {
        mdToken token;
        auto row = raw_cursor<mdtid_MethodPtr>::at(base.get(), index);
        ASSERT_TRUE(row.value().get_column_value(pointer_column, token));
        EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | index, token);
    }
}

TEST_F(CoreAlgorithms, ShiftedReferencesSkipEmptyAndAbsentTablesAndNonReferenceColumns)
{
    auto& context = detail::context(base.get());
    auto row = md_create_cursor<mdtid_TypeDef>(base.get()).value();
    ASSERT_TRUE(row.set_column_value(TypeDef_Flags, 0x1234));
    ASSERT_TRUE(row.set_column_value(TypeDef_TypeName, "Preserved"));
    ASSERT_TRUE(detail::initialize_new_table_details<mdtid_ClassLayout>(&context));
    ASSERT_TRUE(detail::allocate_new_table(&context.tables[mdtid_ClassLayout]));
    ASSERT_FALSE(context.tables[mdtid_TypeRef].is_initialized);
    ASSERT_TRUE(ShiftReferences(mdtid_TypeDef, 1, 1));
    EXPECT_FALSE(context.tables[mdtid_TypeRef].is_initialized);
    EXPECT_EQ(0u, context.tables[mdtid_ClassLayout].row_count);
    uint32_t flags;
    char const* name;
    ASSERT_TRUE(row.get_column_value(TypeDef_Flags, &flags));
    ASSERT_TRUE(row.get_column_value(TypeDef_TypeName, &name));
    EXPECT_EQ(0x1234u, flags);
    EXPECT_STREQ("Preserved", name);
}

TEST_F(CoreAlgorithms, ShiftedReferenceWidthsUseTheUpdatedTablesRowCount)
{
    auto source = AddRow<mdtid_TypeRef>(base.get());
    ASSERT_TRUE(source);
    ASSERT_TRUE(source.value().set_column_value(TypeRef_TypeName, "Preserved"));
    ASSERT_TRUE(source.value().set_column_value(TypeRef_TypeNamespace, "Preserved.Namespace"));
    for (uint32_t index = 1; index < 0x4000; ++index)
        ASSERT_TRUE(AddRow<mdtid_ModuleRef>(base.get()));
    ASSERT_TRUE(source.value().set_column_value(TypeRef_ResolutionScope, CreateTokenType(mdtid_ModuleRef) | 0x3fff));
    auto table = detail::CursorTable(source);
    auto column = static_cast<uint8_t>(TypeRef_ResolutionScope.column_index);
    ASSERT_TRUE(table->column_details[column] & mdtc_b2);
    ASSERT_TRUE(AddRow<mdtid_ModuleRef>(base.get()));
    EXPECT_TRUE(table->column_details[column] & mdtc_b4);
    mdToken token;
    char const* name;
    char const* name_space;
    ASSERT_TRUE(source.value().get_column_value(TypeRef_ResolutionScope, token));
    ASSERT_TRUE(source.value().get_column_value(TypeRef_TypeName, &name));
    ASSERT_TRUE(source.value().get_column_value(TypeRef_TypeNamespace, &name_space));
    EXPECT_EQ(CreateTokenType(mdtid_ModuleRef) | 0x3fff, token);
    EXPECT_STREQ("Preserved", name);
    EXPECT_STREQ("Preserved.Namespace", name_space);
    ASSERT_TRUE(source.value().set_column_value(TypeRef_ResolutionScope, CreateTokenType(mdtid_ModuleRef) | 0x4000));
    ASSERT_TRUE(ShiftReferences(mdtid_ModuleRef, 0x4000, -1));
    EXPECT_TRUE(table->column_details[column] & mdtc_b2);
    ASSERT_TRUE(source.value().get_column_value(TypeRef_ResolutionScope, token));
    ASSERT_TRUE(source.value().get_column_value(TypeRef_TypeName, &name));
    ASSERT_TRUE(source.value().get_column_value(TypeRef_TypeNamespace, &name_space));
    EXPECT_EQ(CreateTokenType(mdtid_ModuleRef) | 0x3fff, token);
    EXPECT_STREQ("Preserved", name);
    EXPECT_STREQ("Preserved.Namespace", name_space);
}

TEST_F(CoreAlgorithms, ShiftedReferenceReadFailuresPropagateToInsertion)
{
    auto& context = detail::context(base.get());
    auto table = &context.tables[mdtid_TypeDef];
    auto column = static_cast<uint8_t>(TypeDef_Extends.column_index);
    auto data = detail::get_writable_table_data(table, true);
    ASSERT_NE(nullptr, data);
    auto offset = (static_cast<uint32_t>(table->column_details[column]) & mdtc_comask) >> 8;
    data[offset] = 3; // TypeDefOrRef tag 3 is reserved.
    EXPECT_FALSE(ShiftReferences(mdtid_TypeRef, 1, 1));
    EXPECT_FALSE(added_row<mdtid_TypeRef>::append(base.get()));
    EXPECT_EQ(0u, context.tables[mdtid_TypeRef].row_count);
    EXPECT_FALSE(context.tables[mdtid_TypeRef].is_adding_new_row);
}

TEST_F(CoreAlgorithms, InsertingIntoAResizedTablePreservesItsLastRowAndClearsTheNewRow)
{
    auto first = AddRow<mdtid_TypeRef>(base.get());
    ASSERT_TRUE(first);
    ASSERT_TRUE(first.value().set_column_value(TypeRef_TypeName, "Preserved"));
    ASSERT_TRUE(first.value().set_column_value(TypeRef_TypeNamespace, "Preserved.Namespace"));
    auto table = detail::CursorTable(first);
    constexpr uint32_t rows = 0x3fff;
    size_t capacity = size_t{rows + 1} * table->row_size_bytes;
    auto data = static_cast<uint8_t*>(alloc_mdmem(table->cxt, capacity));
    ASSERT_NE(nullptr, data);
    std::array<mdtcol_t, 3> columns{};
    std::copy_n(table->column_details, columns.size(), columns.begin());
    for (uint32_t index = 0; index < rows; ++index)
        std::memcpy(data + size_t{index} * table->row_size_bytes, table->data.ptr, table->row_size_bytes);
    table->row_count = rows;
    detail::replace_table_data(table, data, capacity, table->row_size_bytes, columns.data());
    auto last = raw_cursor<mdtid_TypeRef>::at(base.get(), rows).value();
    ASSERT_TRUE(last.set_column_value(TypeRef_ResolutionScope, CreateTokenType(mdtid_TypeRef) | rows));
    auto added = added_row<mdtid_TypeRef>::insert(first.value(), false);
    ASSERT_TRUE(added);
    added.commit();
    EXPECT_EQ(rows + 1, table->row_count);
    EXPECT_TRUE(table->column_details[static_cast<uint8_t>(TypeRef_ResolutionScope.column_index)] & mdtc_b4);

    auto bytes = Save(base.get());
    mdhandle_t raw;
    ASSERT_TRUE(md_create_handle(bytes.data(), bytes.size(), &raw));
    handle_ptr loaded{raw};
    for (auto handle : {base.get(), loaded.get()})
    {
        auto inserted = raw_cursor<mdtid_TypeRef>::at(handle, 1).value();
        mdToken token;
        char const* name;
        ASSERT_TRUE(inserted.get_column_value(TypeRef_ResolutionScope, token));
        EXPECT_EQ(CreateTokenType(mdtid_Module), token);
        ASSERT_TRUE(inserted.get_column_value(TypeRef_TypeName, &name));
        EXPECT_STREQ("", name);
        ASSERT_TRUE(inserted.get_column_value(TypeRef_TypeNamespace, &name));
        EXPECT_STREQ("", name);
        auto tail = raw_cursor<mdtid_TypeRef>::at(handle, rows + 1).value();
        ASSERT_TRUE(tail.get_column_value(TypeRef_ResolutionScope, token));
        EXPECT_EQ(CreateTokenType(mdtid_TypeRef) | (rows + 1), token);
        ASSERT_TRUE(tail.get_column_value(TypeRef_TypeName, &name));
        EXPECT_STREQ("Preserved", name);
        ASSERT_TRUE(tail.get_column_value(TypeRef_TypeNamespace, &name));
        EXPECT_STREQ("Preserved.Namespace", name);
    }
}

TEST_F(CoreAlgorithms, GuidHeapWidthsUseEntryCountsAndPreserveExistingIndices)
{
    auto module = md_create_cursor<mdtid_Module>(base.get()).value();
    mdguid_t expected{0x12345678, 0x1234, 0x5678, {0, 1, 2, 3, 4, 5, 6, 7}};
    ASSERT_TRUE(module.set_column_value(Module_Mvid, expected));
    auto& context = detail::context(base.get());
    auto& source = detail::context(delta.get());
    size_t size = size_t{0x10000} * sizeof(mdguid_t);
    auto data = static_cast<uint8_t*>(alloc_mdmem(&source, size));
    ASSERT_NE(nullptr, data);
    std::memset(data, 0, size);
    std::memcpy(data, &expected, sizeof(expected));
    source.guid_heap = {data, size_t{0xffff} * sizeof(mdguid_t)};
    ASSERT_TRUE(append_heap(&context, &source, mdtc_hguid));
    EXPECT_EQ(size_t{0xffff} * sizeof(mdguid_t), context.guid_heap.size);
    EXPECT_FALSE(context.context_flags & mdc_large_guid_heap);
    auto table = detail::CursorTable(module);
    auto column = static_cast<uint8_t>(Module_Mvid.column_index);
    EXPECT_TRUE(table->column_details[column] & mdtc_b2);
    source.guid_heap.size = size;
    ASSERT_TRUE(append_heap(&context, &source, mdtc_hguid));
    EXPECT_TRUE(context.context_flags & mdc_large_guid_heap);
    EXPECT_TRUE(table->column_details[column] & mdtc_b4);
    EXPECT_EQ(1u, detail::row_storage_access::read(row_reader{module}, Module_Mvid));
    mdguid_t actual;
    ASSERT_TRUE(module.get_column_value(Module_Mvid, &actual));
    EXPECT_EQ(0, std::memcmp(&expected, &actual, sizeof(expected)));
}

#ifdef DNMD_PORTABLE_PDB
TEST_F(CoreAlgorithms, ShiftedReferencesReacquireRowsAfterPdbSchemaGrowth)
{
    auto pdb = CreatePdb();
    ASSERT_NE(nullptr, pdb.get());
    for (uint32_t index : {1u, 2u, 3u})
    {
        auto added = added_row<mdtid_LocalScope>::append(pdb.get());
        ASSERT_TRUE(added);
        ASSERT_TRUE(added.value().set_column_value(LocalScope_Method, CreateTokenType(mdtid_MethodDef) | index));
        ASSERT_TRUE(added.value().set_column_value(LocalScope_StartOffset, index * 10));
    }
    auto table = &detail::context(pdb.get()).tables[mdtid_LocalScope];
    auto column = static_cast<uint8_t>(LocalScope_Method.column_index);
    ASSERT_TRUE(table->column_details[column] & mdtc_b2);
    ASSERT_TRUE(update_table_references_for_shifted_rows(&detail::context(pdb.get()), mdtid_MethodDef, 1, 0xffff));
    EXPECT_TRUE(table->column_details[column] & mdtc_b4);
    for (uint32_t index : {1u, 2u, 3u})
    {
        auto row = raw_cursor<mdtid_LocalScope>::at(pdb.get(), index);
        mdToken token;
        uint32_t offset;
        ASSERT_TRUE(row.value().get_column_value(LocalScope_Method, token));
        ASSERT_TRUE(row.value().get_column_value(LocalScope_StartOffset, &offset));
        EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | (index + 0xffff), token);
        EXPECT_EQ(index * 10, offset);
    }
}

TEST_F(CoreAlgorithms, ExternalRowGrowthScansOnlyPdbTables)
{
    auto pdb = CreatePdb();
    ASSERT_NE(nullptr, pdb.get());
    auto member = AddRow<mdtid_MemberRef>(pdb.get());
    ASSERT_TRUE(member);
    auto debug = AddRow<mdtid_CustomDebugInformation>(pdb.get());
    ASSERT_TRUE(debug);
    ASSERT_TRUE(debug.value().set_column_value(CustomDebugInformation_Parent, CreateTokenType(mdtid_MethodDef) | 0x10000));
    EXPECT_TRUE(detail::CursorTable(member)->column_details[
        static_cast<uint8_t>(MemberRef_Class.column_index)] & mdtc_b2);
    EXPECT_TRUE(detail::CursorTable(debug)->column_details[
        static_cast<uint8_t>(CustomDebugInformation_Parent.column_index)] & mdtc_b4);
}

TEST_F(CoreAlgorithms, CodedWidthsRetainOtherExternalTargets)
{
    auto pdb = CreatePdb();
    ASSERT_NE(nullptr, pdb.get());
    auto first = AddRow<mdtid_CustomDebugInformation>(pdb.get());
    ASSERT_TRUE(first);
    mdToken method = CreateTokenType(mdtid_MethodDef) | 0x10000;
    mdToken field = CreateTokenType(mdtid_Field) | 3;
    ASSERT_TRUE(first.value().set_column_value(CustomDebugInformation_Parent, method));
    auto second = AddRow<mdtid_CustomDebugInformation>(pdb.get());
    ASSERT_TRUE(second);
    ASSERT_TRUE(second.value().set_column_value(CustomDebugInformation_Parent, field));
    EXPECT_TRUE(detail::CursorTable(first)->column_details[
        static_cast<uint8_t>(CustomDebugInformation_Parent.column_index)] & mdtc_b4);
    auto position = md_create_cursor<mdtid_CustomDebugInformation>(pdb.get());
    for (auto expected : {method, field})
    {
        ASSERT_TRUE(position);
        mdToken actual;
        ASSERT_TRUE(position.value().get_column_value(CustomDebugInformation_Parent, actual));
        EXPECT_EQ(expected, actual);
        ASSERT_TRUE(position.move(1));
    }
    EXPECT_EQ(0x10000u << 5, detail::row_storage_access::read(row_reader{first.value()}, CustomDebugInformation_Parent));
    EXPECT_EQ((3u << 5) | 1u, detail::row_storage_access::read(row_reader{second.value()}, CustomDebugInformation_Parent));
}

TEST_F(CoreAlgorithms, HeapGrowthUpdatesBothTableSequences)
{
    auto pdb = CreatePdb();
    ASSERT_NE(nullptr, pdb.get());
    auto system = AddRow<mdtid_ModuleRef>(pdb.get());
    auto local = AddRow<mdtid_LocalVariable>(pdb.get());
    ASSERT_TRUE(system);
    ASSERT_TRUE(local);
    ASSERT_TRUE(system.value().set_column_value(ModuleRef_Name, "System"));
    ASSERT_TRUE(local.value().set_column_value(LocalVariable_Name, "Local"));
    ASSERT_TRUE(local.value().set_column_value(LocalVariable_Index, 0x1234));
    std::string large(70000, 'x');
    ASSERT_NE(0u, detail::add_to_string_heap(&detail::context(pdb.get()), large.c_str()));
    char const* name;
    uint32_t index;
    ASSERT_TRUE(system.value().get_column_value(ModuleRef_Name, &name));
    EXPECT_STREQ("System", name);
    ASSERT_TRUE(local.value().get_column_value(LocalVariable_Name, &name));
    EXPECT_STREQ("Local", name);
    ASSERT_TRUE(local.value().get_column_value(LocalVariable_Index, &index));
    EXPECT_EQ(0x1234u, index);
    EXPECT_TRUE(detail::CursorTable(system)->column_details[static_cast<uint8_t>(ModuleRef_Name.column_index)] & mdtc_b4);
    EXPECT_TRUE(detail::CursorTable(local)->column_details[static_cast<uint8_t>(LocalVariable_Name.column_index)] & mdtc_b4);
}

TEST_F(CoreAlgorithms, HeapGrowthBeforeTableCreationSetsTheHeapWidthFlag)
{
    auto pdb = CreatePdb();
    ASSERT_NE(nullptr, pdb.get());
    std::string large(70000, 'x');
    auto& context = detail::context(pdb.get());
    ASSERT_NE(0u, detail::add_to_string_heap(&context, large.c_str()));
    EXPECT_TRUE(context.context_flags & mdc_large_string_heap);
    auto row = AddRow<mdtid_LocalVariable>(pdb.get());
    ASSERT_TRUE(row);
    ASSERT_TRUE(row.value().set_column_value(LocalVariable_Name, "AfterGrowth"));
    char const* name;
    ASSERT_TRUE(row.value().get_column_value(LocalVariable_Name, &name));
    EXPECT_STREQ("AfterGrowth", name);
    auto table = detail::CursorTable(row);
    auto pointer = table->data.ptr;
    auto details = table->column_details[static_cast<uint8_t>(LocalVariable_Name.column_index)];
    auto memory = context.mem;
    EXPECT_FALSE(update_heap_column_sizes(&context, mdtc_hstring, 1));
    EXPECT_EQ(pointer, table->data.ptr);
    EXPECT_EQ(details, table->column_details[static_cast<uint8_t>(LocalVariable_Name.column_index)]);
    EXPECT_EQ(memory, context.mem);
    ASSERT_TRUE(row.value().get_column_value(LocalVariable_Name, &name));
    EXPECT_STREQ("AfterGrowth", name);
}
#endif

TEST_F(CoreAlgorithms, ValidatesCompleteCompositeSortKeysAndStorage)
{
    auto first = AddRow<mdtid_GenericParam>(base.get());
    auto second = AddRow<mdtid_GenericParam>(base.get());
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(first.value().set_column_value(GenericParam_Owner, CreateTokenType(mdtid_TypeDef) | 1));
    ASSERT_TRUE(second.value().set_column_value(GenericParam_Owner, CreateTokenType(mdtid_TypeDef) | 1));
    ASSERT_TRUE(first.value().set_column_value(GenericParam_Number, 1));
    ASSERT_TRUE(second.value().set_column_value(GenericParam_Number, 2));
    auto table = detail::CursorTable(first);
    table->is_sorted = true;
    EXPECT_TRUE(md_validate(base.get()));
    ASSERT_TRUE(second.value().set_column_value(GenericParam_Number, 0));
    table->is_sorted = true;
    EXPECT_FALSE(md_validate(base.get()));
    ASSERT_TRUE(second.value().set_column_value(GenericParam_Number, 2));
    table->is_sorted = true;
    auto size = table->data.size;
    table->data.size = 1;
    EXPECT_FALSE(md_validate(base.get()));
    table->data.size = size;
    EXPECT_TRUE(md_validate(base.get()));
}

TEST_F(CoreAlgorithms, RejectsPointerTablesInCompressedHeap)
{
    auto method = AddMethod(base.get());
    ASSERT_TRUE(method);
    ASSERT_TRUE(AddParameter(method, 0));
    auto& context = detail::context(base.get());
    ASSERT_TRUE((detail::create_and_fill_indirect_table<mdtid_Param, mdtid_ParamPtr>(&context)));
    auto flags = context.context_flags;
    context.context_flags = static_cast<mdcxt_flag_t>(flags & ~mdc_uncompressed_table_heap);
    EXPECT_FALSE(md_validate(base.get()));
    context.context_flags = flags;
    EXPECT_TRUE(md_validate(base.get()));
}

TEST_F(CoreAlgorithms, DumpsInvalidReferencesWithRawDiagnosticsAndRejectsTruncatedRows)
{
    auto row = AddRow<mdtid_MemberRef>(base.get());
    ASSERT_TRUE(row);
    auto table = detail::CursorTable(row);
    auto data = detail::get_writable_table_data(table, true);
    ASSERT_NE(nullptr, data);
    data[0] = 7;
    testing::internal::CaptureStdout();
    auto success = md_dump_tables(base.get(), mdtid_MemberRef);
    auto output = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(success);
    EXPECT_NE(std::string::npos, output.find("Invalid (0) [0x7]|"));
    auto size = table->data.size;
    table->data.size = 1;
    testing::internal::CaptureStdout();
    success = md_dump_tables(base.get(), mdtid_MemberRef);
    output = testing::internal::GetCapturedStdout();
    table->data.size = size;
    EXPECT_FALSE(success);
    EXPECT_NE(std::string::npos, output.find("Failure to retrieve raw column values"));
}

TEST_F(CoreAlgorithms, AppliesMappedRowsAndWidensHeapColumns)
{
    auto old = AddRow<mdtid_ModuleRef>(base.get());
    ASSERT_TRUE(old);
    ASSERT_TRUE(old.value().set_column_value(ModuleRef_Name, "Before"));
    auto original_size = detail::context(base.get()).strings_heap.size;
    std::string padding(65536, 'x');
    ASSERT_TRUE(md_create_cursor<mdtid_Module>(delta.get()).value().set_column_value(Module_Name, padding.c_str()));
    auto source = AddRow<mdtid_ModuleRef>(delta.get());
    ASSERT_TRUE(source);
    ASSERT_TRUE(source.value().set_column_value(ModuleRef_Name, "Added"));
    ASSERT_NO_FATAL_FAILURE(Rebase(source.value(), ModuleRef_Name, original_size));
    ASSERT_NO_FATAL_FAILURE(Map(CreateTokenType(mdtid_ModuleRef) | 2));
    ASSERT_NO_FATAL_FAILURE(Log(CreateTokenType(mdtid_ModuleRef) | 2));
    Minimal();
    ASSERT_TRUE(md_apply_delta(base.get(), delta.get()));
    auto result = raw_cursor<mdtid_ModuleRef>{base.get(), CreateTokenType(mdtid_ModuleRef) | 2};
    ASSERT_TRUE(result);
    char const* name;
    ASSERT_TRUE(result.value().get_column_value(ModuleRef_Name, &name));
    EXPECT_STREQ("Added", name);
    ASSERT_TRUE(old.value().get_column_value(ModuleRef_Name, &name));
    EXPECT_STREQ("Before", name);
    EXPECT_TRUE(detail::CursorTable(result)->column_details[0] & mdtc_b4);
    EXPECT_EQ(original_size + detail::context(delta.get()).strings_heap.size,
        detail::context(base.get()).strings_heap.size);
}

TEST_F(CoreAlgorithms, CopiesReferencesAndMergedBlobAndUserStringIndices)
{
    uint8_t original[] = {1, 2, 3};
    uint8_t bytes[] = {4, 5, 6, 7};
    auto old = AddRow<mdtid_TypeSpec>(base.get());
    ASSERT_TRUE(old);
    ASSERT_TRUE(old.value().set_column_value(TypeSpec_Signature, original, sizeof(original)));
    auto blob_size = detail::context(base.get()).blob_heap.size;
    auto string_size = detail::context(base.get()).strings_heap.size;
    ASSERT_NE(0, md_add_userstring_to_heap(base.get(), u"Old"));
    ASSERT_NE(0, md_add_userstring_to_heap(delta.get(), u"New"));
    auto user_size = detail::context(base.get()).user_string_heap.size;
    auto source = AddRow<mdtid_MemberRef>(delta.get());
    ASSERT_TRUE(source);
    row_writer writer{source.value()};
    ASSERT_TRUE(writer.write(MemberRef_Class, CreateTokenType(mdtid_TypeDef) | 1));
    ASSERT_TRUE(writer.write(MemberRef_Name, "Imported"));
    ASSERT_TRUE(writer.write(MemberRef_Signature, bytes, sizeof(bytes)));
    ASSERT_NO_FATAL_FAILURE(Rebase(source.value(), MemberRef_Name, string_size));
    ASSERT_NO_FATAL_FAILURE(Rebase(source.value(), MemberRef_Signature, blob_size));
    ASSERT_NO_FATAL_FAILURE(Log(CreateTokenType(mdtid_MemberRef) | 1));
    Minimal();
    ASSERT_TRUE(md_apply_delta(base.get(), delta.get()));
    auto result = raw_cursor<mdtid_MemberRef>{base.get(), CreateTokenType(mdtid_MemberRef) | 1};
    ASSERT_TRUE(result);
    mdToken parent;
    uint8_t const* data;
    uint32_t size;
    ASSERT_TRUE(result.value().get_column_value(MemberRef_Class, parent));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 1, parent);
    ASSERT_TRUE(result.value().get_column_value(MemberRef_Signature, &data, &size));
    ASSERT_EQ(sizeof(bytes), size);
    EXPECT_TRUE(std::equal(data, data + size, bytes));
    auto const& appended = detail::context(delta.get()).user_string_heap;
    auto const& combined = detail::context(base.get()).user_string_heap;
    ASSERT_EQ(user_size + appended.size, combined.size);
    EXPECT_EQ(0, std::memcmp(combined.ptr + user_size, appended.ptr, appended.size));
}

TEST_F(CoreAlgorithms, RejectsInvalidMapsAndNonSequentialAppends)
{
    ASSERT_NO_FATAL_FAILURE(Map(CreateTokenType(mdtid_TypeDef) | 1));
    ASSERT_NO_FATAL_FAILURE(Map(CreateTokenType(mdtid_ModuleRef) | 1));
    ASSERT_NO_FATAL_FAILURE(Map(CreateTokenType(mdtid_TypeDef) | 2));
    Minimal();
    EXPECT_FALSE(md_apply_delta(base.get(), delta.get()));
}

TEST_F(CoreAlgorithms, RejectsMissingTargetRowsAndInvalidOperationParents)
{
    auto source = AddRow<mdtid_ModuleRef>(delta.get());
    ASSERT_TRUE(source);
    ASSERT_NO_FATAL_FAILURE(Map(CreateTokenType(mdtid_ModuleRef) | 5));
    ASSERT_NO_FATAL_FAILURE(Log(CreateTokenType(mdtid_ModuleRef) | 5));
    Minimal();
    EXPECT_FALSE(md_apply_delta(base.get(), delta.get()));
    EXPECT_EQ(0u, detail::context(base.get()).tables[mdtid_ModuleRef].row_count);

    delta.reset(md_create_new_handle());
    ASSERT_NE(nullptr, delta.get());
    ASSERT_NO_FATAL_FAILURE(Log(CreateTokenType(mdtid_MethodDef) | 1, 1));
    Minimal();
    EXPECT_FALSE(md_apply_delta(base.get(), delta.get()));
}

TEST_F(CoreAlgorithms, FinishesOwnedRowsAfterCopyFailure)
{
    auto source = AddRow<mdtid_MemberRef>(delta.get());
    ASSERT_TRUE(source);
    auto data = detail::get_writable_table_data(detail::CursorTable(source), true);
    ASSERT_NE(nullptr, data);
    data[0] = 7;
    ASSERT_NO_FATAL_FAILURE(Log(CreateTokenType(mdtid_MemberRef) | 1));
    Minimal();
    EXPECT_FALSE(md_apply_delta(base.get(), delta.get()));
    auto const& table = detail::context(base.get()).tables[mdtid_MemberRef];
    EXPECT_EQ(1u, table.row_count);
    EXPECT_FALSE(table.is_adding_new_row);
}

TEST_F(CoreAlgorithms, EnforcesVersionIdentityAndDeltaChaining)
{
    EXPECT_FALSE(md_apply_delta(base.get(), delta.get()));
    Minimal();
    ++detail::context(delta.get()).major_ver;
    EXPECT_FALSE(md_apply_delta(base.get(), delta.get()));
    --detail::context(delta.get()).major_ver;
    auto module = md_create_cursor<mdtid_Module>(delta.get()).value();
    mdguid_t a{1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};
    mdguid_t b{2, 3, 4, {5, 6, 7, 8, 9, 10, 11, 12}};
    ASSERT_TRUE(module.set_column_value(Module_Mvid, a));
    EXPECT_FALSE(md_apply_delta(base.get(), delta.get()));
    delta.reset(md_create_new_handle());
    ASSERT_NE(nullptr, delta.get());
    module = md_create_cursor<mdtid_Module>(delta.get()).value();
    Minimal();
    ASSERT_TRUE(module.set_column_value(Module_EncId, a));
    ASSERT_TRUE(md_apply_delta(base.get(), delta.get()));
    mdguid_t actual;
    ASSERT_TRUE(md_create_cursor<mdtid_Module>(base.get()).value().get_column_value(Module_EncId, &actual));
    EXPECT_EQ(0, std::memcmp(&a, &actual, sizeof(a)));
    delta.reset(md_create_new_handle());
    ASSERT_NE(nullptr, delta.get());
    module = md_create_cursor<mdtid_Module>(delta.get()).value();
    ASSERT_TRUE(module.set_column_value(Module_EncBaseId, a));
    ASSERT_TRUE(module.set_column_value(Module_EncId, b));
    Minimal();
    ASSERT_TRUE(md_apply_delta(base.get(), delta.get()));
    ASSERT_TRUE(md_create_cursor<mdtid_Module>(base.get()).value().get_column_value(Module_EncId, &actual));
    EXPECT_EQ(0, std::memcmp(&b, &actual, sizeof(b)));
    EXPECT_FALSE(md_apply_delta(base.get(), delta.get()));
}

TEST_F(CoreAlgorithms, CreatesMethods)
{
    CheckCreation(md_create_cursor<mdtid_TypeDef>(base.get()),
        md_create_cursor<mdtid_TypeDef>(delta.get()), TypeDef_MethodList, 1);
}

TEST_F(CoreAlgorithms, CreatesFields)
{
    CheckCreation(md_create_cursor<mdtid_TypeDef>(base.get()),
        md_create_cursor<mdtid_TypeDef>(delta.get()), TypeDef_FieldList, 2);
}

TEST_F(CoreAlgorithms, CreatesParameters)
{
    CheckCreation(AddMethod(base.get()), AddMethod(delta.get()), MethodDef_ParamList, 3);
}

TEST_F(CoreAlgorithms, CreatesProperties)
{
    CheckCreation(AddRow<mdtid_PropertyMap>(base.get()),
        AddRow<mdtid_PropertyMap>(delta.get()), PropertyMap_PropertyList, 4);
}

TEST_F(CoreAlgorithms, CreatesEvents)
{
    CheckCreation(AddRow<mdtid_EventMap>(base.get()),
        AddRow<mdtid_EventMap>(delta.get()), EventMap_EventList, 5);
}

TEST_F(CoreAlgorithms, SortsNewPointerLists)
{
    CheckParameterSort(false, false);
}

TEST_F(CoreAlgorithms, SortsExistingPointerListsStably)
{
    CheckParameterSort(true, true);
}

#ifdef DNMD_PORTABLE_PDB
TEST_F(CoreAlgorithms, TypedListAddSupportsDirectPortablePdbLists)
{
    handle_ptr pdb{md_create_new_pdb_handle()};
    ASSERT_NE(nullptr, pdb.get());
    auto scope = AddRow<mdtid_LocalScope>(pdb.get());
    ASSERT_TRUE(scope);
    for (uint32_t index : {1u, 2u})
    {
        auto variable = added_row<mdtid_LocalVariable>::in_list(scope.value(), LocalScope_VariableList);
        ASSERT_TRUE(variable);
        ASSERT_TRUE(variable.value().set_column_value(LocalVariable_Index, index));
    }
    {
        auto variable = added_row<mdtid_LocalVariable>::in_list(
            scope.value(), LocalScope_VariableList, LocalVariable_Index, 3);
        ASSERT_TRUE(variable);
        uint32_t index = 0;
        ASSERT_TRUE(variable.value().get_column_value(LocalVariable_Index, &index));
        EXPECT_EQ(3u, index);
    }
    auto constant = added_row<mdtid_LocalConstant>::in_list(scope.value(), LocalScope_ConstantList);
    ASSERT_TRUE(constant);
    constant.commit();
    auto variables = raw_cursor<mdtid_LocalVariable>::nil(pdb.get());
    auto constants = raw_cursor<mdtid_LocalConstant>::nil(pdb.get());
    uint32_t count = 0;
    ASSERT_TRUE(row_reader{scope.value()}.read(LocalScope_VariableList, variables, &count));
    EXPECT_EQ(3u, count);
    ASSERT_TRUE(row_reader{scope.value()}.read(LocalScope_ConstantList, constants, &count));
    EXPECT_EQ(1u, count);
}

TEST_F(CoreAlgorithms, DumpsEmptyMalformedPdbBlobsWithoutReadingPastTheirBounds)
{
    handle_ptr pdb{md_create_new_pdb_handle()};
    ASSERT_NE(nullptr, pdb.get());
    auto document = AddRow<mdtid_Document>(pdb.get());
    auto local = AddRow<mdtid_LocalConstant>(pdb.get());
    ASSERT_TRUE(document);
    ASSERT_TRUE(local);
    uint8_t payload[] = {0};
    ASSERT_TRUE(document.value().set_column_value(Document_Hash, payload, sizeof(payload)));
    testing::internal::CaptureStdout();
    auto document_success = md_dump_tables(pdb.get(), mdtid_Document);
    auto local_success = md_dump_tables(pdb.get(), mdtid_LocalConstant);
    auto output = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(document_success);
    EXPECT_TRUE(local_success);
    EXPECT_NE(std::string::npos, output.find("Invalid PDB Blob (DocumentName)"));
    EXPECT_NE(std::string::npos, output.find("Invalid PDB Blob (LocalConstantSig)"));
}

TEST_F(CoreAlgorithms, ValidatesDescendingPdbSortKeys)
{
    handle_ptr pdb{md_create_new_pdb_handle()};
    ASSERT_NE(nullptr, pdb.get());
    auto first = AddRow<mdtid_LocalScope>(pdb.get());
    auto second = AddRow<mdtid_LocalScope>(pdb.get());
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    for (auto position : {first, second})
    {
        ASSERT_TRUE(position.value().set_column_value(LocalScope_Method, CreateTokenType(mdtid_MethodDef) | 1));
        ASSERT_TRUE(position.value().set_column_value(LocalScope_StartOffset, 5));
    }
    ASSERT_TRUE(first.value().set_column_value(LocalScope_Length, 10));
    ASSERT_TRUE(second.value().set_column_value(LocalScope_Length, 4));
    auto table = detail::CursorTable(first);
    table->is_sorted = true;
    EXPECT_TRUE(md_validate(pdb.get()));
    ASSERT_TRUE(second.value().set_column_value(LocalScope_Length, 12));
    table->is_sorted = true;
    EXPECT_FALSE(md_validate(pdb.get()));
}
#endif
