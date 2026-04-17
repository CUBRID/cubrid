<!-- Parent: ../AGENTS.md -->

# src/storage/index_hnsw/ — HNSW Vector Index

In-memory HNSW (Hierarchical Navigable Small World) graph index for approximate nearest neighbor (ANN) search on vector columns. Server-side (`SERVER_MODE` / `SA_MODE`).

## Architecture

```
hnsw_api.hpp          Abstract interface: hnsw_index_backend, hnsw_index
  |
  v
hnsw_impl.cpp         Concrete backend: cubhnsw_index_backend, cubhnsw_index
  |
  +-> hnsw_algo.hpp         Core HNSW algorithm (search, insert, layer traversal)
  +-> hnsw_storage.hpp      Storage layer: pinned_block over buffer pool pages
  +-> hnsw_graph_base.hpp   Graph layout: span-based node/root/neighbors on tape
  +-> vector_distance.hpp   Distance functions with SIMD dispatch
```

## Key Files

| File | Description |
|------|-------------|
| `hnsw_api.hpp` | Abstract base classes: `hnsw_index_backend`, `hnsw_index`, `hnsw_oid_encoder`, `hnsw_build_params` |
| `hnsw_api.cpp` | Backend registry, OID encoding, `hnsw_index` base implementation |
| `hnsw.hpp` / `hnsw.cpp` | Server entry points: `xhnsw_initialize`, `xhnsw_add_index`, `hnsw_add_element`, `hnsw_search_element` |
| `hnsw_impl.cpp` | Concrete backend (`cubhnsw_index_backend`) — creates/loads/drops indexes, delegates to `algo` |
| `hnsw_algo.hpp` | Core algorithm: `algo::add()`, `algo::search()`, `seek_on_layer_()`, `seek_down_()` |
| `hnsw_algo_common.hpp` | Shared types: `slot_id_t`, `key_id_t`, `level_t`, `node_t`, `root_t`, `neighbors_ref_t` |
| `hnsw_algo_common_stats.hpp` | Per-operation statistics: seek rounds, distance computations, cache hits |
| `hnsw_graph_base.hpp` | Span-based graph layout on contiguous memory tape: root, node, neighbors views |
| `hnsw_storage.hpp` | Storage abstraction: `pinned_block` (RAII page pin), disk/in-memory block management |
| `hnsw_storage.cpp` | Disk storage: page allocation, block read/write through buffer pool |
| `hnsw_inmem_block.hpp/cpp` | In-memory block allocator for graph construction |
| `hnsw_utils.hpp` | Misaligned load/store utilities (from usearch), helper templates |
| `vector_distance.hpp` | Distance function dispatch: L2, cosine, inner product |
| `vector_distance_intrinsics_avx2.cpp` | AVX2-optimized distance (L2, cosine, inner product, int8 quantized) |
| `vector_distance_intrinsics_avx512.cpp` | AVX-512 optimized distance |
| `vector_distance_intrinsics_fallback.cpp` | Scalar fallback distance |
| `vector_distance_omp_simd.cpp` | OpenMP SIMD distance (portable vectorization) |

## Key Types

| Type | Header | Purpose |
|------|--------|---------|
| `hnsw_build_params` | `hnsw_api.hpp` | Index parameters: dimension, M, ef_construction, metric |
| `hnsw_index` | `hnsw_api.hpp` | Abstract index: add, search, remove, filtered_search |
| `hnsw_index_backend` | `hnsw_api.hpp` | Factory: create/drop/load indexes, identified by BTID |
| `algo` | `hnsw_algo.hpp` | Core HNSW algorithm: layer traversal, neighbor selection |
| `node_t` | `hnsw_graph_base.hpp` | Span-based node view: vector, neighbors per level |
| `root_t` | `hnsw_graph_base.hpp` | Graph root: build params, max level, entry point |
| `pinned_block` | `hnsw_storage.hpp` | RAII buffer pool page pin with shared/exclusive locking |
| `storage` | `hnsw_storage.hpp` | Abstract storage: pin/allocate blocks for nodes |

## HNSW Parameters

| Parameter | Default | Purpose |
|-----------|---------|---------|
| `M` | 16 | Max neighbors per node per level (level-0 gets 2*M) |
| `ef_construction` | 64 | Search expansion during index build (higher = better recall, slower build) |
| `ef_search` | — | Search expansion at query time (set per query) |
| `dimension` | — | Vector dimensionality |
| `metric` | Euclidean | Distance metric: L2, cosine, inner product |

## For AI Agents

### Working In This Directory

- This is the **#1 hot path** in the current development cycle
- Code is C++17 with heavy use of templates and span-based memory layouts
- The graph layout uses contiguous memory tapes — understand `hnsw_graph_base.hpp` span design before modifying node structures
- `hnsw_algo.hpp` is the largest file (~39K) — use the section comments to navigate
- Storage integrates with CUBRID's buffer pool (`pgbuf_fix`/`pgbuf_unfix`) — follow the pin/unfix protocol
- `HNSW_MAX_M = 64` constrains stack-allocated neighbor buffers — changes affect stack frame sizes
- `.orig` and `~` backup files exist in this directory — ignore them

### Testing

- Unit tests: check `unit_tests/` for HNSW-related test files
- Benchmark scripts in `ann_benchmarks_local/` (project root)
- Distance functions have separate compilation units per ISA — test on target hardware

### Common Patterns

- Backend registration: `HNSW_REGISTER_BACKEND(ID, LAMBDA)` macro for static registration
- OID encoding: `hnsw_oid_encoder` converts CUBRID OID to/from uint64_t node IDs
- Pinned blocks: always use `pinned_block` RAII wrapper — never raw `pgbuf_fix` in this module
- Distance dispatch: compile-time ISA selection via separate `.cpp` files linked per build target

## Dependencies

### Internal

- `src/storage/` — buffer pool (`pgbuf_fix/unfix`), `storage_common.h` (VPID, OID, BTID)
- `src/compat/` — `dbtype_def.h` (DB_VALUE, DB_VECTOR_DISTANCE_METRIC)
- `src/thread/` — `thread_compat.hpp` (THREAD_ENTRY)
- `src/query/` — `query_evaluator.h` (SCAN_PRED for filtered search)
- `src/monitor/` — `perf_monitor.h` (performance counters)

### External

- Inspired by [usearch](https://github.com/unum-cloud/usearch) — utility functions in `hnsw_utils.hpp`

<!-- MANUAL: -->
