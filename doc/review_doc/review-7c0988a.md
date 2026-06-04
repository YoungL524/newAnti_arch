# Review for 7c0988a

Full commit: `7c0988a8410ca6a5966c21b3f2a35ea1b1c79f06`

## Findings

### P1 - Chunked pipelining still loses the next request when trailers are present or absent

- File: `core/web/HttpParser.cpp`
- Lines: 93-99, 153-159

`decodeChunked()` sets `endPos` by starting at the byte after the zero-size chunk line and skipping every following `\r` or `\n` byte:

```cpp
int trailEnd = lf + 1;
while (trailEnd < dataSize && (data[trailEnd] == '\r' || data[trailEnd] == '\n'))
    trailEnd++;
endPos = trailEnd;
```

This handles the simplest `0\r\n\r\n` case, but it does not parse the chunked trailer section. For a valid request like:

```text
POST /api/data HTTP/1.1
Transfer-Encoding: chunked

4
test
0
X-Trailer: ok

GET /api/status HTTP/1.1

```

`endPos` becomes the start of `X-Trailer: ok` instead of the start of the pipelined `GET`, so the next parse treats a trailer header as a request line and returns a malformed request. Depending on the exact bytes, this can either drop the pipelined request or close the connection with `400 Bad Request`.

There is also no explicit verification that each chunk data block is followed by `\r\n`; the code just skips two bytes after the body. Malformed input can therefore advance the parser over arbitrary bytes.

Suggested fix for Claude Code:

1. Make `decodeChunked()` parse according to chunked framing:
   - chunk-size line ends with CRLF,
   - chunk data must be followed by CRLF,
   - zero-size chunk is followed by zero or more trailer header lines,
   - the trailer section ends at the first empty line, i.e. CRLF.
2. Set `endPos` to the byte immediately after the trailer terminator, not just after the zero chunk line.
3. Return `Malformed` when the expected CRLF after chunk data is missing.
4. Add tests for:
   - `chunked + pipelined GET` with no trailers,
   - `chunked + trailer header + pipelined GET`,
   - malformed chunk missing the CRLF after chunk data.

### P2 - Tests do not cover the bug that this commit intended to fix

- File: `doc/test/src/tst_HttpParser.cpp`

The commit fixes case-insensitive request headers and chunked `consumed`, but the tests still only cover canonical header names and a standalone chunked request. That means regressions in lowercase headers and pipelined chunked parsing can slip through.

Suggested fix for Claude Code:

1. Add a lowercase header test:
   - `content-length`,
   - `connection: close`,
   - `transfer-encoding: chunked`.
2. Add an assertion that `result->consumed` equals the exact end of the first request when extra bytes follow.
3. Add a chunked pipelining test using two requests in one buffer, then parse the remainder with `tryParseRequest()`.

## Architecture Notes

The direction is consistent with the current architecture: `HttpParser` owns protocol parsing/serialization and `WebService` owns socket lifecycle/transport. The latest changes keep that separation intact.

The README/AppController `HttpServer` references from the previous review were cleaned up in the touched files. Remaining references from `rg` are in older business docs / generated HTML / UI type comments and are not runtime risks.

## Verification

I could not run CMake locally because `cmake` is not available in the current PATH. Review is based on source inspection and the pulled diff.
