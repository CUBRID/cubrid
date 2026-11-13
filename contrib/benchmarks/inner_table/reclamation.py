"""Case reclamation of the Inner Table acceptance driver (accept.py --case reclamation).

An ordinary oracle table and an opted-in table get the same changes.

1. Old snapshot: a REPEATABLE READ transaction reads the opted-in table, waits while another session overwrites two
   values and deletes a row, and reads again. Both reads must equal the oracle before the change; a new snapshot must
   equal the oracle after it.
2. Drain: after the old snapshot ends, vacuum must remove every retired chunk row and index entry. The physical
   record count of the chunk heap and the value count of its index must reach the visible chunk row count.
3. Reuse after restart: retire the value with the largest ID, SIGKILL the server before vacuum, restart, and insert
   new values. A new ID may equal the retired one while its dead chunks wait for vacuum; results and checkdb must stay
   correct.
4. Churn: replace every large value repeatedly, draining vacuum each round. The chunk heap must stop growing.

Processes are selected by /proc/PID/exe under this installation, never by name alone.
"""

import random
import re
import subprocess
import time

import accept
import recovery

SUFFIX = accept.CHUNK_TABLE_SUFFIX
C = 't_inner' + SUFFIX
CANON = 'select id, n, octet_length(v) as lv, sha2(v, 256) as hv from {t} order by id;'
ROUNDS = 20


class ReclamationCase:
    def __init__(self, root, name):
        self.root, self.name = root, name
        self.db = accept.Db(root, name)
        self.results = []
        self.rnd = random.Random(accept.SEED + 4)

    def record(self, name, verdict, detail=''):
        self.results.append({'predicate': name, 'verdict': verdict, 'detail': detail})
        print(f'  {verdict:13} {name}{": " + detail if detail else ""}', flush=True)

    def hexs(self, n):
        return "'" + ''.join(self.rnd.choice('0123456789abcdef') for _ in range(n)) + "'"

    def both(self, template):
        for t in ('t_oracle', 't_inner'):
            rc, text = self.db.sql(template.format(t=t))
            if rc or accept.errors(text):
                raise accept.HarnessError(f'statement failed on {t}: {accept.errors(text)[:2]}')

    def canon(self, t):
        rc, text = self.db.sql(CANON.format(t=t))
        return None if rc or accept.errors(text) else accept.rows(text)

    def compare(self, step):
        o, i = self.canon('t_oracle'), self.canon('t_inner')
        ok = o is not None and o == i and o != []
        self.record(f'{step}: equal results', 'VERIFIED' if ok else 'FAILED', '' if ok else 'results differ')
        rc, text = self.db.sql(f'select (select count(*) from t_inner where octet_length(v) >= {accept.THRESHOLD}) '
                               f'as n, (select count(distinct ck div 1048576) from {C}) as c from db_root;')
        r = accept.rows(text)[0] if not rc and accept.rows(text) else None
        ok = r is not None and r['n'] == r['c']
        self.record(f'{step}: chunk rows match large values', 'VERIFIED' if ok else 'FAILED',
                    f'{r["c"]} chunk values for {r["n"]} large values' if r else 'query failed')

    def physical(self):
        """(visible chunk rows, physical chunk records, index values, chunk heap pages)."""
        rc, text = self.db.sql(f'select count(*) as c from {C};\nshow heap capacity of {C};\n'
                               f'show index capacity of {C}.pk_{C}_ck;')
        if rc or accept.errors(text):
            return None
        get = lambda k: int(re.search(rf'{k}\s*:\s*(\d+)', text).group(1))
        return int(accept.rows(text)[0]['c']), get('Num_recs'), get('Total_value'), get('Num_pages')

    def drain(self):
        """Drive vacuum with filler work until the dead chunk rows are gone. Returns the last measurement."""
        p = None
        for _ in range(60):
            p = self.physical()
            if p and p[0] == p[1] == p[2]:
                return True, p
            self.db.sql("insert into f select rownum, repeat('f', 900) from db_class c1, db_class c2 limit 2000;\n"
                        'delete from f;')
            time.sleep(1)
        return False, p

    def run(self, log):
        db = self.db
        setup = ''.join(f'create table {t} (id int primary key, n int, v varchar);\n' for t in ('t_oracle', 't_inner'))
        setup += f'create table {C} (ck bigint primary key, chunk bit varying);\ncreate table f (id int, v varchar(1000));\n'
        rc, text = db.sql(setup)
        if rc or accept.errors(text):
            raise accept.HarnessError(f'setup failed: {accept.errors(text)[:3]}')
        sizes = {1: 5000, 2: 9000, 3: 20000, 4: 6000, 5: 30000}
        for i, n in sizes.items():
            self.both(f'insert into {{t}} values ({i}, 0, {self.hexs(n)});')
        self.both("insert into {t} values (6, 0, 'small');")
        self.compare('after insert')

        # 1. Old snapshot over an overwrite and a DELETE.
        before = self.canon('t_oracle')
        script = self.root / 'old_snapshot.sql'
        script.write_text('set transaction isolation level repeatable read;\n' + CANON.format(t='t_inner')
                          + '\nselect sleep(20) from db_root;\n' + CANON.format(t='t_inner') + '\ncommit;\n')
        out = self.root / 'old_snapshot.out'
        old = subprocess.Popen(['csql', '-u', 'dba', '-l', '--no-auto-commit', '-i', str(script), self.name],
                               stdin=subprocess.DEVNULL, stdout=open(out, 'w'), stderr=subprocess.STDOUT)
        for _ in range(30):
            if 'Result of SELECT' in out.read_text(errors='replace'):
                break
            time.sleep(0.5)
        t0 = time.time()
        self.both(f'update {{t}} set v = {self.hexs(7000)} where id = 1;\n'
                  f'update {{t}} set v = {self.hexs(8000)} where id = 2;\ndelete from {{t}} where id = 3;')
        changed_in = time.time() - t0
        self.compare('new snapshot after overwrite and delete')
        old.wait(timeout=120)
        text = out.read_text(errors='replace')
        reads = [accept.rows(part) for part in text.split('=== <Result of SELECT')[1:]]
        reads = [r for r in reads if r and 'hv' in r[0]]
        ok = len(reads) == 2 and reads[0] == before and reads[1] == before and changed_in < 15
        self.record('old snapshot reads the old full values before and after the change',
                    'VERIFIED' if ok else 'FAILED',
                    f'{len(reads)} reads, change took {changed_in:.1f}s' + ('' if ok else f'; see {out}'))

        # 2. Drain vacuum after the old snapshot ends.
        ok, p = self.drain()
        self.record('vacuum removes retired chunk rows and index entries', 'VERIFIED' if ok else 'FAILED',
                    f'visible {p[0]}, records {p[1]}, index values {p[2]}' if p else 'measurement failed')
        self.compare('after vacuum')

        # 3. Retire the largest ID, crash before vacuum, restart, insert new values.
        rc, text = db.sql(f'select max(ck) div 1048576 as m from {C};')
        top = int(accept.rows(text)[0]['m'])
        self.both("update {t} set v = 'short' where id = 2;")
        recovery.kill_runtime(self.name)
        accept.sh(['cubrid', 'server', 'start', self.name], log, timeout=300)
        self.both(f'insert into {{t}} values (20, 0, {self.hexs(9000)});\n'
                  f'insert into {{t}} values (21, 0, {self.hexs(12000)});')
        rc, text = db.sql(f'select min(ck) div 1048576 as lo, max(ck) div 1048576 as hi from {C} '
                          f'where ck div 1048576 > {top - 1};')
        r = accept.rows(text)[0] if accept.rows(text) else {}
        # an observation, not a predicate: the counter may or may not give the retired ID again
        print(f'  (largest ID before the crash {top}; visible IDs from {r.get("lo")} to {r.get("hi")} after it)',
              flush=True)
        self.compare('after reuse across a crash')
        ok, p = self.drain()
        self.record('vacuum after reuse across a crash', 'VERIFIED' if ok else 'FAILED',
                    f'visible {p[0]}, records {p[1]}, index values {p[2]}' if p else 'measurement failed')
        self.compare('after reuse and vacuum')

        # 4. Churn.
        pages = []
        for k in range(ROUNDS):
            self.both(''.join(f'update {{t}} set v = {self.hexs(n)} where id = {i};\n'
                              for i, n in ((1, 5000), (4, 6000), (5, 30000), (20, 9000), (21, 12000))))
            ok, p = self.drain()
            if not ok:
                self.record(f'churn round {k + 1}: vacuum drains', 'FAILED', f'{p}')
                break
            pages.append(p[3])
        if len(pages) == ROUNDS:
            grew = pages[-1] - pages[4]
            ok = grew <= 2
            self.record('churn: reclaimed chunk space is reused', 'VERIFIED' if ok else 'FAILED',
                        f'chunk heap pages per round {pages}')
        self.compare('after churn')
        return self.results
