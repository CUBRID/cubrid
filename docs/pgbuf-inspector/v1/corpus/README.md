# Conformance corpus for wire contract version 1.0

This directory is the shared test oracle for the page-buffer inspector wire
contract described in [../contract.md](../contract.md). The producer in this
repository and every consumer test against the same bytes here, offline and
independently, so that a disagreement about the wire is found by a unit test
and not by a live integration run.

## Layout

Each case is one subdirectory named by its stable case identifier. A case
holds up to three files:

| File | Written by | Meaning |
| --- | --- | --- |
| `stream.jsonl` | the generator, from `semantic.json` | The exact bytes the consumer reads from the socket for this case, server to client |
| `expected.json` | by hand | What a conforming consumer concludes from those bytes |
| `semantic.json` | by hand | The semantic frames the producer serializer was given to produce `stream.jsonl` |

A case whose bytes are deliberately malformed carries no `semantic.json`,
because the producer serializer cannot produce them; such a case is derived by
a documented mutation of a generated stream, and its `expected.json` names the
source case and the mutation. This revision of the corpus contains no such
case yet.

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
the producer serializer and writes the result to `stream.jsonl`. There is no
hand-authored `stream.jsonl` anywhere in the corpus: a stream the serializer
cannot produce is not canonical.

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
directory (the parent of this directory). It rewrites every `stream.jsonl` from
its `semantic.json`, rewrites `SHA256SUMS`, and rewrites the aggregate hash in
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
