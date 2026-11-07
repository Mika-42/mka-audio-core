#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include "../../src/backend/windows/detail/wasapi_session.hpp"
#include "rt_alloc.hpp"
import mka.audio.convert;
#include "planar_callbacks.hpp"
using namespace wasapi_test;
class WasapiRt:public DevicesFixture{};
TEST_F(WasapiRt, AllFormatsRenderAndCaptureWithoutCxxAllocationOrRelease) {
    auto output=add(L"output",eRender);auto input=add(L"input",eCapture);ControlThread control(api());Session session(control);
    auto cb=converters();cb.process=+[](void*,const float* const*,UINT32,float* const* outputs,UINT32 channels,UINT32 frames)noexcept{
        for(UINT32 ch=0;ch<channels;++ch)for(UINT32 i=0;i<frames;++i)outputs[ch][i]=0.25f;
    };
    for(auto kind:{SampleKind::S16,SampleKind::S24Packed,SampleKind::S24Msb32,SampleKind::S32,SampleKind::F32,SampleKind::F64}){
        ASSERT_EQ(session.open({"output",eRender,2,48000,256,kind}),S_OK);
        std::size_t news=0,frees=0;HRESULT result=S_OK;
        {wasapi_rt::Scope scope;for(int i=0;i<1000;++i)result=session.renderOnce(cb);news=scope.news();frees=scope.deletes();}
        EXPECT_EQ(result,S_OK);EXPECT_EQ(news,0u);EXPECT_EQ(frees,0u);ASSERT_EQ(session.close(),S_OK);
        ASSERT_EQ(session.open({"input",eCapture,2,48000,256,kind}),S_OK);auto& capture=*input->client->capture.Get();capture.count=1;capture.packets[0]={7,AUDCLNT_BUFFERFLAGS_SILENT};
        {wasapi_rt::Scope scope;for(int i=0;i<1000;++i){capture.head=0;result=session.processCapture(cb);}news=scope.news();frees=scope.deletes();}
        EXPECT_EQ(result,S_OK);EXPECT_EQ(news,0u);EXPECT_EQ(frees,0u);ASSERT_EQ(session.close(),S_OK);
    }
    output.Reset();input.Reset();
}
TEST(WasapiAllocationProbe, DetectsOrdinaryArrayNothrowAndAlignedOperations) {
    std::size_t news=0,frees=0;
    {wasapi_rt::Scope scope;
        auto* a=::operator new(16);::operator delete(a);
        auto* b=::operator new[](16);::operator delete[](b);
        auto* c=::operator new(16,std::nothrow);::operator delete(c,std::nothrow);
        auto* d=::operator new(64,std::align_val_t(64));::operator delete(d,std::align_val_t(64));
        news=scope.news();frees=scope.deletes();
    }
    EXPECT_EQ(news,4u);EXPECT_EQ(frees,4u);
}
