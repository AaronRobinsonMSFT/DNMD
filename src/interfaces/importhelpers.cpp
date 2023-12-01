#include "importhelpers.hpp"
#include <cassert>
#include <stack>

#define RETURN_IF_FAILED(exp) \
{ \
    hr = (exp); \
    if (FAILED(hr)) \
    { \
        return hr; \
    } \
}

namespace
{
    HRESULT GetMvid(mdhandle_t image, mdguid_t* mvid)
    {
        mdguid_t mvid;
        mdcursor_t c;
        uint32_t count;
        if (!md_create_cursor(image, mdtid_Module, &c, &count))
            return CLDB_E_FILE_CORRUPT;
        
        if (1 != md_get_column_value_as_guid(c, mdtModule_Mvid, 1, mvid))
            return CLDB_E_FILE_CORRUPT;
        
        return S_OK;
    }
}

HRESULT ImportReferenceToTypeDef(
    mdcursor_t sourceTypeDef,
    mdhandle_t sourceAssembly,
    span<const uint8_t> sourceAssemblyHash,
    mdhandle_t targetAssembly,
    mdhandle_t targetModule,
    std::function<void(mdcursor_t)> onRowAdded,
    mdcursor_t* targetTypeDef)
{
    HRESULT hr;
    mdhandle_t sourceModule = md_extract_handle_from_cursor(sourceTypeDef);

    mdguid_t thisMvid = { 0 };
    mdguid_t targetAssemblyMvid = { 0 };
    mdguid_t sourceAssemblyMvid = { 0 };
    mdguid_t importMvid = { 0 };
    RETURN_IF_FAILED(GetMvid(targetModule, &thisMvid));
    RETURN_IF_FAILED(GetMvid(targetAssembly, &targetAssemblyMvid));
    RETURN_IF_FAILED(GetMvid(sourceAssembly, &sourceAssemblyMvid));
    RETURN_IF_FAILED(GetMvid(sourceModule, &importMvid));

    bool sameModuleMvid = std::memcmp(&thisMvid, &importMvid, sizeof(mdguid_t)) == 0;
    bool sameAssemblyMvid = std::memcmp(&targetAssemblyMvid, &sourceAssemblyMvid, sizeof(mdguid_t)) == 0;

    mdcursor_t resolutionScope;
    if (sameAssemblyMvid && sameModuleMvid)
    {
        uint32_t count;
        if (!md_create_cursor(targetModule, mdtid_Module, &resolutionScope, &count))
            return E_FAIL;
    }
    else if (sameAssemblyMvid && !sameModuleMvid)
    {
        char const* importName;
        mdcursor_t importModule;
        uint32_t count;
        if (!md_create_cursor(sourceModule, mdtid_Module, &importModule, &count)
            || 1 != md_get_column_value_as_utf8(importModule, mdtModule_Name, 1, &importName))
        {
            return E_FAIL;
        }

        md_added_row_t moduleRef;
        if (!md_append_row(targetModule, mdtid_ModuleRef, &moduleRef)
            || 1 != md_set_column_value_as_utf8(moduleRef, mdtModuleRef_Name, 1, &importName))
        {
            return E_FAIL;
        }

        resolutionScope = moduleRef;
        onRowAdded(moduleRef);
    }
    else if (sameModuleMvid)
    {
        // The import can't be the same module and different assemblies.
        // COMPAT-BREAK: CoreCLR allows this for cases where there is no source assembly, with a TODO from FX-era
        // relating to building the core library using multiple modules.
        // CoreCLR doesn't support multimodule assemblies, so we are okay taking a breaking change here.
        return E_INVALIDARG;
    }
    else
    {
        // TODO: Create assembly ref
        mdcursor_t importAssembly;
        uint32_t count;
        if (!md_create_cursor(sourceModule, mdtid_Assembly, &importAssembly, &count))
            return E_FAIL;
        
        uint32_t flags;
        if (1 != md_get_column_value_as_constant(importAssembly, mdtAssembly_Flags, 1, &flags))
            return E_FAIL;

        uint8_t const* publicKey;
        uint32_t publicKeyLength;
        if (1 != md_get_column_value_as_blob(importAssembly, mdtAssembly_PublicKey, 1, &publicKey, &publicKeyLength))
            return E_FAIL;
        
        if (publicKey != nullptr)
        {
            assert(IsAfPublicKey(flags));
            flags &= ~afPublicKey;
            // TODO: Generate token from public key.
        }
        else
        {
            assert(!IsAfPublicKey(flags));
        }

        md_added_row_t assemblyRef;
        if (!md_append_row(targetModule, mdtid_AssemblyRef, &assemblyRef))
            return E_FAIL;
        
        onRowAdded(assemblyRef);
  
        uint32_t temp;
        if (1 != md_get_column_value_as_constant(importAssembly, mdtAssembly_MajorVersion, 1, &temp)
            || 1 != md_set_column_value_as_constant(assemblyRef, mdtAssemblyRef_MajorVersion, 1, &temp))
        {
            return E_FAIL;
        }

        if (1 != md_get_column_value_as_constant(importAssembly, mdtAssembly_MinorVersion, 1, &temp)
            || 1 != md_set_column_value_as_constant(assemblyRef, mdtAssemblyRef_MinorVersion, 1, &temp))
        {
            return E_FAIL;
        }

        if (1 != md_get_column_value_as_constant(importAssembly, mdtAssembly_BuildNumber, 1, &temp)
            || 1 != md_set_column_value_as_constant(assemblyRef, mdtAssemblyRef_BuildNumber, 1, &temp))
        {
            return E_FAIL;
        }

        if (1 != md_get_column_value_as_constant(importAssembly, mdtAssembly_RevisionNumber, 1, &temp)
            || 1 != md_set_column_value_as_constant(assemblyRef, mdtAssemblyRef_RevisionNumber, 1, &temp))
        {
            return E_FAIL;
        }
        
        // TODO: Public Key/Token and Flags
        if (1 != md_set_column_value_as_constant(assemblyRef, mdtAssemblyRef_Flags, 1, &flags))
            return E_FAIL;
        
        char const* assemblyName;
        if (1 != md_get_column_value_as_utf8(importAssembly, mdtAssembly_Name, 1, &assemblyName)
            || 1 != md_set_column_value_as_utf8(assemblyRef, mdtAssemblyRef_Name, 1, &assemblyName))
        {
            return E_FAIL;
        }

        char const* assemblyCulture;
        if (1 != md_get_column_value_as_utf8(importAssembly, mdtAssembly_Culture, 1, &assemblyCulture)
            || 1 != md_set_column_value_as_utf8(assemblyRef, mdtAssemblyRef_Culture, 1, &assemblyCulture))
        {
            return E_FAIL;
        }

        uint8_t const* hash = sourceAssemblyHash;
        uint32_t hashLength = (uint32_t)sourceAssemblyHash.size();
        if (1 != md_set_column_value_as_blob(assemblyRef, mdtAssemblyRef_HashValue, 1, &hash, &hashLength))
            return E_FAIL;
        
        resolutionScope = assemblyRef;
    }

    std::stack<mdcursor_t> typesForTypeRefs;

    mdcursor_t importType;
    if (!md_token_to_cursor(sourceModule, tdImport, &importType))
        return CLDB_E_FILE_CORRUPT;
    
    typesForTypeRefs.push(importType);
    
    mdcursor_t nestedClasses;
    uint32_t nestedClassCount;
    if (!md_create_cursor(sourceModule, mdtid_NestedClass, &nestedClasses, &nestedClassCount))
        return E_FAIL;
    
    mdToken nestedTypeToken = tdImport;
    mdcursor_t nestedClass;
    while (md_find_row_from_cursor(nestedClasses, mdtNestedClass_NestedClass, RidFromToken(nestedTypeToken), &nestedClass))
    {
        mdcursor_t enclosingClass;
        if (1 != md_get_column_value_as_cursor(nestedClass, mdtNestedClass_EnclosingClass, 1, &enclosingClass))
            return E_FAIL;
        
        typesForTypeRefs.push(enclosingClass);
        if (!md_cursor_to_token(enclosingClass, &nestedTypeToken))
            return E_FAIL;
    }

    for (; !typesForTypeRefs.empty(); typesForTypeRefs.pop())
    {
        mdcursor_t typeDef = typesForTypeRefs.top();
        md_added_row_t typeRef;
        if (!md_append_row(targetModule, mdtid_TypeRef, &typeRef))
            return E_FAIL;
        
        if (1 != md_set_column_value_as_cursor(typeRef, mdtTypeRef_ResolutionScope, 1, &resolutionScope))
            return E_FAIL;
        
        char const* typeName;
        if (1 != md_get_column_value_as_utf8(typeDef, mdtTypeDef_TypeName, 1, &typeName)
            || 1 != md_set_column_value_as_utf8(typeRef, mdtTypeRef_TypeName, 1, &typeName))
        {
            return E_FAIL;
        }
        
        char const* typeNamespace;
        if (1 != md_get_column_value_as_utf8(typeDef, mdtTypeDef_TypeNamespace, 1, &typeNamespace)
            || 1 != md_set_column_value_as_utf8(typeRef, mdtTypeRef_TypeNamespace, 1, &typeNamespace))
        {
            return E_FAIL;
        }

        resolutionScope = typeRef;
        onRowAdded(typeRef);
    }

    *targetTypeDef = resolutionScope;

    return S_OK;
}