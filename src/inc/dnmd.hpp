#ifndef _SRC_INC_DNMD_HPP_
#define _SRC_INC_DNMD_HPP_

#include "dnmd.h"
#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>

namespace dnmd
{

struct handle_deleter_t final
{
    using pointer = mdhandle_t;
    void operator()(mdhandle_t handle)
    {
        ::md_destroy_handle(handle);
    }
};

// C++ lifetime wrapper for mdhandle_t type
using handle_ptr = std::unique_ptr<mdhandle_t, handle_deleter_t>;

struct added_row_t final
{
private:
    mdcursor_t new_row{};
public:
    added_row_t() = default;
    explicit added_row_t(mdcursor_t row) : new_row{ row } {}
    added_row_t(added_row_t const& other) = delete;
    added_row_t(added_row_t&& other)
    {
        *this = std::move(other);
    }

    added_row_t& operator=(added_row_t const& other) = delete;
    added_row_t& operator=(added_row_t&& other)
    {
        new_row = other.new_row;
        other.new_row = {}; // Clear the other's row so we don't double-commit.
        return *this;
    }

    ~added_row_t()
    {
        ::md_commit_row_add(new_row);
    }

    operator mdcursor_t()
    {
        return new_row;
    }

    mdcursor_t* operator&()
    {
        return &new_row;
    }
};

struct column_type_blob {};
struct column_type_string {};
struct column_type_guid {};
struct column_type_userstring {};
struct column_type_constant {};

template<mdtable_id_t TableId>
struct cursor;

template<mdtable_id_t... TableIds>
struct cursor_raw;

template<mdtable_id_t TableId, typename ColumnType>
struct column final
{
    col_index_t column_index;
    public:
        explicit constexpr column(col_index_t col)
            : column_index{ col }
        {
        }
};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct cursor_indirect;

namespace detail
{
template<mdtable_id_t TableId, mdtable_id_t... TableIds>
inline constexpr bool contains_table_id_v = ((TableId == TableIds) || ...);
}

template<mdtable_id_t TableId>
struct cursor final
{
    private:
        template<mdtable_id_t...>
        friend struct cursor_raw;
        template<mdtable_id_t, mdtable_id_t>
        friend struct cursor_indirect;

        mdcursor_t _cursor;

        explicit cursor(mdcursor_t c) noexcept : _cursor{ c }
        {
#ifndef NDEBUG
            mdToken tk;
            assert(::md_cursor_is_dereferenceable(_cursor));
            assert(::md_cursor_to_token(_cursor, &tk));
            assert(ExtractTokenType(tk) == TableId);
#endif
        }

    public:
        cursor() = delete;

        operator mdcursor_t() const noexcept
        {
            return _cursor;
        }

        bool get_token(mdToken& tk) const noexcept
        {
            return ::md_cursor_to_token(_cursor, &tk);
        }

        bool get_parent_token(mdToken& tk) const noexcept
        {
            static_assert(TableId == mdtid_Field || TableId == mdtid_MethodDef
                || TableId == mdtid_Param || TableId == mdtid_Event
#ifdef DNMD_PORTABLE_PDB
                || TableId == mdtid_LocalVariable || TableId == mdtid_LocalConstant
#endif
                || TableId == mdtid_Property,
                "The row must belong to a list.");
            return ::md_find_token_of_range_element(_cursor, &tk);
        }

        template<mdtable_id_t... TargetTableIds>
        int32_t get_column_values(column<TableId, cursor_raw<TargetTableIds...>> column, uint32_t count, mdToken* tokens) const noexcept
        {
            return ::md_get_many_rows_column_value_as_token(_cursor, column.column_index, count, tokens);
        }

        template<mdtable_id_t... TargetTableIds>
        bool get_column_value(column<TableId, cursor_raw<TargetTableIds...>> column, cursor_raw<TargetTableIds...>& target_cursor) const noexcept
        {
            mdcursor_t c;
            if (!::md_get_column_value_as_cursor(_cursor, column.column_index, &c))
                return false;
            target_cursor = cursor_raw<TargetTableIds...>{c};
            return true;
        }

        template<mdtable_id_t... TargetTableIds>
        bool get_column_value(column<TableId, cursor_raw<TargetTableIds...>> column, mdToken& tk) const noexcept
        {
            return ::md_get_column_value_as_token(_cursor, column.column_index, &tk);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool get_column_value(column<TableId, cursor_indirect<DirectTableId, IndirectTableId>> column, cursor_indirect<DirectTableId, IndirectTableId>& target_cursor) const noexcept
        {
            mdcursor_t c;
            if (!::md_get_column_value_as_cursor(_cursor, column.column_index, &c))
                return false;
            target_cursor = cursor_indirect<DirectTableId, IndirectTableId>{c};
            return true;
        }

        bool get_column_value(column<TableId, column_type_constant> column, uint32_t* value) const noexcept
        {
            return ::md_get_column_value_as_constant(_cursor, column.column_index, value);
        }

        bool get_column_value(column<TableId, column_type_blob> column, uint8_t const** blob, uint32_t* blob_len) const noexcept
        {
            return md_get_column_value_as_blob(_cursor, column.column_index, blob, blob_len);
        }

        bool get_column_value(column<TableId, column_type_guid> column, mdguid_t* guid) const noexcept
        {
            return md_get_column_value_as_guid(_cursor, column.column_index, guid);
        }

        bool get_column_value(column<TableId, column_type_userstring> column, mduserstring_t* userstring) const noexcept
        {
            return md_get_column_value_as_userstring(_cursor, column.column_index, userstring);
        }

        bool get_column_value(column<TableId, column_type_string> column, char const** str) const noexcept
        {
            return md_get_column_value_as_utf8(_cursor, column.column_index, str);
        }

        bool set_column_value(column<TableId, column_type_constant> column, uint32_t value) const noexcept
        {
            return ::md_set_column_value_as_constant(_cursor, column.column_index, value);
        }

        bool set_column_value(column<TableId, column_type_string> column, char const* value) const noexcept
        {
            return ::md_set_column_value_as_utf8(_cursor, column.column_index, value);
        }

        bool set_column_value(column<TableId, column_type_blob> column, uint8_t const* value, uint32_t length) const noexcept
        {
            return ::md_set_column_value_as_blob(_cursor, column.column_index, value, length);
        }

        bool set_column_value(column<TableId, column_type_guid> column, mdguid_t value) const noexcept
        {
            return ::md_set_column_value_as_guid(_cursor, column.column_index, value);
        }

        bool set_column_value(column<TableId, column_type_userstring> column, char16_t const* value) const noexcept
        {
            return ::md_set_column_value_as_userstring(_cursor, column.column_index, value);
        }

        template<mdtable_id_t... TargetTableIds>
        bool set_column_value(column<TableId, cursor_raw<TargetTableIds...>> column, mdToken value) const noexcept
        {
            return ::md_set_column_value_as_token(_cursor, column.column_index, value);
        }

        template<mdtable_id_t... TargetTableIds, mdtable_id_t ValueTableId>
        bool set_column_value(column<TableId, cursor_raw<TargetTableIds...>> column, cursor<ValueTableId> value) const noexcept
        {
            static_assert(detail::contains_table_id_v<ValueTableId, TargetTableIds...>,
                "The value must belong to a table referenced by the column.");
            return ::md_set_column_value_as_cursor(_cursor, column.column_index, value);
        }

        template<mdtable_id_t... TargetTableIds, mdtable_id_t... ValueTableIds>
        bool set_column_value(column<TableId, cursor_raw<TargetTableIds...>> column, cursor_raw<ValueTableIds...> const& value) const noexcept
        {
            static_assert((detail::contains_table_id_v<ValueTableIds, TargetTableIds...> && ...),
                "The value must only reference tables allowed by the column.");
            return ::md_set_column_value_as_cursor(_cursor, column.column_index, value);
        }

        template<mdtable_id_t... TargetTableIds, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, cursor_raw<TargetTableIds...>> column, cursor_indirect<DirectTableId, IndirectTableId> const& value) const noexcept
        {
            static_assert(detail::contains_table_id_v<DirectTableId, TargetTableIds...>,
                "The target must belong to a table referenced by the column.");
            mdToken token;
            return value.get_target_token(token) && ::md_set_column_value_as_token(_cursor, column.column_index, token);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, cursor_indirect<DirectTableId, IndirectTableId>> column, mdToken value) const noexcept
        {
            return ::md_set_column_value_as_token(_cursor, column.column_index, value);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t ValueTableId>
        bool set_column_value(column<TableId, cursor_indirect<DirectTableId, IndirectTableId>> column, cursor<ValueTableId> value) const noexcept
        {
            static_assert(ValueTableId == DirectTableId || ValueTableId == IndirectTableId,
                "The value must belong to the direct or indirection table.");
            return ::md_set_column_value_as_cursor(_cursor, column.column_index, value);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t... ValueTableIds>
        bool set_column_value(column<TableId, cursor_indirect<DirectTableId, IndirectTableId>> column, cursor_raw<ValueTableIds...> const& value) const noexcept
        {
            static_assert((detail::contains_table_id_v<ValueTableIds, DirectTableId, IndirectTableId> && ...),
                "The value must only reference the direct or indirection table.");
            return ::md_set_column_value_as_cursor(_cursor, column.column_index, value);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool set_column_value(column<TableId, cursor_indirect<DirectTableId, IndirectTableId>> column, cursor_indirect<DirectTableId, IndirectTableId> const& value) const noexcept
        {
            return ::md_set_column_value_as_cursor(_cursor, column.column_index, value);
        }

        template<mdtable_id_t TargetTableId, mdtable_id_t IndirectTableId>
        bool get_column_value_as_range(column<TableId, cursor_indirect<TargetTableId, IndirectTableId>> column, cursor_indirect<TargetTableId, IndirectTableId>& range_cursor, uint32_t* count) const noexcept
        {
            mdcursor_t c;
            if (!::md_get_column_value_as_range(_cursor, column.column_index, &c, count))
                return false;
            range_cursor = cursor_indirect<TargetTableId, IndirectTableId>{c};
            return true;
        }
};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct cursor_indirect final
{
    private:
        mdcursor_t _cursor{};
        mdcursor_t target{};
        bool target_resolved = false;
        bool has_row = false;

    public:
        cursor_indirect() = default;
        explicit cursor_indirect(mdcursor_t c) noexcept : _cursor{ c }
        {
#ifndef NDEBUG
            mdToken tk;
            assert(::md_cursor_to_token(_cursor, &tk));
            assert(ExtractTokenType(tk) == DirectTableId || ExtractTokenType(tk) == IndirectTableId);
#endif
            target_resolved = ::md_resolve_indirect_cursor(_cursor, &target);
            has_row = target_resolved && ::md_cursor_is_dereferenceable(target);
        }

        operator mdcursor_t() const noexcept
        {
            return _cursor;
        }

        explicit operator bool() const noexcept
        {
            return has_row;
        }

        cursor<DirectTableId> as() const noexcept
        {
            assert(has_row);
            return cursor<DirectTableId>{target};
        }

        bool get_token(mdToken& tk) const noexcept
        {
            return ::md_cursor_to_token(_cursor, &tk);
        }

        bool get_target_token(mdToken& tk) const noexcept
        {
            return target_resolved && ::md_cursor_to_token(target, &tk);
        }
};

template<mdtable_id_t... TableIds>
struct cursor_raw final
{
    static_assert(sizeof...(TableIds) != 0, "A raw cursor must specify at least one table.");

    private:
        mdcursor_t _cursor{};

    public:
        cursor_raw() = default;
        explicit cursor_raw(mdcursor_t c) noexcept
            : _cursor{ c }
        {
#ifndef NDEBUG
            mdToken tk;
            assert(::md_cursor_to_token(_cursor, &tk));
            assert(((ExtractTokenType(tk) == TableIds) || ...));
#endif
        }

        operator mdcursor_t() const noexcept
        {
            return _cursor;
        }

        explicit operator bool() const noexcept
        {
            return ::md_cursor_is_dereferenceable(_cursor);
        }

        bool get_token(mdToken& tk) const noexcept
        {
            return ::md_cursor_to_token(_cursor, &tk);
        }

        template<size_t TableCount = sizeof...(TableIds),
            std::enable_if_t<TableCount == 1 && TableCount == sizeof...(TableIds), int> = 0>
        auto as() const noexcept
        {
            return (cursor<TableIds>{_cursor}, ...);
        }

        template<mdtable_id_t CodedTableId>
        cursor_raw<CodedTableId> get_as() const noexcept
        {
            static_assert(((CodedTableId == TableIds) || ...), "CodedTableId must be one of the tables in the coded index.");
            mdToken tk;
            if (!get_token(tk) || ExtractTokenType(tk) != CodedTableId)
                return {};
            return cursor_raw<CodedTableId>{_cursor};
        }

        template<typename TCallable>
        bool visit(TCallable&& callable) const
        {
            mdToken tk;
            if (!*this || !get_token(tk))
                return false;
            return (visit_impl<TableIds>(static_cast<mdtable_id_t>(ExtractTokenType(tk)), std::forward<TCallable>(callable)) || ...);
        }

    private:
        template<mdtable_id_t CodedTableId, typename TCallable>
        bool visit_impl(mdtable_id_t table_id, TCallable&& callable) const
        {
            if (table_id != CodedTableId)
                return false;
            std::forward<TCallable>(callable)(cursor<CodedTableId>{_cursor});
            return true;
        }
};

template<typename T>
struct cursor_tableid;

template<mdtable_id_t TableId>
struct cursor_tableid<cursor<TableId>>
{
    static constexpr mdtable_id_t value = TableId;
};

template<typename T>
inline constexpr mdtable_id_t cursor_tableid_v = cursor_tableid<T>::value;

template<mdtable_id_t... TableIds>
inline bool md_cursor_move(cursor_raw<TableIds...>& cursor, int32_t delta) noexcept
{
    mdcursor_t c = cursor;
    if (!::md_cursor_move(&c, delta))
        return false;
    cursor = cursor_raw<TableIds...>{c};
    return true;
}

template<mdtable_id_t TableId, mdtable_id_t IndirectTableId>
inline bool md_cursor_move(cursor_indirect<TableId, IndirectTableId>& cursor, int32_t delta) noexcept
{
    mdcursor_t c = cursor;
    if (!::md_cursor_move(&c, delta))
        return false;
    cursor = cursor_indirect<TableId, IndirectTableId>{c};
    return true;
}

template<mdtable_id_t... TableIds>
inline bool md_cursor_next(cursor_raw<TableIds...>& cursor) noexcept
{
    return md_cursor_move(cursor, 1);
}

template<mdtable_id_t TableId, mdtable_id_t IndirectTableId>
inline bool md_cursor_next(cursor_indirect<TableId, IndirectTableId>& cursor) noexcept
{
    return md_cursor_move(cursor, 1);
}

namespace detail
{
template<mdtable_id_t TableId>
inline cursor_raw<TableId> find_row(cursor<TableId> begin, col_index_t column, uint32_t value) noexcept
{
    mdcursor_t c;
    if (!::md_find_row_from_cursor(begin, column, value, &c))
        return {};
    return cursor_raw<TableId>{c};
}
}

template<mdtable_id_t TableId>
inline cursor_raw<TableId> md_find_row_from_cursor(cursor<TableId> begin, column<TableId, column_type_constant> column, uint32_t value) noexcept
{
    return detail::find_row(begin, column.column_index, value);
}

template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds>
inline cursor_raw<TableId> md_find_row_from_cursor(cursor<TableId> begin, column<TableId, cursor_raw<TargetTableIds...>> column, mdToken tk) noexcept
{
    if (!((ExtractTokenType(tk) == TargetTableIds) || ...))
        return {};
    if constexpr (sizeof...(TargetTableIds) == 1)
        tk &= 0x00FFFFFF;
    return detail::find_row(begin, column.column_index, tk);
}

template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds, mdtable_id_t SearchTableId>
inline cursor_raw<TableId> md_find_row_from_cursor(cursor<TableId> begin, column<TableId, cursor_raw<TargetTableIds...>> column, cursor<SearchTableId> value) noexcept
{
    static_assert(((SearchTableId == TargetTableIds) || ...), "The value must belong to a table referenced by the column.");
    mdToken tk;
    if (!::md_cursor_to_token(value, &tk))
        return {};
    return md_find_row_from_cursor(begin, column, tk);
}

template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds>
inline md_range_result_t md_find_range_from_cursor(cursor<TableId> begin,
    column<TableId, cursor_raw<TargetTableIds...>> column, mdToken tk,
    cursor_raw<TableId>& start, uint32_t* count) noexcept
{
    if (!((ExtractTokenType(tk) == TargetTableIds) || ...))
        return MD_RANGE_NOT_FOUND;
    if constexpr (sizeof...(TargetTableIds) == 1)
        tk &= 0x00FFFFFF;
    mdcursor_t c;
    auto result = ::md_find_range_from_cursor(begin, column.column_index, tk, &c, count);
    if (result == MD_RANGE_FOUND)
        start = cursor_raw<TableId>{c};
    return result;
}

template<mdtable_id_t TableId, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline cursor_raw<TableId> md_find_row_from_cursor(cursor<TableId> begin, column<TableId, cursor_indirect<DirectTableId, IndirectTableId>> column, cursor_indirect<DirectTableId, IndirectTableId> const& value) noexcept
{
    mdToken tk;
    if (!value.get_token(tk) || (ExtractTokenType(tk) != DirectTableId && ExtractTokenType(tk) != IndirectTableId))
        return {};
    return detail::find_row(begin, column.column_index, tk & 0x00FFFFFF);
}

template<mdtable_id_t TableId>
inline cursor_raw<TableId> md_create_cursor(mdhandle_t handle, uint32_t* count) noexcept
{
    mdcursor_t c;
    if (!::md_create_cursor(handle, TableId, &c, count))
        return {};
    return cursor_raw<TableId>{c};
}

template<mdtable_id_t TableId>
inline cursor_raw<TableId> md_create_cursor(mdhandle_t handle) noexcept
{
    uint32_t count;
    return md_create_cursor<TableId>(handle, &count);
}

template<mdtable_id_t... TableIds>
inline cursor_raw<TableIds...> md_token_to_cursor(mdhandle_t handle, mdToken tk) noexcept
{
    mdcursor_t c;
    if (!((ExtractTokenType(tk) == TableIds) || ...) || !::md_token_to_cursor(handle, tk, &c))
        return {};
    return cursor_raw<TableIds...>{c};
}

inline bool md_is_valid_token(mdhandle_t handle, mdToken tk) noexcept
{
    mdcursor_t c;
    return ::md_token_to_cursor(handle, tk, &c);
}

namespace columns
{
inline constexpr column<mdtid_Module, column_type_constant> Module_Generation{ mdtModule_Generation };
inline constexpr column<mdtid_Module, column_type_string> Module_Name{ mdtModule_Name };
inline constexpr column<mdtid_Module, column_type_guid> Module_Mvid{ mdtModule_Mvid };
inline constexpr column<mdtid_Module, column_type_guid> Module_EncId{ mdtModule_EncId };
inline constexpr column<mdtid_Module, column_type_guid> Module_EncBaseId{ mdtModule_EncBaseId };

inline constexpr column<mdtid_TypeRef, cursor_raw<mdtid_Module, mdtid_ModuleRef, mdtid_AssemblyRef, mdtid_TypeRef>> TypeRef_ResolutionScope{ mdtTypeRef_ResolutionScope };
inline constexpr column<mdtid_TypeRef, column_type_string> TypeRef_TypeName{ mdtTypeRef_TypeName };
inline constexpr column<mdtid_TypeRef, column_type_string> TypeRef_TypeNamespace{ mdtTypeRef_TypeNamespace };

inline constexpr column<mdtid_TypeDef, column_type_constant> TypeDef_Flags{ mdtTypeDef_Flags };
inline constexpr column<mdtid_TypeDef, column_type_string> TypeDef_TypeName{ mdtTypeDef_TypeName };
inline constexpr column<mdtid_TypeDef, column_type_string> TypeDef_TypeNamespace{ mdtTypeDef_TypeNamespace };
inline constexpr column<mdtid_TypeDef, cursor_raw<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> TypeDef_Extends{ mdtTypeDef_Extends };
inline constexpr column<mdtid_TypeDef, cursor_indirect<mdtid_Field, mdtid_FieldPtr>> TypeDef_FieldList{ mdtTypeDef_FieldList };
inline constexpr column<mdtid_TypeDef, cursor_indirect<mdtid_MethodDef, mdtid_MethodPtr>> TypeDef_MethodList{ mdtTypeDef_MethodList };

inline constexpr column<mdtid_Field, column_type_constant> Field_Flags{ mdtField_Flags };
inline constexpr column<mdtid_Field, column_type_string> Field_Name{ mdtField_Name };
inline constexpr column<mdtid_Field, column_type_blob> Field_Signature{ mdtField_Signature };

inline constexpr column<mdtid_MethodDef, column_type_constant> MethodDef_Rva{ mdtMethodDef_Rva };
inline constexpr column<mdtid_MethodDef, column_type_constant> MethodDef_ImplFlags{ mdtMethodDef_ImplFlags };
inline constexpr column<mdtid_MethodDef, column_type_constant> MethodDef_Flags{ mdtMethodDef_Flags };
inline constexpr column<mdtid_MethodDef, column_type_string> MethodDef_Name{ mdtMethodDef_Name };
inline constexpr column<mdtid_MethodDef, column_type_blob> MethodDef_Signature{ mdtMethodDef_Signature };
inline constexpr column<mdtid_MethodDef, cursor_indirect<mdtid_Param, mdtid_ParamPtr>> MethodDef_ParamList{ mdtMethodDef_ParamList };

inline constexpr column<mdtid_Param, column_type_constant> Param_Flags{ mdtParam_Flags };
inline constexpr column<mdtid_Param, column_type_constant> Param_Sequence{ mdtParam_Sequence };
inline constexpr column<mdtid_Param, column_type_string> Param_Name{ mdtParam_Name };

inline constexpr column<mdtid_InterfaceImpl, cursor_raw<mdtid_TypeDef>> InterfaceImpl_Class{ mdtInterfaceImpl_Class };
inline constexpr column<mdtid_InterfaceImpl, cursor_raw<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> InterfaceImpl_Interface{ mdtInterfaceImpl_Interface };

inline constexpr column<mdtid_MemberRef, cursor_raw<mdtid_TypeDef, mdtid_TypeRef, mdtid_ModuleRef, mdtid_MethodDef, mdtid_TypeSpec>> MemberRef_Class{ mdtMemberRef_Class };
inline constexpr column<mdtid_MemberRef, column_type_string> MemberRef_Name{ mdtMemberRef_Name };
inline constexpr column<mdtid_MemberRef, column_type_blob> MemberRef_Signature{ mdtMemberRef_Signature };

inline constexpr column<mdtid_Constant, column_type_constant> Constant_Type{ mdtConstant_Type };
inline constexpr column<mdtid_Constant, cursor_raw<mdtid_Field, mdtid_Param, mdtid_Property>> Constant_Parent{ mdtConstant_Parent };
inline constexpr column<mdtid_Constant, column_type_blob> Constant_Value{ mdtConstant_Value };

inline constexpr column<mdtid_CustomAttribute, cursor_raw<mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param, mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property, mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec, mdtid_Assembly, mdtid_AssemblyRef, mdtid_File, mdtid_ExportedType, mdtid_ManifestResource, mdtid_GenericParam, mdtid_GenericParamConstraint, mdtid_MethodSpec>> CustomAttribute_Parent{ mdtCustomAttribute_Parent };
inline constexpr column<mdtid_CustomAttribute, cursor_raw<mdtid_MethodDef, mdtid_MemberRef>> CustomAttribute_Type{ mdtCustomAttribute_Type };
inline constexpr column<mdtid_CustomAttribute, column_type_blob> CustomAttribute_Value{ mdtCustomAttribute_Value };

inline constexpr column<mdtid_FieldMarshal, cursor_raw<mdtid_Field, mdtid_Param>> FieldMarshal_Parent{ mdtFieldMarshal_Parent };
inline constexpr column<mdtid_FieldMarshal, column_type_blob> FieldMarshal_NativeType{ mdtFieldMarshal_NativeType };

inline constexpr column<mdtid_DeclSecurity, column_type_constant> DeclSecurity_Action{ mdtDeclSecurity_Action };
inline constexpr column<mdtid_DeclSecurity, cursor_raw<mdtid_TypeDef, mdtid_MethodDef, mdtid_Assembly>> DeclSecurity_Parent{ mdtDeclSecurity_Parent };
inline constexpr column<mdtid_DeclSecurity, column_type_blob> DeclSecurity_PermissionSet{ mdtDeclSecurity_PermissionSet };

inline constexpr column<mdtid_ClassLayout, column_type_constant> ClassLayout_PackingSize{ mdtClassLayout_PackingSize };
inline constexpr column<mdtid_ClassLayout, column_type_constant> ClassLayout_ClassSize{ mdtClassLayout_ClassSize };
inline constexpr column<mdtid_ClassLayout, cursor_raw<mdtid_TypeDef>> ClassLayout_Parent{ mdtClassLayout_Parent };

inline constexpr column<mdtid_FieldLayout, column_type_constant> FieldLayout_Offset{ mdtFieldLayout_Offset };
inline constexpr column<mdtid_FieldLayout, cursor_raw<mdtid_Field>> FieldLayout_Field{ mdtFieldLayout_Field };

inline constexpr column<mdtid_StandAloneSig, column_type_blob> StandAloneSig_Signature{ mdtStandAloneSig_Signature };

inline constexpr column<mdtid_EventMap, cursor_raw<mdtid_TypeDef>> EventMap_Parent{ mdtEventMap_Parent };
inline constexpr column<mdtid_EventMap, cursor_indirect<mdtid_Event, mdtid_EventPtr>> EventMap_EventList{ mdtEventMap_EventList };

inline constexpr column<mdtid_Event, column_type_constant> Event_EventFlags{ mdtEvent_EventFlags };
inline constexpr column<mdtid_Event, column_type_string> Event_Name{ mdtEvent_Name };
inline constexpr column<mdtid_Event, cursor_raw<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> Event_EventType{ mdtEvent_EventType };

inline constexpr column<mdtid_PropertyMap, cursor_raw<mdtid_TypeDef>> PropertyMap_Parent{ mdtPropertyMap_Parent };
inline constexpr column<mdtid_PropertyMap, cursor_indirect<mdtid_Property, mdtid_PropertyPtr>> PropertyMap_PropertyList{ mdtPropertyMap_PropertyList };

inline constexpr column<mdtid_Property, column_type_constant> Property_Flags{ mdtProperty_Flags };
inline constexpr column<mdtid_Property, column_type_string> Property_Name{ mdtProperty_Name };
inline constexpr column<mdtid_Property, column_type_blob> Property_Type{ mdtProperty_Type };

inline constexpr column<mdtid_MethodSemantics, column_type_constant> MethodSemantics_Semantics{ mdtMethodSemantics_Semantics };
inline constexpr column<mdtid_MethodSemantics, cursor_raw<mdtid_MethodDef>> MethodSemantics_Method{ mdtMethodSemantics_Method };
inline constexpr column<mdtid_MethodSemantics, cursor_raw<mdtid_Event, mdtid_Property>> MethodSemantics_Association{ mdtMethodSemantics_Association };

inline constexpr column<mdtid_MethodImpl, cursor_raw<mdtid_TypeDef>> MethodImpl_Class{ mdtMethodImpl_Class };
inline constexpr column<mdtid_MethodImpl, cursor_raw<mdtid_MethodDef, mdtid_MemberRef>> MethodImpl_MethodBody{ mdtMethodImpl_MethodBody };
inline constexpr column<mdtid_MethodImpl, cursor_raw<mdtid_MethodDef, mdtid_MemberRef>> MethodImpl_MethodDeclaration{ mdtMethodImpl_MethodDeclaration };

inline constexpr column<mdtid_ModuleRef, column_type_string> ModuleRef_Name{ mdtModuleRef_Name };

inline constexpr column<mdtid_TypeSpec, column_type_blob> TypeSpec_Signature{ mdtTypeSpec_Signature };

inline constexpr column<mdtid_ImplMap, column_type_constant> ImplMap_MappingFlags{ mdtImplMap_MappingFlags };
inline constexpr column<mdtid_ImplMap, cursor_raw<mdtid_Field, mdtid_MethodDef>> ImplMap_MemberForwarded{ mdtImplMap_MemberForwarded };
inline constexpr column<mdtid_ImplMap, column_type_string> ImplMap_ImportName{ mdtImplMap_ImportName };
inline constexpr column<mdtid_ImplMap, cursor_raw<mdtid_ModuleRef>> ImplMap_ImportScope{ mdtImplMap_ImportScope };

inline constexpr column<mdtid_FieldRva, column_type_constant> FieldRva_Rva{ mdtFieldRva_Rva };
inline constexpr column<mdtid_FieldRva, cursor_raw<mdtid_Field>> FieldRva_Field{ mdtFieldRva_Field };

inline constexpr column<mdtid_ENCLog, column_type_constant> ENCLog_Token{ mdtENCLog_Token };
inline constexpr column<mdtid_ENCLog, column_type_constant> ENCLog_Op{ mdtENCLog_Op };

inline constexpr column<mdtid_ENCMap, column_type_constant> ENCMap_Token{ mdtENCMap_Token };

inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_HashAlgId{ mdtAssembly_HashAlgId };
inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_MajorVersion{ mdtAssembly_MajorVersion };
inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_MinorVersion{ mdtAssembly_MinorVersion };
inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_BuildNumber{ mdtAssembly_BuildNumber };
inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_RevisionNumber{ mdtAssembly_RevisionNumber };
inline constexpr column<mdtid_Assembly, column_type_constant> Assembly_Flags{ mdtAssembly_Flags };
inline constexpr column<mdtid_Assembly, column_type_blob> Assembly_PublicKey{ mdtAssembly_PublicKey };
inline constexpr column<mdtid_Assembly, column_type_string> Assembly_Name{ mdtAssembly_Name };
inline constexpr column<mdtid_Assembly, column_type_string> Assembly_Culture{ mdtAssembly_Culture };

inline constexpr column<mdtid_AssemblyProcessor, column_type_constant> AssemblyProcessor_Processor{ static_cast<col_index_t>(0) };

inline constexpr column<mdtid_AssemblyOS, column_type_constant> AssemblyOS_OSPlatformId{ static_cast<col_index_t>(0) };
inline constexpr column<mdtid_AssemblyOS, column_type_constant> AssemblyOS_OSMajorVersion{ static_cast<col_index_t>(1) };
inline constexpr column<mdtid_AssemblyOS, column_type_constant> AssemblyOS_OSMinorVersion{ static_cast<col_index_t>(2) };

inline constexpr column<mdtid_AssemblyRef, column_type_constant> AssemblyRef_MajorVersion{ mdtAssemblyRef_MajorVersion };
inline constexpr column<mdtid_AssemblyRef, column_type_constant> AssemblyRef_MinorVersion{ mdtAssemblyRef_MinorVersion };
inline constexpr column<mdtid_AssemblyRef, column_type_constant> AssemblyRef_BuildNumber{ mdtAssemblyRef_BuildNumber };
inline constexpr column<mdtid_AssemblyRef, column_type_constant> AssemblyRef_RevisionNumber{ mdtAssemblyRef_RevisionNumber };
inline constexpr column<mdtid_AssemblyRef, column_type_constant> AssemblyRef_Flags{ mdtAssemblyRef_Flags };
inline constexpr column<mdtid_AssemblyRef, column_type_blob> AssemblyRef_PublicKeyOrToken{ mdtAssemblyRef_PublicKeyOrToken };
inline constexpr column<mdtid_AssemblyRef, column_type_string> AssemblyRef_Name{ mdtAssemblyRef_Name };
inline constexpr column<mdtid_AssemblyRef, column_type_string> AssemblyRef_Culture{ mdtAssemblyRef_Culture };
inline constexpr column<mdtid_AssemblyRef, column_type_blob> AssemblyRef_HashValue{ mdtAssemblyRef_HashValue };

inline constexpr column<mdtid_AssemblyRefProcessor, column_type_constant> AssemblyRefProcessor_Processor{ static_cast<col_index_t>(0) };
inline constexpr column<mdtid_AssemblyRefProcessor, cursor_raw<mdtid_AssemblyRef>> AssemblyRefProcessor_AssemblyRef{ static_cast<col_index_t>(1) };

inline constexpr column<mdtid_AssemblyRefOS, column_type_constant> AssemblyRefOS_OSPlatformId{ static_cast<col_index_t>(0) };
inline constexpr column<mdtid_AssemblyRefOS, column_type_constant> AssemblyRefOS_OSMajorVersion{ static_cast<col_index_t>(1) };
inline constexpr column<mdtid_AssemblyRefOS, column_type_constant> AssemblyRefOS_OSMinorVersion{ static_cast<col_index_t>(2) };
inline constexpr column<mdtid_AssemblyRefOS, cursor_raw<mdtid_AssemblyRef>> AssemblyRefOS_AssemblyRef{ static_cast<col_index_t>(3) };

inline constexpr column<mdtid_File, column_type_constant> File_Flags{ mdtFile_Flags };
inline constexpr column<mdtid_File, column_type_string> File_Name{ mdtFile_Name };
inline constexpr column<mdtid_File, column_type_blob> File_HashValue{ mdtFile_HashValue };

inline constexpr column<mdtid_ExportedType, column_type_constant> ExportedType_Flags{ mdtExportedType_Flags };
inline constexpr column<mdtid_ExportedType, column_type_constant> ExportedType_TypeDefId{ mdtExportedType_TypeDefId };
inline constexpr column<mdtid_ExportedType, column_type_string> ExportedType_TypeName{ mdtExportedType_TypeName };
inline constexpr column<mdtid_ExportedType, column_type_string> ExportedType_TypeNamespace{ mdtExportedType_TypeNamespace };
inline constexpr column<mdtid_ExportedType, cursor_raw<mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType>> ExportedType_Implementation{ mdtExportedType_Implementation };

inline constexpr column<mdtid_ManifestResource, column_type_constant> ManifestResource_Offset{ mdtManifestResource_Offset };
inline constexpr column<mdtid_ManifestResource, column_type_constant> ManifestResource_Flags{ mdtManifestResource_Flags };
inline constexpr column<mdtid_ManifestResource, column_type_string> ManifestResource_Name{ mdtManifestResource_Name };
inline constexpr column<mdtid_ManifestResource, cursor_raw<mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType>> ManifestResource_Implementation{ mdtManifestResource_Implementation };

inline constexpr column<mdtid_NestedClass, cursor_raw<mdtid_TypeDef>> NestedClass_NestedClass{ mdtNestedClass_NestedClass };
inline constexpr column<mdtid_NestedClass, cursor_raw<mdtid_TypeDef>> NestedClass_EnclosingClass{ mdtNestedClass_EnclosingClass };

inline constexpr column<mdtid_GenericParam, column_type_constant> GenericParam_Number{ mdtGenericParam_Number };
inline constexpr column<mdtid_GenericParam, column_type_constant> GenericParam_Flags{ mdtGenericParam_Flags };
inline constexpr column<mdtid_GenericParam, cursor_raw<mdtid_TypeDef, mdtid_MethodDef>> GenericParam_Owner{ mdtGenericParam_Owner };
inline constexpr column<mdtid_GenericParam, column_type_string> GenericParam_Name{ mdtGenericParam_Name };

inline constexpr column<mdtid_MethodSpec, cursor_raw<mdtid_MethodDef, mdtid_MemberRef>> MethodSpec_Method{ mdtMethodSpec_Method };
inline constexpr column<mdtid_MethodSpec, column_type_blob> MethodSpec_Instantiation{ mdtMethodSpec_Instantiation };

inline constexpr column<mdtid_GenericParamConstraint, cursor_raw<mdtid_GenericParam>> GenericParamConstraint_Owner{ mdtGenericParamConstraint_Owner };
inline constexpr column<mdtid_GenericParamConstraint, cursor_raw<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> GenericParamConstraint_Constraint{ mdtGenericParamConstraint_Constraint };

#ifdef DNMD_PORTABLE_PDB
inline constexpr column<mdtid_Document, column_type_blob> Document_Name{ mdtDocument_Name };
inline constexpr column<mdtid_Document, column_type_guid> Document_HashAlgorithm{ mdtDocument_HashAlgorithm };
inline constexpr column<mdtid_Document, column_type_blob> Document_Hash{ mdtDocument_Hash };
inline constexpr column<mdtid_Document, column_type_guid> Document_Language{ mdtDocument_Language };

inline constexpr column<mdtid_MethodDebugInformation, cursor_raw<mdtid_Document>> MethodDebugInformation_Document{ mdtMethodDebugInformation_Document };
inline constexpr column<mdtid_MethodDebugInformation, column_type_blob> MethodDebugInformation_SequencePoints{ mdtMethodDebugInformation_SequencePoints };

inline constexpr column<mdtid_LocalScope, cursor_raw<mdtid_MethodDef>> LocalScope_Method{ mdtLocalScope_Method };
inline constexpr column<mdtid_LocalScope, cursor_raw<mdtid_ImportScope>> LocalScope_ImportScope{ mdtLocalScope_ImportScope };
inline constexpr column<mdtid_LocalScope, cursor_raw<mdtid_LocalVariable>> LocalScope_VariableList{ mdtLocalScope_VariableList };
inline constexpr column<mdtid_LocalScope, cursor_raw<mdtid_LocalConstant>> LocalScope_ConstantList{ mdtLocalScope_ConstantList };
inline constexpr column<mdtid_LocalScope, column_type_constant> LocalScope_StartOffset{ mdtLocalScope_StartOffset };
inline constexpr column<mdtid_LocalScope, column_type_constant> LocalScope_Length{ mdtLocalScope_Length };

inline constexpr column<mdtid_LocalVariable, column_type_constant> LocalVariable_Attributes{ mdtLocalVariable_Attributes };
inline constexpr column<mdtid_LocalVariable, column_type_constant> LocalVariable_Index{ mdtLocalVariable_Index };
inline constexpr column<mdtid_LocalVariable, column_type_string> LocalVariable_Name{ mdtLocalVariable_Name };

inline constexpr column<mdtid_LocalConstant, column_type_string> LocalConstant_Name{ mdtLocalConstant_Name };
inline constexpr column<mdtid_LocalConstant, column_type_blob> LocalConstant_Signature{ mdtLocalConstant_Signature };

inline constexpr column<mdtid_ImportScope, cursor_raw<mdtid_ImportScope>> ImportScope_Parent{ mdtImportScope_Parent };
inline constexpr column<mdtid_ImportScope, column_type_blob> ImportScope_Imports{ mdtImportScope_Imports };

inline constexpr column<mdtid_StateMachineMethod, cursor_raw<mdtid_MethodDef>> StateMachineMethod_MoveNextMethod{ mdtStateMachineMethod_MoveNextMethod };
inline constexpr column<mdtid_StateMachineMethod, cursor_raw<mdtid_MethodDef>> StateMachineMethod_KickoffMethod{ mdtStateMachineMethod_KickoffMethod };

inline constexpr column<mdtid_CustomDebugInformation, cursor_raw<mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param, mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property, mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec, mdtid_Assembly, mdtid_AssemblyRef, mdtid_File, mdtid_ExportedType, mdtid_ManifestResource, mdtid_GenericParam, mdtid_GenericParamConstraint, mdtid_MethodSpec, mdtid_Document, mdtid_LocalScope, mdtid_LocalVariable, mdtid_LocalConstant, mdtid_ImportScope>> CustomDebugInformation_Parent{ mdtCustomDebugInformation_Parent };
inline constexpr column<mdtid_CustomDebugInformation, column_type_guid> CustomDebugInformation_Kind{ mdtCustomDebugInformation_Kind };
inline constexpr column<mdtid_CustomDebugInformation, column_type_blob> CustomDebugInformation_Value{ mdtCustomDebugInformation_Value };
#endif // DNMD_PORTABLE_PDB
}

} // namespace dnmd

#endif // _SRC_INC_DNMD_HPP_
