# Chronological exchange corpus

Each case supplies exact UTF-8 JSON-lines in chronological client/server order,
plus independently specified validity, publication and scoped lookup outcomes.
These are offline transcripts, not a new bidirectional network framing format.
Canonical cases additionally supply semantic input JSON objects. Encode those
with the producer API and compare exact output bytes; validate every transcript
with the real incremental state verifier using split and coalesced input.
The sibling refusal-only cases predate this transcript format and are server
responses to the requests described by their expected metadata.

Malformed, incomplete, unknown-field, future-enum and reordered cases are
reader tests, not instructions to emit those forms. Future enum values are
unknown state; residency can still be observed from a valid identified record.
A published result is a validated complete or truncated capture, never proof
of currentness. No socket, identity authentication or clock deadline is tested.
