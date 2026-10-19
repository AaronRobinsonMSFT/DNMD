#ifndef _SRC_INC_DNMD_HPP_
#define _SRC_INC_DNMD_HPP_

#include "dnmd.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iterator>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace dnmd
{

struct handle_deleter_t final
{
    using pointer = mdhandle_t;
    void operator()(mdhandle_t handle)
    {
        ::md_destroy_handle(handle);
    }
};

// C++ lifetime wrapper for mdhandle_t type
using handle_ptr = std::unique_ptr<mdhandle_t, handle_deleter_t>;

namespace detail
{
inline void commit_row_add(mdcursor_t row) noexcept;
}

struct added_row_t final
{
private:
    mdcursor_t new_row{};
public:
    added_row_t() = default;
    explicit added_row_t(mdcursor_t row) : new_row{ row } {}
    added_row_t(added_row_t const& other) = delete;
    added_row_t(added_row_t&& other)
    {
        *this = std::move(other);
    }

    added_row_t& operator=(added_row_t const& other) = delete;
    added_row_t& operator=(added_row_t&& other)
    {
        new_row = other.new_row;
        other.new_row = {}; // Clear the other's row so we don't double-commit.
        return *this;
    }

    ~added_row_t()
    {
        detail::commit_row_add(new_row);
    }

    operator mdcursor_t()
    {
        return new_row;
    }

    mdcursor_t* operator&()
    {
        return &new_row;
    }
};

struct column_type_blob {};
struct column_type_string {};
struct column_type_guid {};
struct column_type_userstring {};
struct column_type_constant {};

template<mdtable_id_t TableId>
struct cursor;

template<mdtable_id_t... TableIds>
struct raw_cursor;

template<mdtable_id_t TableId, typename ColumnType>
struct column final
{
    using value_type = ColumnType;

    col_index_t column_index;
    public:
        explicit constexpr column(col_index_t col)
            : column_index{ col }
        {
        }
};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct indirect_cursor;

namespace detail
{
struct stored_column_value {};
struct list_boundary_value {};

template<mdtable_id_t TableId>
constexpr mdtable_id_t parent_table_id() noexcept
{
    if constexpr (TableId == mdtid_Field || TableId == mdtid_MethodDef
        || TableId == mdtid_Event || TableId == mdtid_Property)
        return mdtid_TypeDef;
    else if constexpr (TableId == mdtid_Param)
        return mdtid_MethodDef;
#ifdef DNMD_PORTABLE_PDB
    else if constexpr (TableId == mdtid_LocalVariable || TableId == mdtid_LocalConstant)
        return mdtid_LocalScope;
#endif
    else
        return mdtid_Unused;
}

template<mdtable_id_t TableId>
constexpr mdtable_id_t pointer_table_id() noexcept
{
    if constexpr (TableId == mdtid_Field) return mdtid_FieldPtr;
    else if constexpr (TableId == mdtid_MethodDef) return mdtid_MethodPtr;
    else if constexpr (TableId == mdtid_Param) return mdtid_ParamPtr;
    else if constexpr (TableId == mdtid_Event) return mdtid_EventPtr;
    else if constexpr (TableId == mdtid_Property) return mdtid_PropertyPtr;
    else return mdtid_Unused;
}

template<typename ColumnType>
struct searchable_column : std::false_type {};

template<>
struct searchable_column<column_type_constant> : std::true_type {};

template<mdtable_id_t... TableIds>
struct searchable_column<raw_cursor<TableIds...>> : std::true_type {};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct searchable_column<indirect_cursor<DirectTableId, IndirectTableId>> : std::true_type {};

template<mdtable_id_t TableId>
constexpr auto pointer_column() noexcept
{
    if constexpr (TableId == mdtid_FieldPtr)
        return column<mdtid_FieldPtr, raw_cursor<mdtid_Field>>{mdtFieldPtr_Field};
    else if constexpr (TableId == mdtid_MethodPtr)
        return column<mdtid_MethodPtr, raw_cursor<mdtid_MethodDef>>{mdtMethodPtr_Method};
    else if constexpr (TableId == mdtid_ParamPtr)
        return column<mdtid_ParamPtr, raw_cursor<mdtid_Param>>{mdtParamPtr_Param};
    else if constexpr (TableId == mdtid_EventPtr)
        return column<mdtid_EventPtr, raw_cursor<mdtid_Event>>{mdtEventPtr_Event};
    else
    {
        static_assert(TableId == mdtid_PropertyPtr, "The table must be an indirection table.");
        return column<mdtid_PropertyPtr, raw_cursor<mdtid_Property>>{mdtPropertyPtr_Property};
    }
}

// Parentheses avoid expanding corhdr.h's function-like macro.
inline uint32_t (RidFromToken)(uint64_t tk) noexcept
{
    return static_cast<uint32_t>(tk) & 0x00ffffff;
}

inline mdtable_t* CursorTable(mdcursor_t const& c) noexcept
{
    return reinterpret_cast<mdtable_t*>(c._reserved1);
}

inline uint32_t CursorRow(mdcursor_t const& c) noexcept
{
    return (RidFromToken)(c._reserved2);
}

inline mdcursor_t CreateCursor(mdtable_t const* table, uint32_t row) noexcept
{
    assert(table != nullptr && row <= table->row_count + 1);
    return {reinterpret_cast<intptr_t>(table), static_cast<intptr_t>(row)};
}

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline mdtable_id_t list_target_table(mdtable_t const* source) noexcept
{
    static_assert(DirectTableId >= mdtid_First && DirectTableId < mdtid_End);
    static_assert(IndirectTableId >= mdtid_First && IndirectTableId < mdtid_End);
    return source->cxt->tables[DirectTableId].has_indirect_table ? IndirectTableId : DirectTableId;
}

template<mdtable_id_t TableId, mdtable_id_t TargetTableId>
inline uint32_t list_end_row(mdtable_t const* source, column<TableId, raw_cursor<TargetTableId>>) noexcept
{
    return source->cxt->tables[TargetTableId].row_count + 1;
}

template<mdtable_id_t TableId, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline uint32_t list_end_row(mdtable_t const* source,
    column<TableId, indirect_cursor<DirectTableId, IndirectTableId>>) noexcept
{
    return source->cxt->tables[list_target_table<DirectTableId, IndirectTableId>(source)].row_count + 1;
}

template<mdtable_id_t TableId, typename ColumnType>
inline bool sorted_search(mdtable_t const* table, column<TableId, ColumnType> col) noexcept;

template<mdtable_id_t TableId, typename ColumnType>
inline void update_column_sort_order(mdcursor_t row, column<TableId, ColumnType> col) noexcept;

template<mdtable_id_t TableId, mdtable_id_t... TableIds>
inline constexpr bool contains_table_id_v = ((TableId == TableIds) || ...);

template<mdtable_id_t... TableIds>
struct column_reference_codec
{
    static_assert(sizeof...(TableIds) > 1);
    static_assert(((TableIds == mdtid_Unused || (TableIds >= mdtid_First && TableIds < mdtid_End)) && ...));

    static constexpr mdtable_id_t tables[] = {TableIds...};
    // TODO: Use std::countl_zero to compute the tag width once C++20 is required.
    static constexpr uint32_t tag_bits = [] {
        uint32_t bits = 0;
        for (size_t count = sizeof...(TableIds) - 1; count != 0; count >>= 1)
            ++bits;
        return bits;
    }();

    static bool decode(uint32_t raw, mdtable_id_t& table, uint32_t& row) noexcept
    {
        uint32_t tag = raw & ((uint32_t{1} << tag_bits) - 1);
        if (tag >= sizeof...(TableIds) || tables[tag] == mdtid_Unused)
            return false;
        table = tables[tag];
        row = raw >> tag_bits;
        return true;
    }

    static bool encode(mdToken value, uint32_t& raw) noexcept
    {
        auto table = static_cast<mdtable_id_t>(ExtractTokenType(value));
        for (uint32_t tag = 0; tag < sizeof...(TableIds); ++tag)
        {
            if (tables[tag] == table)
            {
                raw = ((RidFromToken)(value) << tag_bits) | tag;
                return true;
            }
        }
        return false;
    }
};

template<mdtable_id_t TableId>
struct column_reference_codec<TableId>
{
    static_assert(TableId >= mdtid_First && TableId < mdtid_End);

    static bool decode(uint32_t raw, mdtable_id_t& table, uint32_t& row) noexcept
    {
        table = TableId;
        row = (RidFromToken)(raw);
        return true;
    }

    static bool encode(mdToken value, uint32_t& raw) noexcept
    {
        if (ExtractTokenType(value) != TableId)
            return false;
        raw = (RidFromToken)(value);
        return true;
    }
};

// Shared core helpers while its implementation remains C.
extern "C"
{
bool resolve_column_reference(mdtable_t const* source, mdtable_id_t table, uint32_t row, mdcursor_t* cursor);
bool try_get_string(::mdcxt__* context, size_t offset, char const** value);
bool try_get_blob(::mdcxt__* context, size_t offset, uint8_t const** value, uint32_t* length);
bool try_get_guid(::mdcxt__* context, size_t index, mdguid_t* value);
bool try_get_user_string(::mdcxt__* context, size_t offset, mduserstring_t* value, size_t* next_offset);
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

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline bool resolve_indirect_cursor(mdcursor_t source, mdcursor_t& target) noexcept;
}

template<mdtable_id_t TableId, typename ColumnType, typename ValuePolicy = detail::stored_column_value>
struct column_iterator;

template<mdtable_id_t TableId>
bool md_find_token_of_range_element(cursor<TableId> element, mdToken& token) noexcept;

template<mdtable_id_t TableId>
struct row_reader final
{
private:
    friend struct cursor<TableId>;
    template<mdtable_id_t, typename, typename>
    friend struct column_iterator;

    mdtable_t const* _table;
    uint8_t const* _data;

    row_reader(mdtable_t const* table, uint8_t const* data) noexcept : _table{table}, _data{data} {}

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
    bool read_position(column<TableId, raw_cursor<TargetTableIds...>> col, mdcursor_t& value) const noexcept
    {
        mdtable_id_t table;
        uint32_t row;
        return detail::column_reference_codec<TargetTableIds...>::decode(read_raw(col), table, row)
            && detail::resolve_column_reference(_table, table, row, &value);
    }

    template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
    bool read_position(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> col, mdcursor_t& value) const noexcept
    {
        auto target = detail::list_target_table<DirectTableId, IndirectTableId>(_table);
        return detail::resolve_column_reference(_table, target, (detail::RidFromToken)(read_raw(col)), &value);
    }

    void advance() noexcept
    {
        _data += _table->row_size_bytes;
    }

public:
    explicit row_reader(cursor<TableId> const& row) noexcept
        : _table{detail::CursorTable(row._cursor)}
        , _data{_table->data.ptr
            + size_t{detail::CursorRow(row._cursor) - 1} * _table->row_size_bytes}
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
        mdcursor_t position;
        if (!read_position(col, position))
            return false;
        value = raw_cursor<TargetTableIds...>{position};
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
        mdcursor_t position;
        if (!read_position(col, position))
            return false;
        value = indirect_cursor<DirectTableId, IndirectTableId>{position};
        return true;
    }

    template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
    bool read(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> col,
        indirect_cursor<DirectTableId, IndirectTableId>& value, uint32_t* count) const noexcept
    {
        assert(count != nullptr);
        mdcursor_t position;
        if (!read_position(col, position))
            return false;
        auto target = detail::CursorTable(position);
        uint32_t first = detail::CursorRow(position);
        uint32_t length = 0;
        if (first != 0 && first != target->row_count + 1)
        {
            length = target->row_count - first + 1;
            auto next = *this;
            auto end = _table->data.ptr + size_t{_table->row_count} * _table->row_size_bytes;
            for (next.advance(); next._data < end; next.advance())
            {
                mdcursor_t next_position;
                if (!next.read_position(col, next_position))
                    return false;
                assert(detail::CursorTable(next_position) == target);
                uint32_t last = detail::CursorRow(next_position);
                if (last == 0)
                    continue;
                if (last < first)
                    return false;
                length = last - first;
                break;
            }
        }
        value = indirect_cursor<DirectTableId, IndirectTableId>{position};
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

    row_reader<TableId> _reader;
    column<TableId, ColumnType> _column;

    column_iterator(row_reader<TableId> reader, column<TableId, ColumnType> col) noexcept
        : _reader{reader}, _column{col} {}

    void assert_same_column(column_iterator const& other) const noexcept
    {
        assert(_reader._table == other._reader._table && _column.column_index == other._column.column_index);
        (void)other;
    }

public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = uint32_t;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using pointer = void;

    column_iterator() noexcept : _reader{nullptr, nullptr}, _column{static_cast<col_index_t>(0)} {}

    column_iterator(cursor<TableId> const& row, column<TableId, ColumnType> col) noexcept
        : _reader{row}, _column{col} {}

    value_type operator*() const noexcept
    {
        assert(_reader._table != nullptr);
        assert(_reader._data >= _reader._table->data.ptr);
        assert(_reader._data < _reader._table->data.ptr
            + size_t{_reader._table->row_count} * _reader._table->row_size_bytes);
        uint32_t value = _reader.read_raw(_column);
        if constexpr (std::is_same_v<ValuePolicy, detail::list_boundary_value>)
        {
            if (value == 0)
            {
                column_iterator<TableId, ColumnType> probe{_reader, _column};
                auto last = probe.end();
                auto next = std::find_if(++probe, last, [](uint32_t start) { return start != 0; });
                return next == last ? detail::list_end_row(_reader._table, _column) : *next;
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
        assert(_reader._table != nullptr);
#ifndef NDEBUG
        auto index = (_reader._data - _reader._table->data.ptr) / _reader._table->row_size_bytes;
        assert(delta >= -index && delta <= static_cast<difference_type>(_reader._table->row_count) - index);
#endif
        _reader._data += delta * _reader._table->row_size_bytes;
        return *this;
    }

    column_iterator& operator-=(difference_type delta) noexcept { return *this += -delta; }
    column_iterator operator+(difference_type delta) const noexcept { auto result = *this; return result += delta; }
    column_iterator operator-(difference_type delta) const noexcept { auto result = *this; return result -= delta; }
    friend column_iterator operator+(difference_type delta, column_iterator iterator) noexcept { return iterator += delta; }

    difference_type operator-(column_iterator const& other) const noexcept
    {
        assert_same_column(other);
        if (_reader._data == other._reader._data)
            return 0;
        return (_reader._data - other._reader._data) / _reader._table->row_size_bytes;
    }

    reference operator[](difference_type delta) const noexcept { return *(*this + delta); }
    bool operator==(column_iterator const& other) const noexcept
    {
        return _reader._table == other._reader._table && _reader._data == other._reader._data
            && _column.column_index == other._column.column_index;
    }
    bool operator!=(column_iterator const& other) const noexcept { return !(*this == other); }
    bool operator<(column_iterator const& other) const noexcept { assert_same_column(other); return _reader._data < other._reader._data; }
    bool operator>(column_iterator const& other) const noexcept { return other < *this; }
    bool operator<=(column_iterator const& other) const noexcept { return !(other < *this); }
    bool operator>=(column_iterator const& other) const noexcept { return !(*this < other); }

    column_iterator end() const noexcept
    {
        assert(_reader._table != nullptr);
        auto result = *this;
        result._reader._data = _reader._table->data.ptr
            + size_t{_reader._table->row_count} * _reader._table->row_size_bytes;
        return result;
    }

    raw_cursor<TableId> position() const noexcept
    {
        assert(_reader._table != nullptr);
        auto row = static_cast<uint32_t>((_reader._data - _reader._table->data.ptr)
            / _reader._table->row_size_bytes) + 1;
        return raw_cursor<TableId>{detail::CreateCursor(_reader._table, row)};
    }
};

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
struct cursor final
{
    private:
        template<mdtable_id_t...>
        friend struct raw_cursor;
        template<mdtable_id_t, mdtable_id_t>
        friend struct indirect_cursor;
        template<mdtable_id_t>
        friend struct row_reader;
        template<mdtable_id_t>
        friend struct row_writer;

        mdcursor_t _cursor;

        explicit cursor(mdcursor_t c) noexcept : _cursor{ c }
        {
#ifndef NDEBUG
            mdToken tk;
            assert(::md_cursor_is_dereferenceable(_cursor));
            assert(::md_cursor_to_token(_cursor, &tk));
            assert(ExtractTokenType(tk) == TableId);
#endif
        }

    public:
        cursor() = delete;

        operator mdcursor_t() const noexcept
        {
            return _cursor;
        }

        bool get_token(mdToken& tk) const noexcept
        {
            return ::md_cursor_to_token(_cursor, &tk);
        }

        bool get_parent_token(mdToken& tk) const noexcept
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

        template<mdtable_id_t... TargetTableIds>
        int32_t get_column_values(column<TableId, raw_cursor<TargetTableIds...>> column, uint32_t count, mdToken* tokens) const noexcept
        {
            if (count == 0)
                return 0;
            assert(tokens != nullptr);
            row_reader<TableId> reader{*this};
            uint32_t row = detail::CursorRow(_cursor);
            uint32_t available = reader._table->row_count - row + 1;
            if (count > available)
                count = available;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (!reader.read(column, tokens[i]))
                    return -1;
                if (i + 1 < count)
                    reader.advance();
            }
            return static_cast<int32_t>(count);
        }

        template<mdtable_id_t... TargetTableIds>
        bool get_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, raw_cursor<TargetTableIds...>& target_cursor) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, target_cursor);
        }

        template<mdtable_id_t... TargetTableIds>
        bool get_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, mdToken& tk) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, tk);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool get_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, indirect_cursor<DirectTableId, IndirectTableId>& target_cursor) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, target_cursor);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool get_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, mdToken& tk) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, tk);
        }

        bool get_column_value(column<TableId, column_type_constant> column, uint32_t* value) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, value);
        }

        bool get_column_value(column<TableId, column_type_blob> column, uint8_t const** blob, uint32_t* blob_len) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, blob, blob_len);
        }

        bool get_column_value(column<TableId, column_type_guid> column, mdguid_t* guid) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, guid);
        }

        bool get_column_value(column<TableId, column_type_userstring> column, mduserstring_t* userstring) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, userstring);
        }

        bool get_column_value(column<TableId, column_type_string> column, char const** str) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, str);
        }

        bool set_column_value(column<TableId, column_type_constant> column, uint32_t value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        bool set_column_value(column<TableId, column_type_string> column, char const* value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        bool set_column_value(column<TableId, column_type_blob> column, uint8_t const* value, uint32_t length) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value, length);
        }

        bool set_column_value(column<TableId, column_type_guid> column, mdguid_t value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        bool set_column_value(column<TableId, column_type_userstring> column, char16_t const* value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        template<mdtable_id_t... TargetTableIds>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, mdToken value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        template<mdtable_id_t... TargetTableIds, mdtable_id_t ValueTableId>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, cursor<ValueTableId> value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        template<mdtable_id_t... TargetTableIds, mdtable_id_t... ValueTableIds>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, raw_cursor<ValueTableIds...> const& value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        template<mdtable_id_t... TargetTableIds, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, indirect_cursor<DirectTableId, IndirectTableId> const& value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, mdToken value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t ValueTableId>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, cursor<ValueTableId> value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t... ValueTableIds>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, raw_cursor<ValueTableIds...> const& value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, indirect_cursor<DirectTableId, IndirectTableId> const& value) const noexcept
        {
            return row_writer<TableId>{*this}.write(column, value);
        }

        template<mdtable_id_t TargetTableId, mdtable_id_t IndirectTableId>
        bool get_column_value_as_range(column<TableId, indirect_cursor<TargetTableId, IndirectTableId>> column, indirect_cursor<TargetTableId, IndirectTableId>& range_cursor, uint32_t* count) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, range_cursor, count);
        }
};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct indirect_cursor final
{
    private:
        mdcursor_t _cursor{};
        mdcursor_t target{};
        bool target_resolved = false;
        bool has_row = false;

    public:
        indirect_cursor() = default;
        explicit indirect_cursor(mdcursor_t c) noexcept : _cursor{ c }
        {
#ifndef NDEBUG
            mdToken tk;
            assert(::md_cursor_to_token(_cursor, &tk));
            assert(ExtractTokenType(tk) == DirectTableId || ExtractTokenType(tk) == IndirectTableId);
#endif
            target_resolved = detail::resolve_indirect_cursor<DirectTableId, IndirectTableId>(_cursor, target);
            has_row = target_resolved && ::md_cursor_is_dereferenceable(target);
        }

        operator mdcursor_t() const noexcept
        {
            return _cursor;
        }

        explicit operator bool() const noexcept
        {
            return has_row;
        }

        cursor<DirectTableId> as() const noexcept
        {
            assert(has_row);
            return cursor<DirectTableId>{target};
        }

        bool get_token(mdToken& tk) const noexcept
        {
            return ::md_cursor_to_token(_cursor, &tk);
        }

        bool get_target_token(mdToken& tk) const noexcept
        {
            return target_resolved && ::md_cursor_to_token(target, &tk);
        }
};

template<mdtable_id_t... TableIds>
struct raw_cursor final
{
    static_assert(sizeof...(TableIds) != 0, "A raw cursor must specify at least one table.");

    private:
        mdcursor_t _cursor{};

    public:
        raw_cursor() = default;
        explicit raw_cursor(mdcursor_t c) noexcept
            : _cursor{ c }
        {
#ifndef NDEBUG
            mdToken tk;
            assert(::md_cursor_to_token(_cursor, &tk));
            assert(((ExtractTokenType(tk) == TableIds) || ...));
#endif
        }

        operator mdcursor_t() const noexcept
        {
            return _cursor;
        }

        explicit operator bool() const noexcept
        {
            return ::md_cursor_is_dereferenceable(_cursor);
        }

        bool get_token(mdToken& tk) const noexcept
        {
            return ::md_cursor_to_token(_cursor, &tk);
        }

        template<size_t TableCount = sizeof...(TableIds),
            std::enable_if_t<TableCount == 1 && TableCount == sizeof...(TableIds), int> = 0>
        auto as() const noexcept
        {
            return (cursor<TableIds>{_cursor}, ...);
        }

        template<mdtable_id_t CodedTableId>
        raw_cursor<CodedTableId> get_as() const noexcept
        {
            static_assert(CodedTableId != mdtid_Unused, "Reserved tags do not refer to a table.");
            static_assert(((CodedTableId == TableIds) || ...), "CodedTableId must be one of the tables in the coded index.");
            mdToken tk;
            if (!get_token(tk) || ExtractTokenType(tk) != CodedTableId)
                return {};
            return raw_cursor<CodedTableId>{_cursor};
        }

        template<typename TCallable>
        bool visit(TCallable&& callable) const
        {
            mdToken tk;
            if (!*this || !get_token(tk))
                return false;
            return (visit_impl<TableIds>(static_cast<mdtable_id_t>(ExtractTokenType(tk)), std::forward<TCallable>(callable)) || ...);
        }

    private:
        template<mdtable_id_t CodedTableId, typename TCallable>
        bool visit_impl(mdtable_id_t table_id, TCallable&& callable) const
        {
            if constexpr (CodedTableId == mdtid_Unused)
            {
                return false;
            }
            else
            {
                if (table_id != CodedTableId)
                    return false;
                std::forward<TCallable>(callable)(cursor<CodedTableId>{_cursor});
                return true;
            }
        }
};

namespace detail
{
template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline bool resolve_indirect_cursor(mdcursor_t source, mdcursor_t& target) noexcept
{
    auto table = CursorTable(source);
    if (table == nullptr)
        return false;
    if (table->table_id == DirectTableId)
    {
        target = source;
        return true;
    }
    if (table->table_id != IndirectTableId)
        return false;

    uint32_t row = CursorRow(source);
    // Sentinel positions have no pointer row to read.
    if (row == 0 || row == table->row_count + 1)
    {
        mdcursor_t position;
        if (!resolve_column_reference(table, DirectTableId, 0, &position))
            return false;
        if (row != 0)
            position._reserved2 = CursorTable(position)->row_count + 1;
        target = position;
        return true;
    }
    if (row > table->row_count)
        return false;

    row_reader<IndirectTableId> reader{raw_cursor<IndirectTableId>{source}.as()};
    raw_cursor<DirectTableId> position;
    if (!reader.read(pointer_column<IndirectTableId>(), position))
        return false;
    target = position;
    return true;
}
}

template<typename T>
struct cursor_tableid;

template<mdtable_id_t TableId>
struct cursor_tableid<cursor<TableId>>
{
    static constexpr mdtable_id_t value = TableId;
};

template<typename T>
inline constexpr mdtable_id_t cursor_tableid_v = cursor_tableid<T>::value;

template<mdtable_id_t... TableIds>
inline bool md_cursor_move(raw_cursor<TableIds...>& cursor, int32_t delta) noexcept
{
    mdcursor_t c = cursor;
    if (!::md_cursor_move(&c, delta))
        return false;
    cursor = raw_cursor<TableIds...>{c};
    return true;
}

template<mdtable_id_t TableId, mdtable_id_t IndirectTableId>
inline bool md_cursor_move(indirect_cursor<TableId, IndirectTableId>& cursor, int32_t delta) noexcept
{
    mdcursor_t c = cursor;
    if (!::md_cursor_move(&c, delta))
        return false;
    cursor = indirect_cursor<TableId, IndirectTableId>{c};
    return true;
}

template<mdtable_id_t... TableIds>
inline bool md_cursor_next(raw_cursor<TableIds...>& cursor) noexcept
{
    return md_cursor_move(cursor, 1);
}

template<mdtable_id_t TableId, mdtable_id_t IndirectTableId>
inline bool md_cursor_next(indirect_cursor<TableId, IndirectTableId>& cursor) noexcept
{
    return md_cursor_move(cursor, 1);
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

template<mdtable_id_t TableId>
inline raw_cursor<TableId> md_create_cursor(mdhandle_t handle, uint32_t* count) noexcept
{
    mdcursor_t c;
    if (!::md_create_cursor(handle, TableId, &c, count))
        return {};
    return raw_cursor<TableId>{c};
}

template<mdtable_id_t TableId>
inline raw_cursor<TableId> md_create_cursor(mdhandle_t handle) noexcept
{
    uint32_t count;
    return md_create_cursor<TableId>(handle, &count);
}

template<mdtable_id_t... TableIds>
inline raw_cursor<TableIds...> md_token_to_cursor(mdhandle_t handle, mdToken tk) noexcept
{
    mdcursor_t c;
    if (!((ExtractTokenType(tk) == TableIds) || ...) || !::md_token_to_cursor(handle, tk, &c))
        return {};
    return raw_cursor<TableIds...>{c};
}

inline bool md_is_valid_token(mdhandle_t handle, mdToken tk) noexcept
{
    mdcursor_t c;
    return ::md_token_to_cursor(handle, tk, &c);
}

namespace columns
{
inline constexpr column<mdtid_Module, column_type_constant> Module_Generation{ mdtModule_Generation };
inline constexpr column<mdtid_Module, column_type_string> Module_Name{ mdtModule_Name };
inline constexpr column<mdtid_Module, column_type_guid> Module_Mvid{ mdtModule_Mvid };
inline constexpr column<mdtid_Module, column_type_guid> Module_EncId{ mdtModule_EncId };
inline constexpr column<mdtid_Module, column_type_guid> Module_EncBaseId{ mdtModule_EncBaseId };

inline constexpr column<mdtid_TypeRef, raw_cursor<mdtid_Module, mdtid_ModuleRef, mdtid_AssemblyRef, mdtid_TypeRef>> TypeRef_ResolutionScope{ mdtTypeRef_ResolutionScope };
inline constexpr column<mdtid_TypeRef, column_type_string> TypeRef_TypeName{ mdtTypeRef_TypeName };
inline constexpr column<mdtid_TypeRef, column_type_string> TypeRef_TypeNamespace{ mdtTypeRef_TypeNamespace };

inline constexpr column<mdtid_TypeDef, column_type_constant> TypeDef_Flags{ mdtTypeDef_Flags };
inline constexpr column<mdtid_TypeDef, column_type_string> TypeDef_TypeName{ mdtTypeDef_TypeName };
inline constexpr column<mdtid_TypeDef, column_type_string> TypeDef_TypeNamespace{ mdtTypeDef_TypeNamespace };
inline constexpr column<mdtid_TypeDef, raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> TypeDef_Extends{ mdtTypeDef_Extends };
inline constexpr column<mdtid_TypeDef, indirect_cursor<mdtid_Field, mdtid_FieldPtr>> TypeDef_FieldList{ mdtTypeDef_FieldList };
inline constexpr column<mdtid_TypeDef, indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>> TypeDef_MethodList{ mdtTypeDef_MethodList };

inline constexpr column<mdtid_Field, column_type_constant> Field_Flags{ mdtField_Flags };
inline constexpr column<mdtid_Field, column_type_string> Field_Name{ mdtField_Name };
inline constexpr column<mdtid_Field, column_type_blob> Field_Signature{ mdtField_Signature };

inline constexpr column<mdtid_MethodDef, column_type_constant> MethodDef_Rva{ mdtMethodDef_Rva };
inline constexpr column<mdtid_MethodDef, column_type_constant> MethodDef_ImplFlags{ mdtMethodDef_ImplFlags };
inline constexpr column<mdtid_MethodDef, column_type_constant> MethodDef_Flags{ mdtMethodDef_Flags };
inline constexpr column<mdtid_MethodDef, column_type_string> MethodDef_Name{ mdtMethodDef_Name };
inline constexpr column<mdtid_MethodDef, column_type_blob> MethodDef_Signature{ mdtMethodDef_Signature };
inline constexpr column<mdtid_MethodDef, indirect_cursor<mdtid_Param, mdtid_ParamPtr>> MethodDef_ParamList{ mdtMethodDef_ParamList };

inline constexpr column<mdtid_Param, column_type_constant> Param_Flags{ mdtParam_Flags };
inline constexpr column<mdtid_Param, column_type_constant> Param_Sequence{ mdtParam_Sequence };
inline constexpr column<mdtid_Param, column_type_string> Param_Name{ mdtParam_Name };

inline constexpr column<mdtid_InterfaceImpl, raw_cursor<mdtid_TypeDef>> InterfaceImpl_Class{ mdtInterfaceImpl_Class };
inline constexpr column<mdtid_InterfaceImpl, raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> InterfaceImpl_Interface{ mdtInterfaceImpl_Interface };

inline constexpr column<mdtid_MemberRef, raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_ModuleRef, mdtid_MethodDef, mdtid_TypeSpec>> MemberRef_Class{ mdtMemberRef_Class };
inline constexpr column<mdtid_MemberRef, column_type_string> MemberRef_Name{ mdtMemberRef_Name };
inline constexpr column<mdtid_MemberRef, column_type_blob> MemberRef_Signature{ mdtMemberRef_Signature };

inline constexpr column<mdtid_Constant, column_type_constant> Constant_Type{ mdtConstant_Type };
inline constexpr column<mdtid_Constant, raw_cursor<mdtid_Field, mdtid_Param, mdtid_Property>> Constant_Parent{ mdtConstant_Parent };
inline constexpr column<mdtid_Constant, column_type_blob> Constant_Value{ mdtConstant_Value };

inline constexpr column<mdtid_CustomAttribute, raw_cursor<mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param, mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property, mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec, mdtid_Assembly, mdtid_AssemblyRef, mdtid_File, mdtid_ExportedType, mdtid_ManifestResource, mdtid_GenericParam, mdtid_GenericParamConstraint, mdtid_MethodSpec>> CustomAttribute_Parent{ mdtCustomAttribute_Parent };
inline constexpr column<mdtid_CustomAttribute, raw_cursor<mdtid_Unused, mdtid_Unused, mdtid_MethodDef, mdtid_MemberRef, mdtid_Unused>> CustomAttribute_Type{ mdtCustomAttribute_Type };
inline constexpr column<mdtid_CustomAttribute, column_type_blob> CustomAttribute_Value{ mdtCustomAttribute_Value };

inline constexpr column<mdtid_FieldMarshal, raw_cursor<mdtid_Field, mdtid_Param>> FieldMarshal_Parent{ mdtFieldMarshal_Parent };
inline constexpr column<mdtid_FieldMarshal, column_type_blob> FieldMarshal_NativeType{ mdtFieldMarshal_NativeType };

inline constexpr column<mdtid_DeclSecurity, column_type_constant> DeclSecurity_Action{ mdtDeclSecurity_Action };
inline constexpr column<mdtid_DeclSecurity, raw_cursor<mdtid_TypeDef, mdtid_MethodDef, mdtid_Assembly>> DeclSecurity_Parent{ mdtDeclSecurity_Parent };
inline constexpr column<mdtid_DeclSecurity, column_type_blob> DeclSecurity_PermissionSet{ mdtDeclSecurity_PermissionSet };

inline constexpr column<mdtid_ClassLayout, column_type_constant> ClassLayout_PackingSize{ mdtClassLayout_PackingSize };
inline constexpr column<mdtid_ClassLayout, column_type_constant> ClassLayout_ClassSize{ mdtClassLayout_ClassSize };
inline constexpr column<mdtid_ClassLayout, raw_cursor<mdtid_TypeDef>> ClassLayout_Parent{ mdtClassLayout_Parent };

inline constexpr column<mdtid_FieldLayout, column_type_constant> FieldLayout_Offset{ mdtFieldLayout_Offset };
inline constexpr column<mdtid_FieldLayout, raw_cursor<mdtid_Field>> FieldLayout_Field{ mdtFieldLayout_Field };

inline constexpr column<mdtid_StandAloneSig, column_type_blob> StandAloneSig_Signature{ mdtStandAloneSig_Signature };

inline constexpr column<mdtid_EventMap, raw_cursor<mdtid_TypeDef>> EventMap_Parent{ mdtEventMap_Parent };
inline constexpr column<mdtid_EventMap, indirect_cursor<mdtid_Event, mdtid_EventPtr>> EventMap_EventList{ mdtEventMap_EventList };

inline constexpr column<mdtid_Event, column_type_constant> Event_EventFlags{ mdtEvent_EventFlags };
inline constexpr column<mdtid_Event, column_type_string> Event_Name{ mdtEvent_Name };
inline constexpr column<mdtid_Event, raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> Event_EventType{ mdtEvent_EventType };

inline constexpr column<mdtid_PropertyMap, raw_cursor<mdtid_TypeDef>> PropertyMap_Parent{ mdtPropertyMap_Parent };
inline constexpr column<mdtid_PropertyMap, indirect_cursor<mdtid_Property, mdtid_PropertyPtr>> PropertyMap_PropertyList{ mdtPropertyMap_PropertyList };

inline constexpr column<mdtid_Property, column_type_constant> Property_Flags{ mdtProperty_Flags };
inline constexpr column<mdtid_Property, column_type_string> Property_Name{ mdtProperty_Name };
inline constexpr column<mdtid_Property, column_type_blob> Property_Type{ mdtProperty_Type };

inline constexpr column<mdtid_MethodSemantics, column_type_constant> MethodSemantics_Semantics{ mdtMethodSemantics_Semantics };
inline constexpr column<mdtid_MethodSemantics, raw_cursor<mdtid_MethodDef>> MethodSemantics_Method{ mdtMethodSemantics_Method };
inline constexpr column<mdtid_MethodSemantics, raw_cursor<mdtid_Event, mdtid_Property>> MethodSemantics_Association{ mdtMethodSemantics_Association };

inline constexpr column<mdtid_MethodImpl, raw_cursor<mdtid_TypeDef>> MethodImpl_Class{ mdtMethodImpl_Class };
inline constexpr column<mdtid_MethodImpl, raw_cursor<mdtid_MethodDef, mdtid_MemberRef>> MethodImpl_MethodBody{ mdtMethodImpl_MethodBody };
inline constexpr column<mdtid_MethodImpl, raw_cursor<mdtid_MethodDef, mdtid_MemberRef>> MethodImpl_MethodDeclaration{ mdtMethodImpl_MethodDeclaration };

inline constexpr column<mdtid_ModuleRef, column_type_string> ModuleRef_Name{ mdtModuleRef_Name };

inline constexpr column<mdtid_TypeSpec, column_type_blob> TypeSpec_Signature{ mdtTypeSpec_Signature };

inline constexpr column<mdtid_ImplMap, column_type_constant> ImplMap_MappingFlags{ mdtImplMap_MappingFlags };
inline constexpr column<mdtid_ImplMap, raw_cursor<mdtid_Field, mdtid_MethodDef>> ImplMap_MemberForwarded{ mdtImplMap_MemberForwarded };
inline constexpr column<mdtid_ImplMap, column_type_string> ImplMap_ImportName{ mdtImplMap_ImportName };
inline constexpr column<mdtid_ImplMap, raw_cursor<mdtid_ModuleRef>> ImplMap_ImportScope{ mdtImplMap_ImportScope };

inline constexpr column<mdtid_FieldRva, column_type_constant> FieldRva_Rva{ mdtFieldRva_Rva };
inline constexpr column<mdtid_FieldRva, raw_cursor<mdtid_Field>> FieldRva_Field{ mdtFieldRva_Field };

inline constexpr column<mdtid_ENCLog, column_type_constant> ENCLog_Token{ mdtENCLog_Token };
inline constexpr column<mdtid_ENCLog, column_type_constant> ENCLog_Op{ mdtENCLog_Op };

inline constexpr column<mdtid_ENCMap, column_type_constant> ENCMap_Token{ mdtENCMap_Token };

inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_HashAlgId{ mdtAssembly_HashAlgId };
inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_MajorVersion{ mdtAssembly_MajorVersion };
inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_MinorVersion{ mdtAssembly_MinorVersion };
inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_BuildNumber{ mdtAssembly_BuildNumber };
inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_RevisionNumber{ mdtAssembly_RevisionNumber };
inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_Flags{ mdtAssembly_Flags };
inline constexpr column<mdtid_Assembly, column_type_blob> Assembly_PublicKey{ mdtAssembly_PublicKey };
inline constexpr column<mdtid_Assembly, column_type_string> Assembly_Name{ mdtAssembly_Name };
inline constexpr column<mdtid_Assembly, column_type_string> Assembly_Culture{ mdtAssembly_Culture };

inline constexpr column<mdtid_AssemblyProcessor, column_type_constant> AssemblyProcessor_Processor{ static_cast<col_index_t>(0) };

inline constexpr column<mdtid_AssemblyOS, column_type_constant> AssemblyOS_OSPlatformId{ static_cast<col_index_t>(0) };
inline constexpr column<mdtid_AssemblyOS, column_type_constant> AssemblyOS_OSMajorVersion{ static_cast<col_index_t>(1) };
inline constexpr column<mdtid_AssemblyOS, column_type_constant> AssemblyOS_OSMinorVersion{ static_cast<col_index_t>(2) };

inline constexpr column<mdtid_AssemblyRef, column_type_constant> AssemblyRef_MajorVersion{ mdtAssemblyRef_MajorVersion };
inline constexpr column<mdtid_AssemblyRef, column_type_constant> AssemblyRef_MinorVersion{ mdtAssemblyRef_MinorVersion };
inline constexpr column<mdtid_AssemblyRef, column_type_constant> AssemblyRef_BuildNumber{ mdtAssemblyRef_BuildNumber };
inline constexpr column<mdtid_AssemblyRef, column_type_constant> AssemblyRef_RevisionNumber{ mdtAssemblyRef_RevisionNumber };
inline constexpr column<mdtid_AssemblyRef, column_type_constant> AssemblyRef_Flags{ mdtAssemblyRef_Flags };
inline constexpr column<mdtid_AssemblyRef, column_type_blob> AssemblyRef_PublicKeyOrToken{ mdtAssemblyRef_PublicKeyOrToken };
inline constexpr column<mdtid_AssemblyRef, column_type_string> AssemblyRef_Name{ mdtAssemblyRef_Name };
inline constexpr column<mdtid_AssemblyRef, column_type_string> AssemblyRef_Culture{ mdtAssemblyRef_Culture };
inline constexpr column<mdtid_AssemblyRef, column_type_blob> AssemblyRef_HashValue{ mdtAssemblyRef_HashValue };

inline constexpr column<mdtid_AssemblyRefProcessor, column_type_constant> AssemblyRefProcessor_Processor{ static_cast<col_index_t>(0) };
inline constexpr column<mdtid_AssemblyRefProcessor, raw_cursor<mdtid_AssemblyRef>> AssemblyRefProcessor_AssemblyRef{ static_cast<col_index_t>(1) };

inline constexpr column<mdtid_AssemblyRefOS, column_type_constant> AssemblyRefOS_OSPlatformId{ static_cast<col_index_t>(0) };
inline constexpr column<mdtid_AssemblyRefOS, column_type_constant> AssemblyRefOS_OSMajorVersion{ static_cast<col_index_t>(1) };
inline constexpr column<mdtid_AssemblyRefOS, column_type_constant> AssemblyRefOS_OSMinorVersion{ static_cast<col_index_t>(2) };
inline constexpr column<mdtid_AssemblyRefOS, raw_cursor<mdtid_AssemblyRef>> AssemblyRefOS_AssemblyRef{ static_cast<col_index_t>(3) };

inline constexpr column<mdtid_File, column_type_constant> File_Flags{ mdtFile_Flags };
inline constexpr column<mdtid_File, column_type_string> File_Name{ mdtFile_Name };
inline constexpr column<mdtid_File, column_type_blob> File_HashValue{ mdtFile_HashValue };

inline constexpr column<mdtid_ExportedType, column_type_constant> ExportedType_Flags{ mdtExportedType_Flags };
inline constexpr column<mdtid_ExportedType, column_type_constant> ExportedType_TypeDefId{ mdtExportedType_TypeDefId };
inline constexpr column<mdtid_ExportedType, column_type_string> ExportedType_TypeName{ mdtExportedType_TypeName };
inline constexpr column<mdtid_ExportedType, column_type_string> ExportedType_TypeNamespace{ mdtExportedType_TypeNamespace };
inline constexpr column<mdtid_ExportedType, raw_cursor<mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType>> ExportedType_Implementation{ mdtExportedType_Implementation };

inline constexpr column<mdtid_ManifestResource, column_type_constant> ManifestResource_Offset{ mdtManifestResource_Offset };
inline constexpr column<mdtid_ManifestResource, column_type_constant> ManifestResource_Flags{ mdtManifestResource_Flags };
inline constexpr column<mdtid_ManifestResource, column_type_string> ManifestResource_Name{ mdtManifestResource_Name };
inline constexpr column<mdtid_ManifestResource, raw_cursor<mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType>> ManifestResource_Implementation{ mdtManifestResource_Implementation };

inline constexpr column<mdtid_NestedClass, raw_cursor<mdtid_TypeDef>> NestedClass_NestedClass{ mdtNestedClass_NestedClass };
inline constexpr column<mdtid_NestedClass, raw_cursor<mdtid_TypeDef>> NestedClass_EnclosingClass{ mdtNestedClass_EnclosingClass };

inline constexpr column<mdtid_GenericParam, column_type_constant> GenericParam_Number{ mdtGenericParam_Number };
inline constexpr column<mdtid_GenericParam, column_type_constant> GenericParam_Flags{ mdtGenericParam_Flags };
inline constexpr column<mdtid_GenericParam, raw_cursor<mdtid_TypeDef, mdtid_MethodDef>> GenericParam_Owner{ mdtGenericParam_Owner };
inline constexpr column<mdtid_GenericParam, column_type_string> GenericParam_Name{ mdtGenericParam_Name };

inline constexpr column<mdtid_MethodSpec, raw_cursor<mdtid_MethodDef, mdtid_MemberRef>> MethodSpec_Method{ mdtMethodSpec_Method };
inline constexpr column<mdtid_MethodSpec, column_type_blob> MethodSpec_Instantiation{ mdtMethodSpec_Instantiation };

inline constexpr column<mdtid_GenericParamConstraint, raw_cursor<mdtid_GenericParam>> GenericParamConstraint_Owner{ mdtGenericParamConstraint_Owner };
inline constexpr column<mdtid_GenericParamConstraint, raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> GenericParamConstraint_Constraint{ mdtGenericParamConstraint_Constraint };

#ifdef DNMD_PORTABLE_PDB
inline constexpr column<mdtid_Document, column_type_blob> Document_Name{ mdtDocument_Name };
inline constexpr column<mdtid_Document, column_type_guid> Document_HashAlgorithm{ mdtDocument_HashAlgorithm };
inline constexpr column<mdtid_Document, column_type_blob> Document_Hash{ mdtDocument_Hash };
inline constexpr column<mdtid_Document, column_type_guid> Document_Language{ mdtDocument_Language };

inline constexpr column<mdtid_MethodDebugInformation, raw_cursor<mdtid_Document>> MethodDebugInformation_Document{ mdtMethodDebugInformation_Document };
inline constexpr column<mdtid_MethodDebugInformation, column_type_blob> MethodDebugInformation_SequencePoints{ mdtMethodDebugInformation_SequencePoints };

inline constexpr column<mdtid_LocalScope, raw_cursor<mdtid_MethodDef>> LocalScope_Method{ mdtLocalScope_Method };
inline constexpr column<mdtid_LocalScope, raw_cursor<mdtid_ImportScope>> LocalScope_ImportScope{ mdtLocalScope_ImportScope };
inline constexpr column<mdtid_LocalScope, raw_cursor<mdtid_LocalVariable>> LocalScope_VariableList{ mdtLocalScope_VariableList };
inline constexpr column<mdtid_LocalScope, raw_cursor<mdtid_LocalConstant>> LocalScope_ConstantList{ mdtLocalScope_ConstantList };
inline constexpr column<mdtid_LocalScope, column_type_constant> LocalScope_StartOffset{ mdtLocalScope_StartOffset };
inline constexpr column<mdtid_LocalScope, column_type_constant> LocalScope_Length{ mdtLocalScope_Length };

inline constexpr column<mdtid_LocalVariable, column_type_constant> LocalVariable_Attributes{ mdtLocalVariable_Attributes };
inline constexpr column<mdtid_LocalVariable, column_type_constant> LocalVariable_Index{ mdtLocalVariable_Index };
inline constexpr column<mdtid_LocalVariable, column_type_string> LocalVariable_Name{ mdtLocalVariable_Name };

inline constexpr column<mdtid_LocalConstant, column_type_string> LocalConstant_Name{ mdtLocalConstant_Name };
inline constexpr column<mdtid_LocalConstant, column_type_blob> LocalConstant_Signature{ mdtLocalConstant_Signature };

inline constexpr column<mdtid_ImportScope, raw_cursor<mdtid_ImportScope>> ImportScope_Parent{ mdtImportScope_Parent };
inline constexpr column<mdtid_ImportScope, column_type_blob> ImportScope_Imports{ mdtImportScope_Imports };

inline constexpr column<mdtid_StateMachineMethod, raw_cursor<mdtid_MethodDef>> StateMachineMethod_MoveNextMethod{ mdtStateMachineMethod_MoveNextMethod };
inline constexpr column<mdtid_StateMachineMethod, raw_cursor<mdtid_MethodDef>> StateMachineMethod_KickoffMethod{ mdtStateMachineMethod_KickoffMethod };

inline constexpr column<mdtid_CustomDebugInformation, raw_cursor<mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param, mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property, mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec, mdtid_Assembly, mdtid_AssemblyRef, mdtid_File, mdtid_ExportedType, mdtid_ManifestResource, mdtid_GenericParam, mdtid_GenericParamConstraint, mdtid_MethodSpec, mdtid_Document, mdtid_LocalScope, mdtid_LocalVariable, mdtid_LocalConstant, mdtid_ImportScope>> CustomDebugInformation_Parent{ mdtCustomDebugInformation_Parent };
inline constexpr column<mdtid_CustomDebugInformation, column_type_guid> CustomDebugInformation_Kind{ mdtCustomDebugInformation_Kind };
inline constexpr column<mdtid_CustomDebugInformation, column_type_blob> CustomDebugInformation_Value{ mdtCustomDebugInformation_Value };
#endif // DNMD_PORTABLE_PDB
}

namespace detail
{
template<typename Column>
struct sort_key final
{
    Column column;
    bool descending;

    constexpr sort_key(Column col, bool desc = false) noexcept : column{col}, descending{desc} {}
};

template<mdtable_id_t TableId>
constexpr auto table_sort_keys() noexcept
{
    using namespace columns;
    if constexpr (TableId == mdtid_ClassLayout) return std::tuple{sort_key{ClassLayout_Parent}};
    else if constexpr (TableId == mdtid_Constant) return std::tuple{sort_key{Constant_Parent}};
    else if constexpr (TableId == mdtid_CustomAttribute) return std::tuple{sort_key{CustomAttribute_Parent}};
    else if constexpr (TableId == mdtid_DeclSecurity) return std::tuple{sort_key{DeclSecurity_Parent}};
    else if constexpr (TableId == mdtid_FieldLayout) return std::tuple{sort_key{FieldLayout_Field}};
    else if constexpr (TableId == mdtid_FieldMarshal) return std::tuple{sort_key{FieldMarshal_Parent}};
    else if constexpr (TableId == mdtid_FieldRva) return std::tuple{sort_key{FieldRva_Field}};
    else if constexpr (TableId == mdtid_GenericParam)
        return std::tuple{sort_key{GenericParam_Owner}, sort_key{GenericParam_Number}};
    else if constexpr (TableId == mdtid_GenericParamConstraint) return std::tuple{sort_key{GenericParamConstraint_Owner}};
    else if constexpr (TableId == mdtid_ImplMap) return std::tuple{sort_key{ImplMap_MemberForwarded}};
    else if constexpr (TableId == mdtid_InterfaceImpl) return std::tuple{sort_key{InterfaceImpl_Class}};
    else if constexpr (TableId == mdtid_MethodImpl) return std::tuple{sort_key{MethodImpl_Class}};
    else if constexpr (TableId == mdtid_MethodSemantics) return std::tuple{sort_key{MethodSemantics_Association}};
    else if constexpr (TableId == mdtid_NestedClass) return std::tuple{sort_key{NestedClass_NestedClass}};
#ifdef DNMD_PORTABLE_PDB
    else if constexpr (TableId == mdtid_LocalScope)
        return std::tuple{sort_key{LocalScope_Method}, sort_key{LocalScope_StartOffset}, sort_key{LocalScope_Length, true}};
    else if constexpr (TableId == mdtid_StateMachineMethod) return std::tuple{sort_key{StateMachineMethod_MoveNextMethod}};
    else if constexpr (TableId == mdtid_CustomDebugInformation) return std::tuple{sort_key{CustomDebugInformation_Parent}};
#endif
    else return std::tuple{};
}

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

} // namespace dnmd

#endif // _SRC_INC_DNMD_HPP_
