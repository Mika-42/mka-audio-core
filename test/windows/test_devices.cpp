#include <gtest/gtest.h>
#include "fake_devices.hpp"
using namespace wasapi_test;
TEST(ComMocks, QueriesPreserveCanonicalIdentityAndOwnReferencesUntilTheFinalRelease) {
    EXPECT_EQ(liveObjects.load(), 0);
    auto device = makeFake<FakeDevice>();
    EXPECT_EQ(device->QueryInterface(__uuidof(IUnknown), nullptr), E_POINTER);
    void* unsupported = device.Get();
    EXPECT_EQ(device->QueryInterface(__uuidof(IPropertyStore), &unsupported), E_NOINTERFACE);
    EXPECT_EQ(unsupported, nullptr);
    ComPtr<IMMEndpoint> endpoint;
    ASSERT_EQ(device.As(&endpoint), S_OK);
    ComPtr<IUnknown> deviceIdentity, endpointIdentity;
    ASSERT_EQ(device.As(&deviceIdentity), S_OK);
    ASSERT_EQ(endpoint.As(&endpointIdentity), S_OK);
    EXPECT_EQ(deviceIdentity.Get(), endpointIdentity.Get());
    device.Reset();
    endpoint.Reset();
    EXPECT_EQ(liveObjects.load(), 1);
    deviceIdentity.Reset();
    EXPECT_EQ(liveObjects.load(), 1);
    endpointIdentity.Reset();
    EXPECT_EQ(liveObjects.load(), 0);
}
TEST_F(DevicesFixture, EnumeratesOnlyActiveNativeRenderAndCaptureIdsOnTheComOwner) {
    add(L"output:\u00e9", eRender); add(L"capture",eCapture);
    auto disabled=add(L"disabled",eRender);disabled->state=DEVICE_STATE_DISABLED;
    add(L"unknown",eAll);
    ControlThread control(api());
    const auto endpoints=enumerateDevices(control);
    ASSERT_EQ(endpoints.size(),2u);
    EXPECT_EQ(endpoints[0].id,"output:\xC3\xA9");EXPECT_EQ(endpoints[0].flow,eRender);
    EXPECT_EQ(endpoints[1].id,"capture");EXPECT_EQ(endpoints[1].flow,eCapture);
    EXPECT_EQ(endpoints[0].name,endpoints[0].id); // property failure has a usable fallback
    EXPECT_NE(activeEnumerator->calledOn.load(),GetCurrentThreadId());
    disabled.Reset();
}
TEST_F(DevicesFixture, NativeCreationEnumerationAndCountFailuresReturnAnEmptySnapshot) {
    add(L"output",eRender);ControlThread control(api());
    createResult=E_ACCESSDENIED;EXPECT_TRUE(enumerateDevices(control).empty());createResult=S_OK;
    activeEnumerator->enumResult=E_FAIL;EXPECT_TRUE(enumerateDevices(control).empty());activeEnumerator->enumResult=S_OK;
    activeEnumerator->collection->countResult=E_FAIL;EXPECT_TRUE(enumerateDevices(control).empty());
}
TEST_F(DevicesFixture, BadItemsDoNotHideOtherUsableDevicesAndSnapshotOwnsItsStrings) {
    auto bad=add(L"bad",eRender);bad->idResult=E_FAIL;
    auto good=add(L"good",eCapture);ControlThread control(api());
    auto first=enumerateDevices(control);ASSERT_EQ(first.size(),1u);EXPECT_EQ(first[0].id,"good");
    good->id=L"changed";
    auto second=enumerateDevices(control);ASSERT_EQ(second.size(),1u);EXPECT_EQ(second[0].id,"changed");EXPECT_EQ(first[0].id,"good");
    activeEnumerator->collection->itemResult=E_FAIL;EXPECT_TRUE(enumerateDevices(control).empty());
    bad.Reset();good.Reset();
}
TEST(WasapiStrings, RejectsInvalidUtf8AndEmbeddedNullWithoutTruncatingAnEndpointId) {
    EXPECT_TRUE(utf16(std::string_view("id\0other",8)).empty());
    EXPECT_TRUE(utf16("\xFF").empty());EXPECT_TRUE(utf16("").empty());
    EXPECT_EQ(utf8(utf16("endpoint:\xC3\xA9").c_str()),"endpoint:\xC3\xA9");
}
