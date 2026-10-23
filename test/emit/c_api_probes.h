#ifndef DNMD_TEST_EMIT_C_API_PROBES_H
#define DNMD_TEST_EMIT_C_API_PROBES_H

#include <dnmd.h>

#ifdef __cplusplus
extern "C" {
#endif

bool test_c_token_is_valid(mdhandle_t handle, mdToken token);
size_t test_c_table_size(void);
bool test_c_table_is_initialized(mdhandle_t handle, mdtable_id_t table);
bool test_c_first_cursor(mdhandle_t handle, mdtable_id_t table, mdToken* token, uint32_t* count);
bool test_c_column_token(mdhandle_t handle, mdToken row, col_index_t column, mdToken* token);
bool test_c_column_constant(mdhandle_t handle, mdToken row, col_index_t column, uint32_t* value);
bool test_c_column_blob_is_valid(mdhandle_t handle, mdToken row, col_index_t column);
bool test_c_column_cursor(mdhandle_t handle, mdToken row, col_index_t column, mdToken* token);
bool test_c_resolve(mdhandle_t handle, mdToken source, mdToken* target);
md_range_result_t test_c_find_range(mdhandle_t handle, mdToken row, col_index_t column,
    uint32_t key, mdToken* start, uint32_t* count);
bool test_c_move(mdhandle_t handle, mdToken source, int32_t delta, mdToken* target);

#ifdef __cplusplus
}
#endif

#endif
