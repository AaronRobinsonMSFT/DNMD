#include <dnmd_schema.hpp>
#include <dnmd_schema.hpp>
#include <gtest/gtest.h>
#include <array>

namespace
{
    template<mdtable_id_t Element, mdtable_id_t Parent, mdtable_id_t Ptr, mdtable_id_t Owner, typename Kind>
    constexpr bool RelationshipMatches(dnmd::column<Owner, Kind> expected)
    {
        using namespace dnmd;
        constexpr auto list = detail::parent_list_column<Element>();
        bool matches = decltype(list)::table_id == Owner && list.column_index == expected.column_index
            && detail::parent_table_id<Element>() == Parent
            && detail::pointer_table_id<Element>() == Ptr && detail::pointer_table_id(Element) == Ptr;
        if constexpr (Ptr != mdtid_Unused)
        {
            constexpr auto pointer = detail::pointer_column<Ptr>();
            return matches && detail::is_pointer_table<Ptr>() && decltype(pointer)::table_id == Ptr
                && pointer.column_index == 0
                && std::is_same_v<typename decltype(pointer)::value_type, raw_cursor<Element>>;
        }
        else
            return matches;
    }
}

TEST(CppSchemaHeader, CanonicalListRelationships)
{
    using namespace dnmd;
    static_assert(RelationshipMatches<mdtid_Field, mdtid_TypeDef, mdtid_FieldPtr>(columns::TypeDef_FieldList));
    static_assert(RelationshipMatches<mdtid_MethodDef, mdtid_TypeDef, mdtid_MethodPtr>(columns::TypeDef_MethodList));
    static_assert(RelationshipMatches<mdtid_Param, mdtid_MethodDef, mdtid_ParamPtr>(columns::MethodDef_ParamList));
    static_assert(RelationshipMatches<mdtid_Event, mdtid_TypeDef, mdtid_EventPtr>(columns::EventMap_EventList));
    static_assert(RelationshipMatches<mdtid_Property, mdtid_TypeDef, mdtid_PropertyPtr>(columns::PropertyMap_PropertyList));
    static_assert(detail::parent_table_id<mdtid_Module>() == mdtid_Unused);
    static_assert(detail::parent_table_id<mdtid_TypeDef>() == mdtid_Unused);
    static_assert(detail::pointer_table_id<mdtid_Module>() == mdtid_Unused);
    static_assert(detail::pointer_table_id<mdtid_Unused>() == mdtid_Unused);
    static_assert(!detail::is_pointer_table<mdtid_Unused>());
    static_assert(!detail::is_pointer_table<mdtid_Field>());
    static_assert(std::tuple_size_v<decltype(detail::list_columns<mdtid_TypeDef>())> == 2);
    static_assert(std::tuple_size_v<decltype(detail::list_columns<mdtid_EventMap>())> == 1);
    static_assert(std::tuple_size_v<decltype(detail::list_columns<mdtid_Module>())> == 0);
#ifdef DNMD_PORTABLE_PDB
    static_assert(RelationshipMatches<mdtid_LocalVariable, mdtid_LocalScope, mdtid_Unused>(columns::LocalScope_VariableList));
    static_assert(RelationshipMatches<mdtid_LocalConstant, mdtid_LocalScope, mdtid_Unused>(columns::LocalScope_ConstantList));
    static_assert(std::tuple_size_v<decltype(detail::all_list_columns())> == 7);
#else
    static_assert(std::tuple_size_v<decltype(detail::all_list_columns())> == 5);
#endif
}

TEST(CppSchemaHeader, ListTargetsUseElementTableIndirectionState)
{
    using namespace dnmd;
    mdcxt_t context{};
    std::array<mdtable_t, mdtid_End> tables{};
    context.tables = tables.data();
    auto& owner = tables[mdtid_TypeDef];
    owner.cxt = &context;
    owner.has_indirect_table = true;
    tables[mdtid_Field].row_count = 11;
    tables[mdtid_FieldPtr].row_count = 5;
    tables[mdtid_MethodDef].row_count = 7;
    EXPECT_EQ(mdtid_Field, detail::list_target_table(&context, columns::TypeDef_FieldList));
    EXPECT_EQ(12u, detail::list_end_row(&owner, columns::TypeDef_FieldList));
    tables[mdtid_Field].has_indirect_table = true;
    EXPECT_EQ(mdtid_FieldPtr, detail::list_target_table(&context, columns::TypeDef_FieldList));
    EXPECT_EQ(6u, detail::list_end_row(&owner, columns::TypeDef_FieldList));
    EXPECT_EQ(mdtid_MethodDef, detail::list_target_table(&context, columns::TypeDef_MethodList));
    EXPECT_EQ(8u, detail::list_end_row(&owner, columns::TypeDef_MethodList));
    static_assert(detail::select_list_target<mdtid_Field, mdtid_FieldPtr>(false) == mdtid_Field);
    static_assert(detail::select_list_target<mdtid_Field, mdtid_FieldPtr>(true) == mdtid_FieldPtr);
#ifdef DNMD_PORTABLE_PDB
    auto& scope = tables[mdtid_LocalScope];
    scope.cxt = &context;
    tables[mdtid_LocalVariable].has_indirect_table = true;
    tables[mdtid_LocalVariable].row_count = 3;
    EXPECT_EQ(mdtid_LocalVariable, detail::list_target_table(&context, columns::LocalScope_VariableList));
    EXPECT_EQ(4u, detail::list_end_row(&scope, columns::LocalScope_VariableList));
#endif
}

TEST(CppSchemaHeader, StandaloneSchema)
{
    using namespace dnmd;
    static_assert(detail::column_heap<column_type_constant>() == mdhid_None);
    static_assert(detail::column_heap<column_type_string>() == mdhid_String);
    static_assert(detail::column_heap<column_type_blob>() == mdhid_Blob);
    static_assert(detail::column_heap<column_type_guid>() == mdhid_Guid);
    static_assert(detail::column_heap<column_type_userstring>() == mdhid_UserString);
    static_assert(std::is_same_v<detail::all_tables::value_type, mdtable_id_t>);
    static_assert(detail::all_tables::size() == mdtid_End);
    static_assert(std::is_same_v<typename detail::table_range<mdtid_Module, mdtid_TypeDef>::type,
        detail::table_sequence<mdtid_Module, mdtid_TypeRef>>);
    static_assert(!std::is_convertible_v<std::integer_sequence<int, 0, 1>,
        detail::table_sequence<mdtid_Module, mdtid_TypeRef>>);
    constexpr auto keys = detail::table_sort_keys<mdtid_GenericParam>();
    static_assert(std::tuple_size_v<decltype(keys)> == 2);
    EXPECT_EQ(mdtGenericParam_Owner, std::get<0>(keys).column.column_index);
    EXPECT_EQ(mdtGenericParam_Number, std::get<1>(keys).column.column_index);
    static_assert(detail::for_each_column<mdtid_TypeDef>([](auto... columns) {
        return ((static_cast<uint8_t>(columns.column_index) < 6) && ...);
    }));
    static_assert(detail::is_list_column(columns::TypeDef_FieldList));
    static_assert(!detail::is_list_column(columns::TypeDef_Extends));
}

TEST(CppSchemaHeader, SelectedMetadataPreservesListAndKeyOrder)
{
    using namespace dnmd;
    int next_list = 4;
    EXPECT_TRUE(detail::for_each_column<mdtid_TypeDef>([&](auto... columns) {
        return ((static_cast<uint8_t>(columns.column_index) == next_list++) && ...);
    }, detail::list_columns<mdtid_TypeDef>()));
    EXPECT_EQ(6, next_list);

    auto keys = detail::table_sort_keys<mdtid_GenericParam>();
    int next_key = 0;
    auto check = [&](auto key) {
        auto expected = next_key++ == 0 ? mdtGenericParam_Owner : mdtGenericParam_Number;
        return key.column.column_index == expected && !key.descending;
    };
    EXPECT_TRUE(detail::for_each_column<mdtid_GenericParam>([&](auto... selected) {
        return (check(selected) && ...);
    }, keys));
    EXPECT_EQ(2, next_key);
}

TEST(CppSchemaHeader, StandaloneInitialLayout)
{
    using namespace dnmd;
    static_assert(detail::constant_column_width(columns::MethodDef_Rva) == 4);
    static_assert(detail::constant_column_width(columns::MethodDef_ImplFlags) == 2);
    static_assert(detail::constant_column_width(columns::ClassLayout_ClassSize) == 4);
    static_assert(detail::constant_column_width(columns::ClassLayout_PackingSize) == 2);
    constexpr auto counts = [] {
        std::array<uint32_t, mdtid_End> rows{};
        rows[mdtid_TypeDef] = 1;
        rows[mdtid_TypeSpec] = 0x4000;
        rows[mdtid_MemberRef] = 0x2000;
        return rows;
    }();
    static_assert((detail::initial_column_details(columns::CustomAttribute_Type, counts, mdc_none)
        & mdtc_widthmask) == mdtc_b4);
    std::array<mdtcol_t, 6> columns{};
    mdtable_t table{};
    table.column_details = columns.data();
    ASSERT_TRUE(detail::initialize_table_details<mdtid_TypeDef>(counts, mdc_none, false, table));
    EXPECT_EQ(16u, table.row_size_bytes);
    EXPECT_EQ(mdtc_b4, columns[3] & mdtc_widthmask);
    EXPECT_FALSE(table.is_initialized);
}
