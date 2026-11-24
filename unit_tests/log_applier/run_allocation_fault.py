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
"""Compile one production-including TU in /tmp using an existing CS command; no Ninja builds or downloads.

The existing object_representation.c.o supplies the actual string unpack implementation so GNU --wrap=malloc
can inject its allocation failure, which wrapping only the executable's shared-library call cannot do.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--resource-root", type=Path,
                        help="Existing installed resources; defaults to BUILD/_install/CUBRID")
    parser.add_argument("--asan", action="store_true")
    parser.add_argument("--disable-lsan", action="store_true",
                        help="Use ASan without leak checking when LSan is unavailable under ptrace")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    resources = (args.resource_root or (build / "_install/CUBRID")).resolve()
    source = Path(__file__).resolve().with_name("test_log_applier_allocation_fault.cpp")
    root = source.parents[2]
    work = Path(tempfile.mkdtemp(prefix="cubrid-applier-allocation-fault-"))
    print("Artifacts:", work, flush=True)
    target = "cs/CMakeFiles/cubridcs.dir/__/src/transaction/log_applier.c.o"
    commands = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", target],
                                       universal_newlines=True)
    matches = [line for line in commands.splitlines() if " -c " in line and line.endswith("/log_applier.c")]
    if len(matches) != 1:
        raise RuntimeError("Expected exactly one CS log_applier compile command")
    original = shlex.split(matches[0])
    command = []
    i = 0
    while i < len(original):
        arg = original[i]
        if arg in ("-o", "-c", "-MF", "-MT", "-MQ"):
            i += 2
            continue
        if arg in ("-MD", "-MMD"):
            i += 1
            continue
        command.append(arg)
        i += 1
    compiler = command[0]
    obj = work / "allocation_fault.o"
    binary = work / "allocation_fault"
    sanitizers = ["-fsanitize=address", "-fno-omit-frame-pointer"] if args.asan else []
    command += ["-I" + str(root / "src/transaction/applier"), "-ffunction-sections", "-fdata-sections"]
    command += sanitizers + ["-c", str(source), "-o", str(obj)]
    packing_obj = build / "cs/CMakeFiles/cubridcs.dir/__/src/object/object_representation.c.o"
    library = build / "cs/libcubridcs.so"
    if not packing_obj.is_file() or not library.is_file():
        raise RuntimeError("Existing CS library and object_representation object are required; this runner never builds them")
    link = [compiler, str(obj), str(packing_obj), "-Wl,--gc-sections", "-Wl,--wrap=realloc",
            "-Wl,--wrap=malloc", "-L" + str(library.parent), "-Wl,-rpath," + str(library.parent),
            "-lcubridcs", "-pthread", "-ldl"] + sanitizers + ["-o", str(binary)]
    manifest = {"original_cs_command": original, "compile": command, "link": link,
                "source_sha256": hashlib.sha256((root / "src/transaction/log_applier.c").read_bytes()).hexdigest(),
                "test_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "packing_source_sha256": hashlib.sha256((root / "src/object/object_representation.c").read_bytes()).hexdigest(),
                "packing_object_sha256": hashlib.sha256(packing_obj.read_bytes()).hexdigest(),
                "library_sha256_before_link": hashlib.sha256(library.read_bytes()).hexdigest(),
                "packing_object": str(packing_obj), "resources": str(resources), "asan": args.asan,
                "leak_check_disabled": args.disable_lsan,
                "limits": "Only the included TU is instrumented by --asan; prebuilt CS/object code is not rebuilt."}
    (work / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    for label, cmd in (("compile", command), ("link", link)):
        with (work / (label + ".log")).open("w") as log:
            result = subprocess.run(cmd, cwd=str(build), stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(label + " failed; see " + str(work / (label + ".log")))
        print("PASS", label, flush=True)
    env = os.environ.copy()
    env["CUBRID"] = str(resources)
    if args.disable_lsan:
        env["ASAN_OPTIONS"] = env.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
    env["LD_LIBRARY_PATH"] = str(library.parent) + ":" + str(build / "cubrid-cci/cci") + ":" + env.get("LD_LIBRARY_PATH", "")
    result = subprocess.run([str(binary)], cwd=str(work), env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, universal_newlines=True, timeout=30)
    (work / "run.log").write_text(result.stdout)
    manifest["returncode"] = result.returncode
    manifest["source_sha256_after_run"] = hashlib.sha256((root / "src/transaction/log_applier.c").read_bytes()).hexdigest()
    manifest["library_sha256_after_run"] = hashlib.sha256(library.read_bytes()).hexdigest()
    (work / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(result.stdout, end="", flush=True)
    if result.returncode:
        raise RuntimeError("Fault experiment failed; see " + str(work / "run.log"))


if __name__ == "__main__":
    main()
