#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>
#include "../../../src/backend/windows/detail/directsound_host.hpp"
import mka.audio.backend.directsound;

namespace {
using namespace mka_ds_detail;
using namespace mka::audio::core;
const GUID firstGuid{0x12345678, 0x9abc, 0xdef0, {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0}};
const GUID secondGuid{0x87654321, 0xabcd, 0xef01, {0x43, 0x21, 0x65, 0x87, 0xa9, 0xcb, 0xed, 0x0f}};
struct Entry { GUID guid; const wchar_t* name; bool isDefault = false; };
struct Discovery {
    inline static Discovery* current = nullptr;
    DiscoveryApi api;
    Api host;
    std::vector<Entry> entries, inputEntries;
    HRESULT result = DS_OK, inputResult = DS_OK;
    unsigned calls = 0, accepted = 0, inputCalls = 0, inputAccepted = 0;
    DWORD thread = 0, inputThread = 0;
    static HRESULT WINAPI enumerate(LPDSENUMCALLBACKW callback, LPVOID context) {
        auto& mock = *current; ++mock.calls; mock.thread = GetCurrentThreadId();
        for (auto& entry : mock.entries) {
            if (!callback(entry.isDefault ? nullptr : &entry.guid, entry.name, L"mock-driver.dll", context)) break;
            ++mock.accepted;
        }
        return mock.result;
    }
    static HRESULT WINAPI enumerateInput(LPDSENUMCALLBACKW callback, LPVOID context) {
        auto& mock = *current; ++mock.inputCalls; mock.inputThread = GetCurrentThreadId();
        for (auto& entry : mock.inputEntries) {
            if (!callback(entry.isDefault ? nullptr : &entry.guid, entry.name, L"mock-capture.dll", context)) break;
            ++mock.inputAccepted;
        }
        return mock.inputResult;
    }
    Discovery() {
        current = this; api.enumerateOutput = &enumerate; api.enumerateInput = &enumerateInput; testDiscoveryOverride = &api;
        host.devices = nativeDevices; testApiOverride = &host;
    }
    ~Discovery() { testApiOverride = nullptr; testDiscoveryOverride = nullptr; current = nullptr; }
};

TEST(DirectSoundIdentity, GuidSerializationAndParsingHaveAnIndependentCanonicalOracle) {
    const std::string expected = "directsound:output:{12345678-9abc-def0-1234-56789abcdef0}";
    EXPECT_EQ(deviceId(&firstGuid, Flow::output), expected);
    const auto parsed = parseDeviceId(expected); ASSERT_TRUE(parsed); ASSERT_TRUE(parsed->guid);
    EXPECT_EQ(parsed->flow, Flow::output); EXPECT_TRUE(IsEqualGUID(*parsed->guid, firstGuid));
    const auto uppercase = parseDeviceId("directsound:output:{12345678-9ABC-DEF0-1234-56789ABCDEF0}");
    ASSERT_TRUE(uppercase); EXPECT_EQ(deviceId(&*uppercase->guid, uppercase->flow), expected);
    const auto input = parseDeviceId("directsound:input:{12345678-9abc-def0-1234-56789abcdef0}");
    ASSERT_TRUE(input); EXPECT_EQ(input->flow, Flow::input); EXPECT_TRUE(IsEqualGUID(*input->guid, firstGuid));
}

TEST(DirectSoundIdentity, DefaultAliasesAreDirectionSpecificAndNeverAZeroPhysicalGuid) {
    const auto output = parseDeviceId(deviceId(nullptr, Flow::output));
    const auto input = parseDeviceId(deviceId(nullptr, Flow::input));
    ASSERT_TRUE(output); ASSERT_TRUE(input); EXPECT_FALSE(output->guid); EXPECT_FALSE(input->guid);
    EXPECT_EQ(output->flow, Flow::output); EXPECT_EQ(input->flow, Flow::input);
    const GUID zero{}; EXPECT_TRUE(deviceId(&zero, Flow::output).empty());
    EXPECT_TRUE(deviceId(&firstGuid, Flow::duplex).empty());
}

TEST(DirectSoundIdentity, RejectsMalformedPrefixesHexFieldsSeparatorsAndZeroGuid) {
    const char* bad[]{"", "default", "wasapi:output:default", "directsound:duplex:default", "directsound:output:DEFAULT",
        "directsound:output:{00000000-0000-0000-0000-000000000000}",
        "directsound:output:{12345678_9abc-def0-1234-56789abcdef0}",
        "directsound:output:{12345678-9abc-def0-1234-56789abcdefg}",
        "directsound:output:{-2345678-9abc-def0-1234-56789abcdef0}",
        "directsound:output:{12345678-9abc-def0-1234-56789abcde f}",
        "directsound:output:{12345678-9abc-def0-1234-56789abcdef0}extra",
        "directsound:output:{12345678-9abc-def0-1234-56789abcdef}",
        "directsound:output:12345678-9abc-def0-1234-56789abcdef0"};
    for (const auto id : bad) EXPECT_FALSE(parseDeviceId(id)) << id;
}

TEST(DirectSoundDiscovery, EmptyNativeListNeverInventsADefaultEndpoint) {
    Discovery mock; DirectSound backend;
    EXPECT_TRUE(backend.getEndPoints().empty()); EXPECT_EQ(mock.calls, 1u);
    EXPECT_EQ(mock.thread, GetCurrentThreadId());
}

TEST(DirectSoundDiscovery, PreservesOnePhysicalOutputAndItsUtf8NameWithoutUnprovenCapabilities) {
    Discovery mock; mock.entries = {{firstGuid, L"Haut-parleur \u00e9 \U0001f3a4"}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
    EXPECT_EQ(endpoints[0].id, "directsound:output:{12345678-9abc-def0-1234-56789abcdef0}");
    EXPECT_EQ(endpoints[0].name, "Haut-parleur \xc3\xa9 \xf0\x9f\x8e\xa4");
    EXPECT_FALSE(endpoints[0].input); ASSERT_TRUE(endpoints[0].output);
    EXPECT_EQ(endpoints[0].output->maxChannels, 0u); EXPECT_TRUE(endpoints[0].output->formats.empty());
    EXPECT_TRUE(endpoints[0].output->sampleRates.empty()); EXPECT_TRUE(endpoints[0].output->bufferSizes.empty());
}

TEST(DirectSoundDiscovery, DeduplicatesIdentitiesWhileKeepingDistinctHomonymousDevices) {
    Discovery mock; mock.entries = {{{}, L"Primary", true}, {firstGuid, L"Same"}, {firstGuid, L"Duplicate"},
        {secondGuid, L"Same"}, {{}, L"Duplicate default", true}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 3u);
    EXPECT_EQ(endpoints[0].id, "directsound:output:default"); EXPECT_EQ(endpoints[0].name, "Primary");
    EXPECT_EQ(endpoints[1].name, "Same"); EXPECT_EQ(endpoints[2].name, "Same");
    EXPECT_NE(endpoints[1].id, endpoints[2].id);
}

TEST(DirectSoundDiscovery, IgnoresSuppliedZeroGuidButKeepsARealNullGuidDefaultAlias) {
    Discovery mock; mock.entries = {{{}, L"Bad physical"}, {firstGuid, L"Valid"}, {{}, L"Default", true}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 2u);
    EXPECT_EQ(endpoints[1].id, "directsound:output:default");
}

TEST(DirectSoundDiscovery, MissingLabelsFallBackToTheirNativeIdentity) {
    Discovery mock; mock.entries = {{firstGuid, nullptr}, {secondGuid, L""}, {{}, nullptr, true}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 3u);
    EXPECT_EQ(endpoints[0].name, endpoints[0].id); EXPECT_EQ(endpoints[1].name, endpoints[1].id);
    EXPECT_EQ(endpoints[2].name, "Default DirectSound output");
}

TEST(DirectSoundDiscovery, NativeEnumerationFailureDiscardsPartialResults) {
    Discovery mock; mock.entries = {{firstGuid, L"Before failure"}}; mock.result = DSERR_NODRIVER;
    DirectSound backend; EXPECT_TRUE(backend.getEndPoints().empty()); EXPECT_EQ(mock.accepted, 1u);
}

TEST(DirectSoundDiscovery, CallbackFailureDiscardsPartialResultsEvenWhenSdkReturnsSuccess) {
    const wchar_t invalid[]{wchar_t(0xd800), 0};
    Discovery mock; mock.entries = {{firstGuid, L"Valid"}, {secondGuid, invalid}};
    DirectSound backend; EXPECT_TRUE(backend.getEndPoints().empty()); EXPECT_EQ(mock.accepted, 1u);
    Enumeration failed{Flow::output}; failed.failed = true;
    EXPECT_FALSE(collectDevice(nullptr, L"Ignored", nullptr, &failed));
    EXPECT_FALSE(collectDevice(nullptr, L"No context", nullptr, nullptr));
}

TEST(DirectSoundDiscovery, MissingNativeEnumeratorIsAnEmptyListAndNamesAreCopied) {
    Discovery mock; mock.api.enumerateOutput = nullptr;
    DirectSound missing; EXPECT_TRUE(missing.getEndPoints().empty()); EXPECT_EQ(mock.calls, 0u);
    mock.api.enumerateOutput = &Discovery::enumerate;
    std::wstring name = L"Borrowed driver name"; mock.entries = {{firstGuid, name.c_str()}};
    DirectSound backend; const auto snapshot = backend.getEndPoints(); ASSERT_EQ(snapshot.size(), 1u);
    name.assign(L"Changed"); EXPECT_EQ(snapshot[0].name, "Borrowed driver name");
}

TEST(DirectSoundDiscovery, StrictUtf16ConversionRejectsInvalidSurrogates) {
    EXPECT_FALSE(utf8Name(nullptr)); EXPECT_FALSE(utf8Name(L""));
    const wchar_t invalid[]{wchar_t(0xdc00), 0}; EXPECT_FALSE(utf8Name(invalid));
    const auto text = utf8Name(L"\u03a9"); ASSERT_TRUE(text); EXPECT_EQ(*text, "\xce\xa9");
}

TEST(DirectSoundCaptureDiscovery, InputOnlyMetadataUsesItsOwnDirectionAndUtf8Label) {
    Discovery mock; mock.inputEntries = {{firstGuid, L"Microphone \u03a9"}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
    EXPECT_EQ(endpoints[0].id, "directsound:input:{12345678-9abc-def0-1234-56789abcdef0}");
    EXPECT_EQ(endpoints[0].name, "Microphone \xce\xa9"); ASSERT_TRUE(endpoints[0].input); EXPECT_FALSE(endpoints[0].output);
    EXPECT_TRUE(endpoints[0].input->formats.empty()); EXPECT_TRUE(endpoints[0].input->sampleRates.empty());
    EXPECT_EQ(mock.inputCalls, 1u); EXPECT_EQ(mock.inputThread, GetCurrentThreadId());
}

TEST(DirectSoundCaptureDiscovery, EqualGuidsRemainIndependentInputAndOutputEndpoints) {
    Discovery mock; mock.entries = {{firstGuid, L"Output"}}; mock.inputEntries = {{firstGuid, L"Input"}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 2u);
    EXPECT_NE(endpoints[0].id, endpoints[1].id);
    EXPECT_TRUE(endpoints[0].output); EXPECT_FALSE(endpoints[0].input);
    EXPECT_TRUE(endpoints[1].input); EXPECT_FALSE(endpoints[1].output);
}

TEST(DirectSoundCaptureDiscovery, HomonymousDistinctDevicesAreNotPaired) {
    Discovery mock; mock.entries = {{firstGuid, L"Same"}}; mock.inputEntries = {{secondGuid, L"Same"}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 2u);
    EXPECT_EQ(endpoints[0].name, endpoints[1].name); EXPECT_NE(endpoints[0].id, endpoints[1].id);
    EXPECT_FALSE(endpoints[0].input); EXPECT_FALSE(endpoints[1].output);
}

TEST(DirectSoundCaptureDiscovery, DefaultAliasesAndMissingLabelsRemainDirectionSpecific) {
    Discovery mock; mock.entries = {{{}, nullptr, true}}; mock.inputEntries = {{{}, nullptr, true}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 2u);
    EXPECT_EQ(endpoints[0].id, "directsound:output:default"); EXPECT_EQ(endpoints[0].name, "Default DirectSound output");
    EXPECT_EQ(endpoints[1].id, "directsound:input:default"); EXPECT_EQ(endpoints[1].name, "Default DirectSound input");
}

TEST(DirectSoundCaptureDiscovery, CaptureFailureNeverHidesAvailableOutputs) {
    Discovery mock; mock.entries = {{firstGuid, L"Output"}}; mock.inputEntries = {{secondGuid, L"Partial input"}};
    mock.inputResult = DSERR_NODRIVER;
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
    EXPECT_TRUE(endpoints[0].output); EXPECT_FALSE(endpoints[0].input); EXPECT_EQ(mock.inputAccepted, 1u);
}

TEST(DirectSoundCaptureDiscovery, PlaybackFailureNeverHidesAvailableInputs) {
    Discovery mock; mock.entries = {{firstGuid, L"Partial output"}}; mock.result = DSERR_NODRIVER;
    mock.inputEntries = {{secondGuid, L"Input"}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
    EXPECT_TRUE(endpoints[0].input); EXPECT_FALSE(endpoints[0].output);
}

TEST(DirectSoundCaptureDiscovery, InvalidUnicodeDiscardsOnlyTheFailedCaptureEnumeration) {
    const wchar_t invalid[]{wchar_t(0xd800), 0};
    Discovery mock; mock.entries = {{firstGuid, L"Output"}};
    mock.inputEntries = {{firstGuid, L"Partial input"}, {secondGuid, invalid}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
    EXPECT_TRUE(endpoints[0].output); EXPECT_FALSE(endpoints[0].input); EXPECT_EQ(mock.inputAccepted, 1u);
}

TEST(DirectSoundCaptureDiscovery, DuplicateCaptureIdsAreRemovedWithoutMergingDifferentGuids) {
    Discovery mock; mock.inputEntries = {{firstGuid, L"Same"}, {firstGuid, L"Duplicate"},
        {secondGuid, L"Same"}, {{}, L"Invalid physical GUID"}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 2u);
    EXPECT_EQ(endpoints[0].name, "Same"); EXPECT_EQ(endpoints[1].name, "Same"); EXPECT_NE(endpoints[0].id, endpoints[1].id);
    for (const auto& endpoint : endpoints) { EXPECT_TRUE(endpoint.input); EXPECT_FALSE(endpoint.output); }
}

TEST(DirectSoundCaptureDiscovery, MissingCaptureEnumeratorKeepsOutputDiscoveryAvailable) {
    Discovery mock; mock.api.enumerateInput = nullptr; mock.entries = {{firstGuid, L"Output"}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
    EXPECT_EQ(mock.inputCalls, 0u); EXPECT_TRUE(endpoints[0].output);
}

TEST(DirectSoundCaptureDiscovery, DeviceRemovalChangesOnlyNewSnapshots) {
    Discovery mock; mock.inputEntries = {{firstGuid, L"Input"}}; DirectSound backend;
    const auto snapshot = backend.getEndPoints(); ASSERT_EQ(snapshot.size(), 1u);
    mock.inputEntries.clear(); EXPECT_TRUE(backend.getEndPoints().empty());
    EXPECT_EQ(snapshot[0].name, "Input"); EXPECT_EQ(snapshot[0].id, deviceId(&firstGuid, Flow::input));
}
}
