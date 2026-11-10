#!/usr/bin/env python3
"""Acceptance driver for the Inner Table POC.

Usage, from a built and cub-workenv-initialized worktree:

    python3 contrib/benchmarks/inner_table/accept.py --case storage-1 --root OUTDIR
    python3 contrib/benchmarks/inner_table/accept.py --case storage-2 --root OUTDIR
    python3 contrib/benchmarks/inner_table/accept.py --case recovery --root OUTDIR   (needs gdb; see recovery.py)

Case storage-2 adds VARBIT values, two cold columns in one row, binary zeros, a parent row
that stays REC_BIGONE without its cold value, and a short-chunk control.

Case storage-1 runs the same operations on an ordinary oracle table and on an
opted-in table, and compares canonical query results after every operation.
It also checks that the large values of the opted-in table are stored as chunk
rows of its companion class, and that a missing chunk is an error.

Exit 0: every predicate VERIFIED. Exit 1: at least one predicate failed.
Exit 2: missing evidence, setup or harness error.
Every run creates a new disposable database. It never touches another one.
"""

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

THRESHOLD = 2048
CHUNK_TABLE_SUFFIX = '__inner'
SEED = 20261110


class HarnessError(Exception):
    pass


def sh(cmd, out, timeout=600, check=True, **kw):
    """Run a command with stdin from /dev/null and output to a file."""
    with open(out, 'a') as f:
        f.write('$ ' + ' '.join(cmd) + '\n')
        f.flush()
        r = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, timeout=timeout, **kw)
    if check and r.returncode:
        raise HarnessError(f'{cmd[0]} {cmd[1] if len(cmd) > 1 else ""} failed with {r.returncode}; see {out}')
    return r.returncode


class Db:
    def __init__(self, root, name):
        self.root, self.name = root, name
        self.n = 0

    def sql(self, text, autocommit=True):
        """Run SQL in one csql session and return (exit code, output)."""
        self.n += 1
        script = self.root / f'sql-{self.n:03d}.sql'
        out = self.root / f'sql-{self.n:03d}.out'
        script.write_text(text)
        cmd = ['csql', '-u', 'dba', '-l', '-i', str(script)]
        if not autocommit:
            cmd.append('--no-auto-commit')
        cmd.append(self.name)
        with open(out, 'w') as f:
            r = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, timeout=600)
        return r.returncode, out.read_text()


def rows(output):
    """Parse csql -l output into a list of {column: value} rows."""
    result, cur = [], None
    for line in output.splitlines():
        m = re.match(r'<\d+>(.*)', line)
        if m:
            cur = {}
            result.append(cur)
            line = m.group(1)
        elif not line.startswith(' '):
            cur = None
        if cur is not None and ':' in line:
            k, v = line.split(':', 1)
            cur[k.strip()] = v.strip()
    return result


def errors(output):
    return [line for line in output.splitlines() if re.search(r'ERROR|In line \d+', line)]


def make_values():
    """Deterministic values that cover the storage-1 boundaries."""
    rnd = random.Random(SEED)

    def hexs(n):
        return ''.join(rnd.choice('0123456789abcdef') for _ in range(n))

    def multibyte(nbytes):
        s, b = '', 0
        for ch in '가나다라마바사아자차카타파하' * (nbytes // 3 + 1):
            if b + 3 > nbytes:
                break
            s, b = s + ch, b + 3
        return s

    t = THRESHOLD
    return [
        ('null', None),
        ('empty', ''),
        ('one', 'x'),
        ('below', hexs(t - 1)),
        ('at', hexs(t)),
        ('above', hexs(t + 1)),
        ('multibyte', multibyte(3 * t)),
        ('compressible', 'ab' * 8192),
        ('k64', hexs(64 * 1024)),
        ('m1', hexs(1024 * 1024)),
    ]


def lit(v):
    return 'NULL' if v is None else "'" + v.replace("'", "''") + "'"


CANON = ('select id, k, octet_length(v) as olen, char_length(v) as clen, '
         'sha2(v, 256) as h, (v is null) as isnull from {t} order by id;')


class Case:
    def __init__(self, db, inner_supported):
        self.db, self.inner = db, inner_supported
        self.results = []
        self.values = make_values()

    def record(self, name, verdict, detail=''):
        self.results.append({'predicate': name, 'verdict': verdict, 'detail': detail})
        if name.startswith('control: tampered oracle:'):
            return  # the control's own verdict is printed after it is judged
        print(f'  {verdict:13} {name}{": " + detail if detail else ""}', flush=True)

    def both(self, template, autocommit=True):
        out = {}
        for t in ('t_oracle', 't_inner'):
            rc, text = self.db.sql(template.format(t=t), autocommit)
            out[t] = (rc, text)
        return out

    def compare(self, step, tamper=False):
        got = {}
        for t in ('t_oracle', 't_inner'):
            rc, text = self.db.sql(CANON.format(t=t))
            if rc or errors(text):
                self.record(f'{step}: equal results', 'FAILED', f'{t} query failed: {errors(text)[:2]}')
                return False
            got[t] = rows(text)
        if tamper and got['t_oracle']:
            got['t_oracle'][0]['h'] = 'tampered'
        if not got['t_oracle'] and step != 'after delete all':
            self.record(f'{step}: equal results', 'NOT VERIFIED', 'oracle returned no rows')
            return False
        ok = got['t_oracle'] == got['t_inner']
        self.record(f'{step}: equal results', 'VERIFIED' if ok else 'FAILED',
                    '' if ok else f'{len(got["t_oracle"])} oracle rows vs {len(got["t_inner"])} inner rows differ')
        return ok

    def check_chunks(self, step):
        """Visible chunk rows must match the large values of t_inner exactly."""
        rc, text = self.db.sql(
            f'select count(distinct ck div 1048576) as nvalues, nvl(sum(octet_length(chunk)), 0) as nbytes '
            f'from t_inner{CHUNK_TABLE_SUFFIX};')
        rc2, text2 = self.db.sql(
            f'select count(*) as nvalues, nvl(sum(octet_length(v)), 0) as nbytes from t_inner '
            f"where octet_length(v) >= {THRESHOLD} and k <> 'compressible';")
        if rc or rc2 or errors(text) or errors(text2):
            self.record(f'{step}: chunk rows match large values', 'FAILED', 'query failed')
            return
        chunks, large = rows(text)[0], rows(text2)[0]
        n_large = int(large['nvalues'])
        ok = int(chunks['nvalues']) == n_large
        detail = f'{chunks["nvalues"]} chunk values for {n_large} large values'
        self.record(f'{step}: chunk rows match large values', 'VERIFIED' if ok else 'FAILED', detail)

    def run(self):
        db = self.db
        setup = (f'create table t_oracle (id int primary key, k varchar(20), n int, v varchar);\n'
                 f'create table t_inner (id int primary key, k varchar(20), n int, v varchar);\n'
                 f'create table t_inner{CHUNK_TABLE_SUFFIX} (ck bigint primary key, chunk bit varying);\n')
        rc, text = db.sql(setup)
        if rc or errors(text):
            raise HarnessError(f'setup failed: {errors(text)[:3]}')

        inserts = ''.join(f'insert into {{t}} values ({i}, {lit(k)}, 0, {lit(v)});\n'
                          for i, (k, v) in enumerate(self.values, 1))
        self.both(inserts)
        self.compare('after insert')
        self.check_chunks('after insert')

        self.both('insert into {t} values (100, \'rb\', 0, ' + lit(self.values[-2][1]) + ');\nrollback;\n',
                  autocommit=False)
        self.compare('after rolled-back insert')
        self.check_chunks('after rolled-back insert')

        self.both('update {t} set n = n + 1;')
        self.compare('after update of another column')
        self.check_chunks('after update of another column')

        self.both('update {t} set v = v || \'z\' where v is not null;\nrollback;\n', autocommit=False)
        self.compare('after rolled-back value update')

        self.both('update {t} set v = v || \'z\' where v is not null;')
        self.compare('after value update')
        self.check_chunks('after value update')

        self.both("update {t} set v = 'short' where k in ('at', 'k64');")
        self.compare('after update to short value')
        self.check_chunks('after update to short value')

        self.both('update {t} set v = ' + lit(self.values[4][1]) + " where k = 'one';")
        self.compare('after update from short to large')
        self.check_chunks('after update from short to large')

        self.both("delete from {t} where k in ('above', 'm1');\nrollback;\n", autocommit=False)
        self.compare('after rolled-back delete')

        self.both("delete from {t} where k in ('above', 'm1');")
        self.compare('after delete')
        self.check_chunks('after delete')

        self.both('savepoint s1;\nupdate {t} set v = v || \'s\' where v is not null;\n'
                  'update {t} set v = v || \'t\' where v is not null;\nrollback to savepoint s1;\n'
                  'update {t} set n = n + 10;\ncommit;\n', autocommit=False)
        self.compare('after savepoint rollback of two overwrites')
        self.check_chunks('after savepoint rollback of two overwrites')

        # Negative control 1: the comparison must detect a wrong value.
        before = len(self.results)
        self.compare('control: tampered oracle', tamper=True)
        last = self.results[before]
        self.results[before] = {'predicate': 'control: tampered oracle is detected',
                                'verdict': 'VERIFIED' if last['verdict'] == 'FAILED' else 'FAILED',
                                'detail': last['detail']}
        print(f'  {self.results[before]["verdict"]:13} control: tampered oracle is detected', flush=True)

        # Negative control 2: a missing chunk must be an error, not NULL or a partial value.
        rc, text = db.sql(f'select count(*) as c from t_inner{CHUNK_TABLE_SUFFIX};')
        if rc or not rows(text) or int(rows(text)[0]['c']) == 0:
            self.record('control: missing chunk is an error', 'NOT VERIFIED', 'no chunk rows to remove')
        else:
            c = f't_inner{CHUNK_TABLE_SUFFIX}'
            db.sql(f'create table saved_chunk as select * from {c} where ck = (select min(ck) from {c});\n'
                   f'delete from {c} where ck = (select ck from saved_chunk);')
            rc, text = db.sql('select id, sha2(v, 256) as h from t_inner order by id;')
            ok = rc != 0 or bool(errors(text))
            self.record('control: missing chunk is an error', 'VERIFIED' if ok else 'FAILED',
                        errors(text)[0][:120] if errors(text) else 'query returned rows')
            # Put the chunk back so that the final comparison and checkdb see a consistent database.
            db.sql(f'insert into {c} select * from saved_chunk;\ndrop table saved_chunk;')
            self.compare('after the chunk is restored')
        return self.results


def bits(n, rnd, zero=False):
    """A BIT VARYING literal of n bytes."""
    return "X'" + ('00' * n if zero else ''.join(f'{rnd.randrange(256):02x}' for _ in range(n))) + "'"


class Case2(Case):
    """storage-2: VARBIT, two cold columns, binary zeros, residual REC_BIGONE, malformed chunk sets."""

    PAIRS = (('s_oracle', 's_inner'), ('g_oracle', 'g_inner'))
    CANON_S = ('select id, k, n, octet_length(a) as la, sha2(a, 256) as ha, bit_length(b) as lb, '
               'sha2(hex(b), 256) as hb, (a is null) as null_a, (b is null) as null_b from {t} order by id;')
    CANON_G = ('select id, ' + ', '.join(f'sha2(c{i}, 256) as h{i}' for i in range(1, 11))
               + ', octet_length(a) as la, sha2(a, 256) as ha from {t} order by id;')
    LARGE = 2100  # far enough above the threshold that the disk header does not matter

    def pair(self, template, which=0, autocommit=True):
        for t in self.PAIRS[which]:
            self.db.sql(template.format(t=t), autocommit)

    def compare2(self, step, which=0, tamper=False):
        canon = (self.CANON_S, self.CANON_G)[which]
        o, i = self.PAIRS[which]
        got = {}
        for t in (o, i):
            rc, text = self.db.sql(canon.format(t=t))
            if rc or errors(text):
                self.record(f'{step}: equal results', 'FAILED', f'{t} query failed: {errors(text)[:2]}')
                return False
            got[t] = rows(text)
        if tamper and got[o]:
            got[o][0]['id'] = '-1'
        if not got[o]:
            self.record(f'{step}: equal results', 'NOT VERIFIED', 'oracle returned no rows')
            return False
        ok = got[o] == got[i]
        self.record(f'{step}: equal results', 'VERIFIED' if ok else 'FAILED',
                    '' if ok else f'{len(got[o])} oracle rows vs {len(got[i])} inner rows differ')
        return ok

    def keys(self, table):
        rc, text = self.db.sql(f'select ck from {table}{CHUNK_TABLE_SUFFIX} order by ck;')
        return None if rc or errors(text) else [r['ck'] for r in rows(text)]

    def check_chunks2(self, step, which=0):
        t = self.PAIRS[which][1]
        cols = ('a', 'b') if which == 0 else ('a',)
        expect_sql = ' + '.join(f'(select count(*) from {t} where octet_length({c}) >= {self.LARGE})' for c in cols)
        rc, text = self.db.sql(f'select {expect_sql} as n, (select count(distinct ck div 1048576) '
                               f'from {t}{CHUNK_TABLE_SUFFIX}) as c from db_root;')
        if rc or errors(text) or not rows(text):
            self.record(f'{step}: chunk rows match large values', 'FAILED', 'query failed')
            return
        r = rows(text)[0]
        ok = r['n'] == r['c']
        self.record(f'{step}: chunk rows match large values', 'VERIFIED' if ok else 'FAILED',
                    f'{r["c"]} chunk values for {r["n"]} large values')

    def run(self):
        db, rnd = self.db, random.Random(SEED + 2)
        hexs = lambda n: "'" + ''.join(rnd.choice('0123456789abcdef') for _ in range(n)) + "'"
        setup = ''
        for t in ('s_oracle', 's_inner'):
            setup += f'create table {t} (id int primary key, k varchar(20), n int, a varchar, b bit varying);\n'
        for t in ('g_oracle', 'g_inner'):
            cols = ', '.join(f'c{i} varchar' for i in range(1, 11))
            setup += f'create table {t} (id int primary key, {cols}, a varchar);\n'
        for t in ('s_inner', 'g_inner'):
            setup += f'create table {t}{CHUNK_TABLE_SUFFIX} (ck bigint primary key, chunk bit varying);\n'
        rc, text = db.sql(setup)
        if rc or errors(text):
            raise HarnessError(f'setup failed: {errors(text)[:3]}')

        rows_s = [
            (1, 'two-cold', hexs(5000), bits(5000, rnd)),
            (2, 'bits-only', 'NULL', bits(70000, rnd)),
            (3, 'chars-only', hexs(70000), 'NULL'),
            (4, 'zeros', "'small'", bits(4096, rnd, zero=True)),
            (5, 'inline', hexs(500), bits(500, rnd)),
        ]
        self.pair(''.join(f'insert into {{t}} values ({i}, \'{k}\', 0, {a}, {b});\n' for i, k, a, b in rows_s))
        self.compare2('varbit: after insert')
        self.check_chunks2('varbit: after insert')

        before = self.keys('s_inner')
        self.pair('update {t} set n = n + 1;')
        self.compare2('varbit: after update of another column')
        after = self.keys('s_inner')
        self.record('varbit: unchanged columns keep their chunks', 'VERIFIED' if before and before == after else
                    'FAILED', f'{len(before or [])} chunk keys before, {len(after or [])} after')

        self.pair("update {t} set a = a || 'q' where id = 1;\nrollback;\n", autocommit=False)
        self.compare2('varbit: after rolled-back update of one cold column')
        self.pair("update {t} set a = a || 'q' where id = 1;")
        self.compare2('varbit: after update of one of two cold columns')
        self.check_chunks2('varbit: after update of one of two cold columns')
        newer = self.keys('s_inner')
        # Row 1 has one-chunk values in a and b. Only a's chunk may be retired and written again.
        gone = len(set(after or []) - set(newer or []))
        added = len(set(newer or []) - set(after or []))
        b_kept = newer is not None and gone == 1 and added == 1
        self.record('varbit: the other cold column keeps its chunks', 'VERIFIED' if b_kept else 'FAILED',
                    f'{gone} chunk keys retired, {added} written')

        self.pair('update {t} set b = ' + bits(3000, rnd) + ' where id = 4;')
        self.compare2('varbit: after replacing binary zeros')
        self.check_chunks2('varbit: after replacing binary zeros')

        self.pair('delete from {t} where id = 1;\nrollback;\n', autocommit=False)
        self.compare2('varbit: after rolled-back delete of two cold values')
        self.pair('delete from {t} where id = 1;')
        self.compare2('varbit: after delete of two cold values')
        self.check_chunks2('varbit: after delete of two cold values')

        # Residual REC_BIGONE: the hot remainder still exceeds a page after the cold value moves out.
        cols = ', '.join(hexs(1900) for _ in range(10))
        self.pair(f'insert into {{t}} values (1, {cols}, {hexs(6000)});\n'
                  f'insert into {{t}} values (2, {cols}, NULL);\n', which=1)
        self.compare2('bigone: after insert', which=1)
        self.check_chunks2('bigone: after insert', which=1)
        rc, text = db.sql('show heap capacity of g_inner;')
        big = re.search(r'Num_overflowed_recs\s*:\s*(\d+)', text)
        self.record('bigone: the parent row is still a big record', 'VERIFIED' if big and int(big.group(1)) >= 1
                    else 'FAILED', f'Num_overflowed_recs {big.group(1) if big else "absent"}')
        self.pair("update {t} set a = a || 'r';", which=1)
        self.compare2('bigone: after update', which=1)
        self.check_chunks2('bigone: after update', which=1)

        # Negative controls: a wrong value, a malformed chunk and an extra chunk set must be detected.
        before = len(self.results)
        self.compare2('control: tampered oracle', tamper=True)
        last = self.results[before]
        self.results[before] = {'predicate': 'control: tampered oracle is detected',
                                'verdict': 'VERIFIED' if last['verdict'] == 'FAILED' else 'FAILED',
                                'detail': last['detail']}
        print(f'  {self.results[before]["verdict"]:13} control: tampered oracle is detected', flush=True)
        c = f's_inner{CHUNK_TABLE_SUFFIX}'
        db.sql(f'create table saved_chunk as select * from {c} where ck = (select min(ck) from {c});\n'
               f"update {c} set chunk = X'00' where ck = (select ck from saved_chunk);")
        rc, text = db.sql('select id, sha2(hex(b), 256) as h from s_inner order by id;')
        ok = rc != 0 or bool(errors(text))
        self.record('control: a short chunk is an error', 'VERIFIED' if ok else 'FAILED',
                    errors(text)[0][:120] if errors(text) else 'query returned rows')
        db.sql(f'update {c} set chunk = (select chunk from saved_chunk) where ck = (select ck from saved_chunk);\n'
               'drop table saved_chunk;')
        self.compare2('after the chunk is restored')
        return self.results


def own_pids(exe):
    """PIDs of this installation's processes only, selected by /proc/PID/exe."""
    pids = []
    path = os.path.join(os.environ['CUBRID'], 'bin', exe)
    for p in subprocess.run(['pgrep', '-u', str(os.getuid()), '-x', exe], stdout=subprocess.PIPE,
                            text=True).stdout.split():
        try:
            if os.readlink(f'/proc/{p}/exe') == path:
                pids.append(int(p))
        except OSError:
            pass
    return pids


def load_env(wt):
    env_sh = wt / '.cub-workenv/env.sh'
    if not env_sh.exists():
        raise HarnessError('no .cub-workenv; run cub-workenv init')
    out = subprocess.run(['bash', '-c', f'source "{env_sh}" >/dev/null 2>&1; env -0'],
                         stdout=subprocess.PIPE, check=True).stdout
    for item in out.split(b'\0'):
        if b'=' in item:
            k, v = item.split(b'=', 1)
            os.environ[k.decode()] = v.decode()


def supports_inner(cubrid):
    lib = Path(cubrid) / 'lib/libcubrid.so'
    try:
        return b'inner_table_threshold' in lib.read_bytes()
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', required=True, choices=['storage-1', 'storage-2', 'recovery'])
    ap.add_argument('--root', required=True, type=Path)
    args = ap.parse_args()
    wt = Path(subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], text=True).strip())
    load_env(wt)
    stamp = time.strftime('%Y%m%d-%H%M%S')
    root = (args.root / f'{args.case}-{stamp}').resolve()
    root.mkdir(parents=True)
    log = root / 'harness.log'
    summary = {'case': args.case, 'worktree': str(wt),
               'commit': subprocess.check_output(['git', '-C', str(wt), 'rev-parse', 'HEAD'], text=True).strip(),
               'dirty_src': bool(subprocess.check_output(['git', '-C', str(wt), 'status', '--porcelain', '--', 'src'],
                                                         text=True).strip()),
               'cubrid': os.environ['CUBRID'], 'threshold': THRESHOLD, 'seed': SEED}
    inner = supports_inner(os.environ['CUBRID'])
    summary['engine_supports_inner_table'] = inner
    name = 'itacc' + str(int(time.time()))
    summary['database'] = name
    print(f'{args.case}: database {name}, artifacts {root}, inner table support {inner}', flush=True)
    rc = 2
    started = False
    conf = Path(os.environ.get('CUBRID_CONF_FILE') or Path(os.environ['CUBRID']) / 'conf/cubrid.conf')
    conf_backup = None
    if args.case == 'recovery':
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import recovery
        conf_text = conf.read_text()
        try:
            results = recovery.RecoveryCase(root, wt).run(conf, conf_text)
        finally:
            conf.write_text(conf_text)
        summary['results'] = results
        verdicts = {r['verdict'] for r in results}
        rc = 0 if verdicts == {'VERIFIED'} else 1 if 'FAILED' in verdicts else 2
        summary['overall'] = {0: 'VERIFIED', 1: 'FAILED', 2: 'NOT VERIFIED'}[rc]
        (root / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(f'{args.case}: {summary["overall"]} (exit {rc}); {root / "result.json"}')
        return rc
    try:
        sh(['cub-workenv', 'create-db', name], log)
        if inner:
            conf_backup = conf.read_text()
            conf.write_text(conf_backup + f'\n[@{name}]\ninner_table_threshold={THRESHOLD}\n')
        sh(['cubrid', 'server', 'start', name], log, timeout=300)
        started = True
        summary['server_pids'] = own_pids('cub_server')
        results = (Case if args.case == 'storage-1' else Case2)(Db(root, name), inner).run()
        sh(['cubrid', 'server', 'stop', name], log, timeout=300)
        started = False
        ck = sh(['cubrid', 'checkdb', '-S', name], root / 'checkdb.log', check=False)
        results.append({'predicate': 'checkdb after the case', 'verdict': 'VERIFIED' if ck == 0 else 'FAILED',
                        'detail': f'exit {ck}'})
        print(f'  {results[-1]["verdict"]:13} checkdb after the case', flush=True)
        summary['results'] = results
        verdicts = {r['verdict'] for r in results}
        rc = 0 if verdicts == {'VERIFIED'} else 1 if 'FAILED' in verdicts else 2
    except (HarnessError, subprocess.TimeoutExpired, OSError) as e:
        summary['harness_error'] = str(e)
        print(f'HARNESS ERROR: {e}', file=sys.stderr)
        rc = 2
    finally:
        if started:
            sh(['cubrid', 'server', 'stop', name], log, timeout=300, check=False)
        if conf_backup is not None:
            conf.write_text(conf_backup)
    summary['overall'] = {0: 'VERIFIED', 1: 'FAILED', 2: 'NOT VERIFIED'}[rc]
    (root / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(f'{args.case}: {summary["overall"]} (exit {rc}); {root / "result.json"}')
    return rc


if __name__ == '__main__':
    sys.exit(main())
