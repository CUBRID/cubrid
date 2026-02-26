# src/base/ — Core Utilities & Infrastructure

**170 files (~130K lines).** Foundational layer used by every other CUBRID engine module. Provides memory management, error handling, lock-free data structures, platform abstraction, internationalization, serialization, performance monitoring, and general utilities.

## File Inventory by Subsystem

### 1. Memory Management (15 files)

| File | Lines | Description |
|------|-------|-------------|
| `memory_alloc.c` | 1039 | Core memory allocation module. Provides `db_private_alloc`/`db_private_free`/`db_private_realloc` for thread-local heap allocation (LEA heap in `SERVER_MODE`, workspace alloc in `CS_MODE`). Also implements obstack heap wrappers (`db_create_ostk_heap`), private heap lifecycle (`db_create_private_heap`, `db_destroy_private_heap`), and ANSI SQL string comparison (`ansisql_strcmp`, `ansisql_strcasecmp`) with trailing-blank semantics. |
| `memory_alloc.h` | 329 | Header for memory allocation. Defines alignment macros (`DB_ALIGN`, `PTR_ALIGN`, `MAX_ALIGNMENT`), the critical `free_and_init` / `db_private_free_and_init` macros (free + nullify), `MEM_REGION_INIT`/`MEM_REGION_SCRAMBLE` debug markers, and `os_malloc`/`os_free` wrappers with resource tracking in debug builds. |
| `area_alloc.c` | 867 | Slab-like area allocator for fixed-size objects (e.g., parse tree nodes, set elements). Uses lock-free bitmaps (`LF_BITMAP`) per block for concurrent allocation. Blocks are organized into sorted blockset lists with binary-search lookup. Thread-safe via `area_mutex` for block insertion; allocation/free are lock-free via bitmap CAS. Debug mode adds prefix markers to detect double-free. |
| `area_alloc.h` | 121 | Declares `AREA`, `AREA_BLOCK`, `AREA_BLOCKSET_LIST` structures. API: `area_create`/`area_destroy`/`area_alloc`/`area_free`/`area_flush`/`area_validate`/`area_dump`. |
| `mem_block.hpp` | 487 | C++ RAII memory block system in `cubmem` namespace. Core types: `block` (ptr+size pair, non-owning), `stack_block<S>` (stack-allocated buffer), `extensible_block` (heap-extendable, owning, move-only), `extensible_stack_block<S>` (starts on stack, promotes to heap). Provides pluggable `block_allocator` with `alloc_func`/`dealloc_func` pairs. Global allocators: `STANDARD_BLOCK_ALLOCATOR` (new/delete), `EXPONENTIAL_STANDARD_BLOCK_ALLOCATOR` (doubles on grow), `CSTYLE_BLOCK_ALLOCATOR` (malloc/realloc/free). |
| `mem_block.cpp` | 215 | Implements the three global `block_allocator` instances and `single_block_allocator` (reusable memory cache). |
| `memory_private_allocator.hpp` | 367 | C++ STL-compatible allocator (`cubmem::private_allocator<T>`) backed by CUBRID's per-thread private heap (`db_private_alloc`). Also provides `private_pointer_deleter`, `private_unique_ptr`, `PRIVATE_BLOCK_ALLOCATOR`, and `switch_to_global_allocator_and_call()` helper. |
| `memory_private_allocator.cpp` | 156 | Implements private block allocator (exponential growth via `db_private_realloc`) and helper functions for heap ID resolution, allocation/deallocation with heap switching, and debug-mode allocator count tracking. |
| `memory_wrapper.hpp` | 92 | **MUST be the last `#include` in every `.c`/`.cpp` file** (CI-enforced). In `SERVER_MODE`, overloads global `operator new`/`operator new[]`/`operator delete`/`operator delete[]` to route through `cub_alloc`/`cub_free` for memory monitoring. Also `#define new new(__FILE__, __LINE__)` for source tracking. Provides `placement_new` template. |
| `memory_cwrapper.h` | 193 | C-compatible memory wrapper, **safe to include in headers** (unlike `memory_wrapper.hpp`). In `SERVER_MODE`, redefines `malloc`/`calloc`/`realloc`/`strdup`/`free` as inline functions (`cub_alloc`/`cub_free`/etc.) that integrate with memory monitor (`mmon_add_stat`/`mmon_sub_stat`). Uses `malloc_usable_size` for size tracking. |
| `fixed_alloc.c` | 117 | Thin wrapper around `hl_register_fixed_heap`/`hl_fixed_alloc`/`hl_fixed_free` from heaplayers. Provides `db_create_fixed_heap`/`db_destroy_fixed_heap`/`db_fixed_alloc`/`db_fixed_free`. |
| `fixed_size_allocator.hpp` | 265 | C++17 template fixed-size allocator in `cubmem::fixed_size_alloc` namespace. Allocates nodes in blocks of 256 with a freelist. Two specializations: `allocator<T, false>` (standard heap) and `allocator<T, true>` (private heap via `private_allocator`). Exponential block growth. |
| `memory_hash.c/h` | 2722/181 | General-purpose chaining hash table (`MHT_TABLE`) with LRU list support. Multiple hash functions: `mht_1strhash`, `mht_numhash`, `mht_valhash`, `mht_ptrhash`, etc. Also provides `MHT_HLS_TABLE` — a specialized keyless hash table for HASH LIST SCAN operations in the query executor. |
| `memory_reference_store.hpp` | 165 | Template `cubmem::reference_store<T>` for managing mutable vs. immutable ownership of objects. Tracks whether a pointer is owned (mutable, will be deleted) or borrowed (immutable, not deleted). |
| `memory_monitor_*.cpp/hpp` | ~750 | Memory monitoring subsystem. `memory_monitor_sr.hpp`: server-side `cubmem::memory_monitor` class tracks per-file:line allocation stats via metadata appended to each malloc'd block (16-byte `MMON_METAINFO`). Uses `tbb::concurrent_unordered_map` for stat names. `memory_monitor_cl.hpp`: client-side print functions. `memory_monitor_api.cpp`: initialization/finalization API. `memory_monitor_common.hpp`: shared `MMON_SERVER_INFO` structure. Controlled by `PRM_ID_ENABLE_MEMORY_MONITORING` system parameter. |

### 2. Lock-Free Data Structures (14 files)

| File | Lines | Description |
|------|-------|-------------|
| `lock_free.c/h` | 2540/684 | **Legacy** lock-free hash table and freelist using hazard-pointer-like epoch-based reclamation. `LF_ENTRY_DESCRIPTOR` provides callbacks for alloc/free/init/uninit/key_copy/key_cmp/hash/duplicate handling. `LF_TRAN_SYSTEM`/`LF_TRAN_ENTRY` manage transaction-based garbage collection of retired nodes. `LF_FREELIST` manages pre-allocated entry pools. `LF_HASH_TABLE` is a lock-free open-address hash map. Entry-level mutexes are optional (`LF_EM_USING_MUTEX`). |
| `lockfree_hashmap.hpp/cpp` | 1445/— | **Modern C++ replacement** lock-free hash map (`lockfree::hashmap<Key, T>`). Template-based with integrated freelist, transaction system, and statistics (`cubmonitor::atomic_counter_timer_stat`). Supports `find`, `find_or_insert`, `insert`, `erase`, `erase_locked`, `clear`, and iteration. Uses address markers for logical deletion (mark low bit of pointer). Back-buffer for concurrent clear operations. |
| `lockfree_freelist.hpp` | 541 | Template lock-free freelist (`lockfree::freelist<T>`) for node recycling. Uses available list + back-buffer pattern to reduce contention. Nodes are `free_node` extending `tran::reclaimable_node`. Tracks statistics: alloc count, available count, forced allocations, retired count. |
| `lockfree_circular_queue.hpp` | 627 | Lock-free MPMC (multi-producer/multi-consumer) circular queue. Fixed-capacity, power-of-2 sized. Uses atomic cursors with blocking flags for produce/consume synchronization. `produce()`/`consume()` return false on failure (non-blocking). `force_produce()` spins until success. |
| `lockfree_bitmap.hpp/cpp` | 92/276 | Lock-free bitmap (`lockfree::bitmap`) using atomic unsigned int bitfields. Supports two chunking styles: `ONE_CHUNK` and `LIST_OF_CHUNKS`. `get_entry()` finds and claims a free bit via CAS. `free_entry()` releases it. Configurable usage threshold (full or 95th percentile). Round-robin start index for fairness. |
| `lockfree_transaction_system.hpp/cpp` | 94/— | Transaction index management for lock-free structures. Solves ABA problem by deferring node reclamation until all concurrent readers have finished. `lockfree::tran::system` manages a bitmap of transaction indexes. Each thread gets a unique index valid across all tables. |
| `lockfree_transaction_table.hpp/cpp` | 90/— | Per-structure transaction table (`lockfree::tran::table`). Maintains global transaction ID (incremented on each retirement), per-thread descriptors, and minimum active transaction ID (computed periodically every 100 transactions). |
| `lockfree_transaction_descriptor.hpp/cpp` | 101/— | Per-thread transaction descriptor (`lockfree::tran::descriptor`). Manages retired node list, transaction start/end, and reclamation when minimum active ID exceeds retired node's ID. Tracks retire/reclaim counts. |
| `lockfree_transaction_def.hpp` | 36 | Type definitions: `lockfree::tran::index` = `size_t`, `lockfree::tran::id` = `uint64_t`. |
| `lockfree_transaction_reclaimable.hpp` | 75 | Base class `lockfree::tran::reclaimable_node` for nodes in lock-free structures. Virtual `reclaim()` (default: `delete this`). Stores retire transaction ID and next-retired link. |
| `lockfree_address_marker.hpp` | 169 | Template utility for marking pointers by setting their lowest bit (for logical deletion in lock-free lists). `set_adress_mark`, `strip_address_mark`, `is_address_marked`, `atomic_strip_address_mark`. |

### 3. Error Handling (5 files)

| File | Lines | Description |
|------|-------|-------------|
| `error_code.h` | 1775 | All CUBRID error codes as `#define` negative integers. `NO_ERROR = 0`, `ER_FAILED = -1`. ~1700 error codes covering IO, disk, file, B-tree, heap, query, transaction, network, authentication, etc. **Adding a new error code requires updates in 5 places** (see root AGENTS.md). |
| `error_manager.c` | 3412 | Core error management implementation. `er_set()` sets current error with severity, file/line, and variadic args. `er_init()`/`er_final()` lifecycle. Error stack: `er_stack_push()`/`er_stack_pop()`. Error logging to file. Thread-local error context. Custom `assert_release` macro for non-debug builds. |
| `error_manager.h` | 369 | Error manager API. `ARG_FILE_LINE` macro = `__FILE__, __LINE__`. Severity enum: `ER_FATAL_ERROR_SEVERITY`, `ER_ERROR_SEVERITY`, `ER_SYNTAX_ERROR_SEVERITY`, `ER_WARNING_SEVERITY`, `ER_NOTIFICATION_SEVERITY`. Convenience macros `ERROR0`–`ERROR5`, `ERROR_SET_WARNING`/`ERROR_SET_ERROR` with arg variants. `ASSERT_ERROR()`, `ASSERT_ERROR_AND_SET()`, `ASSERT_NO_ERROR()` debug macros. RAII wrapper: `cuberr::manager`. |
| `error_context.hpp` | 118 | C++ error context class (`cuberr::context`). Per-thread error state with stackable error levels (`push_error_stack`/`pop_error_stack`). Each level stores `er_message` with id, severity, file/line, message area, and variadic args. Thread-local registration via `get_thread_local_context()`. |
| `error_context.cpp` | 356 | Implementation of `cuberr::context` and `cuberr::er_message`. Message area management (reserve, clear), error swap, stack operations. |

### 4. Platform Abstraction & Porting (8 files)

| File | Lines | Description |
|------|-------|-------------|
| `porting.c` | 2673 | Platform-specific implementations: `timeval` operations, `cub_dirname`/`cub_basename`, `os_rename_file`, `cub_vsnprintf`, mutex/rwlock wrappers, `getpass` replacement, `lockf_map`, `tz_*` functions for Windows. Atomic operations: `ATOMIC_TAS_*`, `ATOMIC_CAS_*`, `ATOMIC_INC_*`. |
| `porting.h` | 1114 | Master platform abstraction header. Defines `EXPORT_IMPORT`, size constants (`ONE_K`–`ONE_P`), `CTIME_MAX`, `LLONG_MAX`/`LLONG_MIN`. Windows compatibility: maps POSIX functions (`sleep`, `snprintf`, `strcasecmp`, `lseek`, etc.) to Win32 equivalents. Defines `UINT64`, `INT64`, `SOCKET`, `pthread_t` for Windows. Atomic operation macros. `REFPTR` macro for nullable pointer parameters. |
| `porting_inline.hpp` | — | Inline helper functions for porting layer. |
| `process_util.c/h` | 445/34 | Process manipulation: `create_child_process()` with stdin/stdout/stderr redirection, `is_terminated_process()`, `terminate_process()`. Cross-platform (fork/exec on Linux, CreateProcess on Windows). |
| `dynamic_load.c/h` | 1919/118 | Dynamic shared library loading. Platform-specific (`dlopen`/`dlsym` on Linux/Solaris, `shl_load` on HP-UX). `dl_initiate_module()`, `dl_load_object_module()`, `dl_resolve_object_symbol()`. |
| `dl_daemon.c` | — | Daemon helper for dynamic loading. |
| `get_clock_freq.c` | — | Retrieves CPU clock frequency for TSC timer calibration. |
| `cubrid_getopt_long.c/h` | 502/— | CUBRID's own `getopt_long` implementation for command-line argument parsing. |

### 5. Internationalization, Locale & Character Set (16 files)

| File | Lines | Description |
|------|-------|-------------|
| `intl_support.c/h` | 6236/351 | Core internationalization: UTF-8/EUC-KR/ISO-8859-1 character navigation (`intl_nextchar_utf8`, `intl_prevchar_utf8`), charset conversion (`intl_utf8_to_euckr`, `intl_iso88591_to_utf8`, etc.), codepoint operations (`intl_cp_to_utf8`, `intl_utf8_to_cp`), SQL identifier case-insensitive comparison (`intl_identifier_casecmp`), currency symbol lookup, UTF-8 validation (`intl_check_utf8`), string upper/lower with locale-aware alphabets. |
| `language_support.c/h` | 7683/356 | Language and collation system. Manages `LANG_LOCALE_DATA` (date/time formats, calendar names, number symbols, casing rules per locale) and `LANG_COLLATION` (string comparison, pattern matching, next-sequence, split-key functions). Built-in locales: `en_US`, `ko_KR`, `tr_TR`. Up to 256 collations. `lang_init()`/`lang_final()` lifecycle. Collation loading from shared libraries. |
| `locale_support.c/h` | 8084/674 | LDML-based locale support. Defines `COLL_DATA` (collation weights, contractions, expansions), `ALPHABET_DATA` (upper/lower case mappings), `TEXT_CONVERSION`, `UNICODE_NORMALIZATION`. Functions for locale file parsing, collation data loading, alphabet initialization. UCA weight encoding (L1/L2/L3 on 32-bit). |
| `locale_helper.cpp/hpp` | 286/47 | C++ locale bridge (`cublocale` namespace). `convert_utf8_to_string`/`convert_string_to_utf8` using `std::codecvt`. `convert_utf8_to_wstring`/`convert_wstring_to_utf8`. `get_locale()` for std::locale from charset+lang strings. |
| `locale_lib_common.h` | 231 | Shared type definitions for locale libraries: `LOCALE_FILE`, `LOCALE_DATA`, constants for locale data shared between locale compiler and runtime. |
| `unicode_support.c/h` | 1473/— | Unicode normalization (NFC/NFD), composition/decomposition mappings, compatibility decomposition. `unicode_string_need_compose`, `unicode_compose_string`, `unicode_decompose_string`. |
| `uca_support.c/h` | 3590/— | Unicode Collation Algorithm (UCA) implementation. Weight table management, contraction handling, expansion support, collation element comparison. |
| `chartype.c/h` | 276/— | Character classification functions: `char_isalpha`, `char_isdigit`, `char_isspace`, `char_tolower`, `char_toupper` with locale awareness. |
| `charset_converters.h` | — | Header declaring charset conversion function pointers. |
| `ksc5601.h` | 3075 | KSC 5601 (Korean) character set mapping tables (EUC-KR ↔ Unicode). |
| `jisx0212.h` | 2239 | JIS X 0212 (Japanese supplementary) character set mapping tables. |

### 6. Timezone Support (4 files)

| File | Lines | Description |
|------|-------|-------------|
| `tz_support.c/h` | 5503/236 | Runtime timezone operations: `tz_load()`/`tz_unload()` for timezone data library, `tz_create_datetimetz`/`tz_create_timestamptz` for timezone-aware datetime creation, `tz_utc_datetimetz_to_local` for UTC↔local conversion, `tz_str_to_region` for parsing timezone strings, `tz_explain_tz_id` for human-readable timezone info, DST rule evaluation. |
| `tz_compile.c/h` | 6865/— | Timezone data compiler. Parses IANA timezone database files and compiles them into binary format for the timezone shared library. Handles timezone rules, DST transitions, leap seconds, timezone name aliases. |
| `timezone_lib_common.h` | 231 | Shared type definitions: `TZ_DATA`, `TZ_TIMEZONE`, `TZ_NAME`, `TZ_OFFSET_RULE`, `TZ_DS_RULE`, `TZ_REGION`, `TZ_ID`, `TZ_LEAP_SEC`. |

### 7. Performance Monitoring (8 files)

| File | Lines | Description |
|------|-------|-------------|
| `perf_monitor.c/h` | 4522/1699 | Legacy C performance monitoring. Hundreds of performance statistics (page fix/unfix/promote, lock waits, log flushes, etc.) organized by `PERF_STAT_ID`. Multi-dimensional counters (module × page type × mode × latch × condition). `perfmon_start_watch`/`perfmon_stop_watch` for timing. Statistics activation flags for selective monitoring. |
| `perf.hpp/cpp` | 647/— | Modern C++ performance statistics framework (`cubperf` namespace). `statset_definition` acts as factory for statistic sets. Three statistic types: `COUNTER`, `TIMER`, `COUNTER_AND_TIMER`. Supports both atomic and non-atomic stat sets. Automatic timer conversion to desired time units. |
| `perf_def.hpp` | 146 | Core types for `cubperf`: `generic_value<IsAtomic>` (conditional atomic), `generic_statset`, `generic_stat_counter`, `generic_stat_timer`, `generic_stat_counter_and_timer`. Uses `std::chrono::high_resolution_clock`. |
| `perf_monitor_trackers.hpp/cpp` | 87/— | C++ RAII wrappers: `perfmon_counter_timer_tracker` (manual start/track) and `perfmon_counter_timer_raii_tracker` (automatic tracking on destruction). |
| `tsc_timer.c/h` | 262/62 | Time Stamp Counter (TSC) timer. `tsc_getticks()`, `tsc_elapsed_time_usec()`, `tsc_elapsed_utime()`. Uses CPU cycle counter (`cycle.h`) with calibration via `get_clock_freq()`. |
| `cycle.h` | 520 | Portable CPU cycle counter. Supports x86 (`rdtsc`), PowerPC, ARM, IA64, SPARC, Alpha, MIPS, PA-RISC. From FFTW project (MIT license). |

### 8. Serialization & Object Representation (6 files)

| File | Lines | Description |
|------|-------|-------------|
| `packer.hpp/cpp` | 483/1049 | Binary serialization framework (`cubpacking` namespace). `packer` class packs primitives (int, short, bigint, bool, string, OID, DB_VALUE, buffers) into a byte buffer. `unpacker` class mirrors for deserialization. Handles alignment. Supports bulk pack/unpack via templates. Interoperates with legacy `or_buf`. |
| `packable_object.hpp` | 51 | Abstract base class `cubpacking::packable_object` with pure virtual `get_packed_size()`, `pack()`, `unpack()`. Serialization interface for C++ objects sent between client and server. |
| `object_representation.h` | 2767 | Disk representation of objects. Defines byte-level PACK/UNPACK macros (`OR_PUT_BYTE`, `OR_GET_INT`, etc.) with network byte order conversion. `OR_BUF` structure for streaming pack/unpack. Overflow check macros. Constants for on-disk representation sizes (`OR_INT_SIZE`, `OR_OID_SIZE`, etc.). |
| `object_representation_sr.c/h` | 4696/280 | Server-side object representation functions. `or_class_get_*` for reading class properties from disk. Domain/attribute disk format handling. BTID/HFID serialization. Class representation caching. |
| `object_representation_constants.h` | — | Constants for on-disk sizes and type codes shared across client/server. |

### 9. Data Structures (12 files)

| File | Lines | Description |
|------|-------|-------------|
| `binaryheap.c/h` | 475/109 | Array-based binary heap (min/max). Server-mode only. `bh_create`/`bh_destroy`, `bh_insert`/`bh_extract_max`/`bh_try_insert` (insert with replacement). `bh_to_sorted_array()` for in-place sort. Used in query execution (e.g., top-N). |
| `bit.c/h` | 602/84 | Bit manipulation for 8/16/32/64-bit integers. `bitN_count_ones`/`count_zeros`, `count_trailing_ones`/`zeros`, `count_leading_ones`/`zeros`, `is_set`/`set`/`clear`/`set_trailing_bits`. |
| `dynamic_array.c/h` | —/42 | Simple C dynamic array. `da_create(count, elem_size)`, `da_add`/`da_put`/`da_get`/`da_size`/`da_destroy`. Grows automatically. |
| `extensible_array.hpp` | 308 | C++ template extensible arrays (`cubmem` namespace). `extensible_array<T, S>` extends from stack to heap. `appendable_array<T, S>` adds `append()` with tracking of current count. `appendible_block<S>` for raw byte appending. |
| `variable_string.c/h` | 540/72 | C variable-length string (`varstring`). Gap-buffer design with `base`/`limit`/`start`/`end` pointers. `vs_new`/`vs_free`/`vs_clear`/`vs_append`/`vs_prepend`/`vs_sprintf`/`vs_strcat`/`vs_strcpy`/`vs_putc`. Can be stack or heap allocated. |
| `string_buffer.hpp/cpp` | 153/— | C++ `string_buffer` class for formatted text collection using `extensible_block`. Printf-like `operator()` for formatted append. `operator+=` for single char. `hex_dump()` for binary data. No dynamic allocation if content fits initial buffer. |
| `string_utility.hpp` | 73 | C++ string utilities: `lowercase_hash` and `lowercase_compare` functors for case-insensitive `std::unordered_set`. Type alias `string_set_ci_lower`. |
| `rb_tree.h` | 505 | Red-black tree via C macros (from FreeBSD `sys/tree.h`). `RB_HEAD`, `RB_ENTRY`, `RB_INSERT`, `RB_REMOVE`, `RB_FIND`, `RB_FOREACH` macros. Intrusive design (tree links embedded in user struct). |
| `resource_shared_pool.hpp` | 108 | Thread-safe pool for pre-allocated resources (`resource_shared_pool<T>`). Stack-based free list with mutex. `claim()` returns pointer or NULL. `retire()` returns to pool. |

### 10. System Configuration (10 files)

| File | Lines | Description |
|------|-------|-------------|
| `system_parameter.c/h` | 12077/941 | **Largest file in base.** All CUBRID server/client configuration parameters. `PRM_ID_*` enum (~400 parameters). `sysprm_load_and_init()` reads `cubrid.conf`. `prm_get_bool_value`/`prm_get_integer_value`/`prm_get_string_value`/etc. Parameter metadata: name, type, default, min/max, flags (server-only, client-only, reloadable). `SET SYSTEM PARAMETERS` SQL support. |
| `databases_file.c/h` | 1543/90 | Parses and manages `databases.txt` file. `DB_INFO` struct holds name, pathname, hosts, logpath, lobpath. `cfg_read_directory`/`cfg_write_directory` for file I/O. `cfg_find_db`/`cfg_add_db`/`cfg_delete_db` for database registry. |
| `environment_variable.c/h` | 591/62 | `$CUBRID` environment variable resolution. `envvar_root()` returns `$CUBRID` path. Path construction: `envvar_bindir_file`, `envvar_confdir_file`, `envvar_logdir_file`, `envvar_localedir_file`, `envvar_tmpdir_file`, `envvar_tzdata_dir_file`, etc. |
| `release_string.c/h` | 618/77 | Version and release information. `rel_name()`, `rel_release_string()`, `rel_major_release_string()`, `rel_build_number()`. Disk compatibility: `rel_disk_compatible()`, `rel_get_disk_compatible()`. Network compatibility: `rel_get_net_compatible()`. Log compatibility: `rel_is_log_compatible()`. |
| `ini_parser.c/h` | 1046/63 | INI file parser (MIT license, from Nicolas Devillard). `ini_parser_load`/`ini_parser_free`. `ini_getstr`/`ini_getint`/`ini_getfloat`/`ini_gethex`. Section-based lookup. Used for broker configuration. |
| `message_catalog.c/h` | 712/114 | NLS message catalog system. `msgcat_init`/`msgcat_final`. `msgcat_message(catalog_id, set_id, msg_id)` retrieves localized strings. Three catalogs: `MSGCAT_CATALOG_CUBRID` (cubrid.msg), `MSGCAT_CATALOG_CSQL`, `MSGCAT_CATALOG_UTILS`. Set IDs: `MSGCAT_SET_ERROR`, `MSGCAT_SET_PARAMETERS`, `MSGCAT_SET_LOG`, etc. |
| `msgcat_glossary.hpp` | 44 | Message IDs for `MSGCAT_SET_GLOSSARY`: CLASS, TRIGGER, SERIAL, SERVER, SYNONYM, PROCEDURE. |
| `msgcat_set_log.hpp` | 61 | Message IDs for `MSGCAT_SET_LOG`: archive messages, backup messages, recovery messages, etc. |

### 11. Cryptography & Encoding (5 files)

| File | Lines | Description |
|------|-------|-------------|
| `encryption.c/h` | 224/36 | Password encryption: `crypt_seed()`, `crypt_encrypt_printable()` (DES-based), `crypt_encrypt_sha1_printable()`. Used for user authentication. |
| `sha1.c/h` | 418/68 | SHA-1 hash implementation (RFC 3174). `SHA1Reset`/`SHA1Input`/`SHA1Result` for streaming, `SHA1Compute` for one-shot. `SHA1Hash` structure (5 × INT32). `SHA1Compare` for hash equality. |
| `base64.c/h` | 633/39 | Base64 encoding/decoding: `base64_encode`/`base64_decode`. Internal error codes: `BASE64_EMPTY_INPUT`, `BASE64_INVALID_INPUT`. |
| `CRC.h` | 1709 | CRC-32 computation tables and functions. Header-only. |

### 12. Logging & Diagnostics (10 files)

| File | Lines | Description |
|------|-------|-------------|
| `ddl_log.c/h` | 1593/108 | DDL audit logging. Logs DDL statements with metadata (db name, user, IP, broker info, execution time, commit/rollback). `logddl_init`/`logddl_free`/`logddl_write`. Supports CAS, CSQL, LOADDB applications. Password hiding support. |
| `trace_event_log.c` | 657 | Chrome trace event format logging (JSON). For performance trace visualization. |
| `trace_log.h` | 50 | Trace log API: `trace_log_init`/`trace_log_start`/`trace_log_end`. Three levels: OFF, SIMPLE, DETAIL. |
| `event_log.h` | 49 | Server event log: `event_log_init`/`event_log_start`/`event_log_end`, `event_log_sql_string`, `event_log_bind_values`. |
| `fault_injection.c/h` | 486/116 | Debug-only fault injection framework. `FI_TEST_CODE` enum defines injection points (IO, disk, btree, log, query). `FI_TEST(thread, code, state)` macro checks if fault should trigger. Groups: `FI_GROUP_RECOVERY`. Only active in `!NDEBUG` builds. |
| `stack_dump.c/h` | 531/38 | Call stack dump: `er_dump_call_stack()` writes to FILE, `er_dump_call_stack_to_string()` returns string. Uses `backtrace()`/`backtrace_symbols()` on Linux. Function name table (`fname_table`) for symbol resolution. |
| `resource_tracker.hpp/cpp` | 462/— | Template `cubbase::resource_tracker<Res>` for detecting resource leaks, over-usage, and invalid frees. Stackable tracking levels. Used in debug builds to track page fixes, lock acquisitions, memory allocations. Configurable max resources and max reuse. Asserts on leak at `pop_track()`. |

### 13. Utility & Miscellaneous (25+ files)

| File | Lines | Description |
|------|-------|-------------|
| `scope_exit.hpp` | 82 | C++17 `scope_exit<F>` RAII guard. Executes callable on scope exit. `make_scope_exit()` factory. `release()` to disengage. CTAD-enabled. |
| `base_flag.hpp` | 168 | Template `flag<T>` class for bitfield manipulation. `set`/`clear`/`is_set`/`is_all_set`/`is_any_set` + static versions. |
| `fileline_location.hpp/cpp` | 56/— | `cubbase::fileline_location` struct storing file name (truncated to 20 chars) and line number. Used by `resource_tracker` and error management. |
| `printer.hpp/cpp` | 109/— | `print_output` abstract class with virtual `flush()`. `file_print_output` (to FILE*), `string_print_output` (to string_buffer). Printf-like `operator()`. |
| `pinning.hpp/cpp` | 90/— | `cubbase::pinner`/`cubbase::pinnable` reference counting system using `std::set`. `pin()`/`unpin()` for reference management. Asserts all references released on destruction. |
| `pinnable_buffer.hpp/cpp` | 82/— | `cubmem::pinnable_buffer` — buffer with pinning support. Extends `cubbase::pinnable`. Stores raw memory pointer and end pointer. |
| `compressor.hpp` | 176 | LZ4 compression wrapper (`cubcompress` namespace). Template interface: `compress<LZ4>`, `decompress<LZ4>`, `bound<LZ4>`. Thread-local LZ4 stream context. Configurable acceleration parameter. |
| `object_factory.hpp` | 80 | `cubbase::factory<Key, Base>` — type-erased factory pattern. `register_creator<Derived>(key)` maps keys to creator functions. `create_object(key)` instantiates. |
| `filesys.hpp` | 61 | `filesys` namespace: `file_closer` (RAII FILE* deleter), `file_deleter` (RAII unlink), `auto_close_file`/`auto_delete_file` unique_ptr aliases. |
| `filesys_temp.hpp/cpp` | 40/— | Temporary file utilities: `open_temp_filedes()`, `open_temp_file()`, `temp_directory_path()`. |
| `xml_parser.c/h` | 1267/149 | XML parser wrapper around expat library. Schema-driven parsing with element start/end/data callbacks (`ELEM_START_FUNC`/`ELEM_END_FUNC`). Used for LDML locale files and timezone data. Include-loop detection. |
| `util_func.c/h` | 899/79 | General utilities: `hashpjw()` (PJW hash), `util_compare_filepath`, signal handler management, string splitting (`util_split_string`), time parsing (`util_str_to_time_since_epoch`), logging (`util_log_write_result`/`util_log_write_errid`), binary search (`util_bsearch`). |
| `misc_string.c/h` | —/48 | Case-insensitive string ops: `ustr_casestr` (case-insensitive strstr), `ustr_upper`/`ustr_lower`. |
| `hide_password.c/h` | 833/77 | Password hiding in SQL strings for logging. Tracks password offsets and replaces them with `'***'` in output. `password_add_offset`, `password_fprintf`, `password_snprint`. |
| `rand.c` | — | Random number generation. |
| `dtoa.c` | 882 | Double-to-ASCII conversion (David Gay's dtoa). Precise floating-point formatting. |
| `mprec.c/h` | 997/406 | Multi-precision arithmetic support for dtoa. |
| `ieeefp.h` | — | IEEE floating-point definitions. |
| `server_interface.h` | 67 | Server interface constants: `SI_SYS_DATETIME`, `SI_LOCAL_TRANSACTION_ID`, `CHECKDB_*` flags, `COMPACTDB_*` constants. |
| `xserver_interface.h` | 302 | Server-mode-only function declarations. Entry points for boot, locator, log, heap, btree, query, catalog, serial, session, vacuum, etc. Server-side implementations of client-requested operations. |

## Key Architecture Patterns

### Memory Allocation Hierarchy

```
                        ┌──────────────────────────────────┐
                        │    memory_wrapper.hpp             │
                        │  (global new/delete override,     │
                        │   SERVER_MODE only, LAST include) │
                        └──────────┬───────────────────────┘
                                   │
                        ┌──────────▼───────────────────────┐
                        │    memory_cwrapper.h              │
                        │  (malloc/free/calloc override,    │
                        │   SERVER_MODE, safe in headers)   │
                        └──────────┬───────────────────────┘
                                   │
            ┌──────────────────────┼──────────────────────┐
            │                      │                      │
  ┌─────────▼────────┐  ┌─────────▼────────┐  ┌─────────▼────────┐
  │ db_private_alloc  │  │   area_alloc     │  │  fixed_alloc     │
  │ (per-thread LEA   │  │ (slab allocator  │  │ (fixed-size      │
  │  heap, SERVER)    │  │  for parse nodes │  │  record alloc)   │
  │ db_ws_alloc       │  │  + set elements) │  │                  │
  │ (workspace, CS)   │  │                  │  │                  │
  └──────────────────┘  └──────────────────┘  └──────────────────┘
```

### Lock-Free Structure Layers

```
  lockfree::hashmap<K,T>  ──uses──▶  lockfree::freelist<T>
         │                                    │
         └── both use ──▶  lockfree::tran::table
                                    │
                           lockfree::tran::descriptor  (per-thread)
                                    │
                           lockfree::tran::system  (index management)
                                    │
                           lockfree::bitmap  (index bitmap)
```

### Build Mode Behavior

Memory allocation functions behave differently based on build mode:

| Function | `SERVER_MODE` | `CS_MODE` | `SA_MODE` |
|----------|---------------|-----------|-----------|
| `db_private_alloc` | LEA heap via thread's `private_heap_id` | `db_ws_alloc` (workspace) | LEA heap if `db_on_server`, else workspace |
| `os_malloc` | malloc + resource tracking | plain malloc | plain malloc |
| `memory_wrapper.hpp` | Active (overrides new/delete) | Not active | Not active |
| `memory_cwrapper.h` | Active (overrides malloc/free) | Not active | Not active |

## Where to Look

| Task | Primary File(s) | Notes |
|------|-----------------|-------|
| Add error code | `error_code.h` | Also update `dbi_compat.h`, `cubrid.msg` (en+ko), `ER_LAST_ERROR`, CCI's `base_error_code.h` |
| Fix memory leak | `memory_alloc.c`, `resource_tracker.hpp` | Enable resource tracker in debug build |
| Fix lock-free data structure | `lockfree_hashmap.hpp` (modern), `lock_free.c` (legacy) | Modern uses transaction system; legacy uses hazard pointers |
| Add system parameter | `system_parameter.c/h` | Add `PRM_ID_*`, add to `prm_Def` array, update `PRM_LAST_ID` |
| Fix charset/encoding issue | `intl_support.c`, `chartype.c` | Check codeset (UTF8/EUCKR/ISO88591) handling |
| Fix collation issue | `language_support.c`, `locale_support.c`, `uca_support.c` | UCA for UTF-8, built-in for others |
| Fix timezone issue | `tz_support.c` | `tz_create_datetimetz`, DST rule evaluation |
| Add perf counter | `perf_monitor.c` (legacy) or `perf.hpp` (modern) | Legacy: add to `PSTAT_METADATA`; Modern: add to `statset_definition` |
| Port to new platform | `porting.h`, `porting.c` | Check all `#if defined(WINDOWS/LINUX/AIX/HPUX)` guards |
| Add serialized object | `packer.hpp`, `packable_object.hpp` | Implement `get_packed_size`/`pack`/`unpack` |
| Fix memory monitoring | `memory_monitor_sr.hpp`, `memory_cwrapper.h` | Check `MMON_METAINFO` placement, magic number |
| Debug resource leak | `resource_tracker.hpp` | Push/pop track, check `increment`/`decrement` balance |

## Conventions

### Memory Rules
- **Always** use `free_and_init(ptr)` instead of bare `free()` — frees and nullifies
- **Always** use `db_private_free_and_init(thrd, ptr)` for private heap — frees and nullifies
- `memory_wrapper.hpp` **MUST be the last `#include`** with comment `// XXX: SHOULD BE THE LAST INCLUDE HEADER`
- `memory_cwrapper.h` CAN be included in headers; `memory_wrapper.hpp` CANNOT

### Error Handling Rules
- `er_set()` always needs `ARG_FILE_LINE` as 2nd arg — macro expands to `__FILE__, __LINE__`
- Error severity levels: `ER_FATAL_ERROR_SEVERITY` > `ER_ERROR_SEVERITY` > `ER_SYNTAX_ERROR_SEVERITY` > `ER_WARNING_SEVERITY` > `ER_NOTIFICATION_SEVERITY`
- Check with `ASSERT_ERROR()` or `ASSERT_ERROR_AND_SET(error_code)` after operations that should set errors

### Lock-Free Rules
- Modern structures (`lockfree::hashmap`) use `lockfree::tran::system` for safe reclamation
- Legacy structures (`lock_free.c`) use `LF_TRAN_SYSTEM`/`LF_TRAN_ENTRY` — same concept, C API
- Always start transaction before accessing, end after done
- Retired nodes are reclaimed only when all concurrent readers have advanced past the retirement ID

### Resource Tracking (Debug)
- `resource_tracker.hpp` tracks page fixes, lock acquisitions — assert on unbalanced ops in debug builds
- `push_track()` before operation scope, `pop_track()` after — leaks detected at pop

## Anti-Patterns

- **Never** `#include "memory_wrapper.hpp"` before other includes — build will fail
- **Never** use bare `free()` — always `free_and_init()` or `db_private_free_and_init()`
- **Never** modify `heaplayers/` files — 3rd-party code referenced from here
- **Never** include `memory_wrapper.hpp` in a header file — use `memory_cwrapper.h` instead
- **Never** skip the `// XXX: SHOULD BE THE LAST INCLUDE HEADER` comment — CI enforces this
- **Never** suppress errors with empty catch blocks — always use `er_set()` + return codes

## Owner

From `.github/CODEOWNERS`:
- `src/base/` → **@beyondykk9** (primary)
- `src/base/memory_monitor_*` → **@hornetmj**
