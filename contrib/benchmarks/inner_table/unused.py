#!/usr/bin/env python3
"""Unused-cost case of the Inner Table POC (phase 11): throughput of a baseline and a candidate installation.

Usage, after building both as release installations initialized with cub-workenv:

    python3 contrib/benchmarks/inner_table/unused.py --base WORKTREE --cand WORKTREE --root OUTDIR

Each installation gets a new database with the same tables. Only the candidate database enables
inner_table_threshold. The fixture is the ordinary-table workload of contrib/benchmarks/oos-performance/ordinary.py:
id INT PRIMARY KEY, a INT, s VARCHAR(100), with a = MOD(id, 100) and s = SHA2(id). Every value stays inline.

Two kinds of table:
  never   no companion class; the candidate never opts it in
  inline  has a companion class; the candidate opts it in, and every value stays inline

Workloads, each an identical batch over all rows: select (COUNT(*), SUM(a), ten times), insert (INSERT ... SELECT
into an empty table), update (SET a = a + 1 on a fresh copy), delete (all rows of a fresh copy). The table is
restored outside the timed interval, and both servers must be idle before each sample.

A sample records three throughputs of one batch: rows per second of csql-reported statement and commit time, rows per
server instruction and rows per server cycle (perf stat attached to the server process for the batch). --metric picks
the one the verdict uses; the default, instructions, does not move with the load of other processes on the host, which
made the time metric INCONCLUSIVE on a shared host. Pairs alternate their order. After two warmup pairs, at least
--pairs measured pairs; the geometric mean of candidate/baseline ratios is bootstrapped with a recorded seed. VERIFIED: lower 95% bound >= 0.95 and interval width <= 0.02. FAILED: width <= 0.02 and lower bound
< 0.95, or upper bound < 0.95. Otherwise more pairs, up to --max-pairs, then INCONCLUSIVE.

Control: the same procedure compares the baseline with itself plus injected work, an index range read of the seed
table sized from the warmup pairs to cost 6% of the batch in the chosen metric. Each workload's control must not be
VERIFIED.

Exit 0: every workload VERIFIED and every control rejected. Exit 1: a FAILED workload or an accepted control.
Exit 2: otherwise.
"""

import argparse
import json
import math
import os
import random
import re
import signal
import statistics
import subprocess
import sys
import time
from pathlib import Path

SEED = 20261111
BOOTSTRAP = 10000
THRESHOLD = 2048
KINDS = ('never', 'inline')
WORKLOADS = ('select', 'insert', 'update', 'delete')
SELECT_REPEAT = 10
METRICS = ('time', 'instructions', 'cycles')


class HarnessError(Exception):
    pass


def load_env(worktree):
    env_sh = Path(worktree).resolve() / '.cub-workenv/env.sh'
    out = subprocess.run(['bash', '-c', f'unset PRESET_MODE; source "{env_sh}" >/dev/null 2>&1 && env -0'], stdout=subprocess.PIPE,
                         check=True).stdout
    env = dict(item.split(b'=', 1) for item in out.split(b'\0') if b'=' in item)
    env = {k.decode(): v.decode() for k, v in env.items()}
    if env.get('CUBRID_RUNTIME_READY') != '1':
        raise HarnessError(f'{worktree}: cub-workenv environment is not ready')
    return env


class Install:
    def __init__(self, label, worktree, root):
        self.label, self.worktree = label, Path(worktree).resolve()
        self.env = load_env(worktree)
        self.root = root / label
        self.root.mkdir(parents=True)
        self.n = 0
        self.db = None
        self.commit = subprocess.check_output(['git', '-C', str(worktree), 'rev-parse', 'HEAD'], text=True).strip()

    def sh(self, cmd, timeout=1800, check=True):
        with open(self.root / 'harness.log', 'a') as f:
            f.write('$ ' + ' '.join(cmd) + '\n')
            f.flush()
            r = subprocess.run(cmd, env=self.env, cwd=self.worktree, stdin=subprocess.DEVNULL, stdout=f,
                               stderr=subprocess.STDOUT, timeout=timeout)
        if check and r.returncode:
            raise HarnessError(f'{self.label}: {" ".join(cmd[:3])} failed with {r.returncode}')
        return r.returncode

    def sql(self, text, name=None):
        self.n += 1
        stem = self.root / f'{self.n:04d}-{name or "sql"}'
        Path(f'{stem}.sql').write_text(text)
        r = subprocess.run(['csql', '-u', 'dba', '-i', f'{stem}.sql', self.db], env=self.env, cwd=self.worktree,
                           stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                           timeout=3600)
        Path(f'{stem}.out').write_text(r.stdout)
        if r.returncode or re.search(r'ERROR', r.stdout):
            raise HarnessError(f'{self.label}: SQL failed; see {stem}.out')
        return r.stdout

    def server_pid(self):
        exe = os.path.join(self.env['CUBRID'], 'bin', 'cub_server')
        for p in subprocess.run(['pgrep', '-u', str(os.getuid()), '-x', 'cub_server'], stdout=subprocess.PIPE,
                                text=True).stdout.split():
            try:
                if os.readlink(f'/proc/{p}/exe') == exe and self.db in Path(f'/proc/{p}/cmdline').read_text():
                    return int(p)
            except OSError:
                pass
        return None


def cpu_ticks(pid):
    fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    return int(fields[11]) + int(fields[12])


def wait_idle(pids):
    """Wait until every server spends at most 1% CPU over eight consecutive 0.25 s intervals."""
    streak, deadline = 0, time.monotonic() + 120
    hz = os.sysconf('SC_CLK_TCK')
    while time.monotonic() < deadline:
        before = [cpu_ticks(p) for p in pids]
        time.sleep(0.25)
        used = max((cpu_ticks(p) - b) / hz for p, b in zip(pids, before))
        streak = streak + 1 if used <= 0.0101 else 0
        if streak >= 8:
            return
    raise HarnessError('servers did not become idle')


def timed(out):
    """Sum of csql-reported statement and commit times."""
    return sum(float(x) for x in re.findall(r'\((\d+\.\d+) sec\)', out))


def fixture(rows):
    return (f'create table seq (n int primary key);\n'
            f'insert into seq select rownum from db_attribute a, db_attribute b, db_attribute c limit {rows};\n'
            f"create table seed (id int primary key, a int, s varchar(100));\n"
            f"insert into seed select n, mod(n, 100), sha2(n || '', 256) from seq;\n")


def tables(kind):
    ddl = ''
    for w in WORKLOADS:
        t = f'{kind}_{w}'
        ddl += f'create table {t} (id int primary key, a int, s varchar(100));\n'
        if kind == 'inline':
            ddl += f'create table {t}__inner (ck bigint primary key, chunk bit varying);\n'
    return ddl


def prepare(kind, workload):
    """Statements that restore the table of a sample, outside the timed interval."""
    t = f'{kind}_{workload}'
    if workload == 'select':
        return None
    if workload == 'insert':
        return f'truncate {t};\n'
    return f'truncate {t};\ninsert into {t} select * from seed order by id;\n'


def extra_work(rows, total):
    """Statements that read TOTAL seed rows through the primary key."""
    full, rest = divmod(int(total), rows)
    return (f'select sum(a) from seed where id <= {rows};\n' * full
            + (f'select sum(a) from seed where id <= {rest};\n' if rest else ''))


def batch(kind, workload, extra):
    t = f'{kind}_{workload}'
    body = {'select': f'select count(*), sum(a) from {t};\n' * SELECT_REPEAT,
            'insert': f'insert into {t} select * from seed order by id;\n',
            'update': f'update {t} set a = a + 1;\n',
            'delete': f'delete from {t};\n'}[workload]
    return body + extra


class Counters:
    """perf stat on a server process, from start to stop."""

    def __init__(self, pid, path):
        self.path = path
        self.proc = subprocess.Popen(['perf', 'stat', '-x,', '-e', 'instructions,cycles', '-p', str(pid), '-o', str(path)],
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.3)
        if self.proc.poll() is not None:
            raise HarnessError(f'perf stat exited with {self.proc.returncode}')

    def stop(self):
        self.proc.send_signal(signal.SIGINT)
        self.proc.wait(timeout=60)
        got = {}
        for line in Path(self.path).read_text().splitlines():
            f = line.split(',')
            if len(f) > 2 and f[2] in ('instructions', 'cycles') and f[0].isdigit():
                got[f[2]] = int(f[0])
        if set(got) != {'instructions', 'cycles'}:
            raise HarnessError(f'perf stat counted nothing; see {self.path}')
        return got


def geomean_ci(ratios, rnd):
    logs = [math.log(r) for r in ratios]
    means = sorted(math.exp(statistics.fmean(rnd.choices(logs, k=len(logs)))) for _ in range(BOOTSTRAP))
    return math.exp(statistics.fmean(logs)), means[int(0.025 * BOOTSTRAP)], means[int(0.975 * BOOTSTRAP) - 1]


def verdict(lo, hi):
    if hi < 0.95:
        return 'FAILED'
    if hi - lo <= 0.02:
        return 'VERIFIED' if lo >= 0.95 else 'FAILED'
    return 'INCONCLUSIVE'


class Run:
    def __init__(self, args):
        self.args = args
        self.root = (Path(args.root) / time.strftime('unused-%Y%m%d-%H%M%S')).resolve()
        self.root.mkdir(parents=True)
        self.base = Install('base', args.base, self.root)
        self.cand = Install('cand', args.cand, self.root)
        self.rnd = random.Random(SEED)
        self.summary = {'seed': SEED, 'rows': args.rows, 'base_commit': self.base.commit,
                        'cand_commit': self.cand.commit, 'loadavg_start': os.getloadavg(), 'results': []}

    def save(self):
        (self.root / 'summary.json').write_text(json.dumps(self.summary, indent=2) + '\n')

    def start(self):
        stamp = str(int(time.time()))[-8:]
        for inst in (self.base, self.cand):
            inst.db = f'p11{inst.label[0]}{stamp}'
            inst.sh(['cub-workenv', 'create-db', inst.db])
            conf = Path(inst.env['CUBRID_CONF_FILE'])
            inst.conf_backup = conf.read_text()
            if inst is self.cand:
                conf.write_text(inst.conf_backup + f'\n[@{inst.db}]\ninner_table_threshold={THRESHOLD}\n')
            (inst.root / 'cubrid.conf').write_text(conf.read_text())
            inst.sh(['cubrid', 'service', 'start'], check=False)
            inst.sh(['cubrid', 'server', 'start', inst.db], timeout=600)
            inst.pid = inst.server_pid()
            if inst.pid is None:
                raise HarnessError(f'{inst.label}: server pid not found')
            inst.sql(fixture(self.args.rows) + tables('never') + tables('inline')
                     + ''.join(f'insert into {k}_select select * from seed order by id;\n' for k in KINDS)
                     + ''.join(f'update statistics on {k}_select;\n' for k in KINDS), 'fixture')
        self.summary['databases'] = {'base': self.base.db, 'cand': self.cand.db}

    def stop(self):
        for inst in (self.base, self.cand):
            if inst.db:
                inst.sh(['cubrid', 'server', 'stop', inst.db], timeout=600, check=False)
                if hasattr(inst, 'conf_backup'):
                    Path(inst.env['CUBRID_CONF_FILE']).write_text(inst.conf_backup)

    def evidence(self):
        """Plans and storage of the measured tables, recorded once before the samples."""
        for inst in (self.base, self.cand):
            text = 'set trace on output text;\n'
            for k in KINDS:
                text += f'select count(*), sum(a) from {k}_select;\nshow trace;\nshow heap capacity of {k}_select;\n'
            inst.sql(text, 'evidence')

    def sample(self, inst, kind, workload, extra=''):
        prep = prepare(kind, workload)
        if prep:
            inst.sql(prep, f'{kind}-{workload}-prepare')
        wait_idle([self.base.pid, self.cand.pid])
        counters = Counters(inst.pid, inst.root / f'perf-{inst.n + 1:04d}.csv')
        try:
            out = inst.sql(batch(kind, workload, extra), f'{kind}-{workload}')
        finally:
            counts = counters.stop()
        rows = self.args.rows * (SELECT_REPEAT if workload == 'select' else 1)
        if workload != 'select':
            m = re.search(r'(\d+) rows? affected', out)
            if not m or int(m.group(1)) != self.args.rows:
                raise HarnessError(f'{inst.label} {kind} {workload}: unexpected row count')
        else:
            got = re.findall(r'^\s+(\d+)\s+(\d+)\s*$', out, re.M)
            if len(got) != SELECT_REPEAT or len(set(got)) != 1 or int(got[0][0]) != self.args.rows:
                raise HarnessError(f'{inst.label} {kind} select: unexpected result')
        return {'time': rows / timed(out), 'instructions': rows / counts['instructions'], 'cycles': rows / counts['cycles']}

    def calibrate(self):
        """Cost of one seed row of extra work on the baseline, per metric, from a full-range read."""
        costs = []
        for _ in range(3):
            s = self.sample(self.base, 'never', 'select', extra_work(self.args.rows, self.args.rows))
            t = self.sample(self.base, 'never', 'select')
            costs.append({m: (1 / s[m] - 1 / t[m]) * self.args.rows * SELECT_REPEAT / self.args.rows for m in METRICS})
        self.row_cost = {m: statistics.median(c[m] for c in costs) for m in METRICS}
        self.summary['extra_work_cost_per_row'] = self.row_cost

    def measure(self, kind, workload, pair_of, control=False):
        """Return the measured pairs (baseline tput, other tput) and the verdict."""
        a, b = pair_of
        metric = self.args.metric
        extra, extra_rows = '', 0
        pairs, warm = [], []
        n = 0
        while True:
            first, second = ((a, False), (b, True)) if n % 2 == 0 else ((b, True), (a, False))
            got = {}
            for inst, is_other in (first, second):
                got[is_other] = self.sample(inst, kind, workload, extra if is_other else '')
            if n < 2:
                warm.append(got[False][metric])
                if control and n == 1:
                    rows = self.args.rows * (SELECT_REPEAT if workload == 'select' else 1)
                    cost = statistics.median(rows / t for t in warm)
                    extra_rows = max(1, round(0.06 * cost / self.row_cost[metric]))
                    extra = extra_work(self.args.rows, extra_rows)
            else:
                pairs.append((got[False], got[True]))
            n += 1
            m = len(pairs)
            if m >= self.args.pairs and (m - self.args.pairs) % 4 == 0:
                stats = {}
                for x in METRICS:
                    g, lo, hi = geomean_ci([o[x] / base[x] for base, o in pairs], self.rnd)
                    stats[x] = {'geomean': g, 'lo': lo, 'hi': hi, 'verdict': verdict(lo, hi),
                                'base_median': statistics.median(p[0][x] for p in pairs)}
                v = stats[metric]['verdict']
                if v != 'INCONCLUSIVE' or m >= self.args.max_pairs:
                    return {'kind': kind, 'workload': workload, 'control': control, 'pairs': pairs,
                            'extra_rows': extra_rows, 'metric': metric, 'stats': stats, 'verdict': v,
                            **{k: stats[metric][k] for k in ('geomean', 'lo', 'hi', 'base_median')}}

    def run(self):
        try:
            self.start()
            self.evidence()
            self.calibrate()
            for kind in KINDS:
                for workload in WORKLOADS:
                    for control in (False, True):
                        pair_of = (self.base, self.base if control else self.cand)
                        r = self.measure(kind, workload, pair_of, control)
                        self.summary['results'].append(r)
                        self.save()
                        name = f'{"control " if control else ""}{kind} {workload}'
                        other = ', '.join(f'{x} {r["stats"][x]["geomean"]:.4f} [{r["stats"][x]["lo"]:.4f}, '
                                          f'{r["stats"][x]["hi"]:.4f}]' for x in METRICS if x != r['metric'])
                        print(f'  {r["verdict"]:13} {name}: {r["metric"]} ratio {r["geomean"]:.4f} [{r["lo"]:.4f},'
                              f' {r["hi"]:.4f}], {len(r["pairs"])} pairs; {other}', flush=True)
        finally:
            self.summary['loadavg_end'] = os.getloadavg()
            self.save()
            self.stop()
        main = [r['verdict'] for r in self.summary['results'] if not r['control']]
        controls = [r['verdict'] for r in self.summary['results'] if r['control']]
        if 'FAILED' in main or 'VERIFIED' in controls:
            return 1
        return 0 if set(main) == {'VERIFIED'} else 2


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--base', required=True)
    ap.add_argument('--cand', required=True)
    ap.add_argument('--root', required=True)
    ap.add_argument('--rows', type=int, default=200000)
    ap.add_argument('--pairs', type=int, default=12)
    ap.add_argument('--max-pairs', type=int, default=40)
    ap.add_argument('--metric', choices=METRICS, default='instructions')
    args = ap.parse_args()
    try:
        run = Run(args)
        rc = run.run()
    except (HarnessError, subprocess.TimeoutExpired, OSError) as e:
        print(f'HARNESS ERROR: {e}', file=sys.stderr)
        return 2
    print(f'unused: exit {rc}; {run.root / "summary.json"}')
    return rc


if __name__ == '__main__':
    sys.exit(main())
