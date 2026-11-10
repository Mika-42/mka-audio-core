#include <gtest/gtest.h>
#include <cstring>
#include "../../../src/backend/windows/detail/asio_devices.hpp"

namespace {
using namespace mka_asio_detail;
const std::wstring idA = L"{AAAAAAAA-1111-2222-3333-444444444444}";
const std::wstring idB = L"{BBBBBBBB-1111-2222-3333-444444444444}";
struct Entry {
    std::wstring key = L"Driver", description = L"Device", clsid = idA, path = L"C:\\Drivers\\valid.dll";
    bool user = true, registered = true, badSize = false, noTerminator = false, changeSize = false;
    DWORD valueType = REG_SZ, pathType = REG_SZ;
};
struct Registry {
    inline static Registry* current = nullptr;
    RegistryApi api;
    std::vector<Entry> entries;
    unsigned live = 0, opened = 0, closed = 0;
    bool denied = false, directory = false, skipFirst = false, failExpansion = false;
    std::wstring lastPath;
    static HKEY key(std::uintptr_t value) { return reinterpret_cast<HKEY>(value); }
    static std::uintptr_t number(HKEY value) { return reinterpret_cast<std::uintptr_t>(value); }
    static std::wstring upper(std::wstring text) {
        for (auto& ch : text) if (ch >= L'a' && ch <= L'z') ch -= L'a' - L'A';
        return text;
    }
    static LSTATUS WINAPI open(HKEY parent, LPCWSTR path, DWORD, REGSAM, PHKEY out) {
        auto& r = *current; *out = nullptr;
        if (parent == HKEY_CURRENT_USER || parent == HKEY_LOCAL_MACHINE) {
            const bool user = parent == HKEY_CURRENT_USER;
            if (std::none_of(r.entries.begin(), r.entries.end(), [&](const auto& e) { return e.user == user; })) return ERROR_FILE_NOT_FOUND;
            if (std::wstring(path) != L"SOFTWARE\\ASIO") return ERROR_FILE_NOT_FOUND;
            *out = key(user ? 1 : 2);
        } else if (parent == HKEY_CLASSES_ROOT) {
            for (std::size_t i = 0; i < r.entries.size(); ++i)
                if (r.entries[i].registered && upper(path) == upper(L"CLSID\\" + r.entries[i].clsid + L"\\InprocServer32")) {
                    *out = key(1000 + i); break;
                }
        } else if (number(parent) == 1 || number(parent) == 2) {
            for (std::size_t i = 0; i < r.entries.size(); ++i)
                if (r.entries[i].key == path && r.entries[i].user == (number(parent) == 1)) { *out = key(100 + i); break; }
        }
        if (!*out) return ERROR_FILE_NOT_FOUND;
        ++r.live; ++r.opened; return ERROR_SUCCESS;
    }
    static LSTATUS WINAPI enumerate(HKEY root, DWORD index, LPWSTR name, LPDWORD length,
                                   LPDWORD, LPWSTR, LPDWORD, PFILETIME) {
        auto& r = *current;
        if (r.denied) return ERROR_ACCESS_DENIED;
        std::vector<const Entry*> entries;
        for (const auto& entry : r.entries) if (entry.user == (number(root) == 1)) entries.push_back(&entry);
        if (index >= entries.size()) return ERROR_NO_MORE_ITEMS;
        if ((r.skipFirst && index == 0) || entries[index]->key.size() >= *length) return ERROR_MORE_DATA;
        const auto& text = entries[index]->key;
        std::copy(text.begin(), text.end(), name); name[text.size()] = L'\0';
        *length = static_cast<DWORD>(text.size()); return ERROR_SUCCESS;
    }
    static LSTATUS WINAPI query(HKEY value, LPCWSTR name, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD bytes) {
        auto& r = *current; const auto raw = number(value);
        const bool server = raw >= 1000;
        auto& entry = r.entries[raw - (server ? 1000 : 100)];
        const bool clsid = !server && name && std::wstring(name) == L"CLSID";
        std::wstring text = server ? entry.path : clsid ? entry.clsid : entry.description;
        if (!server && !clsid && text.empty()) return ERROR_FILE_NOT_FOUND;
        *type = server ? entry.pathType : clsid ? entry.valueType : REG_SZ;
        auto size = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
        if (!data) { *bytes = clsid && entry.badSize ? 3 : size; return ERROR_SUCCESS; }
        if (clsid && entry.changeSize) { *bytes = size + 2; return ERROR_MORE_DATA; }
        if (*bytes < size) { *bytes = size; return ERROR_MORE_DATA; }
        std::memcpy(data, text.c_str(), size); *bytes = size;
        if (clsid && entry.noTerminator) reinterpret_cast<wchar_t*>(data)[text.size()] = L'X';
        return ERROR_SUCCESS;
    }
    static LSTATUS WINAPI close(HKEY) { --current->live; ++current->closed; return ERROR_SUCCESS; }
    static DWORD WINAPI attributes(LPCWSTR path) {
        current->lastPath = path;
        if (current->lastPath.find(L"missing") != std::wstring::npos) return INVALID_FILE_ATTRIBUTES;
        return current->directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    }
    static DWORD WINAPI expand(LPCWSTR, LPWSTR output, DWORD size) {
        if (current->failExpansion) return 0;
        const std::wstring text = L"\"C:\\Drivers\\expanded.dll\"";
        const auto count = static_cast<DWORD>(text.size() + 1);
        if (output && size >= count) std::copy_n(text.c_str(), count, output);
        return count;
    }
    Registry() {
        EXPECT_EQ(current, nullptr); current = this;
        api = {open, enumerate, query, close, attributes, expand};
    }
    ~Registry() { EXPECT_EQ(live, 0u); EXPECT_EQ(opened, closed); current = nullptr; }
};

TEST(AsioDevices, NoRegisteredDriversReturnsAnEmptySnapshot) {
    Registry registry;
    EXPECT_TRUE(enumerateDevices(registry.api).empty());
}

TEST(AsioDevices, BothRegistryHivesProduceStableGuidIdsAndUtf8Labels) {
    Registry registry;
    registry.entries.push_back({L"User", L"Micro \u00e9", idA});
    Entry machine{L"Machine", L"Output", idB}; machine.user = false; registry.entries.push_back(machine);
    const auto devices = enumerateDevices(registry.api);
    ASSERT_EQ(devices.size(), 2u);
    EXPECT_EQ(devices[0].id, "asio:{aaaaaaaa-1111-2222-3333-444444444444}");
    EXPECT_EQ(devices[1].id, "asio:{bbbbbbbb-1111-2222-3333-444444444444}");
    EXPECT_EQ(devices[0].name, "Micro \xC3\xA9");
}

TEST(AsioDevices, DuplicateClsidUsesTheUserRegistrationAndAppearsOnce) {
    Registry registry;
    Entry machine{L"Machine", L"Machine name", idA}; machine.user = false;
    registry.entries = {machine, Entry{L"User", L"User name", idA}};
    const auto devices = enumerateDevices(registry.api);
    ASSERT_EQ(devices.size(), 1u); EXPECT_EQ(devices[0].name, "User name");
}

TEST(AsioDevices, InvalidAndPhantomRegistrationsDoNotHideAValidDriver) {
    Registry registry;
    registry.entries.push_back({L"ProgId", L"Bad", L"Some.Program"});
    registry.entries.push_back({L"Null", L"Bad", L"{00000000-0000-0000-0000-000000000000}"});
    Entry absent{L"Absent", L"Absent", idA}; absent.registered = false; registry.entries.push_back(absent);
    registry.entries.push_back({L"Missing DLL", L"Missing", idB, L"C:\\missing.dll"});
    registry.entries.push_back({L"Valid", L"Good", L"{CCCCCCCC-1111-2222-3333-444444444444}"});
    const auto devices = enumerateDevices(registry.api);
    ASSERT_EQ(devices.size(), 1u); EXPECT_EQ(devices[0].name, "Good");
}

TEST(AsioDevices, CorruptStringTypesLengthsAndTerminatorsAreRejected) {
    for (unsigned mode = 0; mode < 5; ++mode) {
        Registry registry; registry.entries.emplace_back(); auto& entry = registry.entries.front();
        if (mode == 0) entry.valueType = REG_BINARY;
        if (mode == 1) entry.badSize = true;
        if (mode == 2) entry.noTerminator = true;
        if (mode == 3) entry.changeSize = true;
        if (mode == 4) entry.clsid += std::wstring(1, L'\0') + L"trailing";
        EXPECT_TRUE(enumerateDevices(registry.api).empty());
    }
}

TEST(AsioDevices, MissingOrInvalidDescriptionFallsBackToTheRegistryName) {
    Registry registry; registry.entries.emplace_back();
    registry.entries[0].description.clear();
    auto devices = enumerateDevices(registry.api); ASSERT_EQ(devices.size(), 1u); EXPECT_EQ(devices[0].name, "Driver");
    registry.entries[0].description = std::wstring(1, static_cast<wchar_t>(0xD800));
    devices = enumerateDevices(registry.api); ASSERT_EQ(devices.size(), 1u); EXPECT_EQ(devices[0].name, "Driver");
}

TEST(AsioDevices, ExpandedQuotedServerPathsAreCheckedWithoutLoadingTheDll) {
    Registry registry; registry.entries.emplace_back(); registry.entries[0].pathType = REG_EXPAND_SZ;
    registry.entries[0].path = L"\"%DRIVER_ROOT%\\expanded.dll\"";
    EXPECT_EQ(enumerateDevices(registry.api).size(), 1u);
    EXPECT_EQ(registry.lastPath, L"C:\\Drivers\\expanded.dll");
    registry.failExpansion = true;
    EXPECT_TRUE(enumerateDevices(registry.api).empty());
}

TEST(AsioDevices, DirectoryAndAccessDeniedEntriesReturnNoDriverAndReleaseEveryKey) {
    Registry registry; registry.entries.emplace_back(); registry.directory = true;
    EXPECT_TRUE(enumerateDevices(registry.api).empty());
    registry.directory = false; registry.denied = true;
    EXPECT_TRUE(enumerateDevices(registry.api).empty());
}

TEST(AsioDevices, OneOversizedRegistryKeyDoesNotHideTheNextDriver) {
    Registry registry; registry.entries = {Entry{L"Oversized", L"Skipped", idA}, Entry{L"Valid", L"Good", idB}};
    registry.skipFirst = true;
    const auto devices = enumerateDevices(registry.api);
    ASSERT_EQ(devices.size(), 1u); EXPECT_EQ(devices[0].name, "Good");
}

TEST(AsioDevices, SnapshotsOwnTheirStringsAndIdsSurviveEnumerationReordering) {
    Registry registry; registry.entries = {Entry{L"First", L"Original", idA}, Entry{L"Second", L"Other", idB}};
    const auto before = enumerateDevices(registry.api);
    registry.entries[0].description = L"Changed"; std::swap(registry.entries[0], registry.entries[1]);
    const auto after = enumerateDevices(registry.api);
    ASSERT_EQ(before.size(), 2u); ASSERT_EQ(after.size(), 2u);
    EXPECT_EQ(before[0].name, "Original"); EXPECT_EQ(after[0].name, "Changed");
    EXPECT_EQ(before[0].id, after[0].id); EXPECT_EQ(before[1].id, after[1].id);
}
}
