#ifndef _SRC_INC_DNMD_SEARCH_HPP_
#define _SRC_INC_DNMD_SEARCH_HPP_

#include "dnmd_read.hpp"
#include <algorithm>
#include <iterator>

namespace dnmd
{

namespace detail
{
template<mdtable_id_t TableId, typename ColumnType>
inline bool sorted_search(mdtable_t const* table, column<TableId, ColumnType> col) noexcept
{
    constexpr auto keys = table_sort_keys<TableId>();
    if constexpr (std::tuple_size_v<decltype(keys)> == 0)
        return false;
    else
        return table->is_sorted && !table->is_adding_new_row && !std::get<0>(keys).descending
            && col.column_index == std::get<0>(keys).column.column_index;
}
}

namespace detail
{
template<typename ColumnType, mdtable_id_t... ValueTableIds>
struct search_reference_tables : std::false_type {};

template<mdtable_id_t... TargetTableIds, mdtable_id_t... ValueTableIds>
struct search_reference_tables<raw_cursor<TargetTableIds...>, ValueTableIds...>
    : std::conjunction<std::bool_constant<contains_table_id_v<ValueTableIds, TargetTableIds...>>...> {};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t... ValueTableIds>
struct search_reference_tables<indirect_cursor<DirectTableId, IndirectTableId>, ValueTableIds...>
    : std::conjunction<std::bool_constant<contains_table_id_v<ValueTableIds, DirectTableId, IndirectTableId>>...> {};

template<typename ColumnType, typename Value>
struct search_reference_value : std::false_type {};

template<typename ColumnType, mdtable_id_t ValueTableId>
struct search_reference_value<ColumnType, cursor<ValueTableId>> : search_reference_tables<ColumnType, ValueTableId> {};

template<typename ColumnType, mdtable_id_t... ValueTableIds>
struct search_reference_value<ColumnType, raw_cursor<ValueTableIds...>> : search_reference_tables<ColumnType, ValueTableIds...> {};

template<typename ColumnType, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct search_reference_value<ColumnType, indirect_cursor<DirectTableId, IndirectTableId>>
    : search_reference_tables<ColumnType, DirectTableId> {};

template<mdtable_id_t TableId, typename ColumnType, mdtable_id_t ValueTableId>
inline bool search_token(column<TableId, ColumnType>, cursor<ValueTableId> const& value, mdToken& token) noexcept
{
    token = CreateTokenType(ValueTableId) | CursorRow(value);
    return true;
}

template<mdtable_id_t TableId, typename ColumnType, mdtable_id_t... ValueTableIds>
inline bool search_token(column<TableId, ColumnType>, raw_cursor<ValueTableIds...> const& value, mdToken& token) noexcept
{
    return value.get_token(token);
}

template<mdtable_id_t TableId, typename ColumnType, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline bool search_token(column<TableId, ColumnType>, indirect_cursor<DirectTableId, IndirectTableId> const& value, mdToken& token) noexcept
{
    if constexpr (std::is_same_v<ColumnType, indirect_cursor<DirectTableId, IndirectTableId>>)
        return value.get_token(token);
    else
        return value.get_target_token(token);
}

template<mdtable_id_t TableId>
inline bool search_value(mdtable_t const*, column<TableId, column_type_constant>, uint32_t value, uint32_t& raw) noexcept
{
    raw = value;
    return true;
}

template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds>
inline bool search_value(mdtable_t const*, column<TableId, raw_cursor<TargetTableIds...>>, mdToken value, uint32_t& raw) noexcept
{
    return column_reference_codec<TargetTableIds...>::encode(value, raw);
}

template<mdtable_id_t TableId, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline bool search_value(mdtable_t const* table,
    column<TableId, indirect_cursor<DirectTableId, IndirectTableId>>, mdToken value, uint32_t& raw) noexcept
{
    if (static_cast<mdtable_id_t>(ExtractTokenType(value)) != list_target_table<DirectTableId, IndirectTableId>(table))
        return false;
    raw = (RidFromToken)(value);
    return true;
}

template<mdtable_id_t TableId, typename ColumnType>
inline raw_cursor<TableId> find_row(cursor<TableId> begin, column<TableId, ColumnType> col, uint32_t value) noexcept
{
    column_iterator first{begin, col};
    auto last = first.end();
    auto found = sorted_search(CursorTable(begin), col)
        ? std::lower_bound(first, last, value) : std::find(first, last, value);
    if (found == last || *found != value)
        return {};
    return found.position();
}
}

template<mdtable_id_t TableId, typename ColumnType,
    std::enable_if_t<detail::searchable_column<ColumnType>::value, int> = 0>
inline raw_cursor<TableId> md_find_row_from_cursor(cursor<TableId> begin, column<TableId, ColumnType> col, uint32_t value) noexcept
{
    uint32_t encoded;
    if (!detail::search_value(detail::CursorTable(begin), col, value, encoded))
        return {};
    return detail::find_row(begin, col, encoded);
}

template<mdtable_id_t TableId, typename ColumnType, typename Value,
    std::enable_if_t<detail::search_reference_value<ColumnType, Value>::value, int> = 0>
inline raw_cursor<TableId> md_find_row_from_cursor(cursor<TableId> begin, column<TableId, ColumnType> col,
    Value const& value) noexcept
{
    mdToken token;
    if (!detail::search_token(col, value, token))
        return {};
    return md_find_row_from_cursor(begin, col, token);
}

template<mdtable_id_t TableId, typename ColumnType,
    std::enable_if_t<detail::searchable_column<ColumnType>::value, int> = 0>
inline md_range_result_t md_find_range_from_cursor(cursor<TableId> begin,
    column<TableId, ColumnType> col, uint32_t value,
    raw_cursor<TableId>& start, uint32_t* count) noexcept
{
    assert(count != nullptr);
    uint32_t encoded;
    auto table = detail::CursorTable(begin);
    if (!detail::search_value(table, col, value, encoded))
        return MD_RANGE_NOT_FOUND;
    if (!detail::sorted_search(table, col))
        return MD_RANGE_NOT_SUPPORTED;
    column_iterator first{begin, col};
    auto range = std::equal_range(first, first.end(), encoded);
    if (range.first == range.second)
        return MD_RANGE_NOT_FOUND;
    start = range.first.position();
    *count = static_cast<uint32_t>(range.second - range.first);
    return MD_RANGE_FOUND;
}

template<mdtable_id_t TableId, typename ColumnType, typename Value,
    std::enable_if_t<detail::search_reference_value<ColumnType, Value>::value, int> = 0>
inline md_range_result_t md_find_range_from_cursor(cursor<TableId> begin, column<TableId, ColumnType> col,
    Value const& value, raw_cursor<TableId>& start, uint32_t* count) noexcept
{
    mdToken token;
    if (!detail::search_token(col, value, token))
        return MD_RANGE_NOT_FOUND;
    return md_find_range_from_cursor(begin, col, token, start, count);
}

namespace detail
{
template<mdtable_id_t ElementTableId>
constexpr auto parent_list_column() noexcept
{
    if constexpr (ElementTableId == mdtid_Field) return columns::TypeDef_FieldList;
    else if constexpr (ElementTableId == mdtid_MethodDef) return columns::TypeDef_MethodList;
    else if constexpr (ElementTableId == mdtid_Param) return columns::MethodDef_ParamList;
    else if constexpr (ElementTableId == mdtid_Event) return columns::EventMap_EventList;
    else if constexpr (ElementTableId == mdtid_Property) return columns::PropertyMap_PropertyList;
#ifdef DNMD_PORTABLE_PDB
    else if constexpr (ElementTableId == mdtid_LocalVariable) return columns::LocalScope_VariableList;
    else if constexpr (ElementTableId == mdtid_LocalConstant) return columns::LocalScope_ConstantList;
#endif
    else
        static_assert(parent_table_id<ElementTableId>() != mdtid_Unused, "The row must belong to a list.");
}

template<mdtable_id_t ElementTableId, mdtable_id_t OwnerTableId, typename ColumnType>
inline raw_cursor<OwnerTableId> find_list_owner(cursor<ElementTableId> element,
    column<OwnerTableId, ColumnType> col) noexcept
{
    auto element_table = CursorTable(element);
    auto owner_table = &element_table->cxt->tables[OwnerTableId];
    if (owner_table->row_count == 0)
        return {};

    uint32_t index = CursorRow(element);
    constexpr auto pointer_id = pointer_table_id<ElementTableId>();
    if constexpr (pointer_id != mdtid_Unused)
    {
        if (element_table->has_indirect_table)
        {
            auto pointer_table = &element_table->cxt->tables[pointer_id];
            if (pointer_table->row_count == 0)
                return {};
            auto pointer_begin = raw_cursor<pointer_id>{CreateCursor(pointer_table, 1)}.as();
            auto pointer = md_find_row_from_cursor(pointer_begin, pointer_column<pointer_id>(), element);
            if (!pointer)
                return {};
            index = CursorRow(pointer);
        }
    }

    auto owner_begin = raw_cursor<OwnerTableId>{CreateCursor(owner_table, 1)}.as();
    column_iterator first{owner_begin, col};
    auto last = first.end();
    auto found = last;
    if (element_table->cxt->editor != nullptr || element_table->has_indirect_table)
    {
        // Edited/indirected runs can contain unpartitioned starts; Ptr lookup is already linear.
        auto reverse = std::find_if(std::make_reverse_iterator(last), std::make_reverse_iterator(first),
            [index](uint32_t start) { return start != 0 && start <= index; });
        if (reverse == std::make_reverse_iterator(first))
            return {};
        found = reverse.base() - 1;
    }
    else
    {
        column_iterator<OwnerTableId, ColumnType, list_boundary_value> ordered{owner_begin, col};
        auto after = std::upper_bound(ordered, ordered.end(), index);
        if (after == ordered)
            return {};
        found = column_iterator{(--after).position().as(), col};
    }

    auto next = std::find_if(found + 1, last, [](uint32_t start) { return start != 0; });
    uint32_t end_row = list_end_row(owner_table, col);
    uint32_t end = next == last ? end_row : *next;
    if (*found == 0 || *found > index || end > end_row || index >= end)
        return {};
    return found.position();
}
}

template<mdtable_id_t ElementTableId, mdtable_id_t ParentTableId>
inline bool md_find_cursor_of_range_element(cursor<ElementTableId> element, raw_cursor<ParentTableId>& parent) noexcept
{
    static_assert(detail::parent_table_id<ElementTableId>() != mdtid_Unused, "The row must belong to a list.");
    static_assert(ParentTableId == detail::parent_table_id<ElementTableId>(), "The parent table type is incorrect.");
    auto owner = detail::find_list_owner(element, detail::parent_list_column<ElementTableId>());
    if (!owner)
        return false;
    if constexpr (ElementTableId == mdtid_Event)
        return row_reader{owner.as()}.read(columns::EventMap_Parent, parent);
    else if constexpr (ElementTableId == mdtid_Property)
        return row_reader{owner.as()}.read(columns::PropertyMap_Parent, parent);
    else
    {
        parent = owner;
        return true;
    }
}

template<mdtable_id_t TableId>
inline bool md_find_token_of_range_element(cursor<TableId> element, mdToken& token) noexcept
{
    static_assert(detail::parent_table_id<TableId>() != mdtid_Unused, "The row must belong to a list.");
    raw_cursor<detail::parent_table_id<TableId>()> parent;
    return md_find_cursor_of_range_element(element, parent) && parent.get_token(token);
}

template<mdtable_id_t TableId>
inline bool cursor<TableId>::get_parent_token(mdToken& tk) const noexcept
{
    static_assert(TableId == mdtid_Field || TableId == mdtid_MethodDef
        || TableId == mdtid_Param || TableId == mdtid_Event
#ifdef DNMD_PORTABLE_PDB
        || TableId == mdtid_LocalVariable || TableId == mdtid_LocalConstant
#endif
        || TableId == mdtid_Property,
        "The row must belong to a list.");
    return dnmd::md_find_token_of_range_element(*this, tk);
}

} // namespace dnmd

#endif // _SRC_INC_DNMD_SEARCH_HPP_
