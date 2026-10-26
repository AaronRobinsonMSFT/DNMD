#include <dnmd.hpp>
#include "schema.hpp"
#include "c_api_probes.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>

namespace
{
    using namespace dnmd;
    using namespace dnmd::columns;

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
                ASSERT_TRUE(create_and_fill_indirect_table(&context, mdtid_Param, mdtid_ParamPtr));
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
    void CheckSchemas(raw_cursor<Tables...>)
    {
        auto check = [](auto table) {
            constexpr auto id = decltype(table)::value;
            auto columns = detail::table_columns<id>();
            EXPECT_EQ(test_c_column_count(id), std::tuple_size_v<decltype(columns)>);
            uint8_t index = 0;
            auto check_index = [&](auto col) {
                EXPECT_EQ(index++, static_cast<uint8_t>(col.column_index));
            };
            std::apply([&](auto... cols) {
                (check_index(cols), ...);
            }, columns);
        };
        (check(std::integral_constant<mdtable_id_t, Tables>{}), ...);
    }
}

TEST_F(CoreAlgorithms, TypedSchemasMatchCColumnCountsAndOrder)
{
    CheckSchemas(any_cursor::nil(base.get()));
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
    ASSERT_TRUE(create_and_fill_indirect_table(&context, mdtid_Param, mdtid_ParamPtr));
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
