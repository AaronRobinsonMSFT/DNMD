#include "schema.hpp"

namespace
{
    using namespace dnmd;

    template<mdtable_id_t Table, typename ColumnType>
    bool dump_column(cursor<Table> row, row_reader<Table> const& reader, column<Table, ColumnType> col)
    {
        auto raw = detail::row_storage_access::read(reader, col);
        auto invalid = [&] {
            std::printf("Invalid (%u) [%#x]|", static_cast<uint8_t>(col.column_index), raw);
            return true;
        };
        if constexpr (std::is_same_v<ColumnType, column_type_string>)
        {
            char const* value;
            if (!reader.read(col, &value))
                return invalid();
            std::printf("'%s' [%#x]|", value, raw);
        }
        else if constexpr (std::is_same_v<ColumnType, column_type_guid>)
        {
            mdguid_t value;
            if (!reader.read(col, &value))
                return invalid();
            std::printf("{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x} [%#x]|",
                value.data1, value.data2, value.data3, value.data4[0], value.data4[1],
                value.data4[2], value.data4[3], value.data4[4], value.data4[5],
                value.data4[6], value.data4[7], raw);
        }
        else if constexpr (std::is_same_v<ColumnType, column_type_blob>)
        {
            uint8_t const* value;
            uint32_t length;
            if (!reader.read(col, &value, &length))
                return invalid();
#ifdef DNMD_PORTABLE_PDB
            if constexpr (Table == mdtid_MethodDebugInformation)
            {
                if (col.column_index == columns::MethodDebugInformation_SequencePoints.column_index)
                    return detail::dump_sequence_points(row, value, length, raw);
            }
            int special = detail::dump_pdb_blob(detail::CursorTable(row)->cxt, Table, col.column_index, value, length, raw);
            if (special != 0)
                return special > 0;
#else
            (void)row;
#endif
            auto heap = detail::CursorTable(row)->cxt->blob_heap.ptr;
            size_t offset = value == nullptr || heap == nullptr ? 0 : static_cast<size_t>(value - heap);
            std::printf("Offset: %zu (len: %u) [%#x]|", offset, length, raw);
        }
        else if constexpr (std::is_same_v<ColumnType, column_type_userstring>)
        {
            mduserstring_t value;
            if (!reader.read(col, &value))
                return invalid();
            std::printf("UTF-16 string (%u bytes) [%#x]|", value.str_bytes, raw);
        }
        else if constexpr (std::is_same_v<ColumnType, column_type_constant>)
        {
            uint32_t value;
            if (!reader.read(col, &value))
                return invalid();
            std::printf("0x%08x [%#x]|", value, raw);
        }
        else
        {
            mdToken token;
            if (!reader.read(col, token))
                return invalid();
            std::printf("0x%08x (mdToken) [%#x]|", token, raw);
        }
        return true;
    }

    template<mdtable_id_t Table>
    bool dump_rows(cursor<Table> first)
    {
        auto const& table = *detail::CursorTable(first);
        if (!detail::table_storage_valid(table,
                std::tuple_size_v<decltype(detail::table_columns<Table>())>))
        {
            std::printf("Failure to retrieve raw column values. Table is corrupted.\n");
            return false;
        }
        auto position = raw_cursor<Table>{first};
        for (uint32_t row = 0; row < table.row_count; ++row, position.move(1))
        {
            std::printf("%4u|", row);
            row_reader reader{position.value()};
            if (!std::apply([&](auto... columns) {
                return (dump_column(position.value(), reader, columns) && ...);
            }, detail::table_columns<Table>()))
                return false;
            std::printf("\n");
        }
        return true;
    }
}

bool dump_tables(mdcxt_t* context, int32_t selected)
{
    for (int32_t table = mdtid_First; table < mdtid_End; ++table)
    {
        // Check if the user supplied a table to dump.
        if (selected > -1 && table != selected)
            continue;
        auto const& storage = context->tables[table];
        if (storage.row_count == 0)
            std::printf("Empty table\n");
        else
        {
            std::printf("Table %u (0x%x) rows: %u\n", storage.table_id, storage.table_id, storage.row_count);
            bool success = false;
            if (storage.is_initialized)
            {
                // Create a cursor to the first row.
                auto position = dnmd::any_cursor::at(context, 1, static_cast<mdtable_id_t>(table));
                position.visit([&](auto row) { success = dump_rows(row); });
            }
            if (!success)
            {
                std::printf("Failure in table '%u'\n", table);
                return false;
            }
        }
        std::printf("\n");
    }
    return true;
}
