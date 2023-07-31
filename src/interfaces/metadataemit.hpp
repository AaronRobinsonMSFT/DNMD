#ifndef _SRC_INTERFACES_METADATAEMIT_HPP_
#define _SRC_INTERFACES_METADATAEMIT_HPP_

#include "internal/dnmd_platform.hpp"
#include "tearoffbase.hpp"
#include "controllingiunknown.hpp"

#include <external/cor.h>
#include <external/corhdr.h>

#include <cstdint>
#include <atomic>

class MetadataEmit final : public TearOffBase<IMetaDataEmit2, IMetaDataAssemblyEmit>
{
    mdhandle_t _md_ptr;

protected:
    bool TryGetInterfaceOnThis(REFIID riid, void** ppvObject) override
    {
        if (riid == IID_IMetaDataEmit || riid == IID_IMetaDataEmit)
        {
            *ppvObject = static_cast<IMetaDataEmit2*>(this);
            return true;
        }
        else if (riid == IID_IMetaDataAssemblyEmit)
        {
            *ppvObject = static_cast<IMetaDataAssemblyEmit*>(this);
            return true;
        }
        return false;
    }

public:
    MetadataEmit(IUnknown* controllingUnknown, mdhandle_t md_ptr)
        : TearOffBase(controllingUnknown)
        , _md_ptr{ md_ptr }
    { }

    virtual ~MetadataEmit() = default;

    mdhandle_t MetaData() const
    {
        return _md_ptr;
    }

    // TODO: provide implementations all of the methods below (starting with E_NOTIMPL stubs)

public: // IMetaDataEmit
    STDMETHOD(SetModuleProps)(
        LPCWSTR     szName);

    STDMETHOD(Save)(
        LPCWSTR     szFile,
        DWORD       dwSaveFlags);

    STDMETHOD(SaveToStream)(
        IStream     *pIStream,
        DWORD       dwSaveFlags);

    STDMETHOD(GetSaveSize)(
        CorSaveSize fSave,
        DWORD       *pdwSaveSize);

    STDMETHOD(DefineTypeDef)(
        LPCWSTR     szTypeDef,
        DWORD       dwTypeDefFlags,
        mdToken     tkExtends,
        mdToken     rtkImplements[],
        mdTypeDef   *ptd);

    STDMETHOD(DefineNestedType)(
        LPCWSTR     szTypeDef,
        DWORD       dwTypeDefFlags,
        mdToken     tkExtends,
        mdToken     rtkImplements[],
        mdTypeDef   tdEncloser,
        mdTypeDef   *ptd);

    STDMETHOD(SetHandler)(
        IUnknown    *pUnk);

    STDMETHOD(DefineMethod)(
        mdTypeDef   td,
        LPCWSTR     szName,
        DWORD       dwMethodFlags,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG       cbSigBlob,
        ULONG       ulCodeRVA,
        DWORD       dwImplFlags,
        mdMethodDef *pmd);

    STDMETHOD(DefineMethodImpl)(
        mdTypeDef   td,
        mdToken     tkBody,
        mdToken     tkDecl);

    STDMETHOD(DefineTypeRefByName)(
        mdToken     tkResolutionScope,
        LPCWSTR     szName,
        mdTypeRef   *ptr);

    STDMETHOD(DefineImportType)(
        IMetaDataAssemblyImport *pAssemImport,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        IMetaDataImport *pImport,
        mdTypeDef   tdImport,
        IMetaDataAssemblyEmit *pAssemEmit,
        mdTypeRef   *ptr);

    STDMETHOD(DefineMemberRef)(
        mdToken     tkImport,
        LPCWSTR     szName,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG       cbSigBlob,
        mdMemberRef *pmr);

    STDMETHOD(DefineImportMember)(
        IMetaDataAssemblyImport *pAssemImport,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        IMetaDataImport *pImport,
        mdToken     mbMember,
        IMetaDataAssemblyEmit *pAssemEmit,
        mdToken     tkParent,
        mdMemberRef *pmr);

    STDMETHOD(DefineEvent) (
        mdTypeDef   td,
        LPCWSTR     szEvent,
        DWORD       dwEventFlags,
        mdToken     tkEventType,
        mdMethodDef mdAddOn,
        mdMethodDef mdRemoveOn,
        mdMethodDef mdFire,
        mdMethodDef rmdOtherMethods[],
        mdEvent     *pmdEvent);

    STDMETHOD(SetClassLayout) (
        mdTypeDef   td,
        DWORD       dwPackSize,
        COR_FIELD_OFFSET rFieldOffsets[],
        ULONG       ulClassSize);

    STDMETHOD(DeleteClassLayout) (
        mdTypeDef   td);

    STDMETHOD(SetFieldMarshal) (
        mdToken     tk,
        PCCOR_SIGNATURE pvNativeType,
        ULONG       cbNativeType);

    STDMETHOD(DeleteFieldMarshal) (
        mdToken     tk);

    STDMETHOD(DefinePermissionSet) (
        mdToken     tk,
        DWORD       dwAction,
        void const  *pvPermission,
        ULONG       cbPermission,
        mdPermission *ppm);

    STDMETHOD(SetRVA)(
        mdMethodDef md,
        ULONG       ulRVA);

    STDMETHOD(GetTokenFromSig)(
        PCCOR_SIGNATURE pvSig,
        ULONG       cbSig,
        mdSignature *pmsig);

    STDMETHOD(DefineModuleRef)(
        LPCWSTR     szName,
        mdModuleRef *pmur);


    STDMETHOD(SetParent)(
        mdMemberRef mr,
        mdToken     tk);

    STDMETHOD(GetTokenFromTypeSpec)(
        PCCOR_SIGNATURE pvSig,
        ULONG       cbSig,
        mdTypeSpec *ptypespec);

    STDMETHOD(SaveToMemory)(
        void        *pbData,
        ULONG       cbData);

    STDMETHOD(DefineUserString)(
        LPCWSTR szString,
        ULONG       cchString,
        mdString    *pstk);

    STDMETHOD(DeleteToken)(
        mdToken     tkObj);

    STDMETHOD(SetMethodProps)(
        mdMethodDef md,
        DWORD       dwMethodFlags,
        ULONG       ulCodeRVA,
        DWORD       dwImplFlags);

    STDMETHOD(SetTypeDefProps)(
        mdTypeDef   td,
        DWORD       dwTypeDefFlags,
        mdToken     tkExtends,
        mdToken     rtkImplements[]);

    STDMETHOD(SetEventProps)(
        mdEvent     ev,
        DWORD       dwEventFlags,
        mdToken     tkEventType,
        mdMethodDef mdAddOn,
        mdMethodDef mdRemoveOn,
        mdMethodDef mdFire,
        mdMethodDef rmdOtherMethods[]);

    STDMETHOD(SetPermissionSetProps)(
        mdToken     tk,
        DWORD       dwAction,
        void const  *pvPermission,
        ULONG       cbPermission,
        mdPermission *ppm);

    STDMETHOD(DefinePinvokeMap)(
        mdToken     tk,
        DWORD       dwMappingFlags,
        LPCWSTR     szImportName,
        mdModuleRef mrImportDLL);

    STDMETHOD(SetPinvokeMap)(
        mdToken     tk,
        DWORD       dwMappingFlags,
        LPCWSTR     szImportName,
        mdModuleRef mrImportDLL);

    STDMETHOD(DeletePinvokeMap)(
        mdToken     tk);


    STDMETHOD(DefineCustomAttribute)(
        mdToken     tkOwner,
        mdToken     tkCtor,
        void const  *pCustomAttribute,
        ULONG       cbCustomAttribute,
        mdCustomAttribute *pcv);

    STDMETHOD(SetCustomAttributeValue)(
        mdCustomAttribute pcv,
        void const  *pCustomAttribute,
        ULONG       cbCustomAttribute);

    STDMETHOD(DefineField)(
        mdTypeDef   td,
        LPCWSTR     szName,
        DWORD       dwFieldFlags,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG       cbSigBlob,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue,
        mdFieldDef  *pmd);

    STDMETHOD(DefineProperty)(
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
        mdProperty  *pmdProp);

    STDMETHOD(DefineParam)(
        mdMethodDef md,
        ULONG       ulParamSeq,
        LPCWSTR     szName,
        DWORD       dwParamFlags,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue,
        mdParamDef  *ppd);

    STDMETHOD(SetFieldProps)(
        mdFieldDef  fd,
        DWORD       dwFieldFlags,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue);

    STDMETHOD(SetPropertyProps)(
        mdProperty  pr,
        DWORD       dwPropFlags,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue,
        mdMethodDef mdSetter,
        mdMethodDef mdGetter,
        mdMethodDef rmdOtherMethods[]);

    STDMETHOD(SetParamProps)(
        mdParamDef  pd,
        LPCWSTR     szName,
        DWORD       dwParamFlags,
        DWORD       dwCPlusTypeFlag,
        void const  *pValue,
        ULONG       cchValue);


    STDMETHOD(DefineSecurityAttributeSet)(
        mdToken     tkObj,
        COR_SECATTR rSecAttrs[],
        ULONG       cSecAttrs,
        ULONG       *pulErrorAttr);

    STDMETHOD(ApplyEditAndContinue)(
        IUnknown    *pImport);

    STDMETHOD(TranslateSigWithScope)(
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
        ULONG       *pcbTranslatedSig);

    STDMETHOD(SetMethodImplFlags)(
        mdMethodDef md,
        DWORD       dwImplFlags);

    STDMETHOD(SetFieldRVA)(
        mdFieldDef  fd,
        ULONG       ulRVA);

    STDMETHOD(Merge)(
        IMetaDataImport *pImport,
        IMapToken   *pHostMapToken,
        IUnknown    *pHandler);

    STDMETHOD(MergeEnd)();

public: // IMetaDataEmit2
    STDMETHOD(DefineMethodSpec)(
        mdToken     tkParent,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG       cbSigBlob,
        mdMethodSpec *pmi);

    STDMETHOD(GetDeltaSaveSize)(
        CorSaveSize fSave,
        DWORD       *pdwSaveSize);

    STDMETHOD(SaveDelta)(
        LPCWSTR     szFile,
        DWORD       dwSaveFlags);

    STDMETHOD(SaveDeltaToStream)(
        IStream     *pIStream,
        DWORD       dwSaveFlags);

    STDMETHOD(SaveDeltaToMemory)(
        void        *pbData,
        ULONG       cbData);

    STDMETHOD(DefineGenericParam)(
        mdToken      tk,
        ULONG        ulParamSeq,
        DWORD        dwParamFlags,
        LPCWSTR      szname,
        DWORD        reserved,
        mdToken      rtkConstraints[],
        mdGenericParam *pgp);

    STDMETHOD(SetGenericParamProps)(
        mdGenericParam gp,
        DWORD        dwParamFlags,
        LPCWSTR      szName,
        DWORD        reserved,
        mdToken      rtkConstraints[]);

    STDMETHOD(ResetENCLog)();

public: // IMetaDataAssemblyEmit
    STDMETHOD(DefineAssembly)(
        const void  *pbPublicKey,
        ULONG       cbPublicKey,
        ULONG       ulHashAlgId,
        LPCWSTR     szName,
        const ASSEMBLYMETADATA *pMetaData,
        DWORD       dwAssemblyFlags,
        mdAssembly  *pma);

    STDMETHOD(DefineAssemblyRef)(
        const void  *pbPublicKeyOrToken,
        ULONG       cbPublicKeyOrToken,
        LPCWSTR     szName,
        const ASSEMBLYMETADATA *pMetaData,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        DWORD       dwAssemblyRefFlags,
        mdAssemblyRef *pmdar);

    STDMETHOD(DefineFile)(
        LPCWSTR     szName,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        DWORD       dwFileFlags,
        mdFile      *pmdf);

    STDMETHOD(DefineExportedType)(
        LPCWSTR     szName,
        mdToken     tkImplementation,
        mdTypeDef   tkTypeDef,
        DWORD       dwExportedTypeFlags,
        mdExportedType   *pmdct);

    STDMETHOD(DefineManifestResource)(
        LPCWSTR     szName,
        mdToken     tkImplementation,
        DWORD       dwOffset,
        DWORD       dwResourceFlags,
        mdManifestResource  *pmdmr);

    STDMETHOD(SetAssemblyProps)(
        mdAssembly  pma,
        const void  *pbPublicKey,
        ULONG       cbPublicKey,
        ULONG       ulHashAlgId,
        LPCWSTR     szName,
        const ASSEMBLYMETADATA *pMetaData,
        DWORD       dwAssemblyFlags);

    STDMETHOD(SetAssemblyRefProps)(
        mdAssemblyRef ar,
        const void  *pbPublicKeyOrToken,
        ULONG       cbPublicKeyOrToken,
        LPCWSTR     szName,
        const ASSEMBLYMETADATA *pMetaData,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        DWORD       dwAssemblyRefFlags);

    STDMETHOD(SetFileProps)(
        mdFile      file,
        const void  *pbHashValue,
        ULONG       cbHashValue,
        DWORD       dwFileFlags);

    STDMETHOD(SetExportedTypeProps)(
        mdExportedType   ct,
        mdToken     tkImplementation,
        mdTypeDef   tkTypeDef,
        DWORD       dwExportedTypeFlags);

    STDMETHOD(SetManifestResourceProps)(
        mdManifestResource  mr,
        mdToken     tkImplementation,
        DWORD       dwOffset,
        DWORD       dwResourceFlags);
};

#endif