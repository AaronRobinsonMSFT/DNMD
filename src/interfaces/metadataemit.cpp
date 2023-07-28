#include "metadataemit.hpp"
#include "pal.hpp"

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
    
    mdcursor_t c;
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
    UNREFERENCED_PARAMETER(szFile);
    UNREFERENCED_PARAMETER(dwSaveFlags);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SaveToStream(
        IStream     *pIStream,
        DWORD       dwSaveFlags)
{
    UNREFERENCED_PARAMETER(pIStream);
    UNREFERENCED_PARAMETER(dwSaveFlags);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::GetSaveSize(
        CorSaveSize fSave,
        DWORD       *pdwSaveSize)
{
    UNREFERENCED_PARAMETER(fSave);
    UNREFERENCED_PARAMETER(pdwSaveSize);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineTypeDef(
        LPCWSTR     szTypeDef,
        DWORD       dwTypeDefFlags,
        mdToken     tkExtends,
        mdToken     rtkImplements[],
        mdTypeDef   *ptd)
{
    mdcursor_t c;
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
        mdcursor_t interfaceImpl;
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

    mdcursor_t c;
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

    mdcursor_t existingMethod;
    uint32_t count;
    if (!md_get_column_value_as_range(type, mdtTypeDef_MethodList, &existingMethod, &count))
        return CLDB_E_FILE_CORRUPT;

    md_cursor_move(&existingMethod, count);

    mdcursor_t newMethod;
    if (!md_insert_row_after(existingMethod, &newMethod))
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
    mdcursor_t c;
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
    mdcursor_t c;
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

HRESULT MetadataEmit::DefineImportType(
        IMetaDataAssemblyImport *pAssemImport,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        IMetaDataImport *pImport,
        mdTypeDef   tdImport,
        IMetaDataAssemblyEmit *pAssemEmit,
        mdTypeRef   *ptr)
{
    UNREFERENCED_PARAMETER(pAssemImport);
    UNREFERENCED_PARAMETER(pbHashValue);
    UNREFERENCED_PARAMETER(cbHashValue);
    UNREFERENCED_PARAMETER(pImport);
    UNREFERENCED_PARAMETER(tdImport);
    UNREFERENCED_PARAMETER(pAssemEmit);
    UNREFERENCED_PARAMETER(ptr);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineMemberRef(
        mdToken     tkImport,
        LPCWSTR     szName,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG       cbSigBlob,
        mdMemberRef *pmr)
{
    UNREFERENCED_PARAMETER(tkImport);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(pvSigBlob);
    UNREFERENCED_PARAMETER(cbSigBlob);
    UNREFERENCED_PARAMETER(pmr);
    return E_NOTIMPL;
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
    mdcursor_t c;
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
    mdcursor_t c;
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
    UNREFERENCED_PARAMETER(mr);
    UNREFERENCED_PARAMETER(tk);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::GetTokenFromTypeSpec(
        PCCOR_SIGNATURE pvSig,
        ULONG       cbSig,
        mdTypeSpec *ptypespec)
{
    UNREFERENCED_PARAMETER(pvSig);
    UNREFERENCED_PARAMETER(cbSig);
    UNREFERENCED_PARAMETER(ptypespec);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SaveToMemory(
        void        *pbData,
        ULONG       cbData)
{
    UNREFERENCED_PARAMETER(pbData);
    UNREFERENCED_PARAMETER(cbData);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineUserString(
        LPCWSTR szString,
        ULONG       cchString,
        mdString    *pstk)
{
    UNREFERENCED_PARAMETER(szString);
    UNREFERENCED_PARAMETER(cchString);
    UNREFERENCED_PARAMETER(pstk);
    return E_NOTIMPL;
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
    UNREFERENCED_PARAMETER(md);
    UNREFERENCED_PARAMETER(dwMethodFlags);
    UNREFERENCED_PARAMETER(ulCodeRVA);
    UNREFERENCED_PARAMETER(dwImplFlags);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetTypeDefProps(
        mdTypeDef   td,
        DWORD       dwTypeDefFlags,
        mdToken     tkExtends,
        mdToken     rtkImplements[])
{
    UNREFERENCED_PARAMETER(td);
    UNREFERENCED_PARAMETER(dwTypeDefFlags);
    UNREFERENCED_PARAMETER(tkExtends);
    UNREFERENCED_PARAMETER(rtkImplements);
    return E_NOTIMPL;
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
    UNREFERENCED_PARAMETER(tk);
    UNREFERENCED_PARAMETER(dwMappingFlags);
    UNREFERENCED_PARAMETER(szImportName);
    UNREFERENCED_PARAMETER(mrImportDLL);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetPinvokeMap(
        mdToken     tk,
        DWORD       dwMappingFlags,
        LPCWSTR     szImportName,
        mdModuleRef mrImportDLL)
{
    UNREFERENCED_PARAMETER(tk);
    UNREFERENCED_PARAMETER(dwMappingFlags);
    UNREFERENCED_PARAMETER(szImportName);
    UNREFERENCED_PARAMETER(mrImportDLL);
    return E_NOTIMPL;
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
    UNREFERENCED_PARAMETER(tkOwner);
    UNREFERENCED_PARAMETER(tkCtor);
    UNREFERENCED_PARAMETER(pCustomAttribute);
    UNREFERENCED_PARAMETER(cbCustomAttribute);
    UNREFERENCED_PARAMETER(pcv);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetCustomAttributeValue(
        mdCustomAttribute pcv,
        void const  *pCustomAttribute,
        ULONG       cbCustomAttribute)
{
    UNREFERENCED_PARAMETER(pcv);
    UNREFERENCED_PARAMETER(pCustomAttribute);
    UNREFERENCED_PARAMETER(cbCustomAttribute);
    return E_NOTIMPL;
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
    UNREFERENCED_PARAMETER(pImport);
    return E_NOTIMPL;
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

        if (!md_append_row(MetaData(), mdtid_FieldRva, &c))
            return E_FAIL;
        
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
    UNREFERENCED_PARAMETER(pImport);
    UNREFERENCED_PARAMETER(pHostMapToken);
    UNREFERENCED_PARAMETER(pHandler);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::MergeEnd()
{
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineMethodSpec(
        mdToken     tkParent,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG       cbSigBlob,
        mdMethodSpec *pmi)
{
    UNREFERENCED_PARAMETER(tkParent);
    UNREFERENCED_PARAMETER(pvSigBlob);
    UNREFERENCED_PARAMETER(cbSigBlob);
    UNREFERENCED_PARAMETER(pmi);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::GetDeltaSaveSize(
        CorSaveSize fSave,
        DWORD       *pdwSaveSize)
{
    UNREFERENCED_PARAMETER(fSave);
    UNREFERENCED_PARAMETER(pdwSaveSize);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SaveDelta(
        LPCWSTR     szFile,
        DWORD       dwSaveFlags)
{
    UNREFERENCED_PARAMETER(szFile);
    UNREFERENCED_PARAMETER(dwSaveFlags);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SaveDeltaToStream(
        IStream     *pIStream,
        DWORD       dwSaveFlags)
{
    UNREFERENCED_PARAMETER(pIStream);
    UNREFERENCED_PARAMETER(dwSaveFlags);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SaveDeltaToMemory(
        void        *pbData,
        ULONG       cbData)
{
    UNREFERENCED_PARAMETER(pbData);
    UNREFERENCED_PARAMETER(cbData);
    return E_NOTIMPL;
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
    return E_NOTIMPL;
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
    UNREFERENCED_PARAMETER(pbPublicKey);
    UNREFERENCED_PARAMETER(cbPublicKey);
    UNREFERENCED_PARAMETER(ulHashAlgId);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(pMetaData);
    UNREFERENCED_PARAMETER(dwAssemblyFlags);
    UNREFERENCED_PARAMETER(pma);
    return E_NOTIMPL;
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
    UNREFERENCED_PARAMETER(pbPublicKeyOrToken);
    UNREFERENCED_PARAMETER(cbPublicKeyOrToken);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(pMetaData);
    UNREFERENCED_PARAMETER(pbHashValue);
    UNREFERENCED_PARAMETER(cbHashValue);
    UNREFERENCED_PARAMETER(dwAssemblyRefFlags);
    UNREFERENCED_PARAMETER(pmdar);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineFile(
        LPCWSTR     szName,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        DWORD       dwFileFlags,
        mdFile      *pmdf)
{
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(pbHashValue);
    UNREFERENCED_PARAMETER(cbHashValue);
    UNREFERENCED_PARAMETER(dwFileFlags);
    UNREFERENCED_PARAMETER(pmdf);
    return E_NOTIMPL;
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