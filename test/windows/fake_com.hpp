#pragma once
#include <unknwn.h>
#include <wrl/client.h>
#include <atomic>
#include <utility>

namespace wasapi_test {
// Test objects implement the SDK interfaces directly, including canonical
// IUnknown identity and one owned reference for each successful query.
template<class Derived, class First, class... Rest>
class FakeComObject : public First, public Rest... {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, __uuidof(IUnknown)))
            *out = static_cast<IUnknown*>(static_cast<First*>(this));
        else if (IsEqualIID(iid, __uuidof(First)))
            *out = static_cast<First*>(this);
        else if (!((IsEqualIID(iid, __uuidof(Rest))
                    ? (*out = static_cast<Rest*>(this), true) : false) || ...))
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) delete static_cast<Derived*>(this);
        return remaining;
    }
protected:
    virtual ~FakeComObject() = default;
private:
    std::atomic<ULONG> references_{1};
};

template<class T, class... Arguments>
Microsoft::WRL::ComPtr<T> makeFake(Arguments&&... arguments) {
    Microsoft::WRL::ComPtr<T> result;
    result.Attach(new T(std::forward<Arguments>(arguments)...));
    return result;
}
}
