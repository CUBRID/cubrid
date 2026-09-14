#!/usr/bin/env python3
#
#
#  Copyright 2016 CUBRID Corporation
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#

"""Run real installed cub_server attachment checks in an isolated database registry.
Requires CUBRID/PATH from the selected build. Keeps its log directory for evidence.
"""
import argparse
import json
import os
import re
from pathlib import Path
import socket
import select
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--scan", action="store_true", help="also validate real resident-set scans")
parser.add_argument("--isolation", action="store_true", help="prove SQL progress under stalled observation and overload")
args = parser.parse_args()

root = Path(tempfile.mkdtemp(prefix="pgbuf-server-"))
env = os.environ.copy()
env.update(CUBRID_DATABASES=str(root), CUBRID_TMP=str(root), CUBRID_CONF_FILE=str(root / "cubrid.conf"))
with socket.socket() as reservation:
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]
name = "inspector02"
base = f"[common]\ncubrid_port_id={port}\ndata_buffer_size=64M\nlog_buffer_size=4M\nvacuum_log_block_pages=4\n"
if args.isolation:
    base += "enable_string_compression=no\n"
conf = root / "cubrid.conf"
conf.write_text(base)
(root / "databases.txt").touch()
log = (root / "commands.log").open("w")

def run(*args, check=True):
    log.write("$ " + " ".join(args) + "\n")
    log.flush()
    result = subprocess.run(args, env=env, cwd=root, stdout=log, stderr=subprocess.STDOUT, text=True, timeout=60)
    if check and result.returncode:
        raise RuntimeError(f"{args}: {result.returncode}; see {root / 'commands.log'}")
    return result

def query():
    run("csql", "-u", "dba", "-c", "select 1;", name)

def start(enabled):
    conf.write_text(base + f"enable_pgbuf_inspector={'yes' if enabled else 'no'}\n")
    run("cubrid", "server", "start", name)
    query()

def stop():
    run("cubrid", "server", "stop", name)

def attach(volume_path=None):
    paths = list((root / "pgbuf-inspector").glob("*.sock"))
    assert len(paths) == 1, paths
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(.5)
        client.connect(str(paths[0]))
        client.sendall(b'{"type":"client_hello","supported_majors":[1]}\n')
        response = b""
        while not response.endswith(b"\n"):
            chunk = client.recv(65536 - len(response))
            assert chunk
            response += chunk
        hello = json.loads(response)
        assert hello["type"] == "server_hello", hello
        assert hello["volumes"]
        for forbidden in ("path", "pid", "uid", "gid"):
            assert forbidden not in hello
        main_volume = volume_path or root / name
        expected_files = [main_volume] + sorted(main_volume.parent.glob(main_volume.name + "_x[0-9][0-9][0-9]"))
        assert [v["volid"] for v in hello["volumes"]] == list(range(len(expected_files)))
        for volume, filename in zip(hello["volumes"], expected_files):
            st = filename.stat()
            assert int(volume["device"]) == st.st_dev
            assert int(volume["inode"]) == st.st_ino
            assert int(volume["volume_creation"]) > 0
        if args.scan:
            client.sendall((json.dumps({"type": "scan_request", "incarnation": hello["incarnation"]}) + "\n").encode())
            client.settimeout(2)
            frames = []
            total = 0
            with client.makefile("rb") as stream:
                while True:
                    line = stream.readline(4097)
                    assert line and line.endswith(b"\n") and len(line) <= 4096, line
                    total += len(line)
                    assert total <= 1024 * 1024 * 1024
                    frame = json.loads(line)
                    assert frame["incarnation"] == hello["incarnation"]
                    frames.append(frame)
                    if frame["type"] == "scan_footer":
                        break
            header, footer = frames[0], frames[-1]
            pages = frames[1:-1]
            assert header["type"] == "scan_header"
            assert int(header["scan_seq"]) > 0
            assert int(header["start_time_us"]) > 0 and int(footer["end_time_us"]) > 0
            assert all(frame["scan_seq"] == header["scan_seq"] for frame in frames)
            assert len(pages) == footer["record_count"] > 0
            assert len(pages) <= footer["visited_slots"] <= 65536
            assert isinstance(footer["truncated"], bool)
            required = {"volid", "pageid", "latch_mode", "waiter_present", "fix_count", "dirty", "flushing",
                        "async_flush_requested", "to_vacuum", "lru_zone", "lru_list_kind", "lru_list_index"}
            for page in pages:
                assert page["type"] == "page" and required <= page.keys()
                assert 0 <= page["volid"] <= 32767 and 0 <= page["pageid"] <= 2147483647
                assert page["latch_mode"] in ("none", "read", "write", "flush", "unknown")
                assert page["lru_zone"] in ("lru1", "lru2", "lru3", "void", "invalid")
                kind, index = page["lru_list_kind"], page["lru_list_index"]
                if kind in ("shared", "private"):
                    assert 0 <= index < hello[kind + "_lru_count"]
                else:
                    assert kind in ("none", "invalid") and index is None
                for field in ("page_lsa", "oldest_unflush_lsa"):
                    if field in page and page[field] is not None:
                        assert 0 <= int(page[field]["pageid"]) <= 9223372036854775807
                        assert 0 <= page[field]["offset"] <= 32767
            assert any({"page_lsa", "oldest_unflush_lsa", "page_kind"} <= page.keys() for page in pages)
            (root / (hello["incarnation"] + "-scan.json")).write_text(json.dumps(frames, indent=2))
            print("PASS real resident scan", json.dumps(footer), "framed_bytes", total, flush=True)
        return paths[0], hello

def stalled_worker_check(path):
    """Use unmodified server sockets and committed SQL, with checked stall preconditions."""
    run("csql", "-u", "dba", "-c",
        "create table isolation_rows (id integer, payload varchar(4000)); "
        "insert into isolation_rows select level, repeat('x',3500) from db_root connect by level <= 2000; commit;", name)
    clients = []
    worker = None
    try:
        hellos = []
        for _ in range(2):
            client = socket.socket(socket.AF_UNIX)
            clients.append(client)
            client.settimeout(.5)
            client.connect(str(path))
            client.sendall(b'{"type":"client_hello","supported_majors":[1]}\n')
            hello = b""
            while not hello.endswith(b"\n"):
                chunk = client.recv(65536 - len(hello))
                assert chunk
                hello += chunk
            hellos.append(json.loads(hello))
            assert hellos[-1]["type"] == "server_hello"
        request = (json.dumps({"type": "scan_request", "incarnation": hellos[0]["incarnation"]}) + "\n").encode()
        # Establish a large real capture before using its workload for a stall.
        clients[0].sendall(request)
        baseline = b""
        deadline = time.monotonic() + 2
        while b'"type":"scan_footer"' not in baseline:
            assert time.monotonic() < deadline
            chunk = clients[0].recv(65536)
            assert chunk
            baseline += chunk
        frames = [json.loads(line) for line in baseline.splitlines()]
        assert frames[-1]["type"] == "scan_footer"
        assert frames[-1]["record_count"] == len(frames) - 2
        assert len(baseline) > 128 * 1024, "inconclusive: fixture cannot saturate output"
        time.sleep(.11)  # Cadence only; the next header independently proves acceptance.
        started = time.monotonic()
        clients[0].sendall(request)
        prefix = clients[0].recv(8192, socket.MSG_PEEK)
        assert b'"type":"scan_header"' in prefix and b'"scan_footer"' not in prefix
        hangup = select.poll()
        hangup.register(clients[0], select.POLLHUP | select.POLLERR)
        # Start the second admitted client's scan after the global floor. Both
        # sockets must remain live throughout the worker/overload measurement.
        time.sleep(.11)
        clients[1].sendall(request)
        second_prefix = clients[1].recv(8192, socket.MSG_PEEK)
        assert b'"type":"scan_header"' in second_prefix and b'"scan_footer"' not in second_prefix
        hangup.register(clients[1], select.POLLHUP | select.POLLERR)
        assert not hangup.poll(0), "inconclusive: stalled client already closed"
        def saturated_outputs():
            # Linux socket diagnostics expose the *server* send-memory charge
            # and budget. Match each accepted socket by its client's inode.
            # A large previous capture alone is not a saturation oracle.
            report = subprocess.run(["ss", "-x", "-m", "-n", "-H", "-O"],
                                    capture_output=True, text=True, check=True, timeout=1).stdout
            peers = {str(os.fstat(client.fileno()).st_ino) for client in clients}
            result = []
            for line in report.splitlines():
                fields = line.split()
                if len(fields) < 9 or fields[4] != str(path) or fields[7] not in peers:
                    continue
                memory = re.search(r"skmem:\(([^)]*)\)", line)
                assert memory, "inconclusive: kernel socket-memory diagnostics unavailable"
                values = dict(re.findall(r"([a-z]+)([0-9]+)", memory[1]))
                result.append({"send_queue": int(fields[3]), "charged": int(values["t"]),
                               "budget": int(values["tb"])})
            assert len(result) == 2, "inconclusive: two server sockets not identified"
            assert all(item["send_queue"] > 0 and item["charged"] >= item["budget"] > 0 for item in result), \
                "inconclusive: both current scans must saturate server send memory"
            return result
        saturated_before = saturated_outputs()
        worker_log = (root / "isolation-worker.log").open("w")
        worker = subprocess.Popen(
            ["csql", "-u", "dba", "-c",
             "update isolation_rows set payload='worker-progress' where id=1; commit; "
             "select 314159 from isolation_rows where id=1 and payload='worker-progress';", name],
            env=env, cwd=root, stdout=worker_log, stderr=subprocess.STDOUT)
        busy = 0
        while worker.poll() is None or busy < 2:
            assert time.monotonic() - started < .25, "inconclusive: worker did not overlap live stalled client"
            with socket.socket(socket.AF_UNIX) as excess:
                excess.settimeout(.2)
                excess.connect(str(path))
                response = b""
                while not response.endswith(b"\n"):
                    chunk = excess.recv(4096)
                    assert chunk
                    response += chunk
                assert json.loads(response) == {"type": "error", "code": "busy"}
                busy += 1
        worker_log.close()
        assert worker.returncode == 0
        worker_result = (root / "isolation-worker.log").read_text()
        assert sum(line.strip() == "314159" for line in worker_result.splitlines()) == 2
        assert "1 row affected." in worker_result and "1 row selected." in worker_result
        assert not hangup.poll(0), "inconclusive: SQL completion did not overlap stall"
        saturated_after = saturated_outputs()
        assert not hangup.poll(0), "inconclusive: sockets closed during final saturation check"
        elapsed = time.monotonic() - started
        evidence = {"baseline_bytes": len(baseline), "baseline_records": frames[-1]["record_count"],
                    "stalled_clients": 2, "busy_refusals": busy, "committed_workers": 1, "overlap_seconds": elapsed,
                    "saturation_before": saturated_before, "saturation_after": saturated_after}
        (root / "isolation.json").write_text(json.dumps(evidence, indent=2))
        print("PASS committed SQL while stalled scan and two-client admission overload", json.dumps(evidence), flush=True)
        # Return both live attachments to the shutdown check; no new admission is needed.
        return clients
    except BaseException:
        if worker is not None and worker.poll() is None:
            worker.kill()
            worker.wait()
        for client in clients:
            client.close()
        raise

print("Evidence directory:", root, flush=True)
try:
    run("cubrid", "createdb", "--db-volume-size=20M", "--log-volume-size=20M", "-F", str(root), name, "en_US.utf8")
    start(False)
    assert not (root / "pgbuf-inspector").exists()
    print("PASS default-off server serves SQL without inspector directory", flush=True)
    stop()
    start(True)
    path, first = attach()
    assert path.stat().st_mode & 0o777 == 0o600
    assert path.parent.stat().st_mode & 0o777 == 0o700
    print("PASS enabled identity matches physical persistent volume", json.dumps(first), flush=True)
    if args.isolation:
        pending_clients = stalled_worker_check(path)
    else:
        pending = socket.socket(socket.AF_UNIX)
        pending.settimeout(1)
        pending.connect(str(path))
        pending.sendall(b'{"type":"client_hello","supported_majors":[1]}\n')
        assert pending.recv(65536)
        pending_clients = [pending]
    shutdown_started = time.monotonic()
    stop()
    assert time.monotonic() - shutdown_started < 5
    for pending in pending_clients:
        while pending.recv(65536):
            pass
        pending.close()
    assert not path.exists()
    start(True)
    _, second = attach()
    assert first["incarnation"] != second["incarnation"]
    assert first["volumes"] == second["volumes"]
    print("PASS restart changes incarnation and preserves persistent identity", flush=True)
    if args.isolation:
        with socket.socket(socket.AF_UNIX) as old:
            old.settimeout(.5)
            old.connect(str(path))
            old.sendall((json.dumps({"type": "client_hello", "supported_majors": [1],
                                    "expected_incarnation": first["incarnation"]}) + "\n").encode())
            assert json.loads(old.recv(4096)) == {"type": "error", "code": "incarnation-changed"}
        path.unlink()
        replacement = socket.socket(socket.AF_UNIX)
        replacement.bind(str(path))
        replacement_identity = path.stat()
        stop()
        assert path.stat().st_ino == replacement_identity.st_ino
        replacement.close()
        path.unlink()
        print("PASS shutdown preserves socket pathname replacement", flush=True)
    else:
        stop()
    copied_dir = root / "copied"
    copied_dir.mkdir()
    run("cubrid", "copydb", "-F", str(copied_dir), "-E", str(copied_dir), name, "inspector02copy")
    original_name = name
    name = "inspector02copy"
    start(True)
    _, copied = attach(copied_dir / name)
    assert first["volumes"] != copied["volumes"]
    assert first["volumes"][0]["inode"] != copied["volumes"][0]["inode"]
    print("PASS copied database has distinct physical identity", flush=True)
    stop()
    name = original_name
    path.write_text("preserve me")
    start(True)
    assert path.read_text() == "preserve me"
    log.flush()
    assert "PGBUF_INSPECTOR_UNAVAILABLE" in (root / "commands.log").read_text()
    print("PASS activation failure preserves conflicting entry and SQL service", flush=True)
    if args.isolation:
        path.unlink()
        for _ in range(3):
            query()
            assert not path.exists()
        assert (root / "commands.log").read_text().count("PGBUF_INSPECTOR_UNAVAILABLE") == 1
        print("PASS removed conflict does not trigger a background activation retry", flush=True)
    stop()
finally:
    run("cubrid", "server", "stop", name, check=False)
    run("cub_commdb", "-A", check=False)
    log.close()
