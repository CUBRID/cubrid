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

"""Verify held native page states through the production inspector socket.

Requires the test-only pgbuf_inspector_fixture executable and the matching
installed CUBRID environment. Retains independent synchronization evidence.
"""
import argparse
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile
import time

if not __debug__:
    raise RuntimeError("This assertion-based testcase requires Python without optimization")

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--fixture", type=Path, required=True)
parser.add_argument("--volmap-run", type=Path, help="Explicit cross-repository integration input JSON")
args = parser.parse_args()
fixture = args.fixture.resolve(strict=True)
integration = None
if args.volmap_run:
    from volmap_observation import VolmapObservation
    integration = VolmapObservation(args.volmap_run, fixture)
root = Path(tempfile.mkdtemp(prefix="pgbuf-native-"))
env = os.environ.copy()
env.update(CUBRID_DATABASES=str(root), CUBRID_TMP=str(root), CUBRID_CONF_FILE=str(root / "cubrid.conf"))
(root / "databases.txt").touch()
(root / "cubrid.conf").write_text("[common]\ndata_buffer_size=64M\nlog_buffer_size=4M\n"
                                  "vacuum_log_block_pages=4\nenable_pgbuf_inspector=yes\n")
name = "pgbuffixture"
print("Evidence directory:", root, flush=True)
commands = (root / "commands.log").open("w")
native_log = (root / "native.log").open("wb")
child = None
pending = b""
evidence = []

def acknowledgement(command=None):
    global pending
    started = time.monotonic()
    if command:
        child.stdin.write(command.encode() + b"\n")
        child.stdin.flush()
    deadline = started + (25 if command == "evict" else 8)
    while True:
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            if line.startswith(b"PGFIXTURE "):
                reply = json.loads(line[len(b"PGFIXTURE "):])
                expected = {None: "clean-held", "held": "held", "dirty": "dirty-held", "populate": "populated",
                            "evict": "evicted", "absent": "absent"}
                assert reply["state"] == expected[command], "inconclusive: unexpected native acknowledgement"
                evidence.append({"command": command or "boot", "ack": reply,
                                 "monotonic": time.monotonic(), "elapsed": time.monotonic() - started})
                (root / "synchronization.json").write_text(json.dumps(evidence, indent=2))
                return reply
        remaining = deadline - time.monotonic()
        assert remaining > 0, "inconclusive: native fixture synchronization deadline"
        assert select.select([child.stdout], [], [], remaining)[0], "inconclusive: no native acknowledgement"
        chunk = os.read(child.stdout.fileno(), 65536)
        assert chunk, f"native fixture exited; see {root / 'native.log'}"
        native_log.write(chunk)
        native_log.flush()
        pending += chunk
        assert len(pending) <= 65536

class Observation:
    def __init__(self, path):
        self.client = socket.socket(socket.AF_UNIX)
        self.client.settimeout(.5)
        started = time.monotonic()
        self.client.connect(str(path))
        self.stream = self.client.makefile("rb")
        self.client.sendall(b'{"type":"client_hello","supported_majors":[1]}\n')
        self.hello, _ = self.frame(65536, started + .5)
        assert self.hello["type"] == "server_hello"
        self.sequence = 0

    def frame(self, limit, deadline):
        remaining = deadline - time.monotonic()
        assert remaining > 0, "exchange deadline"
        self.client.settimeout(remaining)
        line = self.stream.readline(limit + 1)
        assert line.endswith(b"\n") and len(line) <= limit, "broken or oversized frame"
        return json.loads(line), line

    def scan(self, label, pause_after_header=False):
        request = (json.dumps({"type": "scan_request", "incarnation": self.hello["incarnation"]}) + "\n").encode()
        for attempt in range(3):
            started = time.monotonic()
            self.client.sendall(request)
            header, line = self.frame(4096, started + 2)
            if header.get("code") != "rate-limited":
                break
            time.sleep(header["retry_after_ms"] / 1000 + .005)
        assert header["type"] == "scan_header"
        if pause_after_header:
            # This deliberately consumes traversal time under output pressure;
            # it is not an oracle for residency/dirty state. Those are held by
            # the native fixture across the exchange and acknowledged separately.
            time.sleep(.15)
        sequence = int(header["scan_seq"])
        assert sequence > self.sequence
        frames = [header]
        raw = bytearray(line)
        while True:
            frame, line = self.frame(4096, started + 2)
            raw.extend(line)
            assert len(raw) <= 67108864
            assert frame["incarnation"] == self.hello["incarnation"]
            assert frame["scan_seq"] == header["scan_seq"]
            frames.append(frame)
            if frame["type"] == "scan_footer":
                break
            assert frame["type"] == "page"
            assert len(frames) <= 65537
        assert header["incarnation"] == self.hello["incarnation"]
        footer = frames[-1]
        assert type(footer["record_count"]) is int and footer["record_count"] == len(frames) - 2
        assert type(footer["visited_slots"]) is int and footer["record_count"] <= footer["visited_slots"] <= 65536
        assert type(footer["truncated"]) is bool
        assert b"pgbuf-fixture-private-page-bytes" not in raw
        (root / (label + ".jsonl")).write_bytes(raw)
        self.sequence = sequence
        return frames[1:-1], footer

    def close(self):
        self.stream.close()
        self.client.close()

observer = None
try:
    subprocess.run(["cubrid", "createdb", "--db-volume-size=20M", "--log-volume-size=20M", "-F", str(root), name,
                    "en_US.utf8"], cwd=root, env=env, stdout=commands, stderr=subprocess.STDOUT, timeout=60, check=True)
    if integration:
        assert not (root / "pgbuf-inspector").exists(), "non-server utility created an endpoint"
        print("PASS non-server createdb exposes no endpoint with inspector enabled", flush=True)
    child = subprocess.Popen([str(fixture), name] + (["--permanent"] if integration else []), cwd=root, env=env,
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=native_log)
    ready = acknowledgement()
    assert ready["state"] == "clean-held"
    target = ready["volid"], ready["pageid"]
    paths = list((root / "pgbuf-inspector").glob("*.sock"))
    assert len(paths) == 1
    if integration:
        integration.start(root, name, paths[0], target, env)
    observer = Observation(paths[0])
    (root / "handshake.json").write_text(json.dumps(observer.hello, indent=2))
    for phase, dirty in [("clean", False), ("dirty", True)]:
        before = acknowledgement("dirty" if dirty else "held")
        pages, footer = observer.scan(phase)
        after = acknowledgement("held")
        assert (before["volid"], before["pageid"]) == target == (after["volid"], after["pageid"])
        assert not footer["truncated"], "inconclusive: complete traversal required"
        matches = [page for page in pages if (page["volid"], page["pageid"]) == target]
        assert len(matches) == 1
        page = matches[0]
        assert page["dirty"] is dirty and page["latch_mode"] == "write" and page["fix_count"] == 1
        assert "page_lsa" not in page and "oldest_unflush_lsa" not in page and "page_kind" not in page
        if integration:
            integration.check(phase, "resident", dirty)
            acknowledgement("held")
        print("PASS", phase, "held native VPID", target, "complete records", len(pages), flush=True)
    assert acknowledgement("populate")["state"] == "populated"
    assert acknowledgement("held")["state"] == "held"
    pages, footer = observer.scan("partial", pause_after_header=True)
    assert footer["truncated"], "inconclusive: fixture failed to establish a partial scan"
    assert acknowledgement("held")["state"] == "held"
    print("PASS partial scan with independently held native page", target, "records", len(pages), flush=True)
    removed = acknowledgement("evict")
    assert (removed["volid"], removed["pageid"]) == target
    for label, partial in [("evicted-complete", False), ("evicted-partial", True)]:
        if partial:
            acknowledgement("populate")
        before = acknowledgement("absent")
        pages, footer = observer.scan(label, pause_after_header=partial)
        after = acknowledgement("absent")
        assert (before["volid"], before["pageid"]) == target == (after["volid"], after["pageid"])
        assert footer["truncated"] is partial, "inconclusive: required scan coverage not established"
        assert not any((page["volid"], page["pageid"]) == target for page in pages)
        # The independently proven native state does not upgrade partial wire
        # coverage: an observer can infer nonresidency only from a complete scan.
        conclusion = "unknown" if footer["truncated"] else "observed-nonresident"
        if integration:
            if partial:
                integration.partial_shared()
            else:
                integration.check(label, "not-resident")
            acknowledgement("absent")
        (root / (label + "-conclusion.json")).write_text(json.dumps({"target": target, "state": conclusion}))
        print("PASS", label, target, conclusion, "records", len(pages), flush=True)
    observer.close()
    observer = None
    child.stdin.write(b"stop\n")
    child.stdin.flush()
    assert child.wait(timeout=10) == 0
    assert not paths[0].exists()
    print("PASS native fixture shutdown", flush=True)
finally:
    if observer:
        observer.close()
    if child is not None and child.poll() is None:
        try:
            child.stdin.write(b"stop\n")
            child.stdin.flush()
            child.wait(timeout=10)
        except (BrokenPipeError, subprocess.TimeoutExpired):
            child.kill()
            child.wait()
    commands.close()
    native_log.close()

    if integration:
        integration.close(root)
