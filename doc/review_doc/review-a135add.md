# Review for a135add

Full commit: `a135add335aa9a4f76066300dd097e789257a9e6`

## Findings

### P1 - Invalid `Content-Length` is accepted as an empty body and can desynchronize pipelined requests

- File: `core/web/HttpParser.cpp`
- Lines: 177-183

The `Content-Length` branch uses `clIt->toInt()` without checking the conversion result or validating that the value is non-negative:

```cpp
int bodyLen = clIt->toInt();
```

In Qt, invalid numeric text converts to `0` when the `ok` flag is not checked. That means a request like `content-length: abc` is treated as a complete request with an empty body. If body bytes follow, they remain in the buffer and can be parsed as the next request on keep-alive connections. A negative value is also not rejected, and `bodyStart + bodyLen` / `buffer.mid(bodyStart, bodyLen)` can produce incorrect consumed/body behavior.

Why this matters:

- malformed HTTP should produce `400 Bad Request`, not silently continue;
- on keep-alive or pipelined sockets, bad body framing can corrupt the parse boundary for the next request;
- this sits in `HttpParser`, the architecture boundary that should make transport code trust `consumed`.

Suggested fix for Claude Code:

1. Parse with the `ok` flag:

```cpp
bool ok = false;
int bodyLen = clIt->toInt(&ok);
if (!ok || bodyLen < 0)
    return std::nullopt;
```

2. Consider rejecting duplicate or conflicting `Content-Length` headers if this parser is expected to be exposed beyond trusted local UI traffic.
3. Add tests:
   - `content-length: abc` returns malformed / `std::nullopt`;
   - `content-length: -1` returns malformed / `std::nullopt`;
   - invalid content length plus trailing bytes does not produce `consumed > 0`.

### P2 - New pipelining tests only check `startsWith`, not that the remainder parses correctly

- File: `doc/test/src/tst_HttpParser.cpp`
- Lines: 164-179

The new tests verify that the leftover buffer starts with the next request. That is useful, but it would miss subtler boundary bugs where `consumed` points into a valid-looking prefix or leaves extra CRLF bytes before/after the next request.

Suggested fix for Claude Code:

After computing `rest`, call `tryParseRequest(rest)` and assert the second request is parsed as expected:

```cpp
auto second = tryParseRequest(rest);
QVERIFY(second.has_value());
QCOMPARE(second->request.method, Method::GET);
QCOMPARE(second->request.path, QStringLiteral("/api/status"));
QCOMPARE(second->consumed, static_cast<size_t>(rest.size()));
```

This better protects the `HttpParser` / `WebService` contract: `consumed` must leave a buffer that is directly parseable by the next iteration in `WebService::onHttpReadyRead()`.

## Fixed Since Previous Review

- Chunked parser now returns the actual end position instead of consuming the entire TCP buffer.
- Chunked trailer headers are parsed until the terminating empty line.
- Missing CRLF after chunk data now returns malformed.
- Tests were added for lowercase headers, consumed bounds, chunked pipelining, trailers, and malformed chunk CRLF.

## Architecture Notes

The implementation still follows the intended split: `HttpParser` owns HTTP framing and parse boundaries, while `WebService` owns sockets and dispatch. The remaining `Content-Length` issue should be fixed in `HttpParser`, not in `WebService`, because only the parser can safely define request boundaries.

## Verification

I could not run CMake locally because `cmake` is not available in the current PATH. Review is based on source inspection and the pulled diff.
