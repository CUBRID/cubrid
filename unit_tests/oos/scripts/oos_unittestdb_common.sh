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

readonly OOS_UNITTESTDB_NAME="unittestdb"
readonly OOS_UNITTESTDB_MARKER_BEGIN="# BEGIN OOS unittestdb fixture"
readonly OOS_UNITTESTDB_MARKER_END="# END OOS unittestdb fixture"

oos_require_env ()
{
  : "${CUBRID:?CUBRID must be set}"
  : "${CUBRID_DATABASES:?CUBRID_DATABASES must be set}"
}

oos_cubrid_conf ()
{
  printf '%s/conf/cubrid.conf\n' "$CUBRID"
}

oos_unittestdb_dir ()
{
  printf '%s/%s\n' "$CUBRID_DATABASES" "$OOS_UNITTESTDB_NAME"
}

oos_database_exists ()
{
  local databases_txt="$CUBRID_DATABASES/databases.txt"

  [ -f "$databases_txt" ] || return 1
  awk -v db="$OOS_UNITTESTDB_NAME" '$1 == db { found = 1 } END { exit found ? 0 : 1 }' "$databases_txt"
}

oos_remove_fixture_sections ()
{
  local conf="$1"
  local tmp_marker
  local tmp_legacy

  [ -f "$conf" ] || return 0

  tmp_marker=$(mktemp "${conf}.oos-marker.XXXXXX")
  tmp_legacy=$(mktemp "${conf}.oos-legacy.XXXXXX")

  awk -v begin="$OOS_UNITTESTDB_MARKER_BEGIN" -v end="$OOS_UNITTESTDB_MARKER_END" '
    $0 == begin { in_fixture = 1; next }
    in_fixture {
      if ($0 == end)
	{
	  in_fixture = 0;
	}
      next;
    }
    { print }
  ' "$conf" > "$tmp_marker"

  awk '
    {
      lines[++n] = $0;
    }
    END {
      while (n > 0 && lines[n] == "")
	{
	  blanks[++blank_count] = lines[n];
	  n--;
	}

      if (n >= 2 && lines[n - 1] == "[@unittestdb]" && lines[n] == "vacuum_log_block_pages=4")
	{
	  n -= 2;
	}

      for (i = 1; i <= n; i++)
	{
	  print lines[i];
	}
      for (i = blank_count; i >= 1; i--)
	{
	  print blanks[i];
	}
    }
  ' "$tmp_marker" > "$tmp_legacy"

  if ! cmp -s "$conf" "$tmp_legacy"; then
    cp "$tmp_legacy" "$conf"
  fi

  rm -f "$tmp_marker" "$tmp_legacy"
}

oos_append_fixture_section ()
{
  local conf="$1"

  {
    printf '\n%s\n' "$OOS_UNITTESTDB_MARKER_BEGIN"
    printf '[@%s]\n' "$OOS_UNITTESTDB_NAME"
    printf 'vacuum_log_block_pages=4\n'
    printf '%s\n' "$OOS_UNITTESTDB_MARKER_END"
  } >> "$conf"
}
