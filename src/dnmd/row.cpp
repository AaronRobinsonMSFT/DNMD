#include "internal.hpp"
#include <algorithm>
#include <array>
#include <memory>

namespace
{
    using namespace dnmd;

    using table_row_counts = std::array<uint64_t, mdtid_End>;

    struct reference_size_change
    {
        mdtable_id_t table;
        table_row_counts const& rows;
    };

    struct heap_size_change
    {
        mdheap_id_t heap;
        uint32_t count;
    };

    template<mdtable_id_t... Tables>
    table_row_counts get_row_counts(mdcxt_t* context, detail::table_sequence<Tables...>)
    {
        static_assert(sizeof...(Tables) == mdtid_End);
        return {{context->tables[Tables].row_count...}};
    }

#ifdef DNMD_PORTABLE_PDB
    template<mdtable_id_t... Tables>
    void include_external_rows(table_row_counts& rows, uint32_t const* external,
        detail::table_sequence<Tables...>)
    {
        ((rows[Tables] += external[Tables]), ...);
    }
#endif

    template<mdtable_id_t Table>
    raw_cursor<Table> position(mdcxt_t* context, uint32_t row)
    {
        return raw_cursor<Table>::at(context, row);
    }

    template<mdtable_id_t Table, typename Kind>
    mdtcol_t column_width(mdtable_t const& table, column<Table, Kind> col, reference_size_change change)
    {
        if constexpr (detail::searchable_column<Kind>::value
            && !std::is_same_v<Kind, column_type_constant>)
        {
            if (detail::references_table(&table, col, change.table))
                return detail::reference_width(detail::max_reference_value(table, col, change.rows), table.cxt->context_flags);
        }
        return static_cast<mdtcol_t>(table.column_details[static_cast<uint8_t>(col.column_index)] & mdtc_widthmask);
    }

    template<mdtable_id_t Table, typename Kind>
    mdtcol_t column_width(mdtable_t const& table, column<Table, Kind> col, heap_size_change change)
    {
        if constexpr (detail::column_heap<Kind>() != mdhid_None)
        {
            if (detail::column_heap<Kind>() == change.heap)
                return detail::reference_width(change.count, table.cxt->context_flags);
        }
        return static_cast<mdtcol_t>(table.column_details[static_cast<uint8_t>(col.column_index)] & mdtc_widthmask);
    }

    template<mdtable_id_t Table, typename Change>
    bool set_column_size_for_max_row_count(mdtable_t& table, Change change)
    {
        constexpr auto count = std::tuple_size_v<decltype(detail::table_columns<Table>())>;
        static_assert(count != 0 && count * 4 <= UINT8_MAX);
        if (!detail::table_storage_valid(table, count))
            return false;
        std::array<mdtcol_t, count> details{};
        uint32_t row_size = 0;
        bool changed = false;
        detail::for_each_column<Table>([&](auto... cols) {
            auto update = [&](auto col) {
                auto index = static_cast<uint8_t>(col.column_index);
                auto previous = table.column_details[index];
                auto width = column_width(table, col, change);
                auto next = static_cast<mdtcol_t>(width | (detail::InsertOffset)(row_size));
                details[index] = next;
                changed = changed || next != previous;
                row_size += width == mdtc_b4 ? 4u : 2u;
            };
            (update(cols), ...);
        });
        if (!changed)
            return true;

        auto release = [context = table.cxt](uint8_t* data) noexcept { detail::free_mdmem(context, data); };
        size_t capacity = 0;
        std::unique_ptr<uint8_t, decltype(release)> data{
            detail::allocate_table_data(&table, static_cast<uint8_t>(row_size), &capacity), release};
        if (!data || table.row_count > capacity / row_size)
            return false;
        auto destination = data.get();
        // Repacking preserves stored values and sort order without invoking heap or reference writes.
        for (auto row = position<Table>(table.cxt, 1); row; row.move(1), destination += row_size)
        {
            row_reader reader{row.value()};
            if (!detail::for_each_column<Table>([&](auto... cols) {
                return (detail::row_storage_access::copy(reader, cols, destination,
                    details[static_cast<uint8_t>(cols.column_index)]) && ...);
            }))
                return false;
        }
        detail::replace_table_data(&table, data.get(), capacity, static_cast<uint8_t>(row_size), details.data());
        data.release();
        return true;
    }

    template<mdtable_id_t... Tables, typename Change>
    bool set_column_sizes(mdcxt_t* context, detail::table_sequence<Tables...>, Change change)
    {
        auto update = [&](auto id) {
            constexpr auto table_id = decltype(id)::value;
            if constexpr (std::tuple_size_v<decltype(detail::table_columns<table_id>())> == 0)
                return true;
            else
            {
                auto& table = context->tables[table_id];
                return !table.is_initialized || set_column_size_for_max_row_count<table_id>(table, change);
            }
        };
        return (update(std::integral_constant<mdtable_id_t, Tables>{}) && ...);
    }

    template<mdtable_id_t Table, typename Kind>
    bool update_shifted_row_references(mdtable_t* table, column<Table, Kind> col,
        mdtable_id_t updated_table, uint32_t changed_row_start, int64_t shift)
    {
        if constexpr (detail::searchable_column<Kind>::value
            && !std::is_same_v<Kind, column_type_constant>)
        {
            if (!detail::references_table(table, col, updated_table))
                return true;
            for (auto row = position<Table>(table->cxt, 1); row; row.move(1))
            {
                mdToken token;
                if (!row_reader{row.value()}.read(col, token))
                    return false;
                auto rid = (detail::RidFromToken)(token);
                if (static_cast<mdtable_id_t>(ExtractTokenType(token)) == updated_table && rid >= changed_row_start)
                {
                    auto shifted = static_cast<uint32_t>(int64_t{rid} + shift);
                    token = CreateTokenType(updated_table) | shifted;
                    if (!row_writer{row.value()}.write(col, token))
                        return false;
                }
            }
            return true;
        }
        else
            return true;
    }

    template<mdtable_id_t... Tables>
    bool update_references_in_tables(mdcxt_t* context, detail::table_sequence<Tables...>, mdtable_id_t updated_table,
        uint32_t changed_row_start, int64_t shift)
    {
        auto new_count = static_cast<uint32_t>(int64_t{context->tables[updated_table].row_count} + shift);
        auto rows = get_row_counts(context, detail::all_tables{});
        rows[updated_table] = new_count;
#ifdef DNMD_PORTABLE_PDB
        auto pdb_rows = rows;
        if (detail::has_pdb(context))
        {
            std::array<uint32_t, mdtid_FirstPdb> external{};
            if (!detail::get_referenced_type_system_table_row_counts(context, external.data()))
                return false;
            include_external_rows(pdb_rows, external.data(), detail::non_pdb_tables{});
        }
#endif
        auto update = [&](auto id) {
            constexpr auto table_id = decltype(id)::value;
            if constexpr (std::tuple_size_v<decltype(detail::table_columns<table_id>())> == 0)
                return true;
            else
            {
                auto& table = context->tables[table_id];
                if (!table.is_initialized) // This table is not used in the current image.
                    return true;
#ifdef DNMD_PORTABLE_PDB
                auto const& counts = table_id >= mdtid_FirstPdb ? pdb_rows : rows;
#else
                auto const& counts = rows;
#endif
                reference_size_change change{updated_table, counts};
                // Widen before shifting; narrow only after rewriting the larger RIDs.
                if (shift >= 0
                    && !set_column_size_for_max_row_count<table_id>(table, change))
                    return false;
                if (!detail::for_each_column<table_id>([&](auto... columns) {
                    return (update_shifted_row_references(&table, columns, updated_table, changed_row_start, shift) && ...);
                }))
                    return false;
                return shift >= 0 || set_column_size_for_max_row_count<table_id>(table, change);
            }
        };
        return (update(std::integral_constant<mdtable_id_t, Tables>{}) && ...);
    }

    template<mdtable_id_t Table>
    void commit(cursor<Table> row)
    {
        auto table = detail::CursorTable(row);
        assert(table->is_adding_new_row);
        // If the table was previously sorted, validate the prior and following rows.
        if (table->is_sorted)
            table->is_sorted = detail::validate_row_sorted_within_table(row);
        table->is_adding_new_row = false;
    }

    template<mdtable_id_t Table, typename F>
    bool constant_column(cursor<Table>, column<Table, column_type_constant> requested, F&& callback)
    {
        auto find = [&](auto col) {
            if constexpr (std::is_same_v<typename decltype(col)::value_type, column_type_constant>)
                return col.column_index == requested.column_index && callback(col);
            else
                return false;
        };
        return detail::for_each_column<Table>([&](auto... columns) {
            return (find(columns) || ...);
        });
    }

    template<typename Kind>
    bool resolve(typename detail::list_traits<Kind>::position_type const& source,
        raw_cursor<detail::list_traits<Kind>::direct>& target)
    {
        using traits = detail::list_traits<Kind>;
        if constexpr (traits::pointer == mdtid_Unused)
        {
            target = source;
            return true;
        }
        else
        {
            auto indirect = indirect_cursor<traits::direct, traits::pointer>{source};
            mdToken token;
            if (!indirect.get_target_token(token))
                return false;
            target = raw_cursor<traits::direct>::at(source.get_handle(), (detail::RidFromToken)(token));
            return true;
        }
    }

    template<mdtable_id_t Owner, typename Kind>
    bool add_to_list(cursor<Owner> owner, column<Owner, Kind> col,
        typename detail::list_traits<Kind>::position_type before,
        typename detail::list_traits<Kind>::position_type first, uint32_t count,
        mdtable_t** added_table, uint32_t* row)
    {
        using traits = detail::list_traits<Kind>;
        constexpr auto child = traits::direct;
        auto target = raw_cursor<child>::nil(owner.get_handle());
        if (!resolve<Kind>(before, target))
            return false;
        auto direct = detail::CursorTable(target);
        auto source = detail::CursorTable(before);
        // A zero-length range may insert at the first row of the following range.
        assert(source == detail::CursorTable(first));
        assert(detail::CursorRow(first) <= detail::CursorRow(before)
            && uint64_t{detail::CursorRow(before)} <= uint64_t{detail::CursorRow(first)} + (count == 0 ? 1 : count));
        if (source != direct)
        {
            if constexpr (traits::pointer != mdtid_Unused)
            {
                // Append the child, then insert its Ptr row at the requested list position.
                bool appended = detail::append_row<child>(direct->cxt, row);
                if (*row != 0)
                    *added_table = direct;
                if (!appended)
                    return false;
                auto child_row = position<child>(direct->cxt, *row).value();
                uint32_t pointer_row = 0;
                bool success = detail::insert_row_relative(before.template as<traits::pointer>(), false, &pointer_row);
                if (success)
                {
                    auto pointer = position<traits::pointer>(source->cxt, pointer_row).value();
                    success = row_writer{pointer}.write(detail::pointer_column<traits::pointer>(), child_row);
                    if (success && (count == 0 || detail::CursorRow(first) == detail::CursorRow(before)))
                    {
                        // A zero count means this is the first element in the list.
                        // Inserting at its start also changes the parent's boundary.
                        // Otherwise, the new element would belong to the preceding parent.
                        success = row_writer{owner}.write(col, pointer);
                    }
                }
                if (pointer_row != 0)
                    commit(position<traits::pointer>(source->cxt, pointer_row).value());
                if (!success)
                {
                    // Finish only rows acquired by this operation; these edits are not rolled back.
                    commit(child_row);
                    *added_table = nullptr;
                    *row = 0;
                }
                return success;
            }
            else
                return false;
        }

        if (detail::CursorRow(before) == direct->row_count + 1)
        {
            // Without indirection, appending at the table's end needs no Ptr table.
            bool appended = detail::append_row<child>(direct->cxt, row);
            if (*row != 0)
                *added_table = direct;
            if (!appended)
                return false;
            if (count == 0)
            {
                // This is the parent's first element. Update its boundary and all
                // preceding rows sharing that boundary so the lists remain consistent.
                raw_cursor<Owner> parent{owner};
                mdToken current;
                if (!row_reader{owner}.read(col, current))
                    return false;
                while (parent.move(-1))
                {
                    mdToken previous;
                    if (!row_reader{parent.value()}.read(col, previous))
                        return false;
                    if ((detail::RidFromToken)(previous) != (detail::RidFromToken)(current))
                    {
                        parent.move(1);
                        break;
                    }
                }
                auto child_row = position<child>(direct->cxt, *row).value();
                for (; detail::CursorRow(parent) <= detail::CursorRow(owner); parent.move(1))
                {
                    if (!row_writer{parent.value()}.write(col, child_row))
                        return false;
                }
            }
            return true;
        }

        if constexpr (traits::pointer != mdtid_Unused)
        {
            // Inserting within a direct list requires a Ptr table.
            if (!detail::create_and_fill_indirect_table<child, traits::pointer>(direct->cxt))
                return false;
            // New Ptr rows initially correspond to the same RIDs in the direct table.
            auto pointer = traits::position_type::at(owner.get_handle(),
                detail::CursorRow(first), traits::pointer);
            return add_to_list(owner, col, pointer, pointer, count, added_table, row);
        }
        else
            return false;
    }

    struct unsorted_list {};

    template<mdtable_id_t Owner, typename Kind, typename Sort>
    bool add_list_row(cursor<Owner> owner, column<Owner, Kind> col, Sort sort,
        uint32_t sort_value, mdtable_t** added_table, uint32_t* row)
    {
        if (added_table != nullptr)
            *added_table = nullptr;
        if (row != nullptr)
            *row = 0;
        if (added_table == nullptr || row == nullptr || !detail::is_list_column(col))
            return false;
        using traits = detail::list_traits<Kind>;
        using source_position = typename traits::position_type;
        // Get the parent's existing range, in its Ptr table when one is present.
        auto value = Kind::nil(owner.get_handle());
        uint32_t count;
        if (!row_reader{owner}.read(col, value, &count))
            return false;
        source_position first{value};
        auto table = detail::CursorTable(first);
        if (!table->is_initialized)
        {
            // Create the absent child table and rebind its empty range to one-past the end.
            if (!detail::initialize_new_table_details(first) || !detail::allocate_new_table(table))
                return false;
            first = source_position::at(owner.get_handle(), 1, static_cast<mdtable_id_t>(table->table_id));
        }
        auto before = first;
        if (count > INT32_MAX || !before.move(static_cast<int32_t>(count)))
            return false;

        if constexpr (!std::is_same_v<Sort, unsorted_list>)
        {
            if (count != 0)
            {
                // Most inserts are at the end; search backwards for the last value <= the new value.
                auto check = before;
                for (; check.move(-1) && detail::CursorRow(check) >= detail::CursorRow(first);)
                {
                    auto target = raw_cursor<traits::direct>::nil(owner.get_handle());
                    uint32_t current = 0;
                    if (!resolve<Kind>(check, target) || !target
                        || !constant_column(target.value(), sort, [&](auto key) {
                            return row_reader{target.value()}.read(key, &current);
                        }))
                        return false;
                    if (current <= sort_value)
                    {
                        before = check;
                        before.move(1);
                        break;
                    }
                }
                // If no earlier value qualifies, insert at the beginning of the list.
                if (detail::CursorRow(check) == 1 || detail::CursorRow(check) < detail::CursorRow(first))
                    before = first;
            }
        }
        if (!add_to_list(owner, col, before, first, count, added_table, row))
            return false;
        if constexpr (!std::is_same_v<Sort, unsorted_list>)
        {
            // Initialize the sort value after acquisition so the caller owns any partial result.
            return constant_column(position<traits::direct>((*added_table)->cxt, *row).value(),
                sort, [&](auto key) {
                    return row_writer{position<traits::direct>((*added_table)->cxt, *row).value()}.write(key, sort_value);
                });
        }
        else
        {
            (void)sort;
            (void)sort_value;
            return true;
        }
    }
}

namespace dnmd::detail
{
bool add_row_to_list(cursor<mdtid_TypeDef> owner, decltype(columns::TypeDef_FieldList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

bool add_row_to_list(cursor<mdtid_TypeDef> owner, decltype(columns::TypeDef_MethodList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

bool add_row_to_list(cursor<mdtid_MethodDef> owner, decltype(columns::MethodDef_ParamList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

bool add_row_to_list(cursor<mdtid_MethodDef> owner, decltype(columns::MethodDef_ParamList) list,
    column<mdtid_Param, column_type_constant> sort, uint32_t value, mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, sort, value, table, row);
}

bool add_row_to_list(cursor<mdtid_EventMap> owner, decltype(columns::EventMap_EventList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

bool add_row_to_list(cursor<mdtid_PropertyMap> owner, decltype(columns::PropertyMap_PropertyList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

#ifdef DNMD_PORTABLE_PDB
bool add_row_to_list(cursor<mdtid_LocalScope> owner, decltype(columns::LocalScope_VariableList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

bool add_row_to_list(cursor<mdtid_LocalScope> owner, decltype(columns::LocalScope_ConstantList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

#endif
}

bool update_heap_column_sizes(mdcxt_t* context, mdheap_id_t heap, uint32_t new_max_count)
{
    assert(context != nullptr);
    if (heap != mdhid_String && heap != mdhid_Blob && heap != mdhid_Guid && heap != mdhid_UserString)
        return false;
    auto flag = detail::get_large_heap_flag(heap);
    if (flag != mdc_none)
    {
        auto flags = static_cast<uint32_t>(context->context_flags);
        bool large = (flags & mdc_minimal_delta) || new_max_count > UINT16_MAX;
        flags = large ? flags | static_cast<uint32_t>(flag) : flags & ~static_cast<uint32_t>(flag);
        context->context_flags = static_cast<mdcxt_flag_t>(flags);
    }
    heap_size_change change{heap, new_max_count};
    if (!set_column_sizes(context, detail::non_pdb_tables{}, change))
        return false;
#ifdef DNMD_PORTABLE_PDB
    return set_column_sizes(context, detail::pdb_tables{}, change);
#else
    return true;
#endif
}

#ifdef DNMD_PORTABLE_PDB
bool update_pdb_column_sizes(mdcxt_t* context, mdtable_id_t updated_table, uint32_t const* row_counts)
{
    assert(context != nullptr && row_counts != nullptr);
    assert(updated_table >= mdtid_First && updated_table < mdtid_FirstPdb);
    auto rows = get_row_counts(context, detail::all_tables{});
    include_external_rows(rows, row_counts, detail::non_pdb_tables{});
    return set_column_sizes(context, detail::pdb_tables{}, reference_size_change{updated_table, rows});
}
#endif

bool update_table_references_for_shifted_rows(mdcxt_t* context, mdtable_id_t updated_table,
    uint32_t changed_row_start, int64_t shift)
{
    assert(context != nullptr && updated_table != mdtid_Unused);
    // Make sure we aren't shifting into negative row ids or above the max row id.
    assert(changed_row_start + shift > 0 && changed_row_start + shift < 0x00ffffff);
    return update_references_in_tables(context, detail::all_tables{}, updated_table, changed_row_start, shift);
}
