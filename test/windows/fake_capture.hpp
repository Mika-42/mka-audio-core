#pragma once
#include "fake_render.hpp"
#include <array>
namespace wasapi_test {
class FakeCapture : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IAudioCaptureClient> {
public:
    FakeCapture(){++liveServices;}
    ~FakeCapture(){--liveServices;}
    struct Packet { UINT32 frames=0;DWORD flags=0;UINT32 announced=0; };
    std::array<Packet,16> packets{};
    unsigned count=0,head=0;
    std::vector<BYTE> bytes=std::vector<BYTE>(65536,0xA5);
    std::atomic<HRESULT> nextResult{S_OK},getResult{S_OK},releaseResult{S_OK};
    unsigned gets=0,releases=0;UINT32 released=0;bool nullBuffer=false;
    HRESULT STDMETHODCALLTYPE GetNextPacketSize(UINT32* out) override {
        if(head>=count)*out=0;else *out=packets[head].announced?packets[head].announced:packets[head].frames;
        return nextResult.load();
    }
    HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** out,UINT32* frames,DWORD* flags,UINT64* position,UINT64* time) override {
        ++gets;if(head>=count)return AUDCLNT_S_BUFFER_EMPTY;
        *frames=packets[head].frames;*flags=packets[head].flags;
        *out=(nullBuffer || (*flags&AUDCLNT_BUFFERFLAGS_SILENT))?nullptr:bytes.data();
        if(position)*position=0;if(time)*time=0;return getResult.load();
    }
    HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32 frames) override {
        ++releases;released=frames;if(frames && head<count)++head;return releaseResult.load();
    }
};
}
