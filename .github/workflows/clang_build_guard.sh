#!/bin/bash
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
if grep -rn 'OR_CHECK_ADD_OVERFLOW\|OR_CHECK_SUB_UNDERFLOW' src/ cubrid-cci/src/ \
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
if ! grep -q 'Wno-c++11-narrowing' cubridmanager/server/CMakeLists.txt; then
  report "cubridmanager lost -Wno-c++11-narrowing; its bundled miniz does not compile under Clang."
fi

if [ $fail -eq 0 ]; then
  echo "clang build guard: OK"
fi
exit $fail
