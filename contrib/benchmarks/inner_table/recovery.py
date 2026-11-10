"""Case recovery of the Inner Table acceptance driver (accept.py --case recovery).

Each scenario uses a new disposable database with an ordinary oracle table and an opted-in table. A statement runs
on the opted-in table and gdb (non-stop) stops the server thread at one point of the Inner Table write path. Then
this environment's cub_master and cub_server get SIGKILL. When the statement did not commit, only the opted-in table
ran it; when it committed, the oracle ran it too. The server restarts twice. After each restart, canonical results,
an index-only answer and the chunk rows must match the oracle, and SA checkdb must pass at the end.

Processes are selected by /proc/PID/exe under this installation, never by name alone.
"""

import os
import re
import signal
import subprocess
import time
from pathlib import Path

import accept

SUFFIX = accept.CHUNK_TABLE_SUFFIX
CANON = ('select id, n, octet_length(v) as lv, sha2(v, 256) as hv, (v is null) as null_v from {t} order by id;')
INDEX = 'select id from {t} where id > 0 using index pk_{t}_id;'


def inner_line(wt, needle):
    """Source line of the first occurrence of NEEDLE in inner_table.cpp, for a breakpoint."""
    for i, line in enumerate((wt / 'src/storage/inner_table.cpp').read_text().splitlines(), 1):
        if needle in line:
            return f'inner_table.cpp:{i}'
    raise accept.HarnessError(f'cannot locate {needle!r} in inner_table.cpp')


class Gdb:
    """gdb/MI in non-stop mode, fed through a FIFO so that the Popen PID is gdb's own."""

    def __init__(self, root, pid, breaks):
        self.out = root / 'gdb.log'
        fifo = root / 'gdb.in'
        if fifo.exists():
            fifo.unlink()
        os.mkfifo(fifo)
        self.proc = subprocess.Popen(['bash', '-c', f'exec gdb -q -nx --interpreter=mi3 <"{fifo}" >"{self.out}" 2>&1'],
                                     stdin=subprocess.DEVNULL, start_new_session=True)
        self.w = open(fifo, 'w')
        self.send('-gdb-set mi-async on', '-gdb-set non-stop on', '-gdb-set pagination off',
                  '-interpreter-exec console "handle SIGPIPE SIGUSR1 SIGUSR2 SIGALRM nostop noprint pass"',
                  f'-target-attach {pid}', *[f'-break-insert -d {b}' for b in breaks])
        time.sleep(5)  # continue only after the attach stops have arrived, or threads stay stopped
        self.send('-exec-continue --all')
        time.sleep(3)

    def send(self, *lines):
        self.w.write(''.join(line + '\n' for line in lines))
        self.w.flush()

    def enable(self):
        """Breakpoints start disabled, so that the probe statements before the scenario do not stop."""
        self.send('-break-enable')

    def hit(self):
        return '*stopped,reason="breakpoint-hit"' in self.out.read_text(errors='replace')

    def kill(self):
        try:
            self.w.close()
        except OSError:
            pass
        try:
            os.kill(self.proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.proc.wait()


def server_pid(name):
    for p in accept.own_pids('cub_server'):
        try:
            if name in Path(f'/proc/{p}/cmdline').read_bytes().split(b'\0')[1].decode():
                return p
        except (OSError, IndexError):
            pass
    return None


def kill_runtime(name):
    """SIGKILL this environment's cub_master first, so that it does not restart the server, then the server."""
    for p in accept.own_pids('cub_master'):
        os.kill(p, signal.SIGKILL)
    p = server_pid(name)
    if p:
        os.kill(p, signal.SIGKILL)
    time.sleep(2)


class Scenario:
    def __init__(self, case, title, stmt, brk=None, committed=False, vacuum=False, control=False):
        self.case, self.title, self.stmt, self.brk = case, title, stmt, brk
        self.control = control
        self.committed, self.vacuum = committed, vacuum
        self.gdb, self.name = None, None

    def cleanup(self):
        """After a harness error: release a stopped server and its gdb."""
        if self.name:
            kill_runtime(self.name)
        if self.gdb:
            self.gdb.kill()

    def run(self, root, conf, conf_text, hexs):
        case = self.case
        name = self.name = 'itrec' + str(int(time.time() * 1000) % 10 ** 10)
        sroot = root / name
        sroot.mkdir()
        log = sroot / 'harness.log'
        db = accept.Db(sroot, name)
        accept.sh(['cub-workenv', 'create-db', name], log)
        conf.write_text(conf_text + f'\n[@{name}]\ninner_table_threshold={accept.THRESHOLD}\n')
        accept.sh(['cubrid', 'server', 'start', name], log, timeout=300)
        setup = ''
        for t in ('r_oracle', 'r_inner'):
            setup += f'create table {t} (id int primary key, n int, v varchar);\n'
        setup += f'create table r_inner{SUFFIX} (ck bigint primary key, chunk bit varying);\n'
        setup += 'create table f (id int, v varchar(1000));\n'
        base = [(1, hexs(5000)), (2, hexs(9000)), (3, hexs(20000)), (4, "'small'")]
        for t in ('r_oracle', 'r_inner'):
            setup += ''.join(f'insert into {t} values ({i}, 0, {v});\n' for i, v in base)
        rc, text = db.sql(setup)
        if rc or accept.errors(text):
            raise accept.HarnessError(f'{name} setup failed: {accept.errors(text)[:3]}')

        label = self.title
        stmt = self.stmt(hexs)
        gdb = None
        if self.committed:
            for t in ('r_oracle', 'r_inner'):
                rc, text = db.sql(stmt.format(t=t), autocommit=False)
                if rc or accept.errors(text):
                    raise accept.HarnessError(f'{label}: statement failed on {t}: {accept.errors(text)[:2]}')
        if self.control:
            # Negative control: the oracle commits what the opted-in table never commits.
            rc, text = db.sql(stmt.format(t='r_oracle'))
            if rc or accept.errors(text):
                raise accept.HarnessError(f'{label}: oracle statement failed')
        if self.brk or self.vacuum:
            pid = server_pid(name)
            if pid is None:
                raise accept.HarnessError(f'{label}: no own cub_server for {name}')
            gdb = self.gdb = Gdb(sroot, pid, [self.brk or 'vacuum_heap_record'])
            rc, _ = db.sql('select 1 from db_root;')
            if rc or gdb.hit():
                raise accept.HarnessError(f'{label}: server unresponsive under gdb, or a breakpoint hit too early')
            gdb.enable()
            time.sleep(1)
        bg = None
        if not self.committed:
            script = sroot / 'stmt.sql'
            script.write_text(stmt.format(t='r_inner'))
            bg = subprocess.Popen(['csql', '-u', 'dba', '--no-auto-commit', '-i', str(script), name],
                                  stdin=subprocess.DEVNULL, stdout=open(sroot / 'stmt.out', 'w'),
                                  stderr=subprocess.STDOUT)
        if self.vacuum:
            for _ in range(40):
                if gdb.hit():
                    break
                db.sql('insert into f select rownum, repeat(\'f\', 900) from db_class c1, db_class c2 limit 2000;')
        if gdb:
            for _ in range(60):
                if gdb.hit():
                    break
                time.sleep(1)
            if not gdb.hit():
                gdb.kill()
                kill_runtime(name)
                case.record(f'{label}: the server stopped at {self.brk or "vacuum_heap_record"}', 'NOT VERIFIED',
                            'breakpoint not hit')
                return
        kill_runtime(name)
        if gdb:
            gdb.kill()
        if bg:
            bg.wait(timeout=60)

        for n in (1, 2):
            if accept.sh(['cubrid', 'server', 'start', name], log, timeout=300, check=False):
                case.record(f'{label}: restart {n}', 'FAILED', f'see {log}')
                accept.sh(['cubrid', 'service', 'stop'], log, check=False)
                return
            if self.control:
                mark = len(case.results)
                self.compare(db, f'{label}: restart {n}', quiet=True)
                found = any(r['verdict'] == 'FAILED' and 'equal results' in r['predicate']
                            for r in case.results[mark:])
                del case.results[mark:]
                case.record(f'{label}: restart {n}: the difference is detected', 'VERIFIED' if found else 'FAILED')
            else:
                self.compare(db, f'{label}: restart {n}')
            accept.sh(['cubrid', 'server', 'stop', name], log, timeout=300, check=False)
        ck = accept.sh(['cubrid', 'checkdb', '-S', name], sroot / 'checkdb.log', check=False)
        case.record(f'{label}: checkdb', 'VERIFIED' if ck == 0 else 'FAILED', f'exit {ck}')

    def compare(self, db, step, quiet=False):
        case = self.case
        if quiet:
            class Quiet:
                results = case.results

                def record(self, name, verdict, detail=''):
                    self.results.append({'predicate': name, 'verdict': verdict, 'detail': detail})
            case = Quiet()
        for what, q in (('equal results', CANON), ('equal index answers', INDEX)):
            got = {}
            for t in ('r_oracle', 'r_inner'):
                rc, text = db.sql(q.format(t=t))
                got[t] = None if rc or accept.errors(text) else accept.rows(text)
            ok = got['r_oracle'] is not None and got['r_oracle'] == got['r_inner'] and got['r_oracle'] != []
            case.record(f'{step}: {what}', 'VERIFIED' if ok else 'FAILED',
                        '' if ok else ('query failed' if got['r_inner'] is None else 'results differ'))
        rc, text = db.sql(f'select (select count(*) from r_inner where octet_length(v) >= {accept.THRESHOLD}) as n, '
                          f'(select count(distinct ck div 1048576) from r_inner{SUFFIX}) as c from db_root;')
        r = accept.rows(text)[0] if not rc and accept.rows(text) else None
        ok = r is not None and r['n'] == r['c']
        case.record(f'{step}: chunk rows match large values', 'VERIFIED' if ok else 'FAILED',
                    f'{r["c"]} chunk values for {r["n"]} large values' if r else 'query failed')


class RecoveryCase:
    def __init__(self, root, wt):
        self.root, self.wt = root, wt
        self.results = []

    def record(self, name, verdict, detail=''):
        self.results.append({'predicate': name, 'verdict': verdict, 'detail': detail})
        print(f'  {verdict:13} {name}{": " + detail if detail else ""}', flush=True)

    def run(self, conf, conf_text):
        rnd = __import__('random').Random(accept.SEED + 3)

        def hexs(n):
            return "'" + ''.join(rnd.choice('0123456789abcdef') for _ in range(n)) + "'"

        after_chunks = inner_line(self.wt, "refs_bytes[i].assign (INNER_REF_SIZE, '\\0');")
        ins = lambda h: f'insert into {{t}} values (10, 0, {h(9000)});\n'
        upd = lambda h: f'update {{t}} set v = {h(9000)} where id = 1;\n'
        dele = lambda h: 'delete from {t} where id = 2;\n'
        sp = lambda h: (f'savepoint s1;\nupdate {{t}} set v = {h(6000)} where id = 3;\n'
                        f'update {{t}} set v = {h(7000)} where id = 3;\nrollback to savepoint s1;\n'
                        'update {t} set n = n + 1;\n')
        scenarios = [
            Scenario(self, 'insert stopped after chunk creation', ins, after_chunks),
            Scenario(self, 'insert stopped during index changes', ins, 'locator_add_or_remove_index'),
            Scenario(self, 'update stopped after parent publication', upd, 'inner_table_retire_refs'),
            Scenario(self, 'update stopped after old-chunk deletion, before commit', upd, 'log_commit_local'),
            Scenario(self, 'delete stopped before commit', dele, 'log_commit_local'),
            Scenario(self, 'update killed after commit', lambda h: upd(h) + 'commit;\n', committed=True),
            Scenario(self, 'savepoint rollback of two overwrites and a hot update, killed after commit',
                     lambda h: sp(h) + 'commit;\n', committed=True),
            Scenario(self, 'control: oracle commits an update that the opted-in table does not', upd,
                     'log_commit_local', control=True),
            Scenario(self, 'update committed, killed during vacuum', lambda h: upd(h) + 'delete from {t} where id = 2;\n'
                     'commit;\n', committed=True, vacuum=True),
        ]
        for s in scenarios:
            try:
                s.run(self.root, conf, conf_text, hexs)
            except (accept.HarnessError, subprocess.TimeoutExpired) as e:
                self.record(f'{s.title}: harness', 'NOT VERIFIED', str(e)[:200])
                s.cleanup()
            finally:
                conf.write_text(conf_text)
        return self.results
