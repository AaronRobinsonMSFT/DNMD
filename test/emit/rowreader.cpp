#include <dnmd.hpp>
#include <gtest/gtest.h>
#include <array>
#include <vector>

namespace
{
    template<typename Reader, typename Column, typename... Outputs>
    auto SupportsRead(int) -> decltype(std::declval<Reader const&>().read(
        std::declval<Column>(), std::declval<Outputs>()...), std::true_type{});

    template<typename Reader, typename Column, typename... Outputs>
    auto SupportsRead(...) -> std::false_type;

    template<typename Reader, typename Column, typename... Outputs>
    inline constexpr bool can_read_v = decltype(SupportsRead<Reader, Column, Outputs...>(0))::value;

    using type_reader = dnmd::row_reader<mdtid_TypeDef>;
    static_assert(sizeof(type_reader) == 2 * sizeof(void*));
    static_assert(can_read_v<type_reader, decltype(dnmd::columns::TypeDef_Flags), uint32_t*>);
    static_assert(can_read_v<type_reader, decltype(dnmd::columns::TypeDef_TypeName), char const**>);
    static_assert(can_read_v<type_reader, decltype(dnmd::columns::TypeDef_Extends), mdToken&>);
    static_assert(can_read_v<type_reader, decltype(dnmd::columns::TypeDef_Extends),
        decltype(dnmd::columns::TypeDef_Extends)::value_type&>);
    static_assert(can_read_v<type_reader, decltype(dnmd::columns::TypeDef_MethodList),
        dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>&>);
    static_assert(can_read_v<type_reader, decltype(dnmd::columns::TypeDef_MethodList),
        dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>&, uint32_t*>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::Module_Name), char const**>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::TypeDef_Flags), char const**>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::TypeDef_TypeName), uint32_t*>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::TypeDef_Extends), mdcursor_t&>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::TypeDef_Extends), dnmd::raw_cursor<mdtid_TypeDef>&>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::TypeDef_MethodList),
        dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr>&>);
    static_assert(!can_read_v<dnmd::row_reader<mdtid_Module>, decltype(dnmd::columns::Module_Mvid), uint32_t*>);
    static_assert(can_read_v<dnmd::row_reader<mdtid_Module>, decltype(dnmd::columns::Module_Mvid), mdguid_t*>);
    static_assert(can_read_v<dnmd::row_reader<mdtid_Module>,
        dnmd::column<mdtid_Module, dnmd::column_type_userstring>, mduserstring_t*>);
    static_assert(!can_read_v<dnmd::row_reader<mdtid_TypeSpec>, decltype(dnmd::columns::TypeSpec_Signature),
        char const**>);
    static_assert(can_read_v<dnmd::row_reader<mdtid_TypeSpec>, decltype(dnmd::columns::TypeSpec_Signature),
        uint8_t const**, uint32_t*>);

    class CppRowReader : public testing::Test
    {
    protected:
        dnmd::handle_ptr handle;

        void SetUp() override
        {
#ifdef DNMD_PORTABLE_PDB
            handle.reset(md_create_new_pdb_handle());
#else
            handle.reset(md_create_new_handle());
#endif
            ASSERT_NE(nullptr, handle.get());
        }

        template<mdtable_id_t TableId>
        dnmd::cursor<TableId> AppendRow()
        {
            mdcursor_t raw{};
            if constexpr (TableId == mdtid_MethodDef)
            {
                auto type = GetRow<mdtid_TypeDef>();
                EXPECT_TRUE(md_add_new_row_to_list(type, dnmd::columns::TypeDef_MethodList.column_index, &raw));
            }
            else
            {
                EXPECT_TRUE(md_append_row(handle.get(), TableId, &raw));
            }
            md_commit_row_add(raw);
            return dnmd::raw_cursor<TableId>{raw}.as();
        }

        template<mdtable_id_t TableId>
        dnmd::cursor<TableId> GetRow()
        {
            auto position = dnmd::md_create_cursor<TableId>(handle.get());
            return position ? position.as() : AppendRow<TableId>();
        }

        template<mdtable_id_t TableId, typename ColumnType>
        void SetRaw(dnmd::cursor<TableId> row, dnmd::column<TableId, ColumnType> column, uint32_t value)
        {
            mdcursor_t raw = row;
            auto table = dnmd::detail::CursorTable(raw);
            auto details = table->column_details[static_cast<uint8_t>(column.column_index)];
            auto data = const_cast<uint8_t*>(table->data.ptr)
                + size_t{dnmd::detail::CursorRow(raw) - 1} * table->row_size_bytes
                + ((static_cast<uint32_t>(details) & mdtc_comask) >> 8);
            for (uint32_t i = 0; i < ((details & mdtc_b4) ? 4u : 2u); ++i)
                data[i] = static_cast<uint8_t>(value >> (8 * i));
        }

        template<mdtable_id_t TableId, mdtable_id_t... Targets, size_t Size>
        void CheckFamily(dnmd::column<TableId, dnmd::raw_cursor<Targets...>> column,
            std::array<mdtable_id_t, Size> const& expected, uint32_t tag_bits)
        {
            auto row = GetRow<TableId>();
            dnmd::row_reader<TableId> reader{row};
            for (uint32_t index : {0u, 1u, 3u})
            {
                for (uint32_t tag = 0; tag < (uint32_t{1} << tag_bits); ++tag)
                {
                    SCOPED_TRACE(testing::Message() << "table=" << TableId << " row=" << index << " tag=" << tag);
                    SetRaw(row, column, (index << tag_bits) | tag);
                    mdToken value = 0x12345678;
                    mdToken c_value = 0x12345678;
                    bool valid = tag < Size && expected[tag] != mdtid_Unused;
                    EXPECT_EQ(valid, reader.read(column, value));
                    EXPECT_EQ(valid, md_get_column_value_as_token(row, column.column_index, &c_value));
                    EXPECT_EQ(c_value, value);
                    if (valid)
                        EXPECT_EQ(CreateTokenType(expected[tag]) | index, value);
                    else
                        EXPECT_EQ(0x12345678u, value);

                    dnmd::raw_cursor<Targets...> position;
                    mdcursor_t c_position{};
                    bool c_success = md_get_column_value_as_cursor(row, column.column_index, &c_position);
                    ASSERT_EQ(c_success, reader.read(column, position));
                    if (c_success)
                    {
                        mdcursor_t raw = position;
                        EXPECT_EQ(c_position._reserved1, raw._reserved1);
                        EXPECT_EQ(c_position._reserved2, raw._reserved2);
                        EXPECT_TRUE(position.get_token(value));
                        EXPECT_EQ(c_value, value);
                        bool visited = position.visit([&](auto target) {
                            static_assert(dnmd::cursor_tableid_v<decltype(target)> != mdtid_Unused);
                            EXPECT_EQ(expected[tag], dnmd::cursor_tableid_v<decltype(target)>);
                        });
                        EXPECT_EQ(static_cast<bool>(position), visited);
                    }
                    else
                    {
                        EXPECT_FALSE(position);
                    }
                }
            }
        }

        template<mdtable_id_t TableId, typename ColumnType>
        void CheckWideIndex(dnmd::column<TableId, ColumnType> column, uint32_t encoded, mdToken expected)
        {
            uint8_t data[] = {static_cast<uint8_t>(encoded), static_cast<uint8_t>(encoded >> 8),
                static_cast<uint8_t>(encoded >> 16), static_cast<uint8_t>(encoded >> 24)};
            std::array<mdtcol_t, 9> columns;
            columns.fill(mdtc_b4);
            mdtable_t table{};
            table.data = {data, sizeof(data)};
            table.row_count = 1;
            table.row_size_bytes = sizeof(data);
            table.column_count = static_cast<uint8_t>(columns.size());
            table.table_id = static_cast<uint8_t>(TableId);
            table.column_details = columns.data();
            auto row = dnmd::raw_cursor<TableId>{mdcursor_t{reinterpret_cast<intptr_t>(&table), 1}}.as();
            dnmd::row_reader<TableId> reader{row};
            mdToken actual = 0;
            ASSERT_TRUE(reader.read(column, actual));
            EXPECT_EQ(expected, actual);
        }

        template<mdtable_id_t OwnerTableId, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        void CheckIndirection(dnmd::column<OwnerTableId, dnmd::indirect_cursor<DirectTableId, IndirectTableId>> list)
        {
            using position_type = dnmd::indirect_cursor<DirectTableId, IndirectTableId>;
            auto owner = GetRow<OwnerTableId>();
            mdcursor_t direct{};
            ASSERT_TRUE(md_add_new_row_to_list(owner, list.column_index, &direct));
            md_commit_row_add(direct);
            EXPECT_FALSE(dnmd::detail::CursorTable(direct)->has_indirect_table);
            position_type initial;
            ASSERT_TRUE(owner.get_column_value(list, initial));
            mdToken initial_token;
            ASSERT_TRUE(initial.get_token(initial_token));
            EXPECT_EQ(CreateTokenType(DirectTableId) | dnmd::detail::CursorRow(direct), initial_token);
            auto other_owner = AppendRow<OwnerTableId>();
            mdcursor_t other{};
            ASSERT_TRUE(md_add_new_row_to_list(other_owner, list.column_index, &other));
            md_commit_row_add(other);
            ASSERT_TRUE(md_add_new_row_to_list(owner, list.column_index, &other));
            md_commit_row_add(other);
            auto pointer = GetRow<IndirectTableId>();
            EXPECT_TRUE(dnmd::detail::CursorTable(direct)->has_indirect_table);
            constexpr auto target_column = dnmd::detail::pointer_column<IndirectTableId>();
            ASSERT_TRUE(pointer.set_column_value(target_column, dnmd::raw_cursor<DirectTableId>{direct}));

            auto check = [&](mdcursor_t source, bool expected_row) {
                position_type position{source};
                EXPECT_EQ(expected_row, static_cast<bool>(position));
                mdToken actual = 0x12345678;
                mdcursor_t expected{};
                bool resolved = md_resolve_indirect_cursor(source, &expected);
                ASSERT_EQ(resolved, position.get_target_token(actual));
                if (resolved)
                {
                    mdToken token;
                    ASSERT_TRUE(md_cursor_to_token(expected, &token));
                    EXPECT_EQ(token, actual);
                    if (position)
                    {
                        mdcursor_t row = position.as();
                        EXPECT_EQ(expected._reserved1, row._reserved1);
                        EXPECT_EQ(expected._reserved2, row._reserved2);
                    }
                }
                else
                {
                    EXPECT_EQ(0x12345678u, actual);
                }
            };
            ASSERT_NO_FATAL_FAILURE(check(direct, true));
            mdcursor_t source = pointer;
            uint32_t target_end = dnmd::detail::CursorTable(direct)->row_count + 1;
            uint32_t source_end = dnmd::detail::CursorTable(source)->row_count + 1;
            ASSERT_NO_FATAL_FAILURE(check(source, true));
            for (uint32_t row : {0u, target_end, target_end + 1})
            {
                ASSERT_TRUE(pointer.set_column_value(target_column, CreateTokenType(DirectTableId) | row));
                ASSERT_NO_FATAL_FAILURE(check(source, false));
            }
            for (bool is_end : {false, true})
            {
                mdcursor_t sentinel = source;
                sentinel._reserved2 = is_end ? source_end : 0;
                ASSERT_NO_FATAL_FAILURE(check(sentinel, false));
                sentinel = direct;
                sentinel._reserved2 = is_end ? target_end : 0;
                ASSERT_NO_FATAL_FAILURE(check(sentinel, false));
            }

            mdToken direct_token;
            ASSERT_TRUE(md_cursor_to_token(direct, &direct_token));
            ASSERT_TRUE(pointer.set_column_value(target_column, direct_token));
            auto table = dnmd::detail::CursorTable(source);
            auto saved = table->column_details[0];
            table->column_details[0] = static_cast<mdtcol_t>(saved & (mdtc_widthmask | mdtc_comask));
            position_type typed{source};
            mdcursor_t c_target{};
            EXPECT_FALSE(md_resolve_indirect_cursor(source, &c_target));
            EXPECT_TRUE(typed);
            mdToken token;
            EXPECT_TRUE(typed.get_target_token(token));
            EXPECT_EQ(direct_token, token);
            table->column_details[0] = saved;

            position_type expected_list;
            uint32_t expected_count;
            ASSERT_TRUE(owner.get_column_value_as_range(list, expected_list, &expected_count));
            mdToken expected_source;
            ASSERT_TRUE(expected_list.get_token(expected_source));
            auto owner_table = dnmd::detail::CursorTable(owner);
            auto& list_details = owner_table->column_details[static_cast<uint8_t>(list.column_index)];
            auto saved_list_details = list_details;
            list_details = static_cast<mdtcol_t>(list_details & (mdtc_widthmask | mdtc_comask));
            dnmd::row_reader reader{owner};
            mdToken actual_source;
            EXPECT_TRUE(reader.read(list, actual_source));
            EXPECT_EQ(expected_source, actual_source);
            position_type actual_list;
            EXPECT_TRUE(reader.read(list, actual_list));
            EXPECT_TRUE(actual_list.get_token(actual_source));
            EXPECT_EQ(expected_source, actual_source);
            uint32_t actual_count;
            ASSERT_TRUE(reader.read(list, actual_list, &actual_count));
            EXPECT_EQ(expected_count, actual_count);
            dnmd::row_writer writer{owner};
            EXPECT_TRUE(writer.write(list, actual_list));
            EXPECT_FALSE(writer.write(list, direct_token));
            dnmd::row_reader after_write{owner};
            EXPECT_TRUE(after_write.read(list, actual_source));
            EXPECT_EQ(expected_source, actual_source);
            list_details = saved_list_details;

            size_t image_size = 0;
            EXPECT_FALSE(md_write_to_buffer(handle.get(), nullptr, &image_size));
            ASSERT_GT(image_size, 0u);
            std::vector<uint8_t> image(image_size);
            ASSERT_TRUE(md_write_to_buffer(handle.get(), image.data(), &image_size));
            mdhandle_t raw;
            ASSERT_TRUE(md_create_handle(image.data(), image.size(), &raw));
            dnmd::handle_ptr loaded{raw};
            auto loaded_direct = dnmd::md_create_cursor<DirectTableId>(loaded.get());
            ASSERT_TRUE(loaded_direct);
            EXPECT_TRUE(dnmd::detail::CursorTable(loaded_direct)->has_indirect_table);
            mdToken owner_token;
            ASSERT_TRUE(owner.get_token(owner_token));
            auto loaded_owner = dnmd::md_token_to_cursor<OwnerTableId>(loaded.get(), owner_token);
            ASSERT_TRUE(loaded_owner);
            dnmd::row_reader loaded_reader{loaded_owner.as()};
            EXPECT_TRUE(loaded_reader.read(list, actual_source));
            EXPECT_EQ(expected_source, actual_source);

            mdcursor_t unchanged = direct;
            EXPECT_FALSE((dnmd::detail::resolve_indirect_cursor<DirectTableId, IndirectTableId>({}, unchanged)));
            EXPECT_EQ(direct._reserved1, unchanged._reserved1);
            EXPECT_EQ(direct._reserved2, unchanged._reserved2);
            source._reserved2 = source_end + 1;
            EXPECT_FALSE((dnmd::detail::resolve_indirect_cursor<DirectTableId, IndirectTableId>(source, unchanged)));
            EXPECT_EQ(direct._reserved1, unchanged._reserved1);
            EXPECT_EQ(direct._reserved2, unchanged._reserved2);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        void CheckEmptyTarget()
        {
            mdtcol_t details = static_cast<mdtcol_t>(mdtc_idx_table | mdtc_b2 | (DirectTableId << 16));
            mdtable_t pointer{};
            pointer.row_count = 1;
            pointer.table_id = static_cast<uint8_t>(IndirectTableId);
            pointer.cxt = dnmd::detail::CursorTable(GetRow<mdtid_Module>())->cxt;
            pointer.column_details = &details;
            for (uint32_t row : {0u, 2u})
            {
                mdcursor_t source{reinterpret_cast<intptr_t>(&pointer), static_cast<intptr_t>(row)};
                dnmd::indirect_cursor<DirectTableId, IndirectTableId> position{source};
                EXPECT_FALSE(position);
                mdToken token;
                ASSERT_TRUE(position.get_token(token));
                EXPECT_EQ(CreateTokenType(IndirectTableId) | row, token);
                ASSERT_TRUE(position.get_target_token(token));
                EXPECT_EQ(CreateTokenType(DirectTableId) | (row == 0 ? 0 : 1), token);
            }
        }
    };
}

using namespace dnmd::columns;

TEST(CursorAccessors, TableIdentityAndRowMask)
{
    mdtable_t table{};
    mdcursor_t const raw{reinterpret_cast<intptr_t>(&table), 0x12345678};
    static_assert(noexcept(dnmd::detail::CursorTable(raw)));
    static_assert(noexcept(dnmd::detail::CursorRow(raw)));
    EXPECT_EQ(&table, dnmd::detail::CursorTable(raw));
    EXPECT_EQ(0x00345678u, dnmd::detail::CursorRow(raw));
    EXPECT_EQ(nullptr, dnmd::detail::CursorTable(mdcursor_t{}));
    EXPECT_EQ(0u, dnmd::detail::CursorRow(mdcursor_t{}));
    EXPECT_EQ(0x00ffffffu, dnmd::detail::CursorRow(mdcursor_t{0, -1}));
    EXPECT_EQ(0x00345678u, (dnmd::detail::RidFromToken)(0x12345678u));
    EXPECT_EQ(0x00345678u, (dnmd::detail::RidFromToken)(uint64_t{0xabcdef0012345678}));
}

TEST_F(CppRowReader, AllCodedIndexTags)
{
    ASSERT_NO_FATAL_FAILURE(CheckFamily(TypeDef_Extends,
        std::array{mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec}, 2));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(Constant_Parent,
        std::array{mdtid_Field, mdtid_Param, mdtid_Property}, 2));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(CustomAttribute_Parent,
        std::array{mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param,
            mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property,
            mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec, mdtid_Assembly,
            mdtid_AssemblyRef, mdtid_File, mdtid_ExportedType, mdtid_ManifestResource, mdtid_GenericParam,
            mdtid_GenericParamConstraint, mdtid_MethodSpec}, 5));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(FieldMarshal_Parent, std::array{mdtid_Field, mdtid_Param}, 1));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(DeclSecurity_Parent,
        std::array{mdtid_TypeDef, mdtid_MethodDef, mdtid_Assembly}, 2));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(MemberRef_Class,
        std::array{mdtid_TypeDef, mdtid_TypeRef, mdtid_ModuleRef, mdtid_MethodDef, mdtid_TypeSpec}, 3));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(MethodSemantics_Association, std::array{mdtid_Event, mdtid_Property}, 1));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(MethodImpl_MethodBody, std::array{mdtid_MethodDef, mdtid_MemberRef}, 1));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(ImplMap_MemberForwarded, std::array{mdtid_Field, mdtid_MethodDef}, 1));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(ExportedType_Implementation,
        std::array{mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType}, 2));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(CustomAttribute_Type,
        std::array{mdtid_Unused, mdtid_Unused, mdtid_MethodDef, mdtid_MemberRef, mdtid_Unused}, 3));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(TypeRef_ResolutionScope,
        std::array{mdtid_Module, mdtid_ModuleRef, mdtid_AssemblyRef, mdtid_TypeRef}, 2));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(GenericParam_Owner, std::array{mdtid_TypeDef, mdtid_MethodDef}, 1));
}

TEST_F(CppRowReader, DirectIndexHasNoTagBits)
{
    ASSERT_NO_FATAL_FAILURE(CheckFamily(ClassLayout_Parent, std::array{mdtid_TypeDef}, 0));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(GenericParamConstraint_Owner, std::array{mdtid_GenericParam}, 0));
}

TEST_F(CppRowReader, ReferenceFailuresPreserveOutputs)
{
    auto row = GetRow<mdtid_TypeDef>();
    dnmd::row_reader<mdtid_TypeDef> reader{row};
    ASSERT_TRUE(row.set_column_value(TypeDef_Extends, row));
    decltype(TypeDef_Extends)::value_type target;
    ASSERT_TRUE(reader.read(TypeDef_Extends, target));
    mdcursor_t before = target;
    SetRaw(row, TypeDef_Extends, 3);
    EXPECT_FALSE(reader.read(TypeDef_Extends, target));
    EXPECT_EQ(before._reserved1, static_cast<mdcursor_t>(target)._reserved1);
    EXPECT_EQ(before._reserved2, static_cast<mdcursor_t>(target)._reserved2);
    SetRaw(row, TypeDef_Extends, 3u << 2);
    EXPECT_FALSE(reader.read(TypeDef_Extends, target));
    EXPECT_EQ(before._reserved1, static_cast<mdcursor_t>(target)._reserved1);
    EXPECT_EQ(before._reserved2, static_cast<mdcursor_t>(target)._reserved2);
}

TEST_F(CppRowReader, ReusesReaderAcrossListRanges)
{
    auto owner = GetRow<mdtid_TypeDef>();
    mdcursor_t child;
    for (uint32_t i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(md_add_new_row_to_list(owner, TypeDef_MethodList.column_index, &child));
        md_commit_row_add(child);
    }
    ASSERT_TRUE(md_add_new_row_to_list(owner, TypeDef_FieldList.column_index, &child));
    md_commit_row_add(child);
    auto next_owner = AppendRow<mdtid_TypeDef>();
    ASSERT_TRUE(md_add_new_row_to_list(next_owner, TypeDef_MethodList.column_index, &child));
    md_commit_row_add(child);

    dnmd::row_reader reader{owner};
    dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr> methods;
    dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr> fields;
    uint32_t method_count;
    uint32_t field_count;
    ASSERT_TRUE(reader.read(TypeDef_MethodList, methods, &method_count));
    EXPECT_EQ(3u, method_count);
    ASSERT_TRUE(reader.read(TypeDef_FieldList, fields, &field_count));
    EXPECT_EQ(1u, field_count);
    ASSERT_TRUE(reader.read(TypeDef_MethodList, methods, &method_count));
    EXPECT_EQ(3u, method_count);
    mdToken token;
    ASSERT_TRUE(methods.get_target_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 1, token);
    ASSERT_TRUE(fields.get_target_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_Field) | 1, token);

    dnmd::row_reader next_reader{next_owner};
    ASSERT_TRUE(next_reader.read(TypeDef_MethodList, methods, &method_count));
    EXPECT_EQ(1u, method_count);
    ASSERT_TRUE(methods.get_target_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 4, token);
    ASSERT_TRUE(next_reader.read(TypeDef_FieldList, fields, &field_count));
    EXPECT_EQ(0u, field_count);
    EXPECT_FALSE(fields);
}

TEST_F(CppRowReader, WideIndices)
{
    constexpr uint32_t row = 0x00abcdef;
    ASSERT_NO_FATAL_FAILURE(CheckWideIndex(ClassLayout_Parent, row, CreateTokenType(mdtid_TypeDef) | row));
    ASSERT_NO_FATAL_FAILURE(CheckWideIndex(MethodImpl_MethodBody,
        (row << 1) | 1, CreateTokenType(mdtid_MemberRef) | row));
    ASSERT_NO_FATAL_FAILURE(CheckWideIndex(TypeDef_Extends,
        (row << 2) | 2, CreateTokenType(mdtid_TypeSpec) | row));
    ASSERT_NO_FATAL_FAILURE(CheckWideIndex(CustomAttribute_Type,
        (row << 3) | 3, CreateTokenType(mdtid_MemberRef) | row));
    ASSERT_NO_FATAL_FAILURE(CheckWideIndex(CustomAttribute_Parent,
        (row << 5) | 21, CreateTokenType(mdtid_MethodSpec) | row));
}

TEST_F(CppRowReader, ReferenceDecodingUsesColumnTypesNotMetadataMaps)
{
    auto row = GetRow<mdtid_TypeDef>();
    dnmd::row_reader<mdtid_TypeDef> reader{row};
    SetRaw(row, TypeDef_Extends, (2u << 2) | 1);
    mdcursor_t raw = row;
    auto table = dnmd::detail::CursorTable(raw);
    auto& details = table->column_details[static_cast<uint8_t>(mdtTypeDef_Extends)];
    auto saved = details;
    details = static_cast<mdtcol_t>((static_cast<uint32_t>(details) & ~(mdtc_categorymask | mdtc_cimask))
        | mdtc_idx_table);
    mdToken token = 0;
    EXPECT_TRUE(reader.read(TypeDef_Extends, token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeRef) | 2, token);
    details = saved;
}

TEST_F(CppRowReader, IndirectConstructorsUseTypedPointerColumns)
{
    ASSERT_NO_FATAL_FAILURE(CheckIndirection(TypeDef_FieldList));
    ASSERT_NO_FATAL_FAILURE(CheckIndirection(TypeDef_MethodList));
    ASSERT_NO_FATAL_FAILURE(CheckIndirection(MethodDef_ParamList));
    ASSERT_NO_FATAL_FAILURE(CheckIndirection(EventMap_EventList));
    ASSERT_NO_FATAL_FAILURE(CheckIndirection(PropertyMap_PropertyList));
}

TEST_F(CppRowReader, EmptyIndirectTargetsKeepSentinelTokens)
{
    ASSERT_NO_FATAL_FAILURE((CheckEmptyTarget<mdtid_Field, mdtid_FieldPtr>()));
    ASSERT_NO_FATAL_FAILURE((CheckEmptyTarget<mdtid_MethodDef, mdtid_MethodPtr>()));
    ASSERT_NO_FATAL_FAILURE((CheckEmptyTarget<mdtid_Param, mdtid_ParamPtr>()));
    ASSERT_NO_FATAL_FAILURE((CheckEmptyTarget<mdtid_Event, mdtid_EventPtr>()));
    ASSERT_NO_FATAL_FAILURE((CheckEmptyTarget<mdtid_Property, mdtid_PropertyPtr>()));
}

TEST_F(CppRowReader, TypeDefListsSelectIndirectionIndependently)
{
    auto owner = GetRow<mdtid_TypeDef>();
    mdcursor_t field;
    ASSERT_TRUE(md_add_new_row_to_list(owner, TypeDef_FieldList.column_index, &field));
    md_commit_row_add(field);
    mdcursor_t method;
    ASSERT_TRUE(md_add_new_row_to_list(owner, TypeDef_MethodList.column_index, &method));
    md_commit_row_add(method);
    auto other = AppendRow<mdtid_TypeDef>();
    mdcursor_t child;
    ASSERT_TRUE(md_add_new_row_to_list(other, TypeDef_FieldList.column_index, &child));
    md_commit_row_add(child);
    ASSERT_TRUE(md_add_new_row_to_list(owner, TypeDef_FieldList.column_index, &child));
    md_commit_row_add(child);
    EXPECT_TRUE(dnmd::detail::CursorTable(field)->has_indirect_table);
    EXPECT_FALSE(dnmd::detail::CursorTable(method)->has_indirect_table);
    dnmd::row_reader reader{owner};
    mdToken token;
    ASSERT_TRUE(reader.read(TypeDef_FieldList, token));
    EXPECT_EQ(mdtid_FieldPtr, ExtractTokenType(token));
    ASSERT_TRUE(reader.read(TypeDef_MethodList, token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 1, token);
}

TEST_F(CppRowReader, LoadingFlagsIndirectionWithAnEmptyElementTable)
{
    std::array<uint8_t, 92> image{};
    std::memcpy(image.data(), "BSJB", 4);
    image[4] = 1;
    image[6] = 1;
    image[12] = 12;
    std::memcpy(image.data() + 16, "v4.0.30319", sizeof("v4.0.30319"));
    image[30] = 1;
    image[32] = 44;
    image[36] = 48;
    std::memcpy(image.data() + 40, "#-", 2);
    image[48] = 2;
    image[51] = 1;
    image[52] = (1 << mdtid_TypeDef) | (1 << mdtid_FieldPtr);
    image[68] = 1;
    image[72] = 1;
    image[86] = 1;
    mdhandle_t raw;
    ASSERT_TRUE(md_create_handle(image.data(), image.size(), &raw));
    dnmd::handle_ptr loaded{raw};
    auto owner = dnmd::md_create_cursor<mdtid_TypeDef>(loaded.get());
    ASSERT_TRUE(owner);
    auto context = dnmd::detail::CursorTable(owner)->cxt;
    EXPECT_EQ(0u, context->tables[mdtid_Field].row_count);
    EXPECT_TRUE(context->tables[mdtid_Field].has_indirect_table);
    dnmd::row_reader reader{owner.as()};
    dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr> field;
    ASSERT_TRUE(reader.read(TypeDef_FieldList, field));
    EXPECT_FALSE(field);
    mdToken token;
    ASSERT_TRUE(field.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_FieldPtr) | 1, token);
    ASSERT_TRUE(field.get_target_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_Field), token);
}

#ifdef DNMD_PORTABLE_PDB
TEST_F(CppRowReader, PortablePdbCodedIndexTags)
{
    ASSERT_NO_FATAL_FAILURE(CheckFamily(CustomDebugInformation_Parent,
        std::array{mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param,
            mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property,
            mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec, mdtid_Assembly,
            mdtid_AssemblyRef, mdtid_File, mdtid_ExportedType, mdtid_ManifestResource, mdtid_GenericParam,
            mdtid_GenericParamConstraint, mdtid_MethodSpec, mdtid_Document, mdtid_LocalScope,
            mdtid_LocalVariable, mdtid_LocalConstant, mdtid_ImportScope}, 5));
}
#endif
