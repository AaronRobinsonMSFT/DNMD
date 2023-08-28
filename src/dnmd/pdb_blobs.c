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