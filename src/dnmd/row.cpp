#include "schema.hpp"

namespace
{
    using namespace dnmd;

    template<mdtable_id_t Table>
    raw_cursor<Table> position(mdcxt_t* context, uint32_t row)
    {
        return raw_cursor<Table>::at(context, row);
    }

    template<mdtable_id_t Table, mdtable_id_t... Targets>
    bool references_table(mdtable_t*, column<Table, raw_cursor<Targets...>>, mdtable_id_t target)
    {
        return ((target == Targets) || ...);
    }

    template<mdtable_id_t Table, mdtable_id_t Direct, mdtable_id_t Ptr>
    bool references_table(mdtable_t* table, column<Table, indirect_cursor<Direct, Ptr>>, mdtable_id_t target)
    {
        return detail::list_target_table<Direct, Ptr>(table) == target;
    }

    template<mdtable_id_t Table, typename Kind>
    bool update_shifted_row_references(mdtable_t* table, column<Table, Kind> col,
        mdtable_id_t updated_table, uint32_t changed_row_start, int64_t shift)
    {
        if constexpr (detail::searchable_column<Kind>::value
            && !std::is_same_v<Kind, column_type_constant>)
        {
            if (!references_table(table, col, updated_table))
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

    template<size_t... Tables>
    bool update_references_in_tables(mdcxt_t* context, std::index_sequence<Tables...>, mdtable_id_t updated_table,
        uint32_t changed_row_start, int64_t shift)
    {
        auto new_count = static_cast<uint32_t>(int64_t{context->tables[updated_table].row_count} + shift);
        auto update = [&](auto id) {
            constexpr auto table_id = decltype(id)::value;
            if constexpr (std::tuple_size_v<decltype(detail::table_columns<table_id>())> == 0)
                return true;
            else
            {
                auto& table = context->tables[table_id];
                if (!table.is_initialized) // This table is not used in the current image.
                    return true;
                // Widen before shifting; narrow only after rewriting the larger RIDs.
                if (shift >= 0
                    && !detail::set_column_size_for_max_row_count(&table, updated_table, mdtc_none, new_count))
                    return false;
                if (!std::apply([&](auto... columns) {
                    return (update_shifted_row_references(&table, columns, updated_table, changed_row_start, shift) && ...);
                }, detail::table_columns<table_id>()))
                    return false;
                return shift >= 0 || detail::set_column_size_for_max_row_count(&table, updated_table, mdtc_none, new_count);
            }
        };
        return (update(std::integral_constant<mdtable_id_t, static_cast<mdtable_id_t>(Tables)>{}) && ...);
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
        return std::apply([&](auto... columns) {
            return (find(columns) || ...);
        }, detail::table_columns<Table>());
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
            if (!create_and_fill_indirect_table(direct->cxt, child, traits::pointer))
                return false;
            auto table = detail::CursorTable(owner);
            auto& details = table->column_details[static_cast<uint8_t>(col.column_index)];
            details = static_cast<mdtcol_t>((static_cast<uint32_t>(details) & ~mdtc_timask)
                | (static_cast<uint32_t>(traits::pointer) << 16));
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
            if (!detail::allocate_new_table(table->cxt, static_cast<mdtable_id_t>(table->table_id)))
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

bool add_row_to_list(cursor<mdtid_TypeDef> owner, decltype(columns::TypeDef_FieldList) list,
    column<mdtid_Field, column_type_constant> sort, uint32_t value, mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, sort, value, table, row);
}

bool add_row_to_list(cursor<mdtid_TypeDef> owner, decltype(columns::TypeDef_MethodList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

bool add_row_to_list(cursor<mdtid_TypeDef> owner, decltype(columns::TypeDef_MethodList) list,
    column<mdtid_MethodDef, column_type_constant> sort, uint32_t value, mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, sort, value, table, row);
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

bool add_row_to_list(cursor<mdtid_EventMap> owner, decltype(columns::EventMap_EventList) list,
    column<mdtid_Event, column_type_constant> sort, uint32_t value, mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, sort, value, table, row);
}

bool add_row_to_list(cursor<mdtid_PropertyMap> owner, decltype(columns::PropertyMap_PropertyList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

bool add_row_to_list(cursor<mdtid_PropertyMap> owner, decltype(columns::PropertyMap_PropertyList) list,
    column<mdtid_Property, column_type_constant> sort, uint32_t value, mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, sort, value, table, row);
}

#ifdef DNMD_PORTABLE_PDB
bool add_row_to_list(cursor<mdtid_LocalScope> owner, decltype(columns::LocalScope_VariableList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

bool add_row_to_list(cursor<mdtid_LocalScope> owner, decltype(columns::LocalScope_VariableList) list,
    column<mdtid_LocalVariable, column_type_constant> sort, uint32_t value, mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, sort, value, table, row);
}

bool add_row_to_list(cursor<mdtid_LocalScope> owner, decltype(columns::LocalScope_ConstantList) list,
    mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, unsorted_list{}, 0, table, row);
}

bool add_row_to_list(cursor<mdtid_LocalScope> owner, decltype(columns::LocalScope_ConstantList) list,
    column<mdtid_LocalConstant, column_type_constant> sort, uint32_t value, mdtable_t** table, uint32_t* row)
{
    return add_list_row(owner, list, sort, value, table, row);
}
#endif
}

bool update_table_references_for_shifted_rows(mdcxt_t* context, mdtable_id_t updated_table,
    uint32_t changed_row_start, int64_t shift)
{
    assert(context != nullptr && updated_table != mdtid_Unused);
    // Make sure we aren't shifting into negative row ids or above the max row id.
    assert(changed_row_start + shift > 0 && changed_row_start + shift < 0x00ffffff);
    return update_references_in_tables(context, detail::table_indices{}, updated_table, changed_row_start, shift);
}

// C cursor adaptation is confined to the public compatibility entrypoints.
namespace
{
    template<typename Sequence>
    struct table_dispatch;

    template<size_t... Tables>
    struct table_dispatch<std::index_sequence<Tables...>>
    {
        template<typename F>
        static bool visit(mdtable_t& table, F&& callback)
        {
            auto visit = [&](auto id) {
                constexpr auto table_id = decltype(id)::value;
                if constexpr (std::tuple_size_v<decltype(detail::table_columns<table_id>())> == 0)
                    return false;
                else
                    return table.table_id == table_id && callback(id);
            };
            return (visit(std::integral_constant<mdtable_id_t, static_cast<mdtable_id_t>(Tables)>{}) || ...);
        }
    };

    template<typename F>
    bool visit_table(mdtable_t& table, F&& callback)
    {
        return table_dispatch<detail::table_indices>::visit(table, std::forward<F>(callback));
    }

    mdtable_t* legacy_table(mdcursor_t row) noexcept
    {
        return reinterpret_cast<mdtable_t*>(row._reserved1);
    }

    uint32_t legacy_row(mdcursor_t row) noexcept
    {
        return (dnmd::detail::RidFromToken)(static_cast<uint64_t>(row._reserved2));
    }

    void legacy_result(mdtable_t* table, uint32_t row, mdcursor_t* output) noexcept
    {
        if (row != 0)
            *output = {reinterpret_cast<intptr_t>(table), static_cast<intptr_t>(row)};
    }

    bool insert_from_c(mdcursor_t relative, bool after, mdcursor_t* output)
    {
        auto table = legacy_table(relative);
        if (output == nullptr || table == nullptr)
            return false;
        uint32_t row = 0;
        bool success = visit_table(*table, [&](auto id) {
            auto typed = position<decltype(id)::value>(table->cxt, legacy_row(relative));
            return detail::insert_row_relative(typed, after, &row);
        });
        legacy_result(table, row, output);
        return success;
    }

    bool add_list_from_c(mdtable_t* table, uint32_t owner, col_index_t list,
        bool sorted, col_index_t sort_column, uint32_t sort_value, mdtable_t** added_table, uint32_t* row)
    {
        if (added_table != nullptr)
            *added_table = nullptr;
        if (row != nullptr)
            *row = 0;
        if (added_table == nullptr || row == nullptr || table == nullptr || !table->is_initialized
            || owner == 0 || owner > table->row_count || owner > 0x00ffffff)
            return false;
        return visit_table(*table, [&](auto id) {
            constexpr auto target = decltype(id)::value;
            if constexpr (target == mdtid_TypeDef || target == mdtid_MethodDef || target == mdtid_EventMap
                || target == mdtid_PropertyMap
#ifdef DNMD_PORTABLE_PDB
                || target == mdtid_LocalScope
#endif
                )
            {
                auto parent = position<target>(table->cxt, owner).value();
                auto find = [&](auto col) {
                    using kind = typename decltype(col)::value_type;
                    if constexpr (detail::list_traits<kind>::supported)
                    {
                        if (!detail::is_list_column(col) || col.column_index != list)
                            return false;
                        if (sorted)
                        {
                            constexpr auto child = detail::list_traits<kind>::direct;
                            return add_list_row(parent, col, column<child, column_type_constant>{sort_column},
                                sort_value, added_table, row);
                        }
                        return add_list_row(parent, col, unsorted_list{}, 0, added_table, row);
                    }
                    else
                        return false;
                };
                return std::apply([&](auto... columns) {
                    return (find(columns) || ...);
                }, detail::table_columns<target>());
            }
            else
                return false;
        });
    }
}

bool md_append_row(mdhandle_t handle, mdtable_id_t table, mdcursor_t* output)
{
    auto context = dnmd::detail::extract_mdcxt(handle);
    if (output == nullptr || context == nullptr || table < mdtid_First || table >= mdtid_End)
        return false;
    uint32_t row = 0;
    bool success = visit_table(context->tables[table], [&](auto id) {
        return dnmd::detail::append_row_to_table<decltype(id)::value>(context, &row);
    });
    legacy_result(&context->tables[table], row, output);
    return success;
}

bool md_insert_row_before(mdcursor_t relative, mdcursor_t* output)
{
    // Inserting before a cursor gives the new row the same target RID as that cursor.
    return insert_from_c(relative, false, output);
}

bool md_insert_row_after(mdcursor_t relative, mdcursor_t* output)
{
    return insert_from_c(relative, true, output);
}

bool md_add_new_row_to_list(mdcursor_t owner, col_index_t list, mdcursor_t* output)
{
    if (output == nullptr)
        return false;
    uint32_t row = 0;
    mdtable_t* table = nullptr;
    bool success = add_list_from_c(legacy_table(owner), legacy_row(owner),
        list, false, list, 0, &table, &row);
    legacy_result(table, row, output);
    return success;
}

bool md_add_new_row_to_sorted_list(mdcursor_t owner, col_index_t list,
    col_index_t sort_column, uint32_t sort_value, mdcursor_t* output)
{
    if (output == nullptr)
        return false;
    uint32_t row = 0;
    mdtable_t* table = nullptr;
    bool success = add_list_from_c(legacy_table(owner), legacy_row(owner),
        list, true, sort_column, sort_value, &table, &row);
    legacy_result(table, row, output);
    return success;
}

void md_commit_row_add(mdcursor_t row)
{
    auto table = legacy_table(row);
    // Zero-initialized C cursors are a no-op, including the empty legacy md_added_row_t guard.
    if (table == nullptr)
        return;
    bool completed = visit_table(*table, [&](auto id) {
        auto value = position<decltype(id)::value>(table->cxt, legacy_row(row));
        if (!value)
            return false;
        commit(value.value());
        return true;
    });
    assert(completed);
    (void)completed;
}
