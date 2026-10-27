#ifndef _SRC_DNMD_SCHEMA_HPP_
#define _SRC_DNMD_SCHEMA_HPP_

#include "internal.hpp"
#include <utility>

namespace dnmd::detail
{
using table_indices = std::make_index_sequence<mdtid_End>;

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
    else if constexpr (Table == mdtid_FieldPtr || Table == mdtid_MethodPtr
        || Table == mdtid_ParamPtr || Table == mdtid_EventPtr || Table == mdtid_PropertyPtr)
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

template<mdtable_id_t Table, typename ColumnType>
constexpr bool is_list_column(column<Table, ColumnType> col) noexcept
{
    return std::apply([col](auto... lists) {
        (void)col;
        return ((col.column_index == lists.column_index) || ...);
    }, list_columns<Table>());
}
}

#endif // _SRC_DNMD_SCHEMA_HPP_
