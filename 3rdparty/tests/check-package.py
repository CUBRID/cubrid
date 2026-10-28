#!/usr/bin/env python3
"""Exercise the real consumer's success and failure paths without network builds."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
prefix = Path(sys.argv[1]).resolve()
state = repo / ".thirdparty" / "package-checks"
state.mkdir(parents=True, exist_ok=True)
results = []
with tempfile.TemporaryDirectory(dir=state) as temporary:
    root = Path(temporary)
    source = root / "source"
    (source / "modules").mkdir(parents=True)
    (source / "modules/ExternalProject.cmake").write_text(
        'message(FATAL_ERROR "UNEXPECTED_EXTERNAL_PROJECT")\n'
    )
    (source / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.21)\n'
        'project(PackageCheck LANGUAGES C CXX)\n'
        'list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/modules")\n'
        f'add_subdirectory("{repo}/3rdparty" dependencies)\n'
    )
    for name, expected in (
        ("valid", None),
        ("missing-prefix", "Could not find a package configuration file"),
        ("false-prefix", "Could not find a package configuration file"),
        ("missing-library", "Incomplete third-party prefix"),
        ("missing-header", "Incomplete third-party prefix"),
        ("missing-odbc-header", "Incomplete third-party prefix"),
        ("missing-soname", "Incomplete third-party prefix"),
        ("missing-license", "Incomplete third-party prefix"),
        ("missing-source", "Incomplete third-party prefix"),
        ("wrong-recipe", "recipe/toolchain mismatch"),
    ):
        candidate = root / name
        if name not in ("missing-prefix", "false-prefix"):
            shutil.copytree(prefix, candidate, symlinks=True, copy_function=os.link)
        if name == "missing-library":
            (candidate / "lib/libre2.a").unlink()
        elif name == "missing-header":
            (candidate / "include/rapidjson/document.h").unlink()
        elif name == "missing-odbc-header":
            (candidate / "include/sqltypes.h").unlink()
        elif name == "missing-soname":
            (candidate / "lib/libodbc.so.2").unlink()
        elif name == "missing-license":
            (candidate / "licenses/libodbc_license.txt").unlink()
        elif name == "missing-source":
            next((candidate / "sources/unixODBC").glob("*.tar.gz")).unlink()
        elif name == "wrong-recipe":
            config = candidate / "lib/cmake/CubridThirdParty/CubridThirdPartyConfig.cmake"
            contents = config.read_text()
            config.unlink()  # Never modify the original prefix through a hard link.
            config.write_text(contents + '\nset(CubridThirdParty_IDENTITY "wrong")\n')
        result = subprocess.run(
            ["cmake", "-S", str(source), "-B", str(root / (name + "-build")),
             "-G", "Ninja",
             f"-DCMAKE_C_COMPILER={os.environ.get('CUBRID_3RDPARTY_CC', '/usr/bin/gcc')}",
             f"-DCMAKE_CXX_COMPILER={os.environ.get('CUBRID_3RDPARTY_CXX', '/usr/bin/g++')}",
             f"-DCUBRID_3RDPARTY_ROOT:PATH={'OFF' if name == 'false-prefix' else candidate}"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        (state / (name + ".log")).write_text(result.stdout)
        passed = (
            (result.returncode == 0 if expected is None else
             result.returncode != 0 and expected in result.stdout)
            and "UNEXPECTED_EXTERNAL_PROJECT" not in result.stdout
        )
        results.append({"case": name, "passed": passed, "exit_code": result.returncode})
print(json.dumps(results, indent=2))
sys.exit(0 if all(item["passed"] for item in results) else 1)
