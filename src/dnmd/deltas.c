#include "internal.h"

static bool append_heaps_from_delta(mdcxt_t* cxt, mdcxt_t* delta)
{
    if (delta == NULL)
        return false;
    
    if (!append_heap(cxt, delta, mdtc_hstring))
        return false;

    if (!append_heap(cxt, delta, mdtc_hguid))
        return false;

    if (!append_heap(cxt, delta, mdtc_hblob))
        return false;

    if (!append_heap(cxt, delta, mdtc_hus))
        return false;

    return true;
}

typedef enum
{
    dops_Default = 0,
    dops_MethodCreate,
    dops_FieldCreate,
    dops_ParamCreate,
    dops_PropertyCreate,
    dops_EventCreate,
} delta_ops_t;

struct map_table_group_t
{
    mdcursor_t start;
    uint32_t count;
};

typedef struct _enc_token_map_t
{
    struct map_table_group_t map_cur_by_table[MDTABLE_MAX_COUNT];
} enc_token_map_t;

static bool initialize_token_map(mdtable_t* map, enc_token_map_t* token_map)
{
    assert(map->table_id == mdtid_ENCMap);
    for (uint32_t i = 0; i < MDTABLE_MAX_COUNT; ++i)
    {
        token_map->map_cur_by_table[i].count = (uint32_t)(-1);
    }

    // If we don't have any entries in the map table, then we don't have any remapped tokens.
    // The initialization we've already done by this point is sufficient.
    if (map->row_count == 0)
        return true;

    // The EncMap table is grouped by token type and sorted by the order of the rows in the tables in the delta.
    mdcursor_t map_cur = create_cursor(map, 1);

    mdtable_id_t previous_table_id = mdtid_Unused;
    for (uint32_t i = 0; i < map->row_count; (void)md_cursor_next(&map_cur), ++i)
    {
        mdToken tk;
        if (1 != md_get_column_value_as_constant(map_cur, mdtENCMap_Token, 1, &tk))
        {
            return false;
        }
        
        mdtable_id_t table_id = ExtractTokenType(tk);
        
        // This token points to a record that may not be a physical token in the image.
        // Strip off this bit.
        if (table_id & 0x80)
            table_id = table_id & 0x7F;

        if (token_map->map_cur_by_table[table_id].count == (uint32_t)(-1))
        {
            token_map->map_cur_by_table[table_id].start = map_cur;
            token_map->map_cur_by_table[table_id].count = 1;
        }
        else if (previous_table_id != table_id)
        {
            // If the set of remapped tokens for this table has already been started, then the previous token
            // must be from the same table as the current token (tokens are grouped by table).
            return false;
        }
        else
        {
            token_map->map_cur_by_table[table_id].count++;
        }

        previous_table_id = table_id;
    }

    return true;
}

static bool resolve_token(enc_token_map_t* token_map, mdToken referenced_token, mdcxt_t* delta_image, mdcursor_t* row_in_delta)
{
    mdtable_id_t type = ExtractTokenType(referenced_token);
    // This token points to a record that may not be a physical token in the image.
    // Strip off this bit.
    if (type & 0x80)
        type = type & 0x7F;

    if (type >= mdtid_End)
        return false;

    uint32_t rid = RidFromToken(referenced_token);

    if (rid == 0)
        return false;

    // If we don't have any EncMap entries for this table,
    // then the token in the EncLog is the token we need to look up in the delta image to get the delta info to apply.
    if (token_map->map_cur_by_table[type].count == (uint32_t)(-1))
    {
        if (!md_token_to_cursor(delta_image, referenced_token, row_in_delta))
            return false;
    }
    else
    {
        mdcursor_t map_record = token_map->map_cur_by_table[type].start;
        for (uint32_t i = 0; i < token_map->map_cur_by_table[type].count; md_cursor_next(&map_record), i++)
        {
            mdToken mappedToken;
            if (1 != md_get_column_value_as_constant(map_record, mdtENCMap_Token, 1, &mappedToken))
                return false;
            
            assert((mdtable_id_t)(ExtractTokenType(mappedToken) & 0x7f) == type);
            if (RidFromToken(mappedToken) == rid)
            {
                if (!md_token_to_cursor(delta_image, TokenFromRid(i + 1, CreateTokenType(type)), row_in_delta))
                    return false;
                
                return true;
            }
        }
        
        // If we have a set of remapped tokens for a table,
        // we will remap all tokens in the EncLog.
        return false;
    }
    return false;
}

static bool process_log(mdcxt_t* cxt, mdcxt_t* delta)
{
    (void)cxt;
    mdtable_t* log = &delta->tables[mdtid_ENCLog];
    mdtable_t* map = &delta->tables[mdtid_ENCMap];
    mdcursor_t log_cur = create_cursor(log, 1);
    mdToken new_parent = mdTokenNil;
    col_index_t parent_col = -1; // Initialize to an invalid column
    mdtable_id_t new_child_table = mdtid_Unused;

    // The EncMap table is grouped by token type and sorted by the order of the rows in the tables in the delta.
    enc_token_map_t token_map;
    initialize_token_map(map, &token_map);

    for (uint32_t i = 0; i < log->row_count; (void)md_cursor_next(&log_cur), ++i)
    {
        mdToken tk;
        uint32_t op;
        if (1 != md_get_column_value_as_constant(log_cur, mdtENCLog_Token, 1, &tk)
            || 1 != md_get_column_value_as_constant(log_cur, mdtENCLog_Op, 1, &op))
        {
            return false;
        }

        switch ((delta_ops_t)op)
        {
        case dops_MethodCreate:
            if (ExtractTokenType(tk) != mdtid_TypeDef)
                return false;
            new_parent = tk;
            parent_col = mdtTypeDef_MethodList;
            new_child_table = mdtid_MethodDef;
            break;
        case dops_FieldCreate:
            if (ExtractTokenType(tk) != mdtid_TypeDef)
                return false;
            new_parent = tk;
            parent_col = mdtTypeDef_FieldList;
            new_child_table = mdtid_Field;
            break;
        case dops_ParamCreate:
            if (ExtractTokenType(tk) != mdtid_MethodDef)
                return false;
            new_parent = tk;
            parent_col = mdtMethodDef_ParamList;
            new_child_table = mdtid_Param;
            break;
        case dops_PropertyCreate:
            if (ExtractTokenType(tk) != mdtid_PropertyMap)
                return false;
            new_parent = tk;
            parent_col = mdtPropertyMap_PropertyList;
            new_child_table = mdtid_Property;
            break;
        case dops_EventCreate:
            if (ExtractTokenType(tk) != mdtid_EventMap)
                return false;
            new_parent = tk;
            parent_col = mdtEventMap_EventList;
            new_child_table = mdtid_Event;
            break;
        case dops_Default:
        {
            mdtable_id_t table_id = ExtractTokenType(tk);
            // This token points to a record that may not be a physical token in the image.
            // Strip off this bit.
            if (table_id & 0x80)
                table_id = table_id & 0x7F;

            if (table_id >= mdtid_End)
                return false;

            uint32_t rid = RidFromToken(tk);

            if (rid == 0)
                return false;
            
            mdcursor_t delta_record;
            resolve_token(&token_map, tk, delta, &delta_record);
            
            // Try resolving the original token to determine what row we're editing.
            // If this operation is the second part of a two-part operation, we'll
            // first create the new row such that it is connected to its parent.
            // Otherwise, we'll try to look up the row in the base image.
            // If we fail to resolve the original token, then we aren't editing an existing row,
            // but instead creating a new row.
            mdcursor_t record_to_edit;
            bool edit_record;

            if (new_parent != mdTokenNil)
            {
                assert(parent_col != -1);
                mdcursor_t parent;
                if (!md_token_to_cursor(cxt, new_parent, &parent))
                    return false;

                mdcursor_t existingList;
                uint32_t count;
                if (!md_get_column_value_as_range(parent, parent_col, &existingList, &count))
                    return false;

                // Move the cursor just past the end of the range. We'll insert a row at the end of the range.
                if (!md_cursor_move(&existingList, count))
                    return false;

                if (cxt->tables[new_child_table].cxt == NULL)
                {
                    // If we don't have a table to add the row to, create one.
                    if (!md_append_row(cxt, new_child_table, &record_to_edit))
                        return false;
                }
                else
                {
                    // Otherwise, insert the row at the correct offset.
                    if (!md_insert_row_before(existingList, &record_to_edit))
                        return false;
                    
                    // If we had to insert a row and the this is the first member for the parent,
                    // then we need to update the list column on the parent.
                    // Otherwise this entry will be associated with the previous entry in the parent table.
                    // TODO: Indirection tables
                    if (count == 0)
                    {
                        if (1 != md_set_column_value_as_cursor(parent, parent_col, 1, &record_to_edit))
                            return false;
                    }
                }

                // Reset the new_parent token as we've now completed that part of the two-part operation.
                new_parent = mdTokenNil;
                parent_col = -1;

                // We've already created the row, so treat the rest of the operation as an edit.
                edit_record = true;
            }
            else
            {
                edit_record = md_token_to_cursor(cxt, tk, &record_to_edit);
            }

            mdtable_t* table = &cxt->tables[table_id];
    
            // We can only add rows directly to the end of the table.
            // TODO: In the future, we could be smarter
            // and try to insert a row in the middle of a table to preserve sorting.
            // For some tables that aren't referred to by other tables, such as CustomAttribute,
            // we could get much better performance by preserving the sorted behavior.
            // If the runtime doesn't have any dependency on tokens being stable, this optimization may reduce
            // the need for maintaining a manual sorting above the metadata layer.
            if (!edit_record)
            {
                if (table->row_count != (rid - 1))
                    return false;
                
                if (!md_append_row(cxt, table_id, &record_to_edit))
                    return false;
                
                // When copying a row, we skip copying over the list columns.
                // If we're adding a new row, we need to initialize the list columns.
                // We'll always initialize them to one-past the end of the target table.
                // This also works if we have indirection tables as an indirection table
                // will always have the same number of rows as the table it points to.
                mdToken endOfTableToken;
                switch (table_id)
                {
                    case mdtid_TypeDef:
                        endOfTableToken = TokenFromRid(cxt->tables[mdtid_Field].row_count + 1, CreateTokenType(mdtid_Field));
                        if (!md_set_column_value_as_token(record_to_edit, mdtTypeDef_FieldList, 1, &endOfTableToken))
                            return false;
                        endOfTableToken = TokenFromRid(cxt->tables[mdtid_MethodDef].row_count + 1, CreateTokenType(mdtid_MethodDef));
                        if (!md_set_column_value_as_token(record_to_edit, mdtTypeDef_MethodList, 1, &endOfTableToken))
                            return false;
                        break;
                    case mdtid_MethodDef:
                        endOfTableToken = TokenFromRid(cxt->tables[mdtid_Param].row_count + 1, CreateTokenType(mdtid_Param));
                        if (!md_set_column_value_as_token(record_to_edit, mdtMethodDef_ParamList, 1, &endOfTableToken))
                            return false;
                        break;
                    case mdtid_PropertyMap:
                        endOfTableToken = TokenFromRid(cxt->tables[mdtid_Property].row_count + 1, CreateTokenType(mdtid_Property));
                        if (!md_set_column_value_as_token(record_to_edit, mdtPropertyMap_PropertyList, 1, &endOfTableToken))
                            return false;
                        break;
                    case mdtid_EventMap:
                        endOfTableToken = TokenFromRid(cxt->tables[mdtid_Event].row_count + 1, CreateTokenType(mdtid_Event));
                        if (!md_set_column_value_as_token(record_to_edit, mdtEventMap_EventList, 1, &endOfTableToken))
                            return false;
                        break;
                    default:
                        break;
                }
            }

            if (!copy_cursor(record_to_edit, delta_record))
                return false;
            break;
        }
        default:
            assert(!"Unknown delta operation");
            return false;
        }
    }

    return true;
}

bool merge_in_delta(mdcxt_t* cxt, mdcxt_t* delta)
{
    assert(cxt != NULL);
    assert(delta != NULL && (delta->context_flags & mdc_minimal_delta));

    // Validate metadata versions
    if (cxt->major_ver != delta->major_ver
        || cxt->minor_ver != delta->minor_ver)
    {
        return false;
    }

    // TODO: Validate EnC Generations

    // Merge heaps
    if (!append_heaps_from_delta(cxt, delta))
    {
        return false;
    }

    // Process delta log
    if (!process_log(cxt, delta))
    {
        return false;
    }

    return true;
}