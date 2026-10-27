#include "internal.h"

static bool is_row_sorted_with_next_row(md_key_info_t const* keys, uint8_t count_keys, mdtable_id_t table_id, mdcursor_t row, mdcursor_t next_row)
{
    // We have a previous row, let's validate that it's sorted.
    for (uint8_t i = 0; i < count_keys; i++)
    {
        col_index_t key_col = index_to_col(keys[i].index, table_id);

        access_cxt_t row_acxt;
        if (!create_access_context(&row, key_col, false, &row_acxt))
            return false;

        // Key columns can only be constant, index into a table, or a coded token index.
        // Heap offset columns cannot be keys.
        assert(row_acxt.col_details & (mdtc_constant | mdtc_idx_table | mdtc_idx_coded));

        access_cxt_t next_acxt;
        if (!create_access_context(&next_row, key_col, false, &next_acxt))
            return false;

        uint32_t row_value;
        if (!read_column_data(&row_acxt, &row_value))
            return false;

        uint32_t next_value;
        if (!read_column_data(&next_acxt, &next_value))
            return false;

        bool column_sorted = keys[i].descending ? (row_value >= next_value) : (row_value <= next_value);
        if (!column_sorted)
            return false;
    }
    return true;
}

void update_column_sort_order(mdcursor_t c, col_index_t col_idx)
{
    mdtable_t* table = CursorTable(&c);
    if (!table->is_sorted || table->is_adding_new_row)
        return;

    md_key_info_t const* keys;
    uint8_t key_count = get_table_keys(table->table_id, &keys);
    uint8_t key_idx;
    for (key_idx = 0; key_idx < key_count; ++key_idx)
    {
        if (keys[key_idx].index == col_to_index(col_idx, table))
            break;
    }
    if (key_idx == key_count)
        return;

    mdcursor_t current_row = c;
    bool success = md_cursor_next(&current_row);
    assert(success);
    (void)success;
    mdcursor_t prior_row = current_row;
    if (md_cursor_move(&prior_row, -1) && !CursorNull(&prior_row))
    {
        if (!is_row_sorted_with_next_row(keys, key_count, table->table_id, prior_row, current_row))
        {
            table->is_sorted = false;
            return;
        }
    }

    mdcursor_t next_row = current_row;
    if (md_cursor_next(&next_row) && !CursorEnd(&next_row))
    {
        if (!is_row_sorted_with_next_row(keys, key_count, table->table_id, current_row, next_row))
            table->is_sorted = false;
    }
}

static bool set_column_value_as_token_or_cursor(mdcursor_t c, uint32_t col_idx, mdToken const* tk, mdcursor_t const* cursor)
{
    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, true, &acxt))
        return false;

    // If we can't write on the underlying table, then fail.
    if (acxt.writable_data == NULL)
        return false;

    // If this isn't an index column, then fail.
    if (!(acxt.col_details & (mdtc_idx_table | mdtc_idx_coded)))
        return false;

    mdToken token;
    if (tk != NULL)
    {
        token = *tk;
    }
    else
    {
        if (!md_cursor_to_token(*cursor, &token))
            return false;
    }

#ifdef DNMD_PORTABLE_PDB
    if (has_pdb(acxt.table->cxt))
    {
        uint32_t table_row = RidFromToken(token);
        mdtable_id_t table_id = ExtractTokenType(token);
        if (table_id < mdtid_FirstPdb)
        {
            if (!update_referenced_type_system_table_row_count(acxt.table->cxt, table_id, table_row))
                return false;
        }
    }
#endif

    uint32_t raw;
    if (acxt.col_details & mdtc_idx_table)
    {
        uint32_t table_row = RidFromToken(token);
        mdtable_id_t table_id = ExtractTokenType(token);
        // The raw value is the row index into the table that
        // is embedded in the column details.
        // Return an error if the provided token does not point to the right table.
        if (ExtractTable(acxt.col_details) != table_id)
            return false;
        raw = table_row;
    }
    else
    {
        assert(acxt.col_details & mdtc_idx_coded);
        if (!compose_coded_index(token, acxt.col_details, &raw))
            return false;
    }

    if (!write_column_data(&acxt, raw))
        return false;

    update_column_sort_order(c, (col_index_t)col_idx);
    return true;
}

bool md_set_column_value_as_token(mdcursor_t c, col_index_t col, mdToken tk)
{
    return set_column_value_as_token_or_cursor(c, col_to_index(col, CursorTable(&c)), &tk, NULL);
}

bool md_set_column_value_as_cursor(mdcursor_t c, col_index_t col, mdcursor_t cursor)
{
    return set_column_value_as_token_or_cursor(c, col_to_index(col, CursorTable(&c)), NULL, &cursor);
}

bool md_set_column_value_as_constant(mdcursor_t c, col_index_t col_idx, uint32_t constant)
{
    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, true, &acxt))
        return false;

    // If this isn't an constant column, then fail.
    if (!(acxt.col_details & mdtc_constant))
        return false;

    if (!write_column_data(&acxt, constant))
        return false;

    update_column_sort_order(c, col_idx);
    return true;
}

#ifdef DEBUG_COLUMN_SORTING
static void validate_column_is_not_key(mdtable_t const* table, col_index_t col_idx)
{
    md_key_info_t const* keys = NULL;
    uint8_t key_count = get_table_keys(table->table_id, &keys);
    for (uint8_t i = 0; i < key_count; i++)
    {
        if (keys[i].index == col_to_index(col_idx, table))
            assert(!"Sorted columns cannot be heap references");
    }
}
#endif

// Set a column value as an existing offset into a heap.
bool set_column_value_as_heap_offset(mdcursor_t c, col_index_t col_idx, uint32_t offset)
{
    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, true, &acxt))
        return false;

    // If this isn't a heap index column, then fail.
    if (!(acxt.col_details & mdtc_idx_heap))
        return false;

    mdstream_t const* heap = get_heap_by_id(acxt.table->cxt, ExtractHeapType(acxt.col_details));
    if (heap == NULL)
        return false;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_is_not_key(acxt.table, col_idx);
#endif

    if (!write_column_data(&acxt, offset))
        return false;

    return true;
}

bool md_set_column_value_as_utf8(mdcursor_t c, col_index_t col_idx, char const* str)
{
    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, true, &acxt))
        return false;

    // If this isn't an constant column, then fail.
    if (!(acxt.col_details & mdtc_hstring))
        return false;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_is_not_key(acxt.table, col_idx);
#endif

    uint32_t heap_offset;
    heap_offset = add_to_string_heap(acxt.table->cxt, str);

    if (heap_offset == 0 && str[0] != '\0')
        return false;

    if (!write_column_data(&acxt, heap_offset))
        return false;

    return true;
}

// TODO: These functions should not call set_column_value_as_heap_offset.
bool md_set_column_value_as_blob(mdcursor_t c, col_index_t col_idx, uint8_t const* blob, uint32_t blob_len)
{
    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, true, &acxt))
        return false;

    // If this isn't an constant column, then fail.
    if (!(acxt.col_details & mdtc_hblob))
        return false;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_is_not_key(acxt.table, col_idx);
#endif

    uint32_t heap_offset = add_to_blob_heap(acxt.table->cxt, blob, blob_len);

    if (heap_offset == 0 && blob_len > 0)
        return false;

    if (!write_column_data(&acxt, heap_offset))
        return false;

    return true;
}

bool md_set_column_value_as_guid(mdcursor_t c, col_index_t col_idx, mdguid_t guid)
{
    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, true, &acxt))
        return false;

    // If this isn't an constant column, then fail.
    if (!(acxt.col_details & mdtc_hguid))
        return false;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_is_not_key(acxt.table, col_idx);
#endif

    uint32_t index = add_to_guid_heap(acxt.table->cxt, guid);

    if (index == 0 && memcmp(&guid, &empty_guid, sizeof(mdguid_t)) != 0)
        return false;

    return set_column_value_as_heap_offset(c, col_idx, index);
}

bool md_set_column_value_as_userstring(mdcursor_t c, col_index_t col_idx, char16_t const* userstring)
{
    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, true, &acxt))
        return false;

    // If this isn't an constant column, then fail.
    if (!(acxt.col_details & mdtc_hus))
        return false;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_is_not_key(acxt.table, col_idx);
#endif

    uint32_t index = add_to_user_string_heap(CursorTable(&c)->cxt, userstring);

    if (index == 0 && userstring[0] != 0)
        return false;

    if (!write_column_data(&acxt, index))
        return false;

    return true;
}
