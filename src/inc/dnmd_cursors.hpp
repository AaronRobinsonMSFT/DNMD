#ifndef _SRC_INC_DNMD_CURSORS_HPP_
#define _SRC_INC_DNMD_CURSORS_HPP_

#include "dnmd_columns.hpp"
#include <limits>
#include <utility>

namespace dnmd
{
namespace detail
{
extern "C" mdcxt_t* extract_mdcxt(mdhandle_t handle);

inline mdcxt_t& context(mdhandle_t handle) noexcept
{
    auto value = extract_mdcxt(handle);
    assert(value != nullptr);
    return *value;
}

struct cursor_access
{
    template<typename P> static mdtable_t* table(P const& p) noexcept { return p._table; }
    template<typename P> static uint32_t row(P const& p) noexcept { return p._row; }
};

template<typename P> inline mdtable_t* CursorTable(P const& p) noexcept { return cursor_access::table(p); }
template<typename P> inline uint32_t CursorRow(P const& p) noexcept { return cursor_access::row(p); }

inline bool is_valid_token(mdcxt_t const& c, mdToken token) noexcept
{
    uint32_t table = ExtractTokenType(token);
    uint32_t row = (RidFromToken)(token);
    return table < static_cast<uint32_t>(mdtid_End) && row != 0 && row <= c.tables[table].row_count;
}

inline bool position_token(mdtable_t const& table, uint32_t row, mdToken& token) noexcept
{
    if (row > 0x00ffffff)
        return false;
    token = CreateTokenType(table.table_id) | row;
    return true;
}

template<typename T> struct is_position : std::false_type {};
template<mdtable_id_t T> struct is_position<cursor<T>> : std::true_type {};
template<mdtable_id_t... T> struct is_position<raw_cursor<T...>> : std::true_type {};
template<mdtable_id_t D, mdtable_id_t P> struct is_position<indirect_cursor<D, P>> : std::true_type {};

template<typename P, mdtable_id_t... Targets>
struct position_targets_subset : std::false_type {};
template<mdtable_id_t T, mdtable_id_t... Targets>
struct position_targets_subset<cursor<T>, Targets...>
    : std::bool_constant<contains_table_id_v<T, Targets...>> {};
template<mdtable_id_t... Sources, mdtable_id_t... Targets>
struct position_targets_subset<raw_cursor<Sources...>, Targets...>
    : std::bool_constant<((Sources == mdtid_Unused || contains_table_id_v<Sources, Targets...>) && ...)> {};
template<mdtable_id_t Direct, mdtable_id_t Ptr, mdtable_id_t... Targets>
struct position_targets_subset<indirect_cursor<Direct, Ptr>, Targets...>
    : std::bool_constant<contains_table_id_v<Direct, Targets...> && contains_table_id_v<Ptr, Targets...>> {};

template<mdtable_id_t... T> struct first_concrete_table;
template<> struct first_concrete_table<> : std::integral_constant<mdtable_id_t, mdtid_Unused> {};
template<mdtable_id_t T, mdtable_id_t... Rest>
struct first_concrete_table<T, Rest...> : std::integral_constant<mdtable_id_t,
    T == mdtid_Unused ? first_concrete_table<Rest...>::value : T> {};
}

template<mdtable_id_t TableId>
struct cursor final
{
private:
    template<mdtable_id_t...> friend struct raw_cursor;
    template<mdtable_id_t, mdtable_id_t> friend struct indirect_cursor;
    template<mdtable_id_t> friend struct row_reader;
    template<mdtable_id_t> friend struct row_writer;
    friend struct detail::cursor_access;
    mdtable_t* _table;
    uint32_t _row;

    cursor(mdtable_t& table, uint32_t row) noexcept : _table{&table}, _row{row}
    {
        assert(table.table_id == TableId && table.is_initialized);
        assert(row != 0 && row <= table.row_count);
    }

public:
    cursor() = delete;
    mdhandle_t get_handle() const noexcept { return _table->cxt; }
    bool get_token(mdToken& token) const noexcept { return detail::position_token(*_table, _row, token); }
        bool get_parent_token(mdToken& tk) const noexcept;

        template<typename C, std::enable_if_t<detail::searchable_column<C>::value
            && !std::is_same_v<C, column_type_constant>, int> = 0>
        int32_t get_column_values(column<TableId, C> column, uint32_t count, mdToken* tokens) const noexcept
        {
            if (count == 0)
                return 0;
            assert(tokens != nullptr);
            row_reader<TableId> reader{*this};
            uint32_t row = _row;
            uint32_t available = reader._table->row_count - row + 1;
            if (count > available)
                count = available;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (!reader.read(column, tokens[i]))
                    return -1;
                if (i + 1 < count)
                    reader.advance();
            }
            return static_cast<int32_t>(count);
        }

        template<mdtable_id_t... TargetTableIds>
        bool get_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, raw_cursor<TargetTableIds...>& target_cursor) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, target_cursor);
        }

        template<mdtable_id_t... TargetTableIds>
        bool get_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, mdToken& tk) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, tk);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool get_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, indirect_cursor<DirectTableId, IndirectTableId>& target_cursor) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, target_cursor);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool get_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, mdToken& tk) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, tk);
        }

        bool get_column_value(column<TableId, column_type_constant> column, uint32_t* value) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, value);
        }

        bool get_column_value(column<TableId, column_type_blob> column, uint8_t const** blob, uint32_t* blob_len) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, blob, blob_len);
        }

        bool get_column_value(column<TableId, column_type_guid> column, mdguid_t* guid) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, guid);
        }

        bool get_column_value(column<TableId, column_type_userstring> column, mduserstring_t* userstring) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, userstring);
        }

        bool get_column_value(column<TableId, column_type_string> column, char const** str) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, str);
        }

        bool set_column_value(column<TableId, column_type_constant> column, uint32_t value) const noexcept;

        bool set_column_value(column<TableId, column_type_string> column, char const* value) const noexcept;

        bool set_column_value(column<TableId, column_type_blob> column, uint8_t const* value, uint32_t length) const noexcept;

        bool set_column_value(column<TableId, column_type_guid> column, mdguid_t value) const noexcept;

        bool set_column_value(column<TableId, column_type_userstring> column, char16_t const* value) const noexcept;

        template<mdtable_id_t... TargetTableIds>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, mdToken value) const noexcept;

        template<mdtable_id_t... TargetTableIds, mdtable_id_t ValueTableId>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, cursor<ValueTableId> value) const noexcept;

        template<mdtable_id_t... TargetTableIds, mdtable_id_t... ValueTableIds>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, raw_cursor<ValueTableIds...> const& value) const noexcept;

        template<mdtable_id_t... TargetTableIds, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, raw_cursor<TargetTableIds...>> column, indirect_cursor<DirectTableId, IndirectTableId> const& value) const noexcept;

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, mdToken value) const noexcept;

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t ValueTableId>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, cursor<ValueTableId> value) const noexcept;

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t... ValueTableIds>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, raw_cursor<ValueTableIds...> const& value) const noexcept;

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, indirect_cursor<DirectTableId, IndirectTableId>> column, indirect_cursor<DirectTableId, IndirectTableId> const& value) const noexcept;

        template<mdtable_id_t TargetTableId, mdtable_id_t IndirectTableId>
        bool get_column_value_as_range(column<TableId, indirect_cursor<TargetTableId, IndirectTableId>> column, indirect_cursor<TargetTableId, IndirectTableId>& range_cursor, uint32_t* count) const noexcept
        {
            return row_reader<TableId>{*this}.read(column, range_cursor, count);
        }
};

template<mdtable_id_t... TableIds>
struct raw_cursor final
{
    static_assert(sizeof...(TableIds) != 0);
private:
    template<mdtable_id_t...> friend struct raw_cursor;
    friend struct detail::cursor_access;
    mdtable_t* _table;
    uint32_t _row;
    static constexpr mdtable_id_t first_table = detail::first_concrete_table<TableIds...>::value;
    static_assert(first_table >= mdtid_First && first_table < mdtid_End);

    raw_cursor(mdtable_t& table, uint32_t row) noexcept : _table{&table}, _row{row}
    {
        assert(((table.table_id == TableIds) || ...));
        assert(uint64_t{row} <= uint64_t{table.row_count} + 1);
    }

public:
    raw_cursor() = delete;
    raw_cursor(mdhandle_t handle, mdToken token) noexcept
        : raw_cursor{detail::context(handle).tables[first_table], 0}
    {
        uint32_t table = ExtractTokenType(token);
        if (((table == static_cast<uint32_t>(TableIds)) || ...)
            && detail::is_valid_token(*_table->cxt, token))
        {
            _table = &_table->cxt->tables[table];
            _row = (detail::RidFromToken)(token);
        }
    }
    template<typename T> raw_cursor(T*, mdToken) = delete;

    template<typename P, std::enable_if_t<detail::position_targets_subset<P, TableIds...>::value
        && !std::is_same_v<P, raw_cursor>, int> = 0>
    raw_cursor(P const& other) noexcept
        : _table{detail::CursorTable(other)}, _row{detail::CursorRow(other)}
    {
    }

    template<typename P, std::enable_if_t<detail::is_position<P>::value
        && !detail::position_targets_subset<P, TableIds...>::value
        && !std::is_same_v<P, raw_cursor>, int> = 0>
    explicit raw_cursor(P const& other) noexcept
        : _table{detail::CursorTable(other)}, _row{detail::CursorRow(other)}
    {
        if (!((_table->table_id == TableIds) || ...))
        {
            _table = &_table->cxt->tables[first_table];
            _row = 0;
        }
        assert(((_table->table_id == TableIds) || ...));
    }

    template<mdtable_id_t T>
    explicit raw_cursor(added_row<T> const& other) noexcept : raw_cursor{other.position()} {}

    static raw_cursor nil(mdhandle_t handle) noexcept
    {
        return raw_cursor{detail::context(handle).tables[first_table], 0};
    }

    static raw_cursor at(mdhandle_t handle, uint32_t row, mdtable_id_t table = first_table) noexcept
    {
        auto& c = detail::context(handle);
        if (table < mdtid_First || table >= mdtid_End
            || !((table == TableIds) || ...)
            || uint64_t{row} > uint64_t{c.tables[table].row_count} + 1)
            return nil(handle);
        return raw_cursor{c.tables[table], row};
    }

    static raw_cursor end(mdhandle_t handle) noexcept
    {
        auto& table = detail::context(handle).tables[first_table];
        assert(table.row_count != UINT32_MAX);
        return raw_cursor{table, table.row_count + 1};
    }

    mdhandle_t get_handle() const noexcept { return _table->cxt; }
    explicit operator bool() const noexcept { return _row != 0 && _row <= _table->row_count; }
    bool get_token(mdToken& token) const noexcept { return detail::position_token(*_table, _row, token); }

    template<mdtable_id_t T>
    bool is() const noexcept { return _table->table_id == T; }

    bool is_nil() const noexcept { return _row == 0; }

    bool move(int32_t delta) noexcept
    {
        int64_t row = int64_t{_row} + delta;
        if (row <= 0 || uint64_t(row) > uint64_t{_table->row_count} + 1 || row > UINT32_MAX)
            return false;
        _row = static_cast<uint32_t>(row);
        return true;
    }

    template<size_t Count = sizeof...(TableIds), std::enable_if_t<Count == 1 && Count == sizeof...(TableIds), int> = 0>
    auto as() const noexcept
    {
        assert(static_cast<bool>(*this));
        return (cursor<TableIds>{*_table, _row}, ...);
    }

    template<mdtable_id_t T>
    raw_cursor<T> get_as() const noexcept
    {
        static_assert(T != mdtid_Unused && ((T == TableIds) || ...));
        return is<T>() ? raw_cursor<T>::at(get_handle(), _row)
            : raw_cursor<T>::nil(get_handle());
    }

    template<typename F>
    bool visit(F&& callable) const
    {
        if (!*this)
            return false;
        return (visit_impl<TableIds>(callable) || ...);
    }

private:
    template<mdtable_id_t T, typename F>
    bool visit_impl(F&& callable) const
    {
        if constexpr (T == mdtid_Unused)
            return false;
        else
        {
            if (!is<T>())
                return false;
            std::forward<F>(callable)(cursor<T>{*_table, _row});
            return true;
        }
    }
};

template<mdtable_id_t Direct, mdtable_id_t Ptr>
struct indirect_cursor final
{
private:
    friend struct detail::cursor_access;
    mdtable_t* _table;
    uint32_t _row;
    raw_cursor<Direct> _target;
    bool _resolved;
    bool _has_row;

    void resolve() noexcept
    {
        _target = raw_cursor<Direct>::nil(get_handle());
        _resolved = true;
        if (_table->table_id == Direct)
            _target = raw_cursor<Direct>::at(get_handle(), _row);
        else if (_row == 0)
        {
            _has_row = false;
            return;
        }
        else if (_row == _table->row_count + 1)
            _target = raw_cursor<Direct>::end(get_handle());
        else
        {
            auto source = raw_cursor<Ptr>::at(get_handle(), _row).as();
            _resolved = row_reader<Ptr>{source}.read(detail::pointer_column<Ptr>(), _target);
        }
        _has_row = _resolved && static_cast<bool>(_target);
    }

public:
    indirect_cursor() = delete;
    template<typename P, std::enable_if_t<detail::is_position<P>::value, int> = 0>
    explicit indirect_cursor(P const& p) noexcept
        : indirect_cursor{raw_cursor<Direct, Ptr>{p}} {}

    explicit indirect_cursor(raw_cursor<Direct, Ptr> const& source) noexcept
        : _table{detail::CursorTable(source)}, _row{detail::CursorRow(source)},
          _target{raw_cursor<Direct>::nil(source.get_handle())}, _resolved{false}, _has_row{false}
    {
        resolve();
    }

    indirect_cursor(mdhandle_t handle, mdToken token) noexcept
        : indirect_cursor{raw_cursor<Direct, Ptr>{handle, token}} {}
    template<typename T> indirect_cursor(T*, mdToken) = delete;

    static indirect_cursor at(mdhandle_t handle, uint32_t row, mdtable_id_t table) noexcept
    {
        return indirect_cursor{raw_cursor<Direct, Ptr>::at(handle, row, table)};
    }

    static indirect_cursor nil(mdhandle_t handle) noexcept
    {
        auto& c = detail::context(handle);
        auto table = c.tables[Direct].has_indirect_table ? Ptr : Direct;
        return at(handle, 0, table);
    }

    static indirect_cursor end(mdhandle_t handle) noexcept
    {
        auto& c = detail::context(handle);
        auto table = c.tables[Direct].has_indirect_table ? Ptr : Direct;
        return at(handle, c.tables[table].row_count + 1, table);
    }

    mdhandle_t get_handle() const noexcept { return _table->cxt; }
    explicit operator bool() const noexcept { return _has_row; }
    cursor<Direct> as() const noexcept { assert(_has_row); return _target.as(); }
    bool get_token(mdToken& token) const noexcept { return detail::position_token(*_table, _row, token); }
    bool get_target_token(mdToken& token) const noexcept { return _resolved && _target.get_token(token); }
    bool move(int32_t delta) noexcept
    {
        auto source = raw_cursor<Direct, Ptr>::at(get_handle(), _row, static_cast<mdtable_id_t>(_table->table_id));
        if (!source.move(delta))
            return false;
        _row = detail::CursorRow(source);
        resolve();
        return true;
    }
};

using any_cursor = raw_cursor<
    mdtid_Module, mdtid_TypeRef, mdtid_TypeDef, mdtid_FieldPtr, mdtid_Field,
    mdtid_MethodPtr, mdtid_MethodDef, mdtid_ParamPtr, mdtid_Param, mdtid_InterfaceImpl,
    mdtid_MemberRef, mdtid_Constant, mdtid_CustomAttribute, mdtid_FieldMarshal,
    mdtid_DeclSecurity, mdtid_ClassLayout, mdtid_FieldLayout, mdtid_StandAloneSig,
    mdtid_EventMap, mdtid_EventPtr, mdtid_Event, mdtid_PropertyMap, mdtid_PropertyPtr,
    mdtid_Property, mdtid_MethodSemantics, mdtid_MethodImpl, mdtid_ModuleRef,
    mdtid_TypeSpec, mdtid_ImplMap, mdtid_FieldRva, mdtid_ENCLog, mdtid_ENCMap,
    mdtid_Assembly, mdtid_AssemblyProcessor, mdtid_AssemblyOS, mdtid_AssemblyRef,
    mdtid_AssemblyRefProcessor, mdtid_AssemblyRefOS, mdtid_File, mdtid_ExportedType,
    mdtid_ManifestResource, mdtid_NestedClass, mdtid_GenericParam, mdtid_MethodSpec,
    mdtid_GenericParamConstraint
#ifdef DNMD_PORTABLE_PDB
    , mdtid_Document, mdtid_MethodDebugInformation, mdtid_LocalScope, mdtid_LocalVariable,
    mdtid_LocalConstant, mdtid_ImportScope, mdtid_StateMachineMethod, mdtid_CustomDebugInformation
#endif
>;

template<typename T> struct cursor_tableid;
template<mdtable_id_t T> struct cursor_tableid<cursor<T>> { static constexpr mdtable_id_t value = T; };
template<typename T> inline constexpr mdtable_id_t cursor_tableid_v = cursor_tableid<T>::value;

template<typename P>
inline auto md_cursor_move(P& p, int32_t delta) noexcept -> decltype(p.move(delta)) { return p.move(delta); }
template<typename P>
inline auto md_cursor_next(P& p) noexcept -> decltype(p.move(1)) { return p.move(1); }

template<mdtable_id_t T>
inline raw_cursor<T> md_create_cursor(mdhandle_t handle, uint32_t* count) noexcept
{
    if (count == nullptr)
        return raw_cursor<T>::nil(handle);
    auto result = raw_cursor<T>{handle, CreateTokenType(T) | 1};
    if (result)
        *count = detail::CursorTable(result)->row_count;
    return result;
}
template<mdtable_id_t T>
inline raw_cursor<T> md_create_cursor(mdhandle_t handle) noexcept { return raw_cursor<T>{handle, CreateTokenType(T) | 1}; }
template<mdtable_id_t... T>
inline raw_cursor<T...> md_token_to_cursor(mdhandle_t handle, mdToken token) noexcept { return raw_cursor<T...>{handle, token}; }
inline bool md_is_valid_token(mdhandle_t handle, mdToken token) noexcept
{
    auto c = detail::extract_mdcxt(handle);
    return c != nullptr && detail::is_valid_token(*c, token);
}
} // namespace dnmd

#include "dnmd_read.hpp"
#endif // _SRC_INC_DNMD_CURSORS_HPP_
