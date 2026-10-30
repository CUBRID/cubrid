#!/usr/bin/env python3
#
#  Copyright 2008 Search Solution Corporation
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
"""Run with python3 cmake/tests/test_build_version.py (CMake, Git, GCC, ccache)."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]


def run(*args, cwd, env=None):
    result = subprocess.run(args, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise AssertionError(f"{' '.join(map(str, args))}\n{result.stdout}")
    return result.stdout.strip()


class BuildVersionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cubrid-version-test-")
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith(("CCACHE_", "GIT_"))}
        self.env.update(GIT_AUTHOR_NAME="Version test",
                        GIT_AUTHOR_EMAIL="version@example.invalid",
                        GIT_COMMITTER_NAME="Version test",
                        GIT_COMMITTER_EMAIL="version@example.invalid")

    def test_common_header_cache(self):
        """A new build identity must preserve an unrelated -ggdb3 cache entry."""
        source = self.path / "source"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.21)
project(version_headers C)
include(CheckTypeSize)
check_type_size(char SIZEOF_CHAR)
check_type_size(short SIZEOF_SHORT)
check_type_size(int SIZEOF_INT)
check_type_size(long SIZEOF_LONG)
check_type_size("long long" SIZEOF_LONG_LONG)
check_type_size("void *" SIZEOF_VOID_P)
set(HAVE_STDBOOL_H 1)
set(CUBRID_MAJOR_VERSION 11)
set(CUBRID_MINOR_VERSION 5)
set(CUBRID_PATCH_VERSION 0)
set(CUBRID_EXTRA_VERSION ${{REVISION}})
set(BUILD_NUMBER 11.5.0.${{REVISION}})
set(CUBRID_VERSION 11.5.0.${{REVISION}}-hash${{REVISION}})
set(RELEASE_STRING 11.5.0)
set(MAJOR_RELEASE_STRING 11.5.0)
configure_file("{ROOT}/cmake/config.h.cmake" config.h)
configure_file("{ROOT}/cmake/version.h.cmake" version.h)
configure_file("{ROOT}/cmake/build_version.h.cmake" build_version.h)
''')
        (source / "consumer.c").write_text(
            '#include "config.h"\nint value(void) { return 42; }\n')
        cache_env = dict(self.env, CCACHE_DIR=str(self.path / "cache"),
                         CCACHE_CONFIGPATH=str(self.path / "ccache.conf"),
                         CCACHE_LOGFILE=str(self.path / "ccache.log"))
        (self.path / "ccache.conf").write_text("direct_mode = true\n")
        build = self.path / "build"
        for revision in (1, 2):
            run("cmake", "-S", str(source), "-B", str(build),
                f"-DREVISION={revision}", cwd=self.path, env=self.env)
            (self.path / "ccache.log").write_text("")
            run("ccache", "gcc", "-ggdb3", f"-I{build}",
                f"-I{ROOT / 'include'}", "-c", str(source / "consumer.c"),
                "-o", str(self.path / "consumer.o"), cwd=self.path, env=cache_env)
        log = (self.path / "ccache.log").read_text()
        results = "\n".join(line for line in log.splitlines() if "Result:" in line)
        self.assertRegex(results, r"Result: (direct_cache_hit|preprocessed_cache_hit|"
                             r"cache hit \((direct|preprocessed)\))")

    def test_git_refresh(self):
        """Build-only refresh works with loose/packed refs and linked worktrees."""
        for generator, tool in (("Ninja", "ninja"), ("Unix Makefiles", "make")):
            if not shutil.which(tool):
                continue
            for linked in (False, True):
                with self.subTest(generator=generator, linked=linked):
                    self.check_git_refresh(generator, linked)

    def check_git_refresh(self, generator, linked):
        case = self.path / (generator.replace(" ", "_") + str(linked))
        case.mkdir()
        repo = case / "repo with spaces"
        repo.mkdir()
        run("git", "init", cwd=repo, env=self.env)
        (repo / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.21)
project(git_revision NONE)
find_package(Git REQUIRED)
include("{ROOT}/cmake/TrackGitRevision.cmake")
cubrid_watch_git_revision()
execute_process(COMMAND "${{GIT_EXECUTABLE}}" rev-parse HEAD
  WORKING_DIRECTORY "${{CMAKE_SOURCE_DIR}}"
  OUTPUT_VARIABLE REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
configure_file(revision.in revision.txt @ONLY)
''')
        (repo / "revision.in").write_text("@REVISION@\n")
        run("git", "add", ".", cwd=repo, env=self.env)
        self.commit(repo)
        if linked:
            source = case / "linked with spaces"
            run("git", "worktree", "add", "-b", "linked", str(source),
                cwd=repo, env=self.env)
        else:
            source = repo
        # No reflog: a packed-only branch must detect creation of its loose ref.
        run("git", "config", "core.logAllRefUpdates", "false", cwd=repo, env=self.env)
        run("git", "pack-refs", "--all", cwd=repo, env=self.env)
        build = case / "build"
        run("cmake", "-S", str(source), "-B", str(build), "-G", generator,
            cwd=case, env=self.env)
        for transition in ("packed", "loose", "detached"):
            time.sleep(1.1)  # Makefile timestamp resolution.
            if transition == "detached":
                run("git", "checkout", "--detach", cwd=source, env=self.env)
            self.commit(source)
            run("cmake", "--build", str(build), cwd=case, env=self.env)
            expected = run("git", "rev-parse", "HEAD", cwd=source, env=self.env)
            self.assertEqual((build / "revision.txt").read_text().strip(), expected)
        mtime = (build / "revision.txt").stat().st_mtime_ns
        run("git", "checkout", "-b", "same-revision", cwd=source, env=self.env)
        run("cmake", "--build", str(build), cwd=case, env=self.env)
        self.assertEqual((build / "revision.txt").stat().st_mtime_ns, mtime)

    def commit(self, repo):
        run("git", "-c", "commit.gpgsign=false", "commit", "--allow-empty",
            "-m", "version test", cwd=repo, env=self.env)


if __name__ == "__main__":
    unittest.main(verbosity=2)
