"""Case projection of the Inner Table acceptance driver (accept.py --case projection). Needs a debug build.

For each fixture, a baseline table (not opted in) and an opted-in table hold the same rows. Each has an unindexed
hot_value column. The server runs with INNER_TABLE_PAGE_TRACE_FILE, so every commit appends the distinct pages its
transaction fixed: user heap, chunk heap, overflow, index, catalog and other pages.

Per fixture, a cold run right after a server restart and a warm run in the same session:
1. select sum(hot_value) scans the heap (TRACE shows a heap scan) and returns equal sums.
2. The opted-in scan fixes zero chunk heap pages and no index page beyond the baseline scan, so no chunk index page.
3. Its data pages (heap + chunk + overflow) are strictly fewer than the baseline's.
4. Reading every large value returns equal hashes; its pages are reported.
Controls: the baseline scan must show heap pages (the trace sees events), and the full read of the opted-in table must
show chunk pages (the trace sees chunk pages when they are read).
"""

import json
import os
import random
import re
import subprocess

import accept

SUFFIX = accept.CHUNK_TABLE_SUFFIX
FIXTURES = {
    # name: (rows, {column: bytes of a large value}, large rows every N rows)
    'k8': (1000, {'c1': 8000}, 1),
    'k64': (200, {'c1': 65536}, 1),
    'multi': (800, {'c1': 4000, 'c2': 6000}, 1),
    'mixed': (2000, {'c1': 9000}, 2),
}
LINE = re.compile(r'tran=(\d+) heap=(\d+) chunk=(\d+) overflow=(\d+) index=(\d+) catalog=(\d+) other=(\d+) classes=(.*)')


class ProjectionCase:
    def __init__(self, root, name):
        self.root, self.name = root, name
        self.db = accept.Db(root, name)
        self.results = []
        self.rnd = random.Random(accept.SEED + 10)
        self.trace = root / 'pages.log'
        self.report = []

    def record(self, name, verdict, detail=''):
        self.results.append({'predicate': name, 'verdict': verdict, 'detail': detail})
        print(f'  {verdict:13} {name}{": " + detail if detail else ""}', flush=True)

    def hexs(self, n):
        return "'" + ''.join(self.rnd.choice('0123456789abcdef') for _ in range(n)) + "'"

    def setup(self, text):
        rc, out = self.db.sql(text)
        if rc or accept.errors(out):
            raise accept.HarnessError(f'setup failed: {accept.errors(out)[:3]}')

    def restart(self, log):
        accept.sh(['cubrid', 'server', 'stop', self.name], log, timeout=300)
        accept.sh(['cubrid', 'server', 'start', self.name], log, timeout=300)

    def measure(self, query, table):
        """Run QUERY twice in one session, with TRACE after each run. Return (rows, [pages], [trace]) per run.

        The pages of a run are those of its commit, recognized by the heap pages of TABLE it read."""
        self.trace.write_text('')
        rc, out = self.db.sql(f'set trace on output text;\n{query}\nshow trace;\n{query}\nshow trace;\n')
        if rc or accept.errors(out):
            raise accept.HarnessError(f'measurement failed: {accept.errors(out)[:2]}')
        lines = [LINE.search(x) for x in self.trace.read_text().splitlines()]
        lines = [m for m in lines if m and f' dba.{table}:' in ' ' + m.group(8)]
        pages = [dict(zip(('heap', 'chunk', 'overflow', 'index', 'catalog', 'other'), (int(x) for x in m.groups()[1:7])))
                 for m in lines[-2:]] if len(lines) >= 2 else [None, None]
        traces = out.split('=== <Result of SELECT Command')[1:]
        traces = [t for t in traces if 'Trace Statistics' in t]
        return accept.rows(out.split('Trace Statistics')[0]), pages, traces

    def fixture(self, log, fx, rows, cols, every):
        base, cand = f'{fx}_base', f'{fx}_cand'
        coldefs = ', '.join(f'{c} varchar' for c in cols)
        self.setup(f'create table {base} (id int primary key, hot_value int, {coldefs});\n'
                   f'create table {cand} (id int primary key, hot_value int, {coldefs});\n'
                   f'create table {cand}{SUFFIX} (ck bigint primary key, chunk bit varying);\n')
        batch = []
        for i in range(1, rows + 1):
            vals = ', '.join(self.hexs(n) if i % every == 0 else "'s'" for n in cols.values())
            batch.append(f'insert into {base} values ({i}, {i * 7 % 1000}, {vals});')
            if len(batch) == 100 or i == rows:
                self.setup('\n'.join(batch) + '\n')
                batch = []
        self.setup(f'insert into {cand} select * from {base};')
        rc, out = self.db.sql(f'select count(distinct ck div 1048576) as c from {cand}{SUFFIX};')
        moved = accept.rows(out)[0]['c'] if accept.rows(out) else '?'
        print(f'  ({fx}: {rows} rows, {moved} values moved to chunk rows)', flush=True)

        full = ', '.join(f'sha2({c}, 256) as h_{c}' for c in cols)
        r = {}
        for t in (base, cand):
            # restart before each table, so that the first run is cold and neither reads pages the other loaded
            self.restart(log)
            rows_, pages, traces = self.measure(f'select sum(hot_value) as s from {t};', t)
            stats = [re.search(rf'SCAN \(table: dba\.{t}\), \(heap time: \d+, fetch: (\d+), ioread: (\d+)', x)
                     for x in traces]
            r[t] = {'sum': rows_[0]['s'] if rows_ else None, 'pages': pages,
                    'heap_scan': len(stats) == 2 and all(stats),
                    'io': [m.groups() if m else None for m in stats]}
        b, c = r[base], r[cand]
        for k, mode in enumerate(('cold', 'warm')):
            step = f'{fx} {mode}'
            if k == 0:
                ok = b['sum'] is not None and b['sum'] == c['sum'] and b['heap_scan'] and c['heap_scan']
                self.record(f'{fx}: heap scans return equal sums', 'VERIFIED' if ok else 'FAILED',
                            f'sum {b["sum"]} / {c["sum"]}, heap scan {b["heap_scan"]} / {c["heap_scan"]}')
            bp, cp = b['pages'][k], c['pages'][k]
            if bp is None or cp is None or bp['heap'] == 0:
                self.record(f'{step}: page events present', 'NOT VERIFIED', f'{bp} / {cp}')
                continue
            ok = cp['chunk'] == 0 and cp['index'] <= bp['index']
            self.record(f'{step}: opted-in scan fixes no chunk page and no index page beyond the baseline',
                        'VERIFIED' if ok else 'FAILED', f'chunk {cp["chunk"]}, index {cp["index"]} vs {bp["index"]}')
            bd = bp['heap'] + bp['chunk'] + bp['overflow']
            cd = cp['heap'] + cp['chunk'] + cp['overflow']
            ok = cd < bd
            self.record(f'{step}: fewer data pages than the baseline', 'VERIFIED' if ok else 'FAILED',
                        f'{cd} vs {bd} (with index pages {cd + cp["index"]} vs {bd + bp["index"]})')
            self.report.append({'fixture': fx, 'mode': mode,
                                'baseline': {**bp, 'fetch_ioread': b['io'][k]}, 'inner': {**cp, 'fetch_ioread': c['io'][k]}})

        # Full values: equal content, and the control that the trace sees chunk pages.
        hb, pb, _ = self.measure(f'select id, {full} from {base} order by id;', base)
        hc, pc, _ = self.measure(f'select id, {full} from {cand} order by id;', cand)
        ok = hb != [] and hb == hc
        self.record(f'{fx}: full values are equal', 'VERIFIED' if ok else 'FAILED', f'{len(hb)} rows')
        pb, pc = pb[-1], pc[-1]
        ok = pc is not None and pc['chunk'] > 0
        self.record(f'{fx}: control, a full read fixes chunk pages', 'VERIFIED' if ok else 'FAILED', f'{pc}')
        self.report.append({'fixture': fx, 'mode': 'full read', 'baseline': pb, 'inner': pc})

    def run(self, log):
        os.environ['INNER_TABLE_PAGE_TRACE_FILE'] = str(self.trace)
        try:
            self.restart(log)
            for fx, (rows, cols, every) in FIXTURES.items():
                self.fixture(log, fx, rows, cols, every)
        finally:
            del os.environ['INNER_TABLE_PAGE_TRACE_FILE']
        (self.root / 'pages.json').write_text(json.dumps(self.report, indent=2) + '\n')
        print(f'  (page report {self.root / "pages.json"})', flush=True)
        return self.results
