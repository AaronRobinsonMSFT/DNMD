#ifndef _SRC_INC_DNMD_PDB_H
#define _SRC_INC_DNMD_PDB_H

#include <dnmd.h>

#ifdef __cplusplus
extern "C" {
#endif

// Methods to parse specialized blob formats defined in the Portable PDB spec.
// https://github.com/dotnet/runtime/blob/main/docs/design/specs/PortablePdb-Metadata.md

// Parse a DocumentName blob into a UTF-8 string.
bool md_parse_document_name(uint8_t const* blob, size_t blob_len, char const** name, size_t* name_len);

// Parse a SequencePoints blob.
typedef struct md_sequence_points__ md_sequence_points_t;
bool md_parse_sequence_points(mdhandle_t handle, uint8_t const* blob, size_t blob_len, md_sequence_points_t* sequence_points, size_t* buffer_len);

// Parse a LocalConstantSig blob.
typedef struct md_local_constant_sig__ md_local_constant_sig_t;
bool md_parse_local_constant_sig(mdhandle_t handle, uint8_t const* blob, size_t blob_len, md_local_constant_sig_t* local_constant_sig, size_t* buffer_len);

// Parse an Imports blob.
typedef struct md_imports__ md_imports_t;
bool md_parse_imports(mdhandle_t handle, uint8_t const* blob, size_t blob_len, md_imports_t* imports, size_t* buffer_len);

// Parse an Edit-and-Continue Local Slot Map blob.
typedef struct md_enc_local_slot_map__ md_enc_local_slots_map_t;
bool md_parse_enc_local_slot_map(uint8_t const* blob, size_t blob_len, md_enc_local_slot_map_t* local_slot_map, size_t* buffer_len);

// Parse an Edit-and-Continue Lambda and Closure Map blob.
typedef struct md_enc_lambda_and_closure_map__ md_enc_lambda_and_closure_map_t;
bool md_parse_enc_lambda_and_closure_map(uint8_t const* blob, size_t blob_len, md_enc_lambda_and_closure_map_t* lambda_and_closure_map, size_t* buffer_len);


#ifdef __cplusplus
}
#endif

#endif // _SRC_INC_DNMD_PDB_H

