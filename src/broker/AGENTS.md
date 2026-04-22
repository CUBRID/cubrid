# src/broker/ — Connection Broker & CAS

Separate process architecture — broker spawns CAS (Common Application Server) worker processes.

## Key Files

| File | Role |
|------|------|
| `broker.c` | Main broker process — listens, dispatches to CAS |
| `cas.c` | CAS worker process — handles client sessions |
| `cas_execute.c` | SQL execution within CAS |
| `cas_function.c` | CAS protocol function dispatch |
| `cas_network.c` | CAS network I/O |
| `cas_handle.c` | Connection/statement handle management |
| `broker_shm.c/h` | Shared memory IPC between broker and CAS processes |
| `broker_config.c` | Broker configuration parsing |
| `broker_monitor.c` | `broker_monitor` utility |
| `broker_admin.c` | `cubrid broker` admin commands |
| `broker_log_util.c/h` | Log file parsing and analysis utilities |
| `broker_log_top.c/h` | Query log analysis (top queries) |
| `broker_acl.c` | Access control list management |
| `shard_metadata.c` | Shard routing metadata |
| `shard_proxy.c` | Shard proxy process |

## Architecture

```
Client App → Broker (port listener) → CAS₁, CAS₂, ... CASₙ → cub_server
                  ↕ shared memory ↕
```

- **Broker**: Single process, binds port, routes connections via shared memory
- **CAS**: One process per client connection, reusable via connection pooling
- **Shared memory**: `T_SHM_BROKER`, `T_SHM_APPL_SERVER` — status, config, statistics
- **Shard proxy**: Optional layer for database sharding

## Where to Look

| Task | File |
|------|------|
| Fix client dispatch | `broker.c` |
| Fix query execution in CAS | `cas_execute.c` |
| Fix connection pooling | `cas.c`, `broker_shm.c` |
| Fix slow query log | `broker_log_util.c`, `broker_log_top.c` |
| Fix broker config | `broker_config.c` |
| Fix ACL/security | `broker_acl.c` |
| Fix shard routing | `shard_metadata.c`, `shard_proxy.c` |

## Conventions

- Functions prefixed `cas_` (CAS process), `broker_` (broker process), `shm_` (shared memory)
- Broker and CAS are separate processes — communicate only through shared memory
- CAS uses its own error handling: `cas_error_log_write()`, not `er_set()`
- Config values come from `cubrid_broker.conf` — parsed in `broker_config.c`

## Gotchas

- Top-level `broker/` directory is just CMake target — actual code is here in `src/broker/`
- CAS processes fork from broker — shared memory must be carefully synchronized
- `T_BROKER_INFO` and `T_APPL_SERVER_INFO` are packed structs in shared memory — alignment matters
- Shard code (`shard_*.c`) is a significant subsystem within this module

