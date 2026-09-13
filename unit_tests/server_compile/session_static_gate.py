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
"""session_static_gate.py - regression gate for process-global state in the
folded client half (workspace#259 axis 2, audit 0-8).

In the merged server the CAS speaker and the SQL client half run once per
session thread inside cub_server, so a file-scope `static` that used to be
per-process is now shared by every session.  Such state must be made
session-local (thread_local / CAS_TLS / DDL_TLS or a csc_*() redirect macro),
and a missed one compiles without a warning - the audit's 0-2 xa_prepare_flag
was exactly that.

This gate lists every file-scope, non-const, non-TLS static VARIABLE in the
client-half source scope below and compares the list against the committed
baseline (session_static_baseline.txt).  Anything NEW fails the gate: either
make it session-local, or - when it is legitimately process-wide (a once-init
cache, a sync primitive, a read-only table) - add it to the baseline with
`--update` and say why in the commit.  The baseline itself is the audit's
open list; shrinking it is progress, growing it needs a reason.

Heuristics (deliberate, keep the tool simple):
  * only lines that START with `static` (file scope); block-scope statics,
    non-static globals and multi-line declarations are not seen
  * `#if defined(SERVER_MODE) ... #else ... #endif` is followed: the non-server
    branch is skipped (and the server branch of `#if !defined(SERVER_MODE)`)
  * `const` anywhere in the declaration exempts it (also `static const char *p`,
    a mutable pointer to const - accepted false negative)
  * function definitions/prototypes are skipped; function-pointer variables
    `static T (*name) (...)` are kept
  * sync primitives (std::mutex/atomic/once_flag, pthread_*_t) are exempt

usage: session_static_gate.py [--update] [--root <cubrid checkout>]
exit:  0 gate passes, 1 new entries, 2 usage/error
"""
import os
import re
import sys
import glob

SCOPE = [
    "src/broker/cas_*.c", "src/broker/cas_*.cpp",
    "src/compat/*.c", "src/compat/*.cpp",
    "src/object/*.c", "src/object/*.cpp",
    "src/transaction/*_cl.c", "src/transaction/*_cl.cpp",
    "src/parser/*.c", "src/parser/*.cpp",
    "src/optimizer/*.c", "src/optimizer/histogram/*.cpp",
    "src/query/*_cl.c", "src/query/*_cl.cpp",
    "src/base/language_support.c",
    "src/connection/driver_session.cpp",
    "src/executables/csql*.c", "src/executables/csql*.cpp",
    "src/method/*.cpp", "src/sp/*_cl.cpp",
]

TLS_WORDS = re.compile(r"\b(thread_local|_Thread_local|__thread|CAS_TLS|DDL_TLS|[A-Z_]+_TLS)\b")
SYNC_WORDS = re.compile(r"\b(std::(mutex|recursive_mutex|shared_mutex|atomic\w*|once_flag|condition_variable)|"
                        r"pthread_(mutex|once|rwlock|cond|key)_t|std::atomic<)")
DECL = re.compile(r"^static\s+(?P<body>[^;{}]*?)\s*(?:=|;)")
NAME = re.compile(r"(?:\(\s*\*\s*)?\b([A-Za-z_]\w*)\s*(?:\)\s*\([^)]*\))?\s*(?:\[[^\]]*\]\s*)*$")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


SERVER_IF = re.compile(r"^\s*#\s*(?:if\s+(?P<neg>!)?\s*defined\s*\(?\s*SERVER_MODE\s*\)?|ifdef\s+SERVER_MODE|ifndef\s+(?P<neg2>SERVER_MODE))\s*$")


def scan_file(path):
    found = []
    text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
    # preprocessor stack: each entry is None (unrelated #if) or a bool "this
    # branch is compiled in the server"; lines are skipped while any entry is False
    stack = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("#"):
            m = SERVER_IF.match(s)
            if m:
                stack.append(m.group("neg") is None and m.group("neg2") is None)
            elif re.match(r"#\s*if", s):
                stack.append(None)
            elif re.match(r"#\s*(else|elif)\b", s) and stack:
                if stack[-1] is not None:
                    stack[-1] = not stack[-1]
            elif re.match(r"#\s*endif\b", s) and stack:
                stack.pop()
            continue
        if any(v is False for v in stack):
            continue
        if not line.startswith("static"):
            continue
        m = DECL.match(line)
        if not m:
            continue
        body = m.group("body")
        if re.search(r"\bconst\b", body) or TLS_WORDS.search(body) or SYNC_WORDS.search(body):
            continue
        # function definition / prototype: a '(' that is not a function-pointer declarator
        if "(" in body and not re.search(r"\(\s*\*\s*[A-Za-z_]\w*\s*\)", body):
            continue
        if "\\" in body or "struct {" in body:
            continue
        nm = NAME.search(body.strip())
        if not nm:
            continue
        found.append(nm.group(1))
    return found


def main(argv):
    update = "--update" in argv
    root = None
    if "--root" in argv:
        root = argv[argv.index("--root") + 1]
    else:
        root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    baseline_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "session_static_baseline.txt")

    current = set()
    for pattern in SCOPE:
        for path in sorted(glob.glob(os.path.join(root, pattern))):
            rel = os.path.relpath(path, root)
            for name in scan_file(path):
                current.add(f"{rel}:{name}")

    if update:
        with open(baseline_path, "w", encoding="utf-8") as f:
            f.write("# file-scope non-const, non-TLS statics in the folded client half - see session_static_gate.py\n")
            for entry in sorted(current):
                f.write(entry + "\n")
        print(f"session_static_gate: baseline rewritten, {len(current)} entries")
        return 0

    if not os.path.exists(baseline_path):
        print(f"session_static_gate: no baseline at {baseline_path} (run with --update)", file=sys.stderr)
        return 2
    baseline = {l.strip() for l in open(baseline_path, encoding="utf-8") if l.strip() and not l.startswith("#")}

    new = sorted(current - baseline)
    gone = sorted(baseline - current)
    if gone:
        print(f"session_static_gate: {len(gone)} baseline entries no longer present (run --update to prune):")
        for e in gone:
            print("  - " + e)
    if new:
        print(f"session_static_gate: FAIL - {len(new)} NEW process-global static(s) in the folded client half:")
        for e in new:
            print("  + " + e)
        print("  make each one session-local (thread_local / CAS_TLS / csc_*() redirect), or justify it and --update")
        return 1
    print(f"session_static_gate: PASS ({len(current)} known entries, 0 new)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
