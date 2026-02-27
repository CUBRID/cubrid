# src/thread/ — Worker Pools & Daemon Threads (C++17)

Modern C++17 threading infrastructure.

## Key Files

| File | Role |
|------|------|
| `thread_manager.hpp/cpp` | Thread manager — creates/destroys worker pools and daemons |
| `thread_worker_pool.hpp` | `cubthread::worker_pool<Context>` — generic thread pool |
| `thread_daemon.hpp/cpp` | `cubthread::daemon` — background daemon threads |
| `thread_entry.hpp` | `THREAD_ENTRY` — per-thread context (passed as `thread_p`) |
| `thread_entry_task.hpp` | `cubthread::entry_task` — base class for pool tasks |
| `thread_looper.hpp` | `cubthread::looper` — configurable sleep/wake patterns for daemons |
| `thread_waiter.hpp` | `cubthread::waiter` — condition variable wrapper |
| `thread_task.hpp` | `cubthread::task<Context>` — generic task base |
| `thread_compat.hpp` | Compatibility layer, thread type enum |
| `thread_lockfree_hash_map.hpp` | Lock-free concurrent hash map |
| `critical_section.c` | Legacy critical section (mutex) implementation |
| `critical_section_tracker.hpp` | CS acquisition tracking for deadlock detection |
| `internal_tasks_worker_pool.hpp` | Internal system task pool |

## Where to Look

| Task | File |
|------|------|
| Add new daemon | Subclass `cubthread::daemon`, register in `thread_manager` |
| Add worker pool task | Subclass `cubthread::entry_task`, submit to pool |
| Fix thread context | `thread_entry.hpp` — `THREAD_ENTRY` fields |
| Fix daemon wake/sleep | `thread_looper.hpp` |
| Fix critical section | `critical_section.c` |
| Fix lock-free map | `thread_lockfree_hash_map.hpp` |

## Architecture

```
thread_manager
  ├── worker_pool (handles client requests)
  │     └── entry_task instances
  ├── daemon threads (vacuum, checkpoint, flush, etc.)
  │     └── looper (sleep pattern)
  └── THREAD_ENTRY (per-thread context)
```

## THREAD_ENTRY

The `THREAD_ENTRY *thread_p` parameter on virtually all server-side functions:
- Thread-local allocator (`db_private_alloc`)
- Transaction index
- Error context
- Lock/latch tracking
- Interrupt flag

## Conventions

- Namespace: `cubthread`
- Worker pool: templated on context type, typically `THREAD_ENTRY`
- Daemons: constructed with `looper` for sleep strategy, `entry_task` for work
- Legacy code uses `critical_section.c` (named CS) — new code should use C++ mutexes
- `thread_compat.hpp` bridges old C code that uses thread IDs

## Gotchas

- `THREAD_ENTRY` is the most-passed parameter in the codebase — changing its layout affects everything
- Worker pool sizing affects performance — too few = contention, too many = overhead
- Daemon `looper` patterns: `INFINITE_LOOPER` (always active), periodic, or delta-based
- Critical sections are named — CS tracker can detect potential deadlocks via ordering

