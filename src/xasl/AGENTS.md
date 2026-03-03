# src/xasl/ — XASL Node Type Definitions

Defines XASL plan structures used by both parser (client) and executor (server).

## Key Files

| File | Role |
|------|------|
| `xasl_unpack_info.hpp` | XASL deserialization context |
| `xasl_predicate.hpp` | Predicate expression types (`PRED_EXPR`, `COMP_EVAL_TERM`) |
| `xasl_aggregate.hpp` | Aggregate node definitions |
| `xasl_analytic.hpp` | Analytic/window function node definitions |
| `xasl_stream.hpp` | XASL stream (serialization format) constants |
| `xasl_sp.hpp` | XASL stored procedure types |

## Role in Pipeline

```
parser/ generates → XASL_NODE trees → serialized via query/xasl_to_stream.c
  → deserialized via query/stream_to_xasl.c → executed by query/query_executor.c
```

This module defines the **shared types** used at serialization and execution boundaries.

## Where to Look

| Task | File |
|------|------|
| Modify predicate structure | `xasl_predicate.hpp` |
| Modify aggregate node | `xasl_aggregate.hpp` |
| Modify analytic node | `xasl_analytic.hpp` |
| Fix XASL stream format | `xasl_stream.hpp` |
| Fix XASL deserialization | `xasl_unpack_info.hpp` |

## Conventions

- Header-only or header-heavy module — mostly type definitions
- Structures must match between client (serializer) and server (deserializer)
- Changes here typically require coordinated changes in `parser/xasl_generation.c`, `query/xasl_to_stream.c`, and `query/stream_to_xasl.c`

## Gotchas

- Adding fields to XASL structures requires updating serialization AND deserialization — mismatches cause crashes
- Shared between `CS_MODE` (client) and `SERVER_MODE` — no mode-specific guards allowed
- `REGU_VARIABLE` is the most complex type — deeply nested unions

