#include "metadataemit.hpp"
#include "pal.hpp"
#include <limits>
#include <fstream>
#include <stack>

#define RETURN_IF_FAILED(exp) \
{ \
    hr = (exp); \
    if (FAILED(hr)) \
    { \
        return hr; \
    } \
}

#define MD_MODULE_TOKEN TokenFromRid(1, mdtModule)
#define MD_GLOBAL_PARENT_TOKEN TokenFromRid(1, mdtTypeDef)

namespace
{
    void SplitTypeName(
        char* typeName,
        char const** nspace,
        char const** name)
    {
        // Search for the last delimiter.
        char* pos = ::strrchr(typeName, '.');
        if (pos == nullptr)
        {
            // No namespace is indicated by an empty string.
            *nspace = "";
            *name = typeName;
        }
        else
        {
            *pos = '\0';
            *nspace = typeName;
            *name = pos + 1;
        }
    }
}

HRESULT MetadataEmit::SetModuleProps(
        LPCWSTR     szName)
{
    pal::StringConvert<WCHAR, char> cvt(szName);
    if (!cvt.Success())
        return E_INVALIDARG;
    
    md_added_row_t c;
    uint32_t count;
    if (!md_create_cursor(MetaData(), mdtid_Module, &c, &count)
        && !md_append_row(MetaData(), mdtid_Module, &c))
        return E_FAIL;

    // Search for a file name in the provided path
    // and use that as the module name.
    char* modulePath = cvt;
    std::size_t len = strlen(modulePath);
    const char* start = modulePath;
    for (std::size_t i = len - 1; i >= 0; i--)
    {
        if (modulePath[i] == '\\')
        {
            start = modulePath + i + 1;
            break;
        }
    }
    
    if (1 != md_set_column_value_as_utf8(c, mdtModule_Name, 1, &start))
        return E_FAIL;

    // TODO: Record ENC Log.

    return S_OK;
}

HRESULT MetadataEmit::Save(
        LPCWSTR     szFile,
        DWORD       dwSaveFlags)
{
    if (dwSaveFlags != 0)
        return E_INVALIDARG;

    pal::StringConvert<WCHAR, char> cvt(szFile);
    if (!cvt.Success())
        return E_INVALIDARG;

    std::ofstream file(cvt, std::ios::binary);
    
    size_t saveSize;
    md_write_to_buffer(MetaData(), nullptr, &saveSize);
    std::unique_ptr<uint8_t[]> buffer { new uint8_t[saveSize] };
    if (!md_write_to_buffer(MetaData(), buffer.get(), &saveSize))
        return E_FAIL;

    size_t totalSaved = 0;
    while (totalSaved < saveSize)
    {
        size_t numBytesToWrite = std::min(saveSize, (size_t)std::numeric_limits<std::streamsize>::max());
        file.write((const char*)buffer.get() + totalSaved, numBytesToWrite);
        if (file.bad())
            return E_FAIL;
        totalSaved += numBytesToWrite;
    }

    return S_OK;
}

HRESULT MetadataEmit::SaveToStream(
        IStream     *pIStream,
        DWORD       dwSaveFlags)
{
    HRESULT hr;
    if (dwSaveFlags != 0)
        return E_INVALIDARG;

    size_t saveSize;
    md_write_to_buffer(MetaData(), nullptr, &saveSize);
    std::unique_ptr<uint8_t[]> buffer { new uint8_t[saveSize] };
    md_write_to_buffer(MetaData(), buffer.get(), &saveSize);

    size_t totalSaved = 0;
    while (totalSaved < saveSize)
    {
        ULONG numBytesToWrite = (ULONG)std::min(saveSize, (size_t)std::numeric_limits<ULONG>::max());
        RETURN_IF_FAILED(pIStream->Write((const char*)buffer.get() + totalSaved, numBytesToWrite, nullptr));
        totalSaved += numBytesToWrite;
    }

    return pIStream->Write(buffer.get(), (ULONG)saveSize, nullptr);
}

HRESULT MetadataEmit::GetSaveSize(
        CorSaveSize fSave,
        DWORD       *pdwSaveSize)
{
    // TODO: Do we want to support different save modes (as specified through dispenser options)?
    // If so, we'll need to handle that here in addition to the ::Save* methods.
    UNREFERENCED_PARAMETER(fSave);
    size_t saveSize;
    md_write_to_buffer(MetaData(), nullptr, &saveSize);
    if (saveSize > std::numeric_limits<DWORD>::max())
        return CLDB_E_TOO_BIG;
    *pdwSaveSize = (DWORD)saveSize;
    return S_OK;
}

HRESULT MetadataEmit::DefineTypeDef(
        LPCWSTR     szTypeDef,
        DWORD       dwTypeDefFlags,
        mdToken     tkExtends,
        mdToken     rtkImplements[],
        mdTypeDef   *ptd)
{
    md_added_row_t c;
    if (!md_append_row(MetaData(), mdtid_TypeDef, &c))
        return E_FAIL;
    
    pal::StringConvert<WCHAR, char> cvt(szTypeDef);
    
    // TODO: Check for duplicate type definitions

    const char* ns;
    const char* name;
    SplitTypeName(cvt, &ns, &name);
    if (1 != md_set_column_value_as_utf8(c, mdtTypeDef_TypeNamespace, 1, &ns))
        return E_FAIL;
    if (1 != md_set_column_value_as_utf8(c, mdtTypeDef_TypeName, 1, &name))
        return E_FAIL;
    
    // TODO: Handle reserved flags
    uint32_t flags = (uint32_t)dwTypeDefFlags;
    if (1 != md_set_column_value_as_constant(c, mdtTypeDef_Flags, 1, &flags))
        return E_FAIL;
    
    if (1 != md_set_column_value_as_token(c, mdtTypeDef_Extends, 1, &tkExtends))
        return E_FAIL;
    
    mdcursor_t fieldCursor;
    uint32_t numFields;
    if (!md_create_cursor(MetaData(), mdtid_Field, &fieldCursor, &numFields))
        return E_FAIL;
    
    md_cursor_move(&fieldCursor, numFields);
    if (1 != md_set_column_value_as_cursor(c, mdtTypeDef_FieldList, 1, &fieldCursor))
        return E_FAIL;
    
    mdcursor_t methodCursor;
    uint32_t numMethods;
    if (!md_create_cursor(MetaData(), mdtid_MethodDef, &methodCursor, &numMethods))
        return E_FAIL;
    
    md_cursor_move(&methodCursor, numMethods);
    if (1 != md_set_column_value_as_cursor(c, mdtTypeDef_MethodList, 1, &methodCursor))
        return E_FAIL;
    
    size_t i = 0;
    mdToken currentImplementation = rtkImplements[i];
    do
    {
        md_added_row_t interfaceImpl;
        if (!md_append_row(MetaData(), mdtid_InterfaceImpl, &interfaceImpl))
            return E_FAIL;
        
        if (1 != md_set_column_value_as_cursor(interfaceImpl, mdtInterfaceImpl_Class, 1, &c))
            return E_FAIL;
        
        if (1 != md_set_column_value_as_token(interfaceImpl, mdtInterfaceImpl_Interface, 1, &currentImplementation))
            return E_FAIL;
    } while ((currentImplementation = rtkImplements[++i]) != mdTokenNil);
    
    // TODO: Update Enc Log

    if (!md_cursor_to_token(c, ptd))
        return E_FAIL;
    
    return S_OK;
}

HRESULT MetadataEmit::DefineNestedType(
        LPCWSTR     szTypeDef,
        DWORD       dwTypeDefFlags,
        mdToken     tkExtends,
        mdToken     rtkImplements[],
        mdTypeDef   tdEncloser,
        mdTypeDef   *ptd)
{
    HRESULT hr;

    if (TypeFromToken(tdEncloser) != mdtTypeDef || IsNilToken(tdEncloser))
        return E_INVALIDARG;

    if (IsTdNested(dwTypeDefFlags))
        return E_INVALIDARG;

    RETURN_IF_FAILED(DefineTypeDef(szTypeDef, dwTypeDefFlags, tkExtends, rtkImplements, ptd));

    md_added_row_t c;
    if (!md_append_row(MetaData(), mdtid_NestedClass, &c))
        return E_FAIL;

    if (1 != md_set_column_value_as_token(c, mdtNestedClass_NestedClass, 1, ptd))
        return E_FAIL;
    
    if (1 != md_set_column_value_as_token(c, mdtNestedClass_EnclosingClass, 1, &tdEncloser))
        return E_FAIL;

    // TODO: Update ENC log
    return S_OK;
}

HRESULT MetadataEmit::SetHandler(
        IUnknown    *pUnk)
{
    UNREFERENCED_PARAMETER(pUnk);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineMethod(
        mdTypeDef       td,
        LPCWSTR         szName,
        DWORD           dwMethodFlags,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG           cbSigBlob,
        ULONG           ulCodeRVA,
        DWORD           dwImplFlags,
        mdMethodDef     *pmd)
{
    mdcursor_t type;
    if (!md_token_to_cursor(MetaData(), td, &type))
        return CLDB_E_FILE_CORRUPT;

    md_added_row_t newMethod;
    if (!md_add_new_row_to_list(type, mdtTypeDef_MethodList, &newMethod))
        return E_FAIL;

    pal::StringConvert<WCHAR, char> cvt(szName);

    const char* name = cvt;
    if (1 != md_set_column_value_as_utf8(newMethod, mdtMethodDef_Name, 1, &name))
        return E_FAIL;
    
    uint32_t flags = dwMethodFlags;
    if (1 != md_set_column_value_as_constant(newMethod, mdtMethodDef_Flags, 1, &flags))
        return E_FAIL;
    
    uint32_t sigLength = cbSigBlob;
    if (1 != md_set_column_value_as_blob(newMethod, mdtMethodDef_Signature, 1, &pvSigBlob, &sigLength))
        return E_FAIL;
    
    uint32_t implFlags = dwImplFlags;
    if (1 != md_set_column_value_as_constant(newMethod, mdtMethodDef_ImplFlags, 1, &implFlags))
        return E_FAIL;
    
    uint32_t rva = ulCodeRVA;
    if (1 != md_set_column_value_as_constant(newMethod, mdtMethodDef_Rva, 1, &rva))
        return E_FAIL;

    if (!md_cursor_to_token(newMethod, pmd))
        return CLDB_E_FILE_CORRUPT;
    
    // TODO: Update ENC log
    return S_OK;
}

HRESULT MetadataEmit::DefineMethodImpl(
        mdTypeDef   td,
        mdToken     tkBody,
        mdToken     tkDecl)
{
    md_added_row_t c;
    if (!md_append_row(MetaData(), mdtid_MethodImpl, &c))
        return E_FAIL;

    if (1 != md_set_column_value_as_token(c, mdtMethodImpl_Class, 1, &td))
        return E_FAIL;
    
    if (1 != md_set_column_value_as_token(c, mdtMethodImpl_MethodBody, 1, &tkBody))
        return E_FAIL;

    if (1 != md_set_column_value_as_token(c, mdtMethodImpl_MethodDeclaration, 1, &tkDecl))
        return E_FAIL;

    // TODO: Update ENC log
    return S_OK;
}

HRESULT MetadataEmit::DefineTypeRefByName(
        mdToken     tkResolutionScope,
        LPCWSTR     szName,
        mdTypeRef   *ptr)
{
    md_added_row_t c;
    if (!md_append_row(MetaData(), mdtid_TypeRef, &c))
        return E_FAIL;

    if (1 != md_set_column_value_as_token(c, mdtTypeRef_ResolutionScope, 1, &tkResolutionScope))
        return E_FAIL;
    
    pal::StringConvert<WCHAR, char> cv(szName);

    if (!cv.Success())
        return E_FAIL;

    const char* ns;
    const char* name;
    SplitTypeName(cv, &ns, &name);

    if (1 != md_set_column_value_as_utf8(c, mdtTypeRef_TypeNamespace, 1, &ns))
        return E_FAIL;
    if (1 != md_set_column_value_as_utf8(c, mdtTypeRef_TypeName, 1, &name))
        return E_FAIL;

    if (!md_cursor_to_token(c, ptr))
        return E_FAIL;
    
    // TODO: Update ENC log
    return S_OK;
}

namespace
{
    HRESULT GetMvid(mdhandle_t handle, mdguid_t* mvid)
    {
        assert(mvid != nullptr);
        mdcursor_t c;
        uint32_t count;
        if (!md_create_cursor(handle, mdtid_Module, &c, &count))
            return E_INVALIDARG;

        if (1 != md_get_column_value_as_guid(c, mdtModule_Mvid, 1, mvid))
            return E_INVALIDARG;
        
        return S_OK;
    }
}

HRESULT MetadataEmit::DefineImportType(
        IMetaDataAssemblyImport *pAssemImport,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        IMetaDataImport *pImport,
        mdTypeDef   tdImport,
        IMetaDataAssemblyEmit *pAssemEmit,
        mdTypeRef   *ptr)
{
    HRESULT hr;
    dncp::com_ptr<IDNMDOwner> assemImport{};

    if (pAssemImport != nullptr)
        RETURN_IF_FAILED(pAssemImport->QueryInterface(IID_IDNMDOwner, (void**)&assemImport));

    dncp::com_ptr<IDNMDOwner> assemEmit{};
    if (pAssemEmit != nullptr)
        RETURN_IF_FAILED(pAssemEmit->QueryInterface(IID_IDNMDOwner, (void**)&assemEmit));

    if (pImport == nullptr)
        return E_INVALIDARG;
    
    dncp::com_ptr<IDNMDOwner> import{};
    RETURN_IF_FAILED(pImport->QueryInterface(IID_IDNMDOwner, (void**)&import));

    mdguid_t thisMvid = { 0 };
    mdguid_t assemEmitMvid = { 0 };
    mdguid_t assemImportMvid = { 0 };
    mdguid_t importMvid = { 0 };
    RETURN_IF_FAILED(GetMvid(MetaData(), &thisMvid));
    RETURN_IF_FAILED(GetMvid(assemEmit->MetaData(), &assemEmitMvid));
    RETURN_IF_FAILED(GetMvid(assemImport->MetaData(), &assemImportMvid));
    RETURN_IF_FAILED(GetMvid(import->MetaData(), &importMvid));

    bool sameModuleMvid = std::memcmp(&thisMvid, &importMvid, sizeof(mdguid_t)) == 0;
    bool sameAssemblyMvid = std::memcmp(&assemEmitMvid, &assemImportMvid, sizeof(mdguid_t)) == 0;

    mdcursor_t resolutionScope;
    if (sameAssemblyMvid && sameModuleMvid)
    {
        uint32_t count;
        if (!md_create_cursor(MetaData(), mdtid_Module, &resolutionScope, &count))
            return E_FAIL;
    }
    else if (sameAssemblyMvid && !sameModuleMvid)
    {
        char const* importName;
        mdcursor_t importModule;
        uint32_t count;
        if (!md_create_cursor(import->MetaData(), mdtid_Module, &importModule, &count)
            || 1 != md_get_column_value_as_utf8(importModule, mdtModule_Name, 1, &importName))
        {
            return E_FAIL;
        }

        md_added_row_t moduleRef;
        if (!md_append_row(MetaData(), mdtid_ModuleRef, &moduleRef)
            || 1 != md_set_column_value_as_utf8(moduleRef, mdtModuleRef_Name, 1, &importName))
        {
            return E_FAIL;
        }

        resolutionScope = moduleRef;
    }
    else if (sameModuleMvid)
    {
        // The import can't be the same module and different assemblies.
        // COMPAT-BREAK: CoreCLR allows this for cases where pAssemImport is null, with a TODO from FX-era.
        return E_INVALIDARG;
    }
    else
    {
        // TODO: Create assembly ref
        mdcursor_t importAssembly;
        uint32_t count;
        if (!md_create_cursor(import->MetaData(), mdtid_Assembly, &importAssembly, &count))
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
        if (!md_append_row(MetaData(), mdtid_AssemblyRef, &assemblyRef))
            return E_FAIL;
  
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

        uint8_t const* hash = (uint8_t const*)pbHashValue;
        uint32_t hashLength = (uint32_t)cbHashValue;
        if (1 != md_set_column_value_as_blob(assemblyRef, mdtAssemblyRef_HashValue, 1, &hash, &hashLength))
            return E_FAIL;
        
        resolutionScope = assemblyRef;
    }

    std::stack<mdcursor_t> typesForTypeRefs;

    mdcursor_t importType;
    if (!md_token_to_cursor(import->MetaData(), tdImport, &importType))
        return CLDB_E_FILE_CORRUPT;
    
    typesForTypeRefs.push(importType);
    
    mdcursor_t nestedClasses;
    uint32_t nestedClassCount;
    if (!md_create_cursor(import->MetaData(), mdtid_NestedClass, &nestedClasses, &nestedClassCount))
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
        if (!md_append_row(MetaData(), mdtid_TypeRef, &typeRef))
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
    }

    if (!md_cursor_to_token(resolutionScope, ptr))
        return E_FAIL;

    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::DefineMemberRef(
        mdToken     tkImport,
        LPCWSTR     szName,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG       cbSigBlob,
        mdMemberRef *pmr)
{
    if (IsNilToken(tkImport))
        tkImport = MD_GLOBAL_PARENT_TOKEN;
    
    pal::StringConvert<WCHAR, char> cvt(szName);
    if (!cvt.Success())
        return E_INVALIDARG;
    const char* name = cvt;

    // TODO: Check for duplicates

    md_added_row_t c;
    if (!md_append_row(MetaData(), mdtid_MemberRef, &c))
        return E_FAIL;
    
    if (1 != md_set_column_value_as_token(c, mdtMemberRef_Class, 1, &tkImport))
        return E_FAIL;
    
    if (1 != md_set_column_value_as_utf8(c, mdtMemberRef_Name, 1, &name))
        return E_FAIL;
    
    uint8_t const* sig = (uint8_t const*)pvSigBlob;
    uint32_t sigLength = cbSigBlob;
    if (1 != md_set_column_value_as_blob(c, mdtMemberRef_Signature, 1, &sig, &sigLength))
        return E_FAIL;
    
    if (!md_cursor_to_token(c, pmr))
        return E_FAIL;
    
    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::DefineImportMember(
        IMetaDataAssemblyImport *pAssemImport,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        IMetaDataImport *pImport,
        mdToken     mbMember,
        IMetaDataAssemblyEmit *pAssemEmit,
        mdToken     tkParent,
        mdMemberRef *pmr)
{
    UNREFERENCED_PARAMETER(pAssemImport);
    UNREFERENCED_PARAMETER(pbHashValue);
    UNREFERENCED_PARAMETER(cbHashValue);
    UNREFERENCED_PARAMETER(pImport);
    UNREFERENCED_PARAMETER(mbMember);
    UNREFERENCED_PARAMETER(pAssemEmit);
    UNREFERENCED_PARAMETER(tkParent);
    UNREFERENCED_PARAMETER(pmr);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineEvent (
        mdTypeDef   td,
        LPCWSTR     szEvent,
        DWORD       dwEventFlags,
        mdToken     tkEventType,
        mdMethodDef mdAddOn,
        mdMethodDef mdRemoveOn,
        mdMethodDef mdFire,
        mdMethodDef rmdOtherMethods[],
        mdEvent     *pmdEvent)
{
    UNREFERENCED_PARAMETER(td);
    UNREFERENCED_PARAMETER(szEvent);
    UNREFERENCED_PARAMETER(dwEventFlags);
    UNREFERENCED_PARAMETER(tkEventType);
    UNREFERENCED_PARAMETER(mdAddOn);
    UNREFERENCED_PARAMETER(mdRemoveOn);
    UNREFERENCED_PARAMETER(mdFire);
    UNREFERENCED_PARAMETER(rmdOtherMethods);
    UNREFERENCED_PARAMETER(pmdEvent);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetClassLayout (
        mdTypeDef   td,
        DWORD       dwPackSize,
        COR_FIELD_OFFSET rFieldOffsets[],
        ULONG       ulClassSize)
{
    UNREFERENCED_PARAMETER(td);
    UNREFERENCED_PARAMETER(dwPackSize);
    UNREFERENCED_PARAMETER(rFieldOffsets);
    UNREFERENCED_PARAMETER(ulClassSize);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DeleteClassLayout (
        mdTypeDef   td)
{
    UNREFERENCED_PARAMETER(td);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetFieldMarshal (
        mdToken     tk,
        PCCOR_SIGNATURE pvNativeType,
        ULONG       cbNativeType)
{
    UNREFERENCED_PARAMETER(tk);
    UNREFERENCED_PARAMETER(pvNativeType);
    UNREFERENCED_PARAMETER(cbNativeType);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DeleteFieldMarshal (
        mdToken     tk)
{
    UNREFERENCED_PARAMETER(tk);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefinePermissionSet (
        mdToken     tk,
        DWORD       dwAction,
        void const  *pvPermission,
        ULONG       cbPermission,
        mdPermission *ppm)
{
    UNREFERENCED_PARAMETER(tk);
    UNREFERENCED_PARAMETER(dwAction);
    UNREFERENCED_PARAMETER(pvPermission);
    UNREFERENCED_PARAMETER(cbPermission);
    UNREFERENCED_PARAMETER(ppm);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetRVA(
        mdMethodDef md,
        ULONG       ulRVA)
{
    mdcursor_t method;
    if (!md_token_to_cursor(MetaData(), md, &method))
        return CLDB_E_FILE_CORRUPT;
    
    uint32_t rva = ulRVA;
    if (1 != md_set_column_value_as_constant(method, mdtMethodDef_Rva, 1, &rva))
        return E_FAIL;

    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::GetTokenFromSig(
        PCCOR_SIGNATURE pvSig,
        ULONG       cbSig,
        mdSignature *pmsig)
{
    md_added_row_t c;
    if (!md_append_row(MetaData(), mdtid_StandAloneSig, &c))
        return E_FAIL;

    uint32_t sigLength = cbSig;
    if (1 != md_set_column_value_as_blob(c, mdtStandAloneSig_Signature, 1, &pvSig, &sigLength))
        return E_FAIL;

    if (!md_cursor_to_token(c, pmsig))
        return CLDB_E_FILE_CORRUPT;

    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::DefineModuleRef(
        LPCWSTR     szName,
        mdModuleRef *pmur)
{
    md_added_row_t c;
    if (!md_append_row(MetaData(), mdtid_ModuleRef, &c))
        return E_FAIL;

    pal::StringConvert<WCHAR, char> cvt(szName);
    const char* name = cvt;

    if (1 != md_set_column_value_as_utf8(c, mdtModuleRef_Name, 1, &name))
        return E_FAIL;
    
    if (!md_cursor_to_token(c, pmur))
        return CLDB_E_FILE_CORRUPT;

    // TODO: Update EncLog
    return S_OK;
}


HRESULT MetadataEmit::SetParent(
        mdMemberRef mr,
        mdToken     tk)
{
    mdcursor_t c;
    if (!md_token_to_cursor(MetaData(), mr, &c))
        return CLDB_E_FILE_CORRUPT;
    
    if (1 != md_set_column_value_as_token(c, mdtMemberRef_Class, 1, &tk))
        return E_FAIL;
    
    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::GetTokenFromTypeSpec(
        PCCOR_SIGNATURE pvSig,
        ULONG       cbSig,
        mdTypeSpec *ptypespec)
{
    md_added_row_t c;
    if (!md_append_row(MetaData(), mdtid_TypeSpec, &c))
        return E_FAIL;

    uint32_t sigLength = cbSig;
    if (1 != md_set_column_value_as_blob(c, mdtTypeSpec_Signature, 1, &pvSig, &sigLength))
        return E_FAIL;

    if (!md_cursor_to_token(c, ptypespec))
        return CLDB_E_FILE_CORRUPT;

    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::SaveToMemory(
        void        *pbData,
        ULONG       cbData)
{
    size_t saveSize = cbData;
    return md_write_to_buffer(MetaData(), (uint8_t*)pbData, &saveSize) ? S_OK : E_OUTOFMEMORY;
}

HRESULT MetadataEmit::DefineUserString(
        LPCWSTR szString,
        ULONG       cchString,
        mdString    *pstk)
{
    std::unique_ptr<char16_t[]> pString{ new char16_t[cchString + 1] };
    std::memcpy(pString.get(), szString, cchString * sizeof(char16_t));
    pString[cchString] = u'\0';

    mduserstringcursor_t c = md_add_userstring_to_heap(MetaData(), pString.get());

    if (c == 0)
        return E_FAIL;

    *pstk = TokenFromRid((mdString)c, mdtString);
    return S_OK;
}

HRESULT MetadataEmit::DeleteToken(
        mdToken     tkObj)
{
    UNREFERENCED_PARAMETER(tkObj);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetMethodProps(
        mdMethodDef md,
        DWORD       dwMethodFlags,
        ULONG       ulCodeRVA,
        DWORD       dwImplFlags)
{
    mdcursor_t c;
    if (!md_token_to_cursor(MetaData(), md, &c))
        return CLDB_E_FILE_CORRUPT;
    
    if (dwMethodFlags != std::numeric_limits<UINT32>::max())
    {
        // TODO: Strip the reserved flags from user input and preserve the existing reserved flags.
        uint32_t flags = dwMethodFlags;
        if (1 != md_set_column_value_as_constant(c, mdtMethodDef_Flags, 1, &flags))
            return E_FAIL;
    }
    
    if (ulCodeRVA != std::numeric_limits<UINT32>::max())
    {
        uint32_t rva = ulCodeRVA;
        if (1 != md_set_column_value_as_constant(c, mdtMethodDef_Rva, 1, &rva))
            return E_FAIL;
    }
    
    if (dwImplFlags != std::numeric_limits<UINT32>::max())
    {
        uint32_t implFlags = dwImplFlags;
        if (1 != md_set_column_value_as_constant(c, mdtMethodDef_ImplFlags, 1, &implFlags))
            return E_FAIL;
    }
    
    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::SetTypeDefProps(
        mdTypeDef   td,
        DWORD       dwTypeDefFlags,
        mdToken     tkExtends,
        mdToken     rtkImplements[])
{
    mdcursor_t c;
    if (!md_token_to_cursor(MetaData(), td, &c))
        return CLDB_E_FILE_CORRUPT;
    
    if (dwTypeDefFlags != std::numeric_limits<UINT32>::max())
    {
        // TODO: Strip the reserved flags from user input and preserve the existing reserved flags.
        uint32_t flags = dwTypeDefFlags;
        if (1 != md_set_column_value_as_constant(c, mdtTypeDef_Flags, 1, &flags))
            return E_FAIL;
    }

    if (tkExtends != std::numeric_limits<UINT32>::max())
    {
        if (IsNilToken(tkExtends))
            tkExtends = mdTypeDefNil;
        
        if (1 != md_set_column_value_as_token(c, mdtTypeDef_Extends, 1, &tkExtends))
            return E_FAIL;
    }

    if (rtkImplements)
    {
        // First null-out the Class columns of the current implementations.
        // We can't delete here as we hand out tokens into this table to the caller.
        // This would be much more efficient if we could delete rows, as nulling out the parent will almost assuredly make the column
        // unsorted.
        mdcursor_t interfaceImplCursor;
        uint32_t numInterfaceImpls;
        if (md_create_cursor(MetaData(), mdtid_InterfaceImpl, &interfaceImplCursor, &numInterfaceImpls)
            && md_find_range_from_cursor(interfaceImplCursor, mdtInterfaceImpl_Class, RidFromToken(td), &interfaceImplCursor, &numInterfaceImpls) != MD_RANGE_NOT_FOUND)
        {
            for (uint32_t i = 0; i < numInterfaceImpls; ++i)
            {
                mdToken parent;
                if (1 != md_get_column_value_as_token(interfaceImplCursor, mdtInterfaceImpl_Class, 1, &parent))
                    return E_FAIL;
                
                // If getting a range was unsupported, then we're doing a whole table scan here.
                // In that case, we can't assume that we've already validated the parent.
                // Update it here.
                if (parent == td)
                {
                    mdToken newParent = mdTypeDefNil;
                    if (1 != md_set_column_value_as_token(interfaceImplCursor, mdtInterfaceImpl_Class, 1, &newParent))
                        return E_FAIL;
                }
            }
        }

        size_t implIndex = 0;
        mdToken currentImplementation = rtkImplements[implIndex];
        do
        {
            md_added_row_t interfaceImpl;
            if (!md_append_row(MetaData(), mdtid_InterfaceImpl, &interfaceImpl))
                return E_FAIL;
            
            if (1 != md_set_column_value_as_cursor(interfaceImpl, mdtInterfaceImpl_Class, 1, &c))
                return E_FAIL;
            
            if (1 != md_set_column_value_as_token(interfaceImpl, mdtInterfaceImpl_Interface, 1, &currentImplementation))
                return E_FAIL;
        } while ((currentImplementation = rtkImplements[++implIndex]) != mdTokenNil);
    }

    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::SetEventProps(
        mdEvent     ev,
        DWORD       dwEventFlags,
        mdToken     tkEventType,
        mdMethodDef mdAddOn,
        mdMethodDef mdRemoveOn,
        mdMethodDef mdFire,
        mdMethodDef rmdOtherMethods[])
{
    UNREFERENCED_PARAMETER(ev);
    UNREFERENCED_PARAMETER(dwEventFlags);
    UNREFERENCED_PARAMETER(tkEventType);
    UNREFERENCED_PARAMETER(mdAddOn);
    UNREFERENCED_PARAMETER(mdRemoveOn);
    UNREFERENCED_PARAMETER(mdFire);
    UNREFERENCED_PARAMETER(rmdOtherMethods);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetPermissionSetProps(
        mdToken     tk,
        DWORD       dwAction,
        void const  *pvPermission,
        ULONG       cbPermission,
        mdPermission *ppm)
{
    UNREFERENCED_PARAMETER(tk);
    UNREFERENCED_PARAMETER(dwAction);
    UNREFERENCED_PARAMETER(pvPermission);
    UNREFERENCED_PARAMETER(cbPermission);
    UNREFERENCED_PARAMETER(ppm);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefinePinvokeMap(
        mdToken     tk,
        DWORD       dwMappingFlags,
        LPCWSTR     szImportName,
        mdModuleRef mrImportDLL)
{
    mdcursor_t c;
    if (!md_token_to_cursor(MetaData(), tk, &c))
        return CLDB_E_FILE_CORRUPT;
    
    if (TypeFromToken(tk) == mdtMethodDef)
    {
        // Set "has impl map flag" and check for duplicates
    }
    else if (TypeFromToken(tk) == mdtFieldDef)
    {
        // Set "has impl map flag" and check for duplicates
    }

    // If we found a duplicate and ENC is on, update.
    // If we found a duplicate and ENC is off, fail.
    // Otherwise, we need to make a new row
    mdcursor_t row_to_edit;
    md_added_row_t added_row_wrapper;

    // TODO: We don't expose tokens for the ImplMap table, so as long as we aren't generating ENC deltas
    // we can insert in-place.
    if (!md_append_row(MetaData(), mdtid_ImplMap, &row_to_edit))
        return E_FAIL;
    added_row_wrapper = md_added_row_t(row_to_edit);

    if (1 != md_set_column_value_as_token(row_to_edit, mdtImplMap_MemberForwarded, 1, &tk))
        return E_FAIL;
    
    if (dwMappingFlags != std::numeric_limits<uint32_t>::max())
    {
        uint32_t mappingFlags = dwMappingFlags;
        if (1 != md_set_column_value_as_constant(row_to_edit, mdtImplMap_MappingFlags, 1, &mappingFlags))
            return E_FAIL;
    }
    
    pal::StringConvert<WCHAR, char> cvt(szImportName);
    const char* name = cvt;
    if (1 != md_set_column_value_as_utf8(row_to_edit, mdtImplMap_ImportName, 1, &name))
        return E_FAIL;
    
    if (IsNilToken(mrImportDLL))
    {
        // TODO: If the token is nil, create a module ref to "" (if it doesn't exist) and use that.
    }

    if (1 != md_set_column_value_as_token(row_to_edit, mdtImplMap_ImportScope, 1, &mrImportDLL))
        return E_FAIL;
    
    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::SetPinvokeMap(
        mdToken     tk,
        DWORD       dwMappingFlags,
        LPCWSTR     szImportName,
        mdModuleRef mrImportDLL)
{
    mdcursor_t c;
    if (!md_token_to_cursor(MetaData(), tk, &c))
        return CLDB_E_FILE_CORRUPT;

    mdcursor_t implMapCursor;
    uint32_t numImplMaps;
    if (!md_create_cursor(MetaData(), mdtid_ImplMap, &implMapCursor, &numImplMaps))
        return E_FAIL;

    mdcursor_t row_to_edit;
    if (!md_find_row_from_cursor(implMapCursor, mdtImplMap_MemberForwarded, tk, &row_to_edit))
        return CLDB_E_RECORD_NOTFOUND;
    
    if (dwMappingFlags != std::numeric_limits<uint32_t>::max())
    {
        uint32_t mappingFlags = dwMappingFlags;
        if (1 != md_set_column_value_as_constant(row_to_edit, mdtImplMap_MappingFlags, 1, &mappingFlags))
            return E_FAIL;
    }
    
    if (szImportName != nullptr)
    {
        pal::StringConvert<WCHAR, char> cvt(szImportName);
        const char* name = cvt;
        if (1 != md_set_column_value_as_utf8(row_to_edit, mdtImplMap_ImportName, 1, &name))
            return E_FAIL;
    }
    
    if (1 != md_set_column_value_as_token(row_to_edit, mdtImplMap_ImportScope, 1, &mrImportDLL))
        return E_FAIL;
    
    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::DeletePinvokeMap(
        mdToken     tk)
{
    UNREFERENCED_PARAMETER(tk);
    return E_NOTIMPL;
}


HRESULT MetadataEmit::DefineCustomAttribute(
        mdToken     tkOwner,
        mdToken     tkCtor,
        void const  *pCustomAttribute,
        ULONG       cbCustomAttribute,
        mdCustomAttribute *pcv)
{
    if (TypeFromToken(tkOwner) == mdtCustomAttribute)
        return E_INVALIDARG;

    if (IsNilToken(tkOwner)
        || IsNilToken(tkCtor)
        || (TypeFromToken(tkCtor) != mdtMethodDef
            && TypeFromToken(tkCtor) != mdtMemberRef) )
    {
        return E_INVALIDARG;
    }

    // TODO: Recognize pseudoattributes and handle them appropriately.

    // We hand out tokens here, so we can't move rows to keep the parent column sorted.
    md_added_row_t new_row;
    if (!md_append_row(MetaData(), mdtid_CustomAttribute, &new_row))
        return E_FAIL;
    
    if (1 != md_set_column_value_as_token(new_row, mdtCustomAttribute_Parent, 1, &tkOwner))
        return E_FAIL;
    
    if (1 != md_set_column_value_as_token(new_row, mdtCustomAttribute_Type, 1, &tkCtor))
        return E_FAIL;
    
    uint8_t const* pCustomAttributeBlob = (uint8_t const*)pCustomAttribute;
    uint32_t customAttributeBlobLen = cbCustomAttribute;
    if (1 != md_set_column_value_as_blob(new_row, mdtCustomAttribute_Value, 1, &pCustomAttributeBlob, &customAttributeBlobLen))
        return E_FAIL;
    
    if (!md_cursor_to_token(new_row, pcv))
        return CLDB_E_FILE_CORRUPT;
    
    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::SetCustomAttributeValue(
        mdCustomAttribute pcv,
        void const  *pCustomAttribute,
        ULONG       cbCustomAttribute)
{
    if (TypeFromToken(pcv) != mdtCustomAttribute)
        return E_INVALIDARG;
    
    mdcursor_t c;
    if (!md_token_to_cursor(MetaData(), pcv, &c))
        return CLDB_E_FILE_CORRUPT;

    uint8_t const* pCustomAttributeBlob = (uint8_t const*)pCustomAttribute;
    uint32_t customAttributeBlobLen = cbCustomAttribute;
    if (1 != md_set_column_value_as_blob(c, mdtCustomAttribute_Value, 1, &pCustomAttributeBlob, &customAttributeBlobLen))
        return E_FAIL;
    
    // TODO: Update EncLog
    return S_OK;
}

HRESULT MetadataEmit::DefineField(
        mdTypeDef   td,
        LPCWSTR     szName,
        DWORD       dwFieldFlags,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG       cbSigBlob,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue,
        mdFieldDef  *pmd)
{
    UNREFERENCED_PARAMETER(td);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(dwFieldFlags);
    UNREFERENCED_PARAMETER(pvSigBlob);
    UNREFERENCED_PARAMETER(cbSigBlob);
    UNREFERENCED_PARAMETER(dwCPlusTypeFlag);
    UNREFERENCED_PARAMETER(pValue);
    UNREFERENCED_PARAMETER(cchValue);
    UNREFERENCED_PARAMETER(pmd);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineProperty(
        mdTypeDef   td,
        LPCWSTR     szProperty,
        DWORD       dwPropFlags,
        PCCOR_SIGNATURE pvSig,
        ULONG       cbSig,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue,
        mdMethodDef mdSetter,
        mdMethodDef mdGetter,
        mdMethodDef rmdOtherMethods[],
        mdProperty  *pmdProp)
{
    UNREFERENCED_PARAMETER(td);
    UNREFERENCED_PARAMETER(szProperty);
    UNREFERENCED_PARAMETER(dwPropFlags);
    UNREFERENCED_PARAMETER(pvSig);
    UNREFERENCED_PARAMETER(cbSig);
    UNREFERENCED_PARAMETER(dwCPlusTypeFlag);
    UNREFERENCED_PARAMETER(pValue);
    UNREFERENCED_PARAMETER(cchValue);
    UNREFERENCED_PARAMETER(mdSetter);
    UNREFERENCED_PARAMETER(mdGetter);
    UNREFERENCED_PARAMETER(rmdOtherMethods  );
    UNREFERENCED_PARAMETER(pmdProp);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineParam(
        mdMethodDef md,
        ULONG       ulParamSeq,
        LPCWSTR     szName,
        DWORD       dwParamFlags,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue,
        mdParamDef  *ppd)
{
    UNREFERENCED_PARAMETER(md);
    UNREFERENCED_PARAMETER(ulParamSeq);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(dwParamFlags);
    UNREFERENCED_PARAMETER(dwCPlusTypeFlag);
    UNREFERENCED_PARAMETER(pValue);
    UNREFERENCED_PARAMETER(cchValue);
    UNREFERENCED_PARAMETER(ppd);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetFieldProps(
        mdFieldDef  fd,
        DWORD       dwFieldFlags,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue)
{
    UNREFERENCED_PARAMETER(fd);
    UNREFERENCED_PARAMETER(dwFieldFlags);
    UNREFERENCED_PARAMETER(dwCPlusTypeFlag);
    UNREFERENCED_PARAMETER(pValue);
    UNREFERENCED_PARAMETER(cchValue);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetPropertyProps(
        mdProperty  pr,
        DWORD       dwPropFlags,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue,
        mdMethodDef mdSetter,
        mdMethodDef mdGetter,
        mdMethodDef rmdOtherMethods[])
{
    UNREFERENCED_PARAMETER(pr);
    UNREFERENCED_PARAMETER(dwPropFlags);
    UNREFERENCED_PARAMETER(dwCPlusTypeFlag);
    UNREFERENCED_PARAMETER(pValue);
    UNREFERENCED_PARAMETER(cchValue);
    UNREFERENCED_PARAMETER(mdSetter);
    UNREFERENCED_PARAMETER(mdGetter);
    UNREFERENCED_PARAMETER(rmdOtherMethods);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetParamProps(
        mdParamDef  pd,
        LPCWSTR     szName,
        DWORD       dwParamFlags,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue)
{
    UNREFERENCED_PARAMETER(pd);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(dwParamFlags);
    UNREFERENCED_PARAMETER(dwCPlusTypeFlag);
    UNREFERENCED_PARAMETER(pValue);
    UNREFERENCED_PARAMETER(cchValue);
    return E_NOTIMPL;
}


HRESULT MetadataEmit::DefineSecurityAttributeSet(
        mdToken     tkObj,
        COR_SECATTR rSecAttrs[],
        ULONG       cSecAttrs,
        ULONG       *pulErrorAttr)
{
    UNREFERENCED_PARAMETER(tkObj);
    UNREFERENCED_PARAMETER(rSecAttrs);
    UNREFERENCED_PARAMETER(cSecAttrs);
    UNREFERENCED_PARAMETER(pulErrorAttr);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::ApplyEditAndContinue(
        IUnknown    *pImport)
{
    HRESULT hr;
    dncp::com_ptr<IDNMDOwner> delta;
    RETURN_IF_FAILED(pImport->QueryInterface(IID_IDNMDOwner, (void**)&delta));

    if (!md_apply_delta(MetaData(), delta->MetaData()))
        return E_INVALIDARG;

    // TODO: Reset and copy EncLog from delta metadata to this metadata.
    return S_OK;
}

HRESULT MetadataEmit::TranslateSigWithScope(
        IMetaDataAssemblyImport *pAssemImport,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        IMetaDataImport *import,
        PCCOR_SIGNATURE pbSigBlob,
        ULONG       cbSigBlob,
        IMetaDataAssemblyEmit *pAssemEmit,
        IMetaDataEmit *emit,
        PCOR_SIGNATURE pvTranslatedSig,
        ULONG       cbTranslatedSigMax,
        ULONG       *pcbTranslatedSig)
{
    UNREFERENCED_PARAMETER(pAssemImport);
    UNREFERENCED_PARAMETER(pbHashValue);
    UNREFERENCED_PARAMETER(cbHashValue);
    UNREFERENCED_PARAMETER(import);
    UNREFERENCED_PARAMETER(pbSigBlob);
    UNREFERENCED_PARAMETER(cbSigBlob);
    UNREFERENCED_PARAMETER(pAssemEmit);
    UNREFERENCED_PARAMETER(emit);
    UNREFERENCED_PARAMETER(pvTranslatedSig);
    UNREFERENCED_PARAMETER(cbTranslatedSigMax);
    UNREFERENCED_PARAMETER(pcbTranslatedSig);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetMethodImplFlags(
        mdMethodDef md,
        DWORD       dwImplFlags)
{
    mdcursor_t c;
    if (!md_token_to_cursor(MetaData(), md, &c))
        return E_INVALIDARG;
    
    uint32_t flags = (uint32_t)dwImplFlags;
    if (1 != md_set_column_value_as_constant(c, mdtMethodDef_ImplFlags, 1, &flags))
        return E_FAIL;

    // TODO: Update ENC log
    return S_OK;
}

HRESULT MetadataEmit::SetFieldRVA(
        mdFieldDef  fd,
        ULONG       ulRVA)
{
    uint32_t rva = (uint32_t)ulRVA;

    mdcursor_t c;
    uint32_t count;
    if (!md_create_cursor(MetaData(), mdtid_FieldRva, &c, &count))
        return E_FAIL;

    if (!md_find_row_from_cursor(c, mdtFieldRva_Field, RidFromToken(fd), &c))
    {
        mdcursor_t field;
        if (!md_token_to_cursor(MetaData(), fd, &field))
            return E_INVALIDARG;
        
        uint32_t flags;
        if (1 != md_get_column_value_as_constant(field, mdtField_Flags, 1, &flags))
            return CLDB_E_FILE_CORRUPT;
        
        flags |= fdHasFieldRVA;
        if (1 != md_set_column_value_as_constant(field, mdtField_Flags, 1, &flags))
            return E_FAIL;

        md_added_row_t new_row;
        if (!md_append_row(MetaData(), mdtid_FieldRva, &new_row))
            return E_FAIL;
        
        c = new_row;
        
        if (1 != md_set_column_value_as_token(c, mdtFieldRva_Field, 1, &fd))
            return E_FAIL;
    }
        
    if (1 != md_set_column_value_as_constant(c, mdtFieldRva_Rva, 1, &rva))
        return E_FAIL;

    // TODO: Update ENC log
    
    return S_OK;
}

HRESULT MetadataEmit::Merge(
        IMetaDataImport *pImport,
        IMapToken   *pHostMapToken,
        IUnknown    *pHandler)
{
    // Not Implemented in CoreCLR
    UNREFERENCED_PARAMETER(pImport);
    UNREFERENCED_PARAMETER(pHostMapToken);
    UNREFERENCED_PARAMETER(pHandler);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::MergeEnd()
{
    // Not Implemented in CoreCLR
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineMethodSpec(
        mdToken     tkParent,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG       cbSigBlob,
        mdMethodSpec *pmi)
{
    if (TypeFromToken(tkParent) != mdtMethodDef && TypeFromToken(tkParent) != mdtMemberRef)
        return META_E_BAD_INPUT_PARAMETER;
    
    if (cbSigBlob == 0 || pvSigBlob == nullptr || pmi == nullptr)
        return META_E_BAD_INPUT_PARAMETER;

    md_added_row_t c;
    if (!md_append_row(MetaData(), mdtid_MethodSpec, &c))
        return E_FAIL;

    if (1 != md_set_column_value_as_token(c, mdtMethodSpec_Method, 1, &tkParent))
        return E_FAIL;

    uint32_t sigLength = cbSigBlob;
    if (1 != md_set_column_value_as_blob(c, mdtMethodSpec_Instantiation, 1, &pvSigBlob, &sigLength))
        return E_FAIL;

    if (!md_cursor_to_token(c, pmi))
        return CLDB_E_FILE_CORRUPT;

    // TODO: Update EncLog
    return S_OK;
}

// TODO: Add EnC mode support to the emit implementation.
// Maybe we can do a layering model where we have a base emit implementation that doesn't support EnC,
// and then a wrapper that does?
HRESULT MetadataEmit::GetDeltaSaveSize(
        CorSaveSize fSave,
        DWORD       *pdwSaveSize)
{
    UNREFERENCED_PARAMETER(fSave);
    UNREFERENCED_PARAMETER(pdwSaveSize);
    return META_E_NOT_IN_ENC_MODE;
}

HRESULT MetadataEmit::SaveDelta(
        LPCWSTR     szFile,
        DWORD       dwSaveFlags)
{
    UNREFERENCED_PARAMETER(szFile);
    UNREFERENCED_PARAMETER(dwSaveFlags);
    return META_E_NOT_IN_ENC_MODE;
}

HRESULT MetadataEmit::SaveDeltaToStream(
        IStream     *pIStream,
        DWORD       dwSaveFlags)
{
    UNREFERENCED_PARAMETER(pIStream);
    UNREFERENCED_PARAMETER(dwSaveFlags);
    return META_E_NOT_IN_ENC_MODE;
}

HRESULT MetadataEmit::SaveDeltaToMemory(
        void        *pbData,
        ULONG       cbData)
{
    UNREFERENCED_PARAMETER(pbData);
    UNREFERENCED_PARAMETER(cbData);
    return META_E_NOT_IN_ENC_MODE;
}

HRESULT MetadataEmit::DefineGenericParam(
        mdToken      tk,
        ULONG        ulParamSeq,
        DWORD        dwParamFlags,
        LPCWSTR      szname,
        DWORD        reserved,
        mdToken      rtkConstraints[],
        mdGenericParam *pgp)
{
    UNREFERENCED_PARAMETER(tk);
    UNREFERENCED_PARAMETER(ulParamSeq);
    UNREFERENCED_PARAMETER(dwParamFlags);
    UNREFERENCED_PARAMETER(szname);
    UNREFERENCED_PARAMETER(reserved);
    UNREFERENCED_PARAMETER(rtkConstraints);
    UNREFERENCED_PARAMETER(pgp);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetGenericParamProps(
        mdGenericParam gp,
        DWORD        dwParamFlags,
        LPCWSTR      szName,
        DWORD        reserved,
        mdToken      rtkConstraints[])
{
    UNREFERENCED_PARAMETER(gp);
    UNREFERENCED_PARAMETER(dwParamFlags);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(reserved);
    UNREFERENCED_PARAMETER(rtkConstraints);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::ResetENCLog()
{
    return META_E_NOT_IN_ENC_MODE;
}

HRESULT MetadataEmit::DefineAssembly(
        const void  *pbPublicKey,
        ULONG       cbPublicKey,
        ULONG       ulHashAlgId,
        LPCWSTR     szName,
        const ASSEMBLYMETADATA *pMetaData,
        DWORD       dwAssemblyFlags,
        mdAssembly  *pma)
{
    if (szName == nullptr || pMetaData == nullptr || pma == nullptr)
        return E_INVALIDARG;

    pal::StringConvert<WCHAR, char> cvt(szName);
    if (!cvt.Success())
        return E_INVALIDARG;
    
    md_added_row_t c;
    uint32_t count;
    if (!md_create_cursor(MetaData(), mdtid_Assembly, &c, &count)
        && !md_append_row(MetaData(), mdtid_Assembly, &c))
        return E_FAIL;

    uint32_t assemblyFlags = dwAssemblyFlags;
    if (cbPublicKey != 0)
    {
        assemblyFlags |= afPublicKey;
    }
    
    const uint8_t* publicKey = (const uint8_t*)pbPublicKey;
    if (publicKey != nullptr)
    {
        uint32_t publicKeyLength = cbPublicKey;
        if (1 != md_set_column_value_as_blob(c, mdtAssembly_PublicKey, 1, &publicKey, &publicKeyLength))
            return E_FAIL;
    }
    
    if (1 != md_set_column_value_as_constant(c, mdtAssembly_Flags, 1, &assemblyFlags))
        return E_FAIL;

    const char* name = cvt;
    if (1 != md_set_column_value_as_utf8(c, mdtAssembly_Name, 1, &name))
        return E_FAIL;
    
    uint32_t hashAlgId = ulHashAlgId;
    if (1 != md_set_column_value_as_constant(c, mdtAssembly_HashAlgId, 1, &hashAlgId))
        return E_FAIL;

    if (pMetaData->usMajorVersion != std::numeric_limits<uint16_t>::max())
    {
        uint32_t majorVersion = pMetaData->usMajorVersion;
        if (1 != md_set_column_value_as_constant(c, mdtAssembly_MajorVersion, 1, &majorVersion))
            return E_FAIL;
    }

    if (pMetaData->usMinorVersion != std::numeric_limits<uint16_t>::max())
    {
        uint32_t minorVersion = pMetaData->usMinorVersion;
        if (1 != md_set_column_value_as_constant(c, mdtAssembly_MinorVersion, 1, &minorVersion))
            return E_FAIL;
    }

    if (pMetaData->usBuildNumber != std::numeric_limits<uint16_t>::max())
    {
        uint32_t buildNumber = pMetaData->usBuildNumber;
        if (1 != md_set_column_value_as_constant(c, mdtAssembly_BuildNumber, 1, &buildNumber))
            return E_FAIL;
    }

    if (pMetaData->usRevisionNumber != std::numeric_limits<uint16_t>::max())
    {
        uint32_t revisionNumber = pMetaData->usRevisionNumber;
        if (1 != md_set_column_value_as_constant(c, mdtAssembly_RevisionNumber, 1, &revisionNumber))
            return E_FAIL;
    }

    if (pMetaData->szLocale != nullptr)
    {
        pal::StringConvert<WCHAR, char> cvtLocale(pMetaData->szLocale);
        if (!cvtLocale.Success())
            return E_INVALIDARG;

        const char* locale = cvtLocale;
        if (1 != md_set_column_value_as_utf8(c, mdtAssembly_Culture, 1, &locale))
            return E_FAIL;
    }

    if (!md_cursor_to_token(c, pma))
        return E_FAIL;

    // TODO: Update ENC Log

    return S_OK;
}

HRESULT MetadataEmit::DefineAssemblyRef(
        const void  *pbPublicKeyOrToken,
        ULONG       cbPublicKeyOrToken,
        LPCWSTR     szName,
        const ASSEMBLYMETADATA *pMetaData,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        DWORD       dwAssemblyRefFlags,
        mdAssemblyRef *pmdar)
{
    if (szName == nullptr || pMetaData == nullptr || pmdar == nullptr)
        return E_INVALIDARG;

    pal::StringConvert<WCHAR, char> cvt(szName);
    if (!cvt.Success())
        return E_INVALIDARG;
    
    md_added_row_t c;
    uint32_t count;
    if (!md_append_row(MetaData(), mdtid_AssemblyRef, &c))
        return E_FAIL;
    
    const uint8_t* publicKey = (const uint8_t*)pbPublicKeyOrToken;
    if (publicKey != nullptr)
    {
        uint32_t publicKeyLength = cbPublicKeyOrToken;
        if (1 != md_set_column_value_as_blob(c, mdtAssemblyRef_PublicKeyOrToken, 1, &publicKey, &publicKeyLength))
            return E_FAIL;
    }

    if (pbHashValue != nullptr)
    {
        uint8_t const* hashValue = (uint8_t const*)pbHashValue;
        uint32_t hashValueLength = cbHashValue;
        if (1 != md_set_column_value_as_blob(c, mdtAssemblyRef_HashValue, 1, &hashValue, &hashValueLength))
            return E_FAIL;
    }
    
    uint32_t assemblyFlags = PrepareForSaving(dwAssemblyRefFlags);
    if (1 != md_set_column_value_as_constant(c, mdtAssemblyRef_Flags, 1, &assemblyFlags))
        return E_FAIL;

    const char* name = cvt;
    if (1 != md_set_column_value_as_utf8(c, mdtAssemblyRef_Name, 1, &name))
        return E_FAIL;

    if (pMetaData->usMajorVersion != std::numeric_limits<uint16_t>::max())
    {
        uint32_t majorVersion = pMetaData->usMajorVersion;
        if (1 != md_set_column_value_as_constant(c, mdtAssemblyRef_MajorVersion, 1, &majorVersion))
            return E_FAIL;
    }

    if (pMetaData->usMinorVersion != std::numeric_limits<uint16_t>::max())
    {
        uint32_t minorVersion = pMetaData->usMinorVersion;
        if (1 != md_set_column_value_as_constant(c, mdtAssemblyRef_MinorVersion, 1, &minorVersion))
            return E_FAIL;
    }

    if (pMetaData->usBuildNumber != std::numeric_limits<uint16_t>::max())
    {
        uint32_t buildNumber = pMetaData->usBuildNumber;
        if (1 != md_set_column_value_as_constant(c, mdtAssemblyRef_BuildNumber, 1, &buildNumber))
            return E_FAIL;
    }

    if (pMetaData->usRevisionNumber != std::numeric_limits<uint16_t>::max())
    {
        uint32_t revisionNumber = pMetaData->usRevisionNumber;
        if (1 != md_set_column_value_as_constant(c, mdtAssemblyRef_RevisionNumber, 1, &revisionNumber))
            return E_FAIL;
    }

    if (pMetaData->szLocale != nullptr)
    {
        pal::StringConvert<WCHAR, char> cvtLocale(pMetaData->szLocale);
        if (!cvtLocale.Success())
            return E_INVALIDARG;

        const char* locale = cvtLocale;
        if (1 != md_set_column_value_as_utf8(c, mdtAssemblyRef_Culture, 1, &locale))
            return E_FAIL;
    }

    if (!md_cursor_to_token(c, pmdar))
        return E_FAIL;

    // TODO: Update ENC Log

    return S_OK;
}

HRESULT MetadataEmit::DefineFile(
        LPCWSTR     szName,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        DWORD       dwFileFlags,
        mdFile      *pmdf)
{
    
    pal::StringConvert<WCHAR, char> cvt(szName);
    if (!cvt.Success())
        return E_INVALIDARG;

    md_added_row_t c;
    
    if (!md_append_row(MetaData(), mdtid_File, &c))
        return E_FAIL;

    char const* name = cvt;

    if (1 != md_set_column_value_as_utf8(c, mdtFile_Name, 1, &name))
        return E_FAIL;
    
    if (pbHashValue != nullptr)
    {
        uint8_t const* hashValue = (uint8_t const*)pbHashValue;
        uint32_t hashValueLength = cbHashValue;
        if (1 != md_set_column_value_as_blob(c, mdtFile_HashValue, 1, &hashValue, &hashValueLength))
            return E_FAIL;
    }

    if (dwFileFlags != std::numeric_limits<uint32_t>::max())
    {
        uint32_t fileFlags = dwFileFlags;
        if (1 != md_set_column_value_as_constant(c, mdtFile_Flags, 1, &fileFlags))
            return E_FAIL;
    }

    if (!md_cursor_to_token(c, pmdf))
        return E_FAIL;
    
    // TODO: Update ENC Log
    return S_OK;
}

HRESULT MetadataEmit::DefineExportedType(
        LPCWSTR     szName,
        mdToken     tkImplementation,
        mdTypeDef   tkTypeDef,
        DWORD       dwExportedTypeFlags,
        mdExportedType   *pmdct)
{
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(tkImplementation);
    UNREFERENCED_PARAMETER(tkTypeDef);
    UNREFERENCED_PARAMETER(dwExportedTypeFlags);
    UNREFERENCED_PARAMETER(pmdct);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineManifestResource(
        LPCWSTR     szName,
        mdToken     tkImplementation,
        DWORD       dwOffset,
        DWORD       dwResourceFlags,
        mdManifestResource  *pmdmr)
{
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(tkImplementation);
    UNREFERENCED_PARAMETER(dwOffset);
    UNREFERENCED_PARAMETER(dwResourceFlags);
    UNREFERENCED_PARAMETER(pmdmr);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetAssemblyProps(
        mdAssembly  pma,
        const void  *pbPublicKey,
        ULONG       cbPublicKey,
        ULONG       ulHashAlgId,
        LPCWSTR     szName,
        const ASSEMBLYMETADATA *pMetaData,
        DWORD       dwAssemblyFlags)
{
    UNREFERENCED_PARAMETER(pma);
    UNREFERENCED_PARAMETER(pbPublicKey);
    UNREFERENCED_PARAMETER(cbPublicKey);
    UNREFERENCED_PARAMETER(ulHashAlgId);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(pMetaData);
    UNREFERENCED_PARAMETER(dwAssemblyFlags);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetAssemblyRefProps(
        mdAssemblyRef ar,
        const void  *pbPublicKeyOrToken,
        ULONG       cbPublicKeyOrToken,
        LPCWSTR     szName,
        const ASSEMBLYMETADATA *pMetaData,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        DWORD       dwAssemblyRefFlags)
{
    UNREFERENCED_PARAMETER(ar);
    UNREFERENCED_PARAMETER(pbPublicKeyOrToken);
    UNREFERENCED_PARAMETER(cbPublicKeyOrToken);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(pMetaData);
    UNREFERENCED_PARAMETER(pbHashValue);
    UNREFERENCED_PARAMETER(cbHashValue);
    UNREFERENCED_PARAMETER(dwAssemblyRefFlags);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetFileProps(
        mdFile      file,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        DWORD       dwFileFlags)
{
    UNREFERENCED_PARAMETER(file);
    UNREFERENCED_PARAMETER(pbHashValue);
    UNREFERENCED_PARAMETER(cbHashValue);
    UNREFERENCED_PARAMETER(dwFileFlags);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetExportedTypeProps(
        mdExportedType   ct,
        mdToken     tkImplementation,
        mdTypeDef   tkTypeDef,
        DWORD       dwExportedTypeFlags)
{
    UNREFERENCED_PARAMETER(ct);
    UNREFERENCED_PARAMETER(tkImplementation);
    UNREFERENCED_PARAMETER(tkTypeDef);
    UNREFERENCED_PARAMETER(dwExportedTypeFlags);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetManifestResourceProps(
        mdManifestResource  mr,
        mdToken     tkImplementation,
        DWORD       dwOffset,
        DWORD       dwResourceFlags)
{
    UNREFERENCED_PARAMETER(mr);
    UNREFERENCED_PARAMETER(tkImplementation);
    UNREFERENCED_PARAMETER(dwOffset);
    UNREFERENCED_PARAMETER(dwResourceFlags);
    return E_NOTIMPL;
}