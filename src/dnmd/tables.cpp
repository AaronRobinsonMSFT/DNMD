#include "internal.hpp"
#include <array>

namespace
{
    using namespace dnmd;

    template<mdtable_id_t... Tables>
    constexpr auto column_counts(detail::table_sequence<Tables...>) noexcept
    {
        return std::array<uint8_t, sizeof...(Tables)>{
            static_cast<uint8_t>(std::tuple_size_v<decltype(detail::table_columns<Tables>())>)...};
    }

    template<mdtable_id_t Table>
    bool initialize(uint32_t const* rows, mdcxt_flag_t flags, bool sorted, mdtable_t* table)
    {
        return detail::initialize_table_details<Table>(rows, flags, sorted, *table);
    }

    template<mdtable_id_t... Tables>
    constexpr auto initializers(detail::table_sequence<Tables...>) noexcept
    {
        return std::array{&initialize<Tables>...};
    }

    constexpr auto table_column_counts = column_counts(detail::all_tables{});
    constexpr auto table_initializers = initializers(detail::all_tables{});
}

extern "C" uint8_t get_table_column_count(mdtable_id_t id)
{
    assert(mdtid_First <= id && id < mdtid_End);
    return table_column_counts[id];
}

extern "C" bool initialize_table_details(uint32_t const* rows, mdcxt_flag_t flags,
    mdtable_id_t id, bool sorted, mdtable_t* table)
{
    assert(rows != nullptr && table != nullptr && mdtid_First <= id && id < mdtid_End);
    return table_initializers[id](rows, flags, sorted, table);
}

extern "C" bool consume_table_rows(mdtable_t* table, uint8_t const** data, size_t* length)
{
    assert(table != nullptr && data != nullptr && length != nullptr);
    assert(table->row_size_bytes != 0);
    if (table->row_count == 0)
        return true;
    if (table->row_size_bytes == 0 || table->row_count > *length / table->row_size_bytes)
        return false;

    auto size = size_t{table->row_count} * table->row_size_bytes;
    table->data.ptr = *data;
    table->data.size = size;
    *data += size;
    *length -= size;
    return true;
}

extern "C" mdtable_id_t get_corresponding_indirection_table(mdtable_id_t id)
{
    return dnmd::detail::pointer_table_id(id);
}
