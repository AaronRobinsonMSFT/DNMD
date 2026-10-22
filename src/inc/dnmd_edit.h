#ifndef _SRC_INC_DNMD_EDIT_H_
#define _SRC_INC_DNMD_EDIT_H_

#include "dnmd.h"

#ifdef __cplusplus
extern "C" {
#endif

bool md_cpp_append_row(mdhandle_t handle, mdtable_id_t table, uint32_t* row);
bool md_cpp_insert_row(mdtable_t* table, uint32_t row, bool after, uint32_t* added_row);
bool md_cpp_add_list_row(mdtable_t* owner, uint32_t row, col_index_t list,
    bool sorted, col_index_t sort_column, uint32_t sort_value,
    mdtable_id_t* added_table, uint32_t* added_row);

#ifdef __cplusplus
}
#endif

#endif // _SRC_INC_DNMD_EDIT_H_
