#include "internal.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <string_view>

extern "C" uint8_t get_table_column_count(mdtable_id_t);
extern "C" bool initialize_table_details(uint32_t const*, mdcxt_flag_t, mdtable_id_t, bool, mdtable_t*);
extern "C" bool consume_table_rows(mdtable_t*, uint8_t const**, size_t*);

namespace
{
    using namespace dnmd;

    // Independent storage kinds: fixed 2/4-byte scalars, String/Guid/Blob heaps, and References.
    constexpr std::string_view layouts[] = {
        "2SGGG",
        "RSS",
        "4SSRRR",
        "R",
        "2SB",
        "R",
        "422SBR",
        "R",
        "22S",
        "RR",
        "RSB",
        "2RB",
        "RRB",
        "RB",
        "2RB",
        "24R",
        "4R",
        "B",
        "RR",
        "R",
        "2SR",
        "RR",
        "R",
        "2SB",
        "2RR",
        "RRR",
        "S",
        "B",
        "2RSR",
        "4R",
        "44",
        "4",
        "422224BSS",
        "4", // AssemblyProcessor
        "444", // AssemblyOS
        "22224BSSB",
        "4R", // AssemblyRefProcessor
        "444R", // AssemblyRefOS
        "4SB",
        "44SSR",
        "44SR",
        "RR",
        "22RS",
        "RB",
        "RR",
#ifdef DNMD_PORTABLE_PDB
        "", "", "",
        "BGBG",
        "RB",
        "RRRR44",
        "22S",
        "SB",
        "RB",
        "RR",
        "RGB",
#endif
    };
    static_assert(std::size(layouts) == mdtid_End);

    constexpr uint32_t ExpectedWidth(char kind, mdcxt_flag_t flags)
    {
        return kind == '4' || (kind == 'S' && (flags & mdc_large_string_heap))
            || (kind == 'G' && (flags & mdc_large_guid_heap))
            || (kind == 'B' && (flags & mdc_large_blob_heap))
            || (kind == 'R' && (flags & mdc_minimal_delta)) ? 4u : 2u;
    }

    struct layout_fixture
    {
        mdcxt_t context{};
        std::array<mdtcol_t, 9> columns{};
        mdtable_t table{};

        layout_fixture()
        {
            table.cxt = &context;
            table.column_details = columns.data();
        }
    };

    template<mdtable_id_t Table>
    void CheckInitialLayout()
    {
        constexpr auto expected = layouts[Table];
        constexpr auto count = std::tuple_size_v<decltype(detail::table_columns<Table>())>;
        EXPECT_EQ(expected.size(), count);
        EXPECT_EQ(expected.size(), get_table_column_count(Table));
        if constexpr (count != 0)
        {
            for (unsigned combination = 0; combination != 16; ++combination)
            {
                SCOPED_TRACE(combination);
                auto flags = static_cast<mdcxt_flag_t>((combination & 7) | ((combination & 8) ? mdc_minimal_delta : 0));
                std::array<uint32_t, mdtid_End> counts{};
                counts[Table] = 1;
                layout_fixture fixture;
                bool sorted = (combination & 1) != 0;
                ASSERT_TRUE(initialize_table_details(counts.data(), flags, Table, sorted, &fixture.table));
                EXPECT_EQ(&fixture.context, fixture.table.cxt);
                EXPECT_EQ(fixture.columns.data(), fixture.table.column_details);
                EXPECT_EQ(Table, fixture.table.table_id);
                EXPECT_EQ(1u, fixture.table.row_count);
                EXPECT_EQ(sorted, fixture.table.is_sorted);
                EXPECT_FALSE(fixture.table.is_initialized);
                EXPECT_FALSE(fixture.table.has_indirect_table);
                EXPECT_EQ(expected.size(), fixture.table.column_count);
                uint32_t offset = 0;
                for (size_t index = 0; index != expected.size(); ++index)
                {
                    SCOPED_TRACE(index);
                    auto width = ExpectedWidth(expected[index], flags);
                    auto word = (offset << 2) | (width == 4 ? 2u : 1u);
                    EXPECT_EQ(word, static_cast<uint32_t>(fixture.columns[index]));
                    offset += width;
                }
                EXPECT_EQ(offset, fixture.table.row_size_bytes);
            }
        }
    }

    template<mdtable_id_t... Tables>
    void CheckInitialLayouts(detail::table_sequence<Tables...>)
    {
        (CheckInitialLayout<Tables>(), ...);
    }

    template<mdtable_id_t Table>
    void CheckNewLayout(mdcxt_t& context)
    {
        if constexpr (!layouts[Table].empty())
        {
            SCOPED_TRACE(Table);
            ASSERT_TRUE(detail::initialize_new_table_details<Table>(&context));
            auto& table = context.tables[Table];
            EXPECT_EQ(&context, table.cxt);
            EXPECT_EQ(Table, table.table_id);
            EXPECT_EQ(0u, table.row_count);
            EXPECT_FALSE(table.is_initialized);
            EXPECT_FALSE(table.has_indirect_table);
            EXPECT_EQ(nullptr, table.data.ptr);
            EXPECT_EQ(0u, table.data.size);
            constexpr mdtable_id_t sorted[] = {mdtid_ClassLayout, mdtid_Constant, mdtid_CustomAttribute,
                mdtid_DeclSecurity, mdtid_FieldLayout, mdtid_FieldMarshal, mdtid_FieldRva,
                mdtid_GenericParam, mdtid_GenericParamConstraint, mdtid_ImplMap, mdtid_InterfaceImpl,
                mdtid_MethodImpl, mdtid_MethodSemantics, mdtid_NestedClass,
#ifdef DNMD_PORTABLE_PDB
                mdtid_LocalScope, mdtid_StateMachineMethod, mdtid_CustomDebugInformation,
#endif
            };
            EXPECT_EQ(std::find(std::begin(sorted), std::end(sorted), Table) != std::end(sorted), table.is_sorted);
            uint32_t offset = 0;
            for (size_t index = 0; index != layouts[Table].size(); ++index)
            {
                auto width = ExpectedWidth(layouts[Table][index], mdc_none);
                EXPECT_EQ((offset << 2) | (width == 4 ? 2u : 1u),
                    static_cast<uint32_t>(table.column_details[index]));
                offset += width;
            }
            EXPECT_EQ(offset, table.row_size_bytes);
        }
    }

    template<mdtable_id_t... Tables>
    void CheckNewLayouts(mdcxt_t& context, detail::table_sequence<Tables...>)
    {
        (CheckNewLayout<Tables>(context), ...);
    }
}

TEST(InitialTableLayout, CompleteLayoutsAndHeapFlagCombinations)
{
    CheckInitialLayouts(detail::all_tables{});
}

TEST(InitialTableLayout, ContextColumnArraysUseCompactStorage)
{
    handle_ptr handle{md_create_new_handle()};
    ASSERT_TRUE(handle);
    auto& context = detail::context(handle.get());
    for (int id = mdtid_First; id + 1 < mdtid_End; ++id)
    {
        SCOPED_TRACE(id);
        auto expected_next = context.tables[id].column_details + layouts[id].size();
        EXPECT_EQ(expected_next, context.tables[id + 1].column_details);
    }
}

TEST(InitialTableLayout, DirectAndEveryCodedFamilyBoundary)
{
    struct reference
    {
        mdtable_id_t owner;
        uint8_t column;
        mdtable_id_t target;
        uint8_t tag_bits;
    };
    constexpr reference references[] = {
        {mdtid_InterfaceImpl, 0, mdtid_TypeDef, 0},
        {mdtid_TypeDef, 3, mdtid_TypeSpec, 2},
        {mdtid_Constant, 1, mdtid_Property, 2},
        {mdtid_CustomAttribute, 0, mdtid_MethodSpec, 5},
        {mdtid_FieldMarshal, 0, mdtid_Param, 1},
        {mdtid_DeclSecurity, 1, mdtid_Assembly, 2},
        {mdtid_MemberRef, 0, mdtid_TypeSpec, 3},
        {mdtid_MethodSemantics, 2, mdtid_Property, 1},
        {mdtid_MethodImpl, 1, mdtid_MemberRef, 1},
        {mdtid_ImplMap, 1, mdtid_MethodDef, 1},
        {mdtid_ExportedType, 4, mdtid_ExportedType, 2},
        {mdtid_CustomAttribute, 1, mdtid_MemberRef, 3},
        {mdtid_TypeRef, 0, mdtid_TypeRef, 2},
        {mdtid_GenericParam, 2, mdtid_MethodDef, 1},
#ifdef DNMD_PORTABLE_PDB
        {mdtid_CustomDebugInformation, 0, mdtid_ImportScope, 5},
        {mdtid_LocalScope, 2, mdtid_LocalVariable, 0},
        {mdtid_LocalScope, 3, mdtid_LocalConstant, 0},
#endif
    };
    for (auto reference : references)
    {
        SCOPED_TRACE(reference.owner);
        SCOPED_TRACE(reference.column);
        auto boundary = uint32_t{1} << (16 - reference.tag_bits);
        for (auto rows : {boundary - 1, boundary})
        {
            std::array<uint32_t, mdtid_End> counts{};
            counts[reference.owner] = 1;
            counts[reference.target] = rows;
            for (auto flags : {mdc_none, mdc_minimal_delta})
            {
                layout_fixture fixture;
                ASSERT_TRUE(initialize_table_details(counts.data(), flags, reference.owner, false, &fixture.table));
                EXPECT_EQ(rows == boundary || flags == mdc_minimal_delta ? mdtc_b4 : mdtc_b2,
                    fixture.columns[reference.column] & mdtc_widthmask);
            }
        }
    }
}

TEST(InitialTableLayout, ListsSelectTheirOwnDirectOrPtrCounts)
{
    struct list
    {
        mdtable_id_t owner;
        uint8_t column;
        mdtable_id_t direct;
        mdtable_id_t pointer;
    };
    constexpr list lists[] = {
        {mdtid_TypeDef, 4, mdtid_Field, mdtid_FieldPtr},
        {mdtid_TypeDef, 5, mdtid_MethodDef, mdtid_MethodPtr},
        {mdtid_MethodDef, 5, mdtid_Param, mdtid_ParamPtr},
        {mdtid_EventMap, 1, mdtid_Event, mdtid_EventPtr},
        {mdtid_PropertyMap, 1, mdtid_Property, mdtid_PropertyPtr},
    };
    for (auto list : lists)
    {
        SCOPED_TRACE(list.owner);
        for (uint32_t pointer_rows : {0u, 1u, 0x10000u})
        {
            std::array<uint32_t, mdtid_End> counts{};
            counts[list.owner] = 1;
            counts[list.direct] = 0x10000;
            counts[list.pointer] = pointer_rows;
            layout_fixture fixture;
            ASSERT_TRUE(initialize_table_details(counts.data(), mdc_none, list.owner, false, &fixture.table));
            auto details = fixture.columns[list.column];
            EXPECT_EQ(pointer_rows == 1 ? mdtc_b2 : mdtc_b4, details & mdtc_widthmask);
            ASSERT_TRUE(initialize_table_details(counts.data(), mdc_none, list.direct, false, &fixture.table));
            EXPECT_EQ(pointer_rows != 0, fixture.table.has_indirect_table);
        }
    }
}

TEST(InitialTableLayout, NativePreparationIsSchemaOnlyAndUsesIndependentSortedDefaults)
{
    mdcxt_t context{};
    std::array<mdtable_t, mdtid_End> tables{};
    std::array<std::array<mdtcol_t, 9>, mdtid_End> columns{};
    context.tables = tables.data();
    for (size_t table = 0; table != tables.size(); ++table)
    {
        tables[table].cxt = &context;
        tables[table].column_details = columns[table].data();
    }
    CheckNewLayouts(context, detail::all_tables{});
    EXPECT_EQ(nullptr, context.editor);
}

TEST(InitialTableLayout, NativePreparationUsesExistingTargetCountsWithoutHeapLengthInference)
{
    mdcxt_t context{};
    std::array<mdtable_t, mdtid_End> tables{};
    std::array<mdtcol_t, 6> columns{};
    context.tables = tables.data();
    context.strings_heap.size = 0x10000;
    auto& table = tables[mdtid_TypeDef];
    table.cxt = &context;
    table.column_details = columns.data();
    tables[mdtid_TypeSpec].row_count = 0x4000;
    tables[mdtid_Field].row_count = 0x10000;
    tables[mdtid_MethodDef].row_count = 0xffff;
    ASSERT_TRUE(detail::initialize_new_table_details<mdtid_TypeDef>(&context));
    EXPECT_EQ(mdtc_b2, columns[1] & mdtc_widthmask);
    EXPECT_EQ(mdtc_b4, columns[3] & mdtc_widthmask);
    EXPECT_EQ(mdtc_b4, columns[4] & mdtc_widthmask);
    EXPECT_EQ(mdtc_b2, columns[5] & mdtc_widthmask);
    EXPECT_EQ(18u, table.row_size_bytes);
    tables[mdtid_FieldPtr].row_count = 1;
    ASSERT_TRUE(detail::initialize_new_table_details<mdtid_TypeDef>(&context));
    EXPECT_EQ(mdtc_b2, columns[4] & mdtc_widthmask);
    EXPECT_EQ(16u, table.row_size_bytes);
    EXPECT_EQ(0u, table.row_count);
    EXPECT_FALSE(table.is_initialized);
    EXPECT_EQ(nullptr, context.editor);
}

TEST(InitialTableLayout, RowConsumptionRejectsTruncationAndOverflowWithoutAdvancing)
{
    layout_fixture fixture;
    std::array<uint8_t, 5> bytes{};
    auto data = bytes.data();
    uint8_t const* input = data;
    size_t length = bytes.size();
    fixture.table.row_size_bytes = 4;
    fixture.table.row_count = UINT32_MAX / 4 + 1;
    EXPECT_FALSE(consume_table_rows(&fixture.table, &input, &length));
    EXPECT_EQ(data, input);
    EXPECT_EQ(bytes.size(), length);
    EXPECT_EQ(nullptr, fixture.table.data.ptr);
    fixture.table.row_size_bytes = 2;
    fixture.table.row_count = 2;
    ASSERT_TRUE(consume_table_rows(&fixture.table, &input, &length));
    EXPECT_EQ(data + 4, input);
    EXPECT_EQ(1u, length);
    EXPECT_EQ(data, fixture.table.data.ptr);
    EXPECT_EQ(4u, fixture.table.data.size);
}
