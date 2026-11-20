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

set -euo pipefail

# Helpers shared by the OOS test-database fixtures. Two kinds of database use them: the
# unittestdb fixture the whole OOS suite shares, and the databases a single binary owns because
# it changes process-global state (no-logging) or kills a writer and recovers. The name of the
# database is always a parameter; only the shared fixture's own name and its cubrid.conf section
# are fixed here.
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

oos_database_dir ()
{
  printf '%s/%s\n' "$CUBRID_DATABASES" "$1"
}

oos_database_exists ()
{
  local db_name="$1"
  local databases_txt="$CUBRID_DATABASES/databases.txt"

  [ -f "$databases_txt" ] || return 1
  awk -v db="$db_name" '$1 == db { found = 1 } END { exit found ? 0 : 1 }' "$databases_txt"
}

# A fixture owns a database only while this receipt matches the database's current identity.
oos_database_receipt ()
{
  printf '%s/.oos-%s.owner\n' "$CUBRID_DATABASES" "$1"
}

oos_database_identity ()
{
  local databases_txt="$CUBRID_DATABASES/databases.txt"
  local registration
  local db_name
  local data_dir
  local _registry_tail
  local volume_identity

  [ -f "$databases_txt" ] || return 1
  # Ambiguous registrations must never grant deletion authority. Compare the fields, not the bytes: createdb and
  # deletedb of any database rewrite every row with other whitespace (cfg_write_directory).
  registration=$(awk -v db="$1" '
    BEGIN { OFS = "\t" }
    $1 == db { $1 = $1; registration = $0; count++ }
    END { if (count != 1) exit 1; print registration }
  ' "$databases_txt") || return 1
  read -r db_name data_dir _registry_tail <<< "$registration"
  [ -n "$data_dir" ] && [ -f "$data_dir/$db_name" ] || return 1
  # The row alone cannot distinguish a database recreated at the same path. The main
  # volume's identity survives ordinary writes but changes when that volume is replaced.
  volume_identity=$(stat -Lc '%d:%i' -- "$data_dir/$db_name" 2>/dev/null) || return 1
  printf '%s\n%s\n' "$registration" "$volume_identity"
}

# Records ownership of a database the caller has just created.
oos_database_claim ()
{
  local receipt
  local receipt_tmp

  receipt=$(oos_database_receipt "$1")
  receipt_tmp=$(mktemp "${receipt}.XXXXXX")
  if ! oos_database_identity "$1" > "$receipt_tmp"; then
    rm -f "$receipt_tmp"
    printf 'cannot identify created fixture database %s\n' "$1" >&2
    return 1
  fi
  mv "$receipt_tmp" "$receipt"
}

oos_database_owned ()
{
  local receipt
  local identity

  receipt=$(oos_database_receipt "$1")
  [ -f "$receipt" ] && identity=$(oos_database_identity "$1") && [ "$identity" = "$(cat "$receipt")" ]
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
