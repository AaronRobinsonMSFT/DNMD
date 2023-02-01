#ifndef _SRC_INTERFACES_TEAROFFBASE_HPP_
#define _SRC_INTERFACES_TEAROFFBASE_HPP_

#include <platform.h>
#include <atomic>
#include <memory>

class ControllingIUnknown;

class TearOffUnknown : public IUnknown
{
    friend class ControllingIUnknown;
private:
    IUnknown* _pUnkOuter;

protected:
    STDMETHOD(TryGetInterfaceOnThis)(REFIID riid, void** ppvObject) PURE;

public:
    TearOffUnknown(IUnknown* outer)
        :_pUnkOuter(outer)
    {

    }

    virtual ~TearOffUnknown() = default;

public: // IUnknown
    STDMETHODIMP_(ULONG) AddRef() override
    {
        return _pUnkOuter->AddRef();
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        return _pUnkOuter->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        // The outer IUnknown must always be returned
        // when IID_IUnknown is requested.
        // To ensure we handle nested composition,
        // we'll call into the outer QI to ensure we propogate
        // up to the true outer IUnknown.
        if (riid == IID_IUnknown)
        {
            return _pUnkOuter->QueryInterface(riid, ppvObject);
        }

        if (TryGetInterfaceOnThis(riid, ppvObject) == S_OK)
        {
            (void)AddRef();
            return S_OK;
        }
        else
        {
            return _pUnkOuter->QueryInterface(riid, ppvObject);
        }
    }
};

template<typename... T>
class TearOffBase : public TearOffUnknown, public T...
{
public:
    using TearOffUnknown::TearOffUnknown;

    STDMETHODIMP_(ULONG) AddRef() override final
    {
        return TearOffUnknown::AddRef();
    }
    STDMETHODIMP_(ULONG) Release() override final
    {
        return TearOffUnknown::Release();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override final
    {
        return TearOffUnknown::QueryInterface(riid, ppvObject);
    }
};

#define TEAR_OFF_IUNKNOWN_IMPLEMENTATION()

template<typename T>
struct ComReleaser
{
    using pointer = T*;
    void operator()(pointer mem)
    {
        mem->Release();
    }
};

template<typename T>
using com_ptr = std::unique_ptr<T*, typename ComReleaser<T>>;

#endif