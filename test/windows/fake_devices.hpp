#pragma once
#include "../../src/backend/windows/detail/wasapi_devices.hpp"
#include "fake_com.hpp"
#include <atomic>
#include <cstring>
#include "fake_client.hpp"
namespace wasapi_test {
using namespace mka_wasapi_detail;
inline std::atomic<int> liveObjects{0};
struct Track { Track(){++liveObjects;} ~Track(){--liveObjects;} };
class FakeDevice : public FakeComObject<FakeDevice, IMMDevice, IMMEndpoint> {
public:
    Track track;
    std::wstring id = L"test:output";
    EDataFlow flow = eRender;
    DWORD state = DEVICE_STATE_ACTIVE;
    HRESULT idResult = S_OK, stateResult = S_OK, flowResult = S_OK, activateResult = S_OK;
    ComPtr<FakeAudioClient> client = makeFake<FakeAudioClient>();
    HRESULT STDMETHODCALLTYPE Activate(REFIID iid, DWORD, PROPVARIANT*, void** out) override {
        *out=nullptr;if (FAILED(activateResult)) return activateResult;return client->QueryInterface(iid,out);
    }
    HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD, IPropertyStore** out) override { *out = nullptr; return E_FAIL; }
    HRESULT STDMETHODCALLTYPE GetId(LPWSTR* out) override {
        *out = static_cast<wchar_t*>(CoTaskMemAlloc((id.size()+1)*sizeof(wchar_t)));
        if (!*out) return E_OUTOFMEMORY;
        std::memcpy(*out,id.c_str(),(id.size()+1)*sizeof(wchar_t)); return idResult;
    }
    HRESULT STDMETHODCALLTYPE GetState(DWORD* out) override { *out = state; return stateResult; }
    HRESULT STDMETHODCALLTYPE GetDataFlow(EDataFlow* out) override { *out = flow; return flowResult; }
};
class FakeCollection : public FakeComObject<FakeCollection, IMMDeviceCollection> {
public:
    Track track;
    std::vector<ComPtr<IMMDevice>> devices;
    HRESULT countResult = S_OK, itemResult = S_OK;
    HRESULT STDMETHODCALLTYPE GetCount(UINT* out) override { *out=static_cast<UINT>(devices.size()); return countResult; }
    HRESULT STDMETHODCALLTYPE Item(UINT index, IMMDevice** out) override {
        *out = nullptr;
        if (FAILED(itemResult)) return itemResult;
        if (index >= devices.size()) return E_INVALIDARG;
        return devices[index].CopyTo(out);
    }
};
class FakeEnumerator : public FakeComObject<FakeEnumerator, IMMDeviceEnumerator> {
public:
    Track track;
    ComPtr<FakeCollection> collection = makeFake<FakeCollection>();
    HRESULT enumResult = S_OK;
    std::atomic<DWORD> calledOn{0};
    HRESULT STDMETHODCALLTYPE EnumAudioEndpoints(EDataFlow flow, DWORD mask, IMMDeviceCollection** out) override {
        calledOn=GetCurrentThreadId();
        if (flow != eAll || mask != DEVICE_STATE_ACTIVE) return E_INVALIDARG;
        *out = nullptr; if (FAILED(enumResult)) return enumResult;
        return collection.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetDefaultAudioEndpoint(EDataFlow, ERole, IMMDevice** out) override { *out=nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDevice(LPCWSTR id, IMMDevice** out) override {
        *out=nullptr;
        for (auto& device:collection->devices) {
            wchar_t* raw=nullptr; const auto hr=device->GetId(&raw);TaskPtr<wchar_t> stored(raw);
            if (SUCCEEDED(hr) && stored && std::wstring_view(stored.get())==id) return device.CopyTo(out);
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    HRESULT STDMETHODCALLTYPE RegisterEndpointNotificationCallback(IMMNotificationClient*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UnregisterEndpointNotificationCallback(IMMNotificationClient*) override { return S_OK; }
};
inline ComPtr<FakeEnumerator> activeEnumerator;
inline HRESULT createResult = S_OK;
inline HRESULT WINAPI create(REFCLSID cls, LPUNKNOWN, DWORD, REFIID iid, LPVOID* out) {
    *out=nullptr;
    if (FAILED(createResult)) return createResult;
    if (cls != __uuidof(MMDeviceEnumerator) || !activeEnumerator) return E_NOINTERFACE;
    return activeEnumerator->QueryInterface(iid,out);
}
inline ComApi api() { return {&CoInitializeEx,&CoUninitialize,create}; }
class DevicesFixture : public testing::Test {
protected:
    void SetUp() override { createResult=S_OK;activeEnumerator=makeFake<FakeEnumerator>(); }
    void TearDown() override { activeEnumerator.Reset();EXPECT_EQ(liveObjects.load(),0); }
    ComPtr<FakeDevice> add(std::wstring id, EDataFlow flow) {
        auto device=makeFake<FakeDevice>();device->id=std::move(id);device->flow=flow;
        activeEnumerator->collection->devices.push_back(device);return device;
    }
};
}
