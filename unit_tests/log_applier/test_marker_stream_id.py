#!/usr/bin/env python3
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
#

"""Standalone catalog/migration tests; no HA servers or existing databases are used.

Run after building cub_admin/csql in debug mode:
  python3 test_marker_stream_id.py --build-dir /path/to/build_debug \
      --resource-root /path/to/CUBRID
The temporary database, SQL and logs are retained at the printed location.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--resource-root", required=True, type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    resources = args.resource_root.resolve()
    repo = Path(__file__).resolve().parents[2]
    work = Path(tempfile.mkdtemp(prefix="cubrid-marker-stream-id-"))
    runtime = work / "runtime"
    runtime.mkdir()
    (runtime / "lib").mkdir()
    (runtime / "upgrade" / "internal").mkdir(parents=True)
    (work / "conf").mkdir()
    (work / "db").mkdir()
    (work / "log").mkdir()
    (work / "databases.txt").write_text("")
    (work / "conf" / "cubrid.conf").write_text("[common]\nha_mode=off\nmax_clients=10\n")
    (runtime / "conf").symlink_to(work / "conf", target_is_directory=True)
    (runtime / "share").mkdir()
    (runtime / "share" / "upgrade").symlink_to(runtime / "upgrade", target_is_directory=True)
    for entry in (resources / "share").iterdir():
        if entry.name != "upgrade":
            (runtime / "share" / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    for entry in resources.iterdir():
        if entry.name not in ("lib", "upgrade", "conf", "log", "databases", "share"):
            (runtime / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    for entry in (resources / "lib").iterdir():
        (runtime / "lib" / entry.name).symlink_to(entry)
    # Launchers may dlopen an absolute CUBRID/lib path: always use the rebuilt libraries.
    for directory, stem in (("sa", "libcubridsa.so"), ("cs", "libcubridcs.so")):
        for library in (build / directory).glob(stem + "*"):
            target = runtime / "lib" / library.name
            if target.exists() or target.is_symlink():
                target.unlink()
            target.symlink_to(library)
    for entry in (repo / "upgrade").iterdir():
        if entry.is_file():
            (runtime / "upgrade" / entry.name).symlink_to(entry)
    env = os.environ.copy()
    env.update(CUBRID=str(runtime), CUBRID_CONF=str(work / "conf"), CUBRID_DATABASES=str(work))
    env["LD_LIBRARY_PATH"] = ":".join(str(build / p) for p in ("sa", "cs", "cubrid-cci/cci"))
    env["JAVA_TOOL_OPTIONS"] = (env.get("JAVA_TOOL_OPTIONS", "") + " -XX:ActiveProcessorCount=2").strip()
    checks = []
    print("Test artifacts:", work, flush=True)

    def command(label, command_args, failure=False):
        result = subprocess.run(command_args, cwd=str(work), env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                universal_newlines=True, timeout=60)
        (work / (label + ".log")).write_text(result.stdout)
        checks.append({"test": label, "exit_code": result.returncode,
                       "expected_failure": failure})
        (work / "results.json").write_text(json.dumps(checks, indent=2) + "\n")
        if (result.returncode == 0) == failure:
            raise RuntimeError(label + " failed; inspect " + str(work / (label + ".log")))
        print("PASS", label, flush=True)
        return result.stdout

    def sql(label, contents, failure=False):
        name = label + ".sql"
        (runtime / "upgrade" / "internal" / name).write_text(contents)
        listing = work / (label + ".list")
        listing.write_text(name + "\n")
        return command(label, [str(build / "bin/cub_admin"), "upgradedb", "--force",
                               "--apply-script-list", str(listing), "markerids"], failure)

    def require(condition):
        return "SELECT 1 / CASE WHEN (" + condition + ") THEN 1 ELSE 0 END FROM db_root;\n"

    def insert_info(path):
        return ("INSERT INTO _db_ha_apply_info "
                "(stream_id, db_name, copied_log_path, committed_lsa_pageid, committed_lsa_offset, "
                "required_lsa_pageid, required_lsa_offset) VALUES "
                "((SELECT last_stream_id FROM CLASS _db_ha_apply_info), 'testdb', '" + path +
                "', 10, 0, 1, 0);\n")

    allocate = "UPDATE CLASS _db_ha_apply_info SET last_stream_id = last_stream_id + 1;\n"
    command("create", [str(build / "bin/cub_admin"), "createdb", "--db-volume-size=64M",
                       "--log-volume-size=32M", "-F", str(work / "db"), "-L", str(work / "log"),
                       "markerids", "en_US.utf8"])
    sql("allocate-two", allocate + insert_info("/copy/a") + allocate + insert_info("/copy/b") +
        "INSERT INTO _db_ha_applied_tx_marker (stream_id, commit_pageid, commit_offset) VALUES (1, 100, 1), (2, 100, 1);\n" +
        require("(SELECT COUNT(*) FROM _db_ha_apply_info WHERE stream_id IN (1,2)) = 2"))
    sql("restart-identity", require("(SELECT last_stream_id FROM CLASS _db_ha_apply_info) = 2") +
        require("(SELECT COUNT(*) FROM _db_ha_applied_tx_marker) = 2"))
    duplicate = sql("duplicate-marker", "INSERT INTO _db_ha_applied_tx_marker (stream_id, commit_pageid, commit_offset) VALUES (1,100,1);", failure=True)
    assert "unique constraint" in duplicate.lower(), duplicate
    sql("own-stream-cleanup", "DELETE FROM _db_ha_applied_tx_marker WHERE stream_id=1;\n" +
        require("(SELECT COUNT(*) FROM _db_ha_applied_tx_marker WHERE stream_id=2)=1"))
    sql("no-id-reuse", "DELETE FROM _db_ha_applied_tx_marker; DELETE FROM _db_ha_apply_info;\n" +
        allocate + insert_info("/copy/a") + require("(SELECT stream_id FROM _db_ha_apply_info)=3"))
    sql("overflow-setup", "UPDATE CLASS _db_ha_apply_info SET last_stream_id=9223372036854775807;")
    overflow = sql("overflow-rejected", allocate, failure=True)
    assert "overflow" in overflow.lower(), overflow
    sql("overflow-unchanged", require("(SELECT last_stream_id FROM CLASS _db_ha_apply_info) = "
                                     "9223372036854775807"))
    # upgrade/v1_to_v2.sql upgrades a database that has no parallel-apply catalogs yet.
    before_pr = """
DELETE FROM _db_ha_apply_info;
DROP TABLE _db_ha_applied_tx_marker;
DROP TABLE _db_ha_apply_stat;
DROP TABLE _db_ha_apply_worker_stat;
ALTER TABLE _db_ha_apply_info DROP COLUMN stream_id;
ALTER TABLE _db_ha_apply_info DROP ATTRIBUTE CLASS last_stream_id;
INSERT INTO _db_ha_apply_info (db_name,copied_log_path,committed_lsa_pageid,committed_lsa_offset,
 required_lsa_pageid,required_lsa_offset) VALUES ('testdb','/copy/a',10,0,1,0),('testdb','/copy/b',10,0,1,0);
"""
    sql("base-db-upgrade", before_pr + (repo / "upgrade/v1_to_v2.sql").read_text() +
        require("(SELECT COUNT(*) FROM _db_ha_apply_info WHERE stream_id>0)=2") +
        allocate + insert_info("/copy/c") + require("(SELECT MAX(stream_id) FROM _db_ha_apply_info)=3"))
    print("All", len(checks), "standalone checks passed. No HA or throughput measurements were run.")


if __name__ == "__main__":
    main()
