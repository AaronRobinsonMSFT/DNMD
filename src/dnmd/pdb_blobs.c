#include "internal.h"

md_blob_parse_result_t md_parse_document_name(mdhandle_t handle, uint8_t const* blob, size_t blob_len, char const* name, size_t* name_len)
{
    mdcxt_t* cxt = extract_mdcxt(handle);
    if (cxt == NULL)
        return mdbpr_InvalidArgument;

    if (blob == NULL || name_len == NULL)
        return mdbpr_InvalidArgument;

    // Only support one-character ASCII seperators.
    // System.Reflection.Metadata.MetadataReader has the same limitation
    uint8_t separator;
    if (!read_u8(&blob, &blob_len, &separator))
        return mdbpr_InvalidBlob;
    
    if (separator > 0x7f)
        return mdbpr_InvalidBlob;
    
    uint8_t* name_current = (uint8_t*)name;
    size_t remaining_name_len = *name_len;
    size_t required_len = 0;
    md_blob_parse_result_t result = mdbpr_Success;
    bool write_separator = false;
    while (blob_len > 0)
    {
        if (write_separator)
        {
            // Add the required space for the separator.
            // If there is space in the buffer, write the separator.
            required_len += 1;
            if (name_current == NULL || remaining_name_len == 0)
            {
                result = mdbpr_InsufficientBuffer;
            }
            else
            {
                write_u8(&name_current, &remaining_name_len, separator);
            }
        }
        write_separator = separator != 0;

        // Get the next part of the path.
        uint32_t part_offset;
        if (!decompress_u32(&blob, &blob_len, &part_offset))
            return mdbpr_InvalidBlob;
        
        // The part blob is a UTF-8 string that is not null-terminated.
        const uint8_t* part;
        uint32_t part_len;
        if (!try_get_blob(cxt, part_offset, &part, &part_len))
            return mdbpr_InvalidBlob;
        
        // Add the required space for the part.
        // If there is space in the buffer, write the part.
        required_len += part_len;
        if (name_current == NULL || remaining_name_len < part_len)
        {
            result = mdbpr_InsufficientBuffer;
            continue;
        }
        else
        {
            memcpy(name_current, part, part_len);
            bool success = advance_output_stream(&name_current, &remaining_name_len, part_len);
            assert(success);
            (void)success;
        }
    }

    // Add the null terminator.
    required_len++;
    if (name_current != NULL && remaining_name_len > 0)
        write_u8(&name_current, &remaining_name_len, 0);
    else
        result = mdbpr_InsufficientBuffer;

    *name_len = required_len;
    return result;
}

// We only support up to UINT32_MAX - 1 sequence points per method.
// Technically, the number of supported sequence points in the spec is unbounded.
// However, the PE format that an ECMA-335 blob is commonly wrapped in
// can only support up to 4GB files, so we can't possibly have UINT32_MAX - 1 entries
// in any existing scenario anyway.
static uint32_t get_num_sequence_points(mdcursor_t debug_info_row, uint8_t const* blob, size_t blob_len)
{
    uint32_t num_records = 0;
    uint32_t ignored;
    if (!decompress_u32(&blob, &blob_len, &ignored)) // header LocalSignature
        return UINT32_MAX;
    
    mdcursor_t document;
    if (1 != md_get_column_value_as_cursor(debug_info_row, mdtMethodDebugInformation_Document, 1, &document))
        return UINT32_MAX;

    if (CursorNull(&document) && !decompress_u32(&blob, &blob_len, &ignored)) // header InitialDocument
        return UINT32_MAX;

    bool first_record = true;
    while (blob_len > 0)
    {
        if (num_records == UINT32_MAX)
            return UINT32_MAX;
        num_records++;
        uint32_t il_offset;
        if (!decompress_u32(&blob, &blob_len, &il_offset)) // ILOffset
            return UINT32_MAX;
        
        // The first record cannot be a document record
        if (!first_record && il_offset == 0)
        {
            uint32_t document_offset;
            if (!decompress_u32(&blob, &blob_len, &document_offset)) // Document
                return UINT32_MAX;
        }
        else
        {
            // We don't need to check if we need to do an unsigned or signed decompression
            // as we will always read the same number of bytes and we don't care about the values here
            // as we're only calculating the number of records.
            uint32_t delta_lines;
            if (!decompress_u32(&blob, &blob_len, &delta_lines)) // DeltaLines
                return UINT32_MAX;
            
            uint32_t delta_columns;
            if (!decompress_u32(&blob, &blob_len, &delta_columns)) // DeltaColumns
                return UINT32_MAX;
            if (delta_lines != 0 || delta_columns != 0)
            {
                uint32_t start_line;
                if (!decompress_u32(&blob, &blob_len, &start_line)) // StartLine
                    return UINT32_MAX;
                uint32_t start_column;
                if (!decompress_u32(&blob, &blob_len, &start_column)) // StartColumn
                    return UINT32_MAX;
            }
        }

        first_record = false;
    }

    return num_records;
}

md_blob_parse_result_t md_parse_sequence_points(mdcursor_t debug_info_row, uint8_t const* blob, size_t blob_len, md_sequence_points_t* sequence_points, size_t* buffer_len)
{
    if (CursorNull(&debug_info_row) || CursorEnd(&debug_info_row))
        return mdbpr_InvalidArgument;
    
    if (blob == NULL || buffer_len == NULL)
        return mdbpr_InvalidArgument;

    uint32_t num_records = get_num_sequence_points(debug_info_row, blob, blob_len);

    if (num_records == UINT32_MAX)
        return mdbpr_InvalidBlob;
    
    size_t required_size = sizeof(md_sequence_points_t) + num_records * sizeof(sequence_points->records[0]);
    if (sequence_points == NULL || *buffer_len < required_size)
    {
        *buffer_len = required_size;
        return mdbpr_InsufficientBuffer;
    }

    // header LocalSignature
    if (!decompress_u32(&blob, &blob_len, &sequence_points->signature))
        return mdbpr_InvalidBlob;
    
    mdcursor_t document;
    if (1 != md_get_column_value_as_cursor(debug_info_row, mdtMethodDebugInformation_Document, 1, &document))
        return mdbpr_InvalidBlob;

    // Create a "null" cursor to default-initialize the document field.
    mdcxt_t* cxt = extract_mdcxt(md_extract_handle_from_cursor(debug_info_row));
    sequence_points->document = create_cursor(&cxt->tables[mdtid_Document], 0);
    
    // header InitialDocument
    uint32_t document_rid = 0;
    if (CursorNull(&document) && !decompress_u32(&blob, &blob_len, &document_rid))
        return mdbpr_InvalidBlob;
    
    if (document_rid != 0
        && !md_token_to_cursor(cxt, CreateTokenType(mdtid_Document) | document_rid, &sequence_points->document))
        return mdbpr_InvalidBlob;

    bool seen_non_hidden_sequence_point = false;
    for (uint32_t i = 0; blob_len > 0 && i < num_records; ++i)
    {
        uint32_t il_offset;
        if (!decompress_u32(&blob, &blob_len, &il_offset)) // ILOffset
            return mdbpr_InvalidBlob;
        if (i != 0 && il_offset == 0)
        {
            uint32_t document_row_id;
            if (!decompress_u32(&blob, &blob_len, &document_row_id)) // Document
                return mdbpr_InvalidBlob;
            
            sequence_points->records[i].kind = mdsp_DocumentRecord;
            if (!md_token_to_cursor(cxt, CreateTokenType(mdtid_Document) | document_row_id, &sequence_points->records[i].document.document))
                return mdbpr_InvalidBlob;
        }
        else
        {
            uint32_t delta_lines;
            if (!decompress_u32(&blob, &blob_len, &delta_lines)) // DeltaLines
                return mdbpr_InvalidBlob;

            int64_t delta_columns;
            if (delta_lines == 0)
            {
                uint32_t raw_delta_columns;
                if (!decompress_u32(&blob, &blob_len, &raw_delta_columns)) // DeltaColumns
                    return mdbpr_InvalidBlob;
                delta_columns = raw_delta_columns;
            }
            else
            {
                int32_t raw_delta_columns;
                if (!decompress_i32(&blob, &blob_len, &raw_delta_columns)) // DeltaColumns
                    return mdbpr_InvalidBlob;
                delta_columns = raw_delta_columns;
            }

            if (delta_columns == 0)
            {
                sequence_points->records[i].kind = mdsp_HiddenSequencePointRecord;
                sequence_points->records[i].hidden_sequence_point.il_offset = il_offset;
            }
            else if (!seen_non_hidden_sequence_point)
            {
                seen_non_hidden_sequence_point = true;
                uint32_t start_line;
                if (!decompress_u32(&blob, &blob_len, &start_line)) // StartLine
                    return mdbpr_InvalidBlob;
                uint32_t start_column;
                if (!decompress_u32(&blob, &blob_len, &start_column)) // StartColumn
                    return mdbpr_InvalidBlob;
                
                sequence_points->records[i].kind = mdsp_SequencePointRecord;
                sequence_points->records[i].sequence_point.il_offset = il_offset;
                sequence_points->records[i].sequence_point.num_lines = delta_lines;
                sequence_points->records[i].sequence_point.delta_columns = delta_columns;
                sequence_points->records[i].sequence_point.start_line = start_line;
                sequence_points->records[i].sequence_point.start_column = start_column;
            }
            else
            {
                // If we've seen a non-hidden sequence point,
                // then the values are compressed signed integers instead of
                // unsigned integers.
                int32_t start_line;
                if (!decompress_i32(&blob, &blob_len, &start_line)) // StartLine
                    return mdbpr_InvalidBlob;
                int32_t start_column;
                if (!decompress_i32(&blob, &blob_len, &start_column)) // StartColumn
                    return mdbpr_InvalidBlob;

                sequence_points->records[i].kind = mdsp_SequencePointRecord;
                sequence_points->records[i].sequence_point.il_offset = il_offset;
                sequence_points->records[i].sequence_point.num_lines = delta_lines;
                sequence_points->records[i].sequence_point.delta_columns = delta_columns;
                sequence_points->records[i].sequence_point.start_line = start_line;
                sequence_points->records[i].sequence_point.start_column = start_column;
            }
        }
    }

    if (!blob_len == 0)
        return mdbpr_InvalidBlob;
    
    sequence_points->record_count = num_records;
    return mdbpr_Success;
}