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
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

root = Path(tempfile.mkdtemp(prefix="pgbuf-server-"))
env = os.environ.copy()
env.update(CUBRID_DATABASES=str(root), CUBRID_TMP=str(root), CUBRID_CONF_FILE=str(root / "cubrid.conf"))
with socket.socket() as reservation:
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]
name = "inspector02"
base = f"[common]\ncubrid_port_id={port}\ndata_buffer_size=64M\nlog_buffer_size=4M\nvacuum_log_block_pages=4\n"
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
        return paths[0], hello

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
    pending = socket.socket(socket.AF_UNIX)
    pending.settimeout(1)
    pending.connect(str(path))
    pending.sendall(b'{"type":"client_hello","supported_majors":[1]}\n')
    assert pending.recv(65536)
    stop()
    assert pending.recv(65536) == b""
    pending.close()
    assert not path.exists()
    start(True)
    _, second = attach()
    assert first["incarnation"] != second["incarnation"]
    assert first["volumes"] == second["volumes"]
    print("PASS restart changes incarnation and preserves persistent identity", flush=True)
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
    stop()
finally:
    run("cubrid", "server", "stop", name, check=False)
    run("cub_commdb", "-A", check=False)
    log.close()
