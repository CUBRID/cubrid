# src/communication/ — Network Interface Layer

Client/server request interface: the request list, the server dispatch table, request packing and unpacking, method/xs callback glue, and the byte-stream transport. Follow [src/AGENTS.md](../AGENTS.md) for build modes and CMake registration. The socket layer below this one is in [src/connection/](../connection/AGENTS.md).

## Key Files

| File | Role |
|------|------|
| `network.h` | `NET_SERVER_REQUEST_LIST` X-macro → `enum net_server_request`; capability flags |
| `network_request_def.hpp` | Server-only `net_request` (`processing_function`, `action_attribute`) and `net_req_act` flags |
| `network_common.cpp` | Request-name array, `get_net_request_name ()` |
| `network_sr.c` | `net_server_init ()` fills `net_Requests[]`; `net_server_request ()` dispatches |
| `network_interface_sr.cpp/.h` | Server handlers `s*` (unpack, call `x*`, reply) |
| `network_cl.c/.h` | Client send primitives (`net_client_request*`), capability checks |
| `network_interface_cl.c/.h` | Client functions `c*` (CS: pack and send; SA: direct call) |
| `network_callback_cl.cpp/.hpp` | Client method/xs callbacks (`xs_queue_send`, `xs_send_queue`, `xs_pack_and_queue`) |
| `network_callback_sr.cpp/.hpp` | Server method/xs callbacks (`xs_callback_send`, `xs_callback_receive`) |
| `network_histogram.cpp/.hpp` | Client per-request histogram (`net_histo_ctx`) |
| `stream_client.c`, `stream_session.cpp`, `stream_handle.cpp` | Byte-stream transport over the `NET_SERVER_STREAM_*` requests (consumer registry: `stream_session.hpp`) |
| `stream_debug_echo.cpp` | Debug echo consumer for the stream transport |

## Build Integration

The `_cl`/`_sr` suffix is only a hint. This table (checked against `cubrid/`, `cs/`, `sa/CMakeLists.txt`) is authoritative. Code that SA mode needs must not go only into a server-only file.

| File | Server (`cubrid/`) | CS (`cs/`) | SA (`sa/`) |
|------|:--:|:--:|:--:|
| `network_common.cpp` | ✓ | ✓ | — |
| `network_sr.c` | ✓ | — | — |
| `network_interface_sr.cpp` | ✓ | — | — |
| `network_callback_sr.cpp` | ✓ | — | ✓ |
| `network_cl.c` | — | ✓ | — |
| `network_interface_cl.c` | — | ✓ | ✓ |
| `network_callback_cl.cpp` | — | ✓ | ✓ |
| `network_histogram.cpp` | — | ✓ | ✓ |
| `stream_client.c` | — | ✓ | ✓ |
| `stream_session.cpp`, `stream_handle.cpp` | ✓ | — | ✓ |
| `stream_debug_echo.cpp` | ✓ | ✓ | — |

## Adding a Server Request

Example names: request `NET_SERVER_FOO`, client `cfoo`, server handler `sfoo`, worker `xfoo`.

1. `network.h`: append `NET_SERVER_REQUEST_ITEM(NET_SERVER_FOO)` at the **end** of `NET_SERVER_REQUEST_LIST`. The enum ordinal is the request number sent on the wire. Inserting in the middle renumbers every later request. Clients and servers of different releases may connect when `rel_get_net_compatible ()` (`src/base/release_string.c`) allows it, so existing numbers must not move. `NET_SERVER_PING_WITH_HANDSHAKE = 999` is fixed.
2. Worker: implement `xfoo (THREAD_ENTRY *, ...)` in the owning server module and declare it in `src/base/xserver_interface.h`. It must build into `libcubridsa` as well, because the SA client calls it directly.
3. Server handler: implement `void sfoo (THREAD_ENTRY *thread_p, unsigned int rid, char *request, int reqlen)` in `network_interface_sr.cpp` and declare it in `network_interface_sr.h`. Copy the unpack and reply pattern of a similar request.
4. Dispatch: in `net_server_init ()` (`network_sr.c`), set `net_Requests[NET_SERVER_FOO].processing_function = sfoo` and, if needed, `action_attribute`. `net_server_request ()` asserts that the function is set. Flags (`net_req_act`):
   - `CHECK_DB_MODIFICATION`: reject the request when the database does not allow modification.
   - `CHECK_AUTHORIZATION`: reject a client that is not DBA (`ER_AU_DBA_ONLY`).
   - `IN_TRANSACTION` / `OUT_TRANSACTION`: set / clear `conn->in_transaction` before / after the handler.
   - `SET_DIAGNOSTICS_INFO`: set on some entries, but `net_server_request ()` does not read it.
   - Most entries set none. Some handlers check authorization themselves.
5. Client: implement `cfoo (...)` in `network_interface_cl.c` and declare it in `network_interface_cl.h`, with both branches:
   - `#if defined (CS_MODE)`: pack the arguments, call `net_client_request*` with `NET_SERVER_FOO`, and unpack the reply.
   - `#else` (SA): `enter_server ()` → `xfoo ()` → `exit_server ()`.
6. Build `libcubrid`, `libcubridcs`, and `libcubridsa`. No unit test covers requests; verify end to end with a CS client against a server.
