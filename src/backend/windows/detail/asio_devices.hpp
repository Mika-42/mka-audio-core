#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "asio_types.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

namespace mka_asio_detail {
struct ChannelCounts { std::uint32_t inputs = 0, outputs = 0; };
struct Device {
    std::string id, name;
    std::optional<ChannelCounts> channels;
    std::vector<std::uint32_t> bufferSizes;
    std::optional<Sample> inputFormat, outputFormat;
    std::vector<std::uint32_t> sampleRates;
};
struct RegistryApi {
    decltype(&RegOpenKeyExW) open = &RegOpenKeyExW;
    decltype(&RegEnumKeyExW) enumerate = &RegEnumKeyExW;
    decltype(&RegQueryValueExW) query = &RegQueryValueExW;
    decltype(&RegCloseKey) close = &RegCloseKey;
    decltype(&GetFileAttributesW) attributes = &GetFileAttributesW;
    decltype(&ExpandEnvironmentStringsW) expand = &ExpandEnvironmentStringsW;
};
class RegistryKey {
public:
    RegistryKey(const RegistryApi& api, HKEY parent, const wchar_t* path) : api_(api) {
        if (api_.open(parent, path, 0, KEY_READ, &key_) != ERROR_SUCCESS) key_ = nullptr;
    }
    ~RegistryKey() { if (key_) api_.close(key_); }
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    explicit operator bool() const noexcept { return key_ != nullptr; }
    HKEY get() const noexcept { return key_; }
private:
    const RegistryApi& api_;
    HKEY key_ = nullptr;
};
inline std::optional<std::wstring> registryString(const RegistryApi& api, HKEY key,
                                                const wchar_t* name, bool expand = false) {
    DWORD type = 0, bytes = 0;
    if (api.query(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS
        || (type != REG_SZ && !(expand && type == REG_EXPAND_SZ))
        || bytes < sizeof(wchar_t) || bytes > 65536 || bytes % sizeof(wchar_t)) return {};
    const auto originalType = type;
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (api.query(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS
        || type != originalType || bytes < sizeof(wchar_t) || bytes % sizeof(wchar_t)
        || bytes / sizeof(wchar_t) > value.size()) return {};
    value.resize(bytes / sizeof(wchar_t));
    if (value.back() != L'\0' || std::find(value.begin(), value.end() - 1, L'\0') != value.end() - 1) return {};
    value.pop_back();
    if (type == REG_EXPAND_SZ) {
        const auto count = api.expand(value.c_str(), nullptr, 0);
        if (!count || count > 32768) return {};
        std::wstring expanded(count, L'\0');
        if (api.expand(value.c_str(), expanded.data(), count) != count || expanded.back() != L'\0') return {};
        expanded.pop_back();
        return expanded;
    }
    return value;
}
inline std::string asioUtf8(const std::wstring& value) {
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) return {};
    std::string output(static_cast<std::size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), -1, output.data(), count, nullptr, nullptr)) return {};
    output.pop_back();
    return output;
}
inline std::optional<std::wstring> asioClassId(const std::wstring& text) {
    if (text.size() != 38 || text.front() != L'{' || text.back() != L'}') return {};
    CLSID clsid{};
    const CLSID empty{};
    if (FAILED(CLSIDFromString(text.c_str(), &clsid)) || IsEqualGUID(clsid, empty)) return {};
    std::array<wchar_t, 39> normalized{};
    if (StringFromGUID2(clsid, normalized.data(), static_cast<int>(normalized.size())) != 39) return {};
    return std::wstring(normalized.data());
}

// Registry discovery reads metadata and checks the registered DLL. It never
// instantiates COM objects, initializes an ASIO driver or starts audio.
inline std::vector<Device> enumerateDevices(const RegistryApi& api = {}) {
    std::vector<Device> result;
    std::unordered_set<std::string> seen;
    for (const auto hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        RegistryKey root(api, hive, L"SOFTWARE\\ASIO");
        if (!root) continue;
        for (DWORD index = 0;; ++index) {
            std::array<wchar_t, 256> name{};
            DWORD length = static_cast<DWORD>(name.size());
            const auto error = api.enumerate(root.get(), index, name.data(), &length, nullptr, nullptr, nullptr, nullptr);
            if (error != ERROR_SUCCESS && error != ERROR_MORE_DATA) break;
            if (error == ERROR_SUCCESS && length && length < name.size()
                && std::find(name.begin(), name.begin() + length, L'\0') == name.begin() + length) {
                RegistryKey entry(api, root.get(), name.data());
                if (entry) {
                    const auto raw = registryString(api, entry.get(), L"CLSID");
                    const auto clsid = raw ? asioClassId(*raw) : std::nullopt;
                    if (clsid) {
                        RegistryKey server(api, HKEY_CLASSES_ROOT, (L"CLSID\\" + *clsid + L"\\InprocServer32").c_str());
                        auto path = server ? registryString(api, server.get(), nullptr, true) : std::nullopt;
                        if (path && !path->empty()) {
                            if (path->size() >= 2 && path->front() == L'"' && path->back() == L'"')
                                *path = path->substr(1, path->size() - 2);
                            const auto attributes = path->empty() ? INVALID_FILE_ATTRIBUTES : api.attributes(path->c_str());
                            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                                auto id = "asio:" + asioUtf8(*clsid);
                                for (auto& ch : id) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
                                if (seen.insert(id).second) {
                                    const auto description = registryString(api, entry.get(), L"Description");
                                    auto label = description ? asioUtf8(*description) : std::string{};
                                    if (label.empty()) label = asioUtf8(std::wstring(name.data(), length));
                                    if (label.empty()) label = id;
                                    result.push_back({std::move(id), std::move(label)});
                                }
                            }
                        }
                    }
                }
            }
            if (index == MAXDWORD) break;
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}
inline std::vector<Device> nativeDevices() { return enumerateDevices(); }
}
