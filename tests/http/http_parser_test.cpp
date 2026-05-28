#include "spp/http/http_parser.h"

#include "spp/http/http_message.h"

#include <string>

#include <gtest/gtest.h>

namespace {

spp::http::HttpRequest ParseOk(std::string_view raw) {
    spp::http::HttpRequest req;
    spp::http::ParseLimits lim;
    auto r = spp::http::TryParseRequest(raw, lim, req);
    EXPECT_EQ(r.status, spp::http::ParseStatus::kComplete);
    return req;
}

}  // namespace

TEST(HttpParserTest, GetWithQueryAndHeaders) {
    auto req = ParseOk(
        "GET /wiki/_search?q=foo&size=5 HTTP/1.1\r\n"
        "Host: x\r\n"
        "Accept: */*\r\n"
        "\r\n");
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.path, "/wiki/_search");
    EXPECT_EQ(req.query, "q=foo&size=5");
    EXPECT_EQ(req.Header("host"), "x");
    auto qp = req.QueryParams();
    EXPECT_EQ(qp["q"], "foo");
    EXPECT_EQ(qp["size"], "5");
}

TEST(HttpParserTest, PostWithContentLength) {
    auto req = ParseOk(
        "POST /wiki/_doc HTTP/1.1\r\n"
        "Content-Length: 9\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "{\"a\":42}\n");
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.body, "{\"a\":42}\n");
}

TEST(HttpParserTest, IncompleteHeadersReturnsNeedMore) {
    spp::http::HttpRequest req;
    spp::http::ParseLimits lim;
    auto r = spp::http::TryParseRequest("GET / HTTP/1.1\r\nHost: x", lim, req);
    EXPECT_EQ(r.status, spp::http::ParseStatus::kNeedMore);
}

TEST(HttpParserTest, MalformedRequestLineErrors) {
    spp::http::HttpRequest req;
    spp::http::ParseLimits lim;
    auto r = spp::http::TryParseRequest("BADLINE\r\n\r\n", lim, req);
    EXPECT_EQ(r.status, spp::http::ParseStatus::kError);
    EXPECT_EQ(r.http_status_on_error, 400);
}

TEST(HttpParserTest, RejectsUnsupportedVersion) {
    spp::http::HttpRequest req;
    spp::http::ParseLimits lim;
    auto r = spp::http::TryParseRequest("GET / HTTP/2.0\r\n\r\n", lim, req);
    EXPECT_EQ(r.status, spp::http::ParseStatus::kError);
    EXPECT_EQ(r.http_status_on_error, 505);
}

TEST(HttpParserTest, RejectsTransferEncoding) {
    spp::http::HttpRequest req;
    spp::http::ParseLimits lim;
    auto r = spp::http::TryParseRequest(
        "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", lim, req);
    EXPECT_EQ(r.status, spp::http::ParseStatus::kError);
}

TEST(UrlDecodeTest, BasicAndPercentAndPlus) {
    EXPECT_EQ(spp::http::UrlDecode("hello"), "hello");
    EXPECT_EQ(spp::http::UrlDecode("a+b"), "a b");
    EXPECT_EQ(spp::http::UrlDecode("a%20b"), "a b");
    EXPECT_EQ(spp::http::UrlDecode("%2F%2f"), "//");
}
