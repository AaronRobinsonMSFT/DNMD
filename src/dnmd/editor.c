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
    for (uint32_t i = 0; i < editor->tables[original_table].table->row_count; i++)
    {
        ((uint32_t*)(&editor->tables[indirect_table].data.ptr))[i] = i;
    }

    target_table->row_count = editor->tables[original_table].table->row_count;
    target_table->cxt = editor->cxt;
    return true;
}