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

# Removes the database of test_catalog_record_gaps and the registry that CMake gives this test alone.

set -euo pipefail

: "${CUBRID:?CUBRID must be set}"
: "${CUBRID_DATABASES:?CUBRID_DATABASES must be set}"

db_name=catgapdb

if [ "$(basename "$CUBRID_DATABASES")" != catalog_test_databases ]; then
  printf 'refusing to use %s: not the registry of this test\n' "$CUBRID_DATABASES" >&2
  exit 1
fi

if [ -f "$CUBRID_DATABASES/databases.txt" ]; then
  cubrid deletedb "$db_name" > /dev/null 2>&1 || true
fi
rm -rf "${CUBRID_DATABASES:?}"
