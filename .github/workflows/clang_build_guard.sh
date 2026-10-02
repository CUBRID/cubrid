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
# Tripwire for the changes that make a Clang build meaningful. Each of these can be
# undone by an ordinary-looking edit, and none of them fails any existing CI job,
# because no CI job builds with Clang. Keep this cheap so it can run on every PR.
#
# Pair it with a real Clang build job when CI budget allows; this only catches the
# known ways the fixes get reverted, not new ones.

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
