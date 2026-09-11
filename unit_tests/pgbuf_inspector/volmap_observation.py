# Copyright 2016 CUBRID Corporation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.

"""Opt-in actual Volmap HTTP checks for the independently held native fixture."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time
import urllib.error
import urllib.request


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class VolmapObservation:
    def __init__(self, configuration, fixture):
        self.config = json.loads(configuration.read_text())
        self.output = Path(self.config['output_directory']).resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.process = None
        self.log = None
        self.previous = None
        self.relay = None
        self.epoch = 0
        self.binary = Path(self.config['consumer_binary']).resolve(strict=True)
        source = Path(self.config['producer_source']).resolve(strict=True)
        consumer = Path(self.config['consumer_source']).resolve(strict=True)
        build = Path(self.config['producer_build']).resolve(strict=True)
        install = Path(self.config['producer_install']).resolve(strict=True)
        assert fixture == build / 'bin/pgbuf_inspector_fixture'
        assert source == Path(__file__).resolve().parents[2]
        assert self.config['format_profile'] == 'feat-oos', 'producer 06 requires the aligned format'
        port = self.config['listen_port']
        assert type(port) is int and 1 <= port <= 65535
        self.origin = f'http://127.0.0.1:{port}'
        with socket.socket() as check:
            check.bind(('127.0.0.1', port))
        corpus = source / 'docs/pgbuf-inspector/v1'
        vendored = consumer / 'fixtures/pgbuf-inspector/v1'
        files = {p.relative_to(corpus): digest(p) for p in corpus.rglob('*') if p.is_file()}
        assert files and files == {p.relative_to(vendored): digest(p) for p in vendored.rglob('*') if p.is_file()}
        metadata = dict(self.config)
        metadata['corpus'] = json.loads((corpus / 'manifest.json').read_text())
        metadata['corpus_files'] = len(files)
        metadata['build_type'] = [s for s in (build / 'CMakeCache.txt').read_text().splitlines()
                                  if s.startswith(('CMAKE_BUILD_TYPE:', 'CMAKE_HOME_DIRECTORY:', 'CMAKE_INSTALL_PREFIX:'))]
        # CMake rewrites RUNPATH on installation, changing bytes but not build ID.
        metadata['elf_build_ids'] = {}
        for built, installed in [(build / 'bin/cub_server', install / 'bin/cub_server'),
                                 (build / 'cubrid/libcubrid.so', install / 'lib/libcubrid.so')]:
            for path in (built, installed):
                notes = subprocess.check_output(['readelf', '-n', str(path)], text=True)
                ids = [line.split('Build ID: ', 1)[1].strip() for line in notes.splitlines() if 'Build ID: ' in line]
                assert len(ids) == 1
                metadata['elf_build_ids'][str(path)] = ids[0]
            assert metadata['elf_build_ids'][str(built)] == metadata['elf_build_ids'][str(installed)]
        metadata['sha256'] = {str(p): digest(p) for p in (
            fixture, self.binary, build / 'bin/cub_server', build / 'cubrid/libcubrid.so',
            install / 'bin/cub_server', install / 'lib/libcubrid.so',
            Path(__file__), source / 'unit_tests/pgbuf_inspector/controlled_observation.py',
            source / 'unit_tests/pgbuf_inspector/pgbuf_inspector_fixture.cpp')}
        for label, repo in [('producer', source), ('consumer', consumer)]:
            metadata[label + '_commit'] = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
            metadata[label + '_status'] = subprocess.check_output(['git', '-C', str(repo), 'status', '--short'], text=True)
            (self.output / (label + '-source.patch')).write_bytes(subprocess.check_output(['git', '-C', str(repo), 'diff', 'HEAD']))
        (self.output / 'inputs.json').write_text(json.dumps(metadata, indent=2) + '\n')
        os.environ.update(CUBRID=str(install), PATH=str(install / 'bin') + os.pathsep + os.environ['PATH'],
                          LD_LIBRARY_PATH=str(install / 'lib') + os.pathsep + str(install / 'cci/lib'))
        libraries = subprocess.check_output(['ldd', str(fixture)], text=True)
        assert str(install / 'lib/libcubrid.so') in libraries and 'not found' not in libraries
        (self.output / 'fixture-libraries.txt').write_text(libraries)

    def http(self, path, body=None):
        payload = None if body is None else json.dumps(body).encode()
        request = urllib.request.Request(self.origin + path, data=payload,
                                         headers={'Origin': self.origin, 'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=3) as response:
            raw = response.read(1048577)
            assert len(raw) <= 1048576
            if '/runtime/' in path:
                assert response.headers['Cache-Control'] == 'no-store'
            value = json.loads(raw)
            if '/runtime/' in path:
                self.sanitized(value)
                for private in [str(self.root), str(self.binary), str(self.path), 'pgbuf-fixture-private-page-bytes']:
                    assert private.encode() not in raw
            return value

    def sanitized(self, value):
        if isinstance(value, dict):
            assert not set(value).intersection({'pid', 'uid', 'gid', 'path', 'socket_path', 'raw_error', 'pointer', 'payload', 'inode', 'device'})
            for child in value.values():
                self.sanitized(child)
        elif isinstance(value, list):
            for child in value:
                self.sanitized(child)

    def start(self, root, name, path, target, env):
        self.root, self.path, self.target, self.name, self.env = root, path, target, name, env
        (self.output / 'dataset.json').write_text(json.dumps({'root': str(root), 'database': name,
            'target': target, 'buffer_pages': 4096, 'page_size': 16384, 'private_permanent_allocation': True}, indent=2))
        persistent = {int(line.split()[0]) for line in (root / (name + '_vinf')).read_text().splitlines()
                      if int(line.split()[0]) >= 0}
        assert target[0] in persistent, 'controlled VPID must belong to an inspected persistent volume'
        command = [str(self.binary), 'serve', '--vinf', str(root / (name + '_vinf')), '--volume-root', str(root),
                   '--format-profile', self.config['format_profile'], '--listen', self.origin.removeprefix('http://'),
                   '--no-follow', '--progress', 'never', '--runtime-page-buffer', '--runtime-socket', str(path)]
        self.log = (self.output / 'consumer.log').open('a')
        self.log.write(json.dumps(command) + '\n')
        self.log.flush()
        self.process = subprocess.Popen(command, cwd=root, env=env, stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 5
        while True:
            assert self.process.poll() is None, 'consumer exited; see consumer.log'
            try:
                self.disk = self.http('/api/v1/session')
                break
            except urllib.error.URLError:
                assert time.monotonic() < deadline, 'consumer startup deadline'
                time.sleep(.02)
        (self.output / ('disk-relay-before.json' if self.relay else 'disk-before.json')).write_text(json.dumps(self.disk, indent=2))

    def check(self, phase, state, dirty=None):
        self.epoch += 1
        body = {'pages': [{'volid': self.target[0], 'pageid': self.target[1]}],
                'epoch': str(self.epoch), 'generation': str(self.disk['snapshot']['generation']),
                'cadence_ms': 500, 'after_request': True}
        for attempt in range(4):
            batch = self.http('/api/v1/runtime/page-buffer/observe', body)
            (self.output / (phase + f'-attempt-{attempt}.json')).write_text(json.dumps(batch, indent=2))
            if batch['capability']['reason'] != 'rate-limited':
                break
            # A second independent wire observer also consumes the global floor.
            # Backoff is scheduling only; native fixes establish page state.
            time.sleep(.65)
        (self.output / (phase + '-http.json')).write_text(json.dumps(batch, indent=2))
        assert batch['capability']['verification'] == 'verified'
        assert batch['requested_count'] == batch['evaluated_count'] == 1
        assert batch['producer_complete'] is True, 'inconclusive: complete consumer capture required'
        assert batch['epoch'] == str(self.epoch) and batch['generation'] == str(self.disk['snapshot']['generation'])
        row, = batch['observations']
        assert (row['volid'], row['pageid']) == self.target and row['state'] == state
        capture = batch['capture']
        if self.previous:
            assert capture['incarnation_binding'] == self.previous['incarnation_binding']
            assert int(capture['sequence']) > int(self.previous['sequence'])
        self.previous = capture
        if dirty is not None:
            evidence = row['evidence']
            assert evidence['dirty'] is dirty and evidence['latch_mode'] == 'write' and evidence['fix_count'] == 1
            kind, index, zone = evidence['lru_list_kind'], evidence['lru_list_index'], evidence['lru_zone']
            if kind in ('shared', 'private'):
                assert zone in ('lru1', 'lru2', 'lru3') and 0 <= index < capture[kind + '_lru_count']
            else:
                assert kind in ('none', 'invalid') and index is None
        else:
            assert row['evidence'] is None
        assert self.http('/api/v1/session') == self.disk, 'runtime observation changed ordinary disk session'
        print('PASS actual Volmap HTTP', phase, self.target, state, flush=True)

    def partial_shared(self):
        from concurrent.futures import ThreadPoolExecutor
        import threading
        upstream = self.path
        self.stop_consumer()
        self.relay = DelayedStream(self.root, upstream, self.output)
        self.start(self.root, self.name, self.relay.path, self.target, self.env)
        barrier = threading.Barrier(8)

        def observe(epoch):
            body = {'pages': [{'volid': self.target[0], 'pageid': self.target[1]}],
                    'epoch': str(epoch), 'generation': str(self.disk['snapshot']['generation']),
                    'cadence_ms': 500, 'after_request': True}
            barrier.wait(timeout=2)
            return self.http('/api/v1/runtime/page-buffer/observe', body)

        with ThreadPoolExecutor(max_workers=8) as callers:
            batches = list(callers.map(observe, range(10, 18)))
        (self.output / 'partial-shared-http.json').write_text(json.dumps(batches, indent=2))
        for epoch, batch in zip(range(10, 18), batches):
            assert batch['epoch'] == str(epoch)
            assert batch['requested_count'] == batch['evaluated_count'] == 1
            assert batch['producer_complete'] is False, 'inconclusive: real partial coverage required'
            row, = batch['observations']
            assert row['state'] == 'unknown' and row['evidence'] is None
            assert (row['volid'], row['pageid']) == self.target
            assert batch['capture']['identity'] == batches[0]['capture']['identity']
        assert self.http('/api/v1/session') == self.disk
        self.stop_consumer()
        self.relay.close()
        assert len(self.relay.requests) == 1, 'tabs multiplied producer scans'
        frames = self.relay.frames
        assert frames[0]['type'] == 'scan_header' and frames[-1]['type'] == 'scan_footer'
        footer = frames[-1]
        assert footer['truncated'] is True
        assert footer['record_count'] == len(frames) - 2
        assert footer['record_count'] <= footer['visited_slots'] <= 65536
        assert all(f['incarnation'] == frames[0]['incarnation'] and f['scan_seq'] == frames[0]['scan_seq'] for f in frames)
        assert not any((f.get('volid'), f.get('pageid')) == self.target for f in frames[1:-1])
        assert batches[0]['capture']['sequence'] == footer['scan_seq']
        self.relay = None
        print('PASS actual Volmap partial omission: eight HTTP callers share one real producer scan, exact raw count', footer['record_count'], flush=True)

    def stop_consumer(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if self.log:
            self.log.close()

    def close(self, root):
        self.stop_consumer()
        if self.relay:
            self.relay.close()
        # Keep raw controls and captures, not the disposable database files.
        for path in root.iterdir():
            if path.suffix in ('.json', '.jsonl', '.log'):
                shutil.copyfile(path, self.output / path.name)


class DelayedStream:
    """Byte-preserving test relay: delay reads after headers, never fabricate frames.

    Direct attachment is checked separately. This relay supplies controlled
    transport backpressure, with the real producer still owning scan/coverage.
    """
    def __init__(self, root, upstream, output):
        import threading
        directory = root / 'relay'
        directory.mkdir(mode=0o700)
        self.path = directory / 'producer.sock'
        self.listener = socket.socket(socket.AF_UNIX)
        self.listener.bind(str(self.path))
        self.path.chmod(0o600)
        self.listener.listen(1)
        self.listener.settimeout(5)
        self.requests = []
        self.frames = []
        self.error = None
        self.output = output
        self.thread = threading.Thread(target=self.run, args=(upstream,), daemon=True)
        self.thread.start()

    def run(self, upstream):
        try:
            with self.listener.accept()[0] as client, socket.socket(socket.AF_UNIX) as producer:
                producer.settimeout(3)
                client.settimeout(3)
                producer.connect(str(upstream))
                with client.makefile('rb') as requests, producer.makefile('rb') as responses:
                    hello = requests.readline(65537)
                    assert hello.endswith(b'\n') and len(hello) <= 65536
                    producer.sendall(hello)
                    reply = responses.readline(65537)
                    assert reply.endswith(b'\n') and len(reply) <= 65536
                    client.sendall(reply)
                    while True:
                        request = requests.readline(4097)
                        if not request:
                            break
                        assert request.endswith(b'\n') and len(request) <= 4096
                        self.requests.append(json.loads(request))
                        producer.sendall(request)
                        raw = bytearray()
                        started = time.monotonic()
                        next_pause = 8192
                        while True:
                            if len(raw) >= next_pause and time.monotonic() < started + 1.5:
                                time.sleep(.12)
                                next_pause = len(raw) + 8192
                            line = responses.readline(4097)
                            assert line.endswith(b'\n') and len(line) <= 4096
                            raw.extend(line)
                            assert len(raw) <= 1073741824
                            frame = json.loads(line)
                            self.frames.append(frame)
                            client.sendall(line)
                            if frame['type'] == 'scan_header':
                                time.sleep(.15)
                            if frame['type'] in ('scan_footer', 'error'):
                                break
                        (self.output / f'relay-scan-{len(self.requests)}.jsonl').write_bytes(raw)
        except Exception as error:
            self.error = repr(error)
        finally:
            self.listener.close()
            (self.output / 'relay.json').write_text(json.dumps({'requests': self.requests, 'error': self.error}, indent=2))

    def close(self):
        self.thread.join(timeout=4)
        assert not self.thread.is_alive(), 'relay did not release resources'
        assert self.error is None, self.error
