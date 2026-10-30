#include "internal.hpp"
#include <algorithm>
#include <array>
#include <new>
#include <vector>

namespace
{
    using namespace dnmd;
    using namespace dnmd::columns;

    enum class delta_operation : uint32_t
    {
        update,
        method,
        field,
        parameter,
        property,
        event
    };

    struct map_group
    {
        uint32_t first = 0;
        uint32_t count = UINT32_MAX;
    };
    using token_map = std::array<map_group, mdtid_End>;

    mdToken remove_record_bit(mdToken token) noexcept { return token & 0x7fffffff; }

    bool initialize_token_map(mdhandle_t delta, token_map& groups)
    {
        auto const& storage = detail::context(delta).tables[mdtid_ENCMap];
        if (storage.row_count != 0 && !detail::table_storage_valid(storage, 1))
            return false;
        auto position = md_create_cursor<mdtid_ENCMap>(delta);
        uint32_t previous = UINT32_MAX;
        for (uint32_t row = 1; position; ++row, position.move(1))
        {
            mdToken token;
            if (!position.value().get_column_value(ENCMap_Token, &token))
                return false;
            auto table = ExtractTokenType(remove_record_bit(token));
            if (table >= groups.size())
                return false;
            auto& group = groups[table];
            if (group.count == UINT32_MAX)
                group = {row, 1};
            else
            {
                if (previous != table || group.count == UINT32_MAX - 1)
                    return false;
                ++group.count;
            }
            previous = table;
        }
        return true;
    }

    any_cursor resolve_token(token_map const& groups, mdToken token, mdhandle_t delta)
    {
        auto table = ExtractTokenType(token);
        auto rid = (detail::RidFromToken)(token);
        if (table >= groups.size() || rid == 0)
            return any_cursor::nil(delta);
        auto const& group = groups[table];
        if (group.count == UINT32_MAX)
            return any_cursor{delta, token};

        auto position = raw_cursor<mdtid_ENCMap>::at(delta, group.first);
        for (uint32_t row = 0; row < group.count; ++row, position.move(1))
        {
            mdToken mapped;
            if (!position || !position.value().get_column_value(ENCMap_Token, &mapped)
                || ExtractTokenType(remove_record_bit(mapped)) != table)
                return any_cursor::nil(delta);
            if ((detail::RidFromToken)(mapped) == rid)
                return any_cursor{delta, CreateTokenType(table) | (row + 1)};
        }
        return any_cursor::nil(delta);
    }

    template<mdtable_id_t Table>
    bool copy_row(cursor<Table> destination, cursor<Table> source)
    {
        constexpr auto count = std::tuple_size_v<decltype(detail::table_columns<Table>())>;
        if (!detail::table_storage_valid(*detail::CursorTable(source), count)
            || !detail::table_storage_valid(*detail::CursorTable(destination), count))
            return false;
        row_reader reader{source};
        row_writer writer{destination};
        auto copy = [&](auto column) {
            using kind = typename decltype(column)::value_type;
            if (detail::is_list_column(column))
                return true;
            if constexpr (std::is_same_v<kind, column_type_constant>)
            {
                uint32_t value;
                return reader.read(column, &value) && writer.write(column, value);
            }
            else if constexpr (std::is_same_v<kind, column_type_string>
                || std::is_same_v<kind, column_type_blob>
                || std::is_same_v<kind, column_type_guid>
                || std::is_same_v<kind, column_type_userstring>)
            {
                // Delta heaps are merged before their already-relative indices are copied.
                return detail::row_storage_access::write_heap_index(
                    writer, column, detail::row_storage_access::read(reader, column));
            }
            else
            {
                mdToken value;
                return reader.read(column, value) && writer.write(column, value);
            }
        };
        return detail::for_each_column<Table>([&](auto... columns) {
            return (copy(columns) && ...);
        });
    }

    bool sort_parameters(cursor<mdtid_MethodDef> method)
    {
        auto context = detail::CursorTable(method)->cxt;
        auto position = indirect_cursor<mdtid_Param, mdtid_ParamPtr>::nil(context);
        uint32_t count;
        if (!row_reader{method}.read(MethodDef_ParamList, position, &count))
            return false;
        if (count < 2)
            return true;

        struct parameter
        {
            raw_cursor<mdtid_Param> row;
            uint32_t sequence;
        };
        std::vector<parameter> parameters;
        if (count > parameters.max_size())
            return false;
        parameters.reserve(count);
        for (uint32_t row = 0; row < count; ++row, position.move(1))
        {
            uint32_t sequence;
            if (!position || !position.value().get_column_value(Param_Sequence, &sequence))
                return false;
            parameters.push_back({position.value(), sequence});
        }
        auto compare = [](parameter const& left, parameter const& right) {
            return left.sequence < right.sequence;
        };
        if (std::is_sorted(parameters.begin(), parameters.end(), compare))
            return true;
        std::stable_sort(parameters.begin(), parameters.end(), compare);

        if (!context->tables[mdtid_Param].has_indirect_table)
        {
            if (!detail::create_and_fill_indirect_table<mdtid_Param, mdtid_ParamPtr>(context))
                return false;
        }
        if (!row_reader{method}.read(MethodDef_ParamList, position, &count))
            return false;
        auto pointer = raw_cursor<mdtid_ParamPtr>::at(context, detail::CursorRow(position));
        for (auto const& parameter : parameters)
        {
            if (!pointer || !row_writer{pointer.value()}.write(detail::pointer_column<mdtid_ParamPtr>(), parameter.row))
                return false;
            pointer.move(1);
        }
        return true;
    }

    template<mdtable_id_t Table, typename ColumnType>
    bool add_child(mdhandle_t context, mdToken token, column<Table, ColumnType> list)
    {
        auto parent = raw_cursor<Table>{context, token};
        if (!parent)
            return false;
        auto added = md_add_new_row_to_list(parent.value(), list);
        return static_cast<bool>(added);
    }

    template<mdtable_id_t Table>
    bool apply_record(mdcxt_t* context, mdToken token, cursor<Table> source, delta_operation previous)
    {
        mdhandle_t handle = context;
        auto destination = raw_cursor<Table>{handle, token};
        if (!destination)
        {
            auto rid = (detail::RidFromToken)(token);
            if (context->tables[Table].row_count != rid - 1)
                return false;
            auto added = added_row<Table>::append(context);
            if (!added)
                return false;
            destination = added.position();
            if (!copy_row(added.value(), source))
                return false;
        }
        else if (!copy_row(destination.value(), source))
            return false;

        if (previous == delta_operation::parameter)
        {
            if constexpr (Table == mdtid_Param)
            {
                auto parent = raw_cursor<mdtid_MethodDef>::nil(context);
                return md_find_cursor_of_range_element(destination.value(), parent)
                    && parent && sort_parameters(parent.value());
            }
            else
                return false;
        }
        return true;
    }

    bool process_log(mdcxt_t* context, mdcxt_t* delta)
    {
        token_map groups;
        if (!initialize_token_map(delta, groups))
            return false;
        if (delta->tables[mdtid_ENCLog].row_count != 0
            && !detail::table_storage_valid(delta->tables[mdtid_ENCLog], 2))
            return false;
        auto position = md_create_cursor<mdtid_ENCLog>(delta);
        auto previous = delta_operation::update;
        for (; position; position.move(1))
        {
            mdToken token;
            uint32_t operation;
            row_reader reader{position.value()};
            if (!reader.read(ENCLog_Token, &token) || !reader.read(ENCLog_Op, &operation))
                return false;
            token = remove_record_bit(token);
            bool success;
            switch (static_cast<delta_operation>(operation))
            {
            case delta_operation::method:
                success = add_child(context, token, TypeDef_MethodList);
                break;
            case delta_operation::field:
                success = add_child(context, token, TypeDef_FieldList);
                break;
            case delta_operation::parameter:
                success = add_child(context, token, MethodDef_ParamList);
                break;
            case delta_operation::property:
                success = add_child(context, token, PropertyMap_PropertyList);
                break;
            case delta_operation::event:
                success = add_child(context, token, EventMap_EventList);
                break;
            case delta_operation::update:
            {
                auto source = resolve_token(groups, token, delta);
                success = false;
                if (!source.visit([&](auto row) {
                    success = apply_record(context, token, row, previous);
                }))
                    return false;
                break;
            }
            default:
                return false;
            }
            if (!success)
                return false;
            previous = static_cast<delta_operation>(operation);
        }
        return true;
    }
}

bool merge_in_delta(mdcxt_t* context, mdcxt_t* delta)
{
    using namespace dnmd;
    using namespace dnmd::columns;
    if (context->major_ver != delta->major_ver || context->minor_ver != delta->minor_ver)
        return false;
    auto base_module = md_create_cursor<mdtid_Module>(context);
    auto delta_module = md_create_cursor<mdtid_Module>(delta);
    if (!base_module || !delta_module)
        return false;
    mdguid_t base_mvid;
    mdguid_t delta_mvid;
    mdguid_t enc_id;
    mdguid_t enc_base;
    row_reader base_reader{base_module.value()};
    row_reader delta_reader{delta_module.value()};
    if (!base_reader.read(Module_Mvid, &base_mvid) || !delta_reader.read(Module_Mvid, &delta_mvid)
        || std::memcmp(&base_mvid, &delta_mvid, sizeof(base_mvid)) != 0
        || !base_reader.read(Module_EncId, &enc_id) || !delta_reader.read(Module_EncBaseId, &enc_base)
        || std::memcmp(&enc_id, &enc_base, sizeof(enc_id)) != 0)
        return false;

    try
    {
        for (auto heap : {mdhid_String, mdhid_Guid, mdhid_Blob, mdhid_UserString})
        {
            if (!append_heap(context, delta, heap))
                return false;
        }
        if (!process_log(context, delta))
            return false;
        return detail::row_storage_access::write_heap_index(row_writer{base_module.value()},
            Module_EncId, detail::row_storage_access::read(delta_reader, Module_EncId));
    }
    catch (std::bad_alloc const&)
    {
        return false;
    }
}
