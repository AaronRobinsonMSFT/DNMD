#ifndef _SRC_INC_DNMD_SCHEMA_HPP_
#define _SRC_INC_DNMD_SCHEMA_HPP_

#include "dnmd_columns.hpp"
#include <tuple>
#include <utility>

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

template<mdtable_id_t TableId>
constexpr auto list_columns() noexcept
{
    using namespace columns;
    if constexpr (TableId == mdtid_TypeDef) return std::tuple{TypeDef_FieldList, TypeDef_MethodList};
    else if constexpr (TableId == mdtid_MethodDef) return std::tuple{MethodDef_ParamList};
    else if constexpr (TableId == mdtid_EventMap) return std::tuple{EventMap_EventList};
    else if constexpr (TableId == mdtid_PropertyMap) return std::tuple{PropertyMap_PropertyList};
#ifdef DNMD_PORTABLE_PDB
    else if constexpr (TableId == mdtid_LocalScope)
        return std::tuple{LocalScope_VariableList, LocalScope_ConstantList};
#endif
    else return std::tuple{};
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

// Invoke a variadic callable with a selected tuple of Table's column metadata,
// such as list_columns<Table>() or table_sort_keys<Table>(). The callable's
// result and the tuple elements' value categories are preserved.
template<mdtable_id_t Table, typename Callable, typename Columns>
constexpr decltype(auto) for_each_column(Callable&& callable, Columns&& columns)
{
    // Column descriptors and sort-key wrappers have heterogeneous types.
    // Unpack their tuple into a typed argument pack so callers can choose
    // their own &&, ||, or comma fold without introducing runtime dispatch.
    return std::apply(std::forward<Callable>(callable), std::forward<Columns>(columns));
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
