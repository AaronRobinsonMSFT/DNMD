#ifndef _SRC_INC_DNMD_SCHEMA_HPP_
#define _SRC_INC_DNMD_SCHEMA_HPP_

#include "dnmd.h"
#include <array>
#include <cassert>
#include <tuple>
#include <type_traits>
#include <utility>

namespace dnmd
{

struct column_type_blob {};
struct column_type_string {};
struct column_type_guid {};
struct column_type_userstring {};
struct column_type_constant {};

template<mdtable_id_t TableId>
struct cursor;

template<mdtable_id_t... TableIds>
struct raw_cursor;

template<mdtable_id_t TableId, typename ColumnType>
struct column final
{
    using value_type = ColumnType;
    static constexpr mdtable_id_t table_id = TableId;

    col_index_t column_index;
    public:
        explicit constexpr column(col_index_t col)
            : column_index{ col }
        {
        }
};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct indirect_cursor;

template<mdtable_id_t TableId>
struct row_reader;

template<mdtable_id_t TableId>
struct row_writer;

template<mdtable_id_t TableId>
struct added_row;

namespace detail
{
struct row_storage_access;

struct stored_column_value {};
struct list_boundary_value {};

// Parentheses keep the C backend's function-like macros from expanding here.
constexpr mdtcol_t (InsertOffset)(uint32_t offset) noexcept
{
    return static_cast<mdtcol_t>((offset << 2) & mdtc_comask);
}

constexpr uint32_t (ExtractOffset)(uint32_t details) noexcept
{
    return (details & mdtc_comask) >> 2;
}

template<typename ColumnType>
struct searchable_column : std::false_type {};

template<>
struct searchable_column<column_type_constant> : std::true_type {};

template<mdtable_id_t... TableIds>
struct searchable_column<raw_cursor<TableIds...>> : std::true_type {};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct searchable_column<indirect_cursor<DirectTableId, IndirectTableId>> : std::true_type {};

// Parentheses avoid expanding corhdr.h's function-like macro.
inline uint32_t (RidFromToken)(uint64_t tk) noexcept
{
    return static_cast<uint32_t>(tk) & 0x00ffffff;
}

template<mdtable_id_t TableId, mdtable_id_t... TableIds>
inline constexpr bool contains_table_id_v = ((TableId == TableIds) || ...);

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

template<typename ColumnType, mdtable_id_t... ValueTableIds>
struct search_reference_tables : std::false_type {};

template<mdtable_id_t... TargetTableIds, mdtable_id_t... ValueTableIds>
struct search_reference_tables<raw_cursor<TargetTableIds...>, ValueTableIds...>
    : std::conjunction<std::bool_constant<contains_table_id_v<ValueTableIds, TargetTableIds...>>...> {};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId, mdtable_id_t... ValueTableIds>
struct search_reference_tables<indirect_cursor<DirectTableId, IndirectTableId>, ValueTableIds...>
    : std::conjunction<std::bool_constant<contains_table_id_v<ValueTableIds, DirectTableId, IndirectTableId>>...> {};

template<typename ColumnType, typename Value>
struct search_reference_value : std::false_type {};

template<typename ColumnType, mdtable_id_t ValueTableId>
struct search_reference_value<ColumnType, cursor<ValueTableId>> : search_reference_tables<ColumnType, ValueTableId> {};

template<typename ColumnType, mdtable_id_t... ValueTableIds>
struct search_reference_value<ColumnType, raw_cursor<ValueTableIds...>> : search_reference_tables<ColumnType, ValueTableIds...> {};

template<typename ColumnType, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct search_reference_value<ColumnType, indirect_cursor<DirectTableId, IndirectTableId>>
    : search_reference_tables<ColumnType, DirectTableId> {};

template<mdtable_id_t... TableIds>
struct column_reference_codec
{
    static_assert(sizeof...(TableIds) > 1);
    static_assert(((TableIds == mdtid_Unused || (TableIds >= mdtid_First && TableIds < mdtid_End)) && ...));

    static constexpr mdtable_id_t tables[] = {TableIds...};
    // TODO: Use std::countl_zero to compute the tag width once C++20 is required.
    static constexpr uint32_t tag_bits = [] {
        uint32_t bits = 0;
        for (size_t count = sizeof...(TableIds) - 1; count != 0; count >>= 1)
            ++bits;
        return bits;
    }();

    static bool decode(uint32_t raw, mdtable_id_t& table, uint32_t& row) noexcept
    {
        uint32_t tag = raw & ((uint32_t{1} << tag_bits) - 1);
        if (tag >= sizeof...(TableIds) || tables[tag] == mdtid_Unused)
            return false;
        table = tables[tag];
        row = raw >> tag_bits;
        return true;
    }

    static bool encode(mdToken value, uint32_t& raw) noexcept
    {
        auto table = static_cast<mdtable_id_t>(ExtractTokenType(value));
        for (uint32_t tag = 0; tag < sizeof...(TableIds); ++tag)
        {
            if (tables[tag] == table)
            {
                raw = ((RidFromToken)(value) << tag_bits) | tag;
                return true;
            }
        }
        return false;
    }
};

template<mdtable_id_t TableId>
struct column_reference_codec<TableId>
{
    static_assert(TableId >= mdtid_First && TableId < mdtid_End);

    static bool decode(uint32_t raw, mdtable_id_t& table, uint32_t& row) noexcept
    {
        table = TableId;
        row = (RidFromToken)(raw);
        return true;
    }

    static bool encode(mdToken value, uint32_t& raw) noexcept
    {
        if (ExtractTokenType(value) != TableId)
            return false;
        raw = (RidFromToken)(value);
        return true;
    }
};

}

template<mdtable_id_t TableId, typename ColumnType, typename ValuePolicy = detail::stored_column_value>
struct column_iterator;

template<typename T> struct cursor_tableid;
template<mdtable_id_t T> struct cursor_tableid<cursor<T>> { static constexpr mdtable_id_t value = T; };
template<typename T> inline constexpr mdtable_id_t cursor_tableid_v = cursor_tableid<T>::value;

namespace columns
{
inline constexpr column<mdtid_Module, column_type_constant> Module_Generation{ mdtModule_Generation };
inline constexpr column<mdtid_Module, column_type_string> Module_Name{ mdtModule_Name };
inline constexpr column<mdtid_Module, column_type_guid> Module_Mvid{ mdtModule_Mvid };
inline constexpr column<mdtid_Module, column_type_guid> Module_EncId{ mdtModule_EncId };
inline constexpr column<mdtid_Module, column_type_guid> Module_EncBaseId{ mdtModule_EncBaseId };

inline constexpr column<mdtid_TypeRef, raw_cursor<mdtid_Module, mdtid_ModuleRef, mdtid_AssemblyRef, mdtid_TypeRef>> TypeRef_ResolutionScope{ mdtTypeRef_ResolutionScope };
inline constexpr column<mdtid_TypeRef, column_type_string> TypeRef_TypeName{ mdtTypeRef_TypeName };
inline constexpr column<mdtid_TypeRef, column_type_string> TypeRef_TypeNamespace{ mdtTypeRef_TypeNamespace };

inline constexpr column<mdtid_TypeDef, column_type_constant> TypeDef_Flags{ mdtTypeDef_Flags };
inline constexpr column<mdtid_TypeDef, column_type_string> TypeDef_TypeName{ mdtTypeDef_TypeName };
inline constexpr column<mdtid_TypeDef, column_type_string> TypeDef_TypeNamespace{ mdtTypeDef_TypeNamespace };
inline constexpr column<mdtid_TypeDef, raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> TypeDef_Extends{ mdtTypeDef_Extends };
inline constexpr column<mdtid_TypeDef, indirect_cursor<mdtid_Field, mdtid_FieldPtr>> TypeDef_FieldList{ mdtTypeDef_FieldList };
inline constexpr column<mdtid_TypeDef, indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>> TypeDef_MethodList{ mdtTypeDef_MethodList };

inline constexpr column<mdtid_Field, column_type_constant> Field_Flags{ mdtField_Flags };
inline constexpr column<mdtid_Field, column_type_string> Field_Name{ mdtField_Name };
inline constexpr column<mdtid_Field, column_type_blob> Field_Signature{ mdtField_Signature };

inline constexpr column<mdtid_MethodDef, column_type_constant> MethodDef_Rva{ mdtMethodDef_Rva };
inline constexpr column<mdtid_MethodDef, column_type_constant> MethodDef_ImplFlags{ mdtMethodDef_ImplFlags };
inline constexpr column<mdtid_MethodDef, column_type_constant> MethodDef_Flags{ mdtMethodDef_Flags };
inline constexpr column<mdtid_MethodDef, column_type_string> MethodDef_Name{ mdtMethodDef_Name };
inline constexpr column<mdtid_MethodDef, column_type_blob> MethodDef_Signature{ mdtMethodDef_Signature };
inline constexpr column<mdtid_MethodDef, indirect_cursor<mdtid_Param, mdtid_ParamPtr>> MethodDef_ParamList{ mdtMethodDef_ParamList };

inline constexpr column<mdtid_Param, column_type_constant> Param_Flags{ mdtParam_Flags };
inline constexpr column<mdtid_Param, column_type_constant> Param_Sequence{ mdtParam_Sequence };
inline constexpr column<mdtid_Param, column_type_string> Param_Name{ mdtParam_Name };

inline constexpr column<mdtid_InterfaceImpl, raw_cursor<mdtid_TypeDef>> InterfaceImpl_Class{ mdtInterfaceImpl_Class };
inline constexpr column<mdtid_InterfaceImpl, raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> InterfaceImpl_Interface{ mdtInterfaceImpl_Interface };

inline constexpr column<mdtid_MemberRef, raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_ModuleRef, mdtid_MethodDef, mdtid_TypeSpec>> MemberRef_Class{ mdtMemberRef_Class };
inline constexpr column<mdtid_MemberRef, column_type_string> MemberRef_Name{ mdtMemberRef_Name };
inline constexpr column<mdtid_MemberRef, column_type_blob> MemberRef_Signature{ mdtMemberRef_Signature };

inline constexpr column<mdtid_Constant, column_type_constant> Constant_Type{ mdtConstant_Type };
inline constexpr column<mdtid_Constant, raw_cursor<mdtid_Field, mdtid_Param, mdtid_Property>> Constant_Parent{ mdtConstant_Parent };
inline constexpr column<mdtid_Constant, column_type_blob> Constant_Value{ mdtConstant_Value };

inline constexpr column<mdtid_CustomAttribute, raw_cursor<mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param, mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property, mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec, mdtid_Assembly, mdtid_AssemblyRef, mdtid_File, mdtid_ExportedType, mdtid_ManifestResource, mdtid_GenericParam, mdtid_GenericParamConstraint, mdtid_MethodSpec>> CustomAttribute_Parent{ mdtCustomAttribute_Parent };
inline constexpr column<mdtid_CustomAttribute, raw_cursor<mdtid_Unused, mdtid_Unused, mdtid_MethodDef, mdtid_MemberRef, mdtid_Unused>> CustomAttribute_Type{ mdtCustomAttribute_Type };
inline constexpr column<mdtid_CustomAttribute, column_type_blob> CustomAttribute_Value{ mdtCustomAttribute_Value };

inline constexpr column<mdtid_FieldMarshal, raw_cursor<mdtid_Field, mdtid_Param>> FieldMarshal_Parent{ mdtFieldMarshal_Parent };
inline constexpr column<mdtid_FieldMarshal, column_type_blob> FieldMarshal_NativeType{ mdtFieldMarshal_NativeType };

inline constexpr column<mdtid_DeclSecurity, column_type_constant> DeclSecurity_Action{ mdtDeclSecurity_Action };
inline constexpr column<mdtid_DeclSecurity, raw_cursor<mdtid_TypeDef, mdtid_MethodDef, mdtid_Assembly>> DeclSecurity_Parent{ mdtDeclSecurity_Parent };
inline constexpr column<mdtid_DeclSecurity, column_type_blob> DeclSecurity_PermissionSet{ mdtDeclSecurity_PermissionSet };

inline constexpr column<mdtid_ClassLayout, column_type_constant> ClassLayout_PackingSize{ mdtClassLayout_PackingSize };
inline constexpr column<mdtid_ClassLayout, column_type_constant> ClassLayout_ClassSize{ mdtClassLayout_ClassSize };
inline constexpr column<mdtid_ClassLayout, raw_cursor<mdtid_TypeDef>> ClassLayout_Parent{ mdtClassLayout_Parent };

inline constexpr column<mdtid_FieldLayout, column_type_constant> FieldLayout_Offset{ mdtFieldLayout_Offset };
inline constexpr column<mdtid_FieldLayout, raw_cursor<mdtid_Field>> FieldLayout_Field{ mdtFieldLayout_Field };

inline constexpr column<mdtid_StandAloneSig, column_type_blob> StandAloneSig_Signature{ mdtStandAloneSig_Signature };

inline constexpr column<mdtid_EventMap, raw_cursor<mdtid_TypeDef>> EventMap_Parent{ mdtEventMap_Parent };
inline constexpr column<mdtid_EventMap, indirect_cursor<mdtid_Event, mdtid_EventPtr>> EventMap_EventList{ mdtEventMap_EventList };

inline constexpr column<mdtid_Event, column_type_constant> Event_EventFlags{ mdtEvent_EventFlags };
inline constexpr column<mdtid_Event, column_type_string> Event_Name{ mdtEvent_Name };
inline constexpr column<mdtid_Event, raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> Event_EventType{ mdtEvent_EventType };

inline constexpr column<mdtid_PropertyMap, raw_cursor<mdtid_TypeDef>> PropertyMap_Parent{ mdtPropertyMap_Parent };
inline constexpr column<mdtid_PropertyMap, indirect_cursor<mdtid_Property, mdtid_PropertyPtr>> PropertyMap_PropertyList{ mdtPropertyMap_PropertyList };

inline constexpr column<mdtid_Property, column_type_constant> Property_Flags{ mdtProperty_Flags };
inline constexpr column<mdtid_Property, column_type_string> Property_Name{ mdtProperty_Name };
inline constexpr column<mdtid_Property, column_type_blob> Property_Type{ mdtProperty_Type };

inline constexpr column<mdtid_MethodSemantics, column_type_constant> MethodSemantics_Semantics{ mdtMethodSemantics_Semantics };
inline constexpr column<mdtid_MethodSemantics, raw_cursor<mdtid_MethodDef>> MethodSemantics_Method{ mdtMethodSemantics_Method };
inline constexpr column<mdtid_MethodSemantics, raw_cursor<mdtid_Event, mdtid_Property>> MethodSemantics_Association{ mdtMethodSemantics_Association };

inline constexpr column<mdtid_MethodImpl, raw_cursor<mdtid_TypeDef>> MethodImpl_Class{ mdtMethodImpl_Class };
inline constexpr column<mdtid_MethodImpl, raw_cursor<mdtid_MethodDef, mdtid_MemberRef>> MethodImpl_MethodBody{ mdtMethodImpl_MethodBody };
inline constexpr column<mdtid_MethodImpl, raw_cursor<mdtid_MethodDef, mdtid_MemberRef>> MethodImpl_MethodDeclaration{ mdtMethodImpl_MethodDeclaration };

inline constexpr column<mdtid_ModuleRef, column_type_string> ModuleRef_Name{ mdtModuleRef_Name };

inline constexpr column<mdtid_TypeSpec, column_type_blob> TypeSpec_Signature{ mdtTypeSpec_Signature };

inline constexpr column<mdtid_ImplMap, column_type_constant> ImplMap_MappingFlags{ mdtImplMap_MappingFlags };
inline constexpr column<mdtid_ImplMap, raw_cursor<mdtid_Field, mdtid_MethodDef>> ImplMap_MemberForwarded{ mdtImplMap_MemberForwarded };
inline constexpr column<mdtid_ImplMap, column_type_string> ImplMap_ImportName{ mdtImplMap_ImportName };
inline constexpr column<mdtid_ImplMap, raw_cursor<mdtid_ModuleRef>> ImplMap_ImportScope{ mdtImplMap_ImportScope };

inline constexpr column<mdtid_FieldRva, column_type_constant> FieldRva_Rva{ mdtFieldRva_Rva };
inline constexpr column<mdtid_FieldRva, raw_cursor<mdtid_Field>> FieldRva_Field{ mdtFieldRva_Field };

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
inline constexpr column<mdtid_AssemblyRefProcessor, raw_cursor<mdtid_AssemblyRef>> AssemblyRefProcessor_AssemblyRef{ static_cast<col_index_t>(1) };

inline constexpr column<mdtid_AssemblyRefOS, column_type_constant> AssemblyRefOS_OSPlatformId{ static_cast<col_index_t>(0) };
inline constexpr column<mdtid_AssemblyRefOS, column_type_constant> AssemblyRefOS_OSMajorVersion{ static_cast<col_index_t>(1) };
inline constexpr column<mdtid_AssemblyRefOS, column_type_constant> AssemblyRefOS_OSMinorVersion{ static_cast<col_index_t>(2) };
inline constexpr column<mdtid_AssemblyRefOS, raw_cursor<mdtid_AssemblyRef>> AssemblyRefOS_AssemblyRef{ static_cast<col_index_t>(3) };

inline constexpr column<mdtid_File, column_type_constant> File_Flags{ mdtFile_Flags };
inline constexpr column<mdtid_File, column_type_string> File_Name{ mdtFile_Name };
inline constexpr column<mdtid_File, column_type_blob> File_HashValue{ mdtFile_HashValue };

inline constexpr column<mdtid_ExportedType, column_type_constant> ExportedType_Flags{ mdtExportedType_Flags };
inline constexpr column<mdtid_ExportedType, column_type_constant> ExportedType_TypeDefId{ mdtExportedType_TypeDefId };
inline constexpr column<mdtid_ExportedType, column_type_string> ExportedType_TypeName{ mdtExportedType_TypeName };
inline constexpr column<mdtid_ExportedType, column_type_string> ExportedType_TypeNamespace{ mdtExportedType_TypeNamespace };
inline constexpr column<mdtid_ExportedType, raw_cursor<mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType>> ExportedType_Implementation{ mdtExportedType_Implementation };

inline constexpr column<mdtid_ManifestResource, column_type_constant> ManifestResource_Offset{ mdtManifestResource_Offset };
inline constexpr column<mdtid_ManifestResource, column_type_constant> ManifestResource_Flags{ mdtManifestResource_Flags };
inline constexpr column<mdtid_ManifestResource, column_type_string> ManifestResource_Name{ mdtManifestResource_Name };
inline constexpr column<mdtid_ManifestResource, raw_cursor<mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType>> ManifestResource_Implementation{ mdtManifestResource_Implementation };

inline constexpr column<mdtid_NestedClass, raw_cursor<mdtid_TypeDef>> NestedClass_NestedClass{ mdtNestedClass_NestedClass };
inline constexpr column<mdtid_NestedClass, raw_cursor<mdtid_TypeDef>> NestedClass_EnclosingClass{ mdtNestedClass_EnclosingClass };

inline constexpr column<mdtid_GenericParam, column_type_constant> GenericParam_Number{ mdtGenericParam_Number };
inline constexpr column<mdtid_GenericParam, column_type_constant> GenericParam_Flags{ mdtGenericParam_Flags };
inline constexpr column<mdtid_GenericParam, raw_cursor<mdtid_TypeDef, mdtid_MethodDef>> GenericParam_Owner{ mdtGenericParam_Owner };
inline constexpr column<mdtid_GenericParam, column_type_string> GenericParam_Name{ mdtGenericParam_Name };

inline constexpr column<mdtid_MethodSpec, raw_cursor<mdtid_MethodDef, mdtid_MemberRef>> MethodSpec_Method{ mdtMethodSpec_Method };
inline constexpr column<mdtid_MethodSpec, column_type_blob> MethodSpec_Instantiation{ mdtMethodSpec_Instantiation };

inline constexpr column<mdtid_GenericParamConstraint, raw_cursor<mdtid_GenericParam>> GenericParamConstraint_Owner{ mdtGenericParamConstraint_Owner };
inline constexpr column<mdtid_GenericParamConstraint, raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>> GenericParamConstraint_Constraint{ mdtGenericParamConstraint_Constraint };

#ifdef DNMD_PORTABLE_PDB
inline constexpr column<mdtid_Document, column_type_blob> Document_Name{ mdtDocument_Name };
inline constexpr column<mdtid_Document, column_type_guid> Document_HashAlgorithm{ mdtDocument_HashAlgorithm };
inline constexpr column<mdtid_Document, column_type_blob> Document_Hash{ mdtDocument_Hash };
inline constexpr column<mdtid_Document, column_type_guid> Document_Language{ mdtDocument_Language };

inline constexpr column<mdtid_MethodDebugInformation, raw_cursor<mdtid_Document>> MethodDebugInformation_Document{ mdtMethodDebugInformation_Document };
inline constexpr column<mdtid_MethodDebugInformation, column_type_blob> MethodDebugInformation_SequencePoints{ mdtMethodDebugInformation_SequencePoints };

inline constexpr column<mdtid_LocalScope, raw_cursor<mdtid_MethodDef>> LocalScope_Method{ mdtLocalScope_Method };
inline constexpr column<mdtid_LocalScope, raw_cursor<mdtid_ImportScope>> LocalScope_ImportScope{ mdtLocalScope_ImportScope };
inline constexpr column<mdtid_LocalScope, raw_cursor<mdtid_LocalVariable>> LocalScope_VariableList{ mdtLocalScope_VariableList };
inline constexpr column<mdtid_LocalScope, raw_cursor<mdtid_LocalConstant>> LocalScope_ConstantList{ mdtLocalScope_ConstantList };
inline constexpr column<mdtid_LocalScope, column_type_constant> LocalScope_StartOffset{ mdtLocalScope_StartOffset };
inline constexpr column<mdtid_LocalScope, column_type_constant> LocalScope_Length{ mdtLocalScope_Length };

inline constexpr column<mdtid_LocalVariable, column_type_constant> LocalVariable_Attributes{ mdtLocalVariable_Attributes };
inline constexpr column<mdtid_LocalVariable, column_type_constant> LocalVariable_Index{ mdtLocalVariable_Index };
inline constexpr column<mdtid_LocalVariable, column_type_string> LocalVariable_Name{ mdtLocalVariable_Name };

inline constexpr column<mdtid_LocalConstant, column_type_string> LocalConstant_Name{ mdtLocalConstant_Name };
inline constexpr column<mdtid_LocalConstant, column_type_blob> LocalConstant_Signature{ mdtLocalConstant_Signature };

inline constexpr column<mdtid_ImportScope, raw_cursor<mdtid_ImportScope>> ImportScope_Parent{ mdtImportScope_Parent };
inline constexpr column<mdtid_ImportScope, column_type_blob> ImportScope_Imports{ mdtImportScope_Imports };

inline constexpr column<mdtid_StateMachineMethod, raw_cursor<mdtid_MethodDef>> StateMachineMethod_MoveNextMethod{ mdtStateMachineMethod_MoveNextMethod };
inline constexpr column<mdtid_StateMachineMethod, raw_cursor<mdtid_MethodDef>> StateMachineMethod_KickoffMethod{ mdtStateMachineMethod_KickoffMethod };

inline constexpr column<mdtid_CustomDebugInformation, raw_cursor<mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param, mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property, mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec, mdtid_Assembly, mdtid_AssemblyRef, mdtid_File, mdtid_ExportedType, mdtid_ManifestResource, mdtid_GenericParam, mdtid_GenericParamConstraint, mdtid_MethodSpec, mdtid_Document, mdtid_LocalScope, mdtid_LocalVariable, mdtid_LocalConstant, mdtid_ImportScope>> CustomDebugInformation_Parent{ mdtCustomDebugInformation_Parent };
inline constexpr column<mdtid_CustomDebugInformation, column_type_guid> CustomDebugInformation_Kind{ mdtCustomDebugInformation_Kind };
inline constexpr column<mdtid_CustomDebugInformation, column_type_blob> CustomDebugInformation_Value{ mdtCustomDebugInformation_Value };
#endif // DNMD_PORTABLE_PDB
}

} // namespace dnmd

namespace dnmd::detail
{
// std::integer_sequence requires an integral type, so use an enum-typed
// equivalent to preserve table identity throughout deduction and expansion.
template<mdtable_id_t... Tables>
struct table_sequence
{
    using value_type = mdtable_id_t;
    static constexpr size_t size() noexcept { return sizeof...(Tables); }
};

template<mdtable_id_t First, int... Offsets>
constexpr auto make_table_sequence(std::integer_sequence<int, Offsets...>) noexcept
{
    return table_sequence<static_cast<mdtable_id_t>(First + Offsets)...>{};
}

template<mdtable_id_t First, mdtable_id_t Last>
struct table_range
{
    static_assert(First >= mdtid_First && First <= Last && Last <= mdtid_End);
    using type = decltype(make_table_sequence<First>(std::make_integer_sequence<int, Last - First>{}));
};

using all_tables = typename table_range<mdtid_First, mdtid_End>::type;

#ifdef DNMD_PORTABLE_PDB
using non_pdb_tables = typename table_range<mdtid_First, mdtid_FirstPdb>::type;
using pdb_tables = typename table_range<mdtid_FirstPdb, mdtid_End>::type;
#else
using non_pdb_tables = all_tables;
#endif

template<typename Kind>
struct list_traits
{
    static constexpr bool supported = false;
};

template<mdtable_id_t Direct, mdtable_id_t Ptr>
struct list_traits<indirect_cursor<Direct, Ptr>>
{
    static constexpr bool supported = true;
    static constexpr mdtable_id_t direct = Direct;
    static constexpr mdtable_id_t pointer = Ptr;
    using position_type = raw_cursor<Direct, Ptr>;
};

template<mdtable_id_t Direct>
struct list_traits<raw_cursor<Direct>>
{
    static constexpr bool supported = true;
    static constexpr mdtable_id_t direct = Direct;
    static constexpr mdtable_id_t pointer = mdtid_Unused;
    using position_type = raw_cursor<Direct>;
};

// Invoke a variadic callable with selected column or sort-key metadata.
// The callable's result and the tuple elements' value categories are preserved.
template<mdtable_id_t Table, typename Callable, typename Columns>
constexpr decltype(auto) for_each_column(Callable&& callable, Columns&& columns)
{
    // Heterogeneous descriptors need a typed argument pack so callers can choose
    // their own &&, ||, or comma fold without introducing runtime dispatch.
    return std::apply(std::forward<Callable>(callable), std::forward<Columns>(columns));
}

constexpr auto all_list_columns() noexcept
{
    using namespace columns;
    return std::tuple{TypeDef_FieldList, TypeDef_MethodList, MethodDef_ParamList,
        EventMap_EventList, PropertyMap_PropertyList
#ifdef DNMD_PORTABLE_PDB
        , LocalScope_VariableList, LocalScope_ConstantList
#endif
    };
}

template<typename Predicate>
constexpr auto matching_list_columns(Predicate) noexcept
{
    // Selection is encoded in the predicate's bool_constant result type.
    auto select = [](auto col) {
        if constexpr (decltype(std::declval<Predicate>()(col))::value)
            return std::tuple{col};
        else
            return std::tuple{};
    };
    return for_each_column<mdtid_Unused>([&](auto... columns) {
        return std::tuple_cat(select(columns)...);
    }, all_list_columns());
}

template<mdtable_id_t Table>
constexpr auto list_columns() noexcept
{
    return matching_list_columns([](auto col) {
        return std::bool_constant<decltype(col)::table_id == Table>{};
    });
}

template<mdtable_id_t Element>
constexpr auto element_list_columns() noexcept
{
    return matching_list_columns([](auto col) {
        return std::bool_constant<list_traits<typename decltype(col)::value_type>::direct == Element>{};
    });
}

template<mdtable_id_t Ptr>
constexpr auto pointer_list_columns() noexcept
{
    return matching_list_columns([](auto col) {
        return std::bool_constant<list_traits<typename decltype(col)::value_type>::pointer == Ptr>{};
    });
}

template<mdtable_id_t Element>
constexpr auto parent_list_column() noexcept
{
    constexpr auto matches = element_list_columns<Element>();
    static_assert(std::tuple_size_v<decltype(matches)> == 1, "The row must belong to exactly one list.");
    return std::get<0>(matches);
}

template<mdtable_id_t Owner>
constexpr auto list_owner_parent_column() noexcept
{
    if constexpr (Owner == mdtid_EventMap)
        return columns::EventMap_Parent;
    else
    {
        static_assert(Owner == mdtid_PropertyMap, "The list owner must use a parent map.");
        return columns::PropertyMap_Parent;
    }
}

template<mdtable_id_t Element>
constexpr mdtable_id_t parent_table_id() noexcept
{
    if constexpr (std::tuple_size_v<decltype(element_list_columns<Element>())> == 0)
        return mdtid_Unused;
    else
    {
        constexpr auto owner = decltype(parent_list_column<Element>())::table_id;
        // Event/property map rows own the list; their Parent column identifies
        // the logical parent exposed by the cursor API.
        if constexpr (owner == mdtid_EventMap || owner == mdtid_PropertyMap)
            return list_traits<typename decltype(list_owner_parent_column<owner>())::value_type>::direct;
        else
            return owner;
    }
}

template<mdtable_id_t Table>
constexpr bool is_pointer_table() noexcept
{
    return Table != mdtid_Unused && std::tuple_size_v<decltype(pointer_list_columns<Table>())> == 1;
}

template<mdtable_id_t Ptr>
constexpr auto pointer_column() noexcept
{
    using matches = decltype(pointer_list_columns<Ptr>());
    static_assert(is_pointer_table<Ptr>(), "The table must be an indirection table.");
    constexpr auto direct = list_traits<typename std::tuple_element_t<0, matches>::value_type>::direct;
    return column<Ptr, raw_cursor<direct>>{static_cast<col_index_t>(0)};
}

constexpr auto make_pointer_table_ids() noexcept
{
    std::array<mdtable_id_t, mdtid_End> tables{};
    for (auto& table : tables)
        table = mdtid_Unused;
    for_each_column<mdtid_Unused>([&](auto... columns) {
        ((tables[list_traits<typename decltype(columns)::value_type>::direct] =
            list_traits<typename decltype(columns)::value_type>::pointer), ...);
    }, all_list_columns());
    return tables;
}

inline constexpr auto pointer_table_ids = make_pointer_table_ids();

template<mdtable_id_t Element>
constexpr mdtable_id_t pointer_table_id() noexcept
{
    if constexpr (Element >= mdtid_First && Element < mdtid_End)
        return pointer_table_ids[Element];
    else
        return mdtid_Unused;
}

constexpr mdtable_id_t pointer_table_id(mdtable_id_t table) noexcept
{
    assert(table >= mdtid_First && table < mdtid_End);
    return pointer_table_ids[table];
}

template<mdtable_id_t Direct, mdtable_id_t Ptr>
constexpr mdtable_id_t select_list_target(bool indirect) noexcept
{
    static_assert(Direct >= mdtid_First && Direct < mdtid_End);
    static_assert(Ptr >= mdtid_First && Ptr < mdtid_End);
    return indirect ? Ptr : Direct;
}

template<mdtable_id_t Direct, mdtable_id_t Ptr>
inline mdtable_id_t list_target_table(mdcxt_t const* context) noexcept
{
    return select_list_target<Direct, Ptr>(context->tables[Direct].has_indirect_table);
}

template<mdtable_id_t Owner, typename Kind>
inline mdtable_id_t list_target_table(mdcxt_t const* context, column<Owner, Kind>) noexcept
{
    using traits = list_traits<Kind>;
    if constexpr (traits::pointer == mdtid_Unused)
        return traits::direct;
    else
        return list_target_table<traits::direct, traits::pointer>(context);
}

template<mdtable_id_t Owner, typename Kind>
inline uint32_t list_end_row(mdtable_t const* source, column<Owner, Kind> col) noexcept
{
    return source->cxt->tables[list_target_table(source->cxt, col)].row_count + 1;
}

template<mdtable_id_t Table, mdtable_id_t... Targets>
constexpr bool references_table(mdtable_t const*, column<Table, raw_cursor<Targets...>>, mdtable_id_t target) noexcept
{
    return ((target == Targets) || ...);
}

template<mdtable_id_t Table, mdtable_id_t Direct, mdtable_id_t Ptr>
inline bool references_table(mdtable_t const* table, column<Table, indirect_cursor<Direct, Ptr>>, mdtable_id_t target) noexcept
{
    return list_target_table<Direct, Ptr>(table->cxt) == target;
}

template<typename Column>
struct sort_key final
{
    Column column;
    bool descending;

    constexpr sort_key(Column col, bool desc = false) noexcept : column{col}, descending{desc} {}
};

template<mdtable_id_t TableId>
constexpr auto table_sort_keys() noexcept
{
    using namespace columns;
    if constexpr (TableId == mdtid_ClassLayout) return std::tuple{sort_key{ClassLayout_Parent}};
    else if constexpr (TableId == mdtid_Constant) return std::tuple{sort_key{Constant_Parent}};
    else if constexpr (TableId == mdtid_CustomAttribute) return std::tuple{sort_key{CustomAttribute_Parent}};
    else if constexpr (TableId == mdtid_DeclSecurity) return std::tuple{sort_key{DeclSecurity_Parent}};
    else if constexpr (TableId == mdtid_FieldLayout) return std::tuple{sort_key{FieldLayout_Field}};
    else if constexpr (TableId == mdtid_FieldMarshal) return std::tuple{sort_key{FieldMarshal_Parent}};
    else if constexpr (TableId == mdtid_FieldRva) return std::tuple{sort_key{FieldRva_Field}};
    else if constexpr (TableId == mdtid_GenericParam)
        return std::tuple{sort_key{GenericParam_Owner}, sort_key{GenericParam_Number}};
    else if constexpr (TableId == mdtid_GenericParamConstraint) return std::tuple{sort_key{GenericParamConstraint_Owner}};
    else if constexpr (TableId == mdtid_ImplMap) return std::tuple{sort_key{ImplMap_MemberForwarded}};
    else if constexpr (TableId == mdtid_InterfaceImpl) return std::tuple{sort_key{InterfaceImpl_Class}};
    else if constexpr (TableId == mdtid_MethodImpl) return std::tuple{sort_key{MethodImpl_Class}};
    else if constexpr (TableId == mdtid_MethodSemantics) return std::tuple{sort_key{MethodSemantics_Association}};
    else if constexpr (TableId == mdtid_NestedClass) return std::tuple{sort_key{NestedClass_NestedClass}};
#ifdef DNMD_PORTABLE_PDB
    else if constexpr (TableId == mdtid_LocalScope)
        return std::tuple{sort_key{LocalScope_Method}, sort_key{LocalScope_StartOffset}, sort_key{LocalScope_Length, true}};
    else if constexpr (TableId == mdtid_StateMachineMethod) return std::tuple{sort_key{StateMachineMethod_MoveNextMethod}};
    else if constexpr (TableId == mdtid_CustomDebugInformation) return std::tuple{sort_key{CustomDebugInformation_Parent}};
#endif
    else return std::tuple{};
}

template<mdtable_id_t Table>
constexpr auto table_columns() noexcept
{
    using namespace columns;
    if constexpr (Table == mdtid_Module)
        return std::tuple{Module_Generation, Module_Name, Module_Mvid, Module_EncId, Module_EncBaseId};
    else if constexpr (Table == mdtid_TypeRef)
        return std::tuple{TypeRef_ResolutionScope, TypeRef_TypeName, TypeRef_TypeNamespace};
    else if constexpr (Table == mdtid_TypeDef)
        return std::tuple{TypeDef_Flags, TypeDef_TypeName, TypeDef_TypeNamespace,
            TypeDef_Extends, TypeDef_FieldList, TypeDef_MethodList};
    else if constexpr (is_pointer_table<Table>())
        return std::tuple{pointer_column<Table>()};
    else if constexpr (Table == mdtid_Field)
        return std::tuple{Field_Flags, Field_Name, Field_Signature};
    else if constexpr (Table == mdtid_MethodDef)
        return std::tuple{MethodDef_Rva, MethodDef_ImplFlags, MethodDef_Flags,
            MethodDef_Name, MethodDef_Signature, MethodDef_ParamList};
    else if constexpr (Table == mdtid_Param)
        return std::tuple{Param_Flags, Param_Sequence, Param_Name};
    else if constexpr (Table == mdtid_InterfaceImpl)
        return std::tuple{InterfaceImpl_Class, InterfaceImpl_Interface};
    else if constexpr (Table == mdtid_MemberRef)
        return std::tuple{MemberRef_Class, MemberRef_Name, MemberRef_Signature};
    else if constexpr (Table == mdtid_Constant)
        return std::tuple{Constant_Type, Constant_Parent, Constant_Value};
    else if constexpr (Table == mdtid_CustomAttribute)
        return std::tuple{CustomAttribute_Parent, CustomAttribute_Type, CustomAttribute_Value};
    else if constexpr (Table == mdtid_FieldMarshal)
        return std::tuple{FieldMarshal_Parent, FieldMarshal_NativeType};
    else if constexpr (Table == mdtid_DeclSecurity)
        return std::tuple{DeclSecurity_Action, DeclSecurity_Parent, DeclSecurity_PermissionSet};
    else if constexpr (Table == mdtid_ClassLayout)
        return std::tuple{ClassLayout_PackingSize, ClassLayout_ClassSize, ClassLayout_Parent};
    else if constexpr (Table == mdtid_FieldLayout)
        return std::tuple{FieldLayout_Offset, FieldLayout_Field};
    else if constexpr (Table == mdtid_StandAloneSig)
        return std::tuple{StandAloneSig_Signature};
    else if constexpr (Table == mdtid_EventMap)
        return std::tuple{EventMap_Parent, EventMap_EventList};
    else if constexpr (Table == mdtid_Event)
        return std::tuple{Event_EventFlags, Event_Name, Event_EventType};
    else if constexpr (Table == mdtid_PropertyMap)
        return std::tuple{PropertyMap_Parent, PropertyMap_PropertyList};
    else if constexpr (Table == mdtid_Property)
        return std::tuple{Property_Flags, Property_Name, Property_Type};
    else if constexpr (Table == mdtid_MethodSemantics)
        return std::tuple{MethodSemantics_Semantics, MethodSemantics_Method, MethodSemantics_Association};
    else if constexpr (Table == mdtid_MethodImpl)
        return std::tuple{MethodImpl_Class, MethodImpl_MethodBody, MethodImpl_MethodDeclaration};
    else if constexpr (Table == mdtid_ModuleRef)
        return std::tuple{ModuleRef_Name};
    else if constexpr (Table == mdtid_TypeSpec)
        return std::tuple{TypeSpec_Signature};
    else if constexpr (Table == mdtid_ImplMap)
        return std::tuple{ImplMap_MappingFlags, ImplMap_MemberForwarded, ImplMap_ImportName, ImplMap_ImportScope};
    else if constexpr (Table == mdtid_FieldRva)
        return std::tuple{FieldRva_Rva, FieldRva_Field};
    else if constexpr (Table == mdtid_ENCLog)
        return std::tuple{ENCLog_Token, ENCLog_Op};
    else if constexpr (Table == mdtid_ENCMap)
        return std::tuple{ENCMap_Token};
    else if constexpr (Table == mdtid_Assembly)
        return std::tuple{Assembly_HashAlgId, Assembly_MajorVersion, Assembly_MinorVersion,
            Assembly_BuildNumber, Assembly_RevisionNumber, Assembly_Flags,
            Assembly_PublicKey, Assembly_Name, Assembly_Culture};
    else if constexpr (Table == mdtid_AssemblyProcessor)
        return std::tuple{AssemblyProcessor_Processor};
    else if constexpr (Table == mdtid_AssemblyOS)
        return std::tuple{AssemblyOS_OSPlatformId, AssemblyOS_OSMajorVersion, AssemblyOS_OSMinorVersion};
    else if constexpr (Table == mdtid_AssemblyRef)
        return std::tuple{AssemblyRef_MajorVersion, AssemblyRef_MinorVersion,
            AssemblyRef_BuildNumber, AssemblyRef_RevisionNumber, AssemblyRef_Flags,
            AssemblyRef_PublicKeyOrToken, AssemblyRef_Name, AssemblyRef_Culture, AssemblyRef_HashValue};
    else if constexpr (Table == mdtid_AssemblyRefProcessor)
        return std::tuple{AssemblyRefProcessor_Processor, AssemblyRefProcessor_AssemblyRef};
    else if constexpr (Table == mdtid_AssemblyRefOS)
        return std::tuple{AssemblyRefOS_OSPlatformId, AssemblyRefOS_OSMajorVersion,
            AssemblyRefOS_OSMinorVersion, AssemblyRefOS_AssemblyRef};
    else if constexpr (Table == mdtid_File)
        return std::tuple{File_Flags, File_Name, File_HashValue};
    else if constexpr (Table == mdtid_ExportedType)
        return std::tuple{ExportedType_Flags, ExportedType_TypeDefId, ExportedType_TypeName,
            ExportedType_TypeNamespace, ExportedType_Implementation};
    else if constexpr (Table == mdtid_ManifestResource)
        return std::tuple{ManifestResource_Offset, ManifestResource_Flags,
            ManifestResource_Name, ManifestResource_Implementation};
    else if constexpr (Table == mdtid_NestedClass)
        return std::tuple{NestedClass_NestedClass, NestedClass_EnclosingClass};
    else if constexpr (Table == mdtid_GenericParam)
        return std::tuple{GenericParam_Number, GenericParam_Flags, GenericParam_Owner, GenericParam_Name};
    else if constexpr (Table == mdtid_MethodSpec)
        return std::tuple{MethodSpec_Method, MethodSpec_Instantiation};
    else if constexpr (Table == mdtid_GenericParamConstraint)
        return std::tuple{GenericParamConstraint_Owner, GenericParamConstraint_Constraint};
#ifdef DNMD_PORTABLE_PDB
    else if constexpr (Table == mdtid_Document)
        return std::tuple{Document_Name, Document_HashAlgorithm, Document_Hash, Document_Language};
    else if constexpr (Table == mdtid_MethodDebugInformation)
        return std::tuple{MethodDebugInformation_Document, MethodDebugInformation_SequencePoints};
    else if constexpr (Table == mdtid_LocalScope)
        return std::tuple{LocalScope_Method, LocalScope_ImportScope, LocalScope_VariableList,
            LocalScope_ConstantList, LocalScope_StartOffset, LocalScope_Length};
    else if constexpr (Table == mdtid_LocalVariable)
        return std::tuple{LocalVariable_Attributes, LocalVariable_Index, LocalVariable_Name};
    else if constexpr (Table == mdtid_LocalConstant)
        return std::tuple{LocalConstant_Name, LocalConstant_Signature};
    else if constexpr (Table == mdtid_ImportScope)
        return std::tuple{ImportScope_Parent, ImportScope_Imports};
    else if constexpr (Table == mdtid_StateMachineMethod)
        return std::tuple{StateMachineMethod_MoveNextMethod, StateMachineMethod_KickoffMethod};
    else if constexpr (Table == mdtid_CustomDebugInformation)
        return std::tuple{CustomDebugInformation_Parent, CustomDebugInformation_Kind, CustomDebugInformation_Value};
#endif
    else
    {
        static_assert(Table == mdtid_Unused
#ifdef DNMD_PORTABLE_PDB
            || (Table > mdtid_GenericParamConstraint && Table < mdtid_FirstPdb)
#endif
            , "The table has no typed column schema.");
        return std::tuple{};
    }
}

// Invoke a variadic callable once with Table's typed column descriptors in schema order.
// For example: for_each_column<Table>([&](auto... cols) { return (process(cols) && ...); }).
// The callable chooses its fold (&&, ||, or comma) and its result is returned unchanged.
// Empty schemas invoke the callable with no arguments.
// Descriptor arguments are temporary; do not retain references to them after the call.
template<mdtable_id_t Table, typename Callable>
constexpr decltype(auto) for_each_column(Callable&& callable)
{
    // Columns have different C++ types, so an ordinary loop would lose the type
    // information needed to select reader/writer overloads. table_columns builds
    // a heterogeneous tuple; the tuple overload unpacks it into the callable's
    // argument pack, where a fold instantiates operations without runtime dispatch.
    return for_each_column<Table>(std::forward<Callable>(callable), table_columns<Table>());
}

template<mdtable_id_t Table>
constexpr uint8_t constant_column_width(column<Table, column_type_constant> col) noexcept
{
    using namespace columns;
    if constexpr (Table == mdtid_TypeDef || Table == mdtid_FieldLayout || Table == mdtid_FieldRva
        || Table == mdtid_ENCLog || Table == mdtid_ENCMap
        || Table == mdtid_AssemblyProcessor || Table == mdtid_AssemblyOS
        || Table == mdtid_AssemblyRefProcessor || Table == mdtid_AssemblyRefOS
        || Table == mdtid_File || Table == mdtid_ExportedType || Table == mdtid_ManifestResource
#ifdef DNMD_PORTABLE_PDB
        || Table == mdtid_LocalScope
#endif
        )
    {
        (void)col;
        return 4;
    }
    else if constexpr (Table == mdtid_MethodDef)
        return col.column_index == MethodDef_Rva.column_index ? 4 : 2;
    else if constexpr (Table == mdtid_ClassLayout)
        return col.column_index == ClassLayout_ClassSize.column_index ? 4 : 2;
    else if constexpr (Table == mdtid_Assembly)
        return col.column_index == Assembly_HashAlgId.column_index || col.column_index == Assembly_Flags.column_index ? 4 : 2;
    else if constexpr (Table == mdtid_AssemblyRef)
        return col.column_index == AssemblyRef_Flags.column_index ? 4 : 2;
    else
    {
        static_assert(Table == mdtid_Module || Table == mdtid_Field || Table == mdtid_Param
            || Table == mdtid_Constant || Table == mdtid_DeclSecurity || Table == mdtid_Event
            || Table == mdtid_Property || Table == mdtid_MethodSemantics || Table == mdtid_ImplMap
            || Table == mdtid_GenericParam
#ifdef DNMD_PORTABLE_PDB
            || Table == mdtid_LocalVariable
#endif
            , "The scalar column needs a fixed storage width.");
        (void)col;
        return 2;
    }
}

template<typename Kind>
constexpr mdheap_id_t column_heap() noexcept
{
    if constexpr (std::is_same_v<Kind, column_type_string>) return mdhid_String;
    else if constexpr (std::is_same_v<Kind, column_type_blob>) return mdhid_Blob;
    else if constexpr (std::is_same_v<Kind, column_type_guid>) return mdhid_Guid;
    else if constexpr (std::is_same_v<Kind, column_type_userstring>) return mdhid_UserString;
    else return mdhid_None;
}

template<mdtable_id_t Table, mdtable_id_t... Targets, typename RowCounts>
constexpr uint64_t max_reference_value(column<Table, raw_cursor<Targets...>>, RowCounts const& rows) noexcept
{
    if constexpr (sizeof...(Targets) == 1)
        return (uint64_t{rows[Targets]} + ...);
    else
    {
        constexpr auto bits = column_reference_codec<Targets...>::tag_bits;
        uint64_t maximum = 0;
        uint32_t tag = 0;
        auto include = [&](auto target) {
            constexpr auto id = decltype(target)::value;
            if constexpr (id != mdtid_Unused)
            {
                auto encoded = (uint64_t{rows[id]} << bits) | tag;
                if (encoded > maximum)
                    maximum = encoded;
            }
            ++tag;
        };
        (include(std::integral_constant<mdtable_id_t, Targets>{}), ...);
        return maximum;
    }
}

template<mdtable_id_t Table, mdtable_id_t... Targets, typename RowCounts>
constexpr uint64_t max_reference_value(mdtable_t const&, column<Table, raw_cursor<Targets...>> col,
    RowCounts const& rows) noexcept
{
    return max_reference_value(col, rows);
}

template<mdtable_id_t Table, mdtable_id_t Direct, mdtable_id_t Ptr, typename RowCounts>
inline uint64_t max_reference_value(mdtable_t const& table, column<Table, indirect_cursor<Direct, Ptr>>,
    RowCounts const& rows) noexcept
{
    return rows[list_target_table<Direct, Ptr>(table.cxt)];
}

constexpr mdtcol_t reference_width(uint64_t maximum, mdcxt_flag_t flags) noexcept
{
    return static_cast<mdtcol_t>((flags & mdc_minimal_delta) || maximum > UINT16_MAX ? mdtc_b4 : mdtc_b2);
}

template<mdtable_id_t Table, typename Kind, typename RowCounts>
constexpr mdtcol_t initial_column_details(column<Table, Kind> col, RowCounts const&, mdcxt_flag_t flags) noexcept
{
    if constexpr (std::is_same_v<Kind, column_type_constant>)
    {
        (void)flags;
        return static_cast<mdtcol_t>(constant_column_width(col) == 4 ? mdtc_b4 : mdtc_b2);
    }
    else
    {
        static_assert(std::is_same_v<Kind, column_type_string> || std::is_same_v<Kind, column_type_guid>
            || std::is_same_v<Kind, column_type_blob>);
        (void)col;
        constexpr auto large = std::is_same_v<Kind, column_type_string> ? mdc_large_string_heap
            : std::is_same_v<Kind, column_type_guid> ? mdc_large_guid_heap : mdc_large_blob_heap;
        return static_cast<mdtcol_t>(flags & large ? mdtc_b4 : mdtc_b2);
    }
}

template<mdtable_id_t Table, mdtable_id_t... Targets, typename RowCounts>
constexpr mdtcol_t initial_column_details(column<Table, raw_cursor<Targets...>> col,
    RowCounts const& rows, mdcxt_flag_t flags) noexcept
{
    return reference_width(max_reference_value(col, rows), flags);
}

template<mdtable_id_t Table, mdtable_id_t Direct, mdtable_id_t Ptr, typename RowCounts>
constexpr mdtcol_t initial_column_details(column<Table, indirect_cursor<Direct, Ptr>>,
    RowCounts const& rows, mdcxt_flag_t flags) noexcept
{
    return reference_width(rows[select_list_target<Direct, Ptr>(rows[Ptr] != 0)], flags);
}

// Row counts include every target table (and PDB external counts when loading).
// This prepares schema only: context ownership and storage state are unchanged.
template<mdtable_id_t Table, typename RowCounts>
inline bool initialize_table_details(RowCounts const& rows, mdcxt_flag_t flags,
    bool sorted, mdtable_t& table) noexcept
{
    constexpr auto count = std::tuple_size_v<decltype(table_columns<Table>())>;
    if constexpr (count == 0)
    {
        (void)rows;
        (void)flags;
        (void)sorted;
        (void)table;
        assert(!"Unknown metadata table ID");
        return false;
    }
    else
    {
        static_assert(count * 4 <= UINT8_MAX);
        assert(rows[Table] != 0 && table.column_details != nullptr);
        if (rows[Table] == 0)
            return false;
        table.row_count = rows[Table];
        table.is_sorted = sorted;
        table.table_id = static_cast<uint8_t>(Table);
        if constexpr (pointer_table_id<Table>() != mdtid_Unused)
            table.has_indirect_table = rows[pointer_table_id<Table>()] != 0;
        else
            table.has_indirect_table = false;
        uint32_t size = 0;
        auto append = [&](auto col) {
            auto details = initial_column_details(col, rows, flags);
            table.column_details[static_cast<uint8_t>(col.column_index)] =
                static_cast<mdtcol_t>(details | (InsertOffset)(size));
            size += details & mdtc_b4 ? 4u : 2u;
        };
        for_each_column<Table>([&](auto... columns) {
            (append(columns), ...);
        });
        table.column_count = static_cast<uint8_t>(count);
        table.row_size_bytes = static_cast<uint8_t>(size);
        return true;
    }
}

template<mdtable_id_t Table, typename ColumnType>
constexpr bool is_list_column(column<Table, ColumnType> col) noexcept
{
    return for_each_column<Table>([col](auto... lists) {
        (void)col;
        return ((col.column_index == lists.column_index) || ...);
    }, list_columns<Table>());
}
}

#endif // _SRC_INC_DNMD_SCHEMA_HPP_
