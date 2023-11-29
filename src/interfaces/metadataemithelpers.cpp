#include "metadataemithelpers.hpp"
#include <dncp.h>
#include <memory>
#include <cassert>

#define RETURN_IF_FAILED(exp) \
{ \
    hr = (exp); \
    if (FAILED(hr)) \
    { \
        return hr; \
    } \
}

HRESULT DefineImportMember(
    IMetaDataEmit* emit,                // [In] Module into which the Member is imported.
    IMetaDataAssemblyImport *pAssemImport,  // [IN] Assembly containing the Member.
    const void  *pbHashValue,           // [IN] Hash Blob for Assembly.
    ULONG        cbHashValue,           // [IN] Count of bytes.
    IMetaDataImport *pImport,           // [IN] Import scope, with member.
    mdToken     mbMember,               // [IN] Member in import scope.
    IMetaDataAssemblyEmit *pAssemEmit,  // [IN] Assembly into which the Member is imported.
    mdToken     tkImport,               // [IN] Classref or classdef in emit scope.
    mdMemberRef *pmr)                   // [OUT] Put member ref here.
{
    HRESULT hr = S_OK;
    assert(pImport && pmr);
    assert(TypeFromToken(tkImport) == mdtTypeRef || TypeFromToken(tkImport) == mdtModuleRef ||
                IsNilToken(tkImport) || TypeFromToken(tkImport) == mdtTypeSpec);
    assert((TypeFromToken(mbMember) == mdtMethodDef && mbMember != mdMethodDefNil) ||
             (TypeFromToken(mbMember) == mdtFieldDef && mbMember != mdFieldDefNil));

    size_t memberNameSize = 128;
    std::unique_ptr<WCHAR[]> memberName { new WCHAR[memberNameSize] }; // Name of the imported member.
    GUID        mvidImport;             // MVID of the import module.
    GUID        mvidEmit;               // MVID of the emit module.
    PCCOR_SIGNATURE pvSig;              // Member's signature.
    ULONG       cbSig;                  // Length of member's signature.
    ULONG       translatedSigLength;        // Length of translated signature.

    if (TypeFromToken(mbMember) == mdtMethodDef)
    {
        ULONG acutalNameLength;
        do {
            hr = pImport->GetMethodProps(mbMember, nullptr, memberName.get(), (DWORD)memberNameSize, &acutalNameLength,
                nullptr, &pvSig, &cbSig, nullptr, nullptr);
            if (hr == CLDB_S_TRUNCATION)
            {
                memberName.reset(new WCHAR[acutalNameLength]);
                memberNameSize = (size_t)acutalNameLength;
                continue;
            }
            break;
        } while (1);
    }
    else    // TypeFromToken(mbMember) == mdtFieldDef
    {
        ULONG acutalNameLength;
        do {
            hr = pImport->GetMethodProps(mbMember, nullptr, memberName.get(),(DWORD)memberNameSize, &acutalNameLength,
                nullptr, &pvSig,&cbSig, nullptr, nullptr);
            if (hr == CLDB_S_TRUNCATION)
            {
                memberName.reset(new WCHAR[acutalNameLength]);
                memberNameSize = (size_t)acutalNameLength;
                continue;
            }
            break;
        } while (1);
    }
    RETURN_IF_FAILED(hr);

    std::unique_ptr<uint8_t[]> translatedSig { new uint8_t[cbSig * 3] }; // Set translated signature buffer size conservatively.

    RETURN_IF_FAILED(TranslateSigWithScope(
        pAssemImport,
        pbHashValue,
        cbHashValue,
        pImport,
        pvSig,
        cbSig,
        pAssemEmit,
        emit,
        translatedSig.get(),
        cbSig * 3,
        &translatedSigLength));

    // Define ModuleRef for imported Member functions

    // Check if the Member being imported is a global function.
    dncp::com_ptr<IMetaDataImport> pEmitImport;
    RETURN_IF_FAILED(emit->QueryInterface(IID_IMetaDataImport, (void**)&pEmitImport));
    RETURN_IF_FAILED(pEmitImport->GetScopeProps(nullptr, 0, nullptr, &mvidEmit));

    DWORD scopeNameSize;
    RETURN_IF_FAILED(pImport->GetScopeProps(nullptr, 0, &scopeNameSize, &mvidImport));
    if (mvidEmit != mvidImport && IsNilToken(tkImport))
    {
        std::unique_ptr<WCHAR[]> scopeName { new WCHAR[scopeNameSize] }; // Name of the imported member's scope.
        RETURN_IF_FAILED(pImport->GetScopeProps(scopeName.get(), scopeNameSize,
                                        nullptr, nullptr));
        RETURN_IF_FAILED(emit->DefineModuleRef(scopeName.get(), &tkImport));
    }

    // Define MemberRef base on the name, sig, and parent
    RETURN_IF_FAILED(emit->DefineMemberRef(
        tkImport,
        memberName.get(),
        translatedSig.get(),
        translatedSigLength,
        pmr));
    
    return hr;
}