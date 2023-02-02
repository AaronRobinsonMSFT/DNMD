#include "metadataemit.hpp"

HRESULT MetadataEmit::SetModuleProps(
        LPCWSTR     szName)
{
    UNREFERENCED_PARAMETER(szName);
    return E_NOTIMPL;
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
    UNREFERENCED_PARAMETER(szTypeDef);
    UNREFERENCED_PARAMETER(dwTypeDefFlags);
    UNREFERENCED_PARAMETER(tkExtends);
    UNREFERENCED_PARAMETER(rtkImplements);
    UNREFERENCED_PARAMETER(ptd);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineNestedType(
        LPCWSTR     szTypeDef,
        DWORD       dwTypeDefFlags,
        mdToken     tkExtends,
        mdToken     rtkImplements[],
        mdTypeDef   tdEncloser,
        mdTypeDef   *ptd)
{
    UNREFERENCED_PARAMETER(szTypeDef);
    UNREFERENCED_PARAMETER(dwTypeDefFlags);
    UNREFERENCED_PARAMETER(tkExtends);
    UNREFERENCED_PARAMETER(rtkImplements);
    UNREFERENCED_PARAMETER(tdEncloser);
    UNREFERENCED_PARAMETER(ptd);
    return E_NOTIMPL;
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
    UNREFERENCED_PARAMETER(td);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(dwMethodFlags);
    UNREFERENCED_PARAMETER(pvSigBlob);
    UNREFERENCED_PARAMETER(cbSigBlob);
    UNREFERENCED_PARAMETER(ulCodeRVA);
    UNREFERENCED_PARAMETER(dwImplFlags);
    UNREFERENCED_PARAMETER(pmd);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineMethodImpl(
        mdTypeDef   td,
        mdToken     tkBody,
        mdToken     tkDecl)
{
    UNREFERENCED_PARAMETER(td);
    UNREFERENCED_PARAMETER(tkBody);
    UNREFERENCED_PARAMETER(tkDecl);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineTypeRefByName(
        mdToken     tkResolutionScope,
        LPCWSTR     szName,
        mdTypeRef   *ptr)
{
    UNREFERENCED_PARAMETER(tkResolutionScope);
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(ptr);
    return E_NOTIMPL;
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
    UNREFERENCED_PARAMETER(md);
    UNREFERENCED_PARAMETER(ulRVA);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::GetTokenFromSig(
        PCCOR_SIGNATURE pvSig,
        ULONG       cbSig,
        mdSignature *pmsig)
{
    UNREFERENCED_PARAMETER(pvSig);
    UNREFERENCED_PARAMETER(cbSig);
    UNREFERENCED_PARAMETER(pmsig);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::DefineModuleRef(
        LPCWSTR     szName,
        mdModuleRef *pmur)
{
    UNREFERENCED_PARAMETER(szName);
    UNREFERENCED_PARAMETER(pmur);
    return E_NOTIMPL;
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
    UNREFERENCED_PARAMETER(md);
    UNREFERENCED_PARAMETER(dwImplFlags);
    return E_NOTIMPL;
}

HRESULT MetadataEmit::SetFieldRVA(
        mdFieldDef  fd,
        ULONG       ulRVA)
{
    UNREFERENCED_PARAMETER(fd);
    UNREFERENCED_PARAMETER(ulRVA);
    return E_NOTIMPL;
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