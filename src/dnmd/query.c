#include "internal.h"

static mdtable_t* CursorTable(mdcursor_t* c)
{
    assert(c != NULL);
    return (mdtable_t*)c->_reserved1;
}

static uint32_t CursorRow(mdcursor_t* c)
{
    assert(c != NULL);
    return RidFromToken(c->_reserved2);
}

static bool CursorNull(mdcursor_t* c)
{
    return CursorRow(c) == 0;
}

static bool CursorEnd(mdcursor_t* c)
{
    return (CursorTable(c)->row_count + 1) == CursorRow(c);
}

mdcursor_t create_cursor(mdtable_t* table, uint32_t row)
{
    assert(table != NULL && row <= (table->row_count + 1));
    mdcursor_t c;
    c._reserved1 = (intptr_t)table;
    c._reserved2 = row;
    return c;
}

static mdtable_t* type_to_table(mdcxt_t* cxt, mdtable_id_t table_id)
{
    assert(cxt != NULL);
    assert(0 <= table_id && table_id < MDTABLE_MAX_COUNT);
    return &cxt->tables[table_id];
}

bool md_create_cursor(mdhandle_t handle, mdtable_id_t table_id, mdcursor_t* cursor, uint32_t* count)
{
    if (count == NULL)
        return false;

    // Set the token to the first row.
    // If the table is empty, the call will return false.
    if (!md_token_to_cursor(handle, (CreateTokenType(table_id) | 1), cursor))
        return false;

    *count = CursorTable(cursor)->row_count;
    return true;
}

static bool cursor_move_no_checks(mdcursor_t* c, int32_t delta)
{
    assert(c != NULL);

    mdtable_t* table = CursorTable(c);
    uint32_t row = CursorRow(c);
    row += delta;
    // Indices into tables begin at 1 - see II.22.
    // They can also point to index n+1, which
    // indicates the end.
    if (row == 0 || row > (table->row_count + 1))
        return false;

    *c = create_cursor(table, row);
    return true;
}

bool md_cursor_move(mdcursor_t* c, int32_t delta)
{
    if (c == NULL || CursorTable(c) == NULL)
        return false;
    return cursor_move_no_checks(c, delta);
}

bool md_cursor_next(mdcursor_t* c)
{
    return md_cursor_move(c, 1);
}

bool md_token_to_cursor(mdhandle_t handle, mdToken tk, mdcursor_t* c)
{
    assert(c != NULL);

    mdcxt_t* cxt = extract_mdcxt(handle);
    if (cxt == NULL)
        return false;

    mdtable_id_t table_id = ExtractTokenType(tk);
    if (table_id >= MDTABLE_MAX_COUNT)
        return false;

    mdtable_t* table = type_to_table(cxt, table_id);

    // Indices into tables begin at 1 - see II.22.
    uint32_t row = RidFromToken(tk);
    if (row == 0 || row > table->row_count)
        return false;

    *c = create_cursor(table, row);
    return true;
}

bool md_cursor_to_token(mdcursor_t c, mdToken* tk)
{
    assert(tk != NULL);

    mdtable_t* table = CursorTable(&c);
    if (table == NULL)
    {
        *tk = mdTokenNil;
        return true;
    }

    mdToken row = RidFromToken(CursorRow(&c));
    *tk = CreateTokenType(table->table_id) | row;
    return (row <= table->row_count);
}

mdhandle_t md_extract_handle_from_cursor(mdcursor_t c)
{
    mdtable_t* table = CursorTable(&c);
    if (table == NULL)
        return NULL;
    return table->cxt;
}

bool md_walk_user_string_heap(mdhandle_t handle, mduserstringcursor_t* cursor, mduserstring_t* str, uint32_t* offset)
{
    mdcxt_t* cxt = extract_mdcxt(handle);
    if (cxt == NULL)
        return -1;

    assert(cursor != NULL);
    *offset = (uint32_t)*cursor;
    size_t next_offset;
    if (!try_get_user_string(cxt, *offset, str, &next_offset))
        return false;

    *cursor = (mduserstringcursor_t)next_offset;
    return true;
}

typedef struct _query_cxt_t
{
    mdtable_t* table;
    mdtcol_t col_details;
    uint8_t const* start;
    uint8_t const* data;
    uint8_t* writable_data;
    uint8_t const* end;
    size_t data_len;
    uint32_t data_len_col;
    uint32_t next_row_stride;
} query_cxt_t;

static uint8_t col_to_index(col_index_t col_idx, mdtable_t const* table)
{
    assert(table != NULL);
    uint32_t idx = (uint32_t)col_idx;
#ifdef DEBUG_TABLE_COLUMN_LOOKUP
    mdtable_id_t tgt_table_id = col_idx >> 8;
    if (tgt_table_id != table->table_id)
    {
        assert(!"Unexpected table/column indexing");
        return false;
    }
    idx = (col_idx & 0xff);
#else
    (void)table;
#endif
    return (uint8_t)idx;
}

static col_index_t index_to_col(uint8_t idx, mdtable_id_t table_id)
{
#ifdef DEBUG_TABLE_COLUMN_LOOKUP
    return (col_index_t)((table_id << 8) | idx);
#else
    (void)table_id;
    return (col_index_t)idx;
#endif
}

static bool create_query_context(mdcursor_t* cursor, col_index_t col_idx, uint32_t row_count, bool make_writable, query_cxt_t* qcxt)
{
    assert(qcxt != NULL);
    mdtable_t* table = CursorTable(cursor);
    if (table == NULL)
        return false;

    uint32_t row = CursorRow(cursor);
    if (row == 0 || row > table->row_count)
        return false;

    uint8_t idx = col_to_index(col_idx, table);
    assert(idx < table->column_count);

    // Metadata row indexing is 1-based.
    row--;
    qcxt->table = table;
    qcxt->col_details = table->column_details[idx];

    // Compute the offset into the first row.
    uint32_t offset = ExtractOffset(qcxt->col_details);
    qcxt->start = qcxt->data = table->data.ptr + (row * table->row_size_bytes) + offset;

    if (make_writable)
    {
        qcxt->writable_data = get_writable_table_data(table, make_writable);
        qcxt->writable_data = qcxt->writable_data + (row * table->row_size_bytes) + offset;
    }
    else
        qcxt->writable_data = NULL;

    // Compute the beginning of the row after the last valid row.
    uint32_t last_row = row + row_count;
    if (last_row > table->row_count)
        last_row = table->row_count;
    qcxt->end = table->data.ptr + (last_row * table->row_size_bytes);

    // Limit the data read to the width of the column
    qcxt->data_len_col = (qcxt->col_details & mdtc_b2) ? 2 : 4;
    qcxt->data_len = qcxt->data_len_col;

    // Compute the next row stride. Take the total length and substract
    // the data length for the column to get at the next row's column.
    qcxt->next_row_stride = table->row_size_bytes - qcxt->data_len_col;
    return true;
}

static void change_query_context_target_col(query_cxt_t* qcxt, col_index_t col_idx)
{
    assert(qcxt != NULL);
    uint8_t idx = col_to_index(col_idx, qcxt->table);
    assert(idx < qcxt->table->column_count);


    int32_t offset_diff = ExtractOffset(qcxt->table->column_details[idx]) - ExtractOffset(qcxt->col_details);

    qcxt->col_details = qcxt->table->column_details[idx];
    qcxt->data_len_col = (qcxt->col_details & mdtc_b2) ? 2 : 4;
    qcxt->data += offset_diff;

    if (qcxt->writable_data != NULL)
        qcxt->writable_data += offset_diff;

    qcxt->data_len = qcxt->data_len_col;
    qcxt->next_row_stride = qcxt->table->row_size_bytes - qcxt->data_len_col;
}

static bool read_column_data(query_cxt_t* qcxt, uint32_t* data)
{
    assert(qcxt != NULL && data != NULL);
    *data = 0;

    if (qcxt->writable_data != NULL)
        qcxt->writable_data += (qcxt->col_details & mdtc_b2) ? 2 : 4;

    return (qcxt->col_details & mdtc_b2)
        ? read_u16(&qcxt->data, &qcxt->data_len, (uint16_t*)data)
        : read_u32(&qcxt->data, &qcxt->data_len, data);
}

static bool prev_row(query_cxt_t* qcxt)
{
    assert(qcxt != NULL);
    // Go back the stride + the length of the column data to get to the end
    // of the column, and then go back the length of the column data to get
    // to the start of the column data.
    uint32_t movement = qcxt->next_row_stride + qcxt->data_len_col * 2;
    qcxt->data -= movement;
    if (qcxt->writable_data != NULL)
        qcxt->writable_data -= movement;

    // Restore the data length of the column data.
    qcxt->data_len = qcxt->data_len_col;
    return qcxt->data > qcxt->start;
}

static bool next_row(query_cxt_t* qcxt)
{
    assert(qcxt != NULL);
    qcxt->data += qcxt->next_row_stride;

    if (qcxt->writable_data != NULL)
        qcxt->writable_data += qcxt->next_row_stride;

    // Restore the data length of the column data.
    qcxt->data_len = qcxt->data_len_col;
    return qcxt->data < qcxt->end;
}

static int32_t get_column_value_as_token_or_cursor(mdcursor_t* c, uint32_t col_idx, uint32_t out_length, mdToken* tk, mdcursor_t* cursor)
{
    assert(c != NULL && out_length != 0 && (tk != NULL || cursor != NULL));

    query_cxt_t qcxt;
    if (!create_query_context(c, col_idx, out_length, false, &qcxt))
        return -1;

    // If this isn't an index column, then fail.
    if (!(qcxt.col_details & (mdtc_idx_table | mdtc_idx_coded)))
        return -1;

    uint32_t table_row;
    mdtable_id_t table_id;

    uint32_t raw;
    int32_t read_in = 0;
    do
    {
        if (!read_column_data(&qcxt, &raw))
            return -1;

        if (qcxt.col_details & mdtc_idx_table)
        {
            // The raw value is the row index into the table that
            // is embedded in the column details.
            table_row = RidFromToken(raw);
            table_id = ExtractTable(qcxt.col_details);
        }
        else
        {
            assert(qcxt.col_details & mdtc_idx_coded);
            if (!decompose_coded_index(raw, qcxt.col_details, &table_id, &table_row))
                return -1;
        }

        if (0 > table_id || table_id >= MDTABLE_MAX_COUNT)
            return -1;

        mdtable_t* table;
        if (tk != NULL)
        {
            tk[read_in] = CreateTokenType(table_id) | table_row;
        }
        else
        {
            // Returning a cursor means pointing directly into a table
            // so we must validate the cursor is valid prior to creation.
            table = type_to_table(qcxt.table->cxt, table_id);

            // Indices into tables begin at 1 - see II.22.
            // However, tables can contain a row ID of 0 to
            // indicate "none" or point 1 past the end.
            if (table_row > table->row_count + 1)
                return -1;

            assert(cursor != NULL);
            cursor[read_in] = create_cursor(table, table_row);
        }
        read_in++;
    } while (out_length > 1 && next_row(&qcxt));

    return read_in;
}

int32_t md_get_column_value_as_token(mdcursor_t c, col_index_t col_idx, uint32_t out_length, mdToken* tk)
{
    if (out_length == 0)
        return 0;
    assert(tk != NULL);
    return get_column_value_as_token_or_cursor(&c, col_idx, out_length, tk, NULL);
}

int32_t md_get_column_value_as_cursor(mdcursor_t c, col_index_t col_idx, uint32_t out_length, mdcursor_t* cursor)
{
    if (out_length == 0)
        return 0;
    assert(cursor != NULL);
    return get_column_value_as_token_or_cursor(&c, col_idx, out_length, NULL, cursor);
}

// Forward declaration
//#define DNMD_DEBUG_FIND_TOKEN_OF_RANGE_ELEMENT
#ifdef DNMD_DEBUG_FIND_TOKEN_OF_RANGE_ELEMENT
static bool _validate_md_find_token_of_range_element(mdcursor_t expected, mdcursor_t begin, uint32_t count);
#endif // DNMD_DEBUG_FIND_TOKEN_OF_RANGE_ELEMENT

bool md_get_column_value_as_range(mdcursor_t c, col_index_t col_idx, mdcursor_t* cursor, uint32_t* count)
{
    assert(cursor != NULL);
    if (1 != get_column_value_as_token_or_cursor(&c, col_idx, 1, NULL, cursor))
        return false;

    // Check if the cursor is null or the end of the table
    if (CursorNull(cursor) || CursorEnd(cursor))
    {
        *count = 0;
        return true;
    }

    mdcursor_t nextMaybe = c;
    // Loop here as we may have a bunch of null cursors in the column.
    for (;;)
    {
        // The cursor into the current table remains valid,
        // we can safely move it at least one beyond the last.
        (void)cursor_move_no_checks(&nextMaybe, 1);

        // Check if we are at the end of the current table. If so,
        // ECMA states we use the remaining rows in the target table.
        if (CursorEnd(&nextMaybe))
        {
            // Add +1 for inclusive count.
            *count = CursorTable(cursor)->row_count - CursorRow(cursor) + 1;
        }
        else
        {
            // Examine the current table's next row value to find the
            // extrema of the target table range.
            mdcursor_t end;
            if (1 != md_get_column_value_as_cursor(nextMaybe, col_idx, 1, &end))
                return false;

            // The next row is a null cursor, which means we need to
            // check the next row in the current table.
            if (CursorNull(&end))
                continue;
            *count = CursorRow(&end) - CursorRow(cursor);
        }
        break;
    }

    // Use the results of this function to validate md_find_token_of_range_element()
#ifdef DNMD_DEBUG_FIND_TOKEN_OF_RANGE_ELEMENT
    (void)_validate_md_find_token_of_range_element(c, *cursor, *count);
#endif // DNMD_DEBUG_FIND_TOKEN_OF_RANGE_ELEMENT

    return true;
}

int32_t md_get_column_value_as_constant(mdcursor_t c, col_index_t col_idx, uint32_t out_length, uint32_t* constant)
{
    if (out_length == 0)
        return 0;
    assert(constant != NULL);

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, out_length, false, &qcxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(qcxt.col_details & mdtc_constant))
        return -1;

    int32_t read_in = 0;
    do
    {
        if (!read_column_data(&qcxt, &constant[read_in]))
            return -1;
        read_in++;
    } while (out_length > 1 && next_row(&qcxt));

    return read_in;
}

int32_t md_get_column_value_as_utf8(mdcursor_t c, col_index_t col_idx, uint32_t out_length, char const** str)
{
    if (out_length == 0)
        return 0;
    assert(str != NULL);

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, out_length, false, &qcxt))
        return -1;

    // If this isn't a #String column, then fail.
    if (!(qcxt.col_details & mdtc_hstring))
        return -1;

    uint32_t offset;
    int32_t read_in = 0;
    do
    {
        if (!read_column_data(&qcxt, &offset))
            return -1;
        if (!try_get_string(qcxt.table->cxt, offset, &str[read_in]))
            return -1;
        read_in++;
    } while (out_length > 1 && next_row(&qcxt));

    return read_in;
}

int32_t md_get_column_value_as_userstring(mdcursor_t c, col_index_t col_idx, uint32_t out_length, mduserstring_t* strings)
{
    if (out_length == 0)
        return 0;
    assert(strings != NULL);

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, out_length, false, &qcxt))
        return -1;

    // If this isn't a #US column, then fail.
    if (!(qcxt.col_details & mdtc_hus))
        return -1;

    size_t unused;
    uint32_t offset;
    int32_t read_in = 0;
    do
    {
        if (!read_column_data(&qcxt, &offset))
            return -1;
        if (!try_get_user_string(qcxt.table->cxt, offset, &strings[read_in], &unused))
            return -1;
        read_in++;
    } while (out_length > 1 && next_row(&qcxt));

    return read_in;
}

int32_t md_get_column_value_as_blob(mdcursor_t c, col_index_t col_idx, uint32_t out_length, uint8_t const** blob, uint32_t* blob_len)
{
    if (out_length == 0)
        return 0;
    assert(blob != NULL && blob_len != NULL);

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, out_length, false, &qcxt))
        return -1;

    // If this isn't a #Blob column, then fail.
    if (!(qcxt.col_details & mdtc_hblob))
        return -1;

    uint32_t offset;
    int32_t read_in = 0;
    do
    {
        if (!read_column_data(&qcxt, &offset))
            return -1;
        if (!try_get_blob(qcxt.table->cxt, offset, &blob[read_in], &blob_len[read_in]))
            return -1;
        read_in++;
    } while (out_length > 1 && next_row(&qcxt));

    return read_in;
}

int32_t md_get_column_value_as_guid(mdcursor_t c, col_index_t col_idx, uint32_t out_length, md_guid_t* guid)
{
    if (out_length == 0)
        return 0;
    assert(guid != NULL);

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, out_length, false, &qcxt))
        return -1;

    // If this isn't a #GUID column, then fail.
    if (!(qcxt.col_details & mdtc_hguid))
        return -1;

    uint32_t idx;
    int32_t read_in = 0;
    do
    {
        if (!read_column_data(&qcxt, &idx))
            return -1;
        if (!try_get_guid(qcxt.table->cxt, idx, &guid[read_in]))
            return -1;
        read_in++;
    } while (out_length > 1 && next_row(&qcxt));

    return read_in;
}

typedef int32_t(*md_bcompare_t)(void const* key, void const* row, void*);

// Since MSVC doesn't have a C11 compatible bsearch_s, defining one below.
// Ideally we would use the one in the standard so the signature is designed
// to match what should eventually exist.
static void const* md_bsearch(
    void const* key,
    void const* base,
    rsize_t count,
    rsize_t element_size,
    md_bcompare_t cmp,
    void* cxt)
{
    assert(key != NULL && base != NULL);
    while (count > 0)
    {
        void const* row = (uint8_t const*)base + (element_size * (count / 2));
        int32_t res = cmp(key, row, cxt);
        if (res == 0)
            return row;

        if (count == 1)
        {
            break;
        }
        else if (res < 0)
        {
            count /= 2;
        }
        else
        {
            base = row;
            count -= count / 2;
        }
    }
    return NULL;
}

static void const* md_lsearch(
    void const* key,
    void const* base,
    rsize_t count,
    rsize_t element_size,
    md_bcompare_t cmp,
    void* cxt)
{
    assert(key != NULL && base != NULL);
    void const* row = base;
    for (rsize_t i = 0; i < count; ++i)
    {
        int32_t res = cmp(key, row, cxt);
        if (res == 0)
            return row;

        // Onto the next row.
        row = (uint8_t const*)row + element_size;
    }
    return NULL;
}

typedef struct _find_cxt_t
{
    uint32_t col_offset;
    uint32_t data_len;
    mdtcol_t col_details;
} find_cxt_t;

static bool create_find_context(mdtable_t* table, col_index_t col_idx, find_cxt_t* fcxt)
{
    assert(table != NULL && fcxt != NULL);

    uint8_t idx = col_to_index(col_idx, table);
    assert(idx < MDTABLE_MAX_COLUMN_COUNT);
    if (idx >= table->column_count)
        return false;

    mdtcol_t cd = table->column_details[idx];
    fcxt->col_offset = ExtractOffset(cd);
    fcxt->data_len = (cd & mdtc_b2) ? 2 : 4;
    fcxt->col_details = cd;
    return true;
}

static int32_t col_compare_2bytes(void const* key, void const* row, void* cxt)
{
    assert(key != NULL && row != NULL && cxt != NULL);

    find_cxt_t* fcxt = (find_cxt_t*)cxt;
    uint8_t const* col_data = (uint8_t const*)row + fcxt->col_offset;

    uint16_t const lhs = *(uint16_t*)key;
    uint16_t rhs = 0;
    size_t col_len = fcxt->data_len;
    assert(col_len == 2);
    bool success = read_u16(&col_data, &col_len, &rhs);
    assert(success && col_len == 0);
    (void)success;

    return (lhs == rhs) ? 0
        : (lhs < rhs) ? -1
        : 1;
}

static int32_t col_compare_4bytes(void const* key, void const* row, void* cxt)
{
    assert(key != NULL && row != NULL && cxt != NULL);

    find_cxt_t* fcxt = (find_cxt_t*)cxt;
    uint8_t const* col_data = (uint8_t const*)row + fcxt->col_offset;

    uint32_t const lhs = *(uint32_t*)key;
    uint32_t rhs = 0;
    size_t col_len = fcxt->data_len;
    assert(col_len == 4);
    bool success = read_u32(&col_data, &col_len, &rhs);
    assert(success && col_len == 0);
    (void)success;

    return (lhs == rhs) ? 0
        : (lhs < rhs) ? -1
        : 1;
}

static void const* cursor_to_row_bytes(mdcursor_t* c)
{
    assert(c != NULL && (CursorRow(c) > 0));
    // Indices into tables begin at 1 - see II.22.
    return &CursorTable(c)->data.ptr[(CursorRow(c) - 1) * CursorTable(c)->row_size_bytes];
}

static bool find_row_from_cursor(mdcursor_t begin, col_index_t idx, uint32_t* value, mdcursor_t* cursor)
{
    mdtable_t* table = CursorTable(&begin);
    if (table == NULL || cursor == NULL)
        return false;

    uint32_t first_row = CursorRow(&begin);
    // Indices into tables begin at 1 - see II.22.
    if (first_row == 0 || first_row > table->row_count)
        return false;

    find_cxt_t fcxt;
    if (!create_find_context(table, idx, &fcxt))
        return false;

    // If the value is for a coded index, update the value.
    if (fcxt.col_details & mdtc_idx_coded)
    {
        if (!compose_coded_index(*value, fcxt.col_details, value))
            return false;
    }

    // Compute the starting row.
    void const* starting_row = cursor_to_row_bytes(&begin);
    md_bcompare_t cmp_func = fcxt.data_len == 2 ? col_compare_2bytes : col_compare_4bytes;
    // Add +1 for inclusive count - use binary search if sorted, otherwise linear.
    void const* row_maybe = (table->is_sorted)
        ? md_bsearch(value, starting_row, (table->row_count - first_row) + 1, table->row_size_bytes, cmp_func, &fcxt)
        : md_lsearch(value, starting_row, (table->row_count - first_row) + 1, table->row_size_bytes, cmp_func, &fcxt);
    if (row_maybe == NULL)
        return false;

    // Compute the found row.
    // Indices into tables begin at 1 - see II.22.
    assert(starting_row <= row_maybe);
    uint32_t row = (uint32_t)(((intptr_t)row_maybe - (intptr_t)starting_row) / table->row_size_bytes) + 1;
    if (row > table->row_count)
        return false;

    *cursor = create_cursor(table, row);
    return true;
}

bool md_find_row_from_cursor(mdcursor_t begin, col_index_t idx, uint32_t value, mdcursor_t* cursor)
{
    return find_row_from_cursor(begin, idx, &value, cursor);
}

md_range_result_t md_find_range_from_cursor(mdcursor_t begin, col_index_t idx, uint32_t value, mdcursor_t* start, uint32_t* count)
{
    // If the table isn't sorted, then a range isn't possible.
    mdtable_t* table = CursorTable(&begin);

    if (!table->is_sorted)
        return MD_RANGE_NOT_SUPPORTED;
        
    md_key_info const* keys;
    uint8_t keys_count = get_table_keys(table->table_id, &keys);
    if (keys_count == 0)
        return MD_RANGE_NOT_SUPPORTED;

    if (keys[0].index != col_to_index(idx, table))
        return MD_RANGE_NOT_SUPPORTED;

    // Currently all tables have ascending primary keys.
    // The algorithm below only works with ascending keys.
    assert(!keys[0].descending);

    // Look for any instance of the value.
    mdcursor_t found;
    if (!find_row_from_cursor(begin, idx, &value, &found))
        return MD_RANGE_NOT_FOUND;

    int32_t res;
    find_cxt_t fcxt;
    // This was already created and validated when the row was found.
    // We assume the data is still valid.
    (void)create_find_context(table, idx, &fcxt);
    md_bcompare_t cmp_func = fcxt.data_len == 2 ? col_compare_2bytes : col_compare_4bytes;

    // A valid value was found, so we are at least within the range.
    // Now find the extrema.
    *start = found;
    while (cursor_move_no_checks(start, -1))
    {
        // Since we are moving backwards in a sorted column,
        // the value should match or be greater.
        res = cmp_func(&value, cursor_to_row_bytes(start), &fcxt);
        assert(res >= 0);
        if (res > 0)
        {
            // Move forward to the start.
            (void)cursor_move_no_checks(start, 1);
            break;
        }
    }

    mdcursor_t end = found;
    while (cursor_move_no_checks(&end, 1) && !CursorEnd(&end))
    {
        // Since we are moving forwards in a sorted column,
        // the value should match or be less.
        res = cmp_func(&value, cursor_to_row_bytes(&end), &fcxt);
        assert(res <= 0);
        if (res < 0)
            break;
    }

    // Compute the row delta
    *count = CursorRow(&end) - CursorRow(start);
    return MD_RANGE_FOUND;
}

// Modeled after C11's bsearch_s. This API performs a binary search
// and instead of returning NULL if the value isn't found, the last
// compare result and row is returned.
static int32_t mdtable_bsearch_closest(
    void const* key,
    mdtable_t* table,
    find_cxt_t* fcxt,
    uint32_t* found_row)
{
    assert(table != NULL && found_row != NULL);
    void const* base = table->data.ptr;
    rsize_t count = table->row_count;
    rsize_t element_size = table->row_size_bytes;

    int32_t res = 0;
    void const* row = base;
    md_bcompare_t cmp_func = fcxt->data_len == 2 ? col_compare_2bytes : col_compare_4bytes;
    while (count > 0)
    {
        row = (uint8_t const*)base + (element_size * (count / 2));
        res = cmp_func(key, row, fcxt);
        if (res == 0 || count == 1)
            break;

        if (res < 0)
        {
            count /= 2;
        }
        else
        {
            base = row;
            count -= count / 2;
        }
    }

    // Compute the found row.
    // Indices into tables begin at 1 - see II.22.
    *found_row = (uint32_t)(((intptr_t)row - (intptr_t)table->data.ptr) / element_size) + 1;
    return res;
}

#ifdef DNMD_DEBUG_FIND_TOKEN_OF_RANGE_ELEMENT
// This function is used to validate the mapping logic between
// md_find_token_of_range_element() and md_get_column_value_as_range().
static bool _validate_md_find_token_of_range_element(mdcursor_t expected, mdcursor_t begin, uint32_t count)
{
#define IF_FALSE_RETURN(exp) { if (!(exp)) assert(false && #exp); return false; }
    mdToken expected_tk = 0;

    // The expected token is often just where the cursor presently points.
    // The Event and Property tables need to be queryed for the expected value.
    switch (CursorTable(&begin)->table_id)
    {
    case mdtid_Field:
    case mdtid_MethodDef:
    case mdtid_Param:
        IF_FALSE_RETURN(md_cursor_to_token(expected, &expected_tk));
        break;
    case mdtid_Event:
        IF_FALSE_RETURN(md_get_column_value_as_token(expected, mdtEventMap_Parent, 1, &expected_tk));
        break;
    case mdtid_Property:
        IF_FALSE_RETURN(md_get_column_value_as_token(expected, mdtPropertyMap_Parent, 1, &expected_tk));
        break;
    default:
        IF_FALSE_RETURN(!"Invalid table ID");
        break;
    }

    mdToken actual;
    mdcursor_t curr = begin;
    for (uint32_t i = 0; i < count; ++i)
    {
        IF_FALSE_RETURN(md_find_token_of_range_element(curr, &actual));
        IF_FALSE_RETURN(expected_tk == actual);
        IF_FALSE_RETURN(md_cursor_next(&curr));
    }
#undef IF_FALSE_RETURN

    return true;
}
#endif // DNMD_DEBUG_FIND_TOKEN_OF_RANGE_ELEMENT

static bool find_range_element(mdcursor_t element, mdcursor_t* tgt_cursor)
{
    assert(tgt_cursor != NULL);
    mdtable_t* table = CursorTable(&element);
    if (table == NULL)
        return false;

    uint32_t row = CursorRow(&element);
    mdtable_id_t tgt_table_id;
    col_index_t tgt_col;
    switch (table->table_id)
    {
    case mdtid_Field:
        tgt_table_id = mdtid_TypeDef;
        tgt_col = mdtTypeDef_FieldList;
        break;
    case mdtid_MethodDef:
        tgt_table_id = mdtid_TypeDef;
        tgt_col = mdtTypeDef_MethodList;
        break;
    case mdtid_Param:
        tgt_table_id = mdtid_MethodDef;
        tgt_col = mdtMethodDef_ParamList;
        break;
    case mdtid_Event:
        tgt_table_id = mdtid_EventMap;
        tgt_col = mdtEventMap_EventList;
        break;
    case mdtid_Property:
        tgt_table_id = mdtid_PropertyMap;
        tgt_col = mdtPropertyMap_PropertyList;
        break;
    default:
        return false;
    }

    mdtable_t* tgt_table = type_to_table(table->cxt, tgt_table_id);

    uint8_t col_index = col_to_index(tgt_col, tgt_table);

    assert((tgt_table->column_details[col_index] & mdtc_idx_table) == mdtc_idx_table);

    // If the column in the target table is not pointing to the starting table,
    // then it is pointing to the corresponding indirection table.
    // We need to find the element in the indirection table that points to the cursor
    // and then find the element in the target table that points to the indirection table.
    if (table_is_indirect_table(ExtractTable(tgt_table->column_details[col_index])))
    {
        mdtable_id_t indir_table_id = ExtractTable(tgt_table->column_details[col_index]);
        col_index_t indir_col = index_to_col(0, indir_table_id);
        mdcursor_t indir_table_cursor;
        uint32_t indir_table_row_count;
        if (!md_create_cursor(table->cxt, indir_table_id, &indir_table_cursor, &indir_table_row_count))
            return false;

        mdcursor_t indir_row;
        if (!find_row_from_cursor(indir_table_cursor, indir_col, &row, &indir_row))
            return false;
        
        // Now that we've found the indirection cell, we can look in the target table for the
        // element that contains the indirection cell in its range.
        row = CursorRow(&indir_row);
    }

    find_cxt_t fcxt;
    if (!create_find_context(tgt_table, tgt_col, &fcxt))
        return false;

    uint32_t found_row;
    int32_t last_cmp = mdtable_bsearch_closest(&row, tgt_table, &fcxt, &found_row);

    // The three result cases are handled as follows.
    // If last < 0, then the cursor is greater than the value so we must move back one.
    // If last == 0, then the cursor matches the value. This could be the first
    //    instance of the value in a run of rows. We are only interested in the
    //    last row with this value.
    // If last > 0, then the cursor is less than the value and begins the list, use it.
    mdcursor_t pos;
    mdcursor_t tmp;
    mdToken tmp_tk;
    if (last_cmp < 0)
    {
        pos = create_cursor(tgt_table, found_row - 1);
    }
    else if (last_cmp == 0)
    {
        tmp = create_cursor(tgt_table, found_row);
        tmp_tk = row;
        do
        {
            pos = tmp;
            if (!cursor_move_no_checks(&tmp, 1)
                || 1 != md_get_column_value_as_token(tmp, tgt_col, 1, &tmp_tk))
            {
                break;
            }
        }
        while (RidFromToken(tmp_tk) == row);
    }
    else
    {
        pos = create_cursor(tgt_table, found_row);
    }

    switch (table->table_id)
    {
    case mdtid_Field:
    case mdtid_MethodDef:
    case mdtid_Param:
        *tgt_cursor = pos;
        return true;
    case mdtid_Event:
        return md_get_column_value_as_cursor(pos, mdtEventMap_Parent, 1, tgt_cursor);
    case mdtid_Property:
        return md_get_column_value_as_cursor(pos, mdtPropertyMap_Parent, 1, tgt_cursor);
    default:
        assert(!"Invalid table ID");
        return false;
    }
}

bool md_find_token_of_range_element(mdcursor_t element, mdToken* tk)
{
    if (tk == NULL)
        return false;
    mdcursor_t cursor;
    if (!find_range_element(element, &cursor))
        return false;
    return md_cursor_to_token(cursor, tk);
}

bool md_find_cursor_of_range_element(mdcursor_t element, mdcursor_t* cursor)
{
    if (cursor == NULL)
        return false;
    return find_range_element(element, cursor);
}

bool md_resolve_indirect_cursor(mdcursor_t c, mdcursor_t* target)
{
    mdtable_t* table = CursorTable(&c);
    if (table == NULL)
        return false;

    if (!table_is_indirect_table(table->table_id))
    {
        // If the table isn't an indirect table,
        // we don't need to resolve an indirection from the cursor.
        // In this case, the original cursor is the target cursor.
        *target = c;
        return true;
    }
    col_index_t col_idx = index_to_col(0, table->table_id);
    return 1 == md_get_column_value_as_cursor(c, col_idx, 1, target);
}

static bool write_column_data(query_cxt_t* qcxt, uint32_t data)
{
    assert(qcxt != NULL && qcxt->writable_data != NULL);

    qcxt->data += (qcxt->col_details & mdtc_b2) ? 2 : 4;

    return (qcxt->col_details & mdtc_b2)
        ? write_u16(&qcxt->writable_data, &qcxt->data_len, (uint16_t)data)
        : write_u32(&qcxt->writable_data, &qcxt->data_len, data);
}

static bool is_column_sorted_with_next_column(query_cxt_t* qcxt, md_key_info const* keys, uint8_t col_key_index)
{
    // Copy the original context so we can use it to avoid rewinding to the prior row
    // for each parent key in the worst case (that the new row has a differing primary key)
    query_cxt_t original_qcxt = *qcxt;
    uint32_t raw;
    bool success = read_column_data(qcxt, &raw);
    assert(success);
    (void)success;

    // If there's no next row, then we're sorted.
    if (!next_row(qcxt))
        return true;

    bool column_sorted = keys[col_key_index].descending ? (raw >= raw) : (raw <= raw);
    if (!column_sorted)
    {
        // If the column isn't sorted, then check if this column isn't the primary key.
        // If it isn't then check the sorting of the parent keys.
        // If the parent keys are sorted, then the table is still sorted.
        for (int16_t parent_key_idx = col_key_index - 1; parent_key_idx >= 0; --parent_key_idx)
        {
            query_cxt_t parent_qcxt = original_qcxt;
            change_query_context_target_col(&parent_qcxt, index_to_col(keys[parent_key_idx].index, parent_qcxt.table->table_id));

            uint32_t raw_prev_parent_key;
            if (!read_column_data(&parent_qcxt, &raw_prev_parent_key))
                return -1;

            (void)next_row(&parent_qcxt); // We know that we will be able to read the next row as we've already read the row previously.

            uint32_t raw_parent_key;
            if (!read_column_data(&parent_qcxt, &raw_parent_key))
                return -1;

            column_sorted = keys[parent_key_idx].descending ? (raw_prev_parent_key >= raw_parent_key) : (raw_prev_parent_key <= raw_parent_key);
        }
    }
    return column_sorted;
}

static int32_t set_column_value_as_token_or_cursor(mdcursor_t c, uint32_t col_idx, mdToken* tk, mdcursor_t* cursor, uint32_t in_length)
{
    assert(in_length != 0 && (tk != NULL || cursor != NULL));

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, in_length, true, &qcxt))
        return -1;
    
    // If we can't write on the underlying table, then fail.
    if (qcxt.writable_data == NULL)
        return -1;

    // If this isn't an index column, then fail.
    if (!(qcxt.col_details & (mdtc_idx_table | mdtc_idx_coded)))
        return -1;

    uint8_t key_idx = UINT8_MAX;
    md_key_info const* keys = NULL;
    if (qcxt.table->is_sorted)
    {
        // If the table is sorted, then we need to validate that we stay sorted.
        // We will not check here if a table goes from unsorted to sorted as that would require
        // significantly more work to validate and is not a correctness issue.
        uint8_t key_count = get_table_keys(qcxt.table->table_id, &keys);
        for (uint8_t i = 0; i < key_count; i++)
        {
            if (keys[i].index == col_to_index(col_idx, qcxt.table))
            {
                key_idx = i;
                break;
            }
        }
    }

    int32_t written = 0;
    do
    {
        mdToken token;
        if (tk != NULL)
        {
            token = tk[written];
        }
        else
        {
            if (!md_cursor_to_token(cursor[written], &token))
                return -1;
        }

        uint32_t raw;
        if (qcxt.col_details & mdtc_idx_table)
        {
            uint32_t table_row = RidFromToken(token);
            mdtable_id_t table_id = ExtractTokenType(token);
            // The raw value is the row index into the table that
            // is embedded in the column details.
            // Return an error if the provided token does not point to the right table.
            if (ExtractTable(qcxt.col_details) != table_id)
                return -1;
            raw = table_row;
        }
        else
        {
            assert(qcxt.col_details & mdtc_idx_coded);
            if (!compose_coded_index(token, qcxt.col_details, &raw))
                return -1;
        }

        write_column_data(&qcxt, raw);

        // If the column we are writing to is a key of a sorted column, then we need to validate that it is sorted correctly.
        // We'll validate against the previous row here and then validate against the next row after we've written all of the columns that we will write.
        if (key_idx != UINT8_MAX)
        {
            assert(keys != NULL);
            // If we have a previous row, make sure that we're sorted with respect to the previous row.
            query_cxt_t prev_row_qcxt = qcxt;
            if (prev_row(&prev_row_qcxt))
            {
                bool is_sorted = is_column_sorted_with_next_column(&prev_row_qcxt, keys, key_idx);
                qcxt.table->is_sorted = is_sorted;

                // If we're not sorted, then invalidate key_idx to avoid checking if we're sorted for future row writes.
                // We won't go from unsorted to sorted.
                if (!is_sorted)
                    key_idx = UINT8_MAX;
            }
        }

        written++;
    } while (in_length > 1 && next_row(&qcxt));

    // Validate that the last row we wrote is sorted with respect to any following rows.
    if (key_idx != UINT8_MAX)
    {
        assert(keys != NULL);
        bool is_sorted = is_column_sorted_with_next_column(&qcxt, keys, key_idx);
        qcxt.table->is_sorted = is_sorted;
    }

    return written;
}

int32_t md_set_column_value_as_token(mdcursor_t c, col_index_t col, uint32_t in_length, mdToken* tk)
{
    if (tk == NULL || in_length == 0)
        return -1;
    return set_column_value_as_token_or_cursor(c, col_to_index(col, CursorTable(&c)), tk, NULL, in_length);
}

int32_t md_set_column_value_as_cursor(mdcursor_t c, col_index_t col, uint32_t in_length, mdcursor_t* cursor)
{
    if (cursor == NULL || in_length == 0)
        return -1;
    return set_column_value_as_token_or_cursor(c, col_to_index(col, CursorTable(&c)), NULL, cursor, in_length);
}

int32_t md_set_column_value_as_constant(mdcursor_t c, col_index_t col_idx, uint32_t in_length, uint32_t* constant)
{
    if (in_length == 0)
        return 0;
    assert(constant != NULL);

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, in_length, true, &qcxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(qcxt.col_details & mdtc_constant))
        return -1;
    
    uint8_t key_idx = UINT8_MAX;
    md_key_info const* keys = NULL;
    if (qcxt.table->is_sorted)
    {
        // If the table is sorted, then we need to validate that we stay sorted.
        // We will not check here if a table goes from unsorted to sorted as that would require
        // significantly more work to validate and is not a correctness issue.
        uint8_t key_count = get_table_keys(qcxt.table->table_id, &keys);
        for (uint8_t i = 0; i < key_count; i++)
        {
            if (keys[i].index == col_to_index(col_idx, qcxt.table))
            {
                key_idx = i;
                break;
            }
        }
    }

    int32_t written = 0;
    do
    {
        if (!write_column_data(&qcxt, constant[written]))
            return -1;
        
        // If the column we are writing to is a key of a sorted column, then we need to validate that it is sorted correctly.
        // We'll validate against the previous row here and then validate against the next row after we've written all of the columns that we will write.
        if (key_idx != UINT8_MAX)
        {
            assert(keys != NULL);
            // If we have a previous row, make sure that we're sorted with respect to the previous row.
            query_cxt_t prev_row_qcxt = qcxt;
            if (prev_row(&prev_row_qcxt))
            {
                bool is_sorted = is_column_sorted_with_next_column(&prev_row_qcxt, keys, key_idx);
                qcxt.table->is_sorted = is_sorted;

                // If we're not sorted, then invalidate key_idx to avoid checking if we're sorted for future row writes.
                // We won't go from unsorted to sorted.
                if (!is_sorted)
                    key_idx = UINT8_MAX;
            }
        }

        written++;
    } while (in_length > 1 && next_row(&qcxt));

    // Validate that the last row we wrote is sorted with respect to any following rows.
    if (key_idx != UINT8_MAX)
    {
        assert(keys != NULL);
        bool is_sorted = is_column_sorted_with_next_column(&qcxt, keys, key_idx);
        qcxt.table->is_sorted = is_sorted;
    }

    return written;
}

static void validate_column_not_sorted(mdtable_t const* table, col_index_t col_idx)
{
    md_key_info const* keys = NULL;
    uint8_t key_count = get_table_keys(table->table_id, &keys);
    for (uint8_t i = 0; i < key_count; i++)
    {
        if (keys[i].index == col_to_index(col_idx, table))
            assert(!"Sorted columns cannot be heap references");
    }
}

int32_t md_set_column_value_as_utf8(mdcursor_t c, col_index_t col_idx, uint32_t in_length, char const** str)
{
    if (in_length == 0)
        return 0;

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, in_length, true, &qcxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(qcxt.col_details & mdtc_hstring))
        return -1;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_not_sorted(qcxt.table, col_idx);
#endif

    int32_t written = 0;
    do
    {
        uint32_t heap_offset;
        if (str[written][0] == '\0')
        {
            heap_offset = 0;
        }
        else
        {
            heap_offset = add_to_string_heap(CursorTable(&c)->cxt->editor, str[written]);
            if (heap_offset == 0)
                return -1;
        }

        if (!write_column_data(&qcxt, heap_offset))
            return -1;
        written++;
    } while (in_length > 1 && next_row(&qcxt));

    return written;
}

int32_t md_set_column_value_as_blob(mdcursor_t c, col_index_t col_idx, uint32_t in_length, uint8_t const** blob, uint32_t* blob_len)
{
    if (in_length == 0)
        return 0;

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, in_length, true, &qcxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(qcxt.col_details & mdtc_hblob))
        return -1;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_not_sorted(qcxt.table, col_idx);
#endif

    int32_t written = 0;
    do
    {
        uint32_t heap_offset = add_to_blob_heap(CursorTable(&c)->cxt->editor, blob[written], blob_len[written]);

        if (heap_offset == 0)
            return -1;

        if (!write_column_data(&qcxt, heap_offset))
            return -1;
        written++;
    } while (in_length > 1 && next_row(&qcxt));

    return written;
}

int32_t md_set_column_value_as_guid(mdcursor_t c, col_index_t col_idx, uint32_t in_length, md_guid_t const* guid)
{
    if (in_length == 0)
        return 0;

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, in_length, true, &qcxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(qcxt.col_details & mdtc_hblob))
        return -1;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_not_sorted(qcxt.table, col_idx);
#endif

    int32_t written = 0;
    do
    {
        uint32_t index = add_to_guid_heap(CursorTable(&c)->cxt->editor, guid[written]);

        if (index == 0)
            return -1;

        if (!write_column_data(&qcxt, index))
            return -1;
        written++;
    } while (in_length > 1 && next_row(&qcxt));

    return written;
}

int32_t md_set_column_value_as_userstring(mdcursor_t c, col_index_t col_idx, uint32_t in_length, char16_t const** userstring)
{
    if (in_length == 0)
        return 0;

    query_cxt_t qcxt;
    if (!create_query_context(&c, col_idx, in_length, true, &qcxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(qcxt.col_details & mdtc_hblob))
        return -1;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_not_sorted(qcxt.table, col_idx);
#endif

    int32_t written = 0;
    do
    {
        uint32_t index = add_to_user_string_heap(CursorTable(&c)->cxt->editor, userstring[written]);

        if (index == 0)
            return -1;

        if (!write_column_data(&qcxt, index))
            return -1;
        written++;
    } while (in_length > 1 && next_row(&qcxt));

    return written;
}

int32_t update_shifted_row_references(mdcursor_t* c, uint32_t count, uint8_t col_index, mdtable_id_t updated_table, uint32_t original_starting_table_index, uint32_t new_starting_table_index)
{
    assert(c != NULL);
    col_index_t col = index_to_col(col_index, CursorTable(c)->table_id);

    query_cxt_t qcxt;
    if (!create_query_context(c, col, count, true, &qcxt))
        return -1;

    // If this isn't an index column, then fail.
    if (!(qcxt.col_details & (mdtc_idx_table | mdtc_idx_coded)))
        return -1;

    int32_t diff = (int32_t)(new_starting_table_index - original_starting_table_index);

    int32_t written = 0;
    do
    {
        uint32_t raw;
        mdtable_id_t table_id;
        uint32_t table_row;
        if (!read_column_data(&qcxt, &raw))
            return -1;

        if (qcxt.col_details & mdtc_idx_table)
        {
            // The raw value is the row index into the table that
            // is embedded in the column details.
            table_row = RidFromToken(raw);
            table_id = ExtractTable(qcxt.col_details);
        }
        else
        {
            assert(qcxt.col_details & mdtc_idx_coded);
            if (!decompose_coded_index(raw, qcxt.col_details, &table_id, &table_row))
                return -1;
        }

        if (table_id != updated_table || table_row < original_starting_table_index)
            continue;

        table_row += diff;

        if (qcxt.col_details & mdtc_idx_table)
        {
            // The raw value is the row index into the table that
            // is embedded in the column details.
            raw = table_row;
        }
        else
        {
            assert(qcxt.col_details & mdtc_idx_coded);
            if (!compose_coded_index(raw, qcxt.col_details, &raw))
                return -1;
        }
        write_column_data(&qcxt, raw);
        written++;
    } while (next_row(&qcxt));
    
    return written;
}

bool md_insert_row_after(mdcursor_t row, mdcursor_t* new_row)
{
    mdtable_t* table = CursorTable(&row);

    // If this is not an append scenario,
    // then we need to check if the table is one that has a corresponding indirection table
    // and create it as we are about to shift rows.

    uint32_t new_row_index = CursorRow(&row) + 1;

    if (new_row_index <= table->row_count)
    {
        mdtable_id_t indirect_table_maybe;
        switch (table->table_id)
        {
        case mdtid_Field:
            indirect_table_maybe = mdtid_FieldPtr;
            break;
        case mdtid_MethodDef:
            indirect_table_maybe = mdtid_MethodPtr;
            break;
        case mdtid_Param:
            indirect_table_maybe = mdtid_ParamPtr;
            break;
        case mdtid_Event:
            indirect_table_maybe = mdtid_EventPtr;
            break;
        case mdtid_Property:
            indirect_table_maybe = mdtid_PropertyPtr;
            break;
        default:
            indirect_table_maybe = mdtid_Unused;
            break;
        }
        if (indirect_table_maybe != mdtid_Unused)
        {
            if (!create_and_fill_indirect_table(table->cxt->editor, table->table_id, indirect_table_maybe))
            {
                return false;
            }
        }
    }
    
    return insert_row_into_table(table->cxt->editor, table->table_id, new_row_index, new_row);
}

bool md_append_row(mdhandle_t handle, mdtable_id_t table_id, mdcursor_t* new_row)
{
    mdcxt_t* cxt = extract_mdcxt(handle);

    if (table_id < mdtid_First || table_id > mdtid_End)
        return false;

    mdtable_t* table = &cxt->tables[table_id];

    if (table->cxt == NULL)
    {
        initialize_new_table_details(table_id, table);
        table->cxt = cxt;
        // Allocate some memory for the table.
        // The number of rows in this allocation is arbitrary.
        // It may be interesting to change the default depending on the target table.
        size_t initial_allocation_size = table->row_size_bytes * 20;
        // The initial table has a size 0 as it has no rows.
        table->data.size = 0;
        table->data.ptr = cxt->editor->tables[table_id].data.ptr = malloc(initial_allocation_size);
        cxt->editor->tables[table_id].data.size = initial_allocation_size;
    }

    return insert_row_into_table(handle, table->table_id, table->cxt != NULL ? table->row_count : 0, new_row);
}
