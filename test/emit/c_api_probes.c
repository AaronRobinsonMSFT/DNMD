#include "c_api_probes.h"

extern mdcxt_t* extract_mdcxt(mdhandle_t handle);
extern uint8_t get_table_column_count(mdtable_id_t table);

uint8_t test_c_column_count(mdtable_id_t table)
{
    return get_table_column_count(table);
}

static bool position(mdhandle_t handle, mdToken token, mdcursor_t* cursor)
{
    mdcxt_t* context = extract_mdcxt(handle);
    uint32_t table = ExtractTokenType(token);
    uint32_t row = token & 0x00ffffffu;
    if (context == NULL || table >= mdtid_End || row > context->tables[table].row_count + 1)
        return false;
    cursor->_reserved1 = (intptr_t)&context->tables[table];
    cursor->_reserved2 = row;
    return true;
}

bool test_c_token_is_valid(mdhandle_t handle, mdToken token)
{
    mdcursor_t cursor;
    bool (*convert)(mdhandle_t, mdToken, mdcursor_t*) = md_token_to_cursor;
    return convert(handle, token, &cursor);
}

size_t test_c_table_size(void)
{
    return sizeof(mdtable_t);
}

bool test_c_table_is_initialized(mdhandle_t handle, mdtable_id_t table)
{
    mdcxt_t* context = extract_mdcxt(handle);
    return context != NULL && table >= mdtid_First && table < mdtid_End
        && context->tables[table].is_initialized;
}

bool test_c_first_cursor(mdhandle_t handle, mdtable_id_t table, mdToken* token, uint32_t* count)
{
    mdcursor_t cursor;
    bool (*create)(mdhandle_t, mdtable_id_t, mdcursor_t*, uint32_t*) = md_create_cursor;
    return create(handle, table, &cursor, count) && md_cursor_to_token(cursor, token);
}

bool test_c_column_token(mdhandle_t handle, mdToken row, col_index_t column, mdToken* token)
{
    mdcursor_t cursor;
    return position(handle, row, &cursor) && md_get_column_value_as_token(cursor, column, token);
}

bool test_c_column_cursor(mdhandle_t handle, mdToken row, col_index_t column, mdToken* token)
{
    mdcursor_t cursor;
    mdcursor_t target;
    return position(handle, row, &cursor) && md_get_column_value_as_cursor(cursor, column, &target)
        && md_cursor_to_token(target, token);
}

bool test_c_column_constant(mdhandle_t handle, mdToken row, col_index_t column, uint32_t* value)
{
    mdcursor_t cursor;
    return position(handle, row, &cursor) && md_get_column_value_as_constant(cursor, column, value);
}

bool test_c_column_blob_is_valid(mdhandle_t handle, mdToken row, col_index_t column)
{
    mdcursor_t cursor;
    uint8_t const* bytes;
    uint32_t length;
    return position(handle, row, &cursor) && md_get_column_value_as_blob(cursor, column, &bytes, &length);
}

bool test_c_resolve(mdhandle_t handle, mdToken source, mdToken* target)
{
    mdcursor_t cursor;
    mdcursor_t resolved;
    return position(handle, source, &cursor) && md_resolve_indirect_cursor(cursor, &resolved)
        && md_cursor_to_token(resolved, target);
}

md_range_result_t test_c_find_range(mdhandle_t handle, mdToken row, col_index_t column,
    uint32_t key, mdToken* start, uint32_t* count)
{
    mdcursor_t cursor;
    mdcursor_t found;
    if (!position(handle, row, &cursor))
        return MD_RANGE_NOT_FOUND;
    md_range_result_t result = md_find_range_from_cursor(cursor, column, key, &found, count);
    if (result == MD_RANGE_FOUND && !md_cursor_to_token(found, start))
        return MD_RANGE_NOT_FOUND;
    return result;
}

bool test_c_move(mdhandle_t handle, mdToken source, int32_t delta, mdToken* target)
{
    mdcursor_t cursor;
    if (!position(handle, source, &cursor))
        return false;
    bool moved = md_cursor_move(&cursor, delta);
    if (!md_cursor_to_token(cursor, target))
        return false;
    return moved;
}

bool test_c_append(mdhandle_t handle, mdtable_id_t table, mdToken* added)
{
    mdcursor_t row = { 0 };
    bool result = md_append_row(handle, table, &row);
    if (row._reserved1 != 0)
        md_cursor_to_token(row, added);
    return result;
}

bool test_c_insert(mdhandle_t handle, mdToken relative, bool after, mdToken* added)
{
    mdcursor_t position_cursor;
    if (!position(handle, relative, &position_cursor))
        return false;
    mdcursor_t row = { 0 };
    bool result = after ? md_insert_row_after(position_cursor, &row) : md_insert_row_before(position_cursor, &row);
    if (row._reserved1 != 0)
        md_cursor_to_token(row, added);
    return result;
}

bool test_c_add_list(mdhandle_t handle, mdToken owner, col_index_t list, bool sorted,
    col_index_t sort_column, uint32_t sort_value, mdToken* added)
{
    mdcursor_t parent;
    if (!position(handle, owner, &parent))
        return false;
    mdcursor_t row = { 0 };
    bool result = sorted
        ? md_add_new_row_to_sorted_list(parent, list, sort_column, sort_value, &row)
        : md_add_new_row_to_list(parent, list, &row);
    if (row._reserved1 != 0)
        md_cursor_to_token(row, added);
    return result;
}

bool test_c_commit(mdhandle_t handle, mdToken token)
{
    mdcursor_t row;
    if (!position(handle, token, &row) || !((mdtable_t*)row._reserved1)->is_adding_new_row)
        return false;
    md_commit_row_add(row);
    return true;
}
