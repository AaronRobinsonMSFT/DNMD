#ifndef _SRC_INC_DNMD_HPP_
#define _SRC_INC_DNMD_HPP_

#include "dnmd.h"
#include <memory>

struct mdhandle_deleter_t final
{
    using pointer = mdhandle_t;
    void operator()(mdhandle_t handle)
    {
        ::md_destroy_handle(handle);
    }
};

// C++ lifetime wrapper for mdhandle_t type
using mdhandle_ptr = std::unique_ptr<mdhandle_t, mdhandle_deleter_t>;

struct md_added_row_t final
{
private:
    mdcursor_t new_row;
public:
    md_added_row_t() = default;
    explicit md_added_row_t(mdcursor_t row) : new_row{ row } {}
    md_added_row_t(md_added_row_t const& other) = delete;
    md_added_row_t(md_added_row_t&& other)
    {
        *this = std::move(other);
    }

    md_added_row_t& operator=(md_added_row_t const& other) = delete;
    md_added_row_t& operator=(md_added_row_t&& other)
    {
        new_row = other.new_row;
        other.new_row = {}; // Clear the other's row so we don't double-commit.
        return *this;
    }

    ~md_added_row_t()
    {
        md_commit_row_add(new_row);
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

enum class md_column_type
{
    blob,
    string,
    guid,
    userstring,
    constant,
};


template<mdtable_id_t TableId>
struct mdcursor;

template<mdtable_id_t... TableIds>
struct mdcoded_index;

template<mdtable_id_t TableId, md_column_type ColumnType>
struct mdcolumn final
{
    template<mdtable_id_t TableId>
    friend struct mdcursor;

    col_index_t column_index;
    public:
        constexpr mdcolumn(col_index_t col)
            : column_index{ col }
        {
        }
};

template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds>
struct mdcolumn final
{
    template<mdtable_id_t TableId>
    friend struct mdcursor;
    col_index_t column_index;
    public:
        constexpr mdcolumn(col_index_t col)
            : column_index{ col }
        {
        }
};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct mdcursor_indirect;

template<mdtable_id_t TableId>
struct mdcolumn final
{
    template<mdtable_id_t TableId>
    friend struct mdcursor;
    col_index_t column_index;
    public:
        constexpr mdcolumn(col_index_t col)
            : column_index{ col }
        {
        }
};

template<mdtable_id_t TableId>
struct mdcursor final
{
    private:
        mdcursor_t cursor;
    public:
        explicit mdcursor(mdcursor_t c) : cursor{ c }
        {
            mdtoken tk;
            if (md_cursor_to_token(cursor, &tk))
            {
                mdtable_id_t table_id = ExtractTokenType(tk);
                assert(table_id == TableId);
            }
        }

        operator mdcursor_t()
        {
            return cursor;
        }

        template<mdtable_id_t TargetTableId>
        bool get_column_value(mdcolumn<TableId, TargetTableId> column, mdcursor<TargetTableId>& target_cursor)
        {
            return md_get_column_value_as_cursor(cursor, column.column_index, &target_cursor.cursor);
        }

        template<mdtable_id_t... TargetTableIds>
        bool get_column_value(mdcolumn<TableId, TargetTableIds...> column, mdcoded_index<TargetTableIds...>& target_coded_index)
        {
            return md_get_column_value_as_cursor(cursor, column.column_index, &target_coded_index.cursor);
        }

        template<mdtable_id_t TargetTableId>
        bool get_column_value_as_token(mdcolumn<TableId, TargetTableId> column, mdToken& tk)
        {
            return md_get_column_value_as_token(cursor, column.column_index, &tk);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool get_column_value(mdcolumn<TableId, mdcursor_indirect<DirectTableId, IndirectTableId>> column, mdcursor_indirect<DirectTableId, IndirectTableId>& target_cursor)
        {
            mdcursor<IndirectTableId> indirect_cursor;
            return md_get_column_value_as_cursor(cursor, column.column_index, &indirect_cursor.cursor);
        }

        bool get_column_value(mdcolumn<TableId, md_column_type::blob> column, uint8_t const** blob, uint32_t* blob_len)
        {
            return md_get_column_value_as_blob(cursor, column.column_index, blob, blob_len);
        }

        bool get_column_value(mdcolumn<TableId, md_column_type::guid> column, mdguid_t* guid)
        {
            return md_get_column_value_as_guid(cursor, column.column_index, guid);
        }

        bool get_column_value(mdcolumn<TableId, md_column_type::userstring> column, mduserstring_t* userstring)
        {
            return md_get_column_value_as_userstring(cursor, column.column_index, userstring);
        }

        bool get_column_value(mdcolumn<TableId, md_column_type::string> column, char const** str)
        {
            return md_get_column_value_as_utf8(cursor, column.column_index, str);
        }
};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct mdcursor_indirect
{
    private:
        mdcursor_t cursor;
    public:
        explicit mdcursor_indirect(mdcursor_t c) : cursor{ c }
        {
            mdtoken tk;
            if (md_cursor_to_token(cursor, &tk))
            {
                mdtable_id_t table_id = ExtractTokenType(tk);
                assert(table_id == DirectTableId || table_id == IndirectTableId);
            }
        }

        operator mdcursor_t()
        {
            return cursor;
        }

        bool try_get(mdcursor<DirectTableId>& direct_cursor)
        {
            mdtoken tk;
            if (!md_cursor_to_token(cursor, &tk))
                return false;

            mdcursor_t target_cursor;
            if (!md_resolve_indirect_cursor(cursor, &target_cursor))
                return false;

            direct_cursor = mdcursor<DirectTableId>(target_cursor);
            return true;
        }
};

template<mdtable_id_t... TableIds>
struct mdcoded_index final
{
    private:
        mdcursor_t cursor;
    public:
        constexpr mdcoded_index(mdcursor_t c)
            : cursor{ c }
        {
        }
    
        template<mdtable_id_t CodedTableId>
        bool get_as(mdcursor<CodedTableId>& target_cursor)
        {
            static_assert(((CodedTableId == TableIds) || ...) , "CodedTableId must be one of the tables in the coded index.");
            mdtoken tk;
            if (!md_cursor_to_token(cursor, &tk))
                return false;

            if (ExtractTokenType(tk) != CodedTableId)
                return false;
            
            target_cursor = mdcursor<CodedTableId>(cursor);
            return true;
        }

        template<typename TCallable>
        void visit(TCallable&& callable)
        {
            (visit_impl<TableIds>(std::forward<TCallable>(callable)) , ...);
        }

    private:
        template<mdtable_id_t CodedTableId, typename TCallable>
        bool visit_impl(TCallable&& callable)
        {
            mdtoken tk;
            if (!md_cursor_to_token(cursor, &tk))
                return;
            if (ExtractTokenType(tk) != CodedTableId)
                return;
            callable(mdcursor<CodedTableId>(cursor));
        }
};

#endif // _SRC_INC_DNMD_HPP_
