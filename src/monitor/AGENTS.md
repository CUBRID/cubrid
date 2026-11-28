# src/monitor/ — Statistic Primitives (`cubmonitor`)

C++ statistic primitives (accumulator, gauge, max/min, timers), per-transaction sheets, a `monitor` registry, and the server-only VACUUM overflow-page threshold monitor.

## Read This First

The `cubmonitor::monitor` registry (`get_global_monitor ()`, `register_statistics ()`, `register_to_monitor ()`) has no caller in the engine; only `unit_tests/monitor/` uses it. A statistic registered there is never fetched or shown by `cubrid statdump`.

- To add a counter that `cubrid statdump` shows, use `src/base/perf_monitor.h` / `perf_monitor.c`: a `PSTAT_*` id in `PERF_STAT_ID` and the entry at the same position in `pstat_Metadata[]`. Put a non-complex statistic (single, counter-timer, or computed ratio) before the first complex entry (the "Complex statistics" group), not just before `PSTAT_COUNT`: the statdump loops stop at the first `PSTAT_COMPLEX_VALUE` entry and call `f_dump_in_file` / `f_dump_in_buffer` on every entry after it, and those pointers are NULL for non-complex entries. `perfmon_initialize ()` asserts that each entry's position matches its id. See [src/base/AGENTS.md](../base/AGENTS.md).
- Use the primitives in this directory as building blocks. Example: `src/base/lockfree_hashmap.hpp` uses `cubmonitor::atomic_counter_timer_stat` from `monitor_collect.hpp`.

## Key Files

| File | Role |
|------|------|
| `monitor_definition.hpp` | `statistic_value` (uint64), clock aliases, `fetch_mode` (`FETCH_GLOBAL`, `FETCH_TRANSACTION_SHEET`) |
| `monitor_statistic.cpp/.hpp` | `primitive`, `atomic_primitive`, and `accumulator_`/`gauge_`/`max_`/`min_statistic` (plain and `_atomic_` variants) |
| `monitor_collect.cpp/.hpp` | Grouped statistics: `timer_statistic`, `counter_timer_statistic`, `atomic_counter_timer_stat` |
| `monitor_transaction.cpp/.hpp` | `transaction_sheet_manager`, `transaction_statistic<S>` |
| `monitor_registration.cpp/.hpp` | `monitor` registry (unused by the engine; see above) |
| `monitor_vacuum_ovfp_threshold.cpp/.hpp` | `ovfp_threshold_mgr`, used by `src/query/vacuum.c` |

## Build Integration

- `monitor_collect`, `monitor_registration`, `monitor_statistic`, `monitor_transaction`: server and SA libraries (`cubrid/`, `sa/CMakeLists.txt`), not CS.
- `monitor_vacuum_ovfp_threshold`: server only (`cubrid/CMakeLists.txt`), guarded with `SERVER_MODE`.

## Conventions

- Namespace `cubmonitor`; `snake_case` names; member fields `m_`.
- `fetch_function` signature: `std::function<void (statistic_value *, fetch_mode)>`.
