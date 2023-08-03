#include "internal.h"

static bool is_row_sorted_with_next_row(md_key_info const* keys, uint8_t count_keys, mdtable_id_t table_id, mdcursor_t row, mdcursor_t next_row)
{
    // We have a previous row, let's validate that it's sorted.
    for (uint8_t i = 0; i < count_keys; i++)
    {
        col_index_t key_col = index_to_col(keys[i].index, table_id);

        access_cxt_t row_acxt;
        if (!create_access_context(&row, key_col, 1, false, &row_acxt))
            return false;

        // Key columns can only be constant, index into a table, or a coded token index.
        // Heap offset columns cannot be keys.
        assert(row_acxt.col_details & (mdtc_constant | mdtc_idx_table | mdtc_idx_coded));

        access_cxt_t next_acxt;
        if (!create_access_context(&next_row, key_col, 1, false, &next_acxt))
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

static int32_t set_column_value_as_token_or_cursor(mdcursor_t c, uint32_t col_idx, mdToken* tk, mdcursor_t* cursor, uint32_t in_length)
{
    assert(in_length != 0 && (tk != NULL || cursor != NULL));

    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, in_length, true, &acxt))
        return -1;
    
    // If we can't write on the underlying table, then fail.
    if (acxt.writable_data == NULL)
        return -1;

    // If this isn't an index column, then fail.
    if (!(acxt.col_details & (mdtc_idx_table | mdtc_idx_coded)))
        return -1;

    uint8_t key_count = 0;
    uint8_t key_idx = UINT8_MAX;
    md_key_info const* keys = NULL;
    // If we're editing already-existing rows, then we need to validate that we stay sorted.
    // If we're in the middle of a row-add operation, we'll wait until the add is complete to validate.
    if (acxt.table->is_sorted && !acxt.table->is_adding_new_row)
    {
        // If the table is sorted, then we need to validate that we stay sorted.
        // We will not check here if a table goes from unsorted to sorted as that would require
        // significantly more work to validate and is not a correctness issue.
        key_count = get_table_keys(acxt.table->table_id, &keys);
        for (uint8_t i = 0; i < key_count; i++)
        {
            if (keys[i].index == col_to_index(col_idx, acxt.table))
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
        if (acxt.col_details & mdtc_idx_table)
        {
            uint32_t table_row = RidFromToken(token);
            mdtable_id_t table_id = ExtractTokenType(token);
            // The raw value is the row index into the table that
            // is embedded in the column details.
            // Return an error if the provided token does not point to the right table.
            if (ExtractTable(acxt.col_details) != table_id)
                return -1;
            raw = table_row;
        }
        else
        {
            assert(acxt.col_details & mdtc_idx_coded);
            if (!compose_coded_index(token, acxt.col_details, &raw))
                return -1;
        }

        write_column_data(&acxt, raw);

        // If the column we are writing to is a key of a sorted column, then we need to validate that it is sorted correctly.
        // We'll validate against the previous row here and then validate against the next row after we've written all of the columns that we will write.
        if (key_idx != UINT8_MAX)
        {
            assert(keys != NULL && key_idx < key_count);
            mdcursor_t current_row = c;
            bool success = md_cursor_move(&current_row, written);
            assert(success);
            mdcursor_t prior_row = current_row;
            if (md_cursor_move(&prior_row, -1) && !CursorNull(&prior_row))
            {
                // If we have a prior row, then we need to check if we're sorted with respect to it.
                if (!is_row_sorted_with_next_row(keys, key_count, acxt.table->table_id, prior_row, current_row))
                {
                    // If we're not sorted, then invalidate key_idx to avoid checking if we're sorted for future row writes.
                    // We won't go from unsorted to sorted.
                    acxt.table->is_sorted = false;
                    key_idx = UINT8_MAX;
                }
            }
        }

        written++;
    } while (in_length > 1 && next_row(&acxt));

    // Validate that the last row we wrote is sorted with respect to any following rows.
    if (key_idx != UINT8_MAX)
    {
        assert(keys != NULL && key_idx < key_count);
        mdcursor_t current_row = c;
        bool success = md_cursor_move(&current_row, written);
        assert(success);
        mdcursor_t next_row = current_row;
        if (md_cursor_move(&next_row, 1) && !CursorEnd(&next_row))
        {
            // If we have a prior row, then we need to check if we're sorted with respect to it.
            if (!is_row_sorted_with_next_row(keys, key_count, acxt.table->table_id, current_row, next_row))
            {
                // If we're not sorted, then invalidate key_idx to avoid checking if we're sorted for future row writes.
                // We won't go from unsorted to sorted.
                acxt.table->is_sorted = false;
                key_idx = UINT8_MAX;
            }
        }
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

    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, in_length, true, &acxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(acxt.col_details & mdtc_constant))
        return -1;
    
    uint8_t key_count = 0;
    uint8_t key_idx = UINT8_MAX;
    md_key_info const* keys = NULL;
    // If we're editing already-existing rows, then we need to validate that we stay sorted.
    // If we're in the middle of a row-add operation, we'll wait until the add is complete to validate.
    if (acxt.table->is_sorted && !acxt.table->is_adding_new_row)
    {
        // If the table is sorted, then we need to validate that we stay sorted.
        // We will not check here if a table goes from unsorted to sorted as that would require
        // significantly more work to validate and is not a correctness issue.
        key_count = get_table_keys(acxt.table->table_id, &keys);
        for (uint8_t i = 0; i < key_count; i++)
        {
            if (keys[i].index == col_to_index(col_idx, acxt.table))
            {
                key_idx = i;
                break;
            }
        }
    }

    int32_t written = 0;
    do
    {
        if (!write_column_data(&acxt, constant[written]))
            return -1;
        
        // If the column we are writing to is a key of a sorted column, then we need to validate that it is sorted correctly.
        // We'll validate against the previous row here and then validate against the next row after we've written all of the columns that we will write.
        if (key_idx != UINT8_MAX)
        {
            assert(keys != NULL && key_idx < key_count);
            mdcursor_t current_row = c;
            bool success = md_cursor_move(&current_row, written);
            assert(success);
            mdcursor_t prior_row = current_row;
            if (md_cursor_move(&prior_row, -1) && !CursorNull(&prior_row))
            {
                // If we have a prior row, then we need to check if we're sorted with respect to it.
                if (!is_row_sorted_with_next_row(keys, key_count, acxt.table->table_id, prior_row, current_row))
                {
                    // If we're not sorted, then invalidate key_idx to avoid checking if we're sorted for future row writes.
                    // We won't go from unsorted to sorted.
                    acxt.table->is_sorted = false;
                    key_idx = UINT8_MAX;
                }
            }
        }

        written++;
    } while (in_length > 1 && next_row(&acxt));

    // Validate that the last row we wrote is sorted with respect to any following rows.
    if (key_idx != UINT8_MAX)
    {
        assert(keys != NULL && key_idx < key_count);
        mdcursor_t current_row = c;
        bool success = md_cursor_move(&current_row, written);
        assert(success);
        mdcursor_t next_row = current_row;
        if (md_cursor_move(&next_row, 1) && !CursorEnd(&next_row))
        {
            // If we have a prior row, then we need to check if we're sorted with respect to it.
            if (!is_row_sorted_with_next_row(keys, key_count, acxt.table->table_id, current_row, next_row))
            {
                // If we're not sorted, then invalidate key_idx to avoid checking if we're sorted for future row writes.
                // We won't go from unsorted to sorted.
                acxt.table->is_sorted = false;
                key_idx = UINT8_MAX;
            }
        }
    }

    return written;
}

#ifdef DEBUG_COLUMN_SORTING
static void validate_column_is_not_key(mdtable_t const* table, col_index_t col_idx)
{
    md_key_info const* keys = NULL;
    uint8_t key_count = get_table_keys(table->table_id, &keys);
    for (uint8_t i = 0; i < key_count; i++)
    {
        if (keys[i].index == col_to_index(col_idx, table))
            assert(!"Sorted columns cannot be heap references");
    }
}
#endif

// Set a column value as an existing offset into a heap.
int32_t set_column_value_as_heap_offset(mdcursor_t c, col_index_t col_idx, uint32_t in_length, uint32_t* offset)
{
    if (in_length == 0)
        return 0;

    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, in_length, true, &acxt))
        return -1;

    // If this isn't a heap index column, then fail.
    if (!(acxt.col_details & mdtc_idx_heap))
        return -1;

    mdstream_t const* heap = get_heap_by_id(acxt.table->cxt, ExtractHeapType(acxt.col_details));
    if (heap == NULL)
        return -1;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_is_not_key(acxt.table, col_idx);
#endif

    int32_t written = 0;
    do
    {
        if (!write_column_data(&acxt, offset[written]))
            return -1;
        written++;
    } while (in_length > 1 && next_row(&acxt));

    return written;
}

int32_t md_set_column_value_as_utf8(mdcursor_t c, col_index_t col_idx, uint32_t in_length, char const** str)
{
    if (in_length == 0)
        return 0;

    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, in_length, true, &acxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(acxt.col_details & mdtc_hstring))
        return -1;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_is_not_key(acxt.table, col_idx);
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
            heap_offset = add_to_string_heap(CursorTable(&c)->cxt, str[written]);
            if (heap_offset == 0)
                return -1;
        }

        if (!write_column_data(&acxt, heap_offset))
            return -1;
        written++;
    } while (in_length > 1 && next_row(&acxt));

    return written;
}

int32_t md_set_column_value_as_blob(mdcursor_t c, col_index_t col_idx, uint32_t in_length, uint8_t const** blob, uint32_t* blob_len)
{
    if (in_length == 0)
        return 0;

    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, in_length, true, &acxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(acxt.col_details & mdtc_hblob))
        return -1;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_is_not_key(acxt.table, col_idx);
#endif

    int32_t written = 0;
    do
    {
        uint32_t heap_offset = add_to_blob_heap(CursorTable(&c)->cxt, blob[written], blob_len[written]);

        if (heap_offset == 0)
            return -1;

        if (!write_column_data(&acxt, heap_offset))
            return -1;
        written++;
    } while (in_length > 1 && next_row(&acxt));

    return written;
}

int32_t md_set_column_value_as_guid(mdcursor_t c, col_index_t col_idx, uint32_t in_length, md_guid_t const* guid)
{
    if (in_length == 0)
        return 0;

    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, in_length, true, &acxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(acxt.col_details & mdtc_hguid))
        return -1;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_is_not_key(acxt.table, col_idx);
#endif

    int32_t written = 0;
    do
    {
        uint32_t index = add_to_guid_heap(CursorTable(&c)->cxt, guid[written]);

        if (index == 0)
            return -1;

        if (!write_column_data(&acxt, index))
            return -1;
        written++;
    } while (in_length > 1 && next_row(&acxt));

    return written;
}

int32_t md_set_column_value_as_userstring(mdcursor_t c, col_index_t col_idx, uint32_t in_length, char16_t const** userstring)
{
    if (in_length == 0)
        return 0;

    access_cxt_t acxt;
    if (!create_access_context(&c, col_idx, in_length, true, &acxt))
        return -1;

    // If this isn't an constant column, then fail.
    if (!(acxt.col_details & mdtc_hblob))
        return -1;

#ifdef DEBUG_COLUMN_SORTING
    validate_column_is_not_key(acxt.table, col_idx);
#endif

    int32_t written = 0;
    do
    {
        uint32_t index = add_to_user_string_heap(CursorTable(&c)->cxt, userstring[written]);

        if (index == 0)
            return -1;

        if (!write_column_data(&acxt, index))
            return -1;
        written++;
    } while (in_length > 1 && next_row(&acxt));

    return written;
}

int32_t update_shifted_row_references(mdcursor_t* c, uint32_t count, uint8_t col_index, mdtable_id_t updated_table, uint32_t original_starting_table_index, uint32_t new_starting_table_index)
{
    assert(c != NULL);
    col_index_t col = index_to_col(col_index, CursorTable(c)->table_id);

    // If this isn't an table or coded index column, then fail.
    if (!(CursorTable(c)->column_details[col_index] & (mdtc_idx_table | mdtc_idx_coded)))
        return -1;

    int32_t diff = (int32_t)(new_starting_table_index - original_starting_table_index);

    for (uint32_t i = 0; i < count; i++, md_cursor_next(c))
    {
        mdToken tk;
        if (1 != md_get_column_value_as_token(*c, col, 1, &tk))
            return -1;
        
        if ((mdtable_id_t)ExtractTokenType(tk) == updated_table)
        {
            uint32_t rid = RidFromToken(tk);
            if (rid >= original_starting_table_index)
            {
                rid += diff;
                tk = TokenFromRid(rid, CreateTokenType(updated_table));
                if (1 != md_set_column_value_as_token(*c, col, 1, &tk))
                    return -1;
            }
        }
    }

    return count;
}

static bool col_points_to_list(mdcursor_t* c, col_index_t col_index)
{
    assert(c != NULL);

    switch (CursorTable(c)->table_id)
    {
    case mdtid_TypeDef:
        return col_index == mdtTypeDef_FieldList || col_index == mdtTypeDef_MethodList;
    case mdtid_PropertyMap:
        return col_index == mdtPropertyMap_PropertyList;
    case mdtid_EventMap:
        return col_index == mdtEventMap_EventList;
    case mdtid_MethodDef:
        return col_index == mdtMethodDef_ParamList;
    }
    return false;
}

static bool copy_cursor_column(mdcursor_t dest, mdcursor_t src, col_index_t idx)
{
    uint32_t column_value;
    mdtable_t* table = CursorTable(&src);
    mdtable_t* dest_table = CursorTable(&dest);
    switch (table->column_details[idx] & mdtc_categorymask)
    {
    case mdtc_constant:
        if (1 != md_get_column_value_as_constant(src, idx, 1, &column_value))
            return false;
        break;
    case mdtc_idx_coded:
    case mdtc_idx_table:
        if (1 != md_get_column_value_as_token(src, idx, 1, &column_value))
            return false;
        break;
    case mdtc_idx_heap:
        if (1 != get_column_value_as_heap_offset(src, idx, 1, &column_value))
            return false;
        break;
    }

    switch (dest_table->column_details[idx] & mdtc_categorymask)
    {
    case mdtc_constant:
        if (1 != md_set_column_value_as_constant(dest, idx, 1, &column_value))
            return false;
        break;
    case mdtc_idx_coded:
    case mdtc_idx_table:
        if (1 != md_set_column_value_as_token(dest, idx, 1, &column_value))
            return false;
        break;
    case mdtc_idx_heap:
        if (1 != set_column_value_as_heap_offset(dest, idx, 1, &column_value))
            return false;
        break;
    }
    return true;
}

static bool set_column_as_end_of_table_cursor(mdcursor_t c, col_index_t col_idx)
{
    mdtable_t* table = CursorTable(&c);
    assert((table->column_details[col_to_index(col_idx, table)] & mdtc_categorymask) == mdtc_idx_table);
    mdtable_id_t target_table = ExtractTable(table->column_details[col_to_index(col_idx, table)]);
    mdcursor_t end_of_table = create_cursor(&table->cxt->tables[target_table], table->cxt->tables[target_table].row_count + 1);

    return md_set_column_value_as_cursor(c, col_idx, 1, &end_of_table);
}

static bool initialize_list_columns(mdcursor_t c)
{
    // Initialize list columns to one-past the end of the target table.
    mdtable_t* table = CursorTable(&c);
    switch (table->table_id)
    {
    case mdtid_TypeDef:
        return set_column_as_end_of_table_cursor(c, mdtTypeDef_FieldList)
            && set_column_as_end_of_table_cursor(c, mdtTypeDef_MethodList);
        break;
    case mdtid_MethodDef:
        return set_column_as_end_of_table_cursor(c, mdtMethodDef_ParamList);
    case mdtid_PropertyMap:
        return set_column_as_end_of_table_cursor(c, mdtPropertyMap_PropertyList);
    case mdtid_EventMap:
        return set_column_as_end_of_table_cursor(c, mdtEventMap_EventList);
    default:
        break;
    }
    return true;
}

static bool insert_row_cursor_relative(mdcursor_t row, int32_t offset, mdcursor_t* new_list_target, mdcursor_t* new_row)
{
    // If this is not an append scenario,
    // then we need to check if the table is one that has a corresponding indirection table
    // and create it as we are about to shift rows.
    // If an indirection table exists, then we can't just insert a row into a table.
    // We must append the row to the end of the table and then update the indirection table.
    // Tables that have indirection tables must always be sorted in a very particular way
    // and shifting tokens in these tables is generally expensive, as they may be referred to
    // by other data streams like IL method bodies.

    mdtable_t* table = CursorTable(&row);
    if (table->cxt == NULL) // We can't turn an insert into a "create table" operation.
        return false;

    // We can't insert a row before the first row of a table.
    assert(offset + (int64_t)CursorRow(&row) >= 0);

    uint32_t new_row_index = CursorRow(&row) + offset;

    if (new_row_index > table->row_count + 1)
        return false;

    // If we're inserting a row in the middle of a table, we need to check if the row
    // is in a table that can be a target of a list column, as we may need to handle indirection tables.
    if (table_is_indirect_table(table->table_id) || new_row_index <= table->row_count)
    {
        // If we are inserting a row into a table that has an indirection table,
        // then we need to actually insert the row at the end and update the indirection table
        // at the requested index.
        // If we are inserting a row into the indirection table, then we need to also add a row
        // at the end of the table that the indirection table points to and pass that row back
        // to the caller.
        // We'll always pass the indirection table row back as new_list_target, and the target
        // table's new row as new_row.
        mdtable_id_t indirect_table_maybe;
        mdtable_id_t parent_table;
        col_index_t col_to_update;
        mdtable_id_t target_table;
        switch (table->table_id)
        {
        case mdtid_FieldPtr:
        case mdtid_Field:
            indirect_table_maybe = mdtid_FieldPtr;
            parent_table = mdtid_TypeDef;
            col_to_update = mdtTypeDef_FieldList;
            target_table = mdtid_Field;
            break;
        case mdtid_MethodPtr:
        case mdtid_MethodDef:
            indirect_table_maybe = mdtid_MethodPtr;
            parent_table = mdtid_TypeDef;
            col_to_update = mdtTypeDef_MethodList;
            target_table = mdtid_MethodDef;
            break;
        case mdtid_ParamPtr:
        case mdtid_Param:
            indirect_table_maybe = mdtid_ParamPtr;
            parent_table = mdtid_MethodDef;
            col_to_update = mdtMethodDef_ParamList;
            target_table = mdtid_Param;
            break;
        case mdtid_EventPtr:
        case mdtid_Event:
            indirect_table_maybe = mdtid_EventPtr;
            parent_table = mdtid_EventMap;
            col_to_update = mdtEventMap_EventList;
            target_table = mdtid_Event;
            break;
        case mdtid_PropertyPtr:
        case mdtid_Property:
            indirect_table_maybe = mdtid_PropertyPtr;
            parent_table = mdtid_PropertyMap;
            col_to_update = mdtPropertyMap_PropertyList;
            target_table = mdtid_Property;
            break;
        default:
            indirect_table_maybe = mdtid_Unused;
            parent_table = mdtid_Unused;
            target_table = mdtid_Unused;
            col_to_update = -1;
            break;
        }
        if (indirect_table_maybe != mdtid_Unused)
        {
            mdcxt_t* cxt = table->cxt;
            assert(col_to_update != -1 && parent_table != mdtid_Unused);
            mdtcol_t* parent_col = &cxt->tables[parent_table].column_details[col_to_update];
            if (ExtractTable(*parent_col) != indirect_table_maybe)
            {
                if (!create_and_fill_indirect_table(cxt, target_table, indirect_table_maybe))
                    return false;
                
                // Clear the target column of the table index, so that we can set it to the new indirection table.
                *parent_col = (*parent_col & ~mdtc_timask) | InsertTable(indirect_table_maybe);
            }
          
            // Insert a row into the indirection table where a new row was requested, so any list columns that should include this
            // row will correctly be able to observe the update.
            if (!insert_row_into_table(cxt, indirect_table_maybe, new_row_index, new_list_target))
                return false;
            
            // Create the new row at the end of the original target table.
            if (!md_append_row(cxt, target_table, new_row))
                return false;
            
            // Set the indirection table row to point to the new row in the actual target table.
            if (1 != md_set_column_value_as_cursor(*new_list_target, index_to_col(0, indirect_table_maybe), 1, new_row))
                return false;

            md_commit_row_add(*new_list_target);

            return true;
        }
    }
    
    if (!insert_row_into_table(table->cxt, table->table_id, new_row_index, new_row))
        return false;

    *new_list_target = *new_row;

    // Now that we have this row, we need to initialize the list columns to the correct values that represent a zero-length list.
    // If we've inserted a row at the end of the table, we'll initalize the columns to the end-of-table cursor.
    // If we've inserted a row in the middle of the table, we'll copy the next row's list column values.
    mdcursor_t next_row = *new_row;
    if (!md_cursor_next(&next_row) || CursorEnd(&next_row))
    {
        return initialize_list_columns(*new_row);
    }

    for (uint8_t i = 0; i < table->column_count; i++)
    {
        col_index_t col = index_to_col(i, table->table_id);
        if (col_points_to_list(&next_row, col))
        {
            if (!copy_cursor_column(*new_row, next_row, col))
                return false;
        }
    }

    *new_list_target = *new_row;
    return true;
}

bool md_insert_row_before(mdcursor_t row, mdcursor_t* new_list_target, mdcursor_t* new_row)
{
    // Inserting a row before a given cursor means that the new row will point to the same
    // target as the given cursor.
    return insert_row_cursor_relative(row, 0, new_list_target, new_row);
}

bool md_insert_row_after(mdcursor_t row, mdcursor_t* new_list_target, mdcursor_t* new_row)
{
    return insert_row_cursor_relative(row, 1, new_list_target, new_row);
}

bool md_append_row(mdhandle_t handle, mdtable_id_t table_id, mdcursor_t* new_row)
{
    mdcxt_t* cxt = extract_mdcxt(handle);

    if (table_id < mdtid_First || table_id > mdtid_End)
        return false;

    mdtable_t* table = &cxt->tables[table_id];

    if (table->cxt == NULL)
    {
        if (!allocate_new_table(cxt, table_id))
            return false;
    }

    if (!insert_row_into_table(cxt, table->table_id, table->cxt != NULL ? table->row_count + 1 : 1, new_row))
        return false;

    // Initialize the list columns for the new row at the end of the table.
    if (!initialize_list_columns(*new_row))
       return false;

    return true;
}

bool copy_cursor(mdcursor_t dest, mdcursor_t src)
{
    mdtable_t* table = CursorTable(&src);
    mdtable_t* dest_table = CursorTable(&dest);
    assert(table->column_count == dest_table->column_count);

    for (uint8_t i = 0; i < table->column_count; i++)
    {
        col_index_t col = index_to_col(i, table->table_id);
        // We don't want to copy over columns that point to lists in other tables.
        // These columns have very particular behavior and are handled separately by
        // direct manipulation in the other operations.
        if (col_points_to_list(&src, col))
            continue;

        if (!copy_cursor_column(dest, src, col))
            return false;
    }

    return true;
}

static bool validate_row_sorted_within_table(mdcursor_t row)
{
    mdtable_t* table = CursorTable(&row);
    md_key_info* keys;
    uint8_t count_keys = get_table_keys(table->table_id, &keys);
    assert(count_keys != 0); // We should only ever have a sorted table for a table with keys.

    mdcursor_t prior_row = row;
    if (md_cursor_move(&prior_row, -1) && !CursorNull(&prior_row))
    {
        if (!is_row_sorted_with_next_row(keys, count_keys, table->table_id, prior_row, row))
            return false;
    }
    mdcursor_t next_row = row;
    if (!md_cursor_next(&next_row) && !CursorEnd(&next_row))
    {
        if (!is_row_sorted_with_next_row(keys, count_keys, table->table_id, row, next_row))
            return false;
    }

    return true;
}

void md_commit_row_add(mdcursor_t row)
{
    mdtable_t* table = CursorTable(&row);
    assert(table->is_adding_new_row);

    // If the table was previously sorted,
    // validate that the current row is sorted with respect to the prior and following rows.
    if (table->is_sorted)
    {
        table->is_sorted = validate_row_sorted_within_table(row);
    }


    table->is_adding_new_row = false;
}