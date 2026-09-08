# CUBRID page-buffer inspector wire contract, version 1.0

Issue: [CBRD-27398](http://jira.cubrid.org/browse/CBRD-27398).

This document is the byte-exact syntax of the state-only page-buffer inspector
protocol that a `cub_server` with the optional inspector enabled speaks to a
local consumer over a Unix domain stream socket. It exists so that the producer
and every consumer test against one fixed syntax and one pinned corpus instead
of prose. The [conformance corpus](corpus/README.md) beside it pins example
streams and expected outcomes; [manifest.json](manifest.json) records the
contract version, the corpus revision and the corpus aggregate hash.

## 1. Semantic authority

The semantics of this protocol were settled in the accepted design decisions
recorded for CBRD-27398: the Unix stream transport with versioned JSON lines,
the startup-only enablement parameter, the state-only wire vocabulary, the
peer and database identity requirements, the exact LRU-list membership fields,
the resource limits and the verification strategy. This document does not
restate or reopen those decisions. Where a decision fixed a meaning but left the
byte form open, this document chooses the form and says so. Where this document
and a decision appear to disagree, the decision is amended explicitly; the
syntax is never adjusted silently.

A reader must keep three facts in mind that the decisions state and this
document repeats because they bound every interpretation of the data:

- A scan is a traversal of a changing buffer pool. Nothing in a scan is an
  atomic snapshot of the pool, and nothing in a page frame is an atomic
  snapshot of the page. Only the tuples that this document says come from one
  load are coherent with each other, and only at the instant of that load.
- A page identity that matches a page on disk proves nothing about whether
  the buffered image equals the on-disk image, whether any change is visible to
  a transaction, or whether anything is durable.
- Omission of a page from a scan means "observed not resident" only when the
  scan's footer says the scan was complete. Omission from a truncated scan
  means unknown.

## 2. Transport and framing

The producer listens on a Unix domain socket of type `SOCK_STREAM`. All data in
both directions is a sequence of frames.

A frame is exactly one JSON object, encoded in UTF-8, followed by exactly one
line feed (byte `0x0A`). The JSON text of a frame never contains a raw line
feed, so the line feed is the unambiguous frame terminator. Frames are objects
only: a frame whose JSON text is an array, a string, a number or a literal is
malformed.

Every frame carries the string field `type` as its first key. The frame kinds
of version 1 are listed in section 8.

## 3. Canonical producer form

The producer writes every frame in one canonical form. The corpus pins that
form byte for byte, so a producer that deviates from it fails conformance even
when the JSON it writes is semantically equivalent.

- Keys appear in the order this document lists them for the frame kind, with
  `type` first.
- No whitespace appears outside string values.
- Integers are written in base 10 with no leading zeros, a leading `-` only
  when negative, and never with a fraction or exponent.
- Booleans are the literals `true` and `false`; an absent-but-known value is
  the literal `null` where the frame kind allows it.
- Strings use minimal escaping: `"` becomes `\"` and `\` becomes `\\`. No other
  escape is produced. Every string the producer emits is ASCII by construction:
  it is a documented enumeration value, a hexadecimal identifier or a code.
  A producer never emits a string containing bytes outside the printable ASCII
  range `0x20` to `0x7E`.

The canonical form binds the producer only. A consumer accepts any JSON that
is valid under RFC 8259 and within the limits of section 4. A consumer that is
byte-strict couples itself to formatting rather than to meaning and is not
conforming.

## 4. Limits

All limits are binary byte counts that include the terminating line feed.

| Limit | Value |
| --- | --- |
| Control frame (`client_hello`, `error`, `scan_request`, `scan_header`, `scan_footer`) | at most 4,096 bytes |
| Page frame (`page`) | at most 4,096 bytes |
| Handshake frame (`server_hello`) | at most 65,536 bytes |
| JSON nesting depth | at most 16, counting the frame object itself as depth 1 and each nested object or array as one more |
| Whole scan, header through footer | at most 64 MiB (67,108,864 bytes) |
| Slots visited per scan | at most 65,536 |
| Page frames emitted per scan | at most 65,536 |

The producer enforces these at encode time and never writes a frame that
violates them. A frame that would exceed its limit is not emitted at all; the
producer reports the condition internally and treats it as a defect, because
the vocabulary of version 1 cannot produce such a frame from valid values. The
consumer enforces the same limits before allocating for a frame or a scan, so
that a producer bug or a hostile peer cannot make it allocate without bound.

## 5. Absent versus null

Two different facts share the surface of "no value":

- A field this document marks **optional** may be absent from a frame. Absence
  means the producer did not supply the field. A consumer treats the field as
  unknown, not as any particular value.
- A field this document marks **nullable** may carry the literal `null`.
  `null` means the value is known to be empty or not applicable, for example a
  list index for a page that is on no list.

Fields that this document marks **mandatory** are never optional. They are the
fields that identify a frame, a page, a scan or a producer, or that carry a
count the consumer must verify. A frame missing a mandatory field is malformed.
For frames inside a scan this discards the whole scan assembly, not just the
frame; the consumer keeps only the previous complete or partial scan it had
already published.

## 6. Forward compatibility and versioning

The protocol version is a pair of integers, `protocol_major` and
`protocol_minor`. This document describes major 1, minor 0.

Within one major version, evolution beyond what version 1.0 defines is
additive only:

- A consumer ignores any field it does not know, in any frame, and still
  processes the frame. The bytes of an unknown field count against the frame
  limit and the scan limit like any other bytes.
- A consumer ignores any frame kind it does not know when that frame appears
  between a `scan_header` and its `scan_footer`. Such a frame counts against
  the scan byte limit but not against the page count. An unknown frame kind
  outside a scan is a protocol violation.
- A consumer treats a string it does not recognize in any enumeration field
  as an unknown state. The frame remains valid. In particular a newer producer
  may emit an enumeration value this document does not list, and an older
  consumer keeps working.
- A producer adds a field, an enumeration value or a frame kind beyond those
  version 1.0 defines only together with an increment of `protocol_minor`, a
  new corpus revision and new conformance cases. Completing the field tables
  of the seven version 1.0 frame kinds (section 8) is not such an addition.

An incompatible change starts a new major version. It is published as a new
contract directory beside this one, never as an edit to this document.

## 7. Never on the wire

The producer never writes, in any frame or version, a memory address, a
thread or process identifier, a raw flag word, a raw page-type ordinal, a
packed LRU index, a native list counter, quota or tick, any page content or a
hash of page content, a file system path, or the text of an operating system
error. Refusal frames carry stable codes and never free text. These exclusions
are not limits to be negotiated; they are part of what makes enabling the
inspector safe.

## 8. Frame kinds

| Kind | Direction | Limit class | Specified |
| --- | --- | --- | --- |
| `client_hello` | client to server | control | pending in this contract's next revision |
| `server_hello` | server to client | handshake | pending |
| `error` | server to client | control | section 9 |
| `scan_request` | client to server | control | pending |
| `scan_header` | server to client | control | pending |
| `page` | server to client | page | pending |
| `scan_footer` | server to client | control | pending |

A kind marked pending already belongs to version 1.0: its name and direction
are fixed by the accepted decisions, and later corpus revisions complete its
field table in this document before any producer emits it. That completion
does not change `protocol_minor`; section 6 governs additions beyond it. The
frame kinds of version 1 are exactly these seven.

## 9. The `error` frame

The producer sends an `error` frame when it refuses a client hello or a scan
request. It never sends an `error` frame between a `scan_header` and its
`scan_footer`; a scan that cannot continue ends with a footer marked truncated,
and a producer that must abandon a scan for any other reason closes the
connection without a footer.

Fields, in canonical order:

| Field | JSON type | Presence | Meaning |
| --- | --- | --- | --- |
| `type` | string `"error"` | mandatory | Frame kind |
| `code` | string | mandatory | One of the stable codes below |
| `supported_majors` | array of positive integers | only with `version-unsupported` | Every protocol major the producer can speak |
| `retry_after_ms` | positive integer | only with `rate-limited` | Milliseconds until the producer will accept a scan request |

A field marked "only with" a code is a producer obligation: the producer sends
it exactly when it sends that code and never otherwise, and a producer that
omits it with its own code or sends it with another code is defective. For the
consumer both fields are optional: a refusal whose detail is absent is still a
refusal with that code, and a detail that arrives with another code is ignored.

Stable codes:

| Code | Sent when | After the frame |
| --- | --- | --- |
| `version-unsupported` | The client hello lists no protocol major the producer speaks | The producer closes the connection |
| `busy` | Two clients are already attached | The producer closes the connection |
| `incarnation-changed` | The client hello names an expected incarnation and it is not this server's incarnation | The producer closes the connection; the client drops every retained observation and may reconnect without an expectation |
| `identity-oversized` | The complete permanent-volume identity set cannot fit within the handshake frame limit | The producer closes the connection; it never sends a partial identity set instead |
| `rate-limited` | A scan request arrives before 100 ms have passed since the previous scan started, counting scans of every client | The connection stays open; the client may send another request after `retry_after_ms` |

The code `parameter-off` is reserved and is never sent by a version 1 producer.
A producer whose enablement parameter is off creates no socket, so there is no
connection on which to send it. A consumer may accept the code defensively;
it must not infer the parameter's value from the absence of a socket alone.

Examples in canonical form:

```text
{"type":"error","code":"busy"}
{"type":"error","code":"version-unsupported","supported_majors":[1]}
{"type":"error","code":"rate-limited","retry_after_ms":60}
```

Each example is one frame and is followed on the wire by one line feed.

## 10. Conformance corpus

The directory [corpus](corpus/README.md) holds one case per subdirectory with
the exact bytes a consumer reads, the expected outcome, and for cases the
producer generates, the semantic input those bytes were produced from. The
file `corpus/SHA256SUMS` lists every corpus file with its SHA-256 digest in the
format `sha256sum` reads, and [manifest.json](manifest.json) records the
SHA-256 of that file as the corpus aggregate hash. A consumer vendors the
corpus directory verbatim, records the aggregate hash and the producer commit
it was taken from, and verifies both offline.

The producer's own unit tests reproduce every generated stream byte for byte
from its semantic input and recompute the aggregate hash. They are built with
the CMake option `UNIT_TEST_PGBUF_INSPECTOR` and run as the executable
`test_pgbuf_inspector`.

## 11. Change control

Any change to a byte under `corpus/` changes `SHA256SUMS`, changes the
aggregate hash, and increments `corpus_revision` in the manifest. Both the
producer and every consumer must show passing offline checks against the new
hash before the change is delivered. A change to this document that alters
what any byte means is a change to the corpus as well and carries new cases.
A change to this document that alters wording only does not change the hash.
