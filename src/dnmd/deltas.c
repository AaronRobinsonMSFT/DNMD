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
        
        // Tokens in the EncMap table may have the high bit set to indicate that they aren't true token references.
        // Deltas produced by CoreCLR, CLR, and ILASM will have this bit set. Roslyn does not utilize this bit.
        // Strip this high bit if it is set. We don't need it.
        mdtable_id_t table_id = ExtractTokenType(tk) & 0x7F;

        if (table_id < mdtid_First || table_id >= mdtid_End)
            return false;

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

static bool resolve_token(enc_token_map_t* token_map, mdToken referenced_token, mdhandle_t delta_image, mdcursor_t* row_in_delta)
{
    mdtable_id_t type = ExtractTokenType(referenced_token);

    if (type < mdtid_First || type >= mdtid_End)
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

static bool add_list_target_row(mdcursor_t parent, col_index_t list_col)
{
    mdcursor_t new_child_record;
    if (!md_add_new_row_to_list(parent, list_col, &new_child_record))
        return false;

    // We've added enough information to the new record to make it valid for sorting purposes.
    // Commit the row add. We'll fill in the rest of the data in the next record in the EncLog.
    md_commit_row_add(new_child_record);

    return true;
}

static bool process_log(mdcxt_t* cxt, mdcxt_t* delta)
{
    // The EncMap table is grouped by token type and sorted by the order of the rows in the tables in the delta.
    mdtable_t* map = &delta->tables[mdtid_ENCMap];
    enc_token_map_t token_map;
    initialize_token_map(map, &token_map);

    mdtable_t* log = &delta->tables[mdtid_ENCLog];
    mdcursor_t log_cur = create_cursor(log, 1);
    for (uint32_t i = 0; i < log->row_count; (void)md_cursor_next(&log_cur), ++i)
    {
        mdToken tk;
        uint32_t op;
        if (1 != md_get_column_value_as_constant(log_cur, mdtENCLog_Token, 1, &tk)
            || 1 != md_get_column_value_as_constant(log_cur, mdtENCLog_Op, 1, &op))
        {
            return false;
        }

        // Tokens in the EncLog table may have the high bit set to indicate that they aren't true token references.
        // Deltas produced by CoreCLR, CLR, and ILASM will have this bit set. Roslyn does not utilize this bit.
        // Strip this high bit if it is set. We don't need it.
        tk &= 0x7fffffff;

        switch ((delta_ops_t)op)
        {
        case dops_MethodCreate:
        {
            if (ExtractTokenType(tk) != mdtid_TypeDef)
                return false;

            // By the time we're adding a member to a list, the parent should already be in the image.
            mdcursor_t parent;
            if (!md_token_to_cursor(cxt, tk, &parent))
                return false;

            if (!add_list_target_row(parent, mdtTypeDef_MethodList))
                return false;
            break;
        }
        case dops_FieldCreate:
        {
            if (ExtractTokenType(tk) != mdtid_TypeDef)
                return false;

            // By the time we're adding a member to a list, the parent should already be in the image.
            mdcursor_t parent;
            if (!md_token_to_cursor(cxt, tk, &parent))
                return false;

            if (!add_list_target_row(parent, mdtTypeDef_FieldList))
                return false;
            break;
        }
        case dops_ParamCreate:
        {
            if (ExtractTokenType(tk) != mdtid_MethodDef)
                return false;


            // By the time we're adding a member to a list, the parent should already be in the image.
            mdcursor_t parent;
            if (!md_token_to_cursor(cxt, tk, &parent))
                return false;

            if (!add_list_target_row(parent, mdtMethodDef_ParamList))
                return false;
            break;
        }
        case dops_PropertyCreate:
        {
            if (ExtractTokenType(tk) != mdtid_PropertyMap)
                return false;

            // By the time we're adding a member to a list, the parent should already be in the image.
            mdcursor_t parent;
            if (!md_token_to_cursor(cxt, tk, &parent))
                return false;

            if (!add_list_target_row(parent, mdtPropertyMap_PropertyList))
                return false;
            break;
        }
        case dops_EventCreate:
        {
            if (ExtractTokenType(tk) != mdtid_EventMap)
                return false;


            // By the time we're adding a member to a list, the parent should already be in the image.
            mdcursor_t parent;
            if (!md_token_to_cursor(cxt, tk, &parent))
                return false;

            if (!add_list_target_row(parent, mdtEventMap_EventList))
                return false;
            break;
        }
        case dops_Default:
        {
            mdtable_id_t table_id = ExtractTokenType(tk);

            if (table_id < mdtid_First || table_id >= mdtid_End)
                return false;

            uint32_t rid = RidFromToken(tk);

            if (rid == 0)
                return false;
            
            mdcursor_t delta_record;
            resolve_token(&token_map, tk, delta, &delta_record);
            
            // Try resolving the original token to determine what row we're editing.
            // We'll try to look up the row in the base image.
            // If we fail to resolve the original token, then we aren't editing an existing row,
            // but instead creating a new row.
            mdcursor_t record_to_edit;
            bool edit_record = md_token_to_cursor(cxt, tk, &record_to_edit);

            mdtable_t* table = &cxt->tables[table_id];
    
            // We can only add rows directly to the end of the table.
            // TODO: In the future, we could be smarter
            // and try to insert a row in the middle of a table to preserve sorting.
            // For some tables that aren't referred to by other tables, such as CustomAttribute,
            // we could get much better performance by preserving the sorted behavior.
            // If the runtime doesn't have any dependency on tokens being stable for these tables,
            // this optimization may reduce the need for maintaining a manual sorting above the metadata layer.
            if (!edit_record)
            {
                if (table->row_count != (rid - 1))
                    return false;
                
                if (!md_append_row(cxt, table_id, &record_to_edit))
                    return false;
            }

            if (!copy_cursor(record_to_edit, delta_record))
                return false;
            
            if (!edit_record)
            {
                md_commit_row_add(record_to_edit);
            }
            // TODO: Write to the ENC Log in cxt to record the change.
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

    mdcursor_t base_module = create_cursor(&cxt->tables[mdtid_Module], 1);
    mdcursor_t delta_module = create_cursor(&delta->tables[mdtid_Module], 1);

    md_guid_t base_mvid;
    if (1 != md_get_column_value_as_guid(base_module, mdtModule_Mvid, 1, &base_mvid))
    {
        return false;
    }

    md_guid_t delta_mvid;
    if (1 != md_get_column_value_as_guid(delta_module, mdtModule_Mvid, 1, &delta_mvid))
    {
        return false;
    }

    // MVIDs must match between base and delta images.
    if (memcmp(&base_mvid, &delta_mvid, sizeof(md_guid_t)) != 0)
    {
        return false;
    }

    // The EncBaseId of the delta must equal the EncId of the base image.
    // This ensures that we are applying deltas in order.
    md_guid_t enc_id;
    md_guid_t delta_enc_base_id;
    if (1 != md_get_column_value_as_guid(base_module, mdtModule_EncId, 1, &enc_id)
        || 1 != md_get_column_value_as_guid(delta_module, mdtModule_EncBaseId, 1, &delta_enc_base_id)
        || memcmp(&enc_id, &delta_enc_base_id, sizeof(md_guid_t)) != 0)
    {
        return false;
    }

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

    // Now that we've applied the delta,
    // update our Enc Id to match the delta's Id in preparation for the next delta.
    // We don't want to manipulate the heap sizes, so we'll pull the heap offset directly from the delta and use that
    // in the base image.
    uint32_t new_enc_base_id_offset;
    if (1 != get_column_value_as_heap_offset(delta_module, mdtModule_EncId, 1, &new_enc_base_id_offset))
    {
        return false;
    }
    if (1 != set_column_value_as_heap_offset(base_module, mdtModule_EncId, 1, &new_enc_base_id_offset))
    {
        return false;
    }

    return true;
}