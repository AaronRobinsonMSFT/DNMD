#include "dnmd.h"
#include "internal.h"

bool create_and_fill_indirect_table(mdeditor_t* editor, mdtable_id_t original_table, mdtable_id_t indirect_table)
{
    // Assert that this image does not have the indirect table yet.
    mdtable_t* target_table = editor->tables[indirect_table].table;
    assert(target_table->row_count == 0);
    initialize_new_table_details(indirect_table, target_table);
    // Assert that the indirection table has exactly one column that points back at the original table.
    assert(target_table->column_count == 1 && target_table->column_details[0] == (InsertTable(original_table) | mdtc_b4 | mdtc_idx_table));
    // If we're allocating an indirection table, then we're about to add new rows to the original table.
    // Allocate more space than we need for the rows we're copying over to be able to handle adding new rows.
    size_t allocation_space = target_table->row_size_bytes * editor->tables[original_table].table->row_count * 2;
    editor->tables[indirect_table].data.ptr = malloc(allocation_space);

    if (editor->tables[indirect_table].data.ptr == NULL)
        return false;

    editor->tables[indirect_table].data.size = allocation_space;
    target_table->data.ptr = editor->tables[indirect_table].data.ptr;
    target_table->data.size = editor->tables[indirect_table].data.size;

    // The indirection table will initially have each row pointing to the matching row in the original table.
    uint8_t* table_data = editor->tables[indirect_table].data.ptr;
    size_t table_len = editor->tables[indirect_table].data.size;
    for (uint32_t i = 0; i < editor->tables[original_table].table->row_count; i++)
    {
        write_u32(&table_data, &table_len, i);
    }

    target_table->row_count = editor->tables[original_table].table->row_count;
    target_table->cxt = editor->cxt;
    return true;
}

uint8_t* get_writable_table_data(mdtable_t* table, bool make_writable)
{
    assert((table->cxt->context_flags & mdc_editable) == mdc_editable);

    mddata_t* table_data = &table->cxt->editor->tables[table->table_id].data;

    if (table_data->ptr == NULL && make_writable)
    {
        // If we're trying to get writable data for a table that has not been edited,
        // then we need to allocate space for it and copy the contents for editing.
        // TODO: Should we allocate more space than the table currently uses to ensure
        // immediate table growth doesn't require a realloc?
        table_data->ptr = malloc(table->data.size);
        if (table_data->ptr == NULL)
            return NULL;
        table_data->size = table->data.size;
        memcpy(table_data->ptr, table->data.ptr, table->data.size);
        table->data.ptr = table_data->ptr;
    }

    return table_data->ptr;
}

bool set_column_size_for_max_row_count(mdeditor_t* editor, mdtable_t* table, mdtable_id_t updated_table, uint32_t new_max_row_count)
{
    assert(table->column_count <= MDTABLE_MAX_COLUMN_COUNT);
    assert(mdtid_First <= updated_table && updated_table <= mdtid_End);
    mdtcol_t new_column_details[MDTABLE_MAX_COLUMN_COUNT];

    uint32_t initial_row_count = editor->cxt->tables[updated_table].row_count;

    for (uint8_t col_index = 0; col_index < table->column_count; col_index++)
    {
        mdtcol_t col_details = table->column_details[col_index];
        uint32_t initial_max_column_value, new_max_column_value;
        if ((col_details & mdtc_idx_table) == mdtc_idx_table && ExtractTable(col_details) == updated_table)
        {
            initial_max_column_value = initial_row_count;
            new_max_column_value = new_max_row_count;
        }
        else if ((col_details & mdtc_idx_coded) == mdtc_idx_coded && is_coded_index_target(col_details, updated_table))
        {
            bool composed = compose_coded_index(TokenFromRid(initial_row_count, updated_table), col_details, &initial_max_column_value);
            assert(composed);
            composed = compose_coded_index(TokenFromRid(new_max_row_count, updated_table), col_details, &new_max_column_value);
            assert(composed);
        }
        else
        {
            new_column_details[col_index] = table->column_details[col_index];
            continue;
        }

        if ((table->column_details[col_index] & mdtc_b2) && new_max_column_value <= UINT16_MAX)
            new_column_details[col_index] = (table->column_details[col_index] & ~mdtc_b2) | mdtc_b4;
        else if ((table->column_details[col_index] & mdtc_b2) && new_max_column_value > UINT16_MAX)
            new_column_details[col_index] = (table->column_details[col_index] & ~mdtc_b4) | mdtc_b2;
    }

    // We want to make sure that we can store as many rows as the current table can in our new allocation.
    size_t table_data_size = editor->tables[table->table_id].data.ptr != NULL ? editor->tables[table->table_id].data.size : table->data.size;
    size_t max_original_rows_in_size = table_data_size / table->row_size_bytes;

    uint8_t new_row_size = 0;
    for (uint8_t col_index = 0; col_index < table->column_count; col_index++)
    {
        new_row_size += (new_column_details[col_index] & mdtc_b2) == mdtc_b2 ? 2 : 4;
    }

    size_t new_allocation_size = max_original_rows_in_size * new_row_size;

    uint8_t* new_data_blob = malloc(new_allocation_size);

    if (new_data_blob == NULL)
        return false;

    // Go through all of the columns of each row and copy them to the new memory for the table
    // in their correct size.
    uint8_t const* table_data = table->data.ptr;
    size_t table_data_length = table->data.size;
    uint8_t* new_table_data = new_data_blob;
    size_t new_table_data_length = new_allocation_size;
    for (uint32_t i = 0; i < table->row_count; i++)
    {
        for (uint8_t col_index = 0; col_index < table->column_count; col_index++)
        {
            // The source and destination column details can only differ by storage width.
            assert((table->column_details[col_index] & ~mdtc_widthmask) == (new_column_details[col_index] & ~mdtc_widthmask));

            uint32_t data = 0;

            if (table->column_details[col_index] & mdtc_b2)
                read_u16(&table_data, &table_data_length, (uint16_t*)&data);
            else
                read_u32(&table_data, &table_data_length, &data);

            if (new_column_details[col_index] & mdtc_b2)
                write_u16(&new_table_data, &new_table_data_length, (uint16_t)data);
            else
                write_u32(&new_table_data, &new_table_data_length, data);
        }
    }

    // Update the public view of the table to have the new schema and point to the new data.
    table->row_size_bytes = new_row_size;
    table->data.ptr = new_data_blob;
    table->data.size = (size_t)table->row_count * table->row_size_bytes;
    memcpy(table->column_details, new_column_details, sizeof(mdtcol_t) * table->column_count);

    // Update the table's corresponding editor to point to the newly-allocated memory.
    if (editor->tables[table->table_id].data.ptr != NULL)
        free(editor->tables[table->table_id].data.ptr);

    editor->tables[table->table_id].data.ptr = new_data_blob;
    editor->tables[table->table_id].data.size = new_allocation_size;

    return true;
}

bool update_table_references_for_shifted_rows(mdeditor_t* editor, mdtable_id_t updated_table, uint32_t changed_row_start, int64_t shift)
{
    // Make sure we aren't shifting into negative row ids or shifting above the max row id. That isn't legal.
    assert(changed_row_start - shift > 0 && changed_row_start + shift < 0x00ffffff);
    for (mdtable_id_t table_id = mdtid_First; table_id < mdtid_End; table_id++)
    {
        mdtable_t* table = &editor->cxt->tables[table_id];
        if (table->cxt == NULL) // This table is not used in the current image
            continue;

        // Update all columns in the table that can refer to the updated table
        // to be the correct width for the updated table's new size.
        set_column_size_for_max_row_count(editor, table, updated_table, (uint32_t)(table->row_count + shift));

        for (uint8_t i = 0; i < table->column_count; i++)
        {
            mdtcol_t col_details = table->column_details[i];
            if (((col_details & mdtc_idx_table) == mdtc_idx_table && ExtractTable(col_details) == updated_table)
                || (col_details & mdtc_idx_coded) == mdtc_idx_coded && is_coded_index_target(col_details, updated_table))
            {
                // We've found a column that will need updating.
                mdcursor_t c;
                uint32_t row_count;
                if (!md_create_cursor(editor->cxt, table_id, &c, &row_count))
                    return false;

                update_shifted_row_references(&c, row_count, i, updated_table, changed_row_start, (uint32_t)(changed_row_start + shift));
            }
        }
    }
    return true;
}

bool insert_row_into_table(mdeditor_t* editor, mdtable_id_t table_id, uint32_t row_index, mdcursor_t* new_row)
{
    mdtable_editor_t* target_table_editor = &editor->tables[table_id];

    if (target_table_editor->table->row_count + 1 < row_index)
        return false;
    
    // If we are out of space in our table, then we need to allocate a new table buffer.
    if (target_table_editor->table->data.size < target_table_editor->table->row_size_bytes * row_index)
    {
        size_t new_size = target_table_editor->data.size * 2;
        void* new_ptr;
        // If we've already allocated space for this table, then we can just realloc.
        // Otherwise we need to allocate for the first time.
        if (target_table_editor->data.ptr != NULL)
            new_ptr = realloc(target_table_editor->data.ptr, new_size);
        else
        {
            new_ptr = malloc(new_size);
            if (new_ptr == NULL)
                return false;
            memcpy(new_ptr, target_table_editor->table->data.ptr, target_table_editor->table->data.size);
        }

        if (new_ptr == NULL)
            return false;
        target_table_editor->data.ptr = new_ptr;
        target_table_editor->data.size = new_size;
        target_table_editor->table->data.ptr = new_ptr;
        target_table_editor->table->data.size = new_size;
    }

    size_t next_row_start_offset = target_table_editor->table->row_size_bytes * row_index;
    size_t last_row_end_offset = target_table_editor->table->row_size_bytes * target_table_editor->table->row_count;

    if (next_row_start_offset < last_row_end_offset)
    {
        // If we're inserting a row in the middle of the table, then we need to move the rows after it down.
        memmove(
            (uint8_t*)target_table_editor->data.ptr + next_row_start_offset + target_table_editor->table->row_size_bytes,
            (uint8_t*)target_table_editor->data.ptr + next_row_start_offset,
            last_row_end_offset - next_row_start_offset);

        // Clear the new row.
        memset((uint8_t*)target_table_editor->data.ptr + next_row_start_offset, 0, target_table_editor->table->row_size_bytes);

        // Update table references
        update_table_references_for_shifted_rows(editor, table_id, target_table_editor->table->row_count, 1);
    }
    else
    {
        // If we're appending to the end of a table, we only need to adjust referencing tables to ensure they can reference this row.
        for (mdtable_id_t i = mdtid_First; i < mdtid_End; i++)
        {
            mdtable_t* table = &editor->cxt->tables[i];
            if (table->cxt == NULL) // This table is not used in the current image
                continue;

            // Update all columns in the table that can refer to the updated table
            // to be the correct width for the updated table's new size.
            set_column_size_for_max_row_count(editor, table, target_table_editor->table->table_id, target_table_editor->table->row_count + 1);
        }
    }

    return md_token_to_cursor(editor->cxt, CreateTokenType(table_id) | row_index, new_row);
}
