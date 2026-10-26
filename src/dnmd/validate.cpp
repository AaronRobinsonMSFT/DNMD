#include "schema.hpp"

bool validate_tables(mdcxt_t* context)
{
    using namespace dnmd;

    if (!(context->context_flags & mdc_uncompressed_table_heap))
    {
        for (auto table : {mdtid_FieldPtr, mdtid_MethodPtr, mdtid_ParamPtr, mdtid_EventPtr, mdtid_PropertyPtr})
        {
            if (context->tables[table].row_count != 0)
                return false;
        }
    }

    for (uint32_t table = mdtid_First; table < mdtid_End; ++table)
    {
        auto const& storage = context->tables[table];
        if (!storage.is_sorted || storage.row_count <= 1)
            continue;
        if (!storage.is_initialized)
            return false;

        bool valid = true;
        auto position = any_cursor::at(context, 1, static_cast<mdtable_id_t>(table));
        position.visit([&](auto first) {
            constexpr auto id = cursor_tableid_v<decltype(first)>;
            if constexpr (std::tuple_size_v<decltype(detail::table_sort_keys<id>())> != 0)
            {
                if (!detail::table_storage_valid(storage,
                        std::tuple_size_v<decltype(detail::table_columns<id>())>))
                {
                    valid = false;
                    return;
                }
                raw_cursor<id> current{first};
                for (uint32_t row = 1; row < storage.row_count; ++row)
                {
                    auto next = current;
                    if (!next.move(1) || !detail::rows_sorted(current.value(), next.value()))
                    {
                        valid = false;
                        return;
                    }
                    current = next;
                }
            }
        });
        if (!valid)
            return false;
    }
    return true;
}
