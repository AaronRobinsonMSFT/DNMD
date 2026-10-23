#include "internal.h"
#include "dnmd_edit.h"

bool md_cpp_append_row(mdhandle_t handle, mdtable_id_t table, uint32_t* row)
{
    if (row == NULL)
        return false;
    *row = 0;

    mdcxt_t* cxt = extract_mdcxt(handle);
    if (cxt == NULL || table < mdtid_First || table >= mdtid_End
        || get_table_column_count(table) == 0
        || (table_is_indirect_table(table) && !cxt->tables[table].is_initialized))
    {
        return false;
    }

    mdcursor_t added = { 0 };
    bool result = md_append_row(handle, table, &added);
    *row = (uint32_t)added._reserved2;
    return result;
}

bool md_cpp_insert_row(mdtable_t* table, uint32_t row, bool after, uint32_t* added_row)
{
    if (added_row == NULL)
        return false;
    *added_row = 0;

    if (table == NULL || !table->is_initialized || row == 0
        || row > table->row_count + 1 || row > 0x00ffffff)
    {
        return false;
    }

    mdcursor_t position = create_cursor(table, row);
    mdcursor_t added = { 0 };
    bool result = after
        ? md_insert_row_after(position, &added)
        : md_insert_row_before(position, &added);
    *added_row = (uint32_t)added._reserved2;
    return result;
}

bool md_cpp_add_list_row(mdtable_t* owner, uint32_t row, col_index_t list,
    bool sorted, col_index_t sort_column, uint32_t sort_value,
    mdtable_id_t* added_table, uint32_t* added_row)
{
    if (added_row != NULL)
        *added_row = 0;
    if (added_table != NULL)
        *added_table = mdtid_Unused;
    if (added_table == NULL || added_row == NULL || owner == NULL
        || !owner->is_initialized || row == 0 || row > owner->row_count
        || row > 0x00ffffff)
    {
        return false;
    }

    mdcursor_t position = create_cursor(owner, row);
    mdcursor_t added = { 0 };
    bool result = sorted
        ? md_add_new_row_to_sorted_list(position, list, sort_column, sort_value, &added)
        : md_add_new_row_to_list(position, list, &added);
    if (CursorTable(&added) != NULL)
    {
        *added_table = (mdtable_id_t)CursorTable(&added)->table_id;
        *added_row = (uint32_t)added._reserved2;
    }
    return result;
}
