#!/usr/bin/env python3
# Copyright 2016 CUBRID Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Exercise standalone workspace writes through loaddb and CSQL.

Requires an installed CUBRID on PATH with the OOS statistics session command.
Uses a private database registry; retained output identifies failed assertions.
"""

import os
from pathlib import Path
import random
import re
import subprocess
import tempfile


class WorkspaceTest:
    def __init__(self):
        self.root = Path(tempfile.mkdtemp(prefix="cubrid-workspace-oos-"))
        self.env = os.environ.copy()
        self.env["CUBRID_DATABASES"] = str(self.root)
        self.env["CUBRID_CONF_FILE"] = str(self.root / "cubrid.conf")
        (self.root / "cubrid.conf").write_text(
            "[common]\ndata_buffer_size=64M\nlog_buffer_size=16M\n"
        )
        self.db = "workspace_oos"
        self.lob = self.root / "lob"
        self.lob.mkdir()
        self.seq = 0
        rng = random.Random(27424)
        self.payload = bytes(rng.randrange(256) for _ in range(5000)).hex()
        print("Evidence:", self.root, flush=True)
        self.run(["cubrid", "createdb", "--db-volume-size=32M", "--log-volume-size=32M",
                  "--db-page-size=16K", "--lob-base-path=" + str(self.lob),
                  "-F", str(self.root), self.db, "en_US.utf8"])

    def run(self, args, data=None, expected_error=None):
        self.seq += 1
        output = self.root / ("%03d.out" % self.seq)
        with output.open("w") as stream:
            result = subprocess.run(args, input=data, text=True, env=self.env, cwd=self.root,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=60)
        text = output.read_text()
        if expected_error:
            assert expected_error.lower() in text.lower(), (args, output, text)
        else:
            assert result.returncode == 0 and "ERROR:" not in text, (args, output, text)
        return text

    def sql(self, data):
        return self.run(["csql", "-S", "-u", "dba", "--no-auto-commit", self.db], data)

    def load(self, data, expected_error=None, options=()):
        fixture = self.root / ("%03d.objects" % self.seq)
        fixture.write_text(data)
        return self.run(["cubrid", "loaddb", "-S", "-u", "dba", "-d", str(fixture),
                         *options, self.db], expected_error=expected_error)

    def check(self, table, predicate, rows, chunks):
        output = self.sql(
            "SELECT CASE WHEN COUNT(*)=%d AND "
            "SUM(CASE WHEN %s THEN 1 ELSE 0 END)=%d "
            "THEN 'VALUE_OK' ELSE 'VALUE_BAD' END AS verdict FROM %s;\n"
            ";oos_stats %s\n" % (rows, predicate, rows, table, table)
        )
        assert "'VALUE_OK'" in output and "'VALUE_BAD'" not in output, output
        match = re.search(r"Live OOS records\s*:\s*(\d+)", output)
        actual = int(match[1]) if match else 0 if "has no OOS file" in output else None
        assert actual == chunks, "%s: expected %d OOS chunks, got %s\n%s" % (table, chunks, actual, output)

    def basic_loader(self):
        self.sql("CREATE TABLE t_load(v BIT VARYING); COMMIT;\n")
        self.load("%%class t_load (v)\nX'%s'\n" % self.payload)
        self.check("t_load", "v=X'%s'" % self.payload, 1, 1)
        print("PASS: standalone loader stores and reads an OOS value", flush=True)

    def workspace_sql(self):
        self.sql("CREATE TABLE t_sql(id INTEGER PRIMARY KEY, v BIT VARYING); COMMIT;\n"
                 "SET SYSTEM PARAMETERS 'insert_execution_mode=0';\n"
                 "INSERT INTO t_sql VALUES (1, X'%s'); COMMIT;\n" % self.payload)
        self.check("t_sql", "id=1 AND v=X'%s'" % self.payload, 1, 1)
        print("PASS: CSQL workspace INSERT uses OOS through a reserved OID", flush=True)

    def references(self):
        self.sql("CREATE TABLE t_refs(id INTEGER PRIMARY KEY, peer t_refs, v BIT VARYING) "
                 "DONT_REUSE_OID; COMMIT;\n")
        self.load("%%id t_refs 1\n%%class t_refs (id peer v)\n"
                  "1: 1 @1|2 X'%s'\n2: 2 @1|1 X'%s'\n" % (self.payload, self.payload))
        self.check("t_refs", "peer.id=3-id AND v=X'%s'" % self.payload, 2, 2)
        print("PASS: forward and backward object references survive OOS demotion", flush=True)

    def partitions(self):
        self.sql("CREATE TABLE t_parts(id INTEGER, v BIT VARYING) "
                 "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN (10), "
                 "PARTITION p1 VALUES LESS THAN MAXVALUE); COMMIT;\n")
        self.load("%%class t_parts (id v)\n1 X'%s'\n11 X'%s'\n" % (self.payload, self.payload))
        self.check("t_parts__p__p0", "id=1 AND v=X'%s'" % self.payload, 1, 1)
        self.check("t_parts__p__p1", "id=11 AND v=X'%s'" % self.payload, 1, 1)
        self.check("t_parts", "v=X'%s'" % self.payload, 2, 0)
        print("PASS: each partition owns its own OOS file", flush=True)

    def rollback(self):
        # The SELECT forces the pending workspace INSERT before observing OOS and rolling back.
        output = self.sql("SET SYSTEM PARAMETERS 'insert_execution_mode=0';\n"
                          "INSERT INTO t_sql VALUES (2, X'%s');\n"
                          "SELECT COUNT(*) FROM t_sql;\n;oos_stats t_sql\nROLLBACK;\n"
                          ";oos_stats t_sql\n" % self.payload)
        assert re.findall(r"Live OOS records\s*:\s*(\d+)", output) == ["2", "1"], output
        self.check("t_sql", "id=1 AND v=X'%s'" % self.payload, 1, 1)
        print("PASS: rollback removes a flushed workspace INSERT's OOS chain", flush=True)

    def update_cleanup(self):
        replacement = "5a" * 5000
        # A row trigger selects the client/workspace UPDATE route, including multi-row flushing.
        self.sql("CREATE TRIGGER refs_update BEFORE UPDATE ON t_refs EXECUTE PRINT 'workspace update';\n"
                 "COMMIT;\n")
        output = self.sql("UPDATE t_refs SET v=X'%s';\nSELECT COUNT(*) FROM t_refs;\n"
                          ";oos_stats t_refs\nROLLBACK;\n;oos_stats t_refs\n" % replacement)
        assert re.findall(r"Live OOS records\s*:\s*(\d+)", output) == ["2", "2"], output
        self.check("t_refs", "peer.id=3-id AND v=X'%s'" % self.payload, 2, 2)
        self.sql("UPDATE t_refs SET v=X'%s'; COMMIT;\n" % replacement)
        self.check("t_refs", "peer.id=3-id AND v=X'%s'" % replacement, 2, 2)
        self.sql("DELETE FROM t_refs; COMMIT;\n")
        output = self.sql(";oos_stats t_refs\n")
        assert re.findall(r"Live OOS records\s*:\s*(\d+)", output) == ["0"], output
        print("PASS: workspace UPDATE rollback, commit, and DELETE preserve chain ownership", flush=True)

    def failed_load(self):
        # One valid new row precedes a duplicate PK. All failed-load writes must roll back.
        self.load("%%class t_sql (id v)\n2 X'%s'\n1 X'%s'\n" % (self.payload, self.payload),
                  expected_error="unique")
        self.check("t_sql", "id=1 AND v=X'%s'" % self.payload, 1, 1)
        print("PASS: failed unique-index load preserves existing rows and leaves no OOS orphan", flush=True)

    def storage_policy(self):
        small = "ab" * 16
        forced = "cd" * 128
        self.sql("CREATE TABLE t_small(id INTEGER, v BIT VARYING); "
                 "CREATE TABLE t_forced(v BIT VARYING STORAGE FORCE_OUTLINE); "
                 "CREATE TABLE t_largest(a BIT VARYING, b BIT VARYING); "
                 "CREATE TABLE t_multi(v BIT VARYING); COMMIT;\n")
        self.load("%%class t_small (id v)\n1 NULL\n2 X''\n3 X'%s'\n"
                  "%%class t_forced (v)\nX'%s'\n"
                  "%%class t_largest (a b)\nX'%s' X'%s'\n"
                  "%%class t_multi (v)\nX'%s'\n" %
                  (small, forced, self.payload[:6000], self.payload[:4000], self.payload * 10))
        self.check("t_small", "(id=1 AND v IS NULL) OR (id=2 AND v=X'') OR (id=3 AND v=X'%s')" % small,
                   3, 0)
        self.check("t_forced", "v=X'%s'" % forced, 1, 1)
        self.check("t_largest", "a=X'%s' AND b=X'%s'" % (self.payload[:6000], self.payload[:4000]), 1, 1)
        output = self.sql(";oos_stats t_largest\n")
        size = re.search(r"Logical data size\s*:\s*(\d+)", output)
        assert size and 3000 < int(size[1]) < 3100, output
        self.check("t_multi", "v=X'%s'" % (self.payload * 10), 1, 4)
        print("PASS: inline/null/empty values, FORCE_OUTLINE, largest-first, and multi-chunk storage", flush=True)

    def filtered_load(self):
        errors = self.root / "ignored-errors.txt"
        errors.write_text("-670\n")  # ER_BTREE_UNIQUE_FAILED
        self.load("%%class t_sql (id v)\n1 X'%s'\n3 X'%s'\n" % (self.payload, self.payload),
                  options=("--error-control-file=" + str(errors),))
        self.check("t_sql", "id IN (1,3) AND v=X'%s'" % self.payload, 2, 2)
        print("PASS: filtered duplicate leaves no OOS orphan and the following row commits", flush=True)

    def external_lobs(self):
        # Seed permanent locators through the query executor. Creating fresh LOBs through
        # workspace INSERT already loses their files on the unmodified base revision.
        self.sql("CREATE TABLE t_lob(id INTEGER, b BLOB, c CLOB, v BIT VARYING); COMMIT;\n"
                 "INSERT INTO t_lob VALUES (1, BIT_TO_BLOB(X'abcd'), CHAR_TO_CLOB('lob value'), X'ab'); "
                 "COMMIT;\n")
        predicate = "id=1 AND BLOB_TO_BIT(b)=X'abcd' AND CLOB_TO_CHAR(c)='lob value' AND v=X'%s'"
        self.check("t_lob", predicate % "ab", 1, 0)
        files = {p: p.read_bytes() for p in self.lob.rglob("*") if p.is_file()}
        assert len(files) == 2, files.keys()
        self.sql("CREATE TRIGGER lob_update BEFORE UPDATE ON t_lob EXECUTE PRINT 'workspace LOB update'; "
                 "COMMIT;\nUPDATE t_lob SET v=X'%s'; SELECT COUNT(*) FROM t_lob; ROLLBACK;\n" % self.payload)
        self.check("t_lob", predicate % "ab", 1, 0)
        assert {p: p.read_bytes() for p in self.lob.rglob("*") if p.is_file()} == files
        self.sql("UPDATE t_lob SET v=X'%s'; COMMIT;\n" % ("5a" * 5000))
        self.check("t_lob", predicate % ("5a" * 5000), 1, 1)
        assert {p: p.read_bytes() for p in self.lob.rglob("*") if p.is_file()} == files
        print("PASS: workspace UPDATE demotion/rollback preserve existing external BLOB/CLOB files", flush=True)

    def partition_move(self):
        self.sql("CREATE TRIGGER parts_update BEFORE UPDATE ON t_parts EXECUTE PRINT 'partition update'; "
                 "COMMIT;\nUPDATE t_parts SET id=12 WHERE id=1; COMMIT;\n")
        output = self.sql(";oos_stats t_parts__p__p0\n")
        assert re.findall(r"Live OOS records\s*:\s*(\d+)", output) == ["0"], output
        self.check("t_parts__p__p1", "id IN (11,12) AND v=X'%s'" % self.payload, 2, 2)
        print("PASS: workspace partition movement transfers OOS ownership", flush=True)

    def no_logging(self):
        original = self.db
        self.db = "ws_oos_nolog"
        try:
            self.run(["cubrid", "createdb", "--db-volume-size=32M", "--log-volume-size=32M",
                      "-F", str(self.root), self.db, "en_US.utf8"])
            self.sql("CREATE TABLE t_nolog(v BIT VARYING); COMMIT;\n")
            self.load("%%class t_nolog (v)\nX'%s'\n" % self.payload, options=("--no-logging",))
            self.check("t_nolog", "v=X'%s'" % self.payload, 1, 1)
            # Deletion is a fresh logged utility invocation, not recovery from a failed no-logging load.
            self.run(["cubrid", "deletedb", self.db])
        finally:
            self.db = original
        print("PASS: successful no-logging load stores and reads OOS", flush=True)


if __name__ == "__main__":
    test = WorkspaceTest()
    test.basic_loader()
    test.workspace_sql()
    test.references()
    test.partitions()
    test.rollback()
    test.update_cleanup()
    test.failed_load()
    test.filtered_load()
    test.external_lobs()
    test.storage_policy()
    test.partition_move()
    test.no_logging()
    test.run(["cubrid", "deletedb", test.db])
