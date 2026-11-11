#pragma once
#include <charconv>
#include <string_view>
#include "directsound_types.hpp"

namespace mka_ds_detail {
inline bool emptyGuid(const GUID& guid) noexcept {
    if (guid.Data1 || guid.Data2 || guid.Data3) return false;
    for (const auto byte : guid.Data4) if (byte) return false;
    return true;
}
inline std::string guidString(const GUID& guid) {
    std::string result{"{"};
    const auto append = [&](std::uint32_t value, unsigned digits) {
        constexpr char hex[] = "0123456789abcdef";
        for (unsigned index = digits; index; --index) result.push_back(hex[(value >> ((index - 1) * 4)) & 15]);
    };
    append(guid.Data1, 8); result += '-'; append(guid.Data2, 4); result += '-'; append(guid.Data3, 4); result += '-';
    append(guid.Data4[0], 2); append(guid.Data4[1], 2); result += '-';
    for (unsigned index = 2; index < 8; ++index) append(guid.Data4[index], 2);
    result += '}';
    return result;
}
struct DeviceId { Flow flow; std::optional<GUID> guid; };
inline std::string deviceId(const GUID* guid, Flow flow) {
    if (flow == Flow::duplex || (guid && emptyGuid(*guid))) return {};
    const auto prefix = flow == Flow::input ? "directsound:input:" : "directsound:output:";
    return std::string(prefix) + (guid ? guidString(*guid) : "default");
}
inline std::optional<DeviceId> parseDeviceId(std::string_view text) noexcept {
    Flow flow;
    if (text.starts_with("directsound:output:")) { flow = Flow::output; text.remove_prefix(19); }
    else if (text.starts_with("directsound:input:")) { flow = Flow::input; text.remove_prefix(18); }
    else return std::nullopt;
    if (text == "default") return DeviceId{flow, std::nullopt};
    if (text.size() != 38 || text.front() != '{' || text.back() != '}' || text[9] != '-' || text[14] != '-'
        || text[19] != '-' || text[24] != '-') return std::nullopt;
    const auto read = [&](std::size_t offset, std::size_t count, std::uint32_t& value) noexcept {
        const auto* first = text.data() + offset;
        const auto parsed = std::from_chars(first, first + count, value, 16);
        // Require exactly hex digits, with no signs, whitespace or prefixes.
        for (std::size_t index = 0; index < count; ++index) {
            const auto c = first[index];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        }
        return parsed.ec == std::errc{} && parsed.ptr == first + count;
    };
    GUID guid{}; std::uint32_t value = 0;
    if (!read(1, 8, value)) return std::nullopt; guid.Data1 = value;
    if (!read(10, 4, value)) return std::nullopt; guid.Data2 = static_cast<WORD>(value);
    if (!read(15, 4, value)) return std::nullopt; guid.Data3 = static_cast<WORD>(value);
    for (unsigned index = 0; index < 8; ++index) {
        if (!read(index < 2 ? 20 + 2 * index : 25 + 2 * (index - 2), 2, value)) return std::nullopt;
        guid.Data4[index] = static_cast<BYTE>(value);
    }
    if (emptyGuid(guid)) return std::nullopt;
    return DeviceId{flow, guid};
}
}
