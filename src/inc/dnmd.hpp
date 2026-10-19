#ifndef _SRC_INC_DNMD_HPP_
#define _SRC_INC_DNMD_HPP_

#include "dnmd.h"
#include <cassert>
#include <cstring>
#include <memory>
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
        ::md_commit_row_add(new_row);
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
void update_column_sort_order(mdcursor_t row, col_index_t column);
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

template<mdtable_id_t TableId>
struct row_reader final
{
private:
    friend struct cursor<TableId>;

    mdtable_t const* _table;
    uint8_t const* _data;

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
        auto target = static_cast<mdtable_id_t>((static_cast<uint32_t>(column_details(col)) & mdtc_timask) >> 16);
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
        auto table = static_cast<mdtable_id_t>((static_cast<uint32_t>(column_details(col)) & mdtc_timask) >> 16);
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
            detail::update_column_sort_order(_cursor, col.column_index);
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
        auto details = table->column_details[static_cast<uint8_t>(col.column_index)];
        auto target = static_cast<mdtable_id_t>((static_cast<uint32_t>(details) & mdtc_timask) >> 16);
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
            return ::md_find_token_of_range_element(_cursor, &tk);
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
            assert(count != nullptr);
            row_reader<TableId> reader{*this};
            mdcursor_t c;
            if (!reader.read_position(column, c))
                return false;
            auto target = detail::CursorTable(c);
            uint32_t first = detail::CursorRow(c);
            uint32_t length = 0;
            if (first != 0 && first != target->row_count + 1)
            {
                length = target->row_count - first + 1;
                uint32_t row = detail::CursorRow(_cursor);
                for (; row < reader._table->row_count; ++row)
                {
                    reader.advance();
                    mdcursor_t next;
                    if (!reader.read_position(column, next))
                        return false;
                    assert(detail::CursorTable(next) == target);
                    uint32_t end = detail::CursorRow(next);
                    if (end == 0)
                        continue;
                    if (end < first)
                        return false;
                    length = end - first;
                    break;
                }
            }
            range_cursor = indirect_cursor<TargetTableId, IndirectTableId>{c};
            *count = length;
            return true;
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
template<mdtable_id_t TableId>
inline raw_cursor<TableId> find_row(cursor<TableId> begin, col_index_t column, uint32_t value) noexcept
{
    mdcursor_t c;
    if (!::md_find_row_from_cursor(begin, column, value, &c))
        return {};
    return raw_cursor<TableId>{c};
}
}

template<mdtable_id_t TableId>
inline raw_cursor<TableId> md_find_row_from_cursor(cursor<TableId> begin, column<TableId, column_type_constant> column, uint32_t value) noexcept
{
    return detail::find_row(begin, column.column_index, value);
}

template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds>
inline raw_cursor<TableId> md_find_row_from_cursor(cursor<TableId> begin, column<TableId, raw_cursor<TargetTableIds...>> column, mdToken tk) noexcept
{
    if (!((ExtractTokenType(tk) == TargetTableIds) || ...))
        return {};
    if constexpr (sizeof...(TargetTableIds) == 1)
        tk = (detail::RidFromToken)(tk);
    return detail::find_row(begin, column.column_index, tk);
}

template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds, mdtable_id_t SearchTableId>
inline raw_cursor<TableId> md_find_row_from_cursor(cursor<TableId> begin, column<TableId, raw_cursor<TargetTableIds...>> column, cursor<SearchTableId> value) noexcept
{
    static_assert(((SearchTableId == TargetTableIds) || ...), "The value must belong to a table referenced by the column.");
    mdToken tk;
    if (!::md_cursor_to_token(value, &tk))
        return {};
    return md_find_row_from_cursor(begin, column, tk);
}

template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds>
inline md_range_result_t md_find_range_from_cursor(cursor<TableId> begin,
    column<TableId, raw_cursor<TargetTableIds...>> column, mdToken tk,
    raw_cursor<TableId>& start, uint32_t* count) noexcept
{
    if (!((ExtractTokenType(tk) == TargetTableIds) || ...))
        return MD_RANGE_NOT_FOUND;
    if constexpr (sizeof...(TargetTableIds) == 1)
        tk = (detail::RidFromToken)(tk);
    mdcursor_t c;
    auto result = ::md_find_range_from_cursor(begin, column.column_index, tk, &c, count);
    if (result == MD_RANGE_FOUND)
        start = raw_cursor<TableId>{c};
    return result;
}

template<mdtable_id_t TableId, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline raw_cursor<TableId> md_find_row_from_cursor(cursor<TableId> begin, column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, indirect_cursor<DirectTableId, IndirectTableId> const& value) noexcept
{
    mdToken tk;
    if (!value.get_token(tk) || (ExtractTokenType(tk) != DirectTableId && ExtractTokenType(tk) != IndirectTableId))
        return {};
    return detail::find_row(begin, column.column_index, (detail::RidFromToken)(tk));
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

} // namespace dnmd

#endif // _SRC_INC_DNMD_HPP_
