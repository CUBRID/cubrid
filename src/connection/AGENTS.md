# src/connection/ — Connection and Socket Layer

CSS (CUBRID Server/client Support) protocol over TCP: connection entries, send/receive, server request intake, and registration with `cub_master`. Follow [src/AGENTS.md](../AGENTS.md) for build modes. Request definitions and handlers are one layer up, in [src/communication/](../communication/AGENTS.md).

## Key Files

| File | Role |
|------|------|
| `connection_defs.h` | `CSS_CONN_ENTRY` and protocol constants |
| `connection_support.cpp` | Send/receive helpers shared by all modes (`css_send_data`, `css_readn`) |
| `connection_sr.c` | Server connection entries (`css_Conn_array`), `css_receive_data` |
| `server_support.c` | Server request handling: `css_push_server_task` → `css_server_task` → `css_internal_request_handler` → `css_Server_request_handler` (set to `net_server_request ()` in `network_sr.c`) |
| `connection_pool.cpp`, `connection_worker.cpp`, `coordinator.cpp`, `receiver.cpp`, `transmitter.cpp`, `connection_context.cpp`, `buffer.cpp`, `master_connector.cpp` | Server-only `cubconn` epoll connection-worker pool; receives requests and hands them to `css_push_server_task` |
| `connection_cl.cpp` | Client connections (class `connection_cl`: `css_connect_to_cubrid_server`, `css_connect_to_master_server`, `css_connect_to_master_for_info`) |
| `client_support.cpp` | Client connect and request helpers (calls `css_connect_to_cubrid_server`) |
| `connection_less.cpp`, `connection_list_cl.cpp` | Client request-id ("connectionless") interface and queued data/commands |
| `tcp.c` (`wintcp.c` on Windows), `host_lookup.c` | Socket operations and host-name lookup |
| `heartbeat.c` | HA process side of the link to the local `cub_master` ("heartbeat resource process common"): `hb_process_init` (used by `copylogdb`/`applylogdb`), `hb_register_to_master`, `hb_deregister_from_master` |

## Build Modes

Check `cubrid/`, `cs/`, and `sa/CMakeLists.txt` before you assume a file is mode-specific. `tcp.c`, `host_lookup.c`, `connection_support.cpp`, and `connection_globals.c` build in all three libraries. `client_support.cpp` and `connection_cl.cpp` build in CS and SA. The `cubconn` files, `connection_sr.c`, and `server_support.c` are server-only. A change to a shared file needs the SA build too.

## Where to Look

| Task | File |
|------|------|
| Client connect/disconnect | `connection_cl.cpp`, `client_support.cpp` |
| Server request intake, epoll workers | `connection_worker.cpp`, `connection_pool.cpp`, `receiver.cpp` |
| Server request dispatch to handlers | `server_support.c` (`css_internal_request_handler`) |
| Partial send/receive, socket errors | `connection_support.cpp`, `tcp.c` |
| HA process registration with `cub_master` | `heartbeat.c` |
| HA node-to-node heartbeat, failover | `src/executables/master_heartbeat.c` (not `heartbeat.c`) |

## Gotchas

- `css_send_data ()` and `css_receive_data ()` return a `css_error_code`. They retry partial I/O internally (`css_send_io_vector` and `css_vector_send` loop on partial writes; `css_readn` loops until `n` bytes arrive). Check the return code against `NO_ERRORS`. Do not add retry loops around them.
- `css_Conn_array` is allocated once in `css_init_conn_list ()` with `css_Num_max_conn` entries (twice the client limit). It does not grow.
