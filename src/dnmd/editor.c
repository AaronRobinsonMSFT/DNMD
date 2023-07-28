#include "dnmd.h"
#include "internal.h"

bool create_and_fill_indirect_table(mdeditor_t* editor, mdtable_id_t original_table, mdtable_id_t indirect_table)
{
    editor->cxt->context_flags |= mdc_edited;
    // Assert that this image does not have the indirect table yet.
    mdtable_t* target_table = editor->tables[indirect_table].table;
    assert(target_table->cxt == NULL);
    initialize_new_table_details(indirect_table, target_table);
    target_table->cxt = editor->cxt;
    // Assert that the indirection table has exactly one column that points back at the original table.
    assert(target_table->column_count == 1 && target_table->column_details[0] == (InsertTable(original_table) | mdtc_b4 | mdtc_idx_table));
    // If we're allocating an indirection table, then we're about to add new rows to the original table.
    // Allocate more space than we need for the rows we're copying over to be able to handle adding new rows.
    size_t allocation_space = target_table->row_size_bytes * editor->tables[original_table].table->row_count * 2;
    void* mem = alloc_mdmem(editor->cxt, allocation_space);
    if (mem == NULL)
        return false;
    editor->tables[indirect_table].data.ptr = mem;

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
    mddata_t* table_data = &table->cxt->editor->tables[table->table_id].data;

    if (table_data->ptr == NULL && make_writable)
    {
        table->cxt->context_flags |= mdc_edited;
        // If we're trying to get writable data for a table that has not been edited,
        // then we need to allocate space for it and copy the contents for editing.
        // TODO: Should we allocate more space than the table currently uses to ensure
        // immediate table growth doesn't require a realloc?
        void* mem = alloc_mdmem(table->cxt, table->data.size);
        if (mem == NULL)
            return NULL;
        table_data->ptr = mem;
        if (table_data->ptr == NULL)
            return NULL;
        table_data->size = table->data.size;
        memcpy(table_data->ptr, table->data.ptr, table->data.size);
        table->data.ptr = table_data->ptr;
    }

    return table_data->ptr;
}

static bool set_column_size_for_max_row_count(mdeditor_t* editor, mdtable_t* table, mdtable_id_t updated_table, mdtcol_t updated_heap, uint32_t new_max_row_count)
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
        else if ((col_details & (mdtc_idx_heap)) == mdtc_idx_heap && ExtractHeapType(col_details) == updated_heap)
        {
            initial_max_column_value = initial_row_count;
            new_max_column_value = new_max_row_count;
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

    // If the row size is the same, then we didn't have to change any column sizes.
    // We are either only expanding or reducing, so we can't end up at the same size with changed column sizes.
    if (new_row_size == table->row_size_bytes)
        return true;

    table->cxt->context_flags |= mdc_edited;

    size_t new_allocation_size = max_original_rows_in_size * new_row_size;

    void* mem = alloc_mdmem(editor->cxt, new_allocation_size);
    if (mem == NULL)
        return false;
    uint8_t* new_data_blob = mem;

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

    // Update the table's corresponding editor to point to the newly-allocated memory,
    // and free the previous allocation if necessary.
    if (editor->tables[table->table_id].data.ptr != NULL)
        free_mdmem(editor->cxt, editor->tables[table->table_id].data.ptr);

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
        set_column_size_for_max_row_count(editor, table, updated_table, mdtc_none, (uint32_t)(table->row_count + shift));

        for (uint8_t i = 0; i < table->column_count; i++)
        {
            mdtcol_t col_details = table->column_details[i];
            if (((col_details & mdtc_idx_table) == mdtc_idx_table && ExtractTable(col_details) == updated_table)
                || (col_details & mdtc_idx_coded) == mdtc_idx_coded && is_coded_index_target(col_details, updated_table))
            {
                // We've found a column that will need updating.
                mdcursor_t c = create_cursor(table, 1);
                update_shifted_row_references(&c, table->row_count, i, updated_table, changed_row_start, (uint32_t)(changed_row_start + shift));
            }
        }
    }
    return true;
}

static bool allocate_more_editable_space(mdcxt_t* cxt, mddata_t* editable_data, mdcdata_t* data)
{
    cxt->context_flags |= mdc_edited;
    size_t new_size = data->size * 2;
    void* new_ptr;
    if (editable_data->ptr != NULL)
    {
        void* mem = alloc_mdmem(cxt, new_size);
        if (mem == NULL)
            return false;
        memcpy(mem, data->ptr, data->size);
        new_ptr = mem;
        free_mdmem(cxt, editable_data->ptr);
    }
    else
    {
        void* mem = alloc_mdmem(cxt, new_size);
        if (mem == NULL)
            return false;
        new_ptr = mem;
        memcpy(new_ptr, data->ptr, data->size);
    }

    if (new_ptr == NULL)
        return false;
    editable_data->ptr = new_ptr;
    editable_data->size = new_size;
    data->ptr = new_ptr;
    // We don't update data->size as the space has been allocated, but it is not in use in the image yet.
    return true;
}

bool insert_row_into_table(mdeditor_t* editor, mdtable_id_t table_id, uint32_t row_index, mdcursor_t* new_row)
{
    editor->cxt->context_flags |= mdc_edited;
    mdtable_editor_t* target_table_editor = &editor->tables[table_id];
    assert(target_table_editor->table->cxt != NULL); // The table should exist in the image before a row is added to it.

    if (target_table_editor->table->row_count + 1 < row_index)
        return false;
    
    // If we are out of space in our table, then we need to allocate a new table buffer.
    if (target_table_editor->data.ptr == NULL || target_table_editor->data.size < target_table_editor->table->row_size_bytes * row_index)
    {
        if (!allocate_more_editable_space(editor->cxt, &target_table_editor->data, &target_table_editor->table->data))
            return false;
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
            set_column_size_for_max_row_count(editor, table, target_table_editor->table->table_id, mdtc_none, target_table_editor->table->row_count + 1);
        }
    }

    target_table_editor->table->data.size += target_table_editor->table->row_size_bytes;
    target_table_editor->table->row_count++;

    *new_row = create_cursor(target_table_editor->table, row_index);
    return true;
}

static md_heap_editor_t* get_heap_editor_by_id(mdeditor_t* editor, mdtcol_t heap_id)
{
    switch (heap_id)
    {
        case mdtc_hblob:
            return &editor->blob_heap;
        case mdtc_hguid:
            return &editor->guid_heap;
        case mdtc_hstring:
            return &editor->strings_heap;
        case mdtc_hus:
            return &editor->user_string_heap;
        default:
            return NULL;
    }
}

static mdstream_t* get_heap_by_id(mdcxt_t* cxt, mdtcol_t heap_id)
{
    switch (heap_id)
    {
        case mdtc_hblob:
            return &cxt->blob_heap;
        case mdtc_hguid:
            return &cxt->guid_heap;
        case mdtc_hstring:
            return &cxt->strings_heap;
        case mdtc_hus:
            return &cxt->user_string_heap;
        default:
            return NULL;
    }
}

static bool reserve_heap_space(mdeditor_t* editor, uint32_t space_size, mdtcol_t heap_id, bool preserve_offsets, uint32_t* heap_offset)
{
    editor->cxt->context_flags |= mdc_edited;

    md_heap_editor_t* heap_editor = get_heap_editor_by_id(editor, heap_id);
    if (heap_editor == NULL)
        return false;

    if (heap_editor->stream->ptr == NULL)
    {
        // Set the default heap size based on likely reasonable sizes for the heaps.
        // In most images, there won't be more than three guids, so we can start with a small heap in that case.
        const size_t initial_heap_size = heap_id == mdtc_hguid ? sizeof(md_guid_t) * 3 : 0x100;
        void* mem = alloc_mdmem(editor->cxt, initial_heap_size);
        if (mem == NULL)
            return false;

        heap_editor->stream->ptr = mem;
        heap_editor->heap.ptr = mem;
        heap_editor->heap.size = initial_heap_size;

        // The first character in the strings heap must be the '\0' - II.24.2.3
        // The first character in the user_string and blob heaps must be the 0 - II.24.2.4
        // The guid heap doesn't start with a 0 byte, but it must be emitted in sizeof(md_guid_t)-based chuncks.
        // If we are preserving offsets, then we don't initialize the heap, as we will be copying an existing heap that has already been validated and
        // we must have the exact same offsets as the existing heap to avoid breaking heap references.
        if (heap_id != mdtc_hguid && !preserve_offsets)
        {
            heap_editor->heap.ptr[0] = 0;
            heap_editor->stream->size = heap_id == mdtc_hguid ? 0 : 1;
        }
    }

    *heap_offset = (uint32_t)heap_editor->stream->size;
    
    if (*heap_offset > UINT32_MAX - space_size)
    {
        // The max heap size is 2^32-1, so we don't have space left to allocate.
        return false;
    }


    uint32_t new_heap_size = *heap_offset + space_size;
    if (new_heap_size > heap_editor->heap.size)
    {
        if (!allocate_more_editable_space(editor->cxt, &heap_editor->heap, heap_editor->stream))
            return false;
    }
    heap_editor->stream->size += space_size;

    // Update heap references in case the additional used space crosses the boundary for index sizes.
    uint32_t index_scale = (heap_id == mdtc_hguid ? sizeof(md_guid_t) : 1);
    assert(heap_editor->stream->size % index_scale == 0);
    for (mdtable_id_t i = mdtid_First; i < mdtid_End; i++)
    {
        mdtable_t* table = &editor->cxt->tables[i];
        if (table->cxt == NULL) // This table is not used in the current image
            continue;

        // Update all columns in the table that can refer to the updated heap
        // to be the correct width for the updated heap's new size.
        set_column_size_for_max_row_count(editor, table, mdtid_Unused, heap_id, (uint32_t)(heap_editor->stream->size / index_scale));
    }

    return true;
}

uint32_t add_to_string_heap(mdeditor_t* editor, char const* str)
{
    // TODO: Deduplicate heap
    uint32_t str_len = (uint32_t)strlen(str);
    uint32_t heap_offset;
    if (!reserve_heap_space(editor, str_len + 1, mdtc_hstring, false, &heap_offset))
    {
        return 0;
    }
    memcpy((uint8_t*)editor->strings_heap.heap.ptr + heap_offset, str, str_len);
    ((uint8_t*)editor->strings_heap.heap.ptr)[heap_offset + str_len] = '\0';
    return heap_offset;
}

uint32_t add_to_blob_heap(mdeditor_t* editor, uint8_t const* data, uint32_t length)
{
    // TODO: Deduplicate heap
    uint8_t compressed_length[4];
    size_t compressed_length_size = 0;
    if (!compress_u32(length, compressed_length, &compressed_length_size))
        return 0;
    
    uint32_t heap_slot_size = length + (uint32_t)compressed_length_size;

    uint32_t heap_offset;
    if (!reserve_heap_space(editor, heap_slot_size, mdtc_hblob, false, &heap_offset))
    {
        return 0;
    }

    memcpy(editor->blob_heap.heap.ptr + heap_offset, compressed_length, compressed_length_size);
    memcpy(editor->blob_heap.heap.ptr + heap_offset + compressed_length_size, data, length);
    return heap_offset;
}

uint32_t add_to_user_string_heap(mdeditor_t* editor, char16_t const* str)
{
    // TODO: Deduplicate heap
    uint32_t str_len;
    uint8_t has_special_char = 0;
    for (str_len = 0; str[str_len] != (char16_t)0; str_len++)
    {
        char16_t c = str[str_len];
        // II.23.2.4
        // There is an additional terminal byte which holds a 1 or 0.
        // The 1 signifies Unicode characters that require handling beyond
        // that normally provided for 8-bit encoding sets.
        //  This final byte holds the value 1 if and only if any UTF16 character
        // within the string has any bit set in its top byte,
        // or its low byte is any of the following: 0x01-0x08, 0x0E–0x1F, 0x27, 0x2D, 0x7F.
        // Otherwise, it holds 0. 
        if ((c & 0x80) != 0)
        {
            has_special_char = 1;
        }
        else if (c >= 0x1 && c <= 0x8)
        {
            has_special_char = 1;
        }
        else if (c >= 0xe && c <= 0x1f)
        {
            has_special_char = 1;
        }
        else if (c == 0x27)
        {
            has_special_char = 1;
        }
        else if (c == 0x2d)
        {
            has_special_char = 1;
        }
        else if (c == 0x7f)
        {
            has_special_char = 1;
        }
    }
    
    
    uint8_t compressed_length[4];
    size_t compressed_length_size = 0;
    if (!compress_u32(str_len, compressed_length, &compressed_length_size))
        return 0;
    
    uint32_t heap_slot_size = str_len + (uint32_t)compressed_length_size + 1;
    uint32_t heap_offset;
    if (!reserve_heap_space(editor, heap_slot_size, mdtc_hus, false, &heap_offset))
    {
        return 0;
    }

    memcpy(editor->user_string_heap.heap.ptr + heap_offset, compressed_length, compressed_length_size);
    memcpy(editor->user_string_heap.heap.ptr + heap_offset + compressed_length_size, str, str_len);

    editor->user_string_heap.heap.ptr[heap_offset + compressed_length_size + str_len] = has_special_char;
    return heap_offset;
}

uint32_t add_to_guid_heap(mdeditor_t* editor, md_guid_t guid)
{
    // TODO: Deduplicate heap

    uint32_t heap_offset;
    if (!reserve_heap_space(editor, sizeof(md_guid_t), mdtc_hguid, false, &heap_offset))
    {
        return 0;
    }

    memcpy(editor->guid_heap.heap.ptr + heap_offset, &guid, sizeof(md_guid_t));
    return heap_offset / sizeof(md_guid_t);
}

bool append_heap(mdcxt_t* cxt, mdcxt_t* delta, mdtcol_t heap_id)
{
    bool is_minimal_delta = (delta->context_flags & mdc_minimal_delta) == mdc_minimal_delta;
    md_heap_editor_t* heap_editor = get_heap_editor_by_id(cxt->editor, heap_id);
    mdstream_t* delta_heap = get_heap_by_id(delta, heap_id);
    if (delta_heap->size == 0)
        return true;
    
    size_t copy_offset;
    size_t delta_size;
    if (is_minimal_delta && heap_id != mdtc_hguid)
    {
        // If the delta image is a minimal delta and the heap is not the GUID heap, we do a full copy from the delta image.
        copy_offset = 0;
        delta_size = delta_heap->size;
    }
    else
    {
        // Otherwise, we only do a partial copy from the stream starting at the end of the existing heap.
        copy_offset = heap_editor->heap.size;
        delta_size = delta_heap->size - copy_offset;
    }

    if (delta_size > UINT32_MAX)
    {
        return false;
    }

    uint32_t heap_offset;
    if (!reserve_heap_space(cxt->editor, (uint32_t)delta_size, heap_id, true, &heap_offset))
    {
        return false;
    }

    memcpy(heap_editor->heap.ptr + heap_offset, delta_heap->ptr + copy_offset, delta_size);

    return true;
}