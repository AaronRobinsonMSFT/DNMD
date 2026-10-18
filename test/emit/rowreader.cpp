#include <dnmd.hpp>
#include <gtest/gtest.h>
#include <array>

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
        dnmd::cursor_indirect<mdtid_MethodDef, mdtid_MethodPtr>&>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::Module_Name), char const**>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::TypeDef_Flags), char const**>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::TypeDef_TypeName), uint32_t*>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::TypeDef_Extends), mdcursor_t&>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::TypeDef_Extends), dnmd::cursor_raw<mdtid_TypeDef>&>);
    static_assert(!can_read_v<type_reader, decltype(dnmd::columns::TypeDef_MethodList),
        dnmd::cursor_indirect<mdtid_Field, mdtid_FieldPtr>&>);
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
            return dnmd::cursor_raw<TableId>{raw}.as();
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
        void CheckFamily(dnmd::column<TableId, dnmd::cursor_raw<Targets...>> column,
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

                    dnmd::cursor_raw<Targets...> position;
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
            auto row = dnmd::cursor_raw<TableId>{mdcursor_t{reinterpret_cast<intptr_t>(&table), 1}}.as();
            dnmd::row_reader<TableId> reader{row};
            mdToken actual = 0;
            ASSERT_TRUE(reader.read(column, actual));
            EXPECT_EQ(expected, actual);
        }

        template<mdtable_id_t OwnerTableId, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        void CheckIndirection(dnmd::column<OwnerTableId, dnmd::cursor_indirect<DirectTableId, IndirectTableId>> list)
        {
            using position_type = dnmd::cursor_indirect<DirectTableId, IndirectTableId>;
            auto owner = GetRow<OwnerTableId>();
            mdcursor_t direct{};
            ASSERT_TRUE(md_add_new_row_to_list(owner, list.column_index, &direct));
            md_commit_row_add(direct);
            auto other_owner = AppendRow<OwnerTableId>();
            mdcursor_t other{};
            ASSERT_TRUE(md_add_new_row_to_list(other_owner, list.column_index, &other));
            md_commit_row_add(other);
            ASSERT_TRUE(md_add_new_row_to_list(owner, list.column_index, &other));
            md_commit_row_add(other);
            auto pointer = GetRow<IndirectTableId>();
            constexpr auto target_column = dnmd::detail::pointer_column<IndirectTableId>();
            ASSERT_TRUE(pointer.set_column_value(target_column, dnmd::cursor_raw<DirectTableId>{direct}));

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
            table->column_details[0] = static_cast<mdtcol_t>(static_cast<uint32_t>(saved) & ~mdtc_categorymask);
            position_type typed{source};
            mdcursor_t c_target{};
            EXPECT_FALSE(md_resolve_indirect_cursor(source, &c_target));
            EXPECT_TRUE(typed);
            mdToken token;
            EXPECT_TRUE(typed.get_target_token(token));
            EXPECT_EQ(direct_token, token);
            table->column_details[0] = saved;

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
                dnmd::cursor_indirect<DirectTableId, IndirectTableId> position{source};
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
