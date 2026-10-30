#include "hcorenum.hpp"
#include <cassert>
#include <new>

namespace
{
    bool ResolveIndirectCursor(dnmd::any_cursor const& source, dnmd::any_cursor& target) noexcept
    {
        auto sentinel = source;
        mdToken token;
        bool resolved;
        switch (dnmd::detail::CursorTable(source)->table_id)
        {
        case mdtid_FieldPtr:
            sentinel = source.as<mdtid_Field>();
            resolved = dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr>{source}.get_target_token(token);
            break;
        case mdtid_MethodPtr:
            sentinel = source.as<mdtid_MethodDef>();
            resolved = dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>{source}.get_target_token(token);
            break;
        case mdtid_ParamPtr:
            sentinel = source.as<mdtid_Param>();
            resolved = dnmd::indirect_cursor<mdtid_Param, mdtid_ParamPtr>{source}.get_target_token(token);
            break;
        case mdtid_EventPtr:
            sentinel = source.as<mdtid_Event>();
            resolved = dnmd::indirect_cursor<mdtid_Event, mdtid_EventPtr>{source}.get_target_token(token);
            break;
        case mdtid_PropertyPtr:
            sentinel = source.as<mdtid_Property>();
            resolved = dnmd::indirect_cursor<mdtid_Property, mdtid_PropertyPtr>{source}.get_target_token(token);
            break;
        default:
            target = source;
            return true;
        }
        if (!resolved)
            return false;
        // Token lookup accepts live rows only; retain resolved nil/end identities.
        uint32_t row = (dnmd::detail::RidFromToken)(token);
        if (row == 0)
        {
            target = sentinel;
        }
        else if (row == dnmd::detail::CursorTable(sentinel)->row_count + 1)
        {
            if (!sentinel.move(static_cast<int32_t>(row)))
                return false;
            target = sentinel;
        }
        else
        {
            target = dnmd::any_cursor{source.get_handle(), token};
        }
        return true;
    }
}

#define RETURN_IF_FAILED(exp) \
{ \
    hr = (exp); \
    if (FAILED(hr)) \
    { \
        return hr; \
    } \
}

HRESULT HCORENUMImpl::CreateTableEnum(_In_ mdhandle_t handle, _In_ uint32_t count, _Out_ HCORENUMImpl** impl) noexcept
{
    assert(impl != nullptr && count > 0);

    HCORENUMImpl* enumImpl;
    enumImpl = (HCORENUMImpl*)::malloc(sizeof(*enumImpl) + (sizeof(enumImpl->_data) * (count - 1)));
    if (enumImpl == nullptr)
        return E_OUTOFMEMORY;

    new (enumImpl) HCORENUMImpl(handle);

    // Immediately set the return.
    *impl = enumImpl;

    // Initialize the linked list of EnumData.
    EnumData* currInit = enumImpl->_curr;
    currInit->Next = nullptr;

    // -1 because the initial impl contains one.
    unsigned char* nextStorage = reinterpret_cast<unsigned char*>(enumImpl) + sizeof(*enumImpl);
    for (size_t i = 0; i < (count - 1); ++i)
    {
        EnumData* nextMaybe = new (nextStorage) EnumData(handle);
        currInit->Next = nextMaybe;
        currInit = nextMaybe;
        currInit->Next = nullptr;
        enumImpl->_last = currInit;
        nextStorage += sizeof(EnumData);
    }

    return S_OK;
}

void HCORENUMImpl::InitTableEnum(_Inout_ HCORENUMImpl& impl, _In_ uint32_t index, _In_ dnmd::any_cursor cursor, _In_ uint32_t rows) noexcept
{
    assert(impl._type == HCORENUMType::Table);
    EnumData* currInit = &impl._data;
    for (uint32_t i = 0; i < index; ++i)
    {
        assert(currInit->Next != nullptr);
        currInit = currInit->Next;
    }

    currInit->Table.Current = cursor;
    currInit->Table.Start = cursor;
    currInit->ReadIn = 0;
    currInit->Total = rows;
}

HRESULT HCORENUMImpl::CreateDynamicEnum(_Out_ HCORENUMImpl** impl, _In_ uint32_t entrySpan) noexcept
{
    assert(impl != nullptr && entrySpan > 0);

    HCORENUMImpl* enumImpl;
    enumImpl = (HCORENUMImpl*)::malloc(sizeof(*enumImpl));
    if (enumImpl == nullptr)
        return E_OUTOFMEMORY;

    new (enumImpl) HCORENUMImpl(entrySpan);

    // Immediately set the return.
    *impl = enumImpl;

    // The page must be a multiple of the entrySpan for reading to be efficient.
    assert(ARRAY_SIZE(enumImpl->_data.Dynamic.Page) % entrySpan == 0);
    return S_OK;
}

HRESULT HCORENUMImpl::AddToDynamicEnum(_Inout_ HCORENUMImpl& impl, uint32_t value) noexcept
{
    assert(impl._type == HCORENUMType::Dynamic);

    // Check if we have exhausted the last page
    EnumData* currData = impl._last;
    if (currData->Total >= ARRAY_SIZE(currData->Dynamic.Page))
    {
        EnumData* newData = (EnumData*)::malloc(sizeof(EnumData));
        if (newData == nullptr)
            return E_OUTOFMEMORY;

        new (newData) EnumData();
        assert(currData->Next == nullptr);
        currData->Next = newData;
        impl._last = newData;
        currData = impl._last;
    }
    currData->Dynamic.Page[currData->Total] = value;
    currData->Total++;
    return S_OK;
}

void HCORENUMImpl::Destroy(_In_ HCORENUMImpl* impl) noexcept
{
    assert(impl != nullptr);
    EnumData* toDelete = impl->_data.Next;
    if (impl->_type == HCORENUMType::Table)
        impl->_data.Table.~TableData();
    while (toDelete != nullptr)
    {
        EnumData* next = toDelete->Next;
        if (impl->_type == HCORENUMType::Table)
            toDelete->Table.~TableData();
        toDelete->~EnumData();
        if (impl->_type == HCORENUMType::Dynamic)
            ::free(toDelete);
        toDelete = next;
    }

    impl->~HCORENUMImpl();
    ::free(impl);
}

uint32_t HCORENUMImpl::Count() const noexcept
{
    // Accumulate all tables in the enumerator
    uint32_t count = 0;
    EnumData const* curr = &_data;
    do
    {
        count += curr->Total;
        curr = curr->Next;
    }
    while (curr != nullptr);

    return count / _entrySpan;
}

HRESULT HCORENUMImpl::ReadTokens(
    mdToken rTokens[],
    ULONG cMax,
    ULONG* pcTokens) noexcept
{
    HRESULT hr;
    uint32_t tokenCount = 0;
    if (cMax == 1)
    {
        hr = ReadOneToken(rTokens[0], tokenCount);
    }
    else
    {
        hr = (_type == HCORENUMType::Table)
            ? ReadTableTokens(rTokens, cMax, tokenCount)
            : ReadDynamicTokens(rTokens, cMax, tokenCount);
    }

    if (pcTokens != nullptr)
        *pcTokens = tokenCount;

    return hr;
}

HRESULT HCORENUMImpl::ReadTokenPairs(
    mdToken rTokens1[],
    mdToken rTokens2[],
    ULONG cMax,
    ULONG* pcTokens) noexcept
{
    assert(_type == HCORENUMType::Dynamic);
    assert(rTokens1 != nullptr && rTokens2 != nullptr && pcTokens != nullptr);
    assert(_entrySpan == 2);

    EnumData* currData = _curr;
    if (currData == nullptr)
        return S_FALSE;

    uint32_t count = 0;
    for (uint32_t i = 0; i < cMax; ++i)
    {
        // Check if all values have been read.
        while (currData->ReadIn == currData->Total)
        {
            currData = currData->Next;
            // Check next link in enumerator list
            if (currData == nullptr)
                goto Done;
            _curr = currData;
        }

        assert(((currData->Total - currData->ReadIn) % 2) == 0);
        rTokens1[count] = currData->Dynamic.Page[currData->ReadIn++];
        rTokens2[count] = currData->Dynamic.Page[currData->ReadIn++];
        count++;
    }
Done:
    *pcTokens = count;
    return S_OK;
}

HRESULT HCORENUMImpl::Reset(_In_ ULONG position) noexcept
{
    return (_type == HCORENUMType::Table)
        ? ResetTableEnum(position)
        : ResetDynamicEnum(position);
}

HRESULT HCORENUMImpl::ReadOneToken(mdToken& rToken, uint32_t& count) noexcept
{
    EnumData* currData = _curr;
    while (currData->ReadIn == currData->Total)
    {
        currData = currData->Next;
        // Check next link in enumerator list
        if (currData == nullptr)
            return S_FALSE;
        _curr = currData;
    }

    if (_type == HCORENUMType::Table)
    {
        dnmd::any_cursor current{currData->Table.Current};
        if (!ResolveIndirectCursor(currData->Table.Current, current))
            return CLDB_E_FILE_CORRUPT;

        if (!current.get_token(rToken))
            return S_FALSE;

        currData->Table.Current.move(1);
    }
    else
    {
        rToken = currData->Dynamic.Page[currData->ReadIn];
    }

    currData->ReadIn++;
    count = 1;
    return S_OK;
}

HRESULT HCORENUMImpl::ReadTableTokens(
    mdToken rTokens[],
    uint32_t cMax,
    uint32_t& tokenCount) noexcept
{
    assert(_type == HCORENUMType::Table);
    assert(rTokens != nullptr);

    EnumData* currData = _curr;
    if (currData == nullptr)
        return S_FALSE;

    uint32_t count = 0;
    for (uint32_t i = 0; i < cMax; ++i)
    {
        // Check if all values have been read.
        while (currData->ReadIn == currData->Total)
        {
            currData = currData->Next;
            // Check next link in enumerator list
            if (currData == nullptr)
                goto Done;
            _curr = currData;
        }

        dnmd::any_cursor current{currData->Table.Current};
        if (!ResolveIndirectCursor(currData->Table.Current, current))
            return CLDB_E_FILE_CORRUPT;

        if (!current.get_token(rTokens[count]))
            break;
        count++;

        if (!currData->Table.Current.move(1))
            break;
        currData->ReadIn++;
    }
Done:
    tokenCount = count;
    return S_OK;
}

HRESULT HCORENUMImpl::ReadDynamicTokens(
    mdToken rTokens[],
    uint32_t cMax,
    uint32_t& tokenCount) noexcept
{
    assert(_type == HCORENUMType::Dynamic);
    assert(rTokens != nullptr);

    EnumData* currData = _curr;
    if (currData == nullptr)
        return S_FALSE;

    uint32_t count = 0;
    for (uint32_t i = 0; i < cMax; ++i)
    {
        // Check if all values have been read.
        while (currData->ReadIn == currData->Total)
        {
            currData = currData->Next;
            // Check next link in enumerator list
            if (currData == nullptr)
                goto Done;
            _curr = currData;
        }

        rTokens[count] = currData->Dynamic.Page[currData->ReadIn];
        currData->ReadIn++;
        count++;
    }
Done:
    tokenCount = count;
    return S_OK;
}

HRESULT HCORENUMImpl::ResetTableEnum(_In_ uint32_t position) noexcept
{
    assert(_type == HCORENUMType::Table);

    uint32_t newReadIn;
    bool reset = false;
    EnumData* currData = &_data;
    while (currData != nullptr)
    {
        dnmd::any_cursor newStart{currData->Table.Start};
        if (reset)
        {
            // Reset the enumerator state
            newReadIn = 0;
        }
        else if (position < currData->Total)
        {
            // The current enumerator contains the position
            if (!newStart.move(position))
                return E_INVALIDARG;
            newReadIn = position;
            reset = true;

            // Update the current state of the enumerator
            _curr = currData;
        }
        else
        {
            // The current enumerator is consumed based on position
            position -= currData->Total;
            if (!newStart.move(currData->Total))
                return E_INVALIDARG;
            newReadIn = currData->Total;
        }

        currData->Table.Current = newStart;
        currData->ReadIn = newReadIn;
        currData = currData->Next;
    }

    return S_OK;
}

HRESULT HCORENUMImpl::ResetDynamicEnum(_In_ uint32_t position) noexcept
{
    assert(_type == HCORENUMType::Dynamic);

    uint32_t newReadIn;
    bool reset = false;
    EnumData* currData = &_data;
    while (currData != nullptr)
    {
        if (reset)
        {
            // Reset the enumerator state
            newReadIn = 0;
        }
        else if (position < currData->Total)
        {
            newReadIn = position;
            reset = true;

            // Update the current state of the enumerator
            _curr = currData;
        }
        else
        {
            // The current enumerator is consumed based on position
            position -= currData->Total;
            newReadIn = currData->Total;
        }

        currData->ReadIn = newReadIn;
        currData = currData->Next;
    }

    return S_OK;
}
