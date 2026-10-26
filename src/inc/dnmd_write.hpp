#ifndef _SRC_INC_DNMD_WRITE_HPP_
#define _SRC_INC_DNMD_WRITE_HPP_

#include "dnmd_read.hpp"
#include "dnmd_edit.h"
#include <cstring>
#include <memory>

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
    if (index > 1 && !rows_sorted(raw_cursor<TableId>::at(table->cxt, index - 1).value(), row))
        return false;
    return index == table->row_count || rows_sorted(row, raw_cursor<TableId>::at(table->cxt, index + 1).value());
}

template<mdtable_id_t TableId, typename ColumnType>
inline void update_column_sort_order(cursor<TableId> row, column<TableId, ColumnType> col) noexcept
{
    auto table = CursorTable(row);
    if (!table->is_sorted || table->is_adding_new_row)
        return;
    bool is_key = std::apply([col](auto... keys) {
        return ((col.column_index == keys.column.column_index) || ...);
    }, table_sort_keys<TableId>());
    if (is_key)
        table->is_sorted = validate_row_sorted_within_table(raw_cursor<TableId>{row}.value());
}
}

template<mdtable_id_t TableId>
struct row_writer final
{
private:
    friend struct detail::row_storage_access;
    cursor<TableId> _cursor;

    template<bool TrackOrder, bool CheckWidth = false, typename ColumnType>
    bool write_raw(column<TableId, ColumnType> col, uint32_t value) const noexcept
    {
        auto table = detail::CursorTable(_cursor);
        auto data = detail::get_writable_table_data(table, true);
        if (data == nullptr)
            return false;

        // Heap growth can change both the row layout and its storage.
        auto details = table->column_details[static_cast<uint8_t>(col.column_index)];
        if constexpr (CheckWidth)
        {
            if (!(details & mdtc_b4) && value > UINT16_MAX)
                return false;
        }
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
    explicit row_writer(cursor<TableId> const& row) noexcept : _cursor{row} {}

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
        mdToken token;
        return value.get_token(token) && write(col, token);
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

namespace detail
{
template<mdtable_id_t Owner, mdtable_id_t Child>
constexpr bool list_column_matches(col_index_t col) noexcept
{
    if constexpr (Owner == mdtid_TypeDef && Child == mdtid_Field) return col == mdtTypeDef_FieldList;
    else if constexpr (Owner == mdtid_TypeDef && Child == mdtid_MethodDef) return col == mdtTypeDef_MethodList;
    else if constexpr (Owner == mdtid_MethodDef && Child == mdtid_Param) return col == mdtMethodDef_ParamList;
    else if constexpr (Owner == mdtid_EventMap && Child == mdtid_Event) return col == mdtEventMap_EventList;
    else if constexpr (Owner == mdtid_PropertyMap && Child == mdtid_Property) return col == mdtPropertyMap_PropertyList;
#ifdef DNMD_PORTABLE_PDB
    else if constexpr (Owner == mdtid_LocalScope && Child == mdtid_LocalVariable) return col == mdtLocalScope_VariableList;
    else if constexpr (Owner == mdtid_LocalScope && Child == mdtid_LocalConstant) return col == mdtLocalScope_ConstantList;
#endif
    else { (void)col; return false; }
}
}

template<mdtable_id_t TableId>
struct added_row final
{
private:
    friend struct detail::cursor_access;
    mdtable_t* _table;
    uint32_t _row;

    added_row(mdtable_t& table, uint32_t row) noexcept : _table{&table}, _row{row}
    {
        assert(table.table_id == TableId);
        assert(row == 0 || (row <= table.row_count && table.is_adding_new_row));
    }

    static added_row finish_acquisition(mdhandle_t handle, bool success, uint32_t row) noexcept
    {
        added_row result{detail::context(handle).tables[TableId], row};
        if (!success)
            result.commit();
        return result;
    }

public:
    added_row() = delete;
    added_row(added_row const&) = delete;
    added_row& operator=(added_row const&) = delete;
    added_row(added_row&& other) noexcept : _table{other._table}, _row{other._row} { other._row = 0; }
    added_row& operator=(added_row&& other) noexcept
    {
        if (this != std::addressof(other))
        {
            commit();
            _table = other._table;
            _row = other._row;
            other._row = 0;
        }
        return *this;
    }
    ~added_row() { commit(); }

    mdhandle_t get_handle() const noexcept { return _table->cxt; }
    raw_cursor<TableId> position() const noexcept { return raw_cursor<TableId>::at(get_handle(), _row); }
    explicit operator bool() const noexcept { return _row != 0; }
    cursor<TableId> value() const noexcept { assert(_row != 0); return position().value(); }
    bool get_token(mdToken& token) const noexcept { return position().get_token(token); }

    void commit() noexcept
    {
        if (_row == 0)
            return;
        assert(_table->is_adding_new_row);
        if (_table->is_sorted)
            _table->is_sorted = detail::validate_row_sorted_within_table(value());
        _table->is_adding_new_row = false;
        _row = 0;
    }

    static added_row append(mdhandle_t handle) noexcept
    {
        static_assert(TableId >= mdtid_First && TableId < mdtid_End);
        uint32_t row = 0;
        bool success = md_cpp_append_row(handle, TableId, &row);
        return finish_acquisition(handle, success, row);
    }

    static added_row insert(cursor<TableId> relative, bool after) noexcept
    {
        uint32_t row = 0;
        bool success = md_cpp_insert_row(detail::CursorTable(relative), detail::CursorRow(relative), after, &row);
        return finish_acquisition(relative.get_handle(), success, row);
    }

    template<mdtable_id_t Owner>
    static added_row in_list(cursor<Owner> owner, col_index_t list, bool sorted,
        col_index_t sort_column, uint32_t sort_value) noexcept
    {
        if (!detail::list_column_matches<Owner, TableId>(list))
            return finish_acquisition(owner.get_handle(), false, 0);
        uint32_t row = 0;
        mdtable_id_t table = TableId;
        bool success = md_cpp_add_list_row(detail::CursorTable(owner), detail::CursorRow(owner),
            list, sorted, sort_column, sort_value, &table, &row);
        assert(row == 0 || table == TableId);
        return finish_acquisition(owner.get_handle(), success, row);
    }
};

template<mdtable_id_t Owner, mdtable_id_t Direct, mdtable_id_t Ptr>
inline added_row<Direct> md_add_new_row_to_list(cursor<Owner> owner,
    column<Owner, indirect_cursor<Direct, Ptr>> list) noexcept
{
    static_assert(Ptr == detail::pointer_table_id<Direct>());
    return added_row<Direct>::in_list(owner, list.column_index, false, list.column_index, 0);
}
template<mdtable_id_t Owner, mdtable_id_t Direct, mdtable_id_t Ptr>
inline added_row<Direct> md_add_new_row_to_sorted_list(cursor<Owner> owner,
    column<Owner, indirect_cursor<Direct, Ptr>> list,
    column<Direct, column_type_constant> sort, uint32_t value) noexcept
{
    static_assert(Ptr == detail::pointer_table_id<Direct>());
    return added_row<Direct>::in_list(owner, list.column_index, true, sort.column_index, value);
}
} // namespace dnmd
#endif // _SRC_INC_DNMD_WRITE_HPP_
