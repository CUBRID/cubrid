#!/bin/bash
#
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
# Why this file exists
# --------------------
# No CI job builds CUBRID with Clang. Every change in CBRD-26726 that makes such a
# build possible, or that fixes what it reported, can therefore be undone by an
# ordinary-looking edit without a single job turning red. One of those changes is the
# -w in build.sh: putting it back silences every warning again, including the
# -Werror=format-security that CMakeLists.txt sets, and nothing would say so.
#
# This script is a cheap text tripwire for exactly those changes, so it can run on
# every PR. Each rule below is labelled R1, R2 ... and the comment above it says which
# change it protects and what breaks if that change is reverted. Every rule was checked
# by injecting the regression it is meant to catch and confirming that it fails.
#
# What it does NOT do: it cannot recognise the defect class in general. The same macro
# that is a bug on a signed operand is correct on an unsigned one, and text alone cannot
# tell the operand types apart - a rule written that way flagged 15 of 17 correct uses.
# Catching new occurrences needs a build with -fsanitize=signed-integer-overflow, which
# is worth adding as a nightly job. This script only catches the known ways these
# particular fixes get reverted.

set -u
fail=0
report () { echo "FAIL: $1"; fail=1; }

# R1 -w disables every warning enabled after it, including -Werror=format-security.
if grep -nE '^\s*export (C|CXX)FLAGS=.*(^|[^o])-w( |"|$)' build.sh; then
  report "build.sh re-introduces -w in the Clang flags; every warning would be silenced."
fi

# R2 the overflow checks must not go back to inspecting an already-overflowed sum.
overflow_scan_dirs="src/"
[ -d cubrid-cci/src ] && overflow_scan_dirs="$overflow_scan_dirs cubrid-cci/src/"
if grep -rn 'OR_CHECK_ADD_OVERFLOW\|OR_CHECK_SUB_UNDERFLOW' $overflow_scan_dirs \
     --include='*.c' --include='*.cpp' --include='*.h' --include='*.hpp' \
     | grep -v 'object_representation\.h'; then
  report "signed overflow is being detected by inspecting the result again (undefined behaviour); use OR_ADD_OVERFLOW / OR_SUB_OVERFLOW."
fi

# R3 a replacement operator delete may not be inline, and its definition must be built.
if grep -nE '^\s*inline void operator delete' src/base/memory_wrapper.hpp; then
  report "operator delete is inline again; the replacement goes missing in a release build."
fi
if ! grep -q 'memory_wrapper\.cpp' cubrid/CMakeLists.txt; then
  report "cubrid/CMakeLists.txt no longer builds memory_wrapper.cpp; operator delete would be left unreplaced."
fi

# R4 integer limits must not be compared against a float/double directly.
if grep -rn 'OR_CHECK_INT_OVERFLOW (db_get_float\|OR_CHECK_BIGINT_OVERFLOW (db_get_float\|OR_CHECK_BIGINT_OVERFLOW (db_get_double' src/ \
     --include='*.c' --include='*.cpp'; then
  report "an integer limit is compared against a floating point value; use the *_FROM_FP form."
fi

# R7 the date and time arithmetic in these two files must not go back to forming a
# signed sum and then handing it to the unsigned wraparound test. A general rule is not
# possible here: the same macro is correct for the genuinely unsigned callers that remain,
# and telling the two apart needs the operand types, not the text. What is checkable is
# that the places that were converted stay converted, so this pins the files where no
# correct use is left. The general case belongs to a sanitiser build, not to grep.
if grep -n 'OR_CHECK_UNS_ADD_OVERFLOW' src/parser/type_checking.c | grep -vE ':[[:space:]]*\*|/\*'; then
  report "type_checking.c forms a signed sum and checks it with the unsigned test again; use OR_ADD_OVERFLOW / OR_SUB_OVERFLOW."
fi
if grep -rn 'bi &= bi - 1\|i &= i - 1\|s &= s - 1' src/query/arithmetic.c; then
  report "arithmetic.c clears the low bit on a signed value again; that is undefined at the type minimum."
fi

# R5 Clang leaves __atomic_* libcalls behind at -O0.
if ! grep -q 'LIBATOMIC_LIBRARY' CMakeLists.txt; then
  report "CMakeLists.txt no longer looks for libatomic; a Clang debug build will not link."
fi

# R6 cubridmanager resets the compile flags, so it needs its own narrowing opt-out.
# This one is advisory, not a failure. cubridmanager is a submodule, and its pointer is bumped
# in a commit of its own after the submodule change merges, so this repository legitimately
# points at a revision without the flag for a while. The binding check belongs in that repository.
if [ -f cubridmanager/server/CMakeLists.txt ] \
   && ! grep -q 'Wno-c++11-narrowing' cubridmanager/server/CMakeLists.txt; then
  echo "NOTE: the cubridmanager revision this points at has no -Wno-c++11-narrowing;"
  echo "      its bundled miniz does not compile under Clang until the submodule pointer is bumped."
fi

if [ $fail -eq 0 ]; then
  echo "clang build guard: OK"
fi
exit $fail
