#include <expected>
#include <limits>
#include <string>
#include <gtest/gtest.h>
import mka.audio.diagnostics;
#include "error_details.hpp"
using namespace mka::audio::core;
using namespace mka_study::diagnostics;
static EndpointConfig request(){return {std::string("\xc3\xa9\0\xff",4),Direction::Output,0,2,48000,Format::Float32,256};}
TEST(StructuredDiagnostics, OwnsOpaqueIdentityAndPreservesUint64StatusAfterControlReuse) {
    auto config=request();Status status{};status.xruns=UINT64_MAX;status.failed=true;status.realtime=RealtimeState::No;
    ErrorDetails details;ASSERT_TRUE(captureError(ErrorType::EndpointUnavailable,Operation::Open,config,status,details));
    config.id="changed";EXPECT_EQ(details.endpoint,std::string("\xc3\xa9\0\xff",4));
    std::string json;ASSERT_TRUE(serialize(details,json));
    EXPECT_NE(json.find("\"id\":\"c3a900ff\""),std::string::npos);
    EXPECT_NE(json.find("\"xruns\":\"18446744073709551615\""),std::string::npos);
    EXPECT_NE(json.find("\"code\":null"),std::string::npos);EXPECT_NE(json.find("\"realtime\":\"No\""),std::string::npos);
}
TEST(StructuredDiagnostics, UnknownEnumsAndKnownZeroOrInt64MinimumNativeCodesAreDistinct) {
    ErrorDetails details;auto config=request();
    ASSERT_TRUE(captureError(static_cast<ErrorType>(99),static_cast<Operation>(99),config,{},details,{static_cast<Domain>(99),INT64_MIN}));
    std::string json;ASSERT_TRUE(serialize(details,json));
    EXPECT_NE(json.find("\"name\":\"Unknown\""),std::string::npos);
    EXPECT_NE(json.find("\"operation\":\"Unknown\""),std::string::npos);
    EXPECT_NE(json.find("\"code\":\"-9223372036854775808\""),std::string::npos);
    details.native={Domain::Alsa,0};ASSERT_TRUE(serialize(details,json));EXPECT_NE(json.find("\"code\":\"0\""),std::string::npos);
}
TEST(StructuredDiagnostics, CapsCopiedIdentityAndReportsOriginalLengthAndTruncation) {
    auto config=request();config.id.assign(4113,'x');ErrorDetails details;
    ASSERT_TRUE(captureError(ErrorType::InvalidState,Operation::Start,config,{},details));
    EXPECT_EQ(details.endpoint.size(),4096u);EXPECT_EQ(details.originalIdBytes,4113u);EXPECT_TRUE(details.truncated);
    std::string json;ASSERT_TRUE(serialize(details,json));EXPECT_LT(json.size(),10240u);
    EXPECT_NE(json.find("\"id_truncated\":true"),std::string::npos);
    config.id.clear();ASSERT_TRUE(captureError(ErrorType::InvalidState,Operation::Close,config,{},details));
    EXPECT_TRUE(details.endpoint.empty());EXPECT_FALSE(details.truncated);EXPECT_EQ(details.originalIdBytes,0u);
}
TEST(StructuredDiagnostics, RejectsInvalidUnboundedRecordsWithoutReplacingThePreviousJson) {
    ErrorDetails details;details.endpoint.assign(4097,'a');std::string json="previous";
    auto result=serialize(details,json);ASSERT_FALSE(result);EXPECT_EQ(result.error(),ErrorType::BufferSizeNotSupported);EXPECT_EQ(json,"previous");
    static_assert(noexcept(serialize(details,json)));
}
