#ifndef _SRC_INTERFACES_DNMDOWNER_HPP_
#define _SRC_INTERFACES_DNMDOWNER_HPP_

#include "internal/dnmd_platform.hpp"
#include "tearoffbase.hpp"
#include "controllingiunknown.hpp"

#include <external/cor.h>
#include <external/corhdr.h>

#include <cstdint>
#include <atomic>

EXTERN_GUID(IID_IDNMDOwner, 0x250ebc02, 0x1a92, 0x4638, 0xaa, 0x6c, 0x3d, 0x0f, 0x98, 0xb3, 0xa6, 0xfb);

// This interface is an IUnknown interface for the purposes of easy discovery.
struct IDNMDOwner : IUnknown
{
    virtual mdhandle_t MetaData() = 0;
};

// We use a reference wrapper around the handle to allow the handle to be swapped out.
// We plan to use swapping to implement table sorting as DNMD itself does not support
// sorting tables or remapping tokens.
class mdhandle_view final
{
private:
    dncp::com_ptr<IDNMDOwner> _owner;
public:
    mdhandle_view(dncp::com_ptr<IDNMDOwner> owner)
        : _owner{ owner.p }
    {
    }

    mdhandle_view(mdhandle_view const& other)
        : _owner{ other._owner.p }
    {
    }

    mdhandle_view(mdhandle_view&& other) = default;

    mdhandle_view& operator=(mdhandle_view const& other)
    {
        _owner = dncp::com_ptr<IDNMDOwner>{ other._owner.p };
        return *this;
    }

    mdhandle_view& operator=(mdhandle_view&& other) = default;

    mdhandle_t get()
    {
        return _owner->MetaData();
    }

    bool operator==(std::nullptr_t)
    {
        return get() == nullptr;
    }
    bool operator!=(std::nullptr_t)
    {
        return get() != nullptr;
    }
};

inline bool operator==(std::nullptr_t, mdhandle_view& view)
{
    return view == nullptr;
}

inline bool operator!=(std::nullptr_t, mdhandle_view& view)
{
    return view != nullptr;
}

class DNMDOwner final : public TearOffBase<IDNMDOwner>
{
private:
    mdhandle_ptr _handle;
    malloc_ptr<void> _malloc_to_free;
    dncp::cotaskmem_ptr<void> _cotaskmem_to_free;

protected:
    virtual bool TryGetInterfaceOnThis(REFIID riid, void** ppvObject) override
    {
        assert(riid != IID_IUnknown);
        if (riid == IID_IDNMDOwner)
        {
            *ppvObject = static_cast<IDNMDOwner*>(this);
            return true;
        }
        return false;
    }

public:
    DNMDOwner(IUnknown* controllingUnknown, mdhandle_ptr md_ptr, malloc_ptr<void> mallocMem, dncp::cotaskmem_ptr<void> cotaskmemMem)
        : TearOffBase(controllingUnknown)
        , _handle{ std::move(md_ptr) }
        , _malloc_to_free{ std::move(mallocMem) }
        , _cotaskmem_to_free{ std::move(cotaskmemMem) }
    { }

    virtual ~DNMDOwner() = default;

public: // IDNMDOwner
    mdhandle_t MetaData() override
    {
        return _handle.get();
    }
};

#endif // !_SRC_INTERFACES_DNMDOWNER_HPP_