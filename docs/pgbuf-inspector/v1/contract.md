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
byte form open, this document chooses the form and says so. A conflict with a decision requires explicit design review; concrete syntax
does not override the accepted semantics.

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

Every frame carries the string field `type`; canonical producer output puts it first. The frame kinds
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
ordinary state records are small. Oversized identity is instead refused with
`identity-oversized`, and scan budgets reserve footer space before emission. The
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
  new corpus revision and new conformance cases.

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
| `client_hello` | client to server | control | section 12 |
| `server_hello` | server to client | handshake | section 12 |
| `error` | server to client | control | section 9 |
| `scan_request` | client to server | control | section 12 |
| `scan_header` | server to client | control | section 12 |
| `page` | server to client | page | section 12 |
| `scan_footer` | server to client | control | section 12 |

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
| `retry_after_ms` | integer 1..4294967295 | only with `rate-limited` | Milliseconds until the producer will accept a scan request |

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

## 12. Complete frame schemas

The tables list canonical key order after `type`. All listed fields are
mandatory unless marked optional. Identifiers `incarnation` and
`expected_incarnation` are 32 lowercase hex characters (128 unpredictable bits).
Every uint64 value is a canonical decimal **string**, from "0" through
"18446744073709551615", with no sign or leading zero. This prevents JSON
number rounding in consumers. Counts, versions, volid, pageid and fix counts
are JSON integers, bounded as below. A timestamp is uint64 Unix microseconds;
wall-clock steps may make an end timestamp earlier than a start timestamp.
These times are display metadata, never the freshness clock.

| Frame | Fields in canonical order |
| --- | --- |
| client_hello | supported_majors: nonempty array of positive int32; expected_incarnation: optional hex id |
| server_hello | protocol_major: 1; protocol_minor: nonnegative int32; incarnation: hex id; database_creation: uint64 string; volumes: nonempty array of identity objects; shared_lru_count, private_lru_count: nonnegative int32 |
| scan_request | incarnation: hex id |
| scan_header | incarnation: hex id; scan_seq: positive uint64 string; start_time_us: uint64 string |
| page | incarnation: hex id; scan_seq: positive uint64 string; volid: integer 0..32767; pageid: integer 0..2147483647; optional state fields below |
| scan_footer | incarnation: hex id; scan_seq: positive uint64 string; end_time_us: uint64 string; record_count, visited_slots: integers 0..65536; truncated: Boolean |

An identity object contains exactly these canonical fields: `volid` (0..32767),
`volume_creation`, `device`, `inode` (uint64 strings). Volume ids are unique.
The array is sorted by volid in producer output. Database creation and volume
creation use the engine's creation value expressed as unsigned seconds since
Unix epoch. Device and inode are the OS file identities, not paths. Temporary
volumes do not participate. Complete-set equality is verified by the attaching
consumer; syntax validation alone is not identity authentication.

Page state fields, in canonical order:

| Field | Type / vocabulary |
| --- | --- |
| latch_mode | string: none, read, write, flush, unknown |
| waiter_present | Boolean |
| fix_count | integer 0..2147483647 |
| dirty, flushing, async_flush_requested, to_vacuum | Boolean |
| lru_zone | string: lru1, lru2, lru3, void, invalid |
| lru_list_kind | string: shared, private, none, invalid |
| lru_list_index | nonnegative int32 or null |
| page_lsa, oldest_unflush_lsa | null or object with pageid (uint64 string 0..9223372036854775807), offset (integer 0..32767) |
| page_kind | string: unknown, ftab, heap, volheader, volbitmap, qresult, ehash, overflow, oos, area, catalog, btree, log, dropped_files, vacuum_data |

Null LSA represents the engine's null LSA, not a negative address encoded as
unsigned. Invalid internal latch/page-kind values become `unknown`; invalid
internal LRU zone or index becomes the documented invalid state. OOS's native
ordinal 8 maps to oos; develop's ordinal 8 maps to area, with subsequent values
shifted. Native ordinals never appear in a frame. A producer includes the full
state set it sampled; readers may accept absent state fields as unknown.
Unknown future enum strings are accepted by readers as unknown evidence, but
v1.0 encoders reject them. Known LRU kind/index pairs must be consistent:
shared/private require a nonnull index within the handshake topology;
none/invalid require null. Known LRU zones cannot carry none; they carry
shared/private or invalid membership. Non-LRU zones carry none/null. Apply
these consistency checks only to recognized enum values: future LRU strings
remain unknown evidence and do not invalidate the capture. Optional missing
fields cannot establish membership.

## 13. Ordering, publication and bounded execution

One connection begins with client_hello and receives server_hello or a terminal
error. A successful hello is followed by sequential scan_request / scan_header /
zero or more page / scan_footer exchanges. A client sends no request while a
scan is in flight. A matching request incarnation is required. Sequence numbers
strictly increase across scans in one server incarnation (gaps are allowed).
A rate-limited refusal returns to ready; terminal refusals close the exchange.
Unexpected known frame kinds, duplicate JSON member names, invalid UTF-8,
invalid value types or malformed framing close the exchange and discard its
unfinished capture. No new free-text protocol error is emitted. Peer refusal
happens before this protocol begins. An incarnation mismatch invalidates the
connection; the producer can send incarnation-changed before a scan begins.

A complete scan footer validates record count before deduplication, visited
slots at least record count, matching sequence/incarnation and the total byte
budget. Only then is the capture published. Duplicate VPIDs are ambiguous,
not last-write-wins. The verifier's scoped lookup returns unknown before a
valid footer, for an unevaluated VPID, or for an omitted VPID in a truncated
scan. Complete omission is observed nonresidency, not proof of current absence.
No observations from separate scans are merged. A failed assembly never
becomes a valid partial capture; a client may retain only its prior usable,
unexpired capture with original age independently of this verifier.

Unknown frame kinds inside a scan are bounded control frames, count toward
scan bytes but not record count, and cannot establish observations. Producer
v1.0 emits only the seven declared kinds. Unknown optional fields are bounded
and ignored. Readers accept field reordering and whitespace; producer encoding
emits only schema fields in canonical order. Depth, duplicate-member and frame
bounds apply to unknown values too, before DOM allocation.

Reserve footer capacity before admitting a record. Visit a slot at most once
and rotate the next start beyond the visited span after truncation. Hitting a
limit exactly on completion need not mean truncation. A stopped traversal is
partial even if few resident records were emitted. The wire validates declared
slot counts but only live collector tests can prove actual traversal.

At most two clients attach. A global 100 ms scan-start floor applies across
clients. Traversal/serialization has a 100 ms elapsed deadline from traversal
start, including backpressure, checked between slots. Output buffering is
64 KiB, stall disconnection occurs after 250 ms without write progress,
connect plus handshake has a 500 ms deadline, and the whole scan exchange
including drain/footer has a 2 s deadline. Cancellation on disconnect/shutdown
releases resources. No deadline permits retaining a page latch or blocking a
database worker. These are scheduling-aware bounds, not real-time guarantees;
offline tests validate wire limits, not socket timing or collector behavior.
