# Page-buffer inspector attachment

The Linux server includes the state-only inspector in every build configuration.
It is disabled by default. Set `enable_pgbuf_inspector=yes` in the server's startup
configuration and restart that database to enable it. The parameter is hidden,
server-only and cannot be changed or reloaded by a client.

Successful activation prints `PGBUF_INSPECTOR_READY` and the explicit local socket
path. The root is the existing master Unix-socket root (`CUBRID_TMP`, otherwise
`/tmp`). The `pgbuf-inspector` subdirectory must belong to the server account and
have mode 0700. The socket has mode 0600; its opaque filename derives from the
canonical database path and creation identity. This diagnostic is local only.

`PGBUF_INSPECTOR_UNAVAILABLE` means activation failed for this incarnation. Normal
database startup continues; the inspector does not retry. Correct the conflicting
path, permissions or resource condition before restarting. Socket absence alone
cannot distinguish disabled from unavailable. Unsafe existing entries are preserved.
Shutdown removes only the socket inode created by this incarnation.

Clients must connect explicitly, verify server credentials and filesystem ownership,
and implement the [wire v1 contract](v1/contract.md). The server checks Linux
`SO_PEERCRED` before sending protocol data. The peer effective UID must exactly
match the server's; root and group membership confer no exception. Other platforms
and non-server binaries do not expose this endpoint.

A handshake carries an unpredictable 128-bit incarnation, LRU topology, database
creation and every persistent volume's creation/device/inode identity. A copy has
its own physical identity; a restart has a new incarnation. Volume creation values
are cached while normal volume boot/format/recovery already reads their metadata.
Attachment copies that metadata under the disk extension mutex using a nonblocking
lock attempt, then obtains file identity after releasing the mutex. It never fixes
or reads a volume-header page. Temporary-type volumes do not enter the proof.
A complete proof that exceeds the handshake byte budget is refused, never shortened.

The daemon admits two clients, validates controls within 4 KiB and depth 16, and
limits each handshake/output frame to 64 KiB. Socket reads and writes are
nonblocking. Attachment expires after 500 ms; pending output expires after 250 ms
without write progress. Output uses a small kernel send buffer so a non-reading
peer cannot hide indefinitely behind kernel buffering. Identity preparation does
not extend the attachment deadline. Clients also enforce their end-to-end deadline.

An authenticated client can request a bounded resident-set scan. See
[scan sampling and limits](scanning.md) for the sampling policy, unknown fields,
partial coverage and resource guarantees.

## Verification

Enable the existing `UNIT_TEST_PGBUF_INSPECTOR` unit-test target when configuring
a test build. `ctest -R '^test_pgbuf_inspector$' --output-on-failure` executes wire
conformance and socket behavior. Credential cases require two real mapped UIDs:

```sh
unshare --map-auto --map-root-user build_preset_debug_gcc/bin/test_pgbuf_inspector '[.credential]'
```

With `CUBRID` and `PATH` selecting the installed debug or release build, run:

```sh
python3 unit_tests/pgbuf_inspector/server_attachment.py
```

The harness creates an isolated database registry, configuration and master port,
checks SQL service alongside endpoint behavior, and prints its retained evidence
directory. It checks default-off absence, physical identity, private permissions,
restart incarnation, copied-database identity, conflicting-path startup and cleanup.
Its copy check also exercises utility mounts with negative system volume IDs.
Pass `--scan` to also validate real resident records, semantic fields, topology,
framing/counts and completion across startup, restart and copied databases.
Use `TMPDIR` on a filesystem with enough space for disposable databases; keep
its path short enough for Unix sockets. Controlled dirty/eviction oracles and
performance acceptance remain separate checks.
