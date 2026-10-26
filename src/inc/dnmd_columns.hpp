#ifndef _SRC_INC_DNMD_COLUMNS_HPP_
#define _SRC_INC_DNMD_COLUMNS_HPP_

#include "dnmd.h"
#include <cassert>
#include <tuple>
#include <type_traits>

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

template<mdtable_id_t TableId>
constexpr mdtable_id_t parent_table_id() noexcept
{
    if constexpr (TableId == mdtid_Field || TableId == mdtid_MethodDef
        || TableId == mdtid_Event || TableId == mdtid_Property)
        return mdtid_TypeDef;
    else if constexpr (TableId == mdtid_Param)
        return mdtid_MethodDef;
#ifdef DNMD_PORTABLE_PDB
    else if constexpr (TableId == mdtid_LocalVariable || TableId == mdtid_LocalConstant)
        return mdtid_LocalScope;
#endif
    else
        return mdtid_Unused;
}

template<mdtable_id_t TableId>
constexpr mdtable_id_t pointer_table_id() noexcept
{
    if constexpr (TableId == mdtid_Field) return mdtid_FieldPtr;
    else if constexpr (TableId == mdtid_MethodDef) return mdtid_MethodPtr;
    else if constexpr (TableId == mdtid_Param) return mdtid_ParamPtr;
    else if constexpr (TableId == mdtid_Event) return mdtid_EventPtr;
    else if constexpr (TableId == mdtid_Property) return mdtid_PropertyPtr;
    else return mdtid_Unused;
}

template<typename ColumnType>
struct searchable_column : std::false_type {};

template<>
struct searchable_column<column_type_constant> : std::true_type {};

template<mdtable_id_t... TableIds>
struct searchable_column<raw_cursor<TableIds...>> : std::true_type {};

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
struct searchable_column<indirect_cursor<DirectTableId, IndirectTableId>> : std::true_type {};

template<mdtable_id_t TableId>
constexpr auto pointer_column() noexcept
{
    if constexpr (TableId == mdtid_FieldPtr)
        return column<mdtid_FieldPtr, raw_cursor<mdtid_Field>>{mdtFieldPtr_Field};
    else if constexpr (TableId == mdtid_MethodPtr)
        return column<mdtid_MethodPtr, raw_cursor<mdtid_MethodDef>>{mdtMethodPtr_Method};
    else if constexpr (TableId == mdtid_ParamPtr)
        return column<mdtid_ParamPtr, raw_cursor<mdtid_Param>>{mdtParamPtr_Param};
    else if constexpr (TableId == mdtid_EventPtr)
        return column<mdtid_EventPtr, raw_cursor<mdtid_Event>>{mdtEventPtr_Event};
    else
    {
        static_assert(TableId == mdtid_PropertyPtr, "The table must be an indirection table.");
        return column<mdtid_PropertyPtr, raw_cursor<mdtid_Property>>{mdtPropertyPtr_Property};
    }
}

// Parentheses avoid expanding corhdr.h's function-like macro.
inline uint32_t (RidFromToken)(uint64_t tk) noexcept
{
    return static_cast<uint32_t>(tk) & 0x00ffffff;
}

template<mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline mdtable_id_t list_target_table(mdtable_t const* source) noexcept
{
    static_assert(DirectTableId >= mdtid_First && DirectTableId < mdtid_End);
    static_assert(IndirectTableId >= mdtid_First && IndirectTableId < mdtid_End);
    return source->cxt->tables[DirectTableId].has_indirect_table ? IndirectTableId : DirectTableId;
}

template<mdtable_id_t TableId, mdtable_id_t TargetTableId>
inline uint32_t list_end_row(mdtable_t const* source, column<TableId, raw_cursor<TargetTableId>>) noexcept
{
    return source->cxt->tables[TargetTableId].row_count + 1;
}

template<mdtable_id_t TableId, mdtable_id_t DirectTableId, mdtable_id_t IndirectTableId>
inline uint32_t list_end_row(mdtable_t const* source,
    column<TableId, indirect_cursor<DirectTableId, IndirectTableId>>) noexcept
{
    return source->cxt->tables[list_target_table<DirectTableId, IndirectTableId>(source)].row_count + 1;
}

template<mdtable_id_t TableId, mdtable_id_t... TableIds>
inline constexpr bool contains_table_id_v = ((TableId == TableIds) || ...);

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

namespace detail
{
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
}

} // namespace dnmd

#endif // _SRC_INC_DNMD_COLUMNS_HPP_
