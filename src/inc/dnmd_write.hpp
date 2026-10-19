#ifndef _SRC_INC_DNMD_WRITE_HPP_
#define _SRC_INC_DNMD_WRITE_HPP_

#include "dnmd_read.hpp"
#include <cstring>

namespace dnmd
{

namespace detail
{
// Shared core helpers while its implementation remains C.
extern "C"
{
uint8_t* get_writable_table_data(mdtable_t* table, bool make_writable);
uint32_t add_to_string_heap(::mdcxt__* context, char const* value);
uint32_t add_to_blob_heap(::mdcxt__* context, uint8_t const* value, uint32_t length);
uint32_t add_to_guid_heap(::mdcxt__* context, mdguid_t value);
uint32_t add_to_user_string_heap(::mdcxt__* context, char16_t const* value);
extern mdguid_t const empty_guid;
#ifdef DNMD_PORTABLE_PDB
bool has_pdb(::mdcxt__* context);
bool update_referenced_type_system_table_row_count(::mdcxt__* context, mdtable_id_t table, uint32_t row);
#endif
}

template<size_t Key = 0, mdtable_id_t TableId>
inline bool rows_sorted(cursor<TableId> first, cursor<TableId> second) noexcept
{
    constexpr auto keys = table_sort_keys<TableId>();
    if constexpr (Key == std::tuple_size_v<decltype(keys)>)
        return true;
    else
    {
        constexpr auto key = std::get<Key>(keys);
        uint32_t first_value = *column_iterator{first, key.column};
        uint32_t second_value = *column_iterator{second, key.column};
        if (first_value == second_value)
            return rows_sorted<Key + 1>(first, second);
        return key.descending ? first_value > second_value : first_value < second_value;
    }
}

template<mdtable_id_t TableId>
inline bool validate_row_sorted_within_table(cursor<TableId> row) noexcept
{
    auto table = CursorTable(row);
    uint32_t index = CursorRow(row);
    if (index > 1 && !rows_sorted(raw_cursor<TableId>{CreateCursor(table, index - 1)}.as(), row))
        return false;
    return index == table->row_count || rows_sorted(row, raw_cursor<TableId>{CreateCursor(table, index + 1)}.as());
}

template<mdtable_id_t TableId, typename ColumnType>
inline void update_column_sort_order(mdcursor_t row, column<TableId, ColumnType> col) noexcept
{
    auto table = CursorTable(row);
    if (!table->is_sorted || table->is_adding_new_row)
        return;
    bool is_key = std::apply([col](auto... keys) {
        return ((col.column_index == keys.column.column_index) || ...);
    }, table_sort_keys<TableId>());
    if (is_key)
        table->is_sorted = validate_row_sorted_within_table(raw_cursor<TableId>{row}.as());
}
}

template<mdtable_id_t TableId>
struct row_writer final
{
private:
    mdcursor_t _cursor;

    template<bool TrackOrder, typename ColumnType>
    bool write_raw(column<TableId, ColumnType> col, uint32_t value) const noexcept
    {
        auto table = detail::CursorTable(_cursor);
        auto data = detail::get_writable_table_data(table, true);
        if (data == nullptr)
            return false;

        // Heap growth can change both the row layout and its storage.
        auto details = table->column_details[static_cast<uint8_t>(col.column_index)];
        data += size_t{detail::CursorRow(_cursor) - 1} * table->row_size_bytes
            + ((static_cast<uint32_t>(details) & mdtc_comask) >> 8);
#ifdef _MSC_VER
        if (details & mdtc_b4)
            std::memcpy(data, &value, sizeof(value));
        else
        {
            auto short_value = static_cast<uint16_t>(value);
            std::memcpy(data, &short_value, sizeof(short_value));
        }
#else
        data[0] = static_cast<uint8_t>(value);
        data[1] = static_cast<uint8_t>(value >> 8);
        if (details & mdtc_b4)
        {
            data[2] = static_cast<uint8_t>(value >> 16);
            data[3] = static_cast<uint8_t>(value >> 24);
        }
#endif
        if constexpr (TrackOrder)
            detail::update_column_sort_order(_cursor, col);
        return true;
    }

    template<typename ColumnType>
    bool write_reference(column<TableId, ColumnType> col, mdToken token, uint32_t encoded) const noexcept
    {
#ifdef DNMD_PORTABLE_PDB
        auto context = detail::CursorTable(_cursor)->cxt;
        auto table = static_cast<mdtable_id_t>(ExtractTokenType(token));
        if (table < mdtid_FirstPdb && detail::has_pdb(context)
            && !detail::update_referenced_type_system_table_row_count(context, table, (detail::RidFromToken)(token)))
            return false;
#else
        (void)token;
#endif
        return write_raw<true>(col, encoded);
    }

public:
    explicit row_writer(cursor<TableId> const& row) noexcept : _cursor{row._cursor} {}

    bool write(column<TableId, column_type_constant> col, uint32_t value) const noexcept
    {
        return write_raw<true>(col, value);
    }

    bool write(column<TableId, column_type_string> col, char const* value) const noexcept
    {
        uint32_t offset = detail::add_to_string_heap(detail::CursorTable(_cursor)->cxt, value);
        return (offset != 0 || value[0] == '\0') && write_raw<false>(col, offset);
    }

    bool write(column<TableId, column_type_blob> col, uint8_t const* value, uint32_t length) const noexcept
    {
        uint32_t offset = detail::add_to_blob_heap(detail::CursorTable(_cursor)->cxt, value, length);
        return (offset != 0 || length == 0) && write_raw<false>(col, offset);
    }

    bool write(column<TableId, column_type_guid> col, mdguid_t value) const noexcept
    {
        uint32_t index = detail::add_to_guid_heap(detail::CursorTable(_cursor)->cxt, value);
        return (index != 0 || std::memcmp(&value, &detail::empty_guid, sizeof(value)) == 0)
            && write_raw<false>(col, index);
    }

    bool write(column<TableId, column_type_userstring> col, char16_t const* value) const noexcept
    {
        uint32_t offset = detail::add_to_user_string_heap(detail::CursorTable(_cursor)->cxt, value);
        return (offset != 0 || value[0] == 0) && write_raw<false>(col, offset);
    }

    template<mdtable_id_t... TargetTableIds>
    bool write(column<TableId, raw_cursor<TargetTableIds...>> col, mdToken value) const noexcept
    {
        uint32_t encoded;
        return detail::column_reference_codec<TargetTableIds...>::encode(value, encoded)
            && write_reference(col, value, encoded);
    }

    template<mdtable_id_t... TargetTableIds, mdtable_id_t ValueTableId,
        std::enable_if_t<detail::contains_table_id_v<ValueTableId, TargetTableIds...>, int> = 0>
    bool write(column<TableId, raw_cursor<TargetTableIds...>> col, cursor<ValueTableId> value) const noexcept
    {
        return write(col, CreateTokenType(ValueTableId) | detail::CursorRow(value));
    }

    template<mdtable_id_t... TargetTableIds, mdtable_id_t... ValueTableIds,
        std::enable_if_t<std::conjunction_v<
            std::bool_constant<detail::contains_table_id_v<ValueTableIds, TargetTableIds...>>...>, int> = 0>
    bool write(column<TableId, raw_cursor<TargetTableIds...>> col, raw_cursor<ValueTableIds...> const& value) const noexcept
    {
        mdToken token;
        return value.get_token(token) && write(col, token);
    }

    template<mdtable_id_t... TargetTableIds, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId,
        std::enable_if_t<detail::contains_table_id_v<DirectTableId, TargetTableIds...>, int> = 0>
    bool write(column<TableId, raw_cursor<TargetTableIds...>> col, indirect_cursor<DirectTableId, IndirectTableId> const& value) const noexcept
    {
        mdToken token;
        return value.get_target_token(token) && write(col, token);
    }

    template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
    bool write(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> col, mdToken value) const noexcept
    {
        auto table = detail::CursorTable(_cursor);
        auto target = detail::list_target_table<DirectTableId, IndirectTableId>(table);
        if (static_cast<mdtable_id_t>(ExtractTokenType(value)) != target)
            return false;
        return write_reference(col, value, (detail::RidFromToken)(value));
    }

    template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t ValueTableId,
        std::enable_if_t<ValueTableId == DirectTableId || ValueTableId == IndirectTableId, int> = 0>
    bool write(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> col, cursor<ValueTableId> value) const noexcept
    {
        return write(col, CreateTokenType(ValueTableId) | detail::CursorRow(value));
    }

    template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t... ValueTableIds,
        std::enable_if_t<std::conjunction_v<
            std::bool_constant<detail::contains_table_id_v<ValueTableIds, DirectTableId, IndirectTableId>>...>, int> = 0>
    bool write(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> col, raw_cursor<ValueTableIds...> const& value) const noexcept
    {
        mdToken token;
        return value.get_token(token) && write(col, token);
    }

    template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
    bool write(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> col, indirect_cursor<DirectTableId, IndirectTableId> const& value) const noexcept
    {
        mdToken token;
        return value.get_token(token) && write(col, token);
    }
};

template<mdtable_id_t TableId>
inline bool cursor<TableId>::set_column_value(column<TableId, column_type_constant> column, uint32_t value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
inline bool cursor<TableId>::set_column_value(column<TableId, column_type_string> column, char const* value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
inline bool cursor<TableId>::set_column_value(column<TableId, column_type_blob> column, uint8_t const* value, uint32_t length) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value, length);
}

template<mdtable_id_t TableId>
inline bool cursor<TableId>::set_column_value(column<TableId, column_type_guid> column, mdguid_t value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
inline bool cursor<TableId>::set_column_value(column<TableId, column_type_userstring> column, char16_t const* value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
template<mdtable_id_t... TargetTableIds>
inline bool cursor<TableId>::set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, mdToken value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
template<mdtable_id_t... TargetTableIds, mdtable_id_t ValueTableId>
inline bool cursor<TableId>::set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, cursor<ValueTableId> value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
template<mdtable_id_t... TargetTableIds, mdtable_id_t... ValueTableIds>
inline bool cursor<TableId>::set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, raw_cursor<ValueTableIds...> const& value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
template<mdtable_id_t... TargetTableIds, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline bool cursor<TableId>::set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, indirect_cursor<DirectTableId, IndirectTableId> const& value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline bool cursor<TableId>::set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, mdToken value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t ValueTableId>
inline bool cursor<TableId>::set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, cursor<ValueTableId> value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t... ValueTableIds>
inline bool cursor<TableId>::set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, raw_cursor<ValueTableIds...> const& value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline bool cursor<TableId>::set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, indirect_cursor<DirectTableId, IndirectTableId> const& value) const noexcept
{
    return row_writer<TableId>{*this}.write(column, value);
}

template<mdtable_id_t TableId>
inline void md_commit_row_add(cursor<TableId> row) noexcept
{
    auto table = detail::CursorTable(row);
    assert(table->is_adding_new_row);
    if (table->is_sorted)
        table->is_sorted = detail::validate_row_sorted_within_table(row);
    table->is_adding_new_row = false;
}

namespace detail
{
inline void commit_row_add(mdcursor_t row) noexcept
{
    auto table = CursorTable(row);
    if (table == nullptr)
        return;

    switch (table->table_id)
    {
    case mdtid_ClassLayout: return dnmd::md_commit_row_add(raw_cursor<mdtid_ClassLayout>{row}.as());
    case mdtid_Constant: return dnmd::md_commit_row_add(raw_cursor<mdtid_Constant>{row}.as());
    case mdtid_CustomAttribute: return dnmd::md_commit_row_add(raw_cursor<mdtid_CustomAttribute>{row}.as());
    case mdtid_DeclSecurity: return dnmd::md_commit_row_add(raw_cursor<mdtid_DeclSecurity>{row}.as());
    case mdtid_FieldLayout: return dnmd::md_commit_row_add(raw_cursor<mdtid_FieldLayout>{row}.as());
    case mdtid_FieldMarshal: return dnmd::md_commit_row_add(raw_cursor<mdtid_FieldMarshal>{row}.as());
    case mdtid_FieldRva: return dnmd::md_commit_row_add(raw_cursor<mdtid_FieldRva>{row}.as());
    case mdtid_GenericParam: return dnmd::md_commit_row_add(raw_cursor<mdtid_GenericParam>{row}.as());
    case mdtid_GenericParamConstraint: return dnmd::md_commit_row_add(raw_cursor<mdtid_GenericParamConstraint>{row}.as());
    case mdtid_ImplMap: return dnmd::md_commit_row_add(raw_cursor<mdtid_ImplMap>{row}.as());
    case mdtid_InterfaceImpl: return dnmd::md_commit_row_add(raw_cursor<mdtid_InterfaceImpl>{row}.as());
    case mdtid_MethodImpl: return dnmd::md_commit_row_add(raw_cursor<mdtid_MethodImpl>{row}.as());
    case mdtid_MethodSemantics: return dnmd::md_commit_row_add(raw_cursor<mdtid_MethodSemantics>{row}.as());
    case mdtid_NestedClass: return dnmd::md_commit_row_add(raw_cursor<mdtid_NestedClass>{row}.as());
#ifdef DNMD_PORTABLE_PDB
    case mdtid_LocalScope: return dnmd::md_commit_row_add(raw_cursor<mdtid_LocalScope>{row}.as());
    case mdtid_StateMachineMethod: return dnmd::md_commit_row_add(raw_cursor<mdtid_StateMachineMethod>{row}.as());
    case mdtid_CustomDebugInformation: return dnmd::md_commit_row_add(raw_cursor<mdtid_CustomDebugInformation>{row}.as());
#endif
    default:
        assert(table->is_adding_new_row);
        table->is_adding_new_row = false;
    }
}
}

} // namespace dnmd

#endif // _SRC_INC_DNMD_WRITE_HPP_
