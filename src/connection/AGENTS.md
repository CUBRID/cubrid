# src/connection/ — Client-Server Communication

28 code files. TCP networking, CSS protocol, heartbeat.

## Key Files

| File | Role |
|------|------|
| `connection_sr.c` | Server-side connection handling |
| `connection_cl.c` | Client-side connection management |
| `connection_support.c` | Connection utility functions |
| `tcp.c` | TCP socket operations (connect, listen, send, recv) |
| `heartbeat.c` | HA heartbeat protocol |
| `server_support.c` | Server request dispatch |
| `client_support.c` | Client request sending |
| `connection_defs.h` | Connection data structures, CSS_CONN_ENTRY |
| `connection_globals.c` | Global connection state |

## Where to Look

| Task | File |
|------|------|
| Fix client connect/disconnect | `connection_cl.c` |
| Fix server request handling | `connection_sr.c`, `server_support.c` |
| Fix TCP-level issue | `tcp.c` |
| Fix HA heartbeat | `heartbeat.c` |
| Fix request/response protocol | `client_support.c`, `server_support.c` |

## Protocol

- CSS (CUBRID Server/client Support) protocol over TCP
- Request-response model: client sends request ID + data, server dispatches to handler
- `CSS_CONN_ENTRY`: per-connection state on server side
- Heartbeat: periodic keep-alive between HA nodes

## Conventions

- Functions prefixed `css_` (CSS protocol), `net_` (network layer)
- Server connections tracked in `css_Conn_array` — fixed-size array
- Client connections use `css_connect_to_master()` → `css_connect_to_cubrid_server()`
- `SERVER_MODE` for server-side files, `CS_MODE` for client-side files

## Gotchas

- Connection module is NOT used in `SA_MODE` — standalone links both client and server in-process
- `heartbeat.c` is HA-specific — only active in replicated setups
- `css_send_data()` / `css_receive_data()` can partially send/recv — must handle short reads/writes

## Owner

CODEOWNERS: @beyondykk9
