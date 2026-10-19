#ifndef _SRC_INC_DNMD_CURSORS_HPP_
#define _SRC_INC_DNMD_CURSORS_HPP_

#include "dnmd_columns.hpp"
#include <utility>

namespace dnmd
{

namespace detail
{
template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline bool resolve_indirect_cursor(mdcursor_t source, mdcursor_t& target) noexcept;
}

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

        bool get_parent_token(mdToken& tk) const noexcept;

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

        bool set_column_value(column<TableId, column_type_constant> column, uint32_t value) const noexcept;

        bool set_column_value(column<TableId, column_type_string> column, char const* value) const noexcept;

        bool set_column_value(column<TableId, column_type_blob> column, uint8_t const* value, uint32_t length) const noexcept;

        bool set_column_value(column<TableId, column_type_guid> column, mdguid_t value) const noexcept;

        bool set_column_value(column<TableId, column_type_userstring> column, char16_t const* value) const noexcept;

        template<mdtable_id_t... TargetTableIds>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, mdToken value) const noexcept;

        template<mdtable_id_t... TargetTableIds, mdtable_id_t ValueTableId>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, cursor<ValueTableId> value) const noexcept;

        template<mdtable_id_t... TargetTableIds, mdtable_id_t... ValueTableIds>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, raw_cursor<ValueTableIds...> const& value) const noexcept;

        template<mdtable_id_t... TargetTableIds, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, indirect_cursor<DirectTableId, IndirectTableId> const& value) const noexcept;

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, mdToken value) const noexcept;

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t ValueTableId>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, cursor<ValueTableId> value) const noexcept;

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t... ValueTableIds>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, raw_cursor<ValueTableIds...> const& value) const noexcept;

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, indirect_cursor<DirectTableId, IndirectTableId> const& value) const noexcept;

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

} // namespace dnmd

// Row reads also complete indirect-cursor construction.
#include "dnmd_read.hpp"

#endif // _SRC_INC_DNMD_CURSORS_HPP_
