#include <dnmd.hpp>
#include <gtest/gtest.h>
#include <array>
#include <vector>

namespace
{
    template<typename Row, typename Column, typename Value>
    auto SupportsFind(int) -> decltype(dnmd::md_find_row_from_cursor(
        std::declval<Row>(), std::declval<Column>(), std::declval<Value>()), std::true_type{});
    template<typename Row, typename Column, typename Value>
    auto SupportsFind(...) -> std::false_type;
    template<typename Row, typename Column, typename Value>
    inline constexpr bool can_find_v = decltype(SupportsFind<Row, Column, Value>(0))::value;

    using type_row = dnmd::cursor<mdtid_TypeDef>;
    using numeric_iterator = dnmd::column_iterator<mdtid_ENCLog, dnmd::column_type_constant>;
    static_assert(std::is_default_constructible_v<numeric_iterator>);
    static_assert(std::is_trivially_copyable_v<numeric_iterator>);
    static_assert(std::is_same_v<std::iterator_traits<numeric_iterator>::iterator_category,
        std::random_access_iterator_tag>);
    static_assert(std::is_same_v<std::iterator_traits<numeric_iterator>::value_type, uint32_t>);
    static_assert(std::is_same_v<decltype(*std::declval<numeric_iterator>()), uint32_t>);
    static_assert(!std::is_assignable_v<decltype(*std::declval<numeric_iterator>()), uint32_t>);
    static_assert(can_find_v<type_row, decltype(dnmd::columns::TypeDef_Flags), uint32_t>);
    static_assert(can_find_v<type_row, decltype(dnmd::columns::TypeDef_Extends), type_row>);
    static_assert(can_find_v<type_row, decltype(dnmd::columns::TypeDef_Extends), dnmd::raw_cursor<mdtid_TypeRef>>);
    static_assert(!can_find_v<type_row, decltype(dnmd::columns::TypeDef_Extends), dnmd::cursor<mdtid_Field>>);
    static_assert(!can_find_v<type_row, decltype(dnmd::columns::Module_Generation), uint32_t>);
    static_assert(!can_find_v<type_row, decltype(dnmd::columns::TypeDef_TypeName), uint32_t>);
    static_assert(!can_find_v<type_row, decltype(dnmd::columns::TypeDef_TypeName), char const*>);
    static_assert(!can_find_v<dnmd::cursor<mdtid_Module>, decltype(dnmd::columns::Module_Mvid), mdguid_t>);
    static_assert(!can_find_v<dnmd::cursor<mdtid_TypeSpec>, decltype(dnmd::columns::TypeSpec_Signature), uint32_t>);

    class CppColumnSearch : public testing::Test
    {
    protected:
        std::vector<uint8_t> image;
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
            mdcursor_t row{};
            if constexpr (TableId == mdtid_MethodDef)
            {
                auto owner = GetRow<mdtid_TypeDef>();
                EXPECT_TRUE(md_add_new_row_to_list(owner, dnmd::columns::TypeDef_MethodList.column_index, &row));
            }
            else
            {
                EXPECT_TRUE(md_append_row(handle.get(), TableId, &row));
            }
            md_commit_row_add(row);
            return dnmd::raw_cursor<TableId>{row}.as();
        }

        template<mdtable_id_t TableId>
        dnmd::cursor<TableId> GetRow()
        {
            auto first = dnmd::md_create_cursor<TableId>(handle.get());
            return first ? first.as() : AppendRow<TableId>();
        }

        template<mdtable_id_t TableId, typename ColumnType>
        void Fill(dnmd::column<TableId, ColumnType> col, std::initializer_list<uint32_t> values)
        {
            for (uint32_t value : values)
            {
                mdcursor_t added;
                ASSERT_TRUE(md_append_row(handle.get(), TableId, &added));
                auto row = dnmd::raw_cursor<TableId>{added}.as();
                ASSERT_TRUE(row.set_column_value(col, value));
                md_commit_row_add(added);
            }
        }

        void Reload()
        {
            size_t size = 0;
            EXPECT_FALSE(md_write_to_buffer(handle.get(), nullptr, &size));
            ASSERT_GT(size, 0u);
            std::vector<uint8_t> next(size);
            ASSERT_TRUE(md_write_to_buffer(handle.get(), next.data(), &size));
            mdhandle_t raw;
            ASSERT_TRUE(md_create_handle(next.data(), next.size(), &raw));
            handle.reset(raw);
            image = std::move(next);
        }

        template<mdtable_id_t TableId>
        void ExpectRow(dnmd::raw_cursor<TableId> position, uint32_t index)
        {
            ASSERT_TRUE(position);
            mdToken token;
            ASSERT_TRUE(position.get_token(token));
            EXPECT_EQ(CreateTokenType(TableId) | index, token);
        }

        template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds>
        void CheckPrimary(dnmd::column<TableId, dnmd::raw_cursor<TargetTableIds...>> col, mdToken nil)
        {
            auto row = GetRow<TableId>();
            dnmd::detail::CursorTable(row)->is_sorted = true;
            dnmd::raw_cursor<TableId> start;
            uint32_t count = 0;
            ASSERT_EQ(dnmd::range_result::found, dnmd::md_find_range_from_cursor(row, col, nil, start, &count));
            EXPECT_EQ(1u, count);
            mdcursor_t c_start;
            uint32_t c_count = 0;
            uint32_t c_key = nil;
            if constexpr (sizeof...(TargetTableIds) == 1)
                c_key = (dnmd::detail::RidFromToken)(nil);
            ASSERT_EQ(MD_RANGE_FOUND, md_find_range_from_cursor(row, col.column_index, c_key, &c_start, &c_count));
            EXPECT_EQ(c_count, count);
            EXPECT_EQ(c_start._reserved1, static_cast<mdcursor_t>(start)._reserved1);
            EXPECT_EQ(c_start._reserved2, static_cast<mdcursor_t>(start)._reserved2);
        }

        template<mdtable_id_t OwnerTableId, mdtable_id_t ElementTableId, mdtable_id_t PointerTableId>
        void CheckParents(dnmd::column<OwnerTableId, dnmd::indirect_cursor<ElementTableId, PointerTableId>> list)
        {
            auto owner = GetRow<OwnerTableId>();
            mdcursor_t first;
            ASSERT_TRUE(md_add_new_row_to_list(owner, list.column_index, &first));
            md_commit_row_add(first);
            auto other_owner = AppendRow<OwnerTableId>();
            mdcursor_t second;
            ASSERT_TRUE(md_add_new_row_to_list(other_owner, list.column_index, &second));
            md_commit_row_add(second);
            mdcursor_t third;
            ASSERT_TRUE(md_add_new_row_to_list(owner, list.column_index, &third));
            md_commit_row_add(third);
            mdToken owner_token;
            mdToken other_token;
            if constexpr (ElementTableId == mdtid_Event || ElementTableId == mdtid_Property)
            {
                auto type = GetRow<mdtid_TypeDef>();
                auto other_type = AppendRow<mdtid_TypeDef>();
                ASSERT_TRUE(type.get_token(owner_token));
                ASSERT_TRUE(other_type.get_token(other_token));
                if constexpr (ElementTableId == mdtid_Event)
                {
                    ASSERT_TRUE(owner.set_column_value(dnmd::columns::EventMap_Parent, type));
                    ASSERT_TRUE(other_owner.set_column_value(dnmd::columns::EventMap_Parent, other_type));
                }
                else
                {
                    ASSERT_TRUE(owner.set_column_value(dnmd::columns::PropertyMap_Parent, type));
                    ASSERT_TRUE(other_owner.set_column_value(dnmd::columns::PropertyMap_Parent, other_type));
                }
            }
            else
            {
                ASSERT_TRUE(owner.get_token(owner_token));
                ASSERT_TRUE(other_owner.get_token(other_token));
            }
            std::array<mdToken, 3> tokens;
            ASSERT_TRUE(md_cursor_to_token(first, &tokens[0]));
            ASSERT_TRUE(md_cursor_to_token(second, &tokens[1]));
            ASSERT_TRUE(md_cursor_to_token(third, &tokens[2]));
            std::array<mdToken, 3> expected{owner_token, other_token, owner_token};
            for (bool loaded : {false, true})
            {
                if (loaded)
                    ASSERT_NO_FATAL_FAILURE(Reload());
                for (size_t i = 0; i < tokens.size(); ++i)
                {
                    auto element = dnmd::md_token_to_cursor<ElementTableId>(handle.get(), tokens[i]);
                    ASSERT_TRUE(element);
                    mdToken actual;
                    ASSERT_TRUE(element.as().get_parent_token(actual));
                    EXPECT_EQ(expected[i], actual);
                    dnmd::raw_cursor<dnmd::detail::parent_table_id<ElementTableId>()> parent;
                    ASSERT_TRUE(dnmd::md_find_cursor_of_range_element(element.as(), parent));
                    ASSERT_TRUE(parent.get_token(actual));
                    EXPECT_EQ(expected[i], actual);
                }
            }
        }
    };
}

using namespace dnmd::columns;

TEST_F(CppColumnSearch, RandomAccessIteratorAndStandardAlgorithms)
{
    ASSERT_NO_FATAL_FAILURE(Fill(ENCLog_Token, {0, 0x80000000, 0x80000000, 0xffffffff}));
    auto row = dnmd::md_create_cursor<mdtid_ENCLog>(handle.get()).as();
    dnmd::column_iterator first{row, ENCLog_Token};
    auto last = first.end();
    EXPECT_EQ(4, std::distance(first, last));
    EXPECT_EQ(0u, *first);
    EXPECT_EQ(0x80000000u, first[1]);
    EXPECT_EQ(0xffffffffu, last[-1]);
    EXPECT_TRUE(first < last);
    EXPECT_TRUE(first <= first);
    EXPECT_TRUE(last > first);
    EXPECT_TRUE(last >= last);
    auto copy = first;
    EXPECT_EQ(0u, *copy++);
    EXPECT_EQ(first + 1, copy);
    EXPECT_EQ(first, --copy);
    copy += 3;
    EXPECT_EQ(0xffffffffu, *copy--);
    copy -= 2;
    EXPECT_EQ(first, copy);
    EXPECT_EQ(first + 2, 2 + first);
    auto range = std::equal_range(first, last, uint32_t{0x80000000});
    EXPECT_EQ(1, range.first - first);
    EXPECT_EQ(2, range.second - range.first);
    EXPECT_EQ(first + 3, std::find(first, last, uint32_t{0xffffffff}));
    EXPECT_EQ(first + 3, std::lower_bound(first, last, uint32_t{0xffffffff}));
    numeric_iterator singular;
    EXPECT_EQ(singular, numeric_iterator{});
    singular = first;
    EXPECT_EQ(first, singular);
    auto end = last.position();
    EXPECT_FALSE(end);
    mdToken token;
    ASSERT_TRUE(end.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_ENCLog) | 5, token);
}

TEST_F(CppColumnSearch, LinearSuffixSearchKeepsAbsoluteRows)
{
    ASSERT_NO_FATAL_FAILURE(Fill(ENCLog_Token, {9, 4, 0xffffffff, 4}));
    auto first = dnmd::md_create_cursor<mdtid_ENCLog>(handle.get());
    ASSERT_TRUE(dnmd::md_cursor_move(first, 2));
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), ENCLog_Token, 4), 4));
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), ENCLog_Token, 0xffffffff), 3));
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(first.as(), ENCLog_Token, 9));
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(first.as(), ENCLog_Token, 100));
}

TEST_F(CppColumnSearch, NarrowConstantKeysAreNotTruncated)
{
    ASSERT_NO_FATAL_FAILURE(Fill(ClassLayout_PackingSize, {1, 5, 3}));
    auto first = dnmd::md_create_cursor<mdtid_ClassLayout>(handle.get());
    dnmd::detail::CursorTable(first)->is_sorted = true;
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), ClassLayout_PackingSize, 3), 3));
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(first.as(), ClassLayout_PackingSize, 0x10001));
    uint32_t count = 0x12345678;
    auto start = first;
    EXPECT_EQ(dnmd::range_result::not_supported, dnmd::md_find_range_from_cursor(first.as(), ClassLayout_PackingSize, 1, start, &count));
    EXPECT_EQ(0x12345678u, count);
    EXPECT_EQ(static_cast<mdcursor_t>(first)._reserved2, static_cast<mdcursor_t>(start)._reserved2);
}

TEST_F(CppColumnSearch, SecondaryKeysDoNotUseBinarySearch)
{
    ASSERT_NO_FATAL_FAILURE(Fill(GenericParam_Number, {0, 7, 1, 9}));
    auto first = dnmd::md_create_cursor<mdtid_GenericParam>(handle.get());
    auto row = first;
    for (uint32_t i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(row.as().set_column_value(GenericParam_Owner, CreateTokenType(mdtid_TypeDef) | (i / 2 + 1)));
        ASSERT_TRUE(dnmd::md_cursor_next(row));
    }
    dnmd::detail::CursorTable(first)->is_sorted = true;
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), GenericParam_Number, 7), 2));
    dnmd::raw_cursor<mdtid_GenericParam> range;
    uint32_t count = 99;
    EXPECT_EQ(dnmd::range_result::not_supported, dnmd::md_find_range_from_cursor(first.as(), GenericParam_Number, 7, range, &count));
    EXPECT_EQ(99u, count);
}

TEST_F(CppColumnSearch, SortedPrimaryRangesRespectTheSuffix)
{
    ASSERT_NO_FATAL_FAILURE(Fill(ClassLayout_Parent, {CreateTokenType(mdtid_TypeDef) | 1,
        CreateTokenType(mdtid_TypeDef) | 2, CreateTokenType(mdtid_TypeDef) | 2,
        CreateTokenType(mdtid_TypeDef) | 2, CreateTokenType(mdtid_TypeDef) | 4}));
    auto first = dnmd::md_create_cursor<mdtid_ClassLayout>(handle.get());
    dnmd::detail::CursorTable(first)->is_sorted = true;
    dnmd::raw_cursor<mdtid_ClassLayout> start;
    uint32_t count;
    ASSERT_EQ(dnmd::range_result::found, dnmd::md_find_range_from_cursor(first.as(), ClassLayout_Parent,
        CreateTokenType(mdtid_TypeDef) | 2, start, &count));
    EXPECT_EQ(3u, count);
    ASSERT_NO_FATAL_FAILURE(ExpectRow(start, 2));
    ASSERT_TRUE(dnmd::md_cursor_move(first, 2));
    ASSERT_EQ(dnmd::range_result::found, dnmd::md_find_range_from_cursor(first.as(), ClassLayout_Parent,
        CreateTokenType(mdtid_TypeDef) | 2, start, &count));
    EXPECT_EQ(2u, count);
    ASSERT_NO_FATAL_FAILURE(ExpectRow(start, 3));
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), ClassLayout_Parent,
        CreateTokenType(mdtid_TypeDef) | 4), 5));
    count = 0x12345678;
    auto before = start;
    EXPECT_EQ(dnmd::range_result::not_found, dnmd::md_find_range_from_cursor(first.as(), ClassLayout_Parent,
        CreateTokenType(mdtid_TypeDef) | 3, start, &count));
    EXPECT_EQ(0x12345678u, count);
    EXPECT_EQ(static_cast<mdcursor_t>(before)._reserved2, static_cast<mdcursor_t>(start)._reserved2);
    EXPECT_EQ(dnmd::range_result::not_found, dnmd::md_find_range_from_cursor(first.as(), ClassLayout_Parent,
        CreateTokenType(mdtid_MethodDef) | 2, start, &count));
}

TEST_F(CppColumnSearch, EmptyTypedRangeKeyPreservesOutputs)
{
    ASSERT_NO_FATAL_FAILURE(Fill(ClassLayout_Parent, {CreateTokenType(mdtid_TypeDef) | 1}));
    auto first = dnmd::md_create_cursor<mdtid_ClassLayout>(handle.get());
    dnmd::detail::CursorTable(first)->is_sorted = true;
    dnmd::raw_cursor<mdtid_TypeDef> missing;
    auto start = first;
    uint32_t count = 0x12345678;
    EXPECT_EQ(dnmd::range_result::not_found, dnmd::md_find_range_from_cursor(
        first.as(), ClassLayout_Parent, missing, start, &count));
    EXPECT_EQ(0x12345678u, count);
    EXPECT_EQ(static_cast<mdcursor_t>(first)._reserved1, static_cast<mdcursor_t>(start)._reserved1);
    EXPECT_EQ(static_cast<mdcursor_t>(first)._reserved2, static_cast<mdcursor_t>(start)._reserved2);
}

TEST_F(CppColumnSearch, CodedIndexSearchUsesStoredOrdering)
{
    ASSERT_NO_FATAL_FAILURE(Fill(CustomAttribute_Parent, {CreateTokenType(mdtid_MethodDef) | 1,
        CreateTokenType(mdtid_Field) | 1, CreateTokenType(mdtid_TypeDef) | 1,
        CreateTokenType(mdtid_MethodDef) | 2, CreateTokenType(mdtid_MethodDef) | 2,
        CreateTokenType(mdtid_TypeRef) | 2}));
    auto first = dnmd::md_create_cursor<mdtid_CustomAttribute>(handle.get());
    dnmd::detail::CursorTable(first)->is_sorted = true;
    dnmd::column_iterator iterator{first.as(), CustomAttribute_Parent};
    EXPECT_EQ(32u, iterator[0]);
    EXPECT_EQ(33u, iterator[1]);
    EXPECT_EQ(35u, iterator[2]);
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), CustomAttribute_Parent,
        CreateTokenType(mdtid_Field) | 1), 2));
    dnmd::raw_cursor<mdtid_CustomAttribute> range;
    uint32_t count;
    ASSERT_EQ(dnmd::range_result::found, dnmd::md_find_range_from_cursor(first.as(), CustomAttribute_Parent,
        CreateTokenType(mdtid_MethodDef) | 2, range, &count));
    EXPECT_EQ(2u, count);
    ASSERT_NO_FATAL_FAILURE(ExpectRow(range, 4));
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(first.as(), CustomAttribute_Parent, 0xff000001));
}

TEST_F(CppColumnSearch, PendingRowsDisableBinaryRangeSearch)
{
    ASSERT_NO_FATAL_FAILURE(Fill(ClassLayout_Parent, {CreateTokenType(mdtid_TypeDef) | 1,
        CreateTokenType(mdtid_TypeDef) | 4}));
    mdcursor_t added;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_ClassLayout, &added));
    ASSERT_TRUE((dnmd::raw_cursor<mdtid_ClassLayout>{added}.as().set_column_value(
        ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | 2)));
    auto first = dnmd::md_create_cursor<mdtid_ClassLayout>(handle.get());
    dnmd::detail::CursorTable(first)->is_sorted = true;
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), ClassLayout_Parent,
        CreateTokenType(mdtid_TypeDef) | 2), 3));
    uint32_t count = 99;
    auto output = first;
    EXPECT_EQ(dnmd::range_result::not_supported, dnmd::md_find_range_from_cursor(first.as(), ClassLayout_Parent,
        CreateTokenType(mdtid_TypeDef) | 2, output, &count));
    EXPECT_EQ(99u, count);
    md_commit_row_add(added);
}

TEST_F(CppColumnSearch, ReservedReferenceTagsDoNotMasqueradeAsNil)
{
    (void)AppendRow<mdtid_CustomAttribute>();
    ASSERT_NO_FATAL_FAILURE(Fill(CustomAttribute_Type, {CreateTokenType(mdtid_MethodDef) | 3}));
    auto first = dnmd::md_create_cursor<mdtid_CustomAttribute>(handle.get());
    dnmd::detail::CursorTable(first)->is_sorted = true;
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), CustomAttribute_Type,
        CreateTokenType(mdtid_MethodDef) | 3), 2));
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(first.as(), CustomAttribute_Type, CreateTokenType(mdtid_MethodDef)));
    EXPECT_FALSE(dnmd::md_find_row_from_cursor(first.as(), CustomAttribute_Type, 0xff000001));
}

TEST_F(CppColumnSearch, TypedKeysIncludeNilAndEndPositions)
{
    auto target = GetRow<mdtid_TypeDef>();
    mdToken target_token;
    ASSERT_TRUE(target.get_token(target_token));
    ASSERT_NO_FATAL_FAILURE(Fill(ClassLayout_Parent, {CreateTokenType(mdtid_TypeDef), target_token, target_token + 1}));
    auto first = dnmd::md_create_cursor<mdtid_ClassLayout>(handle.get());
    dnmd::detail::CursorTable(first)->is_sorted = true;
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), ClassLayout_Parent, target), 2));
    dnmd::raw_cursor<mdtid_TypeDef> position{target};
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), ClassLayout_Parent, position), 2));
    auto nil_row = first.as();
    dnmd::raw_cursor<mdtid_TypeDef> nil;
    ASSERT_TRUE(nil_row.get_column_value(ClassLayout_Parent, nil));
    EXPECT_FALSE(nil);
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), ClassLayout_Parent, nil), 1));
    ASSERT_TRUE(dnmd::md_cursor_next(position));
    EXPECT_FALSE(position);
    ASSERT_NO_FATAL_FAILURE(ExpectRow(dnmd::md_find_row_from_cursor(first.as(), ClassLayout_Parent, position), 3));
    dnmd::raw_cursor<mdtid_ClassLayout> range;
    uint32_t count;
    ASSERT_EQ(dnmd::range_result::found, dnmd::md_find_range_from_cursor(first.as(), ClassLayout_Parent, target, range, &count));
    EXPECT_EQ(1u, count);
}

TEST_F(CppColumnSearch, PrimaryKeyTraitsMatchTheCoreSchema)
{
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(Constant_Parent, CreateTokenType(mdtid_Field)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(CustomAttribute_Parent, CreateTokenType(mdtid_MethodDef)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(DeclSecurity_Parent, CreateTokenType(mdtid_TypeDef)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(FieldLayout_Field, CreateTokenType(mdtid_Field)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(FieldMarshal_Parent, CreateTokenType(mdtid_Field)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(FieldRva_Field, CreateTokenType(mdtid_Field)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(GenericParam_Owner, CreateTokenType(mdtid_TypeDef)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(GenericParamConstraint_Owner, CreateTokenType(mdtid_GenericParam)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(ImplMap_MemberForwarded, CreateTokenType(mdtid_Field)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(InterfaceImpl_Class, CreateTokenType(mdtid_TypeDef)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(MethodImpl_Class, CreateTokenType(mdtid_TypeDef)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(MethodSemantics_Association, CreateTokenType(mdtid_Event)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(NestedClass_NestedClass, CreateTokenType(mdtid_TypeDef)));
#ifdef DNMD_PORTABLE_PDB
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(LocalScope_Method, CreateTokenType(mdtid_MethodDef)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(StateMachineMethod_MoveNextMethod, CreateTokenType(mdtid_MethodDef)));
    ASSERT_NO_FATAL_FAILURE(CheckPrimary(CustomDebugInformation_Parent, CreateTokenType(mdtid_MethodDef)));
#endif
}

TEST_F(CppColumnSearch, ParentLookupsCoverAllPointerFamiliesAndReloads)
{
    ASSERT_NO_FATAL_FAILURE(CheckParents(TypeDef_FieldList));
    ASSERT_NO_FATAL_FAILURE(CheckParents(TypeDef_MethodList));
    ASSERT_NO_FATAL_FAILURE(CheckParents(MethodDef_ParamList));
    ASSERT_NO_FATAL_FAILURE(CheckParents(EventMap_EventList));
    ASSERT_NO_FATAL_FAILURE(CheckParents(PropertyMap_PropertyList));
}

TEST_F(CppColumnSearch, MissingPointerTargetsLeaveTheParentOutputUnchanged)
{
    auto owner = GetRow<mdtid_TypeDef>();
    mdcursor_t first;
    mdcursor_t child;
    ASSERT_TRUE(md_add_new_row_to_list(owner, TypeDef_FieldList.column_index, &first));
    md_commit_row_add(first);
    auto other = AppendRow<mdtid_TypeDef>();
    ASSERT_TRUE(md_add_new_row_to_list(other, TypeDef_FieldList.column_index, &child));
    md_commit_row_add(child);
    ASSERT_TRUE(md_add_new_row_to_list(owner, TypeDef_FieldList.column_index, &child));
    md_commit_row_add(child);
    uint32_t count;
    auto pointers = dnmd::md_create_cursor<mdtid_FieldPtr>(handle.get(), &count);
    ASSERT_TRUE(pointers);
    for (uint32_t i = 0; i < count; ++i, dnmd::md_cursor_next(pointers))
        ASSERT_TRUE(pointers.as().set_column_value(dnmd::detail::pointer_column<mdtid_FieldPtr>(), CreateTokenType(mdtid_Field)));
    mdToken token = 0x12345678;
    auto element = dnmd::raw_cursor<mdtid_Field>{first}.as();
    EXPECT_FALSE(element.get_parent_token(token));
    EXPECT_EQ(0x12345678u, token);
    dnmd::raw_cursor<mdtid_TypeDef> parent{owner};
    mdcursor_t before = parent;
    EXPECT_FALSE(dnmd::md_find_cursor_of_range_element(element, parent));
    EXPECT_EQ(before._reserved1, static_cast<mdcursor_t>(parent)._reserved1);
    EXPECT_EQ(before._reserved2, static_cast<mdcursor_t>(parent)._reserved2);
}

TEST_F(CppColumnSearch, LoadedDirectParentsHandleNilGapsAndEmptyRuns)
{
    auto owner = GetRow<mdtid_TypeDef>();
    std::array<mdToken, 4> children;
    for (auto& token : children)
    {
        mdcursor_t child;
        ASSERT_TRUE(md_add_new_row_to_list(owner, TypeDef_FieldList.column_index, &child));
        md_commit_row_add(child);
        ASSERT_TRUE(md_cursor_to_token(child, &token));
    }
    (void)AppendRow<mdtid_TypeDef>();
    (void)AppendRow<mdtid_TypeDef>();
    std::array<std::array<uint32_t, 3>, 5> starts{{{1, 0, 3}, {1, 1, 3}, {1, 3, 0}, {0, 0, 0}, {3, 0, 4}}};
    std::array<std::array<uint32_t, 4>, 5> owners{{{1, 1, 3, 3}, {2, 2, 3, 3},
        {1, 1, 2, 2}, {0, 0, 0, 0}, {0, 0, 1, 3}}};
    for (size_t config = 0; config < starts.size(); ++config)
    {
        for (uint32_t i = 0; i < 3; ++i)
        {
            auto row = dnmd::md_token_to_cursor<mdtid_TypeDef>(handle.get(), CreateTokenType(mdtid_TypeDef) | (i + 1));
            ASSERT_TRUE(row);
            ASSERT_TRUE(row.as().set_column_value(TypeDef_FieldList, CreateTokenType(mdtid_Field) | starts[config][i]));
        }
        ASSERT_NO_FATAL_FAILURE(Reload());
        for (size_t i = 0; i < children.size(); ++i)
        {
            auto element = dnmd::md_token_to_cursor<mdtid_Field>(handle.get(), children[i]);
            ASSERT_TRUE(element);
            mdToken token = 0x12345678;
            bool found = element.as().get_parent_token(token);
            EXPECT_EQ(owners[config][i] != 0, found);
            EXPECT_EQ(found ? CreateTokenType(mdtid_TypeDef) | owners[config][i] : 0x12345678u, token);
        }
    }
}

TEST_F(CppColumnSearch, EventAndPropertyParentPositionsCanBeNil)
{
    auto type = GetRow<mdtid_TypeDef>();
    auto event_map = GetRow<mdtid_EventMap>();
    auto property_map = GetRow<mdtid_PropertyMap>();
    mdcursor_t event;
    mdcursor_t property;
    ASSERT_TRUE(md_add_new_row_to_list(event_map, EventMap_EventList.column_index, &event));
    md_commit_row_add(event);
    ASSERT_TRUE(md_add_new_row_to_list(property_map, PropertyMap_PropertyList.column_index, &property));
    md_commit_row_add(property);
    ASSERT_TRUE(event_map.set_column_value(EventMap_Parent, CreateTokenType(mdtid_TypeDef)));
    ASSERT_TRUE(property_map.set_column_value(PropertyMap_Parent, CreateTokenType(mdtid_TypeDef)));
    dnmd::raw_cursor<mdtid_TypeDef> parent;
    ASSERT_TRUE(dnmd::md_find_cursor_of_range_element(dnmd::raw_cursor<mdtid_Event>{event}.as(), parent));
    EXPECT_FALSE(parent);
    mdToken token;
    ASSERT_TRUE(dnmd::raw_cursor<mdtid_Property>{property}.as().get_parent_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef), token);
    auto end = dnmd::raw_cursor<mdtid_TypeDef>{type};
    ASSERT_TRUE(dnmd::md_cursor_next(end));
    ASSERT_TRUE(event_map.set_column_value(EventMap_Parent, end));
    ASSERT_TRUE(dnmd::raw_cursor<mdtid_Event>{event}.as().get_parent_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 2, token);
}

#ifdef DNMD_PORTABLE_PDB
TEST_F(CppColumnSearch, PortablePdbLocalRunsFindTheirScopes)
{
    auto scope = GetRow<mdtid_LocalScope>();
    mdcursor_t variable;
    mdcursor_t constant;
    ASSERT_TRUE(md_add_new_row_to_list(scope, LocalScope_VariableList.column_index, &variable));
    md_commit_row_add(variable);
    ASSERT_TRUE(md_add_new_row_to_list(scope, LocalScope_ConstantList.column_index, &constant));
    md_commit_row_add(constant);
    auto other = AppendRow<mdtid_LocalScope>();
    ASSERT_TRUE(other.set_column_value(LocalScope_VariableList, CreateTokenType(mdtid_LocalVariable) | 2));
    ASSERT_TRUE(other.set_column_value(LocalScope_ConstantList, CreateTokenType(mdtid_LocalConstant) | 2));
    ASSERT_TRUE(md_add_new_row_to_list(other, LocalScope_VariableList.column_index, &variable));
    md_commit_row_add(variable);
    ASSERT_TRUE(md_add_new_row_to_list(other, LocalScope_ConstantList.column_index, &constant));
    md_commit_row_add(constant);
    (void)AppendRow<mdtid_LocalScope>();
    ASSERT_NO_FATAL_FAILURE(Reload());
    auto read_variable = dnmd::md_token_to_cursor<mdtid_LocalVariable>(handle.get(), CreateTokenType(mdtid_LocalVariable) | 2);
    auto read_constant = dnmd::md_token_to_cursor<mdtid_LocalConstant>(handle.get(), CreateTokenType(mdtid_LocalConstant) | 2);
    ASSERT_TRUE(read_variable);
    ASSERT_TRUE(read_constant);
    mdToken token;
    ASSERT_TRUE(read_variable.as().get_parent_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_LocalScope) | 2, token);
    ASSERT_TRUE(read_constant.as().get_parent_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_LocalScope) | 2, token);
}
#endif
