#ifndef _SRC_DNMD_INTERNAL_H_
#define _SRC_DNMD_INTERNAL_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <corhdr.h>
#include <dnmd.h>

// Implementations for missing bounds checking APIs.
// See https://en.cppreference.com/w/c/error#Bounds_checking
#if !defined(__STDC_LIB_EXT1__) && !defined(BUILD_WINDOWS)
typedef size_t rsize_t;
#endif // !__STDC_LIB_EXT1__

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

// Safe arithmetic to prevent size_t overflow in allocations.
static inline bool safe_add_size(size_t a, size_t b, size_t* result)
{
    if (a > SIZE_MAX - b)
        return false;
    *result = a + b;
    return true;
}

static inline bool safe_mul_size(size_t a, size_t b, size_t* result)
{
    if (a != 0 && b > SIZE_MAX / a)
        return false;
    *result = a * b;
    return true;
}

#ifndef NDEBUG
#define ASSERT_ASSUME(x) assert(x)
#elif defined(_MSC_VER)
#define ASSERT_ASSUME(x) __assume(x)
#elif defined(__clang__)
#define ASSERT_ASSUME(x) __builtin_assume(x)
#elif defined(__GNUC__)
#define ASSERT_ASSUME(x) do { if (!(x)) __builtin_unreachable(); } while (0)
#else
#define ASSERT_ASSUME(x) (void)(x)
#endif

// Mutable data
typedef struct mddata__
{
    uint8_t* ptr;
    size_t size;
} mddata_t;

// II.24.2.6 - 64 is the maximum value
#define MDTABLE_MAX_COUNT ((size_t)mdtid_End)
static_assert(MDTABLE_MAX_COUNT <= 64, "Specification sets max table count to 64");

#define MDTABLE_MAX_COLUMN_COUNT 9

// Macros used to insert/extract the column offset.
#define InsertOffset(o) ((o << 2) & mdtc_comask)
#define ExtractOffset(o) ((o & mdtc_comask) >> 2)

// Extract a context from the mdhandle_t.
mdcxt_t* extract_mdcxt(mdhandle_t md);

// Allocate and free tracked memory.
void* alloc_mdmem(mdcxt_t* cxt, size_t length);
void free_mdmem(mdcxt_t* cxt, void* mem);

// Merge the supplied delta into the context.
bool merge_in_delta(mdcxt_t* cxt, mdcxt_t* delta);
bool initialize_minimal_table_rows(mdcxt_t* cxt);
bool dump_tables(mdcxt_t* cxt, int32_t table);

//
// Streams
//

mdstream_t* get_heap_by_id(mdcxt_t* cxt, mdheap_id_t heap_id);
mdcxt_flag_t get_large_heap_flag(mdheap_id_t heap_id);

// Strings heap, #Strings - II.24.2.3
bool try_get_string(mdcxt_t* cxt, size_t offset, char const** str);
bool validate_strings_heap(mdcxt_t* cxt);
uint32_t add_to_string_heap(mdcxt_t* cxt, char const* str);

// User strings heap, #US - II.24.2.4
bool try_get_user_string(mdcxt_t* cxt, size_t offset, mduserstring_t* str, size_t* next_offset);
bool validate_user_string_heap(mdcxt_t* cxt);
uint32_t add_to_user_string_heap(mdcxt_t* cxt, char16_t const* str);

// Blob heap, #Blob - II.24.2.4
bool try_get_blob(mdcxt_t* cxt, size_t offset, uint8_t const** blob, uint32_t* blob_len);
bool validate_blob_heap(mdcxt_t* cxt);
uint32_t add_to_blob_heap(mdcxt_t* cxt, uint8_t const* data, uint32_t length);

// GUID heap, #GUID - II.24.2.5
bool try_get_guid(mdcxt_t* cxt, size_t idx, mdguid_t* guid);
bool validate_guid_heap(mdcxt_t* cxt);
uint32_t add_to_guid_heap(mdcxt_t* cxt, mdguid_t guid);

// Table heap, #~ - II.24.2.6
// Note: This can only be done after all streams have been read in.
bool initialize_tables(mdcxt_t* cxt);
bool validate_tables(mdcxt_t* cxt);

// PDB heap, #Pdb - https://github.com/dotnet/runtime/blob/main/docs/design/specs/PortablePdb-Metadata.md#pdb-stream
typedef struct md_pdb__
{
    uint8_t pdb_id[20];
    mdToken entry_point;
    uint64_t referenced_type_system_tables;
    uint32_t type_system_table_rows[MDTABLE_MAX_COUNT];
} md_pdb_t;

// Check if the context has a non-empty PDB stream.
bool has_pdb(mdcxt_t* cxt);

// Interpret in the PDB data stream
// The md_pdb_t will be fully initialized if "true" is returned.
bool try_get_pdb(mdcxt_t* cxt, md_pdb_t* pdb);

//
// Tables
//

// Get the column count for a table.
uint8_t get_table_column_count(mdtable_id_t id);

// Initialize the supplied table details
bool initialize_table_details(
    uint32_t const* all_table_row_counts,
    mdcxt_flag_t context_flags,
    mdtable_id_t id,
    bool is_sorted,
    mdtable_t* table);

// Given the current table, consume the data stream assuming it contains the rows
bool consume_table_rows(mdtable_t* table, uint8_t const** data, size_t* data_len);

// Get the indirection table for a given table
mdtable_id_t get_corresponding_indirection_table(mdtable_id_t table_id);

//
// Manipulation of bits
//

uint32_t align_to(uint32_t val, uint32_t align);
size_t count_set_bits(uint64_t val);

//
// Byte streams
//

bool advance_stream(uint8_t const** data, size_t* data_len, size_t b);

bool read_u8(uint8_t const** data, size_t* data_len, uint8_t* o);
bool read_i8(uint8_t const** data, size_t* data_len, int8_t* o);
bool read_u16(uint8_t const** data, size_t* data_len, uint16_t* o);
bool read_i16(uint8_t const** data, size_t* data_len, int16_t* o);
bool read_u32(uint8_t const** data, size_t* data_len, uint32_t* o);
bool read_i32(uint8_t const** data, size_t* data_len, int32_t* o);
bool read_u64(uint8_t const** data, size_t* data_len, uint64_t* o);
bool read_i64(uint8_t const** data, size_t* data_len, int64_t* o);

bool advance_output_stream(uint8_t** data, size_t* data_len, size_t b);

bool write_u8(uint8_t** data, size_t* data_len, uint8_t o);
bool write_i8(uint8_t** data, size_t* data_len, int8_t o);
bool write_u16(uint8_t** data, size_t* data_len, uint16_t o);
bool write_i16(uint8_t** data, size_t* data_len, int16_t o);
bool write_u32(uint8_t** data, size_t* data_len, uint32_t o);
bool write_i32(uint8_t** data, size_t* data_len, int32_t o);
bool write_u64(uint8_t** data, size_t* data_len, uint64_t o);
bool write_i64(uint8_t** data, size_t* data_len, int64_t o);

// II.23.2
bool decompress_u32(uint8_t const** data, size_t* data_len, uint32_t* o);
bool decompress_i32(uint8_t const** data, size_t* data_len, int32_t* o);
// compressed_len is an in/out parameter. If compress_u32 returns true, then
// compressed_len is set to the number of bytes written to compressed.
bool compress_u32(uint32_t data, uint8_t* compressed, size_t* compressed_len);

// Editing
bool create_and_fill_indirect_table(mdtable_t* original, mdtable_t* indirect);
bool allocate_new_table(mdtable_t* table);
uint8_t* get_writable_table_data(mdtable_t* table, bool make_writable);
uint8_t* allocate_table_data(mdtable_t* table, uint8_t row_size, size_t* capacity);
void replace_table_data(mdtable_t* table, uint8_t* data, size_t capacity, uint8_t row_size, mdtcol_t const* columns);
bool update_heap_column_sizes(mdcxt_t* cxt, mdheap_id_t heap, uint32_t new_max_count);
bool update_table_references_for_shifted_rows(mdcxt_t* cxt, mdtable_id_t updated_table, uint32_t changed_row_start, int64_t shift);
// Physical insertion publishes the RID after acquiring a pending row.
bool insert_row_into_table(mdtable_t* table, uint32_t row_index, uint32_t* new_row);
#ifdef DNMD_PORTABLE_PDB
bool get_referenced_type_system_table_row_counts(mdcxt_t* cxt, uint32_t* row_counts);
bool update_pdb_column_sizes(mdcxt_t* cxt, mdtable_id_t updated_table, uint32_t const* row_counts);
bool update_referenced_type_system_table_row_count(mdcxt_t* cxt, mdtable_id_t updated_table, uint32_t new_max_row_count);
#endif // DNMD_PORTABLE_PDB

// Add the heap with the specified id from the delta image to the cxt image.
bool append_heap(mdcxt_t* cxt, mdcxt_t* delta, mdheap_id_t heap_id);

extern mdguid_t const empty_guid;

#endif // _SRC_DNMD_INTERNAL_H_
