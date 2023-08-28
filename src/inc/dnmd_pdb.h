#ifndef _SRC_INC_DNMD_PDB_H
#define _SRC_INC_DNMD_PDB_H

#include <dnmd.h>

#ifdef __cplusplus
extern "C" {
#endif

// Methods to parse specialized blob formats defined in the Portable PDB spec.
// https://github.com/dotnet/runtime/blob/main/docs/design/specs/PortablePdb-Metadata.md

typedef enum md_blob_parse_result__
{
    mdbpr_Success,
    mdbpr_InvalidBlob,
    mdbpr_InvalidArgument,
    mdbpr_InsufficientBuffer
} md_blob_parse_result_t;

// Parse a DocumentName blob into a UTF-8 string.
md_blob_parse_result_t md_parse_document_name(mdhandle_t handle, uint8_t const* blob, size_t blob_len, char const* name, size_t* name_len);

// Parse a SequencePoints blob.
typedef struct md_sequence_points__
{
    mdToken signature;
    mdcursor_t document;
    uint32_t record_count;
    struct
    {
        enum
        {
            DocumentRecord,
            SequencePointRecord,
            HiddenSequencePointRecord,
        } kind;
        union
        {
            struct
            {
                mdcursor_t document;
            } document;
            struct
            {
                uint32_t il_offset;
                uint32_t num_lines;
                int64_t num_columns;
                int64_t start_line;
                int64_t start_column;
            } sequence_point;
            struct
            {
                uint32_t il_offset;
            } hidden_sequence_point;
        };
    } records[];
} md_sequence_points_t;
md_blob_parse_result_t md_parse_sequence_points(mdhandle_t handle, uint8_t const* blob, size_t blob_len, md_sequence_points_t* sequence_points, size_t* buffer_len);

// Parse a LocalConstantSig blob.
typedef struct md_local_constant_sig__
{
    enum
    {
        PrimitiveConstant,
        EnumConstant,
        GeneralConstant
    } constant_kind;

    union
    {
        struct
        {
            uint8_t type_code;
        } primitive;

        struct
        {
            uint8_t type_code;
            mdToken enum_type;
        } enum_constant;

        struct
        {
            enum
            {
                ValueType,
                Class,
                Object
            } kind;
            mdToken type;
        } general;
    };

    const uint8_t* value_blob;
    const uint32_t value_len;
    
    struct {
        bool required;
        mdToken type;
    } custom_modifiers[];
} md_local_constant_sig_t;
md_blob_parse_result_t md_parse_local_constant_sig(mdhandle_t handle, uint8_t const* blob, size_t blob_len, md_local_constant_sig_t* local_constant_sig, size_t* buffer_len);

// Parse an Imports blob.
typedef struct md_imports__
{
    uint32_t count;
    struct
    {
        uint32_t kind;
        char const* alias;
        uint32_t alias_len;
        mdToken assembly;
        char const* target_namespace;
        uint32_t target_namespace_len;
        mdToken target_type;
    } imports[];
} md_imports_t;
md_blob_parse_result_t md_parse_imports(mdhandle_t handle, uint8_t const* blob, size_t blob_len, md_imports_t* imports, size_t* buffer_len);

// Parse an Edit-and-Continue Local Slot Map blob.
typedef struct md_enc_local_slot_map__
{
    uint32_t syntax_offset_baseline;
    uint32_t slot_count;
    struct
    {
        uint8_t kind;
        uint32_t syntax_offset;
        uint32_t ordinal;
    } slots[];
} md_enc_local_slot_map_t;
md_blob_parse_result_t md_parse_enc_local_slot_map(mdhandle_t handle, uint8_t const* blob, size_t blob_len, md_enc_local_slot_map_t* local_slot_map, size_t* buffer_len);

// Parse an Edit-and-Continue Lambda and Closure Map blob.
typedef struct md_enc_lambda_and_closure_map__
{
    uint32_t method_ordinal;
    uint32_t syntax_offset_baseline;
    uint32_t closure_count;
    struct closure__
    {
        uint32_t syntax_offset;
    };
    struct closure__* closure_syntax_offsets;
    struct
    {
        uint32_t syntax_offset;
        struct closure__* closure;
    } lambda[];
} md_enc_lambda_and_closure_map_t;
md_blob_parse_result_t md_parse_enc_lambda_and_closure_map(mdhandle_t handle, uint8_t const* blob, size_t blob_len, md_enc_lambda_and_closure_map_t* lambda_and_closure_map, size_t* buffer_len);


#ifdef __cplusplus
}
#endif

#endif // _SRC_INC_DNMD_PDB_H

