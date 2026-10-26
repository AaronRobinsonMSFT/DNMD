#ifndef _SRC_INC_DNMD_READ_HPP_
#define _SRC_INC_DNMD_READ_HPP_

#include "dnmd_cursors.hpp"
#include <algorithm>
#include <cstring>
#include <iterator>

namespace dnmd
{

namespace detail
{
// Shared core helpers while its implementation remains C.
extern "C"
{
bool try_get_string(::mdcxt__* context, size_t offset, char const** value);
bool try_get_blob(::mdcxt__* context, size_t offset, uint8_t const** value, uint32_t* length);
bool try_get_guid(::mdcxt__* context, size_t index, mdguid_t* value);
bool try_get_user_string(::mdcxt__* context, size_t offset, mduserstring_t* value, size_t* next_offset);
}
}

template<mdtable_id_t TableId>
struct row_reader final
{
private:
    friend struct cursor<TableId>;
    friend struct detail::row_storage_access;
    template<mdtable_id_t, typename, typename>
    friend struct column_iterator;

    mdtable_t const* _table;
    uint8_t const* _data;

    row_reader(mdtable_t const& table, uint8_t const* data) noexcept : _table{&table}, _data{data} {}

    template<typename ColumnType>
    mdtcol_t column_details(column<TableId, ColumnType> col) const noexcept
    {
        return _table->column_details[static_cast<uint8_t>(col.column_index)];
    }

    template<typename ColumnType>
    uint32_t read_raw(column<TableId, ColumnType> col) const noexcept
    {
        auto details = column_details(col);
        auto data = _data + ((static_cast<uint32_t>(details) & mdtc_comask) >> 8);
#ifdef _MSC_VER
        // Match the core's unaligned little-endian load fast path on MSVC.
        if (details & mdtc_b4)
        {
            uint32_t value;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }
        uint16_t value;
        std::memcpy(&value, data, sizeof(value));
        return value;
#else
        uint32_t value = uint32_t{data[0]} | (uint32_t{data[1]} << 8);
        if (details & mdtc_b4)
            value |= (uint32_t{data[2]} << 16) | (uint32_t{data[3]} << 24);
        return value;
#endif
    }

    template<mdtable_id_t... TargetTableIds>
    bool read_position(column<TableId, raw_cursor<TargetTableIds...>> col,
        mdtable_t const*& target, uint32_t& row) const noexcept
    {
        mdtable_id_t table;
        if (!detail::column_reference_codec<TargetTableIds...>::decode(read_raw(col), table, row))
            return false;
        target = &_table->cxt->tables[table];
        return uint64_t{row} <= uint64_t{target->row_count} + 1;
    }

    template<mdtable_id_t Direct, mdtable_id_t Ptr>
    bool read_position(column<TableId, indirect_cursor<Direct, Ptr>> col,
        mdtable_t const*& target, uint32_t& row) const noexcept
    {
        auto table = detail::list_target_table<Direct, Ptr>(_table);
        target = &_table->cxt->tables[table];
        row = (detail::RidFromToken)(read_raw(col));
        return uint64_t{row} <= uint64_t{target->row_count} + 1;
    }

    void advance() noexcept
    {
        _data += _table->row_size_bytes;
    }

public:
    explicit row_reader(cursor<TableId> const& row) noexcept
        : _table{detail::CursorTable(row)}
        , _data{_table->data.ptr
            + size_t{detail::CursorRow(row) - 1} * _table->row_size_bytes}
    {
    }

    bool read(column<TableId, column_type_constant> col, uint32_t* value) const noexcept
    {
        *value = read_raw(col);
        return true;
    }

    template<mdtable_id_t... TargetTableIds>
    bool read(column<TableId, raw_cursor<TargetTableIds...>> col, mdToken& value) const noexcept
    {
        mdtable_id_t table;
        uint32_t row;
        if (!detail::column_reference_codec<TargetTableIds...>::decode(read_raw(col), table, row))
            return false;
        value = CreateTokenType(table) | row;
        return true;
    }

    template<mdtable_id_t... TargetTableIds>
    bool read(column<TableId, raw_cursor<TargetTableIds...>> col, raw_cursor<TargetTableIds...>& value) const noexcept
    {
        mdtable_t const* table;
        uint32_t row;
        if (!read_position(col, table, row))
            return false;
        value = raw_cursor<TargetTableIds...>::at(_table->cxt, row, static_cast<mdtable_id_t>(table->table_id));
        return true;
    }

    template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
    bool read(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> col, mdToken& value) const noexcept
    {
        auto table = detail::list_target_table<DirectTableId, IndirectTableId>(_table);
        value = CreateTokenType(table) | (detail::RidFromToken)(read_raw(col));
        return true;
    }

    template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
    bool read(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> col, indirect_cursor<DirectTableId, IndirectTableId>& value) const noexcept
    {
        mdtable_t const* table;
        uint32_t row;
        if (!read_position(col, table, row))
            return false;
        value = indirect_cursor<DirectTableId, IndirectTableId>::at(
            _table->cxt, row, static_cast<mdtable_id_t>(table->table_id));
        return true;
    }

    template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
    bool read(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> col,
        indirect_cursor<DirectTableId, IndirectTableId>& value, uint32_t* count) const noexcept
    {
        assert(count != nullptr);
        mdtable_t const* target;
        uint32_t first;
        if (!read_position(col, target, first))
            return false;
        uint32_t length = 0;
        if (first != 0 && first != target->row_count + 1)
        {
            length = target->row_count - first + 1;
            auto next = *this;
            auto end = _table->data.ptr + size_t{_table->row_count} * _table->row_size_bytes;
            for (next.advance(); next._data < end; next.advance())
            {
                mdtable_t const* next_table;
                uint32_t last;
                if (!next.read_position(col, next_table, last))
                    return false;
                assert(next_table == target);
                if (last == 0)
                    continue;
                if (last < first)
                    return false;
                length = last - first;
                break;
            }
        }
        value = indirect_cursor<DirectTableId, IndirectTableId>::at(_table->cxt, first, static_cast<mdtable_id_t>(target->table_id));
        *count = length;
        return true;
    }

    template<mdtable_id_t Target>
    bool read(column<TableId, raw_cursor<Target>> col, raw_cursor<Target>& value, uint32_t* count) const noexcept
    {
        mdtable_t const* target;
        uint32_t first;
        if (!read_position(col, target, first))
            return false;
        uint32_t length = 0;
        if (first != 0 && first != target->row_count + 1)
        {
            length = target->row_count - first + 1;
            auto next = *this;
            auto end = _table->data.ptr + size_t{_table->row_count} * _table->row_size_bytes;
            for (next.advance(); next._data < end; next.advance())
            {
                mdtable_t const* next_table;
                uint32_t last;
                if (!next.read_position(col, next_table, last))
                    return false;
                if (last == 0)
                    continue;
                if (last < first)
                    return false;
                length = last - first;
                break;
            }
        }
        value = raw_cursor<Target>::at(_table->cxt, first);
        *count = length;
        return true;
    }

    bool read(column<TableId, column_type_string> col, char const** value) const noexcept
    {
        return detail::try_get_string(_table->cxt, read_raw(col), value);
    }

    bool read(column<TableId, column_type_blob> col, uint8_t const** value, uint32_t* length) const noexcept
    {
        return detail::try_get_blob(_table->cxt, read_raw(col), value, length);
    }

    bool read(column<TableId, column_type_guid> col, mdguid_t* value) const noexcept
    {
        return detail::try_get_guid(_table->cxt, read_raw(col), value);
    }

    bool read(column<TableId, column_type_userstring> col, mduserstring_t* value) const noexcept
    {
        size_t next_offset;
        return detail::try_get_user_string(_table->cxt, read_raw(col), value, &next_offset);
    }
};

template<mdtable_id_t TableId, typename ColumnType, typename ValuePolicy>
struct column_iterator final
{
    static_assert(detail::searchable_column<ColumnType>::value,
        "Only constant, reference, and list columns support numeric iteration.");
    static_assert(std::is_same_v<ValuePolicy, detail::stored_column_value>
        || std::is_same_v<ValuePolicy, detail::list_boundary_value>);

private:
    template<mdtable_id_t, typename, typename>
    friend struct column_iterator;

    mdtable_t const* _table;
    uint8_t const* _data;
    column<TableId, ColumnType> _column;

    column_iterator(mdtable_t const* table, uint8_t const* data, column<TableId, ColumnType> col) noexcept
        : _table{table}, _data{data}, _column{col} {}

    void assert_same_column(column_iterator const& other) const noexcept
    {
        assert(_table == other._table && _column.column_index == other._column.column_index);
        (void)other;
    }

public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = uint32_t;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using pointer = void;

    column_iterator() noexcept : _table{nullptr}, _data{nullptr}, _column{static_cast<col_index_t>(0)} {}

    column_iterator(cursor<TableId> const& row, column<TableId, ColumnType> col) noexcept
        : _table{detail::CursorTable(row)},
          _data{_table->data.ptr + size_t{detail::CursorRow(row) - 1} * _table->row_size_bytes}, _column{col} {}

    value_type operator*() const noexcept
    {
        assert(_data >= _table->data.ptr);
        assert(_data < _table->data.ptr
            + size_t{_table->row_count} * _table->row_size_bytes);
        uint32_t value = row_reader<TableId>{*_table, _data}.read_raw(_column);
        if constexpr (std::is_same_v<ValuePolicy, detail::list_boundary_value>)
        {
            if (value == 0)
            {
                column_iterator<TableId, ColumnType> probe{_table, _data, _column};
                auto last = probe.end();
                auto next = std::find_if(++probe, last, [](uint32_t start) { return start != 0; });
                return next == last ? detail::list_end_row(_table, _column) : *next;
            }
        }
        return value;
    }

    column_iterator& operator++() noexcept { return *this += 1; }
    column_iterator operator++(int) noexcept { auto old = *this; ++*this; return old; }
    column_iterator& operator--() noexcept { return *this -= 1; }
    column_iterator operator--(int) noexcept { auto old = *this; --*this; return old; }

    column_iterator& operator+=(difference_type delta) noexcept
    {
#ifndef NDEBUG
        auto index = (_data - _table->data.ptr) / _table->row_size_bytes;
        assert(delta >= -index && delta <= static_cast<difference_type>(_table->row_count) - index);
#endif
        _data += delta * _table->row_size_bytes;
        return *this;
    }

    column_iterator& operator-=(difference_type delta) noexcept { return *this += -delta; }
    column_iterator operator+(difference_type delta) const noexcept { auto result = *this; return result += delta; }
    column_iterator operator-(difference_type delta) const noexcept { auto result = *this; return result -= delta; }
    friend column_iterator operator+(difference_type delta, column_iterator iterator) noexcept { return iterator += delta; }

    difference_type operator-(column_iterator const& other) const noexcept
    {
        assert_same_column(other);
        if (_data == other._data)
            return 0;
        return (_data - other._data) / _table->row_size_bytes;
    }

    reference operator[](difference_type delta) const noexcept { return *(*this + delta); }
    bool operator==(column_iterator const& other) const noexcept
    {
        return _table == other._table && _data == other._data
            && _column.column_index == other._column.column_index;
    }
    bool operator!=(column_iterator const& other) const noexcept { return !(*this == other); }
    bool operator<(column_iterator const& other) const noexcept { assert_same_column(other); return _data < other._data; }
    bool operator>(column_iterator const& other) const noexcept { return other < *this; }
    bool operator<=(column_iterator const& other) const noexcept { return !(other < *this); }
    bool operator>=(column_iterator const& other) const noexcept { return !(*this < other); }

    column_iterator end() const noexcept
    {
        auto result = *this;
        result._data = _table->data.ptr
            + size_t{_table->row_count} * _table->row_size_bytes;
        return result;
    }

    raw_cursor<TableId> position() const noexcept
    {
        auto row = static_cast<uint32_t>((_data - _table->data.ptr)
            / _table->row_size_bytes) + 1;
        return raw_cursor<TableId>::at(_table->cxt, row);
    }
};

} // namespace dnmd

#endif // _SRC_INC_DNMD_READ_HPP_
