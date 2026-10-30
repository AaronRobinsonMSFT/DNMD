#include <cassert>

#include "pal.hpp"
#include "metadataimportro.hpp"
#include "hcorenum.hpp"
#include "signatures.hpp"
#include "dnmd.hpp"
#include <cstring>

using namespace dnmd::columns;

#define MD_MODULE_TOKEN TokenFromRid(1, mdtModule)
#define MD_GLOBAL_PARENT_TOKEN TokenFromRid(1, mdtTypeDef)

#define ToHCORENUMImpl(hcorenum) (reinterpret_cast<HCORENUMImpl*>(hcorenum))

#define RETURN_IF_FAILED(exp) \
{ \
    hr = (exp); \
    if (FAILED(hr)) \
    { \
        return hr; \
    } \
}

mdhandle_t MetadataImportRO::MetaData()
{
    return _md_ptr.get();
}

void STDMETHODCALLTYPE MetadataImportRO::CloseEnum(HCORENUM hEnum)
{
    HCORENUMImpl* impl = ToHCORENUMImpl(hEnum);
    if (impl != nullptr)
        HCORENUMImpl::Destroy(impl);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::CountEnum(HCORENUM hEnum, ULONG* pulCount)
{
    if (pulCount == nullptr)
        return E_INVALIDARG;

    HCORENUMImpl* enumImpl = ToHCORENUMImpl(hEnum);
    *pulCount = enumImpl == nullptr
        ? 0
        : enumImpl->Count();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::ResetEnum(HCORENUM hEnum, ULONG ulPos)
{
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(hEnum);
    return enumImpl == nullptr
        ? S_OK
        : enumImpl->Reset(ulPos);
}

namespace
{
    template<mdtable_id_t TableId>
    HRESULT CreateEnumTokens(
        mdhandle_t mdhandle,
        HCORENUMImpl** pEnumImpl)
    {
        HRESULT hr;
        uint32_t rows;
        auto cursor = dnmd::md_create_cursor<TableId>(mdhandle, &rows);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        HCORENUMImpl* enumImpl;
        RETURN_IF_FAILED(HCORENUMImpl::CreateTableEnum(mdhandle, 1, &enumImpl));
        HCORENUMImpl::InitTableEnum(*enumImpl, 0, cursor, rows);
        *pEnumImpl = enumImpl;
        return S_OK;
    }

    template<mdtable_id_t TableId>
    struct TokenRangeFilter final
    {
        dnmd::column<TableId, dnmd::column_type_string> FilterColumn;
        LPCWSTR Value;
    };

    template<mdtable_id_t TableId, mdtable_id_t TargetTableId, mdtable_id_t IndirectTableId>
    HRESULT CreateEnumTokenRange(
        mdhandle_t mdhandle,
        mdToken token,
        dnmd::column<TableId, dnmd::indirect_cursor<TargetTableId, IndirectTableId>> column,
        HCORENUMImpl** pEnumImpl,
        _In_opt_ TokenRangeFilter<TargetTableId> const* filter = nullptr)
    {
        HRESULT hr;
        auto cursor = dnmd::raw_cursor<TableId>{mdhandle, token};
        if (!cursor)
            return CLDB_E_INDEX_NOTFOUND;

        dnmd::indirect_cursor<TargetTableId, IndirectTableId> begin{mdhandle, 0};
        uint32_t count;
        if (!cursor.value().get_column_value_as_range(column, begin, &count))
            return CLDB_E_FILE_CORRUPT;

        HCORENUMImpl* enumImpl;
        if (filter == nullptr || filter->Value == nullptr)
        {
            RETURN_IF_FAILED(HCORENUMImpl::CreateTableEnum(mdhandle, 1, &enumImpl));
            HCORENUMImpl::InitTableEnum(*enumImpl, 0, begin, count);
        }
        else
        {
            assert(filter != nullptr && filter->Value != nullptr);
            pal::StringConvert<WCHAR, char> cvt{ filter->Value };
            if (!cvt.Success())
                return E_INVALIDARG;

            char const* toMatch;
            mdToken matchedTk;
            RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));

            HCORENUMImpl_ptr cleanup{ enumImpl };

            auto curr = begin;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (!curr)
                    return CLDB_E_FILE_CORRUPT;
                if (!curr.value().get_column_value(filter->FilterColumn, &toMatch))
                    return CLDB_E_FILE_CORRUPT;

                if (0 == ::strcmp(toMatch, cvt))
                {
                    if (!curr.get_target_token(matchedTk))
                        return CLDB_E_FILE_CORRUPT;
                    RETURN_IF_FAILED(HCORENUMImpl::AddToDynamicEnum(*enumImpl, matchedTk));
                }
                (void)dnmd::md_cursor_next(curr);
            }

            enumImpl = cleanup.release();
        }

        *pEnumImpl = enumImpl;
        return S_OK;
    }

    template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds>
    HRESULT CreateEnumTokenRangeForSortedTableKey(
        mdhandle_t mdhandle,
        dnmd::column<TableId, dnmd::raw_cursor<TargetTableIds...>> keyColumn,
        mdToken token,
        HCORENUMImpl** pEnumImpl)
    {
        HRESULT hr;
        uint32_t tableCount;
        auto cursor = dnmd::md_create_cursor<TableId>(mdhandle, &tableCount);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        dnmd::raw_cursor<TableId> begin{mdhandle, 0};
        uint32_t count;
        dnmd::range_result result = dnmd::md_find_range_from_cursor(cursor.value(), keyColumn, token, begin, &count);

        if (result == dnmd::range_result::not_found)
        {
            return HCORENUMImpl::CreateDynamicEnum(pEnumImpl);
        }
        else if (result == dnmd::range_result::found)
        {
            HCORENUMImpl* enumImpl;
            RETURN_IF_FAILED(HCORENUMImpl::CreateTableEnum(mdhandle, 1, &enumImpl));
            HCORENUMImpl::InitTableEnum(*enumImpl, 0, begin, count);
            *pEnumImpl = enumImpl;
            return S_OK;
        }
        else
        {
            // Unsorted so we need to search across the entire table
            HCORENUMImpl* enumImpl;
            RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));
            HCORENUMImpl_ptr cleanup{ enumImpl };
            auto curr = cursor;
            uint32_t currCount = tableCount;

            // Read in for matching in bulk
            mdToken matchedGroup[64];
            uint32_t i = 0;
            while (i < currCount)
            {
                int32_t read = curr.value().get_column_values(keyColumn, ARRAY_SIZE(matchedGroup), matchedGroup);
                if (read == 0)
                    break;

                if (read < 0)
                    return CLDB_E_FILE_CORRUPT;
                for (int32_t j = 0; j < read; ++j)
                {
                    if (matchedGroup[j] == token)
                    {
                        mdToken matchedTk;
                        if (!curr.get_token(matchedTk))
                            return CLDB_E_FILE_CORRUPT;
                        RETURN_IF_FAILED(HCORENUMImpl::AddToDynamicEnum(*enumImpl, matchedTk));
                    }
                    (void)dnmd::md_cursor_next(curr);
                }
                i += read;
            }

            *pEnumImpl = cleanup.release();
            return S_OK;
        }
    }

    template<mdtable_id_t TableId>
    HRESULT ReadName(mdhandle_t handle, mdToken token,
        dnmd::column<TableId, dnmd::column_type_string> column, char const** name)
    {
        auto cursor = dnmd::raw_cursor<TableId>{handle, token};
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;
        return cursor.value().get_column_value(column, name) ? S_OK : CLDB_E_FILE_CORRUPT;
    }

    template<mdtable_id_t TableId>
    bool ReadTypeName(dnmd::row_reader<TableId> const& reader, char const** nspace, char const** name)
    {
        static_assert(TableId == mdtid_TypeDef || TableId == mdtid_TypeRef);
        if constexpr (TableId == mdtid_TypeDef)
        {
            return (nspace == nullptr || reader.read(TypeDef_TypeNamespace, nspace))
                && (name == nullptr || reader.read(TypeDef_TypeName, name));
        }
        else
        {
            return (nspace == nullptr || reader.read(TypeRef_TypeNamespace, nspace))
                && (name == nullptr || reader.read(TypeRef_TypeName, name));
        }
    }

    HRESULT ConvertAndReturnStringOutput(
        _In_z_ char const* str,
        _Out_writes_to_opt_(cchBuffer, *pchBuffer)
        WCHAR* szBuffer,
        ULONG cchBuffer,
        ULONG* pchBuffer)
    {
        // Handle empty string.
        if (str[0] == '\0')
        {
            if (szBuffer != nullptr)
                ::memset(&szBuffer[0], 0, sizeof(*szBuffer));
            if (pchBuffer != nullptr)
                *pchBuffer = 0;
            return S_OK;
        }

        HRESULT hr = pal::ConvertUtf8ToUtf16(str, szBuffer, cchBuffer, (uint32_t*)pchBuffer);
        if (FAILED(hr))
        {
            if (hr == E_NOT_SUFFICIENT_BUFFER
                && szBuffer != nullptr
                && cchBuffer > 0)
            {
                ::memset(&szBuffer[cchBuffer - 1], 0, sizeof(*szBuffer));
                return CLDB_S_TRUNCATION;
            }
            return E_INVALIDARG;
        }
        return S_OK;
    }

    HRESULT ConstructTypeName(
        char const* nspace,
        char const* name,
        malloc_ptr<char>& mem)
    {
        char* buffer;
        size_t nspaceLen = nspace == nullptr ? 0 : ::strlen(nspace);
        size_t nameLen = name == nullptr ? 0 : ::strlen(name);
        size_t bufferLength = nspaceLen + nameLen + 1 + 1; // +1 for type delim and +1 for null.

        mem.reset((char*)::malloc(bufferLength * sizeof(*buffer)));
        if (mem == nullptr)
            return E_OUTOFMEMORY;

        buffer = mem.get();
        buffer[0] = '\0';

        if (nspaceLen > 0)
        {
            ::strcat_s(buffer, bufferLength, nspace);
            ::strcat_s(buffer, bufferLength, ".");
        }

        if (nameLen > 0)
            ::strcat_s(buffer, bufferLength, name);

        return S_OK;
    }

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

    // Starting from the supplied cursor, find and then enumerate
    // the range of rows in "lookupRange" with the given "lookupTk"
    // value. When a value is found, the supplied type instance will
    // be used to call back to the caller.
    //
    // Example of type instance:
    //   struct Operation
    //   {
    //       bool operator()(dnmd::cursor<mdtid_MethodImpl> cursor)
    //       {
    //           return stopEnumeration; // Return true to stop, false to continue.
    //       }
    //   };
    template<mdtable_id_t TableId, mdtable_id_t... TargetTableIds, typename T>
    HRESULT EnumTableRange(
        dnmd::cursor<TableId> begin,
        uint32_t count,
        dnmd::column<TableId, dnmd::raw_cursor<TargetTableIds...>> lookupRange,
        mdToken lookupTk,
        T& op)
    {
        dnmd::raw_cursor<TableId> curr{begin.get_handle(), 0};
        uint32_t currCount;
        dnmd::range_result result = dnmd::md_find_range_from_cursor(begin, lookupRange, lookupTk, curr, &currCount);
        if (result == dnmd::range_result::found)
        {
            // Table is sorted and subset found
            for (uint32_t i = 0; i < currCount; ++i)
            {
                if (op(curr.value()))
                    return S_OK;
                (void)dnmd::md_cursor_next(curr);
            }
        }
        else if (result == dnmd::range_result::not_supported)
        {
            // Cannot get a range on this table so we need to search across the entire table
            curr = dnmd::raw_cursor<TableId>{begin};
            currCount = count;

            // Read in for matching in bulk
            mdToken matchedGroup[64];
            uint32_t i = 0;
            while (i < currCount)
            {
                int32_t read = curr.value().get_column_values(lookupRange, ARRAY_SIZE(matchedGroup), matchedGroup);
                if (read == 0)
                    break;

                if (read < 0)
                    return CLDB_E_FILE_CORRUPT;
                for (int32_t j = 0; j < read; ++j)
                {
                    if (matchedGroup[j] == lookupTk)
                    {
                        if (op(curr.value()))
                            return S_OK;
                    }
                    (void)dnmd::md_cursor_next(curr);
                }
                i += read;
            }
        }
        return S_OK;
    }
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumTypeDefs(
    HCORENUM* phEnum,
    mdTypeDef rTypeDefs[],
    ULONG cMax,
    ULONG* pcTypeDefs)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        uint32_t rows;
        auto cursor = dnmd::md_create_cursor<mdtid_TypeDef>(_md_ptr.get(), &rows);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        // From ECMA-335, section II.22.37:
        //  "The first row of the TypeDef table represents the pseudo class that acts as parent for functions
        //  and variables defined at module scope."
        // Based on the above we always skip the first row.
        rows--;
        (void)dnmd::md_cursor_next(cursor);

        RETURN_IF_FAILED(HCORENUMImpl::CreateTableEnum(_md_ptr.get(), 1, &enumImpl));
        HCORENUMImpl::InitTableEnum(*enumImpl, 0, cursor, rows);
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rTypeDefs, cMax, pcTypeDefs);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumInterfaceImpls(
    HCORENUM* phEnum,
    mdTypeDef td,
    mdInterfaceImpl rImpls[],
    ULONG cMax,
    ULONG* pcImpls)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        RETURN_IF_FAILED(CreateEnumTokenRangeForSortedTableKey(_md_ptr.get(), InterfaceImpl_Class, td, &enumImpl));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rImpls, cMax, pcImpls);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumTypeRefs(
    HCORENUM* phEnum,
    mdTypeRef rTypeRefs[],
    ULONG cMax,
    ULONG* pcTypeRefs)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        RETURN_IF_FAILED(CreateEnumTokens<mdtid_TypeRef>(_md_ptr.get(), &enumImpl));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rTypeRefs, cMax, pcTypeRefs);
}

namespace
{
    HRESULT FindTypeDefByName(
        MetadataImportRO* importer,
        char const* nspace,
        char const* name,
        mdToken tkEnclosingClass,
        mdTypeDef* ptd)
    {
        assert(importer != nullptr && nspace != nullptr && name != nullptr && ptd != nullptr);
        *ptd = mdTypeDefNil;

        HRESULT hr;

        // If the caller supplied a TypeRef scope, we need to walk until we find
        // a TypeDef scope we can use to look up the inner definition.
        if (ExtractTokenType(tkEnclosingClass) == mdtid_TypeRef)
        {
            auto typeRefCursor = dnmd::raw_cursor<mdtid_TypeRef>{importer->MetaData(), tkEnclosingClass};
            if (!typeRefCursor)
                return CLDB_E_RECORD_NOTFOUND;

            uint32_t typeRefScope;
            char const* typeRefNspace;
            char const* typeRefName;
            dnmd::row_reader typeRefCursor_reader{typeRefCursor.value()};
            if (!typeRefCursor_reader.read(TypeRef_ResolutionScope, typeRefScope)
                || !typeRefCursor_reader.read(TypeRef_TypeNamespace, &typeRefNspace)
                || !typeRefCursor_reader.read(TypeRef_TypeName, &typeRefName))
            {
                return CLDB_E_FILE_CORRUPT;
            }

            if (tkEnclosingClass == typeRefScope
                && 0 == ::strcmp(name, typeRefName)
                && 0 == ::strcmp(nspace, typeRefNspace))
            {
                // This defensive workaround works around a feature of DotFuscator that adds a bad TypeRef
                // which causes tools like ILDASM to crash. The TypeRef's parent is set to itself
                // which causes this function to recurse infinitely.
                return CLDB_E_FILE_CORRUPT;
            }

            // Update tkEnclosingClass to TypeDef
            RETURN_IF_FAILED(FindTypeDefByName(
                importer,
                typeRefNspace,
                typeRefName,
                (ExtractTokenType(typeRefScope) == mdtid_TypeRef) ? typeRefScope : mdTokenNil,
                &tkEnclosingClass));
            assert(ExtractTokenType(tkEnclosingClass) == mdtid_TypeDef);
        }

        uint32_t count;
        auto cursor = dnmd::md_create_cursor<mdtid_TypeDef>(importer->MetaData(), &count);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        uint32_t flags;
        char const* str;
        mdToken tk;
        mdToken tmpTk;
        for (uint32_t i = 0; i < count; (void)dnmd::md_cursor_next(cursor), ++i)
        {
            dnmd::row_reader reader{cursor.value()};
            if (!reader.read(TypeDef_Flags, &flags))
                return CLDB_E_FILE_CORRUPT;

            // Use XOR to handle the following in a single expression:
            //  - The class is Nested and EnclosingClass passed is nil
            //      or
            //  - The class is not Nested and EnclosingClass passed in is not nil
            if (!(IsTdNested(flags) ^ IsNilToken(tkEnclosingClass)))
                continue;

            // Filter to enclosing class
            if (!IsNilToken(tkEnclosingClass))
            {
                assert(ExtractTokenType(tkEnclosingClass) == mdtid_TypeDef);
                if (!cursor.get_token(tk))
                    return CLDB_E_FILE_CORRUPT;
                hr = importer->GetNestedClassProps(tk, &tmpTk);

                // Skip this type if it doesn't have an enclosing class
                // or its enclosing doesn't match the filter.
                if (FAILED(hr) || tmpTk != tkEnclosingClass)
                    continue;
            }

            if (!reader.read(TypeDef_TypeNamespace, &str))
                return CLDB_E_FILE_CORRUPT;

            if (0 != ::strcmp(nspace, str))
                continue;

            if (!reader.read(TypeDef_TypeName, &str))
                return CLDB_E_FILE_CORRUPT;

            if (0 == ::strcmp(name, str))
            {
                if (!cursor.get_token(*ptd))
                    return CLDB_E_FILE_CORRUPT;
                return S_OK;
            }
        }
        return CLDB_E_RECORD_NOTFOUND;
    }
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::FindTypeDefByName(
    LPCWSTR     szTypeDef,
    mdToken     tkEnclosingClass,
    mdTypeDef* ptd)
{
    if (szTypeDef == nullptr || ptd == nullptr)
        return E_INVALIDARG;

    // Check the enclosing token is either valid or nil.
    if (ExtractTokenType(tkEnclosingClass) != mdtid_TypeDef
        && ExtractTokenType(tkEnclosingClass) != mdtid_TypeRef
        && ExtractTokenType(tkEnclosingClass) != mdtid_Module
        && !IsNilToken(tkEnclosingClass))
    {
        return E_INVALIDARG;
    }
    else if (tkEnclosingClass == MD_MODULE_TOKEN)
    {
        // Module scope is the same as no scope
        tkEnclosingClass = mdTokenNil;
    }

    pal::StringConvert<WCHAR, char> cvt{ szTypeDef };
    if (!cvt.Success())
        return E_INVALIDARG;

    char const* nspace;
    char const* name;
    SplitTypeName(cvt, &nspace, &name);
    return ::FindTypeDefByName(this, nspace, name, tkEnclosingClass, ptd);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetScopeProps(
    _Out_writes_to_opt_(cchName, *pchName)
    LPWSTR      szName,
    ULONG       cchName,
    ULONG* pchName,
    GUID* pmvid)
{
    auto cursor = dnmd::raw_cursor<mdtid_Module>{_md_ptr.get(), MD_MODULE_TOKEN};
    if (!cursor)
        return CLDB_E_INDEX_NOTFOUND;

    GUID mvid;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(Module_Mvid, reinterpret_cast<mdguid_t*>(&mvid)))
        return CLDB_E_FILE_CORRUPT;
    *pmvid = mvid;

    char const* name;
    if (!reader.read(Module_Name, &name))
        return CLDB_E_FILE_CORRUPT;
    return ConvertAndReturnStringOutput(name, szName, cchName, pchName);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetModuleFromScope(
    mdModule* pmd)
{
    if (pmd == nullptr)
        return E_POINTER;

    *pmd = TokenFromRid(1, mdtModule);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetTypeDefProps(
    mdTypeDef   td,
    _Out_writes_to_opt_(cchTypeDef, *pchTypeDef)
    LPWSTR      szTypeDef,
    ULONG       cchTypeDef,
    ULONG* pchTypeDef,
    DWORD* pdwTypeDefFlags,
    mdToken* ptkExtends)
{
    if (ExtractTokenType(td) != mdtid_TypeDef)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_TypeDef>{_md_ptr.get(), td};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    uint32_t flags;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(TypeDef_Flags, &flags))
        return CLDB_E_FILE_CORRUPT;
    *pdwTypeDefFlags = flags;

    mdToken extends;
    if (!reader.read(TypeDef_Extends, extends))
        return CLDB_E_FILE_CORRUPT;
    *ptkExtends = extends == mdTypeDefNil
        ? mdTypeRefNil
        : extends;

    char const* name;
    char const* nspace;
    if (!reader.read(TypeDef_TypeName, &name)
        || !reader.read(TypeDef_TypeNamespace, &nspace))
    {
        return CLDB_E_FILE_CORRUPT;
    }

    HRESULT hr;
    malloc_ptr<char> mem;
    RETURN_IF_FAILED(ConstructTypeName(nspace, name, mem));
    return ConvertAndReturnStringOutput(mem.get(), szTypeDef, cchTypeDef, pchTypeDef);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetInterfaceImplProps(
    mdInterfaceImpl iiImpl,
    mdTypeDef* pClass,
    mdToken* ptkIface)
{
    if (ExtractTokenType(iiImpl) != mdtid_InterfaceImpl)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_InterfaceImpl>{_md_ptr.get(), iiImpl};
    if (!cursor)
        return CLDB_E_INDEX_NOTFOUND;

    mdTypeDef type;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(InterfaceImpl_Class, type))
        return CLDB_E_FILE_CORRUPT;
    *pClass = type;

    mdToken iface;
    if (!reader.read(InterfaceImpl_Interface, iface))
        return CLDB_E_FILE_CORRUPT;
    *ptkIface = iface;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetTypeRefProps(
    mdTypeRef   tr,
    mdToken* ptkResolutionScope,
    _Out_writes_to_opt_(cchName, *pchName)
    LPWSTR      szName,
    ULONG       cchName,
    ULONG* pchName)
{
    if (ExtractTokenType(tr) != mdtid_TypeRef)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_TypeRef>{_md_ptr.get(), tr};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    mdToken resScope;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(TypeRef_ResolutionScope, resScope))
        return CLDB_E_FILE_CORRUPT;
    *ptkResolutionScope = resScope;

    char const* name;
    char const* nspace;
    if (!reader.read(TypeRef_TypeName, &name)
        || !reader.read(TypeRef_TypeNamespace, &nspace))
    {
        return CLDB_E_FILE_CORRUPT;
    }

    HRESULT hr;
    malloc_ptr<char> mem;
    RETURN_IF_FAILED(ConstructTypeName(nspace, name, mem));
    return ConvertAndReturnStringOutput(mem.get(), szName, cchName, pchName);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::ResolveTypeRef(mdTypeRef tr, REFIID riid, IUnknown** ppIScope, mdTypeDef* ptd)
{
    UNREFERENCED_PARAMETER(tr);
    UNREFERENCED_PARAMETER(riid);
    UNREFERENCED_PARAMETER(ppIScope);
    UNREFERENCED_PARAMETER(ptd);

    // Requires VM knowledge
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumMembers(
    HCORENUM* phEnum,
    mdTypeDef   cl,
    mdToken     rMembers[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        if (ExtractTokenType(cl) != mdtid_TypeDef)
            return E_INVALIDARG;

        auto cursor = dnmd::raw_cursor<mdtid_TypeDef>{_md_ptr.get(), cl};
        if (!cursor)
            return CLDB_E_INDEX_NOTFOUND;

        dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr> methodList{_md_ptr.get(), 0};
        uint32_t methodListCount;
        dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr> fieldList{_md_ptr.get(), 0};
        uint32_t fieldListCount;
        dnmd::row_reader reader{cursor.value()};
        if (!reader.read(TypeDef_FieldList, fieldList, &fieldListCount)
            || !reader.read(TypeDef_MethodList, methodList, &methodListCount))
        {
            return CLDB_E_FILE_CORRUPT;
        }

        RETURN_IF_FAILED(HCORENUMImpl::CreateTableEnum(_md_ptr.get(), 2, &enumImpl));
        HCORENUMImpl::InitTableEnum(*enumImpl, 0, methodList, methodListCount);
        HCORENUMImpl::InitTableEnum(*enumImpl, 1, fieldList, fieldListCount);
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rMembers, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumMembersWithName(
    HCORENUM* phEnum,
    mdTypeDef   cl,
    LPCWSTR     szName,
    mdToken     rMembers[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        // If name is null, defer to the EnumMembers() API.
        if (szName == nullptr)
            return EnumMembers(phEnum, cl, rMembers, cMax, pcTokens);

        if (ExtractTokenType(cl) != mdtid_TypeDef)
            return E_INVALIDARG;

        auto position = dnmd::raw_cursor<mdtid_TypeDef>{_md_ptr.get(), cl};
        if (!position)
            return CLDB_E_INDEX_NOTFOUND;
        auto cursor = position.value();

        dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr> methodList{_md_ptr.get(), 0};
        uint32_t methodListCount;
        dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr> fieldList{_md_ptr.get(), 0};
        uint32_t fieldListCount;

        dnmd::row_reader reader{cursor};
        if (!reader.read(TypeDef_FieldList, fieldList, &fieldListCount)
             || !reader.read(TypeDef_MethodList, methodList, &methodListCount))
        {
            return CLDB_E_FILE_CORRUPT;
        }

        assert(szName != nullptr);
        pal::StringConvert<WCHAR, char> cvt{ szName };
        if (!cvt.Success())
            return E_INVALIDARG;

        char const* toMatch;
        mdToken matchedTk;
        RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));

        HCORENUMImpl_ptr cleanup{ enumImpl };

        // Iterate the Type's methods
        for (uint32_t i = 0; i < methodListCount; ++i)
        {
            if (!methodList)
                return CLDB_E_FILE_CORRUPT;
            auto methodCursor = methodList.value();

            if (!methodCursor.get_column_value(MethodDef_Name, &toMatch))
                return CLDB_E_FILE_CORRUPT;

            if (0 == ::strcmp(toMatch, cvt))
            {
                if (!methodCursor.get_token(matchedTk))
                    return CLDB_E_FILE_CORRUPT;
                RETURN_IF_FAILED(HCORENUMImpl::AddToDynamicEnum(*enumImpl, matchedTk));
            }
            (void)dnmd::md_cursor_next(methodList);
        }

        // Iterate the Type's fields
        for (uint32_t i = 0; i < fieldListCount; ++i)
        {
            if (!fieldList)
                return CLDB_E_FILE_CORRUPT;
            auto fieldCursor = fieldList.value();

            if (!fieldCursor.get_column_value(Field_Name, &toMatch))
                return CLDB_E_FILE_CORRUPT;

            if (0 == ::strcmp(toMatch, cvt))
            {
                if (!fieldCursor.get_token(matchedTk))
                    return CLDB_E_FILE_CORRUPT;
                RETURN_IF_FAILED(HCORENUMImpl::AddToDynamicEnum(*enumImpl, matchedTk));
            }
            (void)dnmd::md_cursor_next(fieldList);
        }

        *phEnum = cleanup.release();
    }
    return enumImpl->ReadTokens(rMembers, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumMethods(
    HCORENUM* phEnum,
    mdTypeDef   cl,
    mdMethodDef rMethods[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    return EnumMethodsWithName(phEnum, cl, nullptr, rMethods, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumMethodsWithName(
    HCORENUM* phEnum,
    mdTypeDef   cl,
    LPCWSTR     szName,
    mdMethodDef rMethods[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        if (ExtractTokenType(cl) != mdtid_TypeDef)
            return E_INVALIDARG;

        TokenRangeFilter<mdtid_MethodDef> filter{ MethodDef_Name, szName };
        RETURN_IF_FAILED(CreateEnumTokenRange(_md_ptr.get(), cl, TypeDef_MethodList, &enumImpl, &filter));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rMethods, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumFields(
    HCORENUM* phEnum,
    mdTypeDef   cl,
    mdFieldDef  rFields[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    return EnumFieldsWithName(phEnum, cl, nullptr, rFields, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumFieldsWithName(
    HCORENUM* phEnum,
    mdTypeDef   cl,
    LPCWSTR     szName,
    mdFieldDef  rFields[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        if (ExtractTokenType(cl) != mdtid_TypeDef)
            return E_INVALIDARG;

        TokenRangeFilter<mdtid_Field> filter{ Field_Name, szName };
        RETURN_IF_FAILED(CreateEnumTokenRange(_md_ptr.get(), cl, TypeDef_FieldList, &enumImpl, &filter));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rFields, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumParams(
    HCORENUM* phEnum,
    mdMethodDef mb,
    mdParamDef  rParams[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        if (ExtractTokenType(mb) != mdtid_MethodDef)
            return E_INVALIDARG;

        RETURN_IF_FAILED(CreateEnumTokenRange(_md_ptr.get(), mb, MethodDef_ParamList, &enumImpl));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rParams, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumMemberRefs(
    HCORENUM* phEnum,
    mdToken     tkParent,
    mdMemberRef rMemberRefs[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        uint32_t count;
        auto cursor = dnmd::md_create_cursor<mdtid_MemberRef>(_md_ptr.get(), &count);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));

        HCORENUMImpl_ptr cleanup{ enumImpl };

        // Read in for matching in bulk
        mdToken toMatch[64];
        mdToken matchedTk;
        uint32_t i = 0;
        while (i < count)
        {
            int32_t read = cursor.value().get_column_values(MemberRef_Class, ARRAY_SIZE(toMatch), toMatch);
            if (read == 0)
                break;

            if (read < 0)
                return CLDB_E_FILE_CORRUPT;
            for (int32_t j = 0; j < read; ++j)
            {
                if (toMatch[j] == tkParent)
                {
                    if (!cursor.get_token(matchedTk))
                        return CLDB_E_FILE_CORRUPT;
                    RETURN_IF_FAILED(HCORENUMImpl::AddToDynamicEnum(*enumImpl, matchedTk));
                }
                (void)dnmd::md_cursor_next(cursor);
            }
            i += read;
        }
        *phEnum = cleanup.release();
    }
    return enumImpl->ReadTokens(rMemberRefs, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumMethodImpls(
    HCORENUM* phEnum,
    mdTypeDef   td,
    mdToken     rMethodBody[],
    mdToken     rMethodDecl[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        if (ExtractTokenType(td) != mdtid_TypeDef)
            return E_INVALIDARG;

        uint32_t count;
        auto cursor = dnmd::md_create_cursor<mdtid_MethodImpl>(_md_ptr.get(), &count);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl, 2));
        HCORENUMImpl_ptr cleanup{ enumImpl };

        struct _Finder
        {
            HCORENUMImpl& EnumImpl;
            mdToken Body;
            mdToken Decl;
            HRESULT hr;
            HRESULT Result; // Result of the operation

            bool operator()(dnmd::cursor<mdtid_MethodImpl> c)
            {
                dnmd::row_reader reader{c};
                if (!reader.read(MethodImpl_MethodBody, Body)
                    || !reader.read(MethodImpl_MethodDeclaration, Decl))
                {
                    Result = CLDB_E_FILE_CORRUPT;
                    return true;
                }

                if (FAILED(hr = HCORENUMImpl::AddToDynamicEnum(EnumImpl, Body))
                    || FAILED(hr = HCORENUMImpl::AddToDynamicEnum(EnumImpl, Decl)))
                {
                    Result = hr;
                    return true;
                }

                return false;
            }
        } finder{ *enumImpl, mdTokenNil, mdTokenNil, S_OK, S_OK };

        RETURN_IF_FAILED(EnumTableRange(cursor.value(), count, MethodImpl_Class, td, finder));
        RETURN_IF_FAILED(finder.Result);

        *phEnum = cleanup.release();
    }
    return enumImpl->ReadTokenPairs(rMethodBody, rMethodDecl, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumPermissionSets(
    HCORENUM* phEnum,
    mdToken     tk,
    DWORD       dwActions,
    mdPermission rPermission[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        uint32_t table = ExtractTokenType(tk);
        if (table != mdtid_TypeDef
            && table != mdtid_MethodDef
            && table != mdtid_Assembly)
        {
            *pcTokens = 0;
            return S_FALSE;
        }

        uint32_t count;
        auto cursor = dnmd::md_create_cursor<mdtid_DeclSecurity>(_md_ptr.get(), &count);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        if (!IsNilToken(tk))
        {
            if (dnmd::md_find_range_from_cursor(cursor.value(), DeclSecurity_Parent, tk, cursor, &count) == dnmd::range_result::not_found)
                return CLDB_E_RECORD_NOTFOUND;
        }

        if (IsNilToken(tk) && IsDclActionNil(dwActions))
        {
            RETURN_IF_FAILED(HCORENUMImpl::CreateTableEnum(_md_ptr.get(), 1, &enumImpl));
            HCORENUMImpl::InitTableEnum(*enumImpl, 0, cursor, count);
        }
        else
        {
            uint32_t action;
            mdToken parent;
            mdToken toAdd;
            RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));

            HCORENUMImpl_ptr cleanup{ enumImpl };
            for (uint32_t i = 0; i < count; ++i)
            {
                dnmd::row_reader reader{cursor.value()};
                if ((IsDclActionNil(dwActions)
                        || (reader.read(DeclSecurity_Action, &action)
                            && action == dwActions))
                    && (IsNilToken(tk)
                        || (reader.read(DeclSecurity_Parent, parent)
                            && parent == tk)))
                {
                    if (!cursor.get_token(toAdd))
                        return CLDB_E_FILE_CORRUPT;
                    RETURN_IF_FAILED(HCORENUMImpl::AddToDynamicEnum(*enumImpl, toAdd));
                }

                if (!dnmd::md_cursor_next(cursor))
                    return CLDB_E_RECORD_NOTFOUND;
            }
            enumImpl = cleanup.release();
        }
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rPermission, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::FindMember(
    mdTypeDef   td,
    LPCWSTR     szName,
    PCCOR_SIGNATURE pvSigBlob,
    ULONG       cbSigBlob,
    mdToken* pmb)
{
    HRESULT hr = FindMethod(td, szName, pvSigBlob, cbSigBlob, (mdMethodDef*)pmb);
    if (hr == CLDB_E_RECORD_NOTFOUND)
        hr = FindField(td, szName, pvSigBlob, cbSigBlob, (mdFieldDef*)pmb);
    return hr;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::FindMethod(
    mdTypeDef   td,
    LPCWSTR     szName,
    PCCOR_SIGNATURE pvSigBlob,
    ULONG       cbSigBlob,
    mdMethodDef* pmb)
{
    if (ExtractTokenType(td) != mdtid_TypeDef && td != mdTokenNil)
        return E_INVALIDARG;

    if (td == mdTypeDefNil || td == mdTokenNil)
        td = MD_GLOBAL_PARENT_TOKEN;

    auto typedefCursor = dnmd::raw_cursor<mdtid_TypeDef>{_md_ptr.get(), td};
    if (!typedefCursor)
        return CLDB_E_INDEX_NOTFOUND;

    dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr> methodCursor{_md_ptr.get(), 0};
    uint32_t count;
    if (!typedefCursor.value().get_column_value_as_range(TypeDef_MethodList, methodCursor, &count))
        return CLDB_E_FILE_CORRUPT;

    inline_span<uint8_t> methodDefSig;
    try
    {
        GetMethodDefSigFromMethodRefSig({ (uint8_t*)pvSigBlob, (size_t)cbSigBlob }, methodDefSig);
    }
    catch (std::exception const&)
    {
        return E_INVALIDARG;
    }

    pal::StringConvert<WCHAR, char> cvt{ szName };
    if (!cvt.Success())
        return E_INVALIDARG;

    for (uint32_t i = 0; i < count; (void)dnmd::md_cursor_next(methodCursor), ++i)
    {
        if (!methodCursor)
            return CLDB_E_FILE_CORRUPT;
        auto target = methodCursor.value();
        uint32_t flags;
        dnmd::row_reader target_reader{target};
        if (!target_reader.read(MethodDef_Flags, &flags))
            return CLDB_E_FILE_CORRUPT;

        // Ignore PrivateScope methods. By the spec, they can only be referred to by a MethodDef token
        // and cannot be discovered in any other way.
        if (IsMdPrivateScope(flags))
            continue;

        char const* methodName;
        if (!target_reader.read(MethodDef_Name, &methodName))
            return CLDB_E_FILE_CORRUPT;
        if (::strncmp(methodName, cvt, cvt.Length()) != 0)
            continue;

        if (pvSigBlob != nullptr)
        {
            uint8_t const* sig;
            uint32_t sigLen;
            if (!target_reader.read(MethodDef_Signature, &sig, &sigLen))
                return CLDB_E_FILE_CORRUPT;
            if (sigLen != methodDefSig.size()
                || ::memcmp((uint8_t*)methodDefSig, sig, sigLen) != 0)
            {
                continue;
            }
        }
        if (!target.get_token(*pmb))
            return CLDB_E_FILE_CORRUPT;
        return S_OK;
    }
    return CLDB_E_RECORD_NOTFOUND;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::FindField(
    mdTypeDef   td,
    LPCWSTR     szName,
    PCCOR_SIGNATURE pvSigBlob,
    ULONG       cbSigBlob,
    mdFieldDef* pmb)
{
    if (ExtractTokenType(td) != mdtid_TypeDef && td != mdTokenNil)
        return E_INVALIDARG;

    if (td == mdTypeDefNil || td == mdTokenNil)
        td = MD_GLOBAL_PARENT_TOKEN;

    auto typedefCursor = dnmd::raw_cursor<mdtid_TypeDef>{_md_ptr.get(), td};
    if (!typedefCursor)
        return CLDB_E_INDEX_NOTFOUND;

    dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr> fieldCursor{_md_ptr.get(), 0};
    uint32_t count;
    if (!typedefCursor.value().get_column_value_as_range(TypeDef_FieldList, fieldCursor, &count))
        return CLDB_E_FILE_CORRUPT;

    pal::StringConvert<WCHAR, char> cvt{ szName };
    if (!cvt.Success())
        return E_INVALIDARG;

    for (uint32_t i = 0; i < count; (void)dnmd::md_cursor_next(fieldCursor), ++i)
    {
        if (!fieldCursor)
            return CLDB_E_FILE_CORRUPT;
        auto target = fieldCursor.value();
        uint32_t flags;
        dnmd::row_reader target_reader{target};
        if (!target_reader.read(Field_Flags, &flags))
            return CLDB_E_FILE_CORRUPT;

        // Ignore PrivateScope fields. By the spec, they can only be referred to by a FieldDef token
        // and cannot be discovered in any other way.
        if (IsFdPrivateScope(flags))
            continue;

        char const* name;
        if (!target_reader.read(Field_Name, &name))
            return CLDB_E_FILE_CORRUPT;
        if (::strncmp(name, cvt, cvt.Length()) != 0)
            continue;

        if (pvSigBlob != nullptr)
        {
            uint8_t const* sig;
            uint32_t sigLen;
            if (!target_reader.read(Field_Signature, &sig, &sigLen))
                return CLDB_E_FILE_CORRUPT;
            if (cbSigBlob != sigLen
                || ::memcmp(pvSigBlob, sig, sigLen) != 0)
            {
                continue;
            }
        }
        if (!target.get_token(*pmb))
            return CLDB_E_FILE_CORRUPT;
        return S_OK;
    }
    return CLDB_E_RECORD_NOTFOUND;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::FindMemberRef(
    mdTypeRef   td,
    LPCWSTR     szName,
    PCCOR_SIGNATURE pvSigBlob,
    ULONG       cbSigBlob,
    mdMemberRef* pmr)
{
    if (ExtractTokenType(td) != mdtid_TypeRef
        && ExtractTokenType(td) != mdtid_MethodDef
        && ExtractTokenType(td) != mdtid_ModuleRef
        && ExtractTokenType(td) != mdtid_TypeDef
        && ExtractTokenType(td) != mdtid_TypeSpec)
    {
        return E_INVALIDARG;
    }

    if (szName == nullptr || pmr == nullptr)
        return CLDB_E_RECORD_NOTFOUND;

    if (IsNilToken(td))
        td = MD_GLOBAL_PARENT_TOKEN;

    uint32_t count;
    auto cursor = dnmd::md_create_cursor<mdtid_MemberRef>(_md_ptr.get(), &count);
    if (!cursor)
        return CLDB_E_FILE_CORRUPT;

    for (uint32_t i = 0; i < count; (void)dnmd::md_cursor_next(cursor), ++i)
    {
        mdToken refParent;
        dnmd::row_reader reader{cursor.value()};
        if (!reader.read(MemberRef_Class, refParent))
            return CLDB_E_FILE_CORRUPT;

        if (refParent != td)
            continue;

        if (szName != nullptr)
        {
            char const* name;
            if (!reader.read(MemberRef_Name, &name))
                return CLDB_E_FILE_CORRUPT;
            pal::StringConvert<WCHAR, char> cvt{ szName };
            if (!cvt.Success())
                return E_INVALIDARG;
            if (::strncmp(name, cvt, cvt.Length()) != 0)
                continue;
        }

        if (pvSigBlob != nullptr)
        {
            uint8_t const* sig;
            uint32_t sigLen;
            if (!reader.read(MemberRef_Signature, &sig, &sigLen))
                return CLDB_E_FILE_CORRUPT;
            if (cbSigBlob != sigLen
                || ::memcmp(pvSigBlob, sig, sigLen) != 0)
            {
                continue;
            }
        }
        if (!cursor.get_token(*pmr))
            return CLDB_E_FILE_CORRUPT;
        return S_OK;
    }
    return CLDB_E_RECORD_NOTFOUND;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetMethodProps(
    mdMethodDef mb,
    mdTypeDef* pClass,
    _Out_writes_to_opt_(cchMethod, *pchMethod)
    LPWSTR      szMethod,
    ULONG       cchMethod,
    ULONG* pchMethod,
    DWORD* pdwAttr,
    PCCOR_SIGNATURE* ppvSigBlob,
    ULONG* pcbSigBlob,
    ULONG* pulCodeRVA,
    DWORD* pdwImplFlags)
{
    if (ExtractTokenType(mb) != mdtid_MethodDef)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_MethodDef>{_md_ptr.get(), mb};
    if (!cursor)
        return CLDB_E_INDEX_NOTFOUND;

    mdTypeDef classDef;
    if (!cursor.value().get_parent_token(classDef))
        return CLDB_E_RECORD_NOTFOUND;
    *pClass = classDef;

    uint32_t attrs;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(MethodDef_Flags, &attrs))
        return CLDB_E_FILE_CORRUPT;
    *pdwAttr = attrs;

    uint32_t rva;
    if (!reader.read(MethodDef_Rva, &rva))
        return CLDB_E_FILE_CORRUPT;
    *pulCodeRVA = rva;

    uint32_t implFlags;
    if (!reader.read(MethodDef_ImplFlags, &implFlags))
        return CLDB_E_FILE_CORRUPT;
    *pdwImplFlags = implFlags;

    uint8_t const* sig;
    uint32_t sigLen;
    if (!reader.read(MethodDef_Signature, &sig, &sigLen))
        return CLDB_E_FILE_CORRUPT;
    *ppvSigBlob = sig;
    *pcbSigBlob = sigLen;

    char const* name;
    if (!reader.read(MethodDef_Name, &name))
        return CLDB_E_FILE_CORRUPT;
    return ConvertAndReturnStringOutput(name, szMethod, cchMethod, pchMethod);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetMemberRefProps(
    mdMemberRef mr,
    mdToken* ptk,
    _Out_writes_to_opt_(cchMember, *pchMember)
    LPWSTR      szMember,
    ULONG       cchMember,
    ULONG* pchMember,
    PCCOR_SIGNATURE* ppvSigBlob,
    ULONG* pbSig)
{
    if (ExtractTokenType(mr) != mdtid_MemberRef)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_MemberRef>{_md_ptr.get(), mr};
    if (!cursor)
        return CLDB_E_INDEX_NOTFOUND;

    mdToken type;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(MemberRef_Class, type))
        return CLDB_E_FILE_CORRUPT;

    *ptk = type;

    uint8_t const* sig;
    uint32_t sigLen;
    if (!reader.read(MemberRef_Signature, &sig, &sigLen))
        return CLDB_E_FILE_CORRUPT;

    *ppvSigBlob = sig;
    *pbSig = sigLen;

    char const* name;
    if (!reader.read(MemberRef_Name, &name))
        return CLDB_E_FILE_CORRUPT;
    return ConvertAndReturnStringOutput(name, szMember, cchMember, pchMember);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumProperties(
    HCORENUM* phEnum,
    mdTypeDef   td,
    mdProperty  rProperties[],
    ULONG       cMax,
    ULONG* pcProperties)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        if (ExtractTokenType(td) != mdtid_TypeDef)
            return E_INVALIDARG;

        // Create cursor for PropertyMap table
        auto propertyMap = dnmd::md_create_cursor<mdtid_PropertyMap>(_md_ptr.get());
        if (!propertyMap)
            return CLDB_E_RECORD_NOTFOUND;

        // Find the entry in the PropertyMap table and then
        // resolve the column to the range in the Property table.
        dnmd::raw_cursor<mdtid_PropertyMap> typedefPropMap{_md_ptr.get(), 0};
        dnmd::indirect_cursor<mdtid_Property, mdtid_PropertyPtr> propertyList{_md_ptr.get(), 0};
        uint32_t propertyListCount;
        if (!(typedefPropMap = dnmd::md_find_row_from_cursor(propertyMap.value(), PropertyMap_Parent, td))
            || !typedefPropMap.value().get_column_value_as_range(PropertyMap_PropertyList, propertyList, &propertyListCount))
        {
            return CLDB_E_FILE_CORRUPT;
        }

        RETURN_IF_FAILED(HCORENUMImpl::CreateTableEnum(_md_ptr.get(), 1, &enumImpl));
        HCORENUMImpl::InitTableEnum(*enumImpl, 0, propertyList, propertyListCount);
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rProperties, cMax, pcProperties);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumEvents(
    HCORENUM* phEnum,
    mdTypeDef   td,
    mdEvent     rEvents[],
    ULONG       cMax,
    ULONG* pcEvents)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        if (ExtractTokenType(td) != mdtid_TypeDef)
            return E_INVALIDARG;

        // Create cursor for EventMap table
        auto eventMap = dnmd::md_create_cursor<mdtid_EventMap>(_md_ptr.get());
        if (!eventMap)
            return CLDB_E_RECORD_NOTFOUND;

        // Find the entry in the EventMap table and then
        // resolve the column to the range in the Event table.
        dnmd::raw_cursor<mdtid_EventMap> typedefEventMap{_md_ptr.get(), 0};
        dnmd::indirect_cursor<mdtid_Event, mdtid_EventPtr> eventList{_md_ptr.get(), 0};
        uint32_t eventListCount;
        if (!(typedefEventMap = dnmd::md_find_row_from_cursor(eventMap.value(), EventMap_Parent, td))
            || !typedefEventMap.value().get_column_value_as_range(EventMap_EventList, eventList, &eventListCount))
        {
            return CLDB_E_FILE_CORRUPT;
        }

        RETURN_IF_FAILED(HCORENUMImpl::CreateTableEnum(_md_ptr.get(), 1, &enumImpl));
        HCORENUMImpl::InitTableEnum(*enumImpl, 0, eventList, eventListCount);
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rEvents, cMax, pcEvents);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetEventProps(
    mdEvent     ev,
    mdTypeDef* pClass,
    // Should be defined as _Out_writes_to_opt_(cchEvent, *pchEvent) and non-const. Mistake from initial release.
    LPCWSTR     szEvent,
    ULONG       cchEvent,
    ULONG* pchEvent,
    DWORD* pdwEventFlags,
    mdToken* ptkEventType,
    mdMethodDef* pmdAddOn,
    mdMethodDef* pmdRemoveOn,
    mdMethodDef* pmdFire,
    mdMethodDef rmdOtherMethod[],
    ULONG       cMax,
    ULONG* pcOtherMethod)
{
    if (ExtractTokenType(ev) != mdtid_Event)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_Event>{_md_ptr.get(), ev};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    mdTypeDef classDef;
    if (!cursor.value().get_parent_token(classDef))
        return CLDB_E_RECORD_NOTFOUND;
    *pClass = classDef;

    uint32_t flags;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(Event_EventFlags, &flags))
        return CLDB_E_FILE_CORRUPT;
    *pdwEventFlags = flags;

    mdToken type;
    if (!reader.read(Event_EventType, type))
        return CLDB_E_FILE_CORRUPT;
    *ptkEventType = type;

    uint32_t methodSemCount;
    auto methodSemCursor = dnmd::md_create_cursor<mdtid_MethodSemantics>(_md_ptr.get(), &methodSemCount);
    if (!methodSemCursor)
        return CLDB_E_RECORD_NOTFOUND;

    struct _Finder
    {
        mdMethodDef AddOn;
        mdMethodDef RemoveOn;
        mdMethodDef Fire;
        mdMethodDef* Other;
        uint32_t const OtherLen;
        uint32_t OtherCount;
        HRESULT Result; // Result of the operation

        bool operator()(dnmd::cursor<mdtid_MethodSemantics> c)
        {
            mdMethodDef tk;
            uint32_t semantics;
            dnmd::row_reader reader{c};
            if (!reader.read(MethodSemantics_Method, tk)
                || !reader.read(MethodSemantics_Semantics, &semantics))
            {
                Result = CLDB_E_FILE_CORRUPT;
                return true; // Failure detected, so stop.
            }
            switch (semantics)
            {
            case msAddOn: AddOn = tk;
                break;
            case msRemoveOn: RemoveOn = tk;
                break;
            case msFire: Fire = tk;
                break;
            case msOther:
                if (OtherCount < OtherLen)
                    Other[OtherCount] = tk;
                OtherCount++;
                break;
            default:
                assert(!"Unknown semantic");
            }
            return false;
        }
    } finder{ mdMethodDefNil, mdMethodDefNil, mdMethodDefNil, rmdOtherMethod, cMax, 0, S_OK };

    HRESULT hr;
    RETURN_IF_FAILED(EnumTableRange(methodSemCursor.value(), methodSemCount, MethodSemantics_Association, ev, finder));
    RETURN_IF_FAILED(finder.Result);

    *pmdAddOn = finder.AddOn;
    *pmdRemoveOn = finder.RemoveOn;
    *pmdFire = finder.Fire;
    *pcOtherMethod = finder.OtherCount;

    char const* name;
    if (!reader.read(Event_Name, &name))
        return CLDB_E_FILE_CORRUPT;
    // The const_cast<> is needed because the signature incorrectly expresses the
    // desired semantics. This has been wrong since .NET Framework 1.0.
    return ConvertAndReturnStringOutput(name, const_cast<WCHAR*>(szEvent), cchEvent, pchEvent);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumMethodSemantics(
    HCORENUM* phEnum,
    mdMethodDef mb,
    mdToken     rEventProp[],
    ULONG       cMax,
    ULONG* pcEventProp)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        if (ExtractTokenType(mb) != mdtid_MethodDef)
            return E_INVALIDARG;

        uint32_t count;
        auto cursor = dnmd::md_create_cursor<mdtid_MethodSemantics>(_md_ptr.get(), &count);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));

        HCORENUMImpl_ptr cleanup{ enumImpl };

        // Read in for matching in bulk
        mdToken toMatch[64];
        mdToken matchedTk;
        uint32_t i = 0;
        while (i < count)
        {
            int32_t read = cursor.value().get_column_values(MethodSemantics_Method, ARRAY_SIZE(toMatch), toMatch);
            if (read == 0)
                break;

            if (read < 0)
                return CLDB_E_FILE_CORRUPT;
            for (int32_t j = 0; j < read; ++j)
            {
                if (toMatch[j] == mb)
                {
                    if (!cursor.value().get_column_value(MethodSemantics_Association, matchedTk))
                        return CLDB_E_FILE_CORRUPT;
                    RETURN_IF_FAILED(HCORENUMImpl::AddToDynamicEnum(*enumImpl, matchedTk));
                }
                (void)dnmd::md_cursor_next(cursor);
            }
            i += read;
        }
        *phEnum = cleanup.release();
    }
    return enumImpl->ReadTokens(rEventProp, cMax, pcEventProp);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetMethodSemantics(
    mdMethodDef mb,
    mdToken     tkEventProp,
    DWORD* pdwSemanticsFlags)
{
    if (ExtractTokenType(mb) != mdtid_MethodDef || pdwSemanticsFlags == nullptr)
        return E_INVALIDARG;

    uint32_t count;
    auto cursor = dnmd::md_create_cursor<mdtid_MethodSemantics>(_md_ptr.get(), &count);
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    struct _Finder
    {
        mdMethodDef const MethodDef; // Look for this methoddef
        uint32_t Value; // Value to acquire
        HRESULT Result; // Result of the operation

        bool operator()(dnmd::cursor<mdtid_MethodSemantics> c)
        {
            mdToken matchedTk;
            dnmd::row_reader reader{c};
            if (reader.read(MethodSemantics_Method, matchedTk)
                && MethodDef == matchedTk)
            {
                // Found result, stop iterating
                Result = (reader.read(MethodSemantics_Semantics, &Value))
                    ? S_OK
                    : CLDB_E_FILE_CORRUPT;
                return true;
            }
            return false;
        }
    } finder{ mb, 0, CLDB_E_RECORD_NOTFOUND };

    HRESULT hr;
    RETURN_IF_FAILED(EnumTableRange(cursor.value(), count, MethodSemantics_Association, tkEventProp, finder));
    RETURN_IF_FAILED(finder.Result);

    *pdwSemanticsFlags = finder.Value;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetClassLayout(
    mdTypeDef   td,
    DWORD* pdwPackSize,
    COR_FIELD_OFFSET rFieldOffset[],
    ULONG       cMax,
    ULONG* pcFieldOffset,
    ULONG* pulClassSize)
{
    if (ExtractTokenType(td) != mdtid_TypeDef)
        return E_INVALIDARG;

    dnmd::raw_cursor<mdtid_ClassLayout> entry{_md_ptr.get(), 0};
    bool foundLayout = false;
    auto begin = dnmd::md_create_cursor<mdtid_ClassLayout>(_md_ptr.get());
    if (!begin
        || !(entry = dnmd::md_find_row_from_cursor(begin.value(), ClassLayout_Parent, td)))
    {
        *pdwPackSize = 0;
        *pulClassSize = 0;
    }
    else
    {
        foundLayout = true;

        uint32_t packSize;
        uint32_t classSize;
        // Acquire the packing and class sizes for the type and cursor to the typedef entry.
        dnmd::row_reader entry_reader{entry.value()};
        if (!entry_reader.read(ClassLayout_PackingSize, &packSize)
            || !entry_reader.read(ClassLayout_ClassSize, &classSize))
        {
            return CLDB_E_FILE_CORRUPT;
        }

        *pdwPackSize = packSize;
        *pulClassSize = classSize;
    }

    auto typeEntry = dnmd::raw_cursor<mdtid_TypeDef>{_md_ptr.get(), td};
    if (!typeEntry)
        return CLDB_E_RECORD_NOTFOUND;

    // Get the list of field data
    dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr> fieldList{_md_ptr.get(), 0};
    uint32_t fieldListCount;
    if (!typeEntry.value().get_column_value_as_range(TypeDef_FieldList, fieldList, &fieldListCount))
        return CLDB_E_FILE_CORRUPT;

    *pcFieldOffset = fieldListCount;
    if (fieldListCount > 0)
    {
        // It is possible the table is empty and can therefore fail. This API permits this
        // behavior and sets the offset as -1 if this occurs.
        auto fieldLayoutBegin = dnmd::md_create_cursor<mdtid_FieldLayout>(_md_ptr.get());

        uint32_t readIn = 0;
        for (uint32_t i = 0; i < cMax; ++i)
        {
            COR_FIELD_OFFSET& offset = rFieldOffset[i];
            if (!fieldList || !fieldList.get_target_token(offset.ridOfField))
                return CLDB_E_FILE_CORRUPT;

            // See above comment about empty FieldLayout table.
            offset.ulOffset = (ULONG)-1;
            dnmd::raw_cursor<mdtid_FieldLayout> fieldLayoutRow{_md_ptr.get(), 0};
            if (fieldLayoutBegin
                && (fieldLayoutRow = dnmd::md_find_row_from_cursor(fieldLayoutBegin.value(), FieldLayout_Field, offset.ridOfField)))
            {
                (void)fieldLayoutRow.value().get_column_value(FieldLayout_Offset, (uint32_t*)&offset.ulOffset);
                foundLayout = true;
            }

            readIn++;
            if (readIn >= fieldListCount)
                break;

            if (!dnmd::md_cursor_next(fieldList))
                return CLDB_E_FILE_CORRUPT;
        }
    }

    if (!foundLayout)
        return CLDB_E_RECORD_NOTFOUND;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetFieldMarshal(
    mdToken     tk,
    PCCOR_SIGNATURE* ppvNativeType,
    ULONG* pcbNativeType)
{
    if (ExtractTokenType(tk) != mdtid_Param && ExtractTokenType(tk) != mdtid_Field)
        return E_INVALIDARG;

    dnmd::raw_cursor<mdtid_FieldMarshal> fieldMarshalRow{_md_ptr.get(), 0};
    auto cursor = dnmd::md_create_cursor<mdtid_FieldMarshal>(_md_ptr.get());
    if (!cursor
        || !(fieldMarshalRow = dnmd::md_find_row_from_cursor(cursor.value(), FieldMarshal_Parent, tk)))
    {
        return CLDB_E_RECORD_NOTFOUND;
    }

    uint8_t const* sig;
    uint32_t sigLen;
    if (!fieldMarshalRow.value().get_column_value(FieldMarshal_NativeType, &sig, &sigLen))
        return CLDB_E_FILE_CORRUPT;

    *ppvNativeType = (PCCOR_SIGNATURE)sig;
    *pcbNativeType = sigLen;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetRVA(
    mdToken     tk,
    ULONG* pulCodeRVA,
    DWORD* pdwImplFlags)
{
    uint32_t codeRVA = 0;
    uint32_t implFlags = 0;

    if (ExtractTokenType(tk) == mdtid_MethodDef)
    {
        auto cursor = dnmd::raw_cursor<mdtid_MethodDef>{_md_ptr.get(), tk};
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        dnmd::row_reader reader{cursor.value()};
        if (!reader.read(MethodDef_Rva, &codeRVA)
            || !reader.read(MethodDef_ImplFlags, &implFlags))
        {
            return CLDB_E_FILE_CORRUPT;
        }
    }
    else
    {
        if (ExtractTokenType(tk) != mdtid_Field)
            return E_INVALIDARG;

        auto cursor = dnmd::md_create_cursor<mdtid_FieldRva>(_md_ptr.get());
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;
        auto fieldRvaRow = dnmd::md_find_row_from_cursor(cursor.value(), FieldRva_Field, tk);
        if (!fieldRvaRow)
        {
            return CLDB_E_RECORD_NOTFOUND;
        }

        if (!fieldRvaRow.value().get_column_value(FieldRva_Rva, &codeRVA))
            return CLDB_E_FILE_CORRUPT;
    }

    if (pulCodeRVA != nullptr)
        *pulCodeRVA = (ULONG)codeRVA;

    if (pdwImplFlags != nullptr)
        *pdwImplFlags = (DWORD)implFlags;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetPermissionSetProps(
    mdPermission pm,
    DWORD* pdwAction,
    void const** ppvPermission,
    ULONG* pcbPermission)
{
    if (ExtractTokenType(pm) != mdtid_DeclSecurity)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_DeclSecurity>{_md_ptr.get(), pm};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    dnmd::row_reader reader{cursor.value()};
    if (pdwAction != nullptr
        && !reader.read(DeclSecurity_Action, (uint32_t*)pdwAction))
    {
        return CLDB_E_FILE_CORRUPT;
    }

    if (ppvPermission != nullptr
        && !reader.read(DeclSecurity_PermissionSet, (uint8_t const**)ppvPermission, (uint32_t*)pcbPermission))
    {
        return CLDB_E_FILE_CORRUPT;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetSigFromToken(
    mdSignature mdSig,
    PCCOR_SIGNATURE* ppvSig,
    ULONG* pcbSig)
{
    if (ExtractTokenType(mdSig) != mdtid_StandAloneSig || ppvSig == nullptr || pcbSig == nullptr)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_StandAloneSig>{_md_ptr.get(), mdSig};
    if (!cursor)
        return CLDB_E_INDEX_NOTFOUND;

    uint8_t const* sig;
    uint32_t sigLen;
    if (!cursor.value().get_column_value(StandAloneSig_Signature, &sig, &sigLen))
        return CLDB_E_FILE_CORRUPT;
    *ppvSig = (PCCOR_SIGNATURE)sig;
    *pcbSig = sigLen;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetModuleRefProps(
    mdModuleRef mur,
    _Out_writes_to_opt_(cchName, *pchName)
    LPWSTR      szName,
    ULONG       cchName,
    ULONG* pchName)
{
    if (ExtractTokenType(mur) != mdtid_ModuleRef)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_ModuleRef>{_md_ptr.get(), mur};
    if (!cursor)
        return CLDB_E_INDEX_NOTFOUND;

    char const* name;
    if (!cursor.value().get_column_value(ModuleRef_Name, &name))
        return CLDB_E_FILE_CORRUPT;
    return ConvertAndReturnStringOutput(name, szName, cchName, pchName);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumModuleRefs(
    HCORENUM* phEnum,
    mdModuleRef rModuleRefs[],
    ULONG       cMax,
    ULONG* pcModuleRefs)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        RETURN_IF_FAILED(CreateEnumTokens<mdtid_ModuleRef>(_md_ptr.get(), &enumImpl));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rModuleRefs, cMax, pcModuleRefs);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetTypeSpecFromToken(
    mdTypeSpec typespec,
    PCCOR_SIGNATURE* ppvSig,
    ULONG* pcbSig)
{
    if (ExtractTokenType(typespec) != mdtid_TypeSpec)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_TypeSpec>{_md_ptr.get(), typespec};
    if (!cursor
        || !cursor.value().get_column_value(TypeSpec_Signature, ppvSig, (uint32_t*)pcbSig))
    {
        return CLDB_E_RECORD_NOTFOUND;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetNameFromToken(            // Not Recommended! May be removed!
    mdToken     tk,
    MDUTF8CSTR* pszUtf8NamePtr)
{
    if (pszUtf8NamePtr == nullptr)
        return E_INVALIDARG;

    switch (ExtractTokenType(tk))
    {
    case mdtid_Module:
        return ReadName(_md_ptr.get(), tk, Module_Name, pszUtf8NamePtr);
    case mdtid_TypeRef:
        return ReadName(_md_ptr.get(), tk, TypeRef_TypeName, pszUtf8NamePtr);
    case mdtid_TypeDef:
        return ReadName(_md_ptr.get(), tk, TypeDef_TypeName, pszUtf8NamePtr);
    case mdtid_Field:
        return ReadName(_md_ptr.get(), tk, Field_Name, pszUtf8NamePtr);
    case mdtid_MethodDef:
        return ReadName(_md_ptr.get(), tk, MethodDef_Name, pszUtf8NamePtr);
    case mdtid_Param:
        return ReadName(_md_ptr.get(), tk, Param_Name, pszUtf8NamePtr);
    case mdtid_MemberRef:
        return ReadName(_md_ptr.get(), tk, MemberRef_Name, pszUtf8NamePtr);
    case mdtid_Event:
        return ReadName(_md_ptr.get(), tk, Event_Name, pszUtf8NamePtr);
    case mdtid_Property:
        return ReadName(_md_ptr.get(), tk, Property_Name, pszUtf8NamePtr);
    case mdtid_ModuleRef:
        return ReadName(_md_ptr.get(), tk, ModuleRef_Name, pszUtf8NamePtr);
    default:
        return E_INVALIDARG;
    }

}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumUnresolvedMethods(
    HCORENUM* phEnum,
    mdToken     rMethods[],
    ULONG       cMax,
    ULONG* pcTokens)
{
    UNREFERENCED_PARAMETER(phEnum);
    UNREFERENCED_PARAMETER(rMethods);
    UNREFERENCED_PARAMETER(cMax);
    UNREFERENCED_PARAMETER(pcTokens);

    // IMetaDataEmit only
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetUserString(
    mdString    stk,
    _Out_writes_to_opt_(cchString, *pchString)
    LPWSTR      szString,
    ULONG       cchString,
    ULONG* pchString)
{
    if (ExtractTokenType(stk) != ExtractTokenType(mdtString) || pchString == nullptr)
        return E_INVALIDARG;

    mduserstringcursor_t cursor = RidFromToken(stk);
    mduserstring_t string;
    uint32_t offset;
    if (!md_walk_user_string_heap(_md_ptr.get(), &cursor, &string, &offset))
        return CLDB_E_INDEX_NOTFOUND;

    // Strings in #US should have a trailing single byte.
    if (string.str_bytes % sizeof(WCHAR) == 0)
        return CLDB_E_FILE_CORRUPT;

    // Compute the string size in characters
    uint32_t retCharLen = (string.str_bytes - 1) / sizeof(WCHAR);
    *pchString = retCharLen;
    if (cchString > 0 && szString != nullptr)
    {
        uint32_t toCopyWChar = cchString < retCharLen ? cchString : retCharLen;
        ::memcpy(szString, string.str, toCopyWChar * sizeof(WCHAR));
        if (cchString < retCharLen)
        {
            ::memset(&szString[toCopyWChar - 1], 0, sizeof(WCHAR)); // Ensure null terminator
            return CLDB_S_TRUNCATION;
        }
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetPinvokeMap(
    mdToken     tk,
    DWORD* pdwMappingFlags,
    _Out_writes_to_opt_(cchImportName, *pchImportName)
    LPWSTR      szImportName,
    ULONG       cchImportName,
    ULONG* pchImportName,
    mdModuleRef* pmrImportDLL)
{
    if (ExtractTokenType(tk) != mdtid_MethodDef && ExtractTokenType(tk) != mdtid_Field)
        return E_INVALIDARG;

    dnmd::raw_cursor<mdtid_ImplMap> implRow{_md_ptr.get(), 0};
    auto cursor = dnmd::md_create_cursor<mdtid_ImplMap>(_md_ptr.get());
    if (!cursor
        || !(implRow = dnmd::md_find_row_from_cursor(cursor.value(), ImplMap_MemberForwarded, tk)))
    {
        return CLDB_E_RECORD_NOTFOUND;
    }

    uint32_t flags;
    dnmd::row_reader implRow_reader{implRow.value()};
    if (!implRow_reader.read(ImplMap_MappingFlags, &flags))
        return CLDB_E_FILE_CORRUPT;
    *pdwMappingFlags = flags;

    mdModuleRef token;
    if (!implRow_reader.read(ImplMap_ImportScope, token))
        return CLDB_E_FILE_CORRUPT;
    *pmrImportDLL = token;

    char const* importName;
    if (!implRow_reader.read(ImplMap_ImportName, &importName))
        return CLDB_E_FILE_CORRUPT;
    return ConvertAndReturnStringOutput(importName, szImportName, cchImportName, pchImportName);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumSignatures(
    HCORENUM* phEnum,
    mdSignature rSignatures[],
    ULONG       cMax,
    ULONG* pcSignatures)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        RETURN_IF_FAILED(CreateEnumTokens<mdtid_StandAloneSig>(_md_ptr.get(), &enumImpl));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rSignatures, cMax, pcSignatures);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumTypeSpecs(
    HCORENUM* phEnum,
    mdTypeSpec  rTypeSpecs[],
    ULONG       cMax,
    ULONG* pcTypeSpecs)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        RETURN_IF_FAILED(CreateEnumTokens<mdtid_TypeSpec>(_md_ptr.get(), &enumImpl));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rTypeSpecs, cMax, pcTypeSpecs);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumUserStrings(
    HCORENUM* phEnum,
    mdString    rStrings[],
    ULONG       cMax,
    ULONG* pcStrings)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));

        HCORENUMImpl_ptr cleanup{ enumImpl };
        mduserstring_t us;
        uint32_t offset;
        mduserstringcursor_t cursor = 0;
        for (;;)
        {
            if (!md_walk_user_string_heap(_md_ptr.get(), &cursor, &us, &offset))
                break;

            // Ignore strings that are of zero length.
            if (us.str_bytes == 0)
                continue;

            // Add mdtString token types to the enumeration.
            RETURN_IF_FAILED(HCORENUMImpl::AddToDynamicEnum(*enumImpl, RidToToken(offset, mdtString)));
        }

        *phEnum = cleanup.release();
    }
    return enumImpl->ReadTokens(rStrings, cMax, pcStrings);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetParamForMethodIndex(
    mdMethodDef md,
    ULONG       ulParamSeq,
    mdParamDef* ppd)
{
    if (ExtractTokenType(md) != mdtid_MethodDef || ulParamSeq == UINT32_MAX || ppd == nullptr)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_MethodDef>{_md_ptr.get(), md};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    dnmd::indirect_cursor<mdtid_Param, mdtid_ParamPtr> curr{_md_ptr.get(), 0};
    uint32_t count;
    if (!cursor.value().get_column_value_as_range(MethodDef_ParamList, curr, &count))
        return CLDB_E_FILE_CORRUPT;

    uint32_t seqMaybe;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!curr)
            return CLDB_E_FILE_CORRUPT;
        auto target = curr.value();
        if (!target.get_column_value(Param_Sequence, &seqMaybe))
            return CLDB_E_FILE_CORRUPT;

        if (ulParamSeq == seqMaybe)
        {
            if (!target.get_token(*ppd))
                return CLDB_E_FILE_CORRUPT;
            return S_OK;
        }

        // If the requested sequence value is less than what is returned,
        // we are done. Param sequences numbers are ordered - see II.22.33.
        if (ulParamSeq < seqMaybe)
            break;

        (void)dnmd::md_cursor_next(curr);
    }

    return CLDB_E_RECORD_NOTFOUND;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumCustomAttributes(
    HCORENUM* phEnum,
    mdToken     tk,
    mdToken     tkType,
    mdCustomAttribute rCustomAttributes[],
    ULONG       cMax,
    ULONG* pcCustomAttributes)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        uint32_t count;
        auto cursor = dnmd::md_create_cursor<mdtid_CustomAttribute>(_md_ptr.get(), &count);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        dnmd::raw_cursor<mdtid_CustomAttribute> curr{_md_ptr.get(), 0};
        uint32_t currCount;
        if (IsNilToken(tk))
        {
            // Caller is looking across all attributes
            assert(IsNilToken(tkType)); // Ignoring type filter
            RETURN_IF_FAILED(HCORENUMImpl::CreateTableEnum(_md_ptr.get(), 1, &enumImpl));
            HCORENUMImpl::InitTableEnum(*enumImpl, 0, cursor, count);
        }
        else
        {
            dnmd::range_result result = dnmd::md_find_range_from_cursor(cursor.value(), CustomAttribute_Parent, tk, curr, &currCount);
            if (IsNilToken(tkType) && result != dnmd::range_result::not_supported)
            {
                // Caller is looking for all associated attributes and we got a table range.
                if (result == dnmd::range_result::found)
                {
                    RETURN_IF_FAILED(HCORENUMImpl::CreateTableEnum(_md_ptr.get(), 1, &enumImpl));
                    HCORENUMImpl::InitTableEnum(*enumImpl, 0, curr, currCount);
                }
                else if (result == dnmd::range_result::not_found)
                {
                    // If there are no tokens found, create an empty enumeration.
                    RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));
                }
            }
            else
            {
                RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));

                HCORENUMImpl_ptr cleanup{ enumImpl };

                // We need to search across the entire table as we couldn't get a range.
                curr = cursor;
                currCount = count;

                // Read in for matching in bulk
                mdToken toMatch[64];
                mdToken matchedTk;
                uint32_t i = 0;
                while (i < currCount)
                {
                    int32_t read = curr.value().get_column_values(CustomAttribute_Parent, ARRAY_SIZE(toMatch), toMatch);
                    if (read == 0)
                        break;

                    if (read < 0)
                        return CLDB_E_FILE_CORRUPT;
                    for (int32_t j = 0; j < read; ++j)
                    {
                        if (toMatch[j] == tkType)
                        {
                            if (!curr.get_token(matchedTk))
                                return CLDB_E_FILE_CORRUPT;
                            RETURN_IF_FAILED(HCORENUMImpl::AddToDynamicEnum(*enumImpl, matchedTk));
                        }
                        (void)dnmd::md_cursor_next(curr);
                    }
                    i += read;
                }
                enumImpl = cleanup.release();
            }
        }
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rCustomAttributes, cMax, pcCustomAttributes);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetCustomAttributeProps(
    mdCustomAttribute cv,
    mdToken* ptkObj,
    mdToken* ptkType,
    void const** ppBlob,
    ULONG* pcbSize)
{
    if (ExtractTokenType(cv) != mdtid_CustomAttribute)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_CustomAttribute>{_md_ptr.get(), cv};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    mdToken obj;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(CustomAttribute_Parent, obj))
        return CLDB_E_FILE_CORRUPT;
    *ptkObj = obj;

    mdToken type;
    if (!reader.read(CustomAttribute_Type, type))
        return CLDB_E_FILE_CORRUPT;
    *ptkType = type;

    uint8_t const* blob;
    uint32_t blobLen;
    if (!reader.read(CustomAttribute_Value, &blob, &blobLen))
        return CLDB_E_FILE_CORRUPT;
    *ppBlob = blob;
    *pcbSize = blobLen;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::FindTypeRef(
    mdToken     tkResolutionScope,
    LPCWSTR     szName,
    mdTypeRef* ptr)
{
    uint32_t count;
    auto cursor = dnmd::md_create_cursor<mdtid_TypeRef>(_md_ptr.get(), &count);
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    pal::StringConvert<WCHAR, char> cvt{ szName };
    if (!cvt.Success())
        return E_INVALIDARG;

    char const* nspace;
    char const* name;
    SplitTypeName(cvt, &nspace, &name);

    bool scopeIsSet = !IsNilToken(tkResolutionScope);
    mdToken resMaybe;
    char const* str;
    for (uint32_t i = 0; i < count; (void)dnmd::md_cursor_next(cursor), ++i)
    {
        dnmd::row_reader reader{cursor.value()};
        if (!reader.read(TypeRef_ResolutionScope, resMaybe))
            return CLDB_E_FILE_CORRUPT;

        // See if the Resolution scopes match.
        if ((IsNilToken(resMaybe) && scopeIsSet)    // User didn't state scope.
            || resMaybe != tkResolutionScope)       // Match user scope.
        {
            continue;
        }

        if (!reader.read(TypeRef_TypeNamespace, &str))
            return CLDB_E_FILE_CORRUPT;

        if (0 != ::strcmp(nspace, str))
            continue;

        if (!reader.read(TypeRef_TypeName, &str))
            return CLDB_E_FILE_CORRUPT;

        if (0 == ::strcmp(name, str))
        {
            if (!cursor.get_token(*ptr))
                return CLDB_E_FILE_CORRUPT;
            return S_OK;
        }
    }

    // Not found.
    return CLDB_E_RECORD_NOTFOUND;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetMemberProps(
    mdToken     mb,
    mdTypeDef* pClass,
    _Out_writes_to_opt_(cchMember, *pchMember)
    LPWSTR      szMember,
    ULONG       cchMember,
    ULONG* pchMember,
    DWORD* pdwAttr,
    PCCOR_SIGNATURE* ppvSigBlob,
    ULONG* pcbSigBlob,
    ULONG* pulCodeRVA,
    DWORD* pdwImplFlags,
    DWORD* pdwCPlusTypeFlag,
    UVCP_CONSTANT* ppValue,
    ULONG* pcchValue)
{
    if (ExtractTokenType(mb) == mdtid_MethodDef)
    {
        return GetMethodProps(
            mb,
            pClass,
            szMember,
            cchMember,
            pchMember,
            pdwAttr,
            ppvSigBlob,
            pcbSigBlob,
            pulCodeRVA,
            pdwImplFlags);
    }

    if (ExtractTokenType(mb) == mdtid_Field)
    {
        return GetFieldProps(
            mb,
            pClass,
            szMember,
            cchMember,
            pchMember,
            pdwAttr,
            ppvSigBlob,
            pcbSigBlob,
            pdwCPlusTypeFlag,
            ppValue,
            pcchValue);
    }

    return E_INVALIDARG;
}

namespace
{
    HRESULT FindConstant(
        mdhandle_view ptr,
        uint32_t lookupValue,
        DWORD& cnstCorType,
        UVCP_CONSTANT& cnst,
        ULONG& cnstLen)
    {
        assert(ptr != nullptr);

        dnmd::raw_cursor<mdtid_Constant> constantPropCursor{ptr.get(), 0};
        uint32_t corType;
        uint8_t const* defaultValue;
        uint32_t defaultValueLen;
        auto constantCursor = dnmd::md_create_cursor<mdtid_Constant>(ptr.get());
        if (!constantCursor
            || !(constantPropCursor = dnmd::md_find_row_from_cursor(constantCursor.value(), Constant_Parent, lookupValue)))
        {
            corType = ELEMENT_TYPE_VOID;
            defaultValue = nullptr;
            defaultValueLen = 0;
        }
        else
        {
            dnmd::row_reader constantPropCursor_reader{constantPropCursor.value()};
            if (!constantPropCursor_reader.read(Constant_Type, &corType))
                return CLDB_E_FILE_CORRUPT;
            if (!constantPropCursor_reader.read(Constant_Value, &defaultValue, &defaultValueLen))
                return CLDB_E_FILE_CORRUPT;
        }

        cnstCorType = corType;
        cnst = (UVCP_CONSTANT)defaultValue;
        cnstLen = corType == ELEMENT_TYPE_STRING
            ? defaultValueLen / sizeof(WCHAR)
            : 0; // Only string return a non-zero length for the constant value.
        return S_OK;
    }
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetFieldProps(
    mdFieldDef  mb,
    mdTypeDef* pClass,
    _Out_writes_to_opt_(cchField, *pchField)
    LPWSTR      szField,
    ULONG       cchField,
    ULONG* pchField,
    DWORD* pdwAttr,
    PCCOR_SIGNATURE* ppvSigBlob,
    ULONG* pcbSigBlob,
    DWORD* pdwCPlusTypeFlag,
    UVCP_CONSTANT* ppValue,
    ULONG* pcchValue)
{
    if (ExtractTokenType(mb) != mdtid_Field)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_Field>{_md_ptr.get(), mb};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    mdTypeDef classDef;
    if (!cursor.value().get_parent_token(classDef))
        return CLDB_E_RECORD_NOTFOUND;
    *pClass = classDef;

    uint32_t flags;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(Field_Flags, &flags))
        return CLDB_E_FILE_CORRUPT;
    *pdwAttr = flags;

    uint8_t const* sig;
    uint32_t sigLen;
    if (!reader.read(Field_Signature, &sig, &sigLen))
        return CLDB_E_FILE_CORRUPT;
    *ppvSigBlob = (PCCOR_SIGNATURE)sig;
    *pcbSigBlob = sigLen;

    HRESULT hr;
    RETURN_IF_FAILED(FindConstant(_md_ptr, mb, *pdwCPlusTypeFlag, *ppValue, *pcchValue));

    char const* name;
    if (!reader.read(Field_Name, &name))
        return CLDB_E_FILE_CORRUPT;
    return ConvertAndReturnStringOutput(name, szField, cchField, pchField);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetPropertyProps(
    mdProperty  prop,
    mdTypeDef* pClass,
    // Should be defined as _Out_writes_to_opt_(cchProperty, *pchProperty) and non-const. Mistake from initial release.
    LPCWSTR     szProperty,
    ULONG       cchProperty,
    ULONG* pchProperty,
    DWORD* pdwPropFlags,
    PCCOR_SIGNATURE* ppvSig,
    ULONG* pbSig,
    DWORD* pdwCPlusTypeFlag,
    UVCP_CONSTANT* ppDefaultValue,
    ULONG* pcchDefaultValue,
    mdMethodDef* pmdSetter,
    mdMethodDef* pmdGetter,
    mdMethodDef rmdOtherMethod[],
    ULONG       cMax,
    ULONG* pcOtherMethod)
{
    if (ExtractTokenType(prop) != mdtid_Property)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_Property>{_md_ptr.get(), prop};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    mdTypeDef classDef;
    if (!cursor.value().get_parent_token(classDef))
        return CLDB_E_RECORD_NOTFOUND;
    *pClass = classDef;

    uint32_t flags;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(Property_Flags, &flags))
        return CLDB_E_FILE_CORRUPT;
    *pdwPropFlags = flags;

    uint8_t const* sig;
    uint32_t sigLen;
    if (!reader.read(Property_Type, &sig, &sigLen))
        return CLDB_E_FILE_CORRUPT;
    *ppvSig = sig;
    *pbSig = sigLen;

    HRESULT hr;
    RETURN_IF_FAILED(FindConstant(_md_ptr, prop, *pdwCPlusTypeFlag, *ppDefaultValue, *pcchDefaultValue));

    uint32_t methodSemCount;
    auto methodSemCursor = dnmd::md_create_cursor<mdtid_MethodSemantics>(_md_ptr.get(), &methodSemCount);
    if (!methodSemCursor)
        return CLDB_E_RECORD_NOTFOUND;

    struct _Finder
    {
        mdMethodDef Setter;
        mdMethodDef Getter;
        mdMethodDef* Other;
        uint32_t const OtherLen;
        uint32_t OtherCount;
        HRESULT Result; // Result of the operation

        bool operator()(dnmd::cursor<mdtid_MethodSemantics> c)
        {
            mdMethodDef tk;
            uint32_t semantics;
            dnmd::row_reader reader{c};
            if (!reader.read(MethodSemantics_Method, tk)
                || !reader.read(MethodSemantics_Semantics, &semantics))
            {
                Result = CLDB_E_FILE_CORRUPT;
                return true; // Failure detected, so stop.
            }
            switch (semantics)
            {
            case msSetter: Setter = tk;
                break;
            case msGetter: Getter = tk;
                break;
            case msOther:
                if (OtherCount < OtherLen)
                    Other[OtherCount] = tk;
                OtherCount++;
                break;
            default:
                assert(!"Unknown semantic");
            }
            return false;
        }
    } finder{ mdMethodDefNil, mdMethodDefNil, rmdOtherMethod, cMax, 0, S_OK };

    RETURN_IF_FAILED(EnumTableRange(methodSemCursor.value(), methodSemCount, MethodSemantics_Association, prop, finder));

    RETURN_IF_FAILED(finder.Result);

    *pmdSetter = finder.Setter;
    *pmdGetter = finder.Getter;
    *pcOtherMethod = finder.OtherCount;

    char const* name;
    if (!reader.read(Property_Name, &name))
        return CLDB_E_FILE_CORRUPT;

    // The const_cast<> is needed because the signature incorrectly expresses the
    // desired semantics. This has been wrong since .NET Framework 1.0.
    return ConvertAndReturnStringOutput(name, const_cast<WCHAR*>(szProperty), cchProperty, pchProperty);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetParamProps(
    mdParamDef  tk,
    mdMethodDef* pmd,
    ULONG* pulSequence,
    _Out_writes_to_opt_(cchName, *pchName)
    LPWSTR      szName,
    ULONG       cchName,
    ULONG* pchName,
    DWORD* pdwAttr,
    DWORD* pdwCPlusTypeFlag,
    UVCP_CONSTANT* ppValue,
    ULONG* pcchValue)
{
    if (ExtractTokenType(tk) != mdtid_Param)
        return E_INVALIDARG;

    auto cursor = dnmd::raw_cursor<mdtid_Param>{_md_ptr.get(), tk};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    mdMethodDef methodDef;
    if (!cursor.value().get_parent_token(methodDef))
        return CLDB_E_FILE_CORRUPT;
    *pmd = methodDef;

    uint32_t seq;
    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(Param_Sequence, &seq))
        return CLDB_E_FILE_CORRUPT;
    *pulSequence = seq;

    uint32_t flags;
    if (!reader.read(Param_Flags, &flags))
        return CLDB_E_FILE_CORRUPT;
    *pdwAttr = flags;

    HRESULT hr;
    RETURN_IF_FAILED(FindConstant(_md_ptr, tk, *pdwCPlusTypeFlag, *ppValue, *pcchValue));

    char const* name;
    if (!reader.read(Param_Name, &name))
        return CLDB_E_FILE_CORRUPT;
    return ConvertAndReturnStringOutput(name, szName, cchName, pchName);
}

namespace
{
    // See TypeSpec definition at II.23.2.14
    HRESULT ExtractTypeDefRefFromSpec(uint8_t const* specBlob, uint32_t specBlobLen, mdToken& tk)
    {
        assert(specBlob != nullptr);
        if (specBlobLen == 0)
            return COR_E_BADIMAGEFORMAT;

        PCCOR_SIGNATURE sig = specBlob;
        PCCOR_SIGNATURE sigEnd = specBlob + specBlobLen;

        ULONG data;
        sig += CorSigUncompressData(sig, &data);

        while (sig < sigEnd
            && (CorIsModifierElementType((CorElementType)data)
                || data == ELEMENT_TYPE_GENERICINST))
        {
            sig += CorSigUncompressData(sig, &data);
        }

        if (sig >= sigEnd)
            return COR_E_BADIMAGEFORMAT;

        if (data == ELEMENT_TYPE_VALUETYPE || data == ELEMENT_TYPE_CLASS)
        {
            if (mdTokenNil == CorSigUncompressToken(sig, &tk))
                return COR_E_BADIMAGEFORMAT;
            return S_OK;
        }

        tk = mdTokenNil;
        return S_FALSE;
    }

    HRESULT ResolveTypeDefRefSpecToName(mdhandle_t handle, mdToken typeTk, char const** nspace, char const** name)
    {
        assert(nspace != nullptr && name != nullptr);

        for (;;)
        {
            uint32_t tokenType = ExtractTokenType(typeTk);
            if (tokenType != mdtid_TypeDef && tokenType != mdtid_TypeRef && tokenType != mdtid_TypeSpec)
            {
                assert(!"Unexpected token in ResolveTypeDefRefSpecToName");
                return E_FAIL;
            }
            auto cursor = dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>{handle, typeTk};
            HRESULT hr = S_OK;
            bool isSpec = false;
            if (!cursor.visit([&](auto row) {
                if constexpr (dnmd::cursor_tableid_v<decltype(row)> == mdtid_TypeSpec)
                {
                    isSpec = true;
                    uint8_t const* specBlob;
                    uint32_t specBlobLen;
                    if (!row.get_column_value(TypeSpec_Signature, &specBlob, &specBlobLen))
                        hr = CLDB_E_FILE_CORRUPT;
                    else
                        hr = ExtractTypeDefRefFromSpec(specBlob, specBlobLen, typeTk);
                }
                else
                {
                    dnmd::row_reader reader{row};
                    hr = ReadTypeName(reader, nspace, name) ? S_OK : CLDB_E_FILE_CORRUPT;
                }
            }))
                return CLDB_E_FILE_CORRUPT;
            if (FAILED(hr) || hr == S_FALSE || !isSpec)
                return hr;
        }
    }
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetCustomAttributeByName(
    mdToken     tkObj,
    LPCWSTR     szName,
    void const** ppData,
    ULONG* pcbData)
{
    if (szName == nullptr || ppData == nullptr || pcbData == nullptr)
        return E_INVALIDARG;

    uint32_t count;
    auto cursor = dnmd::md_create_cursor<mdtid_CustomAttribute>(_md_ptr.get(), &count);
    if (!cursor)
        return S_FALSE; // If no custom attributes are defined, treat it the same as if the attribute is not found.

    char buffer[1024];
    pal::StringConvert<WCHAR, char> cvt{ szName, buffer };
    if (!cvt.Success())
        return E_INVALIDARG;

    struct
    {
        HRESULT hr;
        void const** ppData;
        ULONG* pcbData;
        pal::StringConvert<WCHAR, char> const& cvt;
        mdhandle_t Handle;

        bool operator()(dnmd::cursor<mdtid_CustomAttribute> custAttr)
        {
            char const* nspace;
            char const* name;

            mdToken tgtType = mdTokenNil;
            size_t len;
            char const* curr;

            decltype(CustomAttribute_Type)::value_type type{Handle, 0};
            dnmd::row_reader custAttr_reader{custAttr};
            if (!custAttr_reader.read(CustomAttribute_Type, type))
            {
                hr = CLDB_E_FILE_CORRUPT;
                return true;
            }

            // Resolve the cursor based on its type using visit().
            if (!type.visit([&](auto typeCursor) {
                if constexpr (dnmd::cursor_tableid_v<decltype(typeCursor)> == mdtid_MethodDef)
                {
                    if (!typeCursor.get_parent_token(tgtType))
                    {
                        hr = CLDB_E_FILE_CORRUPT;
                    }
                }
                else if constexpr (dnmd::cursor_tableid_v<decltype(typeCursor)> == mdtid_MemberRef)
                {
                    if (!typeCursor.get_column_value(MemberRef_Class, tgtType))
                    {
                        hr = CLDB_E_FILE_CORRUPT;
                    }
                }
            }))
            {
                hr = CLDB_E_FILE_CORRUPT;
                return true;
            }

            if (FAILED(hr))
                return true;

            hr = ResolveTypeDefRefSpecToName(Handle, tgtType, &nspace, &name);
            if (hr != S_OK)
            {
                return true;
            }
            else
            {
                hr = S_FALSE;
                curr = cvt;
                if (nspace[0] != '\0')
                {
                    len = ::strlen(nspace);
                    if (0 != ::strncmp(cvt, nspace, len))
                        return false;
                    curr += len;

                    // Check for overrun and next character
                    if (cvt.Length() <= len || curr[0] != '.')
                        return false;
                    curr += 1;
                }

                if (0 == ::strcmp(curr, name))
                {
                    uint8_t const* data;
                    uint32_t dataLen;
                    if (!custAttr_reader.read(CustomAttribute_Value, &data, &dataLen))
                    {
                        hr = CLDB_E_FILE_CORRUPT;
                        return true;
                    }
                    *ppData = data;
                    *pcbData = dataLen;
                    hr = S_OK;
                    return true;
                }
            }
            return false;
        }
    } finder {S_FALSE, ppData, pcbData, cvt, _md_ptr.get()};

    HRESULT result = EnumTableRange(cursor.value(), count, CustomAttribute_Parent, tkObj, finder);
    if (FAILED(result))
        finder.hr = result;

    if (finder.hr != S_OK)
    {
        *ppData = nullptr;
        *pcbData = 0;
    }
    return finder.hr;
}

BOOL STDMETHODCALLTYPE MetadataImportRO::IsValidToken(
    mdToken     tk)
{
    // If we can create a cursor, the token is valid.
    return dnmd::md_is_valid_token(_md_ptr.get(), tk)
        ? TRUE
        : FALSE;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetNestedClassProps(
    mdTypeDef   tdNestedClass,
    mdTypeDef* ptdEnclosingClass)
{
    if (ExtractTokenType(tdNestedClass) != mdtid_TypeDef)
        return E_INVALIDARG;

    auto cursor = dnmd::md_create_cursor<mdtid_NestedClass>(_md_ptr.get());
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    auto nestedClassRow = dnmd::md_find_row_from_cursor(cursor.value(), NestedClass_NestedClass, tdNestedClass);
    if (!nestedClassRow)
    {
        return CLDB_E_RECORD_NOTFOUND;
    }

    dnmd::raw_cursor<mdtid_TypeDef> enclosingClass{_md_ptr.get(), 0};
    if (!nestedClassRow.value().get_column_value(NestedClass_EnclosingClass, enclosingClass))
        return CLDB_E_FILE_CORRUPT;

    if (!enclosingClass.get_token(*ptdEnclosingClass))
        return CLDB_E_FILE_CORRUPT;
    return S_OK;
}

namespace
{
    uint32_t const FoundValue = 0;
    uint32_t const InvalidReadCount = (uint32_t)-1;

    struct ReadSigContext
    {
        mdhandle_t Handle;
        CorPinvokeMap PinvokeCallConv;
    };

    // II.23.2.8 TypeDefOrRefOrSpecEncoded as potential calling convention
    uint32_t ReadTypeDefOrRefOrSpecEncodedAsCallConv(PCCOR_SIGNATURE sig, ReadSigContext& cxt)
    {
        mdToken tk;
        uint32_t readIn = CorSigUncompressToken(sig, &tk);
        if (IsNilToken(tk) || ExtractTokenType(tk) == mdtid_TypeSpec)
            return readIn;

        // See if this token is a calling convention.
        uint32_t tkType = ExtractTokenType(tk);
        if (tkType != mdtid_TypeRef && tkType != mdtid_TypeDef)
            return readIn;

        auto cursor = dnmd::raw_cursor<mdtid_TypeRef, mdtid_TypeDef>{cxt.Handle, tk};
        uint32_t result = readIn;
        if (!cursor.visit([&](auto row) {
            dnmd::row_reader reader{row};
            char const* nspace;
            if (!ReadTypeName(reader, &nspace, nullptr))
            {
                result = InvalidReadCount;
                return;
            }
            if (0 == ::strcmp(nspace, CMOD_CALLCONV_NAMESPACE) || 0 == ::strcmp(nspace, CMOD_CALLCONV_NAMESPACE_OLD))
            {
                char const* name;
                if (!ReadTypeName(reader, nullptr, &name))
                {
                    result = InvalidReadCount;
                    return;
                }
                if (0 == ::strcmp(name, CMOD_CALLCONV_NAME_CDECL))
                    cxt.PinvokeCallConv = pmCallConvCdecl;
                else if (0 == ::strcmp(name, CMOD_CALLCONV_NAME_STDCALL))
                    cxt.PinvokeCallConv = pmCallConvStdcall;
                else if (0 == ::strcmp(name, CMOD_CALLCONV_NAME_THISCALL))
                    cxt.PinvokeCallConv = pmCallConvThiscall;
                else if (0 == ::strcmp(name, CMOD_CALLCONV_NAME_FASTCALL))
                    cxt.PinvokeCallConv = pmCallConvFastcall;
                else
                    return;
                result = FoundValue;
            }
        }))
            return InvalidReadCount;
        return result;
    }

    // Handles processing the following metadata signature rules.
    //
    // S := MethodDefSig | MethodRefSig | StandAloneMethodSig
    // II.23.2.1  MethodDefSig : = (DEFAULT | VARARG | (GENERIC GenParamCount)) ParamCount RetType Param*
    // II.23.2.2  MethodRefSig : = (DEFAULT | (GENERIC GenParamCount)) ParamCount RetType Param*
    //  | VARARG ParamCount RetType Param* (SENTINEL Param+)?
    // II.23.2.3  StandAloneMethodSig : = (DEFAULT | STDCALL | THISCALL | FASTCALL) ParamCount RetType Param*
    //  | (VARARG | C) ParamCount RetType Param* (SENTINEL Param+)?
    //
    // II.23.2.11 RetType := CustomMod* (BYREF? Type | TYPEDBYREF | VOID)
    // II.23.2.10 Param := CustomMod* (BYREF? Type | TYPEDBYREF)
    // II.23.2.7  CustomMod := (CMOD_OPT | CMOD_REQD) TypeDefOrRefOrSpecEncoded
    // II.23.2.12 Type := BOOLEAN | CHAR | I1 | U1 | I2 | U2 | I4 | U4 | I8 | U8 | R4 | R8 | I | U
    //  | ARRAY Type ArrayShape
    //  | CLASS TypeDefOrRefOrSpecEncoded
    //  | FNPTR MethodDefSig
    //  | FNPTR MethodRefSig
    //  | GENERICINST (CLASS | VALUETYPE) TypeDefOrRefOrSpecEncoded GenArgCount Type*
    //  | MVAR Number
    //  | OBJECT
    //  | PTR CustomMod* Type
    //  | PTR CustomMod* VOID
    //  | STRING
    //  | SZARRAY CustomMod* Type
    //  | VALUETYPE TypeDefOrRefOrSpecEncoded
    //  | VAR Number
    // II.23.2.13 ArrayShape := Rank NumSizes Size* NumLoBounds LoBound*
    //
    // II.23.2.8  TypeDefOrRefOrSpecEncoded := mdToken
    // GenArgCount := uint32
    // ParamCount := uint32
    // Number := uint32
    // Rank := uint32
    // NumSizes := uint32
    // Size := uint32
    // NumLoBounds := uint32
    // LoBound := int32
    uint32_t ReadCorType_CallConv(PCCOR_SIGNATURE sig, PCCOR_SIGNATURE sigEnd, ReadSigContext& cxt)
    {
#define RETURN_IF_NOT_ADVANCE(stmt) \
{\
    if (sigCurr >= sigEnd) return InvalidReadCount; \
    cnt = stmt; \
    if (cnt <= FoundValue) return cnt; \
    sigCurr += cnt; \
}

        assert(sig != nullptr && sigEnd != nullptr);

        PCCOR_SIGNATURE sigCurr = sig;

        // Process modifiers (e.g., PTR or BYREF) and VARARG sentinel values - see MethodDefSig and MethodRefSig.
        CorElementType corType = CorSigUncompressElementType(sigCurr);
        while (CorIsModifierElementType(corType) || corType == ELEMENT_TYPE_SENTINEL)
            corType = CorSigUncompressElementType(sigCurr);

        // Read in CorType
        uint32_t cnt = 0;
        ULONG data;
        ULONG tmp;
        int32_t stmp;
        mdToken tk;
        switch (corType)
        {
        case ELEMENT_TYPE_SZARRAY:
            RETURN_IF_NOT_ADVANCE(ReadCorType_CallConv(sigCurr, sigEnd, cxt)); // CustomMod* Type
            break;
        case ELEMENT_TYPE_VAR:
        case ELEMENT_TYPE_MVAR:
            RETURN_IF_NOT_ADVANCE(CorSigUncompressData(sigCurr, &data)); // Number
            break;
        case ELEMENT_TYPE_GENERICINST:
            RETURN_IF_NOT_ADVANCE(ReadCorType_CallConv(sigCurr, sigEnd, cxt)); // (CLASS | VALUETYPE) TypeDefOrRefOrSpecEncoded
            RETURN_IF_NOT_ADVANCE(CorSigUncompressData(sigCurr, &data)); // GenArgCount
            for (uint32_t i = 0; i < data; ++i)
                RETURN_IF_NOT_ADVANCE(ReadCorType_CallConv(sigCurr, sigEnd, cxt)); // Type*
            break;
        case ELEMENT_TYPE_FNPTR: // MethodDefSig | MethodRefSig
            RETURN_IF_NOT_ADVANCE(CorSigUncompressData(sigCurr, &data)); // Metadata calling convention
            RETURN_IF_NOT_ADVANCE(CorSigUncompressData(sigCurr, &data)); // ParamCount
            RETURN_IF_NOT_ADVANCE(ReadCorType_CallConv(sigCurr, sigEnd, cxt)); // RetType
            for (uint32_t i = 0; i < data; ++i)
                RETURN_IF_NOT_ADVANCE(ReadCorType_CallConv(sigCurr, sigEnd, cxt)); // Type*
            break;
        case ELEMENT_TYPE_ARRAY:
            RETURN_IF_NOT_ADVANCE(ReadCorType_CallConv(sigCurr, sigEnd, cxt)); // Type
            RETURN_IF_NOT_ADVANCE(CorSigUncompressData(sigCurr, &data)); // Rank
            RETURN_IF_NOT_ADVANCE(CorSigUncompressData(sigCurr, &data)); // NumSizes
            for (uint32_t i = 0; i < data; ++i)
                RETURN_IF_NOT_ADVANCE(CorSigUncompressData(sigCurr, &tmp)); // Size*
            RETURN_IF_NOT_ADVANCE(CorSigUncompressData(sigCurr, &data)); // NumLoBounds
            for (uint32_t i = 0; i < data; ++i)
                RETURN_IF_NOT_ADVANCE(CorSigUncompressSignedInt(sigCurr, &stmp)); // LoBounds*
            break;
        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
            RETURN_IF_NOT_ADVANCE(CorSigUncompressToken(sigCurr, &tk)); // TypeDefOrRefOrSpecEncoded
            break;
        case ELEMENT_TYPE_CMOD_REQD:
        case ELEMENT_TYPE_CMOD_OPT:
            RETURN_IF_NOT_ADVANCE(ReadTypeDefOrRefOrSpecEncodedAsCallConv(sigCurr, cxt)); // TypeDefOrRefOrSpecEncoded
            RETURN_IF_NOT_ADVANCE(ReadCorType_CallConv(sigCurr, sigEnd, cxt)); // (Target) Type
            break;
        default:
            break;
        }

        if (sigCurr > sigEnd)
            return InvalidReadCount;

        return (uint32_t)(sigCurr - sig);
#undef RETURN_IF_NOT_ADVANCE
    }
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetNativeCallConvFromSig(
    void const* pvSig,
    ULONG       cbSig,
    ULONG* pCallConv)
{
    if (cbSig == 0 || pCallConv == nullptr)
        return E_INVALIDARG;

    PCCOR_SIGNATURE sig = (PCCOR_SIGNATURE)pvSig;
    PCCOR_SIGNATURE sigEnd = sig + cbSig;
    ULONG callConv; // Metadata callconv position value, not the expected return type.

    // Signature processing specified in ECMA-335 sections:
    //  - II.23.2.1 MethodDefSig
    //  - II.23.2.2 MethodRefSig
    //  - II.23.2.3 StandAloneMethodSig - Primary signature containing native calling conventions
    //
    uint32_t cnt = CorSigUncompressData(sig, &callConv);
    if (cnt == InvalidReadCount)
        return CORSEC_E_INVALID_IMAGE_FORMAT;

    PCCOR_SIGNATURE sigTypeArgs = sig + cnt;
    PCCOR_SIGNATURE sigArgCount = sigTypeArgs;

    // Check for generic signature, defined in II.23.2.1.
    if (callConv & IMAGE_CEE_CS_CALLCONV_GENERIC)
    {
        ULONG typeArgs;
        cnt = CorSigUncompressData(sigTypeArgs, &typeArgs);
        if (cnt == InvalidReadCount)
            return CORSEC_E_INVALID_IMAGE_FORMAT;
        sigArgCount = sigTypeArgs + cnt;
    }

    // Read in signature arg count.
    assert(sigArgCount < sigEnd);
    ULONG argCount;
    cnt = CorSigUncompressData(sigArgCount, &argCount);
    if (cnt == InvalidReadCount)
        return CORSEC_E_INVALID_IMAGE_FORMAT;

    ReadSigContext cxt{ _md_ptr.get(), pmCallConvWinapi };

    PCCOR_SIGNATURE sigRetType = sigArgCount + cnt;
    assert(sigRetType < sigEnd);
    // Process the return type - II.23.2.11.
    cnt = ReadCorType_CallConv(sigRetType, sigEnd, cxt);
    if (cnt == InvalidReadCount)
        return CORSEC_E_INVALID_IMAGE_FORMAT;

    // Check if calling convention was found. If not
    // found, continue looking on the arguments.
    if (cnt != 0)
    {
        PCCOR_SIGNATURE sigArgs = sigRetType + cnt;
        PCCOR_SIGNATURE sigCurrArg = sigArgs;
        // Process arguments - II.23.2.10.
        for (uint32_t i = 0; i < argCount; ++i)
        {
            assert(sigCurrArg < sigEnd);

            cnt = ReadCorType_CallConv(sigCurrArg, sigEnd, cxt);
            if (cnt == InvalidReadCount)
                return CORSEC_E_INVALID_IMAGE_FORMAT;

            if (cnt == FoundValue)
                goto Done;

            // Move to the next argument
            sigCurrArg = sigCurrArg + cnt;
        }
    }
Done:
    *pCallConv = cxt.PinvokeCallConv;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::IsGlobal(
    mdToken     pd,
    int* pbGlobal)
{
    if (IsValidToken(pd) != TRUE)
        return E_INVALIDARG;

    mdToken parent;
    BOOL result;
    switch (ExtractTokenType(pd))
    {
    case mdtid_TypeDef:
        result = pd == MD_GLOBAL_PARENT_TOKEN ? TRUE : FALSE;
        break;
    case mdtid_Field:
    case mdtid_MethodDef:
    case mdtid_Event:
    case mdtid_Property:
    {
        auto cursor = dnmd::raw_cursor<mdtid_Field, mdtid_MethodDef, mdtid_Event, mdtid_Property>{_md_ptr.get(), pd};
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;
        bool found = false;
        if (!cursor.visit([&](auto row) { found = row.get_parent_token(parent); }) || !found)
            return CLDB_E_FILE_CORRUPT;
        result = parent == MD_GLOBAL_PARENT_TOKEN ? TRUE : FALSE;
        break;
    }
    default:
        result = FALSE;
    }

    *pbGlobal = result;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumGenericParams(
    HCORENUM* phEnum,
    mdToken      tk,
    mdGenericParam rGenericParams[],
    ULONG       cMax,
    ULONG* pcGenericParams)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        uint32_t count;
        auto cursor = dnmd::md_create_cursor<mdtid_GenericParam>(_md_ptr.get(), &count);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));
        HCORENUMImpl_ptr cleanup{ enumImpl };

        struct _Finder
        {
            HCORENUMImpl& EnumImpl;
            mdToken Token;
            HRESULT hr;
            HRESULT Result; // Result of the operation

            bool operator()(dnmd::cursor<mdtid_GenericParam> c)
            {
                if (!c.get_token(Token))
                {
                    Result = CLDB_E_FILE_CORRUPT;
                    return true;
                }
                if (FAILED(hr = HCORENUMImpl::AddToDynamicEnum(EnumImpl, Token)))
                {
                    Result = hr;
                    return true;
                }

                return false;
            }
        } finder{ *enumImpl, mdTokenNil, S_OK, S_OK };

        RETURN_IF_FAILED(EnumTableRange(cursor.value(), count, GenericParam_Owner, tk, finder));
        RETURN_IF_FAILED(finder.Result);

        *phEnum = cleanup.release();
    }
    return enumImpl->ReadTokens(rGenericParams, cMax, pcGenericParams);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetGenericParamProps(
    mdGenericParam gp,
    ULONG* pulParamSeq,
    DWORD* pdwParamFlags,
    mdToken* ptOwner,
    DWORD* reserved,
    _Out_writes_to_opt_(cchName, *pchName)
    LPWSTR       wzname,
    ULONG        cchName,
    ULONG* pchName)
{
    UNREFERENCED_PARAMETER(reserved);

    auto cursor = dnmd::raw_cursor<mdtid_GenericParam>{_md_ptr.get(), gp};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(GenericParam_Owner, *ptOwner))
        return CLDB_E_FILE_CORRUPT;

    uint32_t sequenceNumber;
    if (!reader.read(GenericParam_Number, &sequenceNumber))
        return CLDB_E_FILE_CORRUPT;
    *pulParamSeq = sequenceNumber;

    uint32_t paramFlags;
    if (!reader.read(GenericParam_Flags, &paramFlags))
        return CLDB_E_FILE_CORRUPT;
    *pdwParamFlags = paramFlags;

    char const* name;
    if (!reader.read(GenericParam_Name, &name))
        return CLDB_E_FILE_CORRUPT;

    return ConvertAndReturnStringOutput(name, wzname, cchName, pchName);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetMethodSpecProps(
    mdMethodSpec mi,
    mdToken* tkParent,
    PCCOR_SIGNATURE* ppvSigBlob,
    ULONG* pcbSigBlob)
{
    auto cursor = dnmd::raw_cursor<mdtid_MethodSpec>{_md_ptr.get(), mi};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(MethodSpec_Method, *tkParent))
        return CLDB_E_FILE_CORRUPT;

    uint8_t const* sig;
    uint32_t sigLen;
    if (!reader.read(MethodSpec_Instantiation, &sig, &sigLen))
        return CLDB_E_FILE_CORRUPT;

    *ppvSigBlob = (PCCOR_SIGNATURE)sig;
    *pcbSigBlob = sigLen;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumGenericParamConstraints(
    HCORENUM* phEnum,
    mdGenericParam tk,
    mdGenericParamConstraint rGenericParamConstraints[],
    ULONG       cMax,
    ULONG* pcGenericParamConstraints)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        uint32_t count;
        auto cursor = dnmd::md_create_cursor<mdtid_GenericParamConstraint>(_md_ptr.get(), &count);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));
        HCORENUMImpl_ptr cleanup{ enumImpl };

        struct _Finder
        {
            HCORENUMImpl& EnumImpl;
            mdToken Token;
            HRESULT hr;
            HRESULT Result; // Result of the operation

            bool operator()(dnmd::cursor<mdtid_GenericParamConstraint> c)
            {
                if (!c.get_token(Token))
                {
                    Result = CLDB_E_FILE_CORRUPT;
                    return true;
                }
                if (FAILED(hr = HCORENUMImpl::AddToDynamicEnum(EnumImpl, Token)))
                {
                    Result = hr;
                    return true;
                }

                return false;
            }
        } finder{ *enumImpl, mdTokenNil, S_OK, S_OK };

        RETURN_IF_FAILED(EnumTableRange(cursor.value(), count, GenericParamConstraint_Owner, tk, finder));
        RETURN_IF_FAILED(finder.Result);

        *phEnum = cleanup.release();
    }
    return enumImpl->ReadTokens(rGenericParamConstraints, cMax, pcGenericParamConstraints);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetGenericParamConstraintProps(
    mdGenericParamConstraint gpc,
    mdGenericParam* ptGenericParam,
    mdToken* ptkConstraintType)
{
    auto cursor = dnmd::raw_cursor<mdtid_GenericParamConstraint>{_md_ptr.get(), gpc};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    dnmd::row_reader reader{cursor.value()};
    if (!reader.read(GenericParamConstraint_Owner, *ptGenericParam))
        return CLDB_E_FILE_CORRUPT;

    if (!reader.read(GenericParamConstraint_Constraint, *ptkConstraintType))
        return CLDB_E_FILE_CORRUPT;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetPEKind(
    DWORD* pdwPEKind,
    DWORD* pdwMAchine)
{
    UNREFERENCED_PARAMETER(pdwPEKind);
    UNREFERENCED_PARAMETER(pdwMAchine);

    // Requires PE data to be available.
    // This implementation only has the metadata tables.
    // It does not have any information about the PE envelope.
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetVersionString(
    _Out_writes_to_opt_(ccBufSize, *pccBufSize)
    LPWSTR      pwzBuf,
    DWORD       ccBufSize,
    DWORD* pccBufSize)
{
    char const* versionString = md_get_version_string(_md_ptr.get());
    if (versionString == nullptr)
        versionString = "";
    return ConvertAndReturnStringOutput(versionString, pwzBuf, ccBufSize, pccBufSize);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumMethodSpecs(
    HCORENUM* phEnum,
    mdToken      tk,
    mdMethodSpec rMethodSpecs[],
    ULONG       cMax,
    ULONG* pcMethodSpecs)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        uint32_t count;
        auto cursor = dnmd::md_create_cursor<mdtid_MethodSpec>(_md_ptr.get(), &count);
        if (!cursor)
            return CLDB_E_RECORD_NOTFOUND;

        RETURN_IF_FAILED(HCORENUMImpl::CreateDynamicEnum(&enumImpl));
        HCORENUMImpl_ptr cleanup{ enumImpl };

        struct _Finder
        {
            HCORENUMImpl& EnumImpl;
            mdToken Token;
            HRESULT hr;
            HRESULT Result; // Result of the operation

            bool operator()(dnmd::cursor<mdtid_MethodSpec> c)
            {
                if (!c.get_token(Token))
                {
                    Result = CLDB_E_FILE_CORRUPT;
                    return true;
                }
                if (FAILED(hr = HCORENUMImpl::AddToDynamicEnum(EnumImpl, Token)))
                {
                    Result = hr;
                    return true;
                }

                return false;
            }
        } finder{ *enumImpl, mdTokenNil, S_OK, S_OK };

        RETURN_IF_FAILED(EnumTableRange(cursor.value(), count, MethodSpec_Method, tk, finder));
        RETURN_IF_FAILED(finder.Result);

        *phEnum = cleanup.release();
    }
    return enumImpl->ReadTokens(rMethodSpecs, cMax, pcMethodSpecs);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetAssemblyProps(
    mdAssembly  mda,
    void const  **ppbPublicKey,
    ULONG       *pcbPublicKey,
    ULONG       *pulHashAlgId,
    _Out_writes_to_opt_(cchName, *pchName)
    LPWSTR      szName,
    ULONG       cchName,
    ULONG       *pchName,
    ASSEMBLYMETADATA *pMetaData,
    DWORD       *pdwAssemblyFlags)
{

    auto cursor = dnmd::raw_cursor<mdtid_Assembly>{_md_ptr.get(), mda};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    dnmd::row_reader reader{cursor.value()};
    if (ppbPublicKey != nullptr)
    {
        uint8_t const* publicKey;
        uint32_t publicKeySize;
        if (!reader.read(Assembly_PublicKey, &publicKey, &publicKeySize))
            return CLDB_E_FILE_CORRUPT;

        *ppbPublicKey = publicKey;
        *pcbPublicKey = publicKeySize;
    }

    if (pulHashAlgId != nullptr)
    {
        uint32_t hashAlgId;
        if (!reader.read(Assembly_HashAlgId, &hashAlgId))
            return CLDB_E_FILE_CORRUPT;
        *pulHashAlgId = hashAlgId;
    }

    if (pMetaData != nullptr)
    {
        uint32_t majorVersion;
        uint32_t minorVersion;
        uint32_t buildNumber;
        uint32_t patchNumber;
        if (!reader.read(Assembly_MajorVersion, &majorVersion))
            return CLDB_E_FILE_CORRUPT;

        if (!reader.read(Assembly_MinorVersion, &minorVersion))
            return CLDB_E_FILE_CORRUPT;

        if (!reader.read(Assembly_BuildNumber, &buildNumber))
            return CLDB_E_FILE_CORRUPT;

        if (!reader.read(Assembly_RevisionNumber, &patchNumber))
            return CLDB_E_FILE_CORRUPT;

        pMetaData->usMajorVersion = static_cast<uint16_t>(majorVersion);
        pMetaData->usMinorVersion = static_cast<uint16_t>(minorVersion);
        pMetaData->usBuildNumber = static_cast<uint16_t>(buildNumber);
        pMetaData->usRevisionNumber = static_cast<uint16_t>(patchNumber);

        char const* culture;
        if (!reader.read(Assembly_Culture, &culture))
            return CLDB_E_FILE_CORRUPT;

        HRESULT hr;
        RETURN_IF_FAILED(ConvertAndReturnStringOutput(culture, pMetaData->szLocale, pMetaData->cbLocale, &pMetaData->cbLocale));

        // We do not read the AssemblyOS or AssemblyProcessor tables to fill these arrays since neither .NET Framework or CoreCLR do.
        pMetaData->ulProcessor = 0;
        pMetaData->ulOS = 0;
    }

    if (pdwAssemblyFlags != nullptr)
    {
        uint32_t assemblyFlags;
        if (!reader.read(Assembly_Flags, &assemblyFlags))
            return CLDB_E_FILE_CORRUPT;

        uint8_t const* publicKey;
        uint32_t publicKeySize;
        if (!reader.read(Assembly_PublicKey, &publicKey, &publicKeySize))
            return CLDB_E_FILE_CORRUPT;
        if (publicKeySize != 0)
            assemblyFlags |= afPublicKey;

        *pdwAssemblyFlags = assemblyFlags;
    }

    if (szName != nullptr || pchName != nullptr)
    {
        char const* name;
        if (!reader.read(Assembly_Name, &name))
            return CLDB_E_FILE_CORRUPT;

        return ConvertAndReturnStringOutput(name, szName, cchName, pchName);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetAssemblyRefProps(
    mdAssemblyRef mdar,
    void const  **ppbPublicKeyOrToken,
    ULONG       *pcbPublicKeyOrToken,
    _Out_writes_to_opt_(cchName, *pchName)
    LPWSTR      szName,
    ULONG       cchName,
    ULONG       *pchName,
    ASSEMBLYMETADATA *pMetaData,
    void const  **ppbHashValue,
    ULONG       *pcbHashValue,
    DWORD       *pdwAssemblyRefFlags)
{

    auto cursor = dnmd::raw_cursor<mdtid_AssemblyRef>{_md_ptr.get(), mdar};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    dnmd::row_reader reader{cursor.value()};
    if (ppbPublicKeyOrToken != nullptr)
    {
        uint8_t const* publicKeyOrToken;
        uint32_t publicKeyOrTokenSize;
        if (!reader.read(AssemblyRef_PublicKeyOrToken, &publicKeyOrToken, &publicKeyOrTokenSize))
            return CLDB_E_FILE_CORRUPT;

        *ppbPublicKeyOrToken = publicKeyOrToken;
        *pcbPublicKeyOrToken = publicKeyOrTokenSize;
    }

    if (ppbHashValue != nullptr)
    {
        uint8_t const* hashValue;
        uint32_t hashValueSize;
        if (!reader.read(AssemblyRef_HashValue, &hashValue, &hashValueSize))
            return CLDB_E_FILE_CORRUPT;
        *ppbHashValue = hashValue;
        *pcbHashValue = hashValueSize;
    }

    if (pMetaData != nullptr)
    {
        uint32_t majorVersion;
        uint32_t minorVersion;
        uint32_t buildNumber;
        uint32_t patchNumber;
        if (!reader.read(AssemblyRef_MajorVersion, &majorVersion))
            return CLDB_E_FILE_CORRUPT;

        if (!reader.read(AssemblyRef_MinorVersion, &minorVersion))
            return CLDB_E_FILE_CORRUPT;

        if (!reader.read(AssemblyRef_BuildNumber, &buildNumber))
            return CLDB_E_FILE_CORRUPT;

        if (!reader.read(AssemblyRef_RevisionNumber, &patchNumber))
            return CLDB_E_FILE_CORRUPT;

        pMetaData->usMajorVersion = static_cast<uint16_t>(majorVersion);
        pMetaData->usMinorVersion = static_cast<uint16_t>(minorVersion);
        pMetaData->usBuildNumber = static_cast<uint16_t>(buildNumber);
        pMetaData->usRevisionNumber = static_cast<uint16_t>(patchNumber);

        char const* culture;
        if (!reader.read(AssemblyRef_Culture, &culture))
            return CLDB_E_FILE_CORRUPT;

        HRESULT hr;
        RETURN_IF_FAILED(ConvertAndReturnStringOutput(culture, pMetaData->szLocale, pMetaData->cbLocale, &pMetaData->cbLocale));

        // We do not read the AssemblyRefOS or AssemblyRefProcessor tables to fill these arrays since neither .NET Framework or CoreCLR do.
        pMetaData->ulProcessor = 0;
        pMetaData->ulOS = 0;
    }

    if (pdwAssemblyRefFlags != nullptr)
    {
        uint32_t assemblyRefFlags;
        if (!reader.read(AssemblyRef_Flags, &assemblyRefFlags))
            return CLDB_E_FILE_CORRUPT;

        *pdwAssemblyRefFlags = assemblyRefFlags;
    }

    if (szName != nullptr || pchName != nullptr)
    {
        char const* name;
        if (!reader.read(AssemblyRef_Name, &name))
            return CLDB_E_FILE_CORRUPT;

        return ConvertAndReturnStringOutput(name, szName, cchName, pchName);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetFileProps(
    mdFile      mdf,
    _Out_writes_to_opt_(cchName, *pchName)
    LPWSTR      szName,
    ULONG       cchName,
    ULONG       *pchName,
    void const  **ppbHashValue,
    ULONG       *pcbHashValue,
    DWORD       *pdwFileFlags)
{

    auto cursor = dnmd::raw_cursor<mdtid_File>{_md_ptr.get(), mdf};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    dnmd::row_reader reader{cursor.value()};
    if (ppbHashValue != nullptr)
    {
        uint8_t const* hashValue;
        uint32_t hashValueSize;
        if (!reader.read(File_HashValue, &hashValue, &hashValueSize))
            return CLDB_E_FILE_CORRUPT;
        *ppbHashValue = hashValue;
        *pcbHashValue = hashValueSize;
    }

    if (pdwFileFlags != nullptr)
    {
        uint32_t fileFlags;
        if (!reader.read(File_Flags, &fileFlags))
            return CLDB_E_FILE_CORRUPT;

        *pdwFileFlags = fileFlags;
    }

    if (szName != nullptr || pchName != nullptr)
    {
        char const* name;
        if (!reader.read(File_Name, &name))
            return CLDB_E_FILE_CORRUPT;

        return ConvertAndReturnStringOutput(name, szName, cchName, pchName);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetExportedTypeProps(
    mdExportedType   mdct,
    _Out_writes_to_opt_(cchName, *pchName)
    LPWSTR      szName,
    ULONG       cchName,
    ULONG       *pchName,
    mdToken     *ptkImplementation,
    mdTypeDef   *ptkTypeDef,
    DWORD       *pdwExportedTypeFlags)
{

    auto cursor = dnmd::raw_cursor<mdtid_ExportedType>{_md_ptr.get(), mdct};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    dnmd::row_reader reader{cursor.value()};
    if (ptkImplementation != nullptr)
    {
        if (!reader.read(ExportedType_Implementation, *ptkImplementation))
            return CLDB_E_FILE_CORRUPT;
    }

    if (ptkTypeDef != nullptr)
    {
        // This column points into the TypeDef table of another module,
        // so it isn't a true column reference here.
        if (!reader.read(ExportedType_TypeDefId, ptkTypeDef))
            return CLDB_E_FILE_CORRUPT;
    }

    if (pdwExportedTypeFlags != nullptr)
    {
        uint32_t exportedTypeFlags;
        if (!reader.read(ExportedType_Flags, &exportedTypeFlags))
            return CLDB_E_FILE_CORRUPT;

        *pdwExportedTypeFlags = exportedTypeFlags;
    }

    if (szName != nullptr || pchName != nullptr)
    {
        char const* name;
        if (!reader.read(ExportedType_TypeName, &name))
            return CLDB_E_FILE_CORRUPT;

        char const* nspace;
        if (!reader.read(ExportedType_TypeNamespace, &nspace))
            return CLDB_E_FILE_CORRUPT;

        HRESULT hr;
        malloc_ptr<char> mem;
        RETURN_IF_FAILED(ConstructTypeName(nspace, name, mem));

        return ConvertAndReturnStringOutput((char*)mem.get(), szName, cchName, pchName);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetManifestResourceProps(
    mdManifestResource  mdmr,
    _Out_writes_to_opt_(cchName, *pchName)
    LPWSTR      szName,
    ULONG       cchName,
    ULONG       *pchName,
    mdToken     *ptkImplementation,
    DWORD       *pdwOffset,
    DWORD       *pdwResourceFlags)
{

    auto cursor = dnmd::raw_cursor<mdtid_ManifestResource>{_md_ptr.get(), mdmr};
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    dnmd::row_reader reader{cursor.value()};
    if (ptkImplementation != nullptr)
    {
        if (!reader.read(ManifestResource_Implementation, *ptkImplementation))
            return CLDB_E_FILE_CORRUPT;
    }

    if (pdwOffset != nullptr)
    {
        uint32_t offset;
        if (!reader.read(ManifestResource_Offset, &offset))
            return CLDB_E_FILE_CORRUPT;

        *pdwOffset = offset;
    }

    if (pdwResourceFlags != nullptr)
    {
        uint32_t resourceFlags;
        if (!reader.read(ManifestResource_Flags, &resourceFlags))
            return CLDB_E_FILE_CORRUPT;

        *pdwResourceFlags = resourceFlags;
    }
    if (szName != nullptr || pchName != nullptr)
    {
        char const* name;
        if (!reader.read(ManifestResource_Name, &name))
            return CLDB_E_FILE_CORRUPT;

        return ConvertAndReturnStringOutput(name, szName, cchName, pchName);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumAssemblyRefs(
    HCORENUM    *phEnum,
    mdAssemblyRef rAssemblyRefs[],
    ULONG       cMax,
    ULONG       *pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        RETURN_IF_FAILED(CreateEnumTokens<mdtid_AssemblyRef>(_md_ptr.get(), &enumImpl));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rAssemblyRefs, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumFiles(
    HCORENUM    *phEnum,
    mdFile      rFiles[],
    ULONG       cMax,
    ULONG       *pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        RETURN_IF_FAILED(CreateEnumTokens<mdtid_File>(_md_ptr.get(), &enumImpl));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rFiles, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumExportedTypes(
    HCORENUM    *phEnum,
    mdExportedType   rExportedTypes[],
    ULONG       cMax,
    ULONG       *pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        RETURN_IF_FAILED(CreateEnumTokens<mdtid_ExportedType>(_md_ptr.get(), &enumImpl));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rExportedTypes, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::EnumManifestResources(
    HCORENUM    *phEnum,
    mdManifestResource  rManifestResources[],
    ULONG       cMax,
    ULONG       *pcTokens)
{
    HRESULT hr;
    HCORENUMImpl* enumImpl = ToHCORENUMImpl(*phEnum);
    if (enumImpl == nullptr)
    {
        RETURN_IF_FAILED(CreateEnumTokens<mdtid_ManifestResource>(_md_ptr.get(), &enumImpl));
        *phEnum = enumImpl;
    }
    return enumImpl->ReadTokens(rManifestResources, cMax, pcTokens);
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::GetAssemblyFromScope(
    mdAssembly  *ptkAssembly)
{
    auto cursor = dnmd::md_create_cursor<mdtid_Assembly>(_md_ptr.get());
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;
    if (!cursor.get_token(*ptkAssembly))
        return CLDB_E_FILE_CORRUPT;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::FindExportedTypeByName(
    LPCWSTR     szName,
    mdToken     mdtExportedType,
    mdExportedType   *ptkExportedType)
{
    if (szName == nullptr)
        return E_INVALIDARG;

    uint32_t count;
    auto cursor = dnmd::md_create_cursor<mdtid_ExportedType>(_md_ptr.get(), &count);
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    pal::StringConvert<WCHAR, char> cvt{ szName };
    if (!cvt.Success())
        return E_INVALIDARG;
    char const* nspace;
    char const* name;
    SplitTypeName(cvt, &nspace, &name);

    for (uint32_t i = 0; i < count; dnmd::md_cursor_next(cursor), i++)
    {
        mdToken implementation;
        dnmd::row_reader reader{cursor.value()};
        if (!reader.read(ExportedType_Implementation, implementation))
            return CLDB_E_FILE_CORRUPT;

        // Handle the case of nested vs. non-nested classes
        if (ExtractTokenType(implementation) == mdtid_ExportedType && !IsNilToken(implementation))
        {
            // Current ExportedType being looked at is a nested type, so
            // comparing the implementation token.
            if (implementation != mdtExportedType)
                continue;
        }
        else if (CreateTokenType(ExtractTokenType(mdtExportedType)) == mdtExportedType
                && !IsNilToken(mdtExportedType))
        {
            // ExportedType passed in is nested but the current ExportedType is not.
            continue;
        }

        char const* recordNspace;
        if (!reader.read(ExportedType_TypeNamespace, &recordNspace))
            return CLDB_E_FILE_CORRUPT;

        if (::strcmp(nspace, recordNspace) != 0)
            continue;

        char const* recordName;
        if (!reader.read(ExportedType_TypeName, &recordName))
            return CLDB_E_FILE_CORRUPT;

        if (::strcmp(name, recordName) != 0)
            continue;

        if (!cursor.get_token(*ptkExportedType))
            return CLDB_E_FILE_CORRUPT;
        return S_OK;
    }
    return CLDB_E_RECORD_NOTFOUND;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::FindManifestResourceByName(
    LPCWSTR     szName,
    mdManifestResource *ptkManifestResource)
{
    if (szName == nullptr)
        return E_INVALIDARG;

    uint32_t count;
    auto cursor = dnmd::md_create_cursor<mdtid_ManifestResource>(_md_ptr.get(), &count);
    if (!cursor)
        return CLDB_E_RECORD_NOTFOUND;

    pal::StringConvert<WCHAR, char> cvt{ szName };
    if (!cvt.Success())
        return E_INVALIDARG;

    for (uint32_t i = 0; i < count; dnmd::md_cursor_next(cursor), i++)
    {
        mdManifestResource token;
        if (!cursor.get_token(token))
            return CLDB_E_FILE_CORRUPT;

        char const* name;
        if (!cursor.value().get_column_value(ManifestResource_Name, &name))
            return CLDB_E_FILE_CORRUPT;

        if (::strncmp(name, cvt, cvt.Length()) == 0)
        {
            *ptkManifestResource = token;
            return S_OK;
        }
    }
    return CLDB_E_RECORD_NOTFOUND;
}

HRESULT STDMETHODCALLTYPE MetadataImportRO::FindAssembliesByName(
    LPCWSTR  szAppBase,
    LPCWSTR  szPrivateBin,
    LPCWSTR  szAssemblyName,
    IUnknown *ppIUnk[],
    ULONG    cMax,
    ULONG    *pcAssemblies)
{
    UNREFERENCED_PARAMETER(szAppBase);
    UNREFERENCED_PARAMETER(szPrivateBin);
    UNREFERENCED_PARAMETER(szAssemblyName);
    UNREFERENCED_PARAMETER(ppIUnk);
    UNREFERENCED_PARAMETER(cMax);
    UNREFERENCED_PARAMETER(pcAssemblies);
    // Requires VM knowledge and is only supported in .NET Framework.
    return E_NOTIMPL;
}
