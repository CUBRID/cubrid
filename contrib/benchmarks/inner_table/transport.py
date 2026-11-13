"""Case transport of the Inner Table acceptance driver (accept.py --case transport).

An ordinary oracle table and an opted-in table get the same statements, and each step compares their results.

1. Indexes: prefix, function, composite and unique indexes, index-only and index-driven reads, uniqueness failures.
2. Triggers: a trigger reads obj.v and new.v through the client workspace; another trigger writes large values.
3. Representation change: ADD COLUMN and MODIFY of the large column.
4. Rolled-back and failed DDL: TRUNCATE, DROP and RENAME rolled back; a DROP that fails on a foreign key; DDL on
   the companion class itself is refused.
5. RENAME, committed, survives a restart.
6. Unload and load: the dump holds full values and no chunk rows; a fresh database loads it into equal tables.
7. Standalone mode: DML and compactdb with the server stopped.
8. Committed TRUNCATE and DROP remove the chunk rows and the companion class.

After every step the chunk audit checks that the companion holds exactly one value ID per large value.
"""

import os
import random
import re
import subprocess
import time
from pathlib import Path

import accept

SUFFIX = accept.CHUNK_TABLE_SUFFIX
T = THRESHOLD = accept.THRESHOLD
TABLES = ('t_oracle', 't_inner')
CANON = 'select id, n, k, octet_length(v) as lv, sha2(v, 256) as hv from {t} order by id;'
NORMALIZE = [(re.compile(r'\(\d+\.\d+ sec\)'), ''), (re.compile(r't_oracle|t_inner'), 'T'),
             (re.compile(r'\((?:B\+tree|CLASS_OID|OID): [^)]*\)'), '')]


def normalize(text):
    for rx, rep in NORMALIZE:
        text = rx.sub(rep, text)
    return text


class TransportCase:
    def __init__(self, root, name):
        self.root, self.name = root, name
        self.db = accept.Db(root, name)
        self.results = []
        self.rnd = random.Random(accept.SEED + 5)
        self.extra_dbs = []

    def record(self, name, verdict, detail=''):
        self.results.append({'predicate': name, 'verdict': verdict, 'detail': detail})
        print(f'  {verdict:13} {name}{": " + detail if detail else ""}', flush=True)

    def hexs(self, n):
        return "'" + ''.join(self.rnd.choice('0123456789abcdef') for _ in range(n)) + "'"

    def sql(self, text, db=None, sa=False, autocommit=True):
        if db is None and not sa:
            return self.db.sql(text, autocommit)
        self.db.n += 1
        script = self.root / f'sql-{self.db.n:03d}.sql'
        script.write_text(text)
        cmd = ['csql', '-u', 'dba', '-l', '-i', str(script)] + (['-S'] if sa else []) + [db or self.name]
        r = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, timeout=600)
        (self.root / f'sql-{self.db.n:03d}.out').write_text(r.stdout)
        return r.returncode, r.stdout

    def setup(self, text):
        rc, out = self.sql(text)
        if rc or accept.errors(out):
            raise accept.HarnessError(f'setup failed: {accept.errors(out)[:3]}')

    def same(self, step, template, autocommit=True, expect_error=None):
        """Run TEMPLATE on both tables; outcomes and normalized outputs must be equal."""
        outs = []
        for t in TABLES:
            rc, out = self.sql(template.format(t=t), autocommit=autocommit)
            outs.append(out)
        errs = [bool(accept.errors(o)) for o in outs]
        ok = normalize(outs[0]) == normalize(outs[1])
        if expect_error is not None:
            ok = ok and errs == [expect_error, expect_error]
        self.record(f'{step}: same outcome on both tables', 'VERIFIED' if ok else 'FAILED',
                    '' if ok else f'errors {errs}; see sql-{self.db.n - 1:03d}.out and sql-{self.db.n:03d}.out')
        return ok

    def canon(self, t, db=None, sa=False):
        rc, out = self.sql(CANON.format(t=t), db=db, sa=sa)
        return None if rc or accept.errors(out) else accept.rows(out)

    def audit(self, step, t='t_inner', db=None, sa=False):
        """The companion holds one value ID per large value, and no other rows."""
        rc, out = self.sql(f'select (select count(*) from {t} where octet_length(v) >= {T}) as n, '
                           f'(select count(distinct ck div 1048576) from {t}{SUFFIX}) as c, '
                           f'(select count(*) from {t}{SUFFIX}) as r from db_root;', db=db, sa=sa)
        r = accept.rows(out)[0] if not rc and accept.rows(out) else None
        ok = r is not None and r['n'] == r['c'] and int(r['r']) >= int(r['c'])
        self.record(f'{step}: chunk rows match large values', 'VERIFIED' if ok else 'FAILED',
                    f'{r["c"]} value IDs in {r["r"]} chunk rows for {r["n"]} large values' if r else 'query failed')

    def compare(self, step, db=None, sa=False, oracle=None):
        o = oracle if oracle is not None else self.canon('t_oracle', db=db, sa=sa)
        i = self.canon('t_inner', db=db, sa=sa)
        ok = o is not None and o == i and o != []
        self.record(f'{step}: equal results', 'VERIFIED' if ok else 'FAILED', '' if ok else 'results differ')
        self.audit(step, db=db, sa=sa)
        return o

    def class_exists(self, name, db=None, sa=False):
        rc, out = self.sql(f"select count(*) as c from db_class where class_name = '{name}';", db=db, sa=sa)
        return int(accept.rows(out)[0]['c']) if not rc and accept.rows(out) else None

    def run(self, log):
        self.log = log
        try:
            return self._run()
        finally:
            for d in self.extra_dbs:
                accept.sh(['cubrid', 'server', 'stop', d], log, timeout=300, check=False)

    def _run(self):
        setup = ''
        for t in TABLES:
            setup += (f'create table {t} (id int primary key, n int, k int, v varchar);\n'
                      f'create table log_{t} (id int, ev varchar(8), h varchar(64), l int);\n'
                      f'create table feed_{t} (id int, v varchar);\n')
        setup += f'create table t_inner{SUFFIX} (ck bigint primary key, chunk bit varying);\n'
        self.setup(setup)
        sizes = {1: 3000, 2: 9000, 3: 20000, 4: 65536, 5: 7000, 6: 400000}
        for i, n in sizes.items():
            self.same(f'insert {i}', f'insert into {{t}} values ({i}, {i % 3}, {i}, {self.hexs(n)});')
        self.same('insert small and null', "insert into {t} values (7, 1, 7, 'small'), (8, 2, 8, NULL);")
        self.compare('after insert')

        # 1. Indexes.
        for ddl in ('create index ip_{t} on {t} (v(16));', 'create index if_{t} on {t} (substr(v, 1, 24));',
                    'create index ic_{t} on {t} (n, k);', 'create unique index iu_{t} on {t} (k);'):
            self.same(f'index DDL {ddl.split(" on ")[0].split()[-1]}', ddl, expect_error=False)
        reads = [
            'select /*+ recompile */ id, sha2(v, 256) h from {t} where v >= \'0\' using index ip_{t} order by id;',
            'select /*+ recompile */ id, octet_length(v) l from {t} '
            'where substr(v, 1, 24) > \'7\' using index if_{t} order by id;',
            'select /*+ recompile */ id, sha2(v, 256) h from {t} where n = 1 and k > 0 using index ic_{t} order by id;',
            'select /*+ recompile */ k, sha2(v, 256) h from {t} where k between 2 and 6 using index iu_{t} order by k;',
        ]
        for i, q in enumerate(reads):
            self.same(f'index read {i + 1}', q, expect_error=False)
        self.same('unique failure on insert', f'insert into {{t}} values (9, 0, 3, {self.hexs(5000)});',
                  expect_error=True)
        self.same('unique failure in a multi-row update',
                  f'update {{t}} set k = 4, v = {self.hexs(6000)} where id in (3, 5);', expect_error=True)
        self.same('primary key failure in INSERT ... SELECT',
                  f"insert into {{t}} select id + 10, n, id + 100, v from {{t}} where id in (1, 2);\n"
                  f"insert into {{t}} select id + 10, n, id + 200, v from {{t}} where id in (1, 2);", )
        self.same('unique failure rolled back in a transaction',
                  f'update {{t}} set v = {self.hexs(12000)} where id = 2;\n'
                  f'insert into {{t}} values (30, 0, 2, {self.hexs(4000)});\nrollback;', autocommit=False)
        self.compare('after index steps')

        # 2. Triggers.
        self.setup(''.join(
            f"create trigger ta_{t} after update on {t} execute insert into log_{t} "
            f"values (obj.id, 'after', sha2(obj.v, 256), octet_length(obj.v));\n"
            f"create trigger tb_{t} before insert on {t} execute insert into log_{t} "
            f"values (new.id, 'before', sha2(new.v, 256), octet_length(new.v));\n"
            f"create trigger tf_{t} after insert on feed_{t} execute insert into {t} "
            f"values (obj.id, 0, obj.id, obj.v);\n" for t in TABLES))
        self.same('trigger reads obj.v', f'update {{t}} set n = n + 1, v = {self.hexs(15000)} where id = 1;\n'
                  'update {t} set n = n + 1 where id in (2, 3, 7);')
        self.same('trigger writes a large value', f'insert into feed_{{t}} values (40, {self.hexs(11000)});\n'
                  f'insert into feed_{{t}} values (41, {self.hexs(2500)});')
        self.same('trigger log', 'select id, ev, h, l from log_{t} order by id, ev, l;', expect_error=False)
        self.compare('after triggers')

        # 3. Representation change.
        self.same('add column', 'alter table {t} add column z int default 5;\nupdate {t} set z = 6 where id = 2;',
                  expect_error=False)
        self.same('read after add column', 'select id, z, sha2(v, 256) h from {t} order by id;', expect_error=False)
        self.same('modify the large column', 'alter table {t} modify v varchar(1000000);', expect_error=False)
        self.same('update after modify', f'update {{t}} set v = {self.hexs(8000)} where id = 3;', expect_error=False)
        self.compare('after representation change')

        # 4. Rolled-back and failed DDL.
        before = self.canon('t_oracle')
        for ddl in ('truncate {t};', 'drop table {t};', 'rename table {t} to {t}_x;'):
            for t in TABLES:
                rc, out = self.sql(ddl.format(t=t) + '\nrollback;', autocommit=False)
                if accept.errors(out):
                    raise accept.HarnessError(f'{ddl!r} failed on {t}: {accept.errors(out)[:2]}')
            self.compare(f'{ddl.split()[0]} rolled back', oracle=before)
        self.same('add a referencing table', 'create table ref_{t} (id int primary key, tid int, '
                  'foreign key (tid) references {t} (id));\ninsert into ref_{t} values (1, 4);', expect_error=False)
        rc, out = self.sql('drop table t_inner;')
        ok = bool(accept.errors(out)) and self.class_exists('t_inner') == 1 and self.class_exists(f't_inner{SUFFIX}') == 1
        self.record('DROP that fails on a foreign key keeps the table and its companion', 'VERIFIED' if ok else 'FAILED')
        self.compare('after failed DROP', oracle=before)
        refused = []
        for ddl in (f'drop table t_inner{SUFFIX};', f'truncate t_inner{SUFFIX};',
                    f'rename table t_inner{SUFFIX} to zz;'):
            rc, out = self.sql(ddl)
            refused.append(bool(accept.errors(out)))
        ok = all(refused) and self.class_exists(f't_inner{SUFFIX}') == 1
        self.record('DDL on the companion class itself is refused', 'VERIFIED' if ok else 'FAILED', f'{refused}')
        self.compare('after refused companion DDL', oracle=before)

        # 5. Committed RENAME survives a restart.
        self.setup('drop table ref_t_oracle;\ndrop table ref_t_inner;\n'
                   'rename table t_oracle to t_oracle_r;\nrename table t_inner to t_inner_r;')
        ok = self.class_exists(f't_inner_r{SUFFIX}') == 1 and self.class_exists(f't_inner{SUFFIX}') == 0
        self.record('RENAME renames the companion class', 'VERIFIED' if ok else 'FAILED')
        accept.sh(['cubrid', 'server', 'stop', self.name], self.log, timeout=300)
        accept.sh(['cubrid', 'server', 'start', self.name], self.log, timeout=300)
        o = self.canon('t_oracle_r')
        i = self.canon('t_inner_r')
        ok = o is not None and o == i and o == before
        self.record('renamed table reads its values after a restart', 'VERIFIED' if ok else 'FAILED')
        self.setup('rename table t_oracle_r to t_oracle;\nrename table t_inner_r to t_inner;')
        before = self.compare('after rename back')

        # 6. Unload and load.
        self.unload_load(before)

        # 7. Standalone mode.
        accept.sh(['cubrid', 'server', 'stop', self.name], self.log, timeout=300)
        sa_dml = (f'update {{t}} set v = {self.hexs(30000)} where id = 2;\n'
                  f'insert into {{t}} (id, n, k, v) values (50, 0, 50, {self.hexs(9000)});\n'
                  'delete from {t} where id = 4;\n')
        for t in TABLES:
            rc, out = self.sql(sa_dml.format(t=t), sa=True)
            if rc or accept.errors(out):
                raise accept.HarnessError(f'standalone DML failed on {t}: {accept.errors(out)[:2]}')
        self.compare('standalone DML', sa=True)
        rc = accept.sh(['cubrid', 'compactdb', '-S', self.name], self.root / 'compactdb.log', check=False)
        self.record('compactdb', 'VERIFIED' if rc == 0 else 'FAILED', f'exit {rc}')
        self.compare('after compactdb', sa=True)
        accept.sh(['cubrid', 'server', 'start', self.name], self.log, timeout=300)
        self.compare('after restart')

        # 8. Committed TRUNCATE and DROP.
        self.setup('truncate t_oracle;\ntruncate t_inner;')
        rc, out = self.sql(f'select count(*) as c from t_inner{SUFFIX};')
        ok = not rc and accept.rows(out) and accept.rows(out)[0]['c'] == '0'
        self.record('TRUNCATE empties the companion', 'VERIFIED' if ok else 'FAILED')
        self.same('insert after TRUNCATE', f'insert into {{t}} (id, n, k, v) values (1, 0, 1, {self.hexs(9000)});',
                  expect_error=False)
        self.compare('after TRUNCATE and insert')
        self.setup('drop table t_oracle;\ndrop table t_inner;')
        ok = self.class_exists(f't_inner{SUFFIX}') == 0
        self.record('DROP removes the companion class', 'VERIFIED' if ok else 'FAILED')
        return self.results

    def unload_load(self, before):
        out_dir = self.root / 'unload'
        out_dir.mkdir()
        accept.sh(['cubrid', 'unloaddb', '-C', '-u', 'dba', self.name], out_dir / 'unload.log', cwd=out_dir)
        objects = (out_dir / f'{self.name}_objects').read_text(errors='replace')
        sections = {}
        cur = None
        for line in objects.splitlines():
            m = re.match(r'%class \[dba\]\.\[(\w+)\]', line)
            if m:
                cur = m.group(1)
                sections[cur] = []
            elif cur:
                sections[cur].append(line)
        ok = (f't_inner{SUFFIX}' not in sections and 't_inner' in sections
              and sorted(sections['t_inner']) == sorted(sections.get('t_oracle', [])))
        self.record('dump holds full values and no chunk rows', 'VERIFIED' if ok else 'FAILED',
                    f'classes {sorted(sections)}')
        name2 = 'ital' + str(int(time.time()))[-9:]
        accept.sh(['cub-workenv', 'create-db', name2], self.log)
        conf = Path(os.environ['CUBRID_CONF_FILE'])
        conf.write_text(conf.read_text() + f'\n[@{name2}]\ninner_table_threshold={THRESHOLD}\n')
        accept.sh(['cubrid', 'server', 'start', name2], self.log, timeout=300)
        self.extra_dbs.append(name2)
        rc = accept.sh(['cubrid', 'loaddb', '-C', '-u', 'dba', '-s', f'{self.name}_schema',
                        '-d', f'{self.name}_objects', name2], out_dir / 'load.log', check=False, cwd=out_dir)
        self.record('loaddb', 'VERIFIED' if rc == 0 else 'FAILED', f'exit {rc}')
        self.compare('loaded database', db=name2, oracle=before)
        rc = accept.sh(['cubrid', 'server', 'stop', name2], self.log, timeout=300, check=False)
        self.extra_dbs.remove(name2)
        rc = accept.sh(['cubrid', 'checkdb', '-S', name2], self.root / 'checkdb-loaded.log', check=False)
        self.record('checkdb of the loaded database', 'VERIFIED' if rc == 0 else 'FAILED', f'exit {rc}')
