#!/usr/bin/env bash
#
#  Copyright 2026 CUBRID Corporation
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

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/oos_unittestdb_common.sh"

oos_require_env

cubrid server stop "$OOS_UNITTESTDB_NAME" 2>/dev/null || true
if oos_database_exists; then
  cubrid deletedb "$OOS_UNITTESTDB_NAME"
fi

oos_remove_fixture_sections "$(oos_cubrid_conf)"
