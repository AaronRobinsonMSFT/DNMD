#ifndef _SRC_INC_DNMD_PDB_H_
#define _SRC_INC_DNMD_PDB_H_

#include "dnmd.h"

#ifdef __cplusplus
extern "C" {
#endif

// Read the #Pdb identity. The in/out length reports the required buffer size.
// Returns false for a missing stream, invalid argument, or insufficient buffer.
bool md_get_pdb_id(mdhandle_t handle, size_t* pdb_id_len, uint8_t* pdb_id);

#ifdef __cplusplus
}
#endif

#endif // _SRC_INC_DNMD_PDB_H_
