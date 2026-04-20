# src/communication/ — Network Interface Layer

Client/server network interface: request packing/unpacking, server dispatch table, method/xs callback glue, and client-side histogram collection.

## Key Files

| File | Role |
|------|------|
| `network.h` | Central definitions: `NET_SERVER_REQUEST_LIST`, request constants, macros |
| `network_request_def.hpp` | Server-only `net_request` struct, `net_req_act` flags, `net_server_func` type |
| `network_common.cpp` | Shared helpers: request-name array, `get_net_request_name()` |
| `network_cl.c` | Client-side support: request sending, error mapping, capability checks |
| `network_interface_cl.c/.h` | Client-side interface bridging higher-level APIs to network layer |
| `network_sr.c` | Server-side support: `net_server_init()`, `net_Requests[]` dispatch table |
| `network_interface_sr.c/.h` | Server-side request processing and enter/exit server logic |
| `network_callback_cl.cpp/.hpp` | Client callback glue (`xs_queue_send`, `xs_set/get_conn_info`) for method/xs in CS/SA |
| `network_callback_sr.cpp/.hpp` | Server callback glue (`xs_callback_send/receive`) with server and non-server variants |
| `network_histogram.cpp/.hpp` | Client-side per-request histogram/perf collection (`net_histo_ctx`) |

## Where to Look

| Task | File |
|------|------|
| Add new server request | `network.h` (add to request list) + `network_sr.c` (register handler in `net_Requests[]`) + `network_interface_sr.c` (implement handler) |
| Add client-side request call | `network_interface_cl.c` |
| Fix method/xs callback | `network_callback_cl.cpp` (client) or `network_callback_sr.cpp` (server) |
| Fix request histogram | `network_histogram.cpp` |

## Client vs Server Split

- **File naming**: `_cl` suffix = client (CS_MODE/SA_MODE), `_sr` suffix = server (SERVER_MODE/SA_MODE)
- **Preprocessor**: `network_request_def.hpp` enforces `SERVER_MODE` with `#error`
- **Build targets**: different files per mode (see below)

## Build Integration

| File | Server (`cubrid/`) | CS (`cs/`) | SA (`sa/`) |
|------|:--:|:--:|:--:|
| `network_common.cpp` | ✓ | ✓ | — |
| `network_sr.c` | ✓ | — | — |
| `network_interface_sr.c` | ✓ | — | — |
| `network_callback_sr.cpp` | ✓ | — | ✓ |
| `network_cl.c` | — | ✓ | — |
| `network_interface_cl.c` | — | ✓ | ✓ |
| `network_callback_cl.cpp` | — | ✓ | ✓ |
| `network_histogram.cpp` | — | ✓ | ✓ |

## Conventions

- Function prefixes: `net_`, `net_client_*`, `net_server_*` (network), `css_*` (connection support), `sboot_*` (boot handlers), `stran_*` (transaction handlers), `xs_*` (method/xs callbacks)
- Server dispatch: `net_Requests[]` array indexed by `NET_SERVER_*` constants; each entry has `processing_function` + `action_attribute` flags
- Follow `memory_wrapper.hpp` as last include rule

