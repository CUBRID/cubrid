# src/monitor/ — Performance Statistics

Centralized performance-statistics collection and export. Provides statistic primitives (accumulator, gauge, max/min), per-transaction sheet tracking, a global registry for named statistics, and a server-only VACUUM overflow-page threshold monitor.

## Key Files

| File | Role |
|------|------|
| `monitor_definition.hpp` | Core types: `statistic_value` (uint64), clock aliases, `fetch_mode` constants |
| `monitor_statistic.cpp/.hpp` | Statistic primitives and templates: `primitive`, `atomic_primitive`, `accumulator`, `gauge`, `max`/`min` |
| `monitor_transaction.cpp/.hpp` | `transaction_sheet_manager` (per-transaction sheet lifecycle) and `transaction_statistic<S>` wrapper |
| `monitor_collect.cpp/.hpp` | Grouped-statistics helpers: `timer_statistic`, `counter_timer_statistic`, name-builder utilities |
| `monitor_registration.cpp/.hpp` | Global `monitor` registry: `register_statistics()`, `fetch_global_statistics()`, `get_global_monitor()` |
| `monitor_vacuum_ovfp_threshold.cpp/.hpp` | Server-only VACUUM overflow-page threshold monitor (`ovfp_threshold_mgr`) |

## Where to Look

| Task | File |
|------|------|
| Add a new statistic | Create statistic object → `monitor_registration.hpp` (`register_statistics()`) |
| Fix per-transaction stats | `monitor_transaction.cpp` (`transaction_sheet_manager`) |
| Fix statistic fetch/export | `monitor_registration.cpp` (fetch loop) |
| Fix VACUUM OVFP monitoring | `monitor_vacuum_ovfp_threshold.cpp` (server-only) |

## Public API

- `get_global_monitor()` — returns the global `monitor` instance
- `monitor::register_statistics(count, fetch_function, names)` — register named statistics
- `monitor::fetch_global_statistics(buf)` / `fetch_transaction_statistics(buf)` — snapshot all statistics
- `monitor::allocate_statistics_buffer()` — allocate buffer sized for all registered statistics
- `transaction_sheet_manager::start_watch()` / `end_watch()` / `get_sheet()` — per-transaction sheet lifecycle

## Build Integration

- **Core files** (`monitor_collect`, `monitor_registration`, `monitor_statistic`, `monitor_transaction`): compiled into Server and SA builds (not part of the CS build; `cs/CMakeLists.txt` does not list these sources)
- **`monitor_vacuum_ovfp_threshold.*`**: server-only component, included only in the Server build (guarded with `#if defined(SERVER_MODE)`)

## Conventions

- Namespace: `cubmonitor`
- Types/functions: `snake_case` (e.g., `transaction_sheet_manager`, `register_statistics`)
- Member fields: `m_` prefix (e.g., `m_all_names`, `m_total_statistics_count`)
- `statistic_value` is the universal wire type (uint64)
- `fetch_mode`: `FETCH_GLOBAL` or `FETCH_TRANSACTION_SHEET`
- `fetch_function` signature: `std::function<void(statistic_value*, fetch_mode)>`

