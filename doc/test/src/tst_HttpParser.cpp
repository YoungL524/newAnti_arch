/**
 * @file tst_HttpParser.cpp
 * @brief web::HttpParser 单元测试
 */
#include <QtTest>
#include "web/HttpParser.h"

using namespace web;

class TestHttpParser : public QObject {
    Q_OBJECT

private slots:
    // ── GET ──
    void parse_GET_request() {
        QByteArray raw = "GET /api/status HTTP/1.1\r\nHost: localhost:8088\r\nConnection: close\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->request.method, Method::GET);
        QCOMPARE(result->request.path, QStringLiteral("/api/status"));
        QVERIFY(result->request.body.isEmpty());
        QCOMPARE(result->consumed, raw.size());
        QVERIFY(!result->keepAlive);
    }

    void parse_GET_with_query() {
        QByteArray raw = "GET /api/status?page=1 HTTP/1.1\r\nHost: localhost\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->request.path, QStringLiteral("/api/status"));
        QCOMPARE(result->request.queryString, QStringLiteral("page=1"));
    }

    // ── POST with Content-Length ──
    void parse_POST_with_body() {
        QByteArray raw = "POST /api/start HTTP/1.1\r\nHost: localhost\r\nContent-Length: 23\r\n\r\n{\"ok\":true,\"id\"";
        // Incomplete body
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->consumed, 0u);  // not complete

        raw += ":\"test\"}";
        result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->request.method, Method::POST);
        QCOMPARE(result->request.path, QStringLiteral("/api/start"));
        QCOMPARE(result->request.body, QByteArray("{\"ok\":true,\"id\":\"test\"}"));
        QVERIFY(result->consumed > 0);
    }

    void parse_POST_no_body() {
        QByteArray raw = "POST /api/stop HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->request.method, Method::POST);
        QVERIFY(result->request.body.isEmpty());
    }

    // ── Chunked ──
    void parse_chunked_body() {
        // "test" in two chunks: 4\r\ntest\r\n + 3\r\nend\r\n + 0\r\n\r\n
        QByteArray raw = "POST /api/data HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n4\r\ntest\r\n3\r\nend\r\n0\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->request.body, QByteArray("testend"));
        QVERIFY(result->consumed > 0);
    }

    void parse_chunked_incomplete() {
        QByteArray raw = "POST /api/data HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n4\r\ntest\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->consumed, 0u);  // incomplete
    }

    // ── Keep-Alive detection ──
    void parse_keep_alive_http11_default() {
        QByteArray raw = "GET /api/status HTTP/1.1\r\nHost: localhost\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QVERIFY(result->keepAlive);  // HTTP/1.1 defaults to keep-alive
    }

    void parse_keep_alive_explicit() {
        QByteArray raw = "GET / HTTP/1.0\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QVERIFY(result->keepAlive);
    }

    void parse_close_explicit() {
        QByteArray raw = "GET / HTTP/1.1\r\nConnection: close\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QVERIFY(!result->keepAlive);
    }

    // ── Malformed ──
    void parse_malformed_request() {
        QByteArray raw = "GET\r\n\r\n";  // missing path
        auto result = tryParseRequest(raw);
        QVERIFY(!result.has_value());
    }

    // ── serialize ──
    void serialize_response_has_correct_format() {
        HttpResponse resp;
        resp.setStatus(200);
        resp.setContent(QByteArray("hello"), "text/plain");
        QByteArray wire = serializeResponse(resp);
        QVERIFY(wire.contains("HTTP/1.1 200 OK"));
        QVERIFY(wire.contains("content-type: text/plain"));
        QVERIFY(wire.contains("content-length: 5"));
        QVERIFY(wire.endsWith("hello"));
    }

    void serialize_response_with_keep_alive() {
        HttpResponse resp;
        resp.setContent(QByteArray("{}"), "application/json");
        resp.applyKeepAlive();
        QByteArray wire = serializeResponse(resp);
        QVERIFY(wire.contains("connection: keep-alive"));
        QVERIFY(wire.contains("keep-alive: timeout=5"));
    }

    void serialize_error_response() {
        QByteArray wire = serializeResponse(HttpResponse{400, {}, QByteArray("{\"ok\":false}")});
        QVERIFY(wire.startsWith("HTTP/1.1 400 Bad Request"));
    }

    // ── Lowercase headers ──
    void parse_lowercase_content_length() {
        QByteArray raw = "POST /api/start HTTP/1.1\r\nhost: localhost\r\ncontent-length: 6\r\n\r\nabcdef";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->request.body, QByteArray("abcdef"));
    }

    void parse_lowercase_transfer_encoding_chunked() {
        QByteArray raw = "POST /api/data HTTP/1.1\r\nhost: localhost\r\ntransfer-encoding: chunked\r\n\r\n4\r\ntest\r\n0\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->request.body, QByteArray("test"));
    }

    void parse_lowercase_connection_close() {
        QByteArray raw = "GET / HTTP/1.1\r\nconnection: close\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QVERIFY(!result->keepAlive);
    }

    // ── Consumed bounds ──
    void parse_POST_consumed_exact_with_trailing_data() {
        QByteArray raw = "POST /api/start HTTP/1.1\r\nhost: localhost\r\ncontent-length: 3\r\n\r\nabcGET /next HTTP/1.1\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->request.body, QByteArray("abc"));
        // consumed should be exactly after the body, not including the pipelined GET
        QByteArray rest = raw.mid(static_cast<int>(result->consumed));
        QVERIFY(rest.startsWith("GET /next"));
    }

    void parse_chunked_consumed_exact_with_pipelined_get() {
        QByteArray raw = "POST /api/data HTTP/1.1\r\nhost: localhost\r\ntransfer-encoding: chunked\r\n\r\n4\r\ntest\r\n0\r\n\r\nGET /api/status HTTP/1.1\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->request.body, QByteArray("test"));
        QByteArray rest = raw.mid(static_cast<int>(result->consumed));
        QVERIFY(rest.startsWith("GET /api/status"));
    }

    void parse_chunked_consumed_exact_with_trailer_and_pipelined_get() {
        QByteArray raw = "POST /api/data HTTP/1.1\r\nhost: localhost\r\ntransfer-encoding: chunked\r\n\r\n4\r\ntest\r\n0\r\nX-Trailer: ok\r\n\r\nGET /api/status HTTP/1.1\r\n\r\n";
        auto result = tryParseRequest(raw);
        QVERIFY(result.has_value());
        QCOMPARE(result->request.body, QByteArray("test"));
        QByteArray rest = raw.mid(static_cast<int>(result->consumed));
        QVERIFY(rest.startsWith("GET /api/status"));
    }

    // ── Malformed chunk CRLF ──
    void parse_chunked_missing_crlf_returns_malformed() {
        QByteArray raw = "POST /api/data HTTP/1.1\r\nhost: localhost\r\ntransfer-encoding: chunked\r\n\r\n4\r\ntest0\r\n\r\n";  // missing CRLF after 'test'
        auto result = tryParseRequest(raw);
        QVERIFY(!result.has_value());
    }
};

QTEST_MAIN(TestHttpParser)
#include "tst_HttpParser.moc"
