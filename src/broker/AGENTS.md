# src/broker/ — Connection Broker & CAS

The broker process accepts driver connections and hands each one to a CAS (Common Application Server) worker process, which runs the session against `cub_server`. Executables are defined in the top-level `broker/CMakeLists.txt`; the code is here.

## Architecture

```
Client driver → cub_broker ──(client socket via send_fd/recv_fd)──→ cub_cas ×N → cub_server
                    └──────── shared memory (status, config, statistics) ────┘
                 (optional cub_proxy layer for sharding)
```

- On Unix, the broker passes the accepted client socket to a CAS (or proxy) over a Unix-domain socket with `SCM_RIGHTS` (`send_fd ()` in `broker_send_fd.c`, `recv_fd ()` in `broker_recv_fd.c`; called from `broker.c` and `cas_common_main.c`). Connection handoff bugs can be on this channel, not only in shared memory.
- Shared memory (`broker_shm.c/h`): `T_SHM_BROKER`, `T_SHM_APPL_SERVER`, `T_APPL_SERVER_INFO`, and `T_BROKER_INFO` (`broker_config.h`). The structs are not packed; their layout is shared by every process that attaches the segment.

## Key Files

| File | Role |
|------|------|
| `broker.c` | Broker process: listens, dispatches to CAS, forks and execs CAS and proxy |
| `cas.c` | CAS process main loop; `server_fn_table[]` maps function codes to handlers |
| `cas_function.c` | CAS protocol handlers (`fn_*`) |
| `cas_execute.c` | SQL execution in CAS |
| `cas_protocol.h` | Protocol: `t_cas_func_code` (`CAS_FC_*`), protocol versions |
| `cas_log.c` | CAS SQL log and slow-query log writer (`cas_slow_log_*`) |
| `broker_config.c` | `cubrid_broker.conf` parsing (including `SLOW_LOG`, `LONG_QUERY_TIME`) |
| `broker_shm.c/h` | Shared memory create/attach |
| `broker_admin.c`, `broker_admin_pub.c` | `cubrid broker` admin commands |
| `broker_monitor.c` | `broker_monitor` utility |
| `broker_acl.c` | Access control list |
| `broker_log_top.c`, `broker_log_util.c` | `broker_log_top` offline SQL-log analyzer (separate executable) |
| `shard_proxy.c`, `shard_metadata.c` | Shard proxy process and routing metadata |

## Where to Look

| Task | File |
|------|------|
| Client dispatch, CAS spawning | `broker.c` |
| Socket handoff broker → CAS | `broker_send_fd.c`, `broker_recv_fd.c`, `cas_common_main.c` |
| Query execution in CAS | `cas_execute.c`, `cas_function.c` |
| What the slow-query log records | `cas_log.c` (thresholds in `broker_config.c`) |
| Offline SQL-log analysis | `broker_log_top.c`, `broker_log_util.c` |
| Broker config | `broker_config.c` |
| ACL/security | `broker_acl.c` |
| Shard routing | `shard_metadata.c`, `shard_proxy.c` |

## Gotchas

- Adding a CAS function code: add `CAS_FC_*` before `CAS_FC_MAX` in `cas_protocol.h`, and add the handler and its name to `server_fn_table[]` and `server_func_name[]` in both `cas.c` and `cas_cgw.c` (the gateway CAS, `cub_cas_cgw`). A `static_assert` in each file checks the table size against `CAS_FC_MAX - 1`. Also add an entry at the same position in `proxy_client_fn_table[]` and `proxy_cas_fn_table[]` (`shard_proxy_handler.c`; use `fn_proxy_client_not_supported` / `fn_proxy_cas_relay_only` when the proxy does not handle the code): `cub_proxy` indexes both by `func_code - 1` for every code below `CAS_FC_MAX`, with no size check.
- Shared-memory layout: the attach check compares `magic` with `MAGIC_NUMBER`, which is built from the release version only (`broker_shm.c`). A layout change within one version is not detected, so rebuild and restart every broker-side process together.
- Shard code (`shard_*.c`) is a large subsystem inside this module.
