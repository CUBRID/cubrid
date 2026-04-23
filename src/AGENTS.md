# src/ — CUBRID Engine Source

**Compilation**: `.c` files are compiled with C++17 compiler (see `c_to_cpp.sh`). No C++ exceptions — C error model throughout.

24 modules. Same source → 3 binaries via `SERVER_MODE`/`SA_MODE`/`CS_MODE` guards.

For the full module list, see the root [AGENTS.md](../AGENTS.md#structure).

## Cross-Module Dependencies

`base/` and `compat/` (DB_VALUE) are foundational — used by nearly all modules. Omitted below.

### Query Processing Pipeline (client → server)

```
parser/ ←→ optimizer/ ──→ xasl/ ──→ query/
  │              │          │          │
  ├→ object/     ├→ object/ ├→ object/ ├→ object/
  └→ storage/    └→ storage/└→ storage/ ├→ storage/ ←→ transaction/
                                        └→ transaction/
```

### Key relationships

- `parser/` ↔ `optimizer/` — bidirectional (parser calls optimizer, optimizer references parser types)
- `object/` — central dependency: parser, optimizer, query, xasl, method, sp all include schema/object headers
- `storage/` ↔ `transaction/` — strong cycle (storage uses MVCC/locks; transaction uses pages/B-tree for recovery)
- `query/` ↔ `storage/` — cycle (query scans storage; storage calls back into query executor)
- `method/`, `sp/` — cross-cutting: depend on parser, object, query, transaction

## Build Integration

No per-module CMakeLists.txt. All source lists in top-level `cubrid/CMakeLists.txt`, `cs/CMakeLists.txt`, `sa/CMakeLists.txt`.

To add a new source file: edit the appropriate top-level CMakeLists.txt, not anything inside `src/`.

## Preprocessor Mode Boundaries

| Code area | SERVER_MODE | SA_MODE | CS_MODE | Notes |
|-----------|-------------|---------|---------|-------|
| Parser/optimizer | ✗ | ✓ | ✓ | |
| Query execution | ✓ | ✓ | ✗ | CS includes client-side helpers (query_cl.c) only |
| Storage/buffer | ✓ | ✓ | ✗ | CS includes client-side subset (statistics_cl.c, es.c) only |
| Transaction/lock | ✓ | ✓ | ✗ | CS includes client-side subset (transaction_cl.c, boot_cl.c) only |
| Client API (db_\*) | partial | ✓ | ✓ | Server includes reduced compat subset (db_date, db_json, etc.) |
| Connection/network | ✓ | ✓ | ✓ | Different files per mode: `_sr` (server/SA), `_cl` (CS/SA) |
