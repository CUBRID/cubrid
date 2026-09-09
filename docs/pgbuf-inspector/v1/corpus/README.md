# Conformance corpus for wire contract version 1.0

This directory is the shared test oracle for the page-buffer inspector wire
contract described in [../contract.md](../contract.md). The producer in this
repository and every consumer test against the same bytes here, offline and
independently, so that a disagreement about the wire is found by a unit test
and not by a live integration run.

## Layout

The original refusal cases are top-level subdirectories. Such a case
holds up to three files:

| File | Written by | Meaning |
| --- | --- | --- |
| `stream.jsonl` | the generator, from `semantic.json` | The exact bytes the consumer reads from the socket for this case, server to client |
| `expected.json` | by hand | What a conforming consumer concludes from those bytes |
| `semantic.json` | by hand | The semantic frames the producer serializer was given to produce `stream.jsonl` |

A case whose bytes are deliberately malformed carries no `semantic.json`,
because the producer serializer cannot produce them; such a case is derived by
a documented mutation of a generated stream, and its `expected.json` names the
source case and the mutation. The `exchanges` subtree adds complete chronological transcripts and malformed
reader cases; its README defines the independent expected-outcome format.

Two files at the top of this directory bind the corpus:

- `SHA256SUMS` lists every file under this directory except itself, one per
  line in the format that `sha256sum` reads, sorted by path.
- The corpus aggregate hash is the SHA-256 of the bytes of `SHA256SUMS`. It is
  recorded in [../manifest.json](../manifest.json) together with the contract
  version and the corpus revision.

## Verifying the corpus

On any Unix host, from this directory:

```sh
sha256sum -c SHA256SUMS
sha256sum SHA256SUMS
```

The first command reports `OK` for every listed file. The second prints the
aggregate hash, which must equal `corpus_sha256` in the manifest. No project
tooling is required for either step.

## `semantic.json`

```json
{
  "case": "refusal-busy",
  "frames": [
    { "frame": "error", "code": "busy" }
  ]
}
```

`frames` lists the semantic frames in the order they were encoded. Each frame
object carries `frame`, the frame kind, followed by the frame's fields under
the names the contract uses on the wire. The generator encodes the list with
the producer serializer and writes the result to `stream.jsonl`. Canonical exchange cases use `input.json` with full protocol objects.
Malformed and additive reader cases have hand-authored bytes and are never
regenerated through the canonical encoder.

## `expected.json`

```json
{
  "case": "refusal-rate-limited",
  "category": "refusal",
  "outcome": "refused",
  "refusal_code": "rate-limited",
  "retry_after_ms": 60,
  "reply_to": "scan_request",
  "connection": "open",
  "frame_count": 1
}
```

| Field | Meaning |
| --- | --- |
| `case` | The case identifier, equal to the directory name |
| `category` | One of `handshake`, `refusal`, `scan`, `malformed`, `limit`, `additive`, `duplicate` |
| `outcome` | One of the outcomes below |
| `refusal_code` | Present when the outcome is `refused`: the code the consumer decoded |
| `supported_majors`, `retry_after_ms` | Present when the refusal carried them: the values the consumer decoded |
| `reply_to` | `client_hello` or `scan_request`: which client frame the stream answers |
| `connection` | `open` or `closed`: whether the producer keeps the connection after the last frame in the stream |
| `frame_count` | The number of frames in `stream.jsonl` |

Outcomes:

| Outcome | Meaning |
| --- | --- |
| `accepted-complete` | The consumer publishes a complete scan |
| `accepted-partial` | The consumer publishes a truncated scan as partial evidence |
| `discarded` | The consumer discards the whole assembly and keeps only prior published evidence; `discard_reason` names why, from a vocabulary that the corpus revision introducing the first malformed case publishes here |
| `refused` | The consumer receives a refusal and maps it to a capability state |
| `connection-closed` | The consumer treats the connection as broken after already published evidence, which it keeps |

Later corpus revisions add the fields that scan cases need, such as the decoded
scan summary and normalized pages. Consumers ignore fields they do not use.

## Regenerating streams and the checksum file

The generator is a producer-side executable built beside the unit tests. Enable
the CMake option `UNIT_TEST_PGBUF_INSPECTOR`, build the target
`pgbuf_inspector_corpus_generator`, and run it with the path of the contract
directory (the parent of this directory). It rewrites refusal streams from
`semantic.json` and canonical exchange streams from `input.json`, preserves
reader-only streams and all expected outcomes, rewrites `SHA256SUMS`, and rewrites the aggregate hash in
the manifest. It never touches `expected.json` or `semantic.json`, and it does
not increment `corpus_revision`; that is a deliberate edit by the person
changing the corpus.

The unit test `test_pgbuf_inspector` then proves that the checked-in streams
equal what the serializer produces, that every expected outcome agrees with the
stream it describes, and that the checked-in checksum file and manifest match
the files on disk.

## Vendoring into a consumer

1. Copy this directory verbatim, including `SHA256SUMS`.
2. Record the aggregate hash from the manifest and the commit of this
   repository the copy was taken from.
3. Add an offline test that recomputes `SHA256SUMS` from the vendored files,
   hashes it, and compares the result with the recorded aggregate hash.
4. Test the consumer's decoder against every `stream.jsonl` and assert the
   conclusions in the matching `expected.json`.

The aggregate hash is the authority for content. The commit is provenance.

## Change control

Any byte change under this directory changes `SHA256SUMS` and therefore the
aggregate hash. The change increments `corpus_revision` in the manifest, and
delivery of the change requires matching offline evidence from the producer's
unit tests and from every consumer's vendored copy. A semantic disagreement
found while implementing against the corpus is resolved by amending the
accepted decision it concerns, never by adjusting bytes to suit one side.

## Reproducible large boundaries

The `test_pgbuf_inspector` executable's `[limits]` cases generate boundaries
without storing hundreds of MiB in the repository. Byte counts include LF.
Reproduce the same recipes in a consumer's independent tests:

- Pad client_hello and page objects with spaces before LF to 4,095 / 4,096 /
  4,097 bytes. Accept only the first two. Pad server_hello to 65,535 / 65,536 /
  65,537 bytes with the same expectation. A frame without LF must fail before
  its applicable buffer limit is exceeded.
- Add an unknown member containing nested arrays of scalar zero. Count the
  root object as depth one; total depths 15 / 16 / 17 accept / accept / reject.
- Emit 65,535 / 65,536 / 65,537 page frames under one header. Count repeated
  VPIDs before deduplication; the first two finish with matching counts and
  ambiguous lookup, the last is rejected. Independently set visited_slots to
  those values with zero records; reject only 65,537.
- Between the complete example's header and a zero-record footer, repeat
  unknown control frames padded up to 4,096 bytes, shortening the last frames
  so the total header-through-footer byte count is 67,108,863 / 67,108,864 /
  67,108,865. Accept / accept / reject at the footer. These unknown frames count
  bytes but cannot increase record_count or establish residency.

These generators exercise the production verifier and preserve a bounded
working set. They do not prove actual slot traversal, elapsed deadlines or
live producer memory/CPU gates. The generator is a test maintenance executable;
no Python, hash library or other runtime dependency is added to the producer.
