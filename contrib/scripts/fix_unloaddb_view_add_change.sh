#!/bin/bash
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

usage()
{
  echo "Usage: $0 -i INPUT_SCHEMA_SQL [-o OUTPUT_SCHEMA_SQL] [--inplace]"
  echo
  echo "  -i INPUT_SCHEMA_SQL   unloaddb schema sql file"
  echo "  -o OUTPUT_SCHEMA_SQL  output file (default: stdout)"
  echo "  --inplace             modify INPUT_SCHEMA_SQL in-place"
  echo
  echo "What it does (for CUBRID <= 11.0 unloaddb artifacts):"
  echo "  1) Skips: ALTER VIEW ... ADD QUERY ... SELECT ... NA, ... NA ..."
  echo "  2) Rewrites: ALTER VIEW ... CHANGE QUERY 1 SELECT ... FROM ..."
  echo "     to:       ALTER VIEW ... ADD QUERY SELECT ... FROM ..."
}

in_file=""
out_file=""
inplace=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    -i)
      in_file="${2:-}"
      shift 2
      ;;
    -o)
      out_file="${2:-}"
      shift 2
      ;;
    --inplace)
      inplace=1
      shift 1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1"
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$in_file" ]]; then
  usage
  exit 1
fi

if [[ $inplace -eq 1 ]]; then
  if [[ -z "$out_file" ]]; then
    out_file="${in_file}.tmp.$$"
  fi
fi

if [[ ! -f "$in_file" ]]; then
  echo "Input file not found: $in_file" 1>&2
  exit 1
fi

awk '
  BEGIN {
    IGNORECASE=1
  }

  function is_alter_vclass_view_start(line) {
    return (line ~ /^[[:space:]]*ALTER[[:space:]]+(VIEW|VCLASS)[[:space:]]+/)
  }

  function normalize_ident(s,    t) {
    t=s
    gsub(/^[[:space:]]+|[[:space:]]+$/, "", t)
    gsub(/"/, "", t)
    gsub(/^\[/, "", t)
    gsub(/\]$/, "", t)
    return tolower(t)
  }

  function extract_view_norm(s,    m) {
    # Expect: ALTER (VIEW|VCLASS) <name> ...
    if (match(s, /^[[:space:]]*ALTER[[:space:]]+(VIEW|VCLASS)[[:space:]]+([^[:space:];]+)/, m)) {
      return normalize_ident(m[1])
    }
    return ""
  }

  function has_na_placeholder(s,    t) {
    # Make NA matching tolerant for "NA,NA" / "NA , NA" etc.
    t=s
    gsub(/,/, " , ", t)
    gsub(/\(/, " ( ", t)
    gsub(/\)/, " ) ", t)
    gsub(/[[:space:]]+/, " ", t)
    return (tolower(t) ~ /(^|[[:space:]])na([[:space:]]|,)/)
  }

  function finalize_stmt() {
    if (!in_stmt) return

    stmt_norm = stmt
    gsub(/[[:space:]]+/, " ", stmt_norm)

    # (1) Skip: ALTER VIEW ... ADD QUERY ... (SELECT ... NA, ... NA ... FROM ...)
    if (stmt ~ /^[[:space:]]*ALTER[[:space:]]+(VIEW|VCLASS)/ &&
        stmt ~ /ADD[[:space:]]+QUERY/ &&
        stmt ~ /SELECT/ &&
        has_na_placeholder(stmt) &&
        tolower(stmt_norm) ~ /[[:space:]]from[[:space:]]/) {
      skipped++
      in_stmt=0
      stmt=""
      return
    }

    # (2) Rewrite all CHANGE QUERY n (n can be any number):
    #     ALTER VIEW/VCLASS ... CHANGE QUERY n ...  => ALTER VIEW/VCLASS ... ADD QUERY ...
    if (stmt ~ /^[[:space:]]*ALTER[[:space:]]+(VIEW|VCLASS)/ &&
        stmt ~ /CHANGE[[:space:]]+QUERY[[:space:]]+[0-9]+/) {
      out = stmt
      gsub(/CHANGE[[:space:]]+QUERY[[:space:]]+[0-9]+/, "ADD QUERY", out)
      print out
      changed++
      in_stmt=0
      stmt=""
      return
    }

    print stmt
    in_stmt=0
    stmt=""
  }

  {
    if (!in_stmt) {
      if (is_alter_vclass_view_start($0)) {
        in_stmt=1
        stmt=$0 "\n"
      } else {
        print
      }
    } else {
      stmt = stmt $0 "\n"
    }

    if (in_stmt && index($0, ";") > 0) {
      finalize_stmt()
    }
  }

  END {
    if (in_stmt) {
      # Incomplete statement (no semicolon). Best-effort print as-is.
      print stmt
    }
    if (skipped + changed > 0) {
      printf "fix_unloaddb_view_add_change: skipped=%d rewritten=%d\n", skipped, changed > "/dev/stderr"
    }
  }
' "$in_file" > "${out_file:-/dev/stdout}"

if [[ $inplace -eq 1 ]]; then
  mv -f "$out_file" "$in_file"
fi

