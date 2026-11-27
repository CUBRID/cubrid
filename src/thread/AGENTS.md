# src/thread/ — Worker Pools, Daemons, Thread Entries (`cubthread`)

Thread manager, worker pools, daemon threads, the per-thread context `THREAD_ENTRY`, and legacy critical sections. Follow [src/AGENTS.md](../AGENTS.md) for build modes and memory rules.

## Key Files

| File | Role |
|------|------|
| `thread_manager.hpp/.cpp` | `cubthread::manager` (`get_manager ()`): entry pool, `create_worker_pool`, `create_daemon`, `push_task`; `REGISTER_*` macros; `thread_create_worker_pool<stats_t, pool_t> ()` |
| `thread_worker_pool.hpp` | `class worker_pool` (not a template; `context_type = entry`) |
| `thread_worker_pool_impl.*`, `thread_worker_pool_elastic.hpp` | Pool implementations, chosen by `pool_t::basic` / `pool_t::elastic` and `stats_t` (`thread_compat.hpp`) |
| `thread_worker_pool_taskcap.*` | `worker_pool_task_capper`: limits tasks pushed into a pool |
| `thread_entry.hpp/.cpp` | `cubthread::entry` = `THREAD_ENTRY` |
| `thread_task.hpp`, `thread_entry_task.hpp` | `task<Context>`; `entry_task` = `task<entry>`; `entry_manager` |
| `thread_daemon.hpp/.cpp` | `cubthread::daemon` (created by the manager; do not subclass) |
| `thread_looper.hpp`, `thread_waiter.hpp` | Daemon sleep patterns; condition-variable wrapper |
| `concurrency_slot.hpp/.cpp` | Subscriber/publisher slots |
| `thread_lockfree_hash_map.hpp` | `cubthread::lockfree_hashmap` |
| `critical_section.c/.h`, `critical_section_tracker.*` | Legacy named critical sections (`CSECT_*`) and their usage tracker |

## Adding a Daemon

1. Implement the work as an `entry_task` (override `execute (entry &)`).
2. Choose a `looper`:
   - `looper ()` (default): after each run, sleeps until an explicit `wakeup ()`. It has no timer. (The task runs once at start, then once per wakeup.)
   - `looper (delta_time)`: fixed period.
   - `looper (std::array<delta_time, N>)`: increasing periods, reset when woken up.
   - `looper (period_function)`: period computed each loop.
3. Put `REGISTER_DAEMON (name);` at file scope. The manager sizes its entry pool from the `REGISTER_*` registries, and `create_daemon ()` asserts that the daemon count stays within `REGISTER_DAEMON` registrations.
4. Call `cubthread::get_manager ()->create_daemon (looper, task, "name")`, and `destroy_daemon ()` at shutdown. Daemons exist only in `SERVER_MODE`: in SA, `create_daemon ()` asserts and returns `NULL`, so put the daemon code under `#if defined (SERVER_MODE)`. Examples: `src/session/session.c`, `src/storage/double_write_buffer.cpp`.

## Adding a Worker Pool or Task

- Pool: `REGISTER_WORKERPOOL (name, getter)` at file scope, then `thread_create_worker_pool<...> (...)` or `get_manager ()->create_worker_pool<...> (...)`.
- Task: subclass `entry_task` and push it with `get_manager ()->push_task (pool, task)`.

## Gotchas

- `THREAD_ENTRY *thread_p` is passed to nearly every server function. It carries `private_heap_id` (for `db_private_alloc`), `tran_index`, `interrupted`, the error context (`m_error`), latch/lock wait state, and the CS tracker. A layout change affects the whole server.
- Adding a critical section: add `CSECT_*` before `CSECT_LAST` in `critical_section.h` and its name at the same position in `csect_Names[]` in `critical_section.c`. The tracker (debug `SERVER_MODE` only) asserts on illegal re-entry and reader/writer misuse. The only lock order it checks is in `check_csect_interdependencies ()`; if the new section must not be entered while another is held, add that check there.
