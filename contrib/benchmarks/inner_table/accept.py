#!/usr/bin/env python3
"""Acceptance driver for the Inner Table POC.

Usage, from a built and cub-workenv-initialized worktree:

    python3 contrib/benchmarks/inner_table/accept.py --case storage-1 --root OUTDIR

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
            f'select count(distinct value_id) as nvalues, nvl(sum(octet_length(chunk)), 0) as nbytes '
            f'from t_inner{CHUNK_TABLE_SUFFIX};')
        rc2, text2 = self.db.sql(
            f'select count(*) as nvalues, nvl(sum(octet_length(v)), 0) as nbytes from t_inner '
            f'where octet_length(v) >= {THRESHOLD};')
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
                 f'create table t_inner{CHUNK_TABLE_SUFFIX} (value_id bigint not null, ord int not null, chunk varchar,'
                 f' constraint pk_t_inner{CHUNK_TABLE_SUFFIX} primary key (value_id, ord));\n'
                 f'create serial t_inner{CHUNK_TABLE_SUFFIX}_seq;\n')
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
        print(f'  -> {self.results[before]["verdict"]} control: tampered oracle is detected', flush=True)

        # Negative control 2: a missing chunk must be an error, not NULL or a partial value.
        rc, text = db.sql(f'select count(*) as c from t_inner{CHUNK_TABLE_SUFFIX};')
        if rc or not rows(text) or int(rows(text)[0]['c']) == 0:
            self.record('control: missing chunk is an error', 'NOT VERIFIED', 'no chunk rows to remove')
        else:
            db.sql(f'delete from t_inner{CHUNK_TABLE_SUFFIX} where ord = 0 and value_id = '
                   f'(select min(value_id) from t_inner{CHUNK_TABLE_SUFFIX});')
            rc, text = db.sql('select id, sha2(v, 256) as h from t_inner order by id;')
            ok = rc != 0 or bool(errors(text))
            self.record('control: missing chunk is an error', 'VERIFIED' if ok else 'FAILED',
                        errors(text)[0][:120] if errors(text) else 'query returned rows')
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
    ap.add_argument('--case', required=True, choices=['storage-1'])
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
    conf = Path(os.environ['CUBRID']) / 'conf/cubrid.conf'
    conf_backup = None
    try:
        sh(['cub-workenv', 'create-db', name], log)
        if inner:
            conf_backup = conf.read_text()
            conf.write_text(conf_backup + f'\n[@{name}]\ninner_table_threshold={THRESHOLD}\n')
        sh(['cubrid', 'server', 'start', name], log, timeout=300)
        started = True
        summary['server_pids'] = own_pids('cub_server')
        results = Case(Db(root, name), inner).run()
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
