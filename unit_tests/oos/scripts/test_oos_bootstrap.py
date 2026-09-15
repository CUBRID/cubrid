#!/usr/bin/env python3
# Copyright 2026 CUBRID Corporation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.
"""Verify catalog bootstrap bypasses deferred preparation on a fresh 4KB database."""
import os
from pathlib import Path
import re

from test_oos_loader import LoaderFixture


def main():
    fixture = LoaderFixture()
    fixture.isolate_runtime(Path(os.environ["CUBRID"]))
    try:
        fixture.run("create", ["cubrid", "createdb", "--db-page-size=4K", "--db-volume-size=64M",
                               "--log-volume-size=64M", "t17", "en_US.utf8"])
        _, output = fixture.run("catalog-files", ["cubrid", "diagdb", "-d", "1", "t17"])
        assert "(_db_authorization)" in output, "authorization heap missing from diagnostics"
        outlined = [line for line in output.splitlines() if "(_db_authorization), OOS for HFID:" in line]
        assert not outlined, "\n".join(outlined)
        _, output = fixture.run("catalog-users", ["csql", "-S", "-u", "dba", "-c",
            "SELECT COUNT(*) FROM _db_user WHERE name IN ('DBA', 'PUBLIC')", "t17"])
        assert re.findall(r"^\s*(\d+)\s*$", output, re.M) == ["2"], output
        print("PASS fresh 4KB catalog users and complete-record authorization storage", flush=True)
    finally:
        fixture.close()


if __name__ == "__main__":
    main()
