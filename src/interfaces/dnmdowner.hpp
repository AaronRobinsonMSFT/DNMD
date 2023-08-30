#ifndef _SRC_INTERFACES_DNMDOWNER_HPP_
#define _SRC_INTERFACES_DNMDOWNER_HPP_

#include "internal/dnmd_platform.hpp"
#include "tearoffbase.hpp"
#include "controllingiunknown.hpp"

#include <external/cor.h>
#include <external/corhdr.h>

#include <cstdint>
#include <atomic>


// We use a reference wrapper around the handle to allow the handle to be swapped out.
// We plan to use swapping to implement table sorting as DNMD itself does not support
// sorting tables or remapping tokens.
struct mdhandle_view
{
private:
    mdhandle_ptr& ptr;
public:
    mdhandle_view() = delete;
    mdhandle_view(mdhandle_ptr& ptr)
        : ptr{ ptr }
    { }

    mdhandle_t get() const
    {
        return ptr.get();
    }

    bool operator==(std::nullptr_t) const
    {
        return ptr == nullptr;
    }
    bool operator!=(std::nullptr_t) const
    {
        return ptr != nullptr;
    }
};

inline bool operator==(std::nullptr_t, mdhandle_view const& view)
{
    return view == nullptr;
}

inline bool operator!=(std::nullptr_t, mdhandle_view const& view)
{
    return view != nullptr;
}

EXTERN_GUID(IID_IDNMDOwner, 0x250ebc02, 0x1a92, 0x4638, 0xaa, 0x6c, 0x3d, 0x0f, 0x98, 0xb3, 0xa6, 0xfb);

// This interface is an IUnknown interface for the purposes of easy discovery.
struct IDNMDOwner : IUnknown
{
    virtual mdhandle_view MetaData() = 0;
};

class DNMDOwner final : public TearOffBase<IDNMDOwner>
{
private:
    mdhandle_ptr m_handle;
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
        , m_handle{ std::move(md_ptr) }
        , _malloc_to_free{ std::move(mallocMem) }
        , _cotaskmem_to_free{ std::move(cotaskmemMem) }
    { }

    virtual ~DNMDOwner() = default;

public: // IDNMDOwner
    mdhandle_view MetaData()
    {
        // We use a reference here to enable us to swap out the underlying metadata handle.
        return m_handle;
    }
};

#endif // !_SRC_INTERFACES_DNMDOWNER_HPP_