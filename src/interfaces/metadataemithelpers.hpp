#ifndef _SRC_INTERFACES_METADATAEMITHELPERS_HPP
#define _SRC_INTERFACES_METADATAEMITHELPERS_HPP

#include <external/cor.h>
#include <external/corhdr.h>

HRESULT DefineImportMember(
    IMetaDataEmit* emit,                // [In] Module into which the Member is imported.
    IMetaDataAssemblyImport *pAssemImport,  // [IN] Assembly containing the Member.
    const void  *pbHashValue,           // [IN] Hash Blob for Assembly.
    ULONG        cbHashValue,           // [IN] Count of bytes.
    IMetaDataImport *pImport,           // [IN] Import scope, with member.
    mdToken     mbMember,               // [IN] Member in import scope.
    IMetaDataAssemblyEmit *pAssemEmit,  // [IN] Assembly into which the Member is imported.
    mdToken     tkImport,               // [IN] Classref or classdef in emit scope.
    mdMemberRef *pmr);                  // [OUT] Put member ref here.

#endif // _SRC_INTERFACES_METADATAEMITHELPERS_HPP