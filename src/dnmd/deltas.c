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

static bool process_log(mdcxt_t* cxt, mdcxt_t* delta)
{
    (void)cxt;
    mdtable_t* log = &delta->tables[mdtid_ENCLog];
    //mdtable_t* map = &delta->tables[mdtid_ENCMap];
    mdcursor_t cur = create_cursor(log, 1);
    mdToken tk;
    uint32_t op;
    for (uint32_t i = 0; i < log->row_count; (void)md_cursor_next(&cur), ++i)
    {
        if (1 != md_get_column_value_as_constant(cur, mdtENCLog_Token, 1, &tk)
            || 1 != md_get_column_value_as_constant(cur, mdtENCLog_Op, 1, &op))
        {
            return false;
        }

        switch ((delta_ops_t)op)
        {
        case dops_MethodCreate:
        case dops_ParamCreate:
        case dops_FieldCreate:
        case dops_PropertyCreate:
        case dops_EventCreate:
        case dops_Default:
            assert(!"Not implemented delta operation");
            break;
        default:
            assert(!"Unknown delta operation");
            return false;
        }
    }

    return false;
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