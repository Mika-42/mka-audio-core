#pragma once
#include <audioclient.h>
#include "fake_com.hpp"
#include <vector>
#include <atomic>
namespace wasapi_test {
inline std::atomic<int> liveServices{0};
class FakeRender : public FakeComObject<FakeRender, IAudioRenderClient> {
public:
    FakeRender(){++liveServices;}
    ~FakeRender(){--liveServices;}
    std::vector<BYTE> bytes=std::vector<BYTE>(65536,0xA5);
    std::atomic<HRESULT> getResult{S_OK}, releaseResult{S_OK};
    UINT32 acquired=0,released=0;DWORD releaseFlags=0;
    unsigned gets=0,releases=0;bool nullBuffer=false;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT32 frames,BYTE** out) override {
        ++gets;acquired=frames;*out=nullBuffer?nullptr:bytes.data();return getResult.load();
    }
    HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32 frames,DWORD flags) override {
        ++releases;released=frames;releaseFlags=flags;return releaseResult.load();
    }
};
}
