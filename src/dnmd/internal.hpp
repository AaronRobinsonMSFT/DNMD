#ifndef _SRC_DNMD_INTERNAL_HPP_
#define _SRC_DNMD_INTERNAL_HPP_

#include <dnmd.hpp>
#include <cinttypes>
#include <cstdio>

extern "C"
{
bool initialize_minimal_table_rows(mdcxt_t* context);
bool validate_tables(mdcxt_t* context);
bool merge_in_delta(mdcxt_t* context, mdcxt_t* delta);
bool dump_tables(mdcxt_t* context, int32_t table);
bool append_heap(mdcxt_t* context, mdcxt_t* delta, mdtcol_t heap);
bool create_and_fill_indirect_table(mdcxt_t* context, mdtable_id_t direct, mdtable_id_t indirect);
bool update_table_references_for_shifted_rows(mdcxt_t* context, mdtable_id_t updated_table,
    uint32_t changed_row_start, int64_t shift);
bool decompress_u32(uint8_t const** data, size_t* length, uint32_t* value);
bool decompress_i32(uint8_t const** data, size_t* length, int32_t* value);
}

namespace dnmd::detail
{
extern "C" bool set_column_size_for_max_row_count(mdtable_t* table, mdtable_id_t updated_table,
    mdtcol_t updated_heap, uint32_t new_max_row_count);

inline bool table_storage_valid(mdtable_t const& table, size_t columns) noexcept
{
    if (!table.is_initialized || table.column_count != columns || table.column_details == nullptr
        || table.row_size_bytes == 0 || table.data.ptr == nullptr
        || table.row_count > table.data.size / table.row_size_bytes)
        return false;
    for (size_t column = 0; column < columns; ++column)
    {
        auto details = static_cast<uint32_t>(table.column_details[column]);
        auto width = details & mdtc_widthmask;
        if (width != mdtc_b2 && width != mdtc_b4)
            return false;
        auto offset = (details & mdtc_comask) >> 8;
        auto bytes = width == mdtc_b4 ? 4u : 2u;
        if (offset > table.row_size_bytes || bytes > table.row_size_bytes - offset)
            return false;
    }
    return true;
}

struct row_storage_access
{
    template<mdtable_id_t Table, typename ColumnType>
    static uint32_t read(row_reader<Table> const& reader, column<Table, ColumnType> col) noexcept
    {
        return reader.read_raw(col);
    }

    template<mdtable_id_t Table, typename HeapType>
    static bool write_heap_index(row_writer<Table> const& writer,
        column<Table, HeapType> col, uint32_t value) noexcept
    {
        static_assert(std::is_same_v<HeapType, column_type_string>
            || std::is_same_v<HeapType, column_type_blob>
            || std::is_same_v<HeapType, column_type_guid>
            || std::is_same_v<HeapType, column_type_userstring>);
        return writer.template write_raw<false, true>(col, value);
    }
};

#ifdef DNMD_PORTABLE_PDB
int dump_pdb_blob(mdcxt_t* context, mdtable_id_t table, col_index_t column,
    uint8_t const* blob, uint32_t length, uint32_t raw);
bool dump_sequence_points(cursor<mdtid_MethodDebugInformation> method,
    uint8_t const* blob, uint32_t length, uint32_t raw);
#endif
}

#endif // _SRC_DNMD_INTERNAL_HPP_
