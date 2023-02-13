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
            new_ptr = malloc(new_size);

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
    // TODO: Update all table references to this table to point at the correct rows.
    }

    return md_token_to_cursor(editor->cxt, CreateTokenType(table_id) | row_index, new_row);
}