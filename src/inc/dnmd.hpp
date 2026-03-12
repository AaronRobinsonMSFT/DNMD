#ifndef _SRC_INC_DNMD_HPP_
#define _SRC_INC_DNMD_HPP_

#include "dnmd.h"
#include <memory>

struct mdhandle_deleter_t final
{
    using pointer = mdhandle_t;
    void operator()(mdhandle_t handle)
    {
        ::md_destroy_handle(handle);
    }
};

// C++ lifetime wrapper for mdhandle_t type
using mdhandle_ptr = std::unique_ptr<mdhandle_t, mdhandle_deleter_t>;

struct md_added_row_t final
{
private:
    mdcursor_t new_row;
public:
    md_added_row_t() = default;
    explicit md_added_row_t(mdcursor_t row) : new_row{ row } {}
    md_added_row_t(md_added_row_t const& other) = delete;
    md_added_row_t(md_added_row_t&& other)
    {
        *this = std::move(other);
    }

    md_added_row_t& operator=(md_added_row_t const& other) = delete;
    md_added_row_t& operator=(md_added_row_t&& other)
    {
        new_row = other.new_row;
        other.new_row = {}; // Clear the other's row so we don't double-commit.
        return *this;
    }

    ~md_added_row_t()
    {
        md_commit_row_add(new_row);
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

struct md_column_type_blob {};
struct md_column_type_string {};
struct md_column_type_guid {};
struct md_column_type_userstring {};
struct md_column_type_constant {};

template<mdtable_id_t TableId>
struct mdcursor;

template<mdtable_id_t... TableIds>
struct mdcoded_index;

template<mdtable_id_t TableId, typename ColumnType>
struct mdcolumn final
{
    template<mdtable_id_t TableId>
    friend struct mdcursor;

    col_index_t column_index;
    public:
        explicit constexpr mdcolumn(col_index_t col)
            : column_index{ col }
        {
        }
};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct mdcursor_indirect;

template<mdtable_id_t TableId>
struct mdcursor final
{
    private:
        mdcursor_t cursor;
    public:
        mdcursor() = default;
        explicit mdcursor(mdcursor_t c) : cursor{ c }
        {
            mdtoken tk;
            if (md_cursor_to_token(cursor, &tk))
            {
                mdtable_id_t table_id = ExtractTokenType(tk);
                assert(table_id == TableId);
            }
        }

        operator mdcursor_t()
        {
            return cursor;
        }

        template<mdtable_id_t TargetTableId>
        bool get_column_value(mdcolumn<TableId, mdcursor<TargetTableId>> column, mdcursor<TargetTableId>& target_cursor)
        {
            return md_get_column_value_as_cursor(cursor, column.column_index, &target_cursor.cursor);
        }

        template<mdtable_id_t... TargetTableIds>
        bool get_column_value(mdcolumn<TableId, mdcoded_index<TargetTableIds...>> column, mdcoded_index<TargetTableIds...>& target_coded_index)
        {
            return md_get_column_value_as_cursor(cursor, column.column_index, &target_coded_index.cursor);
        }

        template<mdtable_id_t TargetTableId>
        bool get_column_value(mdcolumn<TableId, mdcursor<TargetTableId>> column, mdToken& tk)
        {
            return md_get_column_value_as_token(cursor, column.column_index, &tk);
        }

        template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
        bool get_column_value(mdcolumn<TableId, mdcursor_indirect<DirectTableId, IndirectTableId>> column, mdcursor_indirect<DirectTableId, IndirectTableId>& target_cursor)
        {
            return md_get_column_value_as_cursor(cursor, column.column_index, &target_cursor.cursor);
        }

        bool get_column_value(mdcolumn<TableId, md_column_type_blob> column, uint8_t const** blob, uint32_t* blob_len)
        {
            return md_get_column_value_as_blob(cursor, column.column_index, blob, blob_len);
        }

        bool get_column_value(mdcolumn<TableId, md_column_type_guid> column, mdguid_t* guid)
        {
            return md_get_column_value_as_guid(cursor, column.column_index, guid);
        }

        bool get_column_value(mdcolumn<TableId, md_column_type_userstring> column, mduserstring_t* userstring)
        {
            return md_get_column_value_as_userstring(cursor, column.column_index, userstring);
        }

        bool get_column_value(mdcolumn<TableId, md_column_type_string> column, char const** str)
        {
            return md_get_column_value_as_utf8(cursor, column.column_index, str);
        }

        template<mdtable_id_t TargetTableId, mdtable_id_t IndirectTableId>
        bool get_column_value_as_range(mdcolumn<TableId, mdcursor_indirect<TargetTableId, IndirectTableId>> column, mdcursor_indirect<TargetTableId, IndirectTableId>& range_cursor, uint32_t* count)
        {
            return md_get_column_value_as_range(cursor, column.column_index, &range_cursor.cursor, count)
        }
};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct mdcursor_indirect final
{
    private:
        mdcursor_t cursor;
    public:
        mdcursor_indirect() = default;
        explicit constexpr mdcursor_indirect(mdcursor_t c) : cursor{ c }
        {
            mdtoken tk;
            if (md_cursor_to_token(cursor, &tk))
            {
                mdtable_id_t table_id = ExtractTokenType(tk);
                assert(table_id == DirectTableId || table_id == IndirectTableId);
            }
        }

        operator mdcursor_t()
        {
            return cursor;
        }

        bool try_get(mdcursor<DirectTableId>& direct_cursor)
        {
            mdtoken tk;
            if (!md_cursor_to_token(cursor, &tk))
                return false;

            mdcursor_t target_cursor;
            if (!md_resolve_indirect_cursor(cursor, &target_cursor))
                return false;

            direct_cursor = mdcursor<DirectTableId>(target_cursor);
            return true;
        }
};

template<mdtable_id_t... TableIds>
struct mdcoded_index final
{
    private:
        mdcursor_t cursor;
    public:
        mdcoded_index() = default;
        explicit constexpr mdcoded_index(mdcursor_t c)
            : cursor{ c }
        {
            mdtoken tk;
            if (md_cursor_to_token(cursor, &tk))
            {
                mdtable_id_t table_id = ExtractTokenType(tk);
                assert(((table_id == TableIds) || ...));
            }
        }
    
        template<mdtable_id_t CodedTableId>
        bool get_as(mdcursor<CodedTableId>& target_cursor)
        {
            static_assert(((CodedTableId == TableIds) || ...) , "CodedTableId must be one of the tables in the coded index.");
            mdtoken tk;
            if (!md_cursor_to_token(cursor, &tk))
                return false;

            if (ExtractTokenType(tk) != CodedTableId)
                return false;
            
            target_cursor = mdcursor<CodedTableId>(cursor);
            return true;
        }

        template<typename TCallable>
        void visit(TCallable&& callable)
        {
            (visit_impl<TableIds>(std::forward<TCallable>(callable)) || false ...);
        }

    private:
        template<mdtable_id_t CodedTableId, typename TCallable>
        bool visit_impl(TCallable&& callable)
        {
            mdtoken tk;
            if (!md_cursor_to_token(cursor, &tk))
                return false;
            if (ExtractTokenType(tk) != CodedTableId)
                return false;
            callable(mdcursor<CodedTableId>(cursor));
            return true;
        }
};

template<mdtable_id_t TableId>
inline bool md_cursor_next(mdcursor<TableId>& cursor)
{
    mdcursor_t c = cursor;
    if (!md_cursor_next(&c))
        return false;
    cursor = mdcursor<TableId>{c};
    return true;
}

template<mdtable_id_t TableId, mdtable_id_t IndirectTableId>
inline bool md_cursor_next(mdcursor_indirect<TableId, IndirectTableId>& cursor)
{
    mdcursor_t c = cursor;
    if (!md_cursor_next(&c))
        return false;
    cursor = mdcursor_indirect<TableId, IndirectTableId>{c};
    return true;
}

// Strongly-typed overloads for md_find_row_from_cursor

// Overload for constant columns
template<mdtable_id_t TableId>
inline bool md_find_row_from_cursor(mdcursor<TableId> begin, mdcolumn<TableId, md_column_type_constant> column, uint32_t value, mdcursor<TableId>& cursor)
{
    mdcursor_t c;
    bool result = ::md_find_row_from_cursor(begin, column.column_index, value, &c);
    if (result)
        cursor = mdcursor<TableId>{c};
    return result;
}

// Overload for cursor columns
template<mdtable_id_t TableId, mdtable_id_t TargetTableId>
inline bool md_find_row_from_cursor(mdcursor<TableId> begin, mdcolumn<TableId, mdcursor<TargetTableId>> column, mdToken tk, mdcursor<TableId>& cursor)
{
    mdToken tk;
    if (!::md_cursor_to_token(value, &tk))
        return false;
    
    uint32_t rid = tk & 0x00FFFFFF; // Extract RID from token
    mdcursor_t c;
    bool result = ::md_find_row_from_cursor(begin, column.column_index, rid, &c);
    if (result)
        cursor = mdcursor<TableId>{c};
    return result;
}

// Overload for cursor columns
template<mdtable_id_t TableId, mdtable_id_t TargetTableId>
inline bool md_find_row_from_cursor(mdcursor<TableId> begin, mdcolumn<TableId, mdcursor<TargetTableId>> column, mdcursor<TargetTableId> value, mdcursor<TableId>& cursor)
{
    mdToken tk;
    if (!::md_cursor_to_token(value, &tk))
        return false;
    
    return md_find_row_from_cursor(begin, column, tk, cursor);
}

// Overload for indirect cursor columns
template<mdtable_id_t TableId, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline bool md_find_row_from_cursor(mdcursor<TableId> begin, mdcolumn<TableId, mdcursor_indirect<DirectTableId, IndirectTableId>> column, mdcursor<DirectTableId> value, mdcursor<TableId>& cursor)
{
    mdToken tk;
    if (!::md_cursor_to_token(value, &tk))
        return false;
    
    uint32_t rid = tk & 0x00FFFFFF; // Extract RID from token
    mdcursor_t c;
    bool result = ::md_find_row_from_cursor(begin, column.column_index, rid, &c);
    if (result)
        cursor = mdcursor<TableId>{c};
    return result;
}

// Overload for coded index columns
template<mdtable_id_t TableId, mdtable_id_t... CodedTableIds>
inline bool md_find_row_from_cursor(mdcursor<TableId> begin, mdcolumn<TableId, mdcoded_index<CodedTableIds...>> column, mdToken tk, mdcursor<TableId>& cursor)
{
    mdcursor_t c;
    bool result = ::md_find_row_from_cursor(begin, column.column_index, tk, &c);
    if (result)
        cursor = mdcursor<TableId>{c};
    return result;
}

// Overload for coded index columns
template<mdtable_id_t TableId, mdtable_id_t... CodedTableIds, mdtable_id_t SearchTableId>
inline bool md_find_row_from_cursor(mdcursor<TableId> begin, mdcolumn<TableId, mdcoded_index<CodedTableIds...>> column, mdcursor<SearchTableId> value, mdcursor<TableId>& cursor)
{
    mdToken tk;
    if (!::md_cursor_to_token(value, &tk))
        return false;

    return md_find_row_from_cursor(begin, column, tk, cursor);
}

template<mdtable_id_t TableId>
inline bool md_create_cursor(mdhandle_t handle, mdcursor<TableId>& cursor, uint32_t* count)
{
    mdcursor_t c;
    bool result = ::md_create_cursor(handle, TableId, &c, count);
    if (result)
        cursor = mdcursor<TableId>{c};
    return result;
}

namespace dnmd_columns
{
inline constexpr mdcolumn<mdtid_Module, md_column_type_constant> Module_Generation{ mdtModule_Generation };
inline constexpr mdcolumn<mdtid_Module, md_column_type_string> Module_Name{ mdtModule_Name };
inline constexpr mdcolumn<mdtid_Module, md_column_type_guid> Module_Mvid{ mdtModule_Mvid };
inline constexpr mdcolumn<mdtid_Module, md_column_type_guid> Module_EncId{ mdtModule_EncId };
inline constexpr mdcolumn<mdtid_Module, md_column_type_guid> Module_EncBaseId{ mdtModule_EncBaseId };

inline constexpr mdcolumn<mdtid_TypeRef, mdcoded_index<mdtid_Module, mdtid_ModuleRef, mdtid_AssemblyRef, mdtid_TypeRef>> TypeRef_ResolutionScope{ mdtTypeRef_ResolutionScope };
inline constexpr mdcolumn<mdtid_TypeRef, md_column_type_string> TypeRef_TypeName{ mdtTypeRef_TypeName };
inline constexpr mdcolumn<mdtid_TypeRef, md_column_type_string> TypeRef_TypeNamespace{ mdtTypeRef_TypeNamespace };

inline constexpr mdcolumn<mdtid_TypeDef, md_column_type_constant> TypeDef_Flags{ mdtTypeDef_Flags };
inline constexpr mdcolumn<mdtid_TypeDef, md_column_type_string> TypeDef_TypeName{ mdtTypeDef_TypeName };
inline constexpr mdcolumn<mdtid_TypeDef, md_column_type_string> TypeDef_TypeNamespace{ mdtTypeDef_TypeNamespace };
inline constexpr mdcolumn<mdtid_TypeDef, mdcoded_index<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> TypeDef_Extends{ mdtTypeDef_Extends };
inline constexpr mdcolumn<mdtid_TypeDef, mdcursor_indirect<mdtid_Field, mdtid_FieldPtr>> TypeDef_FieldList{ mdtTypeDef_FieldList };
inline constexpr mdcolumn<mdtid_TypeDef, mdcursor_indirect<mdtid_MethodDef, mdtid_MethodPtr>> TypeDef_MethodList{ mdtTypeDef_MethodList };

inline constexpr mdcolumn<mdtid_Field, md_column_type_constant> Field_Flags{ mdtField_Flags };
inline constexpr mdcolumn<mdtid_Field, md_column_type_string> Field_Name{ mdtField_Name };
inline constexpr mdcolumn<mdtid_Field, md_column_type_blob> Field_Signature{ mdtField_Signature };

inline constexpr mdcolumn<mdtid_MethodDef, md_column_type_constant> MethodDef_Rva{ mdtMethodDef_Rva };
inline constexpr mdcolumn<mdtid_MethodDef, md_column_type_constant> MethodDef_ImplFlags{ mdtMethodDef_ImplFlags };
inline constexpr mdcolumn<mdtid_MethodDef, md_column_type_constant> MethodDef_Flags{ mdtMethodDef_Flags };
inline constexpr mdcolumn<mdtid_MethodDef, md_column_type_string> MethodDef_Name{ mdtMethodDef_Name };
inline constexpr mdcolumn<mdtid_MethodDef, md_column_type_blob> MethodDef_Signature{ mdtMethodDef_Signature };
inline constexpr mdcolumn<mdtid_MethodDef, mdcursor_indirect<mdtid_Param, mdtid_ParamPtr>> MethodDef_ParamList{ mdtMethodDef_ParamList };

inline constexpr mdcolumn<mdtid_Param, md_column_type_constant> Param_Flags{ mdtParam_Flags };
inline constexpr mdcolumn<mdtid_Param, md_column_type_constant> Param_Sequence{ mdtParam_Sequence };
inline constexpr mdcolumn<mdtid_Param, md_column_type_string> Param_Name{ mdtParam_Name };

inline constexpr mdcolumn<mdtid_InterfaceImpl, mdcursor<mdtid_TypeDef>> InterfaceImpl_Class{ mdtInterfaceImpl_Class };
inline constexpr mdcolumn<mdtid_InterfaceImpl, mdcoded_index<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> InterfaceImpl_Interface{ mdtInterfaceImpl_Interface };

inline constexpr mdcolumn<mdtid_MemberRef, mdcoded_index<mdtid_TypeDef, mdtid_TypeRef, mdtid_ModuleRef, mdtid_MethodDef, mdtid_TypeSpec>> MemberRef_Class{ mdtMemberRef_Class };
inline constexpr mdcolumn<mdtid_MemberRef, md_column_type_string> MemberRef_Name{ mdtMemberRef_Name };
inline constexpr mdcolumn<mdtid_MemberRef, md_column_type_blob> MemberRef_Signature{ mdtMemberRef_Signature };

inline constexpr mdcolumn<mdtid_Constant, md_column_type_constant> Constant_Type{ mdtConstant_Type };
inline constexpr mdcolumn<mdtid_Constant, mdcoded_index<mdtid_Field, mdtid_Param, mdtid_Property>> Constant_Parent{ mdtConstant_Parent };
inline constexpr mdcolumn<mdtid_Constant, md_column_type_blob> Constant_Value{ mdtConstant_Value };

inline constexpr mdcolumn<mdtid_CustomAttribute, mdcoded_index<mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param, mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property, mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec, mdtid_Assembly, mdtid_AssemblyRef, mdtid_File, mdtid_ExportedType, mdtid_ManifestResource, mdtid_GenericParam, mdtid_GenericParamConstraint, mdtid_MethodSpec>> CustomAttribute_Parent{ mdtCustomAttribute_Parent };
inline constexpr mdcolumn<mdtid_CustomAttribute, mdcoded_index<mdtid_MethodDef, mdtid_MemberRef>> CustomAttribute_Type{ mdtCustomAttribute_Type };
inline constexpr mdcolumn<mdtid_CustomAttribute, md_column_type_blob> CustomAttribute_Value{ mdtCustomAttribute_Value };

inline constexpr mdcolumn<mdtid_FieldMarshal, mdcoded_index<mdtid_Field, mdtid_Param>> FieldMarshal_Parent{ mdtFieldMarshal_Parent };
inline constexpr mdcolumn<mdtid_FieldMarshal, md_column_type_blob> FieldMarshal_NativeType{ mdtFieldMarshal_NativeType };

inline constexpr mdcolumn<mdtid_DeclSecurity, md_column_type_constant> DeclSecurity_Action{ mdtDeclSecurity_Action };
inline constexpr mdcolumn<mdtid_DeclSecurity, mdcoded_index<mdtid_TypeDef, mdtid_MethodDef, mdtid_Assembly>> DeclSecurity_Parent{ mdtDeclSecurity_Parent };
inline constexpr mdcolumn<mdtid_DeclSecurity, md_column_type_blob> DeclSecurity_PermissionSet{ mdtDeclSecurity_PermissionSet };

inline constexpr mdcolumn<mdtid_ClassLayout, md_column_type_constant> ClassLayout_PackingSize{ mdtClassLayout_PackingSize };
inline constexpr mdcolumn<mdtid_ClassLayout, md_column_type_constant> ClassLayout_ClassSize{ mdtClassLayout_ClassSize };
inline constexpr mdcolumn<mdtid_ClassLayout, mdcursor<mdtid_TypeDef>> ClassLayout_Parent{ mdtClassLayout_Parent };

inline constexpr mdcolumn<mdtid_FieldLayout, md_column_type_constant> FieldLayout_Offset{ mdtFieldLayout_Offset };
inline constexpr mdcolumn<mdtid_FieldLayout, mdcursor<mdtid_Field>> FieldLayout_Field{ mdtFieldLayout_Field };

inline constexpr mdcolumn<mdtid_StandAloneSig, md_column_type_blob> StandAloneSig_Signature{ mdtStandAloneSig_Signature };

inline constexpr mdcolumn<mdtid_EventMap, mdcursor<mdtid_TypeDef>> EventMap_Parent{ mdtEventMap_Parent };
inline constexpr mdcolumn<mdtid_EventMap, mdcursor_indirect<mdtid_Event, mdtid_EventPtr>> EventMap_EventList{ mdtEventMap_EventList };

inline constexpr mdcolumn<mdtid_Event, md_column_type_constant> Event_EventFlags{ mdtEvent_EventFlags };
inline constexpr mdcolumn<mdtid_Event, md_column_type_string> Event_Name{ mdtEvent_Name };
inline constexpr mdcolumn<mdtid_Event, mdcoded_index<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> Event_EventType{ mdtEvent_EventType };

inline constexpr mdcolumn<mdtid_PropertyMap, mdcursor<mdtid_TypeDef>> PropertyMap_Parent{ mdtPropertyMap_Parent };
inline constexpr mdcolumn<mdtid_PropertyMap, mdcursor_indirect<mdtid_Property, mdtid_PropertyPtr>> PropertyMap_PropertyList{ mdtPropertyMap_PropertyList };

inline constexpr mdcolumn<mdtid_Property, md_column_type_constant> Property_Flags{ mdtProperty_Flags };
inline constexpr mdcolumn<mdtid_Property, md_column_type_string> Property_Name{ mdtProperty_Name };
inline constexpr mdcolumn<mdtid_Property, md_column_type_blob> Property_Type{ mdtProperty_Type };

inline constexpr mdcolumn<mdtid_MethodSemantics, md_column_type_constant> MethodSemantics_Semantics{ mdtMethodSemantics_Semantics };
inline constexpr mdcolumn<mdtid_MethodSemantics, mdcursor<mdtid_MethodDef>> MethodSemantics_Method{ mdtMethodSemantics_Method };
inline constexpr mdcolumn<mdtid_MethodSemantics, mdcoded_index<mdtid_Event, mdtid_Property>> MethodSemantics_Association{ mdtMethodSemantics_Association };

inline constexpr mdcolumn<mdtid_MethodImpl, mdcursor<mdtid_TypeDef>> MethodImpl_Class{ mdtMethodImpl_Class };
inline constexpr mdcolumn<mdtid_MethodImpl, mdcoded_index<mdtid_MethodDef, mdtid_MemberRef>> MethodImpl_MethodBody{ mdtMethodImpl_MethodBody };
inline constexpr mdcolumn<mdtid_MethodImpl, mdcoded_index<mdtid_MethodDef, mdtid_MemberRef>> MethodImpl_MethodDeclaration{ mdtMethodImpl_MethodDeclaration };

inline constexpr mdcolumn<mdtid_ModuleRef, md_column_type_string> ModuleRef_Name{ mdtModuleRef_Name };

inline constexpr mdcolumn<mdtid_TypeSpec, md_column_type_blob> TypeSpec_Signature{ mdtTypeSpec_Signature };

inline constexpr mdcolumn<mdtid_ImplMap, md_column_type_constant> ImplMap_MappingFlags{ mdtImplMap_MappingFlags };
inline constexpr mdcolumn<mdtid_ImplMap, mdcoded_index<mdtid_Field, mdtid_MethodDef>> ImplMap_MemberForwarded{ mdtImplMap_MemberForwarded };
inline constexpr mdcolumn<mdtid_ImplMap, md_column_type_string> ImplMap_ImportName{ mdtImplMap_ImportName };
inline constexpr mdcolumn<mdtid_ImplMap, mdcursor<mdtid_ModuleRef>> ImplMap_ImportScope{ mdtImplMap_ImportScope };

inline constexpr mdcolumn<mdtid_FieldRva, md_column_type_constant> FieldRva_Rva{ mdtFieldRva_Rva };
inline constexpr mdcolumn<mdtid_FieldRva, mdcursor<mdtid_Field>> FieldRva_Field{ mdtFieldRva_Field };

inline constexpr mdcolumn<mdtid_ENCLog, md_column_type_constant> ENCLog_Token{ mdtENCLog_Token };
inline constexpr mdcolumn<mdtid_ENCLog, md_column_type_constant> ENCLog_Op{ mdtENCLog_Op };

inline constexpr mdcolumn<mdtid_ENCMap, md_column_type_constant> ENCMap_Token{ mdtENCMap_Token };

inline constexpr mdcolumn<mdtid_Assembly, md_column_type_constant> Assembly_HashAlgId{ mdtAssembly_HashAlgId };
inline constexpr mdcolumn<mdtid_Assembly, md_column_type_constant> Assembly_MajorVersion{ mdtAssembly_MajorVersion };
inline constexpr mdcolumn<mdtid_Assembly, md_column_type_constant> Assembly_MinorVersion{ mdtAssembly_MinorVersion };
inline constexpr mdcolumn<mdtid_Assembly, md_column_type_constant> Assembly_BuildNumber{ mdtAssembly_BuildNumber };
inline constexpr mdcolumn<mdtid_Assembly, md_column_type_constant> Assembly_RevisionNumber{ mdtAssembly_RevisionNumber };
inline constexpr mdcolumn<mdtid_Assembly, md_column_type_constant> Assembly_Flags{ mdtAssembly_Flags };
inline constexpr mdcolumn<mdtid_Assembly, md_column_type_blob> Assembly_PublicKey{ mdtAssembly_PublicKey };
inline constexpr mdcolumn<mdtid_Assembly, md_column_type_string> Assembly_Name{ mdtAssembly_Name };
inline constexpr mdcolumn<mdtid_Assembly, md_column_type_string> Assembly_Culture{ mdtAssembly_Culture };

inline constexpr mdcolumn<mdtid_AssemblyProcessor, md_column_type_constant> AssemblyProcessor_Processor{ static_cast<col_index_t>(0) };

inline constexpr mdcolumn<mdtid_AssemblyOS, md_column_type_constant> AssemblyOS_OSPlatformId{ static_cast<col_index_t>(0) };
inline constexpr mdcolumn<mdtid_AssemblyOS, md_column_type_constant> AssemblyOS_OSMajorVersion{ static_cast<col_index_t>(1) };
inline constexpr mdcolumn<mdtid_AssemblyOS, md_column_type_constant> AssemblyOS_OSMinorVersion{ static_cast<col_index_t>(2) };

inline constexpr mdcolumn<mdtid_AssemblyRef, md_column_type_constant> AssemblyRef_MajorVersion{ mdtAssemblyRef_MajorVersion };
inline constexpr mdcolumn<mdtid_AssemblyRef, md_column_type_constant> AssemblyRef_MinorVersion{ mdtAssemblyRef_MinorVersion };
inline constexpr mdcolumn<mdtid_AssemblyRef, md_column_type_constant> AssemblyRef_BuildNumber{ mdtAssemblyRef_BuildNumber };
inline constexpr mdcolumn<mdtid_AssemblyRef, md_column_type_constant> AssemblyRef_RevisionNumber{ mdtAssemblyRef_RevisionNumber };
inline constexpr mdcolumn<mdtid_AssemblyRef, md_column_type_constant> AssemblyRef_Flags{ mdtAssemblyRef_Flags };
inline constexpr mdcolumn<mdtid_AssemblyRef, md_column_type_blob> AssemblyRef_PublicKeyOrToken{ mdtAssemblyRef_PublicKeyOrToken };
inline constexpr mdcolumn<mdtid_AssemblyRef, md_column_type_string> AssemblyRef_Name{ mdtAssemblyRef_Name };
inline constexpr mdcolumn<mdtid_AssemblyRef, md_column_type_string> AssemblyRef_Culture{ mdtAssemblyRef_Culture };
inline constexpr mdcolumn<mdtid_AssemblyRef, md_column_type_blob> AssemblyRef_HashValue{ mdtAssemblyRef_HashValue };

inline constexpr mdcolumn<mdtid_AssemblyRefProcessor, md_column_type_constant> AssemblyRefProcessor_Processor{ static_cast<col_index_t>(0) };
inline constexpr mdcolumn<mdtid_AssemblyRefProcessor, mdcursor<mdtid_AssemblyRef>> AssemblyRefProcessor_AssemblyRef{ static_cast<col_index_t>(1) };

inline constexpr mdcolumn<mdtid_AssemblyRefOS, md_column_type_constant> AssemblyRefOS_OSPlatformId{ static_cast<col_index_t>(0) };
inline constexpr mdcolumn<mdtid_AssemblyRefOS, md_column_type_constant> AssemblyRefOS_OSMajorVersion{ static_cast<col_index_t>(1) };
inline constexpr mdcolumn<mdtid_AssemblyRefOS, md_column_type_constant> AssemblyRefOS_OSMinorVersion{ static_cast<col_index_t>(2) };
inline constexpr mdcolumn<mdtid_AssemblyRefOS, mdcursor<mdtid_AssemblyRef>> AssemblyRefOS_AssemblyRef{ static_cast<col_index_t>(3) };

inline constexpr mdcolumn<mdtid_File, md_column_type_constant> File_Flags{ mdtFile_Flags };
inline constexpr mdcolumn<mdtid_File, md_column_type_string> File_Name{ mdtFile_Name };
inline constexpr mdcolumn<mdtid_File, md_column_type_blob> File_HashValue{ mdtFile_HashValue };

inline constexpr mdcolumn<mdtid_ExportedType, md_column_type_constant> ExportedType_Flags{ mdtExportedType_Flags };
inline constexpr mdcolumn<mdtid_ExportedType, md_column_type_constant> ExportedType_TypeDefId{ mdtExportedType_TypeDefId };
inline constexpr mdcolumn<mdtid_ExportedType, md_column_type_string> ExportedType_TypeName{ mdtExportedType_TypeName };
inline constexpr mdcolumn<mdtid_ExportedType, md_column_type_string> ExportedType_TypeNamespace{ mdtExportedType_TypeNamespace };
inline constexpr mdcolumn<mdtid_ExportedType, mdcoded_index<mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType>> ExportedType_Implementation{ mdtExportedType_Implementation };

inline constexpr mdcolumn<mdtid_ManifestResource, md_column_type_constant> ManifestResource_Offset{ mdtManifestResource_Offset };
inline constexpr mdcolumn<mdtid_ManifestResource, md_column_type_constant> ManifestResource_Flags{ mdtManifestResource_Flags };
inline constexpr mdcolumn<mdtid_ManifestResource, md_column_type_string> ManifestResource_Name{ mdtManifestResource_Name };
inline constexpr mdcolumn<mdtid_ManifestResource, mdcoded_index<mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType>> ManifestResource_Implementation{ mdtManifestResource_Implementation };

inline constexpr mdcolumn<mdtid_NestedClass, mdcursor<mdtid_TypeDef>> NestedClass_NestedClass{ mdtNestedClass_NestedClass };
inline constexpr mdcolumn<mdtid_NestedClass, mdcursor<mdtid_TypeDef>> NestedClass_EnclosingClass{ mdtNestedClass_EnclosingClass };

inline constexpr mdcolumn<mdtid_GenericParam, md_column_type_constant> GenericParam_Number{ mdtGenericParam_Number };
inline constexpr mdcolumn<mdtid_GenericParam, md_column_type_constant> GenericParam_Flags{ mdtGenericParam_Flags };
inline constexpr mdcolumn<mdtid_GenericParam, mdcoded_index<mdtid_TypeDef, mdtid_MethodDef>> GenericParam_Owner{ mdtGenericParam_Owner };
inline constexpr mdcolumn<mdtid_GenericParam, md_column_type_string> GenericParam_Name{ mdtGenericParam_Name };

inline constexpr mdcolumn<mdtid_MethodSpec, mdcoded_index<mdtid_MethodDef, mdtid_MemberRef>> MethodSpec_Method{ mdtMethodSpec_Method };
inline constexpr mdcolumn<mdtid_MethodSpec, md_column_type_blob> MethodSpec_Instantiation{ mdtMethodSpec_Instantiation };

inline constexpr mdcolumn<mdtid_GenericParamConstraint, mdcursor<mdtid_GenericParam>> GenericParamConstraint_Owner{ mdtGenericParamConstraint_Owner };
inline constexpr mdcolumn<mdtid_GenericParamConstraint, mdcoded_index<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> GenericParamConstraint_Constraint{ mdtGenericParamConstraint_Constraint };

#ifdef DNMD_PORTABLE_PDB
inline constexpr mdcolumn<mdtid_Document, md_column_type_blob> Document_Name{ mdtDocument_Name };
inline constexpr mdcolumn<mdtid_Document, md_column_type_guid> Document_HashAlgorithm{ mdtDocument_HashAlgorithm };
inline constexpr mdcolumn<mdtid_Document, md_column_type_blob> Document_Hash{ mdtDocument_Hash };
inline constexpr mdcolumn<mdtid_Document, md_column_type_guid> Document_Language{ mdtDocument_Language };

inline constexpr mdcolumn<mdtid_MethodDebugInformation, mdcursor<mdtid_Document>> MethodDebugInformation_Document{ mdtMethodDebugInformation_Document };
inline constexpr mdcolumn<mdtid_MethodDebugInformation, md_column_type_blob> MethodDebugInformation_SequencePoints{ mdtMethodDebugInformation_SequencePoints };

inline constexpr mdcolumn<mdtid_LocalScope, mdcursor<mdtid_MethodDef>> LocalScope_Method{ mdtLocalScope_Method };
inline constexpr mdcolumn<mdtid_LocalScope, mdcursor<mdtid_ImportScope>> LocalScope_ImportScope{ mdtLocalScope_ImportScope };
inline constexpr mdcolumn<mdtid_LocalScope, mdcursor<mdtid_LocalVariable>> LocalScope_VariableList{ mdtLocalScope_VariableList };
inline constexpr mdcolumn<mdtid_LocalScope, mdcursor<mdtid_LocalConstant>> LocalScope_ConstantList{ mdtLocalScope_ConstantList };
inline constexpr mdcolumn<mdtid_LocalScope, md_column_type_constant> LocalScope_StartOffset{ mdtLocalScope_StartOffset };
inline constexpr mdcolumn<mdtid_LocalScope, md_column_type_constant> LocalScope_Length{ mdtLocalScope_Length };

inline constexpr mdcolumn<mdtid_LocalVariable, md_column_type_constant> LocalVariable_Attributes{ mdtLocalVariable_Attributes };
inline constexpr mdcolumn<mdtid_LocalVariable, md_column_type_constant> LocalVariable_Index{ mdtLocalVariable_Index };
inline constexpr mdcolumn<mdtid_LocalVariable, md_column_type_string> LocalVariable_Name{ mdtLocalVariable_Name };

inline constexpr mdcolumn<mdtid_LocalConstant, md_column_type_string> LocalConstant_Name{ mdtLocalConstant_Name };
inline constexpr mdcolumn<mdtid_LocalConstant, md_column_type_blob> LocalConstant_Signature{ mdtLocalConstant_Signature };

inline constexpr mdcolumn<mdtid_ImportScope, mdcursor<mdtid_ImportScope>> ImportScope_Parent{ mdtImportScope_Parent };
inline constexpr mdcolumn<mdtid_ImportScope, md_column_type_blob> ImportScope_Imports{ mdtImportScope_Imports };

inline constexpr mdcolumn<mdtid_StateMachineMethod, mdcursor<mdtid_MethodDef>> StateMachineMethod_MoveNextMethod{ mdtStateMachineMethod_MoveNextMethod };
inline constexpr mdcolumn<mdtid_StateMachineMethod, mdcursor<mdtid_MethodDef>> StateMachineMethod_KickoffMethod{ mdtStateMachineMethod_KickoffMethod };

inline constexpr mdcolumn<mdtid_CustomDebugInformation, mdcoded_index<mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param, mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property, mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec, mdtid_Assembly, mdtid_AssemblyRef, mdtid_File, mdtid_ExportedType, mdtid_ManifestResource, mdtid_GenericParam, mdtid_GenericParamConstraint, mdtid_MethodSpec, mdtid_Document, mdtid_LocalScope, mdtid_LocalVariable, mdtid_LocalConstant, mdtid_ImportScope>> CustomDebugInformation_Parent{ mdtCustomDebugInformation_Parent };
inline constexpr mdcolumn<mdtid_CustomDebugInformation, md_column_type_guid> CustomDebugInformation_Kind{ mdtCustomDebugInformation_Kind };
inline constexpr mdcolumn<mdtid_CustomDebugInformation, md_column_type_blob> CustomDebugInformation_Value{ mdtCustomDebugInformation_Value };
#endif // DNMD_PORTABLE_PDB
}

#endif // _SRC_INC_DNMD_HPP_
