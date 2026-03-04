# src/base/ — Core Utilities & Infrastructure

Foundational layer used by every other CUBRID engine module. Provides memory management, error handling, lock-free data structures, platform abstraction, internationalization, serialization, performance monitoring, and general utilities.

For project-wide coding style, anti-patterns, and error handling conventions, see the root [AGENTS.md](../../AGENTS.md).

## Subsystems

### 1. Memory Management

| File | Description |
|------|-------------|
| `memory_alloc.c/h` | Core allocation: `db_private_alloc`/`free`/`realloc`, `free_and_init` macro, obstack heap wrappers |
| `area_alloc.c/h` | Slab-like allocator for fixed-size objects (parse tree nodes, set elements); lock-free bitmap per block |
| `mem_block.hpp/cpp` | C++ RAII memory blocks (`cubmem` namespace): `extensible_block`, `stack_block`, pluggable `block_allocator` |
| `memory_private_allocator.hpp/cpp` | STL-compatible `cubmem::private_allocator<T>` backed by per-thread private heap |
| `memory_wrapper.hpp` | **MUST be last `#include`** (CI-enforced). SERVER_MODE: overloads global new/delete for memory monitoring |
| `memory_cwrapper.h` | C-compatible memory wrapper, **safe in headers**. SERVER_MODE: redefines malloc/free as tracked inline functions |
| `fixed_alloc.c` | Thin wrapper around heaplayers fixed-size heap |
| `fixed_size_allocator.hpp` | C++17 template fixed-size allocator with freelist, block growth |
| `memory_hash.c/h` | General-purpose chaining hash table (`MHT_TABLE`) with LRU support; also `MHT_HLS_TABLE` for hash list scan |
| `memory_reference_store.hpp` | Template `cubmem::reference_store<T>` for owned vs. borrowed pointer tracking |
| `memory_monitor_*.cpp/hpp` | Memory monitoring subsystem: per-file:line allocation stats, 16-byte `MMON_METAINFO` metadata per block |

### 2. Lock-Free Data Structures

| File | Description |
|------|-------------|
| `lock_free.c/h` | **Legacy** lock-free hash table + freelist with epoch-based reclamation (`LF_HASH_TABLE`, `LF_FREELIST`) |
| `lockfree_hashmap.hpp/cpp` | **Modern** C++ lock-free hash map (`lockfree::hashmap<Key, T>`) with address-marker deletion |
| `lockfree_freelist.hpp` | Template lock-free freelist for node recycling |
| `lockfree_circular_queue.hpp` | Lock-free MPMC circular queue (fixed-capacity, power-of-2) |
| `lockfree_bitmap.hpp/cpp` | Lock-free bitmap for index allocation via CAS |
| `lockfree_transaction_*.hpp/cpp` | Transaction system for safe reclamation: `system` (index management), `table` (per-structure), `descriptor` (per-thread), `reclaimable_node` (base class) |
| `lockfree_address_marker.hpp` | Pointer low-bit marking for logical deletion |

### 3. Error Handling

| File | Description |
|------|-------------|
| `error_code.h` | All error codes as `#define` negative integers (~1700 codes). Adding new codes requires 6-place update (see root AGENTS.md) |
| `error_manager.c/h` | `er_set()`, error stack, error logging, severity levels, `ASSERT_ERROR()` macros |
| `error_context.hpp/cpp` | C++ per-thread error context (`cuberr::context`) with stackable error levels |

### 4. Platform Abstraction & Porting

| File | Description |
|------|-------------|
| `porting.c/h` | Master platform abstraction: timeval, atomic ops, POSIX↔Win32 function mapping |
| `process_util.c/h` | Cross-platform process creation/termination |
| `dynamic_load.c/h` | Dynamic shared library loading (`dlopen`/`dlsym` on Linux, `shl_load` on HP-UX) |
| `cubrid_getopt_long.c/h` | CUBRID's own `getopt_long` implementation |

### 5. Internationalization, Locale & Character Set

| File | Description |
|------|-------------|
| `intl_support.c/h` | Core i18n: UTF-8/EUC-KR/ISO-8859-1 navigation, charset conversion, codepoint ops |
| `language_support.c/h` | Language and collation system: `LANG_LOCALE_DATA`, `LANG_COLLATION`, built-in locales |
| `locale_support.c/h` | LDML-based locale: `COLL_DATA`, `ALPHABET_DATA`, UCA weight encoding |
| `locale_helper.cpp/hpp` | C++ locale bridge (`cublocale` namespace): UTF-8↔wstring conversion |
| `unicode_support.c/h` | Unicode normalization (NFC/NFD), composition/decomposition |
| `uca_support.c/h` | Unicode Collation Algorithm implementation |
| `chartype.c/h` | Character classification with locale awareness |
| `ksc5601.h`, `jisx0212.h` | Korean (KSC 5601) and Japanese (JIS X 0212) character set mapping tables |

### 6. Timezone Support

| File | Description |
|------|-------------|
| `tz_support.c/h` | Runtime timezone ops: load/unload, UTC↔local conversion, DST rule evaluation |
| `tz_compile.c/h` | IANA timezone database → binary format compiler |
| `timezone_lib_common.h` | Shared timezone type definitions (`TZ_DATA`, `TZ_TIMEZONE`, `TZ_OFFSET_RULE`) |

### 7. Performance Monitoring

| File | Description |
|------|-------------|
| `perf_monitor.c/h` | Legacy C perf stats: hundreds of counters organized by `PERF_STAT_ID` |
| `perf.hpp/cpp` | Modern C++ perf framework (`cubperf` namespace): `COUNTER`, `TIMER`, `COUNTER_AND_TIMER` |
| `perf_def.hpp` | Core types for `cubperf` |
| `perf_monitor_trackers.hpp/cpp` | RAII wrappers for perf timing |
| `tsc_timer.c/h` | CPU Time Stamp Counter timer |
| `cycle.h` | Portable CPU cycle counter (from FFTW, MIT license) |

### 8. Serialization & Object Representation

| File | Description |
|------|-------------|
| `packer.hpp/cpp` | Binary serialization (`cubpacking` namespace): pack/unpack primitives, DB_VALUE, OID |
| `packable_object.hpp` | Abstract base for serializable C++ objects: `get_packed_size`/`pack`/`unpack` |
| `object_representation.h` | Disk representation macros (`OR_PUT_*`/`OR_GET_*`), `OR_BUF`, on-disk size constants |
| `object_representation_sr.c/h` | Server-side object representation: class property reading, domain/attribute disk format |

### 9. Data Structures

| File | Description |
|------|-------------|
| `binaryheap.c/h` | Array-based binary heap (min/max), used in top-N queries |
| `bit.c/h` | Bit manipulation for 8/16/32/64-bit integers |
| `dynamic_array.c/h` | Simple C dynamic array |
| `extensible_array.hpp` | C++ template extensible arrays (`cubmem` namespace) |
| `variable_string.c/h` | C variable-length string with gap-buffer design |
| `string_buffer.hpp/cpp` | C++ formatted text collection using `extensible_block` |
| `rb_tree.h` | Red-black tree via C macros (from FreeBSD) |
| `resource_shared_pool.hpp` | Thread-safe pool for pre-allocated resources |

### 10. System Configuration

| File | Description |
|------|-------------|
| `system_parameter.c/h` | **Largest file in base.** ~400 `PRM_ID_*` parameters, reads `cubrid.conf` |
| `databases_file.c/h` | `databases.txt` parser and database registry |
| `environment_variable.c/h` | `$CUBRID` path resolution and directory helpers |
| `release_string.c/h` | Version info, disk/network/log compatibility checks |
| `ini_parser.c/h` | INI file parser (MIT license), used for broker config |
| `message_catalog.c/h` | NLS message catalog: `msgcat_message(catalog, set, msg)` for localized strings |

### 11. Cryptography & Encoding

| File | Description |
|------|-------------|
| `encryption.c/h` | Password encryption (DES-based, SHA-1) for authentication |
| `sha1.c/h` | SHA-1 hash implementation (RFC 3174) |
| `base64.c/h` | Base64 encoding/decoding |
| `CRC.h` | CRC-32 computation (header-only) |

### 12. Logging & Diagnostics

| File | Description |
|------|-------------|
| `ddl_log.c/h` | DDL audit logging with metadata (user, IP, execution time) |
| `fault_injection.c/h` | Debug-only fault injection framework (`FI_TEST` macro) |
| `stack_dump.c/h` | Call stack dump via `backtrace()` on Linux |
| `resource_tracker.hpp/cpp` | Template resource leak detector for debug builds (page fixes, locks, memory) |

### 13. Utility & Miscellaneous

| File | Description |
|------|-------------|
| `scope_exit.hpp` | C++17 `scope_exit<F>` RAII guard |
| `base_flag.hpp` | Template `flag<T>` for bitfield manipulation |
| `compressor.hpp` | LZ4 compression wrapper (`cubcompress` namespace) |
| `xml_parser.c/h` | XML parser wrapper around expat (used for LDML locale, timezone data) |
| `util_func.c/h` | General utilities: hashing, filepath comparison, signal handling, string splitting |
| `hide_password.c/h` | Password hiding in SQL strings for logging |
| `xserver_interface.h` | Server-mode-only function declarations (boot, locator, log, heap, btree, query) |
| `server_interface.h` | Server interface constants |

## Key Architecture Patterns

### Memory Allocation Hierarchy

- `memory_wrapper.hpp` → `memory_cwrapper.h` → allocators (`db_private_alloc`, `area_alloc`, `fixed_alloc`)
- `memory_wrapper.hpp`: SERVER_MODE only, overrides global `new`/`delete`, **MUST be last include**
- `memory_cwrapper.h`: SERVER_MODE only, overrides `malloc`/`free`, safe in headers
- `db_private_alloc`: per-thread LEA heap (SERVER), `db_ws_alloc` workspace (CS), conditional (SA)
- `area_alloc`: slab allocator for parse nodes, set elements

### Lock-Free Structure Layers

- `lockfree::hashmap` → `lockfree::freelist` → `lockfree::tran::table` → `tran::descriptor` (per-thread) → `tran::system` → `lockfree::bitmap`
- Legacy equivalent: `LF_HASH_TABLE` → `LF_FREELIST` → `LF_TRAN_SYSTEM`

## Where to Look

| Task | Primary File(s) |
|------|-----------------|
| Add error code | `error_code.h` (see root AGENTS.md for 6-place procedure) |
| Fix memory leak | `memory_alloc.c`, `resource_tracker.hpp` |
| Fix lock-free structure | `lockfree_hashmap.hpp` (modern), `lock_free.c` (legacy) |
| Add system parameter | `system_parameter.c/h` — add `PRM_ID_*`, update `prm_Def`, `PRM_LAST_ID` |
| Fix charset/collation | `intl_support.c`, `language_support.c`, `uca_support.c` |
| Fix timezone | `tz_support.c` |
| Add perf counter | `perf_monitor.c` (legacy) or `perf.hpp` (modern) |

## Conventions (base/-specific)

### Error Handling
- `er_set()` always needs `ARG_FILE_LINE` as 2nd arg — macro expands to `__FILE__, __LINE__`
- Severity ordering: `ER_FATAL_ERROR_SEVERITY` > `ER_ERROR_SEVERITY` > `ER_SYNTAX_ERROR_SEVERITY` > `ER_WARNING_SEVERITY` > `ER_NOTIFICATION_SEVERITY`
- Check with `ASSERT_ERROR()` or `ASSERT_ERROR_AND_SET(error_code)` after operations that should set errors

### Lock-Free Rules
- Modern structures (`lockfree::hashmap`) use `lockfree::tran::system` for safe reclamation
- Legacy structures (`lock_free.c`) use `LF_TRAN_SYSTEM`/`LF_TRAN_ENTRY` — same concept, C API
- Always start transaction before accessing, end after done
- Retired nodes are reclaimed only when all concurrent readers have advanced past the retirement ID

### Resource Tracking (Debug)
- `resource_tracker.hpp` tracks page fixes, lock acquisitions — assert on unbalanced ops in debug builds
- `push_track()` before operation scope, `pop_track()` after — leaks detected at pop

### Memory-Specific
- `memory_cwrapper.h` CAN be included in headers; `memory_wrapper.hpp` CANNOT
- Never include `memory_wrapper.hpp` in a header file
