#ifndef _SRC_INC_DNMD_WRITE_HPP_
#define _SRC_INC_DNMD_WRITE_HPP_

#include "dnmd_read.hpp"
#include "dnmd_schema.hpp"
#include <cstring>
#include <memory>

namespace dnmd
{

namespace detail
{
// Shared storage helpers.
extern "C"
{
uint8_t* get_writable_table_data(mdtable_t* table, bool make_writable);
uint32_t add_to_string_heap(::mdcxt__* context, char const* value);
uint32_t add_to_blob_heap(::mdcxt__* context, uint8_t const* value, uint32_t length);
uint32_t add_to_guid_heap(::mdcxt__* context, mdguid_t value);
uint32_t add_to_user_string_heap(::mdcxt__* context, char16_t const* value);
bool allocate_new_table(::mdcxt__* context, mdtable_id_t table);
bool initialize_new_table_details(::mdcxt__* context, mdtable_id_t id, mdtable_t* table);
bool insert_row_into_table(mdtable_t* table, uint32_t row, uint32_t* added_row);
extern mdguid_t const empty_guid;
#ifdef DNMD_PORTABLE_PDB
bool has_pdb(::mdcxt__* context);
bool update_referenced_type_system_table_row_count(::mdcxt__* context, mdtable_id_t table, uint32_t row);
#endif
}

// Indirection failures finish owned child/Ptr additions and clear both outputs.
// Other failures can still return an owned pending child row.
bool add_row_to_list(cursor<mdtid_TypeDef> owner, decltype(columns::TypeDef_FieldList) list,
    mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_TypeDef> owner, decltype(columns::TypeDef_FieldList) list,
    column<mdtid_Field, column_type_constant> sort, uint32_t value, mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_TypeDef> owner, decltype(columns::TypeDef_MethodList) list,
    mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_TypeDef> owner, decltype(columns::TypeDef_MethodList) list,
    column<mdtid_MethodDef, column_type_constant> sort, uint32_t value, mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_MethodDef> owner, decltype(columns::MethodDef_ParamList) list,
    mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_MethodDef> owner, decltype(columns::MethodDef_ParamList) list,
    column<mdtid_Param, column_type_constant> sort, uint32_t value, mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_EventMap> owner, decltype(columns::EventMap_EventList) list,
    mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_EventMap> owner, decltype(columns::EventMap_EventList) list,
    column<mdtid_Event, column_type_constant> sort, uint32_t value, mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_PropertyMap> owner, decltype(columns::PropertyMap_PropertyList) list,
    mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_PropertyMap> owner, decltype(columns::PropertyMap_PropertyList) list,
    column<mdtid_Property, column_type_constant> sort, uint32_t value, mdtable_t** added_table, uint32_t* added_row);
#ifdef DNMD_PORTABLE_PDB
bool add_row_to_list(cursor<mdtid_LocalScope> owner, decltype(columns::LocalScope_VariableList) list,
    mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_LocalScope> owner, decltype(columns::LocalScope_VariableList) list,
    column<mdtid_LocalVariable, column_type_constant> sort, uint32_t value, mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_LocalScope> owner, decltype(columns::LocalScope_ConstantList) list,
    mdtable_t** added_table, uint32_t* added_row);
bool add_row_to_list(cursor<mdtid_LocalScope> owner, decltype(columns::LocalScope_ConstantList) list,
    column<mdtid_LocalConstant, column_type_constant> sort, uint32_t value, mdtable_t** added_table, uint32_t* added_row);
#endif

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
    bool is_key = for_each_column<TableId>([col](auto... keys) {
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

    static void write_value(uint8_t* data, mdtcol_t details, uint32_t value) noexcept
    {
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
    }

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
        write_value(data, details, value);
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
template<mdtable_id_t Owner, typename Kind, mdtable_id_t... Targets>
inline bool initialize_list_column(cursor<Owner> owner, column<Owner, Kind> col,
    raw_cursor<Targets...> target_position) noexcept
{
    auto target = CursorTable(target_position);
    bool initialized = target->is_initialized;
    if (!initialized
        && !initialize_new_table_details(target->cxt, static_cast<mdtable_id_t>(target->table_id), target))
        return false;
    // Initialize list columns to nil for absent tables, otherwise one-past the end.
    auto end = raw_cursor<Targets...>::at(target->cxt, initialized ? target->row_count + 1 : 0,
        static_cast<mdtable_id_t>(target->table_id));
    return row_writer{owner}.write(col, end);
}

template<mdtable_id_t Owner, mdtable_id_t Direct, mdtable_id_t Ptr>
inline bool initialize_list_column(cursor<Owner> owner,
    column<Owner, indirect_cursor<Direct, Ptr>> col) noexcept
{
    auto source = CursorTable(owner);
    auto target = list_target_table<Direct, Ptr>(source);
    return initialize_list_column(owner, col, raw_cursor<Direct, Ptr>::at(source->cxt, 0, target));
}

template<mdtable_id_t Owner, mdtable_id_t Direct>
inline bool initialize_list_column(cursor<Owner> owner, column<Owner, raw_cursor<Direct>> col) noexcept
{
    return initialize_list_column(owner, col, raw_cursor<Direct>::nil(owner.get_handle()));
}

template<mdtable_id_t Table>
inline bool initialize_list_columns(cursor<Table> row) noexcept
{
    if constexpr (Table == mdtid_TypeDef || Table == mdtid_MethodDef
        || Table == mdtid_EventMap || Table == mdtid_PropertyMap)
    {
        return for_each_column<Table>([&](auto... columns) {
            return (initialize_list_column(row, columns) && ...);
        }, list_columns<Table>());
    }
    else
        return true;
}

template<mdtable_id_t Table>
inline bool append_row(mdcxt_t* context, uint32_t* row) noexcept
{
    // Append to the end of an already initialized table.
    auto& table = context->tables[Table];
    assert(table.is_initialized);
    if (!insert_row_into_table(&table, table.row_count + 1, row))
        return false;
    return initialize_list_columns(raw_cursor<Table>::at(context, *row).value());
}

template<mdtable_id_t Table>
inline bool append_row_to_table(mdcxt_t* context, uint32_t* row) noexcept
{
    static_assert(Table >= mdtid_First && Table < mdtid_End);
    if (row == nullptr)
        return false;
    *row = 0;
    if (context == nullptr)
        return false;
    if constexpr (pointer_table_id<Table>() != mdtid_Unused
#ifdef DNMD_PORTABLE_PDB
        || (Table > mdtid_GenericParamConstraint && Table < mdtid_FirstPdb)
#endif
        )
    {
        // Child rows must be added through their parent's list.
        return false;
    }
    else
    {
        auto& table = context->tables[Table];
        if (!table.is_initialized)
        {
            if constexpr (Table == mdtid_FieldPtr || Table == mdtid_MethodPtr
                || Table == mdtid_ParamPtr || Table == mdtid_EventPtr || Table == mdtid_PropertyPtr)
                return false;
            else if (!allocate_new_table(context, Table))
                return false;
        }
        return append_row<Table>(context, row);
    }
}

template<mdtable_id_t Table>
inline bool insert_row_relative(raw_cursor<Table> relative, bool after, uint32_t* row) noexcept
{
    if (row == nullptr)
        return false;
    *row = 0;
    auto table = CursorTable(relative);
    uint32_t index = CursorRow(relative);
    // We can't turn an insert into a "create table" operation.
    if (!table->is_initialized || index == 0 || index > 0x00ffffff)
        return false;
    if constexpr (pointer_table_id<Table>() != mdtid_Unused)
    {
        // Child tables must be edited through their parent's list.
        return false;
    }
    else
    {
        index += after ? 1 : 0;
        if (uint64_t{index} > uint64_t{table->row_count} + 1
            || !insert_row_into_table(table, index, row))
            return false;
        auto added = raw_cursor<Table>::at(table->cxt, *row).value();
        // At the end, initialize empty lists; in the middle, copy the next row's boundaries.
        if (*row == table->row_count)
            return initialize_list_columns(added);
        auto next = raw_cursor<Table>::at(table->cxt, *row + 1).value();
        return for_each_column<Table>([&](auto... columns) {
            auto copy = [&](auto col) {
                mdToken token;
                return row_reader{next}.read(col, token) && row_writer{added}.write(col, token);
            };
            (void)copy;
            return (copy(columns) && ...);
        }, list_columns<Table>());
    }
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

    static added_row finish_acquisition(mdtable_t& table, bool success, uint32_t row) noexcept
    {
        added_row result{table, row};
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
        auto& table = detail::context(handle).tables[TableId];
        uint32_t row = 0;
        bool success = detail::append_row_to_table<TableId>(table.cxt, &row);
        assert(success == (row != 0));
        return finish_acquisition(table, success, row);
    }

    static added_row insert(cursor<TableId> relative, bool after) noexcept
    {
        auto table = detail::CursorTable(relative);
        uint32_t row = 0;
        bool success = detail::insert_row_relative(raw_cursor<TableId>{relative}, after, &row);
        assert(success == (row != 0));
        return finish_acquisition(*table, success, row);
    }

    template<mdtable_id_t Owner, typename Kind>
    static added_row in_list(cursor<Owner> owner, column<Owner, Kind> list) noexcept
    {
        static_assert(std::is_same_v<Kind, raw_cursor<TableId>>
            || std::is_same_v<Kind, indirect_cursor<TableId, detail::pointer_table_id<TableId>()>>);
        auto& target = detail::context(owner.get_handle()).tables[TableId];
        uint32_t row = 0;
        mdtable_t* table = nullptr;
        bool success = detail::add_row_to_list(owner, list, &table, &row);
        assert(row == 0 || table == &target);
        return finish_acquisition(target, success, row);
    }

    template<mdtable_id_t Owner, typename Kind>
    static added_row in_list(cursor<Owner> owner, column<Owner, Kind> list,
        column<TableId, column_type_constant> sort, uint32_t value) noexcept
    {
        static_assert(std::is_same_v<Kind, raw_cursor<TableId>>
            || std::is_same_v<Kind, indirect_cursor<TableId, detail::pointer_table_id<TableId>()>>);
        auto& target = detail::context(owner.get_handle()).tables[TableId];
        uint32_t row = 0;
        mdtable_t* table = nullptr;
        bool success = detail::add_row_to_list(owner, list, sort, value, &table, &row);
        assert(row == 0 || table == &target);
        return finish_acquisition(target, success, row);
    }
};

template<mdtable_id_t Owner, mdtable_id_t Direct, mdtable_id_t Ptr>
inline added_row<Direct> md_add_new_row_to_list(cursor<Owner> owner,
    column<Owner, indirect_cursor<Direct, Ptr>> list) noexcept
{
    static_assert(Ptr == detail::pointer_table_id<Direct>());
    return added_row<Direct>::in_list(owner, list);
}
template<mdtable_id_t Owner, mdtable_id_t Direct, mdtable_id_t Ptr>
inline added_row<Direct> md_add_new_row_to_sorted_list(cursor<Owner> owner,
    column<Owner, indirect_cursor<Direct, Ptr>> list,
    column<Direct, column_type_constant> sort, uint32_t value) noexcept
{
    static_assert(Ptr == detail::pointer_table_id<Direct>());
    return added_row<Direct>::in_list(owner, list, sort, value);
}
} // namespace dnmd
#endif // _SRC_INC_DNMD_WRITE_HPP_
