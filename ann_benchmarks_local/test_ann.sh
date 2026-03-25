#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="$SCRIPT_DIR"

# User-facing settings
DB_NAME="${DB_NAME:-demodb}"
DB_USER="${DB_USER:-dba}"
DATASET_HDF5="${DATASET_HDF5:-$SCRIPT_DIR/nytimes-256-angular.hdf5}"
DATASET_DOWNLOAD_URL="${DATASET_DOWNLOAD_URL:-}"

TOPK="${TOPK:-10}"
PROGRESS_EVERY="${PROGRESS_EVERY:-100}"
TRAIN_ROW_LIMIT="${TRAIN_ROW_LIMIT:-}"

HNSW_M="${HNSW_M:-24}"
HNSW_EF_CONSTRUCTION="${HNSW_EF_CONSTRUCTION:-200}"
HNSW_EF_SEARCH_VALUES="${HNSW_EF_SEARCH_VALUES:-200 400}"

# Internal derived paths and per-run state
DATASET_FILENAME="$(basename -- "$DATASET_HDF5")"
DATASET_NAME="${DATASET_FILENAME%.hdf5}"
DATASET_FILE_STEM="${DATASET_NAME//-/_}"
SCHEMA_FILE="$DATA_DIR/${DATASET_FILE_STEM}_schema"
OBJECT_FILE="$DATA_DIR/${DATASET_FILE_STEM}_object"
OBJECT_NAN_MARKER="$SCRIPT_DIR/${DATASET_FILE_STEM}_object.nan_sanitized"
OBJECT_LOAD_MARKER="$SCRIPT_DIR/${DATASET_FILE_STEM}_object.load_sanitized"
RESULT_CSV="$SCRIPT_DIR/${DATASET_NAME}_ef_search_results.csv"
RESULT_SVG="$SCRIPT_DIR/${DATASET_NAME}_ef_search_results.svg"
EXCLUDED_QUERY_FILE="$SCRIPT_DIR/${DATASET_NAME}_excluded_queries.txt"
QUERY_ID_FILE="$SCRIPT_DIR/${DATASET_NAME}_query_ids.txt"
QUERY_ID_CACHE_MARKER="$SCRIPT_DIR/${DATASET_NAME}_query_ids.cache_ready"
GT_CACHE_FILE="$SCRIPT_DIR/${DATASET_NAME}_gt_topk${TOPK}.out"
GT_CACHE_MARKER="$SCRIPT_DIR/${DATASET_NAME}_gt_topk${TOPK}.cache_ready"
PERF_OUTPUT_DIR="$SCRIPT_DIR/perf_${DATASET_FILE_STEM}"
HNSW_EF_SEARCH=""

PERF_ENABLE="${PERF_ENABLE:-0}"
PERF_STAT_ENABLE="${PERF_STAT_ENABLE:-1}"
PERF_TARGETS="${PERF_TARGETS:-csql cub_server}"
PERF_STAT_EVENTS="${PERF_STAT_EVENTS:-task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses}"
PERF_RECORD_TARGETS="${PERF_RECORD_TARGETS:-}"
PERF_RECORD_PROFILE="${PERF_RECORD_PROFILE:-hot}"
PERF_RECORD_EVENT="${PERF_RECORD_EVENT:-}"
PERF_RECORD_FREQ="${PERF_RECORD_FREQ:-99}"
PERF_RECORD_PERIOD="${PERF_RECORD_PERIOD:-}"
PERF_CALL_GRAPH="${PERF_CALL_GRAPH:-fp}"
PERF_FLAMEGRAPH="${PERF_FLAMEGRAPH:-1}"
FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-$SCRIPT_DIR/flame_graph}"
STACKCOLLAPSE_PERF="${STACKCOLLAPSE_PERF:-}"
FLAMEGRAPH_PL="${FLAMEGRAPH_PL:-}"
FLAMEGRAPH_REPO_URL="${FLAMEGRAPH_REPO_URL:-https://github.com/brendangregg/FlameGraph.git}"
FLAMEGRAPH_TARBALL_URL="${FLAMEGRAPH_TARBALL_URL:-https://github.com/brendangregg/FlameGraph/archive/refs/heads/master.tar.gz}"

log() {
  printf '[%s] %s\n' "$(date '+%F %T')" "$*"
}

get_dataset_download_url() {
  if [[ -n "$DATASET_DOWNLOAD_URL" ]]; then
    printf '%s\n' "$DATASET_DOWNLOAD_URL"
    return
  fi

  printf 'https://ann-benchmarks.com/%s\n' "$DATASET_FILENAME"
}

run_stage() {
  local stage_name="$1"
  shift
  local start_ts
  local end_ts
  local elapsed_sec

  log "stage start: $stage_name"
  start_ts="$(date +%s)"
  "$@"
  end_ts="$(date +%s)"
  elapsed_sec="$((end_ts - start_ts))"
  log "stage done: $stage_name (${elapsed_sec}s)"
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    printf 'missing command: %s\n' "$1" >&2
    exit 1
  }
}

download_file_with_python() {
  local source_url="$1"
  local destination_path="$2"
  local destination_dir
  local tmp_file

  destination_dir="$(dirname "$destination_path")"
  mkdir -p "$destination_dir"
  tmp_file="$(mktemp "$destination_dir/.dataset_download.XXXXXX")"

  if ! python3 - "$source_url" "$tmp_file" <<'PY'
import sys
from urllib.request import build_opener, install_opener, urlretrieve

source_url = sys.argv[1]
destination_path = sys.argv[2]

opener = build_opener()
opener.addheaders = [("User-agent", "Mozilla/5.0")]
install_opener(opener)
urlretrieve(source_url, destination_path)
PY
  then
    rm -f "$tmp_file"
    return 1
  fi

  mv "$tmp_file" "$destination_path"
}

ensure_dataset_hdf5() {
  local dataset_url

  if [[ -f "$DATASET_HDF5" ]]; then
    return 0
  fi

  dataset_url="$(get_dataset_download_url)"
  log "dataset hdf5 not found: $DATASET_HDF5"
  log "downloading dataset from $dataset_url"

  if download_file_with_python "$dataset_url" "$DATASET_HDF5"; then
    log "downloaded dataset to $DATASET_HDF5"
    return 0
  fi

  printf 'failed to download dataset: %s -> %s\n' \
    "$dataset_url" "$DATASET_HDF5" >&2
  return 1
}

resolve_existing_file() {
  local candidate="$1"

  if [[ -n "$candidate" && -f "$candidate" ]]; then
    printf '%s\n' "$candidate"
  fi
}

has_word() {
  local needle="$1"
  shift || true
  local word

  for word in "$@"; do
    if [[ "$word" == "$needle" ]]; then
      return 0
    fi
  done

  return 1
}

perf_enabled_for_target() {
  local target="$1"

  if (( PERF_STAT_ENABLE != 1 )); then
    return 1
  fi

  has_word "$target" $PERF_TARGETS
}

perf_record_enabled_for_target() {
  local target="$1"
  has_word "$target" $PERF_RECORD_TARGETS
}

sanitize_perf_label() {
  printf '%s' "$1" | tr -c '[:alnum:]_.-' '_'
}

get_perf_record_event() {
  if [[ -n "$PERF_RECORD_EVENT" ]]; then
    printf '%s\n' "$PERF_RECORD_EVENT"
    return
  fi

  case "$PERF_RECORD_PROFILE" in
    hot)
      printf 'cycles\n'
      ;;
    instructions)
      printf 'instructions\n'
      ;;
    branch)
      printf 'branch-misses\n'
      ;;
    cache)
      printf 'cache-misses\n'
      ;;
    custom)
      printf 'cycles\n'
      ;;
    *)
      printf 'unknown PERF_RECORD_PROFILE: %s\n' "$PERF_RECORD_PROFILE" >&2
      exit 1
      ;;
  esac
}

append_perf_record_profile_suffix() {
  local label="$1"

  case "$PERF_RECORD_PROFILE" in
    hot)
      printf '%s\n' "$label"
      ;;
    instructions|branch|cache|custom)
      printf '%s.%s\n' "$label" "$PERF_RECORD_PROFILE"
      ;;
    *)
      printf '%s\n' "$label"
      ;;
  esac
}

get_cub_server_pid() {
  ps -eo pid=,comm=,args= | awk -v db_name="$DB_NAME" '
    $2 == "cub_server" && index($0, db_name) {
      print $1
      exit
    }
  '
}

ensure_db_access() {
  local server_pid=""

  if csql -u "$DB_USER" -q -N "$DB_NAME" -c "SELECT 1;" >/dev/null 2>&1; then
    return
  fi

  log "database access check failed for $DB_NAME; attempting to restore cub_master/server access"
  cubrid server start "$DB_NAME" >/dev/null 2>&1 || true

  if csql -u "$DB_USER" -q -N "$DB_NAME" -c "SELECT 1;" >/dev/null 2>&1; then
    return
  fi

  server_pid="$(get_cub_server_pid || true)"
  if [[ -n "$server_pid" ]]; then
    log "restarting orphaned cub_server pid=$server_pid for $DB_NAME"
    kill -TERM "$server_pid" >/dev/null 2>&1 || true
    sleep 2
  fi

  cubrid server start "$DB_NAME" >/dev/null 2>&1 || true

  if ! csql -u "$DB_USER" -q -N "$DB_NAME" -c "SELECT 1;" >/dev/null 2>&1; then
    printf 'failed to restore database access for %s\n' "$DB_NAME" >&2
    exit 1
  fi
}

start_perf_stat_attach() {
  local pid="$1"
  local output_file="$2"
  local log_file="${output_file}.log"

  if ! kill -0 "$pid" 2>/dev/null; then
    return 1
  fi

  perf stat \
    -x, \
    -e "$PERF_STAT_EVENTS" \
    -p "$pid" \
    -o "$output_file" >"$log_file" 2>&1 &
  echo $!
}

start_perf_record_attach() {
  local pid="$1"
  local output_file="$2"
  local log_file="${output_file}.log"
  local record_event
  local -a perf_cmd

  if ! kill -0 "$pid" 2>/dev/null; then
    return 1
  fi

  record_event="$(get_perf_record_event)"
  perf_cmd=(
    perf record
    -g
    --call-graph "$PERF_CALL_GRAPH"
    -e "$record_event"
    -p "$pid"
    -o "$output_file"
  )

  if [[ -n "$PERF_RECORD_PERIOD" ]]; then
    perf_cmd+=(-c "$PERF_RECORD_PERIOD")
  else
    perf_cmd+=(-F "$PERF_RECORD_FREQ")
  fi

  "${perf_cmd[@]}" >"$log_file" 2>&1 &
  echo $!
}

stop_perf_background_job() {
  local perf_pid="$1"
  local waited=0

  if [[ -z "$perf_pid" ]]; then
    return
  fi

  if kill -0 "$perf_pid" 2>/dev/null; then
    kill -INT "$perf_pid" 2>/dev/null || true
  fi

  while kill -0 "$perf_pid" 2>/dev/null && (( waited < 50 )); do
    sleep 0.1
    waited=$((waited + 1))
  done

  if kill -0 "$perf_pid" 2>/dev/null; then
    kill -TERM "$perf_pid" 2>/dev/null || true
  fi

  wait "$perf_pid" 2>/dev/null || true
}

flamegraph_tools_ready() {
  [[ -f "$FLAMEGRAPH_DIR/flamegraph.pl" && -f "$FLAMEGRAPH_DIR/stackcollapse-perf.pl" ]]
}

download_flamegraph_with_git() {
  if ! command -v git >/dev/null 2>&1; then
    return 1
  fi

  rm -rf "$FLAMEGRAPH_DIR"
  git clone --depth 1 "$FLAMEGRAPH_REPO_URL" "$FLAMEGRAPH_DIR"
}

download_flamegraph_with_tarball() {
  local archive_file
  local tmp_dir
  local extracted_dir

  archive_file="$(mktemp /tmp/flamegraph.XXXXXX.tar.gz)"
  tmp_dir="$(mktemp -d /tmp/flamegraph.XXXXXX)"

  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$FLAMEGRAPH_TARBALL_URL" -o "$archive_file"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$archive_file" "$FLAMEGRAPH_TARBALL_URL"
  else
    rm -f "$archive_file"
    rmdir "$tmp_dir"
    return 1
  fi

  tar -xzf "$archive_file" -C "$tmp_dir"
  extracted_dir="$(find "$tmp_dir" -mindepth 1 -maxdepth 1 -type d | head -n 1)"

  if [[ -z "$extracted_dir" ]]; then
    rm -f "$archive_file"
    rm -rf "$tmp_dir"
    return 1
  fi

  rm -rf "$FLAMEGRAPH_DIR"
  mv "$extracted_dir" "$FLAMEGRAPH_DIR"
  rm -f "$archive_file"
  rm -rf "$tmp_dir"
}

ensure_flamegraph_tools() {
  if (( PERF_FLAMEGRAPH != 1 )); then
    return
  fi

  if flamegraph_tools_ready; then
    return
  fi

  mkdir -p "$(dirname "$FLAMEGRAPH_DIR")"
  log "flame graph tools not found under $FLAMEGRAPH_DIR; downloading"

  if download_flamegraph_with_git || download_flamegraph_with_tarball; then
    if flamegraph_tools_ready; then
      log "downloaded flame graph tools to $FLAMEGRAPH_DIR"
      return
    fi
  fi

  printf 'failed to provision FlameGraph tools in %s\n' "$FLAMEGRAPH_DIR" >&2
  exit 1
}

resolve_stackcollapse_perf() {
  local candidate=""

  candidate="$(resolve_existing_file "$STACKCOLLAPSE_PERF")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(resolve_existing_file "$FLAMEGRAPH_DIR/stackcollapse-perf.pl")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(resolve_existing_file "$SCRIPT_DIR/../FlameGraph/stackcollapse-perf.pl")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(command -v stackcollapse-perf.pl 2>/dev/null || true)"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
  fi
}

resolve_flamegraph_pl() {
  local candidate=""

  candidate="$(resolve_existing_file "$FLAMEGRAPH_PL")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(resolve_existing_file "$FLAMEGRAPH_DIR/flamegraph.pl")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(resolve_existing_file "$SCRIPT_DIR/../FlameGraph/flamegraph.pl")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(command -v flamegraph.pl 2>/dev/null || true)"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
  fi
}

generate_flamegraph() {
  local perf_data_file="$1"
  local svg_file="$2"
  local stackcollapse_perf
  local flamegraph_pl
  local tmp_svg_file

  if (( PERF_FLAMEGRAPH != 1 )); then
    return
  fi

  if [[ ! -s "$perf_data_file" ]]; then
    return
  fi

  ensure_flamegraph_tools

  stackcollapse_perf="$(resolve_stackcollapse_perf)"
  flamegraph_pl="$(resolve_flamegraph_pl)"

  if [[ -z "$stackcollapse_perf" || -z "$flamegraph_pl" ]]; then
    log "skipping flame graph for $perf_data_file: flamegraph tools not found"
    return
  fi

  tmp_svg_file="$(mktemp /tmp/nytimes_flamegraph.XXXXXX.svg)"
  if ! perf script -i "$perf_data_file" \
    | perl "$stackcollapse_perf" \
    | perl "$flamegraph_pl" --title "$(basename "$svg_file" .svg)" > "$tmp_svg_file"; then
    rm -f "$tmp_svg_file"
    log "failed to generate flame graph from $perf_data_file"
    return
  fi

  if grep -q 'ERROR: No valid input provided to flamegraph.pl' "$tmp_svg_file"; then
    rm -f "$tmp_svg_file"
    log "flame graph input was empty for $perf_data_file"
    return
  fi

  mv "$tmp_svg_file" "$svg_file"
}

run_csql_input_with_perf() {
  local sql_file="$1"
  local out_file="$2"
  local label="$3"
  local stat_label
  local record_label
  local safe_stat_label
  local safe_record_label
  local enable_csql_record=1
  local csql_pid=""
  local csql_status=0
  local csql_stat_pid=""
  local csql_record_pid=""
  local server_pid=""
  local server_stat_pid=""
  local server_record_pid=""

  if (( PERF_ENABLE != 1 )); then
    ensure_db_access
    csql -u "$DB_USER" -q -N --delimiter='|' "$DB_NAME" -i "$sql_file" > "$out_file"
    return
  fi

  mkdir -p "$PERF_OUTPUT_DIR"
  stat_label="$label"
  record_label="$(append_perf_record_profile_suffix "$label")"
  safe_stat_label="$(sanitize_perf_label "$stat_label")"
  safe_record_label="$(sanitize_perf_label "$record_label")"
  ensure_db_access

  if [[ "$safe_stat_label" == build_index_* ]]; then
    enable_csql_record=0
  fi

  csql -u "$DB_USER" -q -N --delimiter='|' "$DB_NAME" -i "$sql_file" > "$out_file" &
  csql_pid=$!

  if perf_enabled_for_target csql; then
    csql_stat_pid="$(
      start_perf_stat_attach "$csql_pid" "$PERF_OUTPUT_DIR/${safe_stat_label}.csql.stat.csv" || true
    )"
  fi

  if (( enable_csql_record == 1 )) && perf_record_enabled_for_target csql; then
    csql_record_pid="$(
      start_perf_record_attach "$csql_pid" "$PERF_OUTPUT_DIR/${safe_record_label}.csql.data" || true
    )"
  fi

  if perf_enabled_for_target cub_server || perf_record_enabled_for_target cub_server; then
    server_pid="$(get_cub_server_pid || true)"

    if [[ -n "$server_pid" ]]; then
      if perf_enabled_for_target cub_server; then
        server_stat_pid="$(
          start_perf_stat_attach "$server_pid" "$PERF_OUTPUT_DIR/${safe_stat_label}.cub_server.stat.csv" || true
        )"
      fi

      if perf_record_enabled_for_target cub_server; then
        server_record_pid="$(
          start_perf_record_attach "$server_pid" "$PERF_OUTPUT_DIR/${safe_record_label}.cub_server.data" || true
        )"
      fi
    else
      log "perf requested for cub_server, but no PID was found for $DB_NAME"
    fi
  fi

  set +e
  wait "$csql_pid"
  csql_status=$?
  set -e

  stop_perf_background_job "$csql_stat_pid"
  stop_perf_background_job "$csql_record_pid"
  stop_perf_background_job "$server_stat_pid"
  stop_perf_background_job "$server_record_pid"

  if (( enable_csql_record == 1 )) && perf_record_enabled_for_target csql; then
    generate_flamegraph \
      "$PERF_OUTPUT_DIR/${safe_record_label}.csql.data" \
      "$PERF_OUTPUT_DIR/${safe_record_label}.csql.flamegraph.svg"
  fi

  if perf_record_enabled_for_target cub_server; then
    generate_flamegraph \
      "$PERF_OUTPUT_DIR/${safe_record_label}.cub_server.data" \
      "$PERF_OUTPUT_DIR/${safe_record_label}.cub_server.flamegraph.svg"
  fi

  if (( csql_status != 0 )); then
    return "$csql_status"
  fi
}

sanitize_schema_file() {
  if [[ ! -f "$SCHEMA_FILE" ]]; then
    return
  fi

  if grep -q "/nytimes_256_angular_" "$SCHEMA_FILE"; then
    log "normalizing schema file: $SCHEMA_FILE"
    perl -0pi -e '
      s{CREATE TABLE \S*nytimes_256_angular_train\b}{CREATE TABLE nytimes_256_angular_train}g;
      s{CREATE TABLE \S*nytimes_256_angular_test\b}{CREATE TABLE nytimes_256_angular_test}g;
      s{CREATE TABLE \S*nytimes_256_angular_answer\b}{CREATE TABLE nytimes_256_angular_answer}g;
    ' "$SCHEMA_FILE"
  fi
}

write_load_schema_file() {
  local load_schema_file="$1"

  awk '
    BEGIN {
      removed = 0
    }
    /^[[:space:]]*CREATE[[:space:]]+VECTOR[[:space:]]+INDEX[[:space:]]/ {
      removed = 1
      next
    }
    {
      print
    }
    END {
      if (removed) {
        printf "removed CREATE VECTOR INDEX statements from load schema\n" > "/dev/stderr"
      }
    }
  ' "$SCHEMA_FILE" > "$load_schema_file"
}

sanitize_object_file() {
  if [[ ! -f "$OBJECT_FILE" ]]; then
    return
  fi

  if grep -q "/nytimes_256_angular_" "$OBJECT_FILE"; then
    log "normalizing object file header: $OBJECT_FILE"
    perl -0pi -e '
      s{^%id \S*nytimes_256_angular_train\b}{%id nytimes_256_angular_train}m;
      s{^%id \S*nytimes_256_angular_test\b}{%id nytimes_256_angular_test}m;
      s{^%id \S*nytimes_256_angular_answer\b}{%id nytimes_256_angular_answer}m;
      s{^%class \S*nytimes_256_angular_train\b}{%class nytimes_256_angular_train}m;
      s{^%class \S*nytimes_256_angular_test\b}{%class nytimes_256_angular_test}m;
      s{^%class \S*nytimes_256_angular_answer\b}{%class nytimes_256_angular_answer}m;
    ' "$OBJECT_FILE"
  fi

  if [[ ! -f "$OBJECT_NAN_MARKER" || "$OBJECT_NAN_MARKER" -ot "$OBJECT_FILE" ]]; then
    if rg -q '(^|[^[:alpha:]_])-?nan([^[:alpha:]_]|$)' "$OBJECT_FILE"; then
      log "replacing nan values with NULL in $OBJECT_FILE"
      perl -0pi -e 's{(?<![[:alpha:]_])-?nan(?![[:alpha:]_])}{NULL}gi' "$OBJECT_FILE"
    fi
    touch "$OBJECT_NAN_MARKER"
  fi

  if [[ ! -f "$OBJECT_LOAD_MARKER" || "$OBJECT_LOAD_MARKER" -ot "$OBJECT_FILE" ]]; then
    log "dropping malformed vector rows in $OBJECT_FILE"
    perl -i -ne '
      if (/^%class\s+(\S+)/) {
        $class = $1;
        print;
        next;
      }

      if (($class eq q{nytimes_256_angular_train} || $class eq q{nytimes_256_angular_test})
          && (/^(\d+)\s+'\[.*NULL.*\]'$/ || /^(\d+)\s+NULL$/)) {
        next;
      }

      print;
    ' "$OBJECT_FILE"
    touch "$OBJECT_LOAD_MARKER"
  fi
}

write_load_object_file() {
  local load_object_file="$1"

  if [[ -z "$TRAIN_ROW_LIMIT" ]]; then
    printf '%s\n' "$OBJECT_FILE"
    return
  fi

  printf '[%s] limiting nytimes_256_angular_train rows to %s for load\n' \
    "$(date '+%F %T')" \
    "$TRAIN_ROW_LIMIT" >&2
  perl -ne '
    BEGIN {
      $limit = $ENV{"TRAIN_ROW_LIMIT"};
      $train_count = 0;
      $class = "";
    }

    if (/^%class\s+(\S+)/) {
      $class = $1;
      print;
      next;
    }

    if ($class eq q{nytimes_256_angular_train} && /^[0-9]/) {
      if ($train_count >= $limit) {
        next;
      }
      $train_count++;
      print;
      next;
    }

    print;
  ' "$OBJECT_FILE" > "$load_object_file"

  printf '%s\n' "$load_object_file"
}

sql_capture() {
  local query="$1"
  ensure_db_access
  csql -u "$DB_USER" -q -N --delimiter='|' "$DB_NAME" -c "$query"
}

sql_exec() {
  local query="$1"
  ensure_db_access
  csql -u "$DB_USER" "$DB_NAME" -c "$query"
}

validate_required_classes() {
  local missing_classes=""
  local required_classes=(
    nytimes_256_angular_train
    nytimes_256_angular_test
    nytimes_256_angular_answer
  )
  local class_name
  local found

  for class_name in "${required_classes[@]}"; do
    found="$(
      sql_capture "
        SELECT class_name
          FROM db_class
         WHERE class_name = '${class_name}';
      " | awk -F'|' 'NF > 0 {print $1; exit}'
    )"

    if [[ "$found" != "$class_name" ]]; then
      missing_classes+=" $class_name"
    fi
  done

  if [[ -n "$missing_classes" ]]; then
    printf 'required dataset classes are missing in %s:%s\n' \
      "$DB_NAME" \
      "$missing_classes" >&2
    printf 'expected classes: nytimes_256_angular_train, nytimes_256_angular_test, nytimes_256_angular_answer\n' >&2
    return 1
  fi
}

invalidate_dataset_cache() {
  log "invalidating cached query/results artifacts for $DATASET_NAME"
  rm -f \
    "$QUERY_ID_FILE" \
    "$EXCLUDED_QUERY_FILE" \
    "$QUERY_ID_CACHE_MARKER" \
    "$GT_CACHE_FILE" \
    "$GT_CACHE_MARKER" \
    "$RESULT_CSV" \
    "$RESULT_SVG"
}

prepare_ground_truth_cache() {
  local total_queries

  total_queries="${#QUERY_IDS[@]}"

  if [[ -s "$GT_CACHE_FILE" && -f "$GT_CACHE_MARKER" && "$GT_CACHE_MARKER" -nt "$QUERY_ID_FILE" ]]; then
    log "reusing cached ground-truth neighbors from $GT_CACHE_FILE"
    return
  fi

  log "fetching ground-truth neighbors for ${total_queries} queries with one set-based query"
  sql_capture "
    SELECT 2, id, neighbor_id
      FROM (
        SELECT id,
               neighbor_id,
               ROW_NUMBER() OVER (PARTITION BY id ORDER BY neighbor_distance) AS rn
          FROM nytimes_256_angular_answer
         WHERE neighbor_distance IS NOT NULL
      ) gt
     WHERE rn <= ${TOPK}
     ORDER BY id, rn;
  " > "$GT_CACHE_FILE"

  if [[ ! -s "$GT_CACHE_FILE" ]]; then
    printf 'failed to fetch ground-truth neighbors from nytimes_256_angular_answer\n' >&2
    exit 1
  fi

  touch "$GT_CACHE_MARKER"
  log "cached ground-truth neighbors to $GT_CACHE_FILE"
}

setup_demo_db() {
  if [[ -z "${CUBRID:-}" ]]; then
    printf 'CUBRID environment variable is not set.\n' >&2
    exit 1
  fi

  if ! grep -qx 'stored_procedure=no' "$CUBRID/conf/cubrid.conf"; then
    echo 'stored_procedure=no' >> "$CUBRID/conf/cubrid.conf"
  fi

  cubrid server stop "$DB_NAME" || true
  cubrid deletedb "$DB_NAME" || true

  (
    cd "$CUBRID/demo"
    ./make_cubrid_demo.sh
  )

  cubrid server start "$DB_NAME"
}

load_dataset() {
  local load_schema_file
  local load_object_file
  local object_input_file

  load_schema_and_object_files() {
    sanitize_schema_file
    sanitize_object_file
    load_schema_file="$(mktemp /tmp/nytimes_load_schema.XXXXXX)"
    load_object_file="$(mktemp /tmp/nytimes_load_object.XXXXXX)"
    write_load_schema_file "$load_schema_file"
    object_input_file="$(write_load_object_file "$load_object_file")"
    log "loading dataset from $SCHEMA_FILE / $OBJECT_FILE"
    cubrid loaddb -s "$load_schema_file" -d "$object_input_file" -C -u "$DB_USER" "$DB_NAME" -v --no-statistics
    rm -f "$load_schema_file"
    if [[ "$object_input_file" == "$load_object_file" ]]; then
      rm -f "$load_object_file"
    fi
    return
  }

  if [[ -f "$SCHEMA_FILE" && -f "$OBJECT_FILE" ]]; then
    load_schema_and_object_files
    return
  fi

  if ensure_dataset_hdf5; then
    log "converting hdf5 dataset to loaddb files from $DATASET_HDF5"
    cubrid loaddb -h "$DATASET_HDF5" -C -u "$DB_USER" "$DB_NAME" --no-statistics

    if [[ -f "$SCHEMA_FILE" && -f "$OBJECT_FILE" ]]; then
      load_schema_and_object_files
      return
    fi

    printf 'dataset conversion did not produce expected files: %s, %s\n' \
      "$SCHEMA_FILE" "$OBJECT_FILE" >&2
    exit 1
  fi

  printf 'dataset not found or download failed. checked: %s, %s, %s\n' \
    "$DATASET_HDF5" "$SCHEMA_FILE" "$OBJECT_FILE" >&2
  exit 1
}

build_index() {
  local sql_file
  local out_file

  log "creating vector index (M=$HNSW_M, ef_construction=$HNSW_EF_CONSTRUCTION)"

  sql_file="$(mktemp /tmp/nytimes_build_index_sql.XXXXXX)"
  out_file="$(mktemp /tmp/nytimes_build_index_out.XXXXXX)"
  printf "CREATE VECTOR INDEX vidx_nytimes_train ON nytimes_256_angular_train (vec COSINE) WITH (M = %s, ef_construction = %s);\n" \
    "$HNSW_M" \
    "$HNSW_EF_CONSTRUCTION" > "$sql_file"

  run_csql_input_with_perf \
    "$sql_file" \
    "$out_file" \
    "build_index_m${HNSW_M}_efc${HNSW_EF_CONSTRUCTION}"

  cat "$out_file"
  rm -f "$sql_file" "$out_file"
}

prepare_query_ids() {
  local tmp_test_ids_file
  local tmp_valid_ids_file

  if [[ -s "$QUERY_ID_FILE" && -s "$EXCLUDED_QUERY_FILE" && -f "$QUERY_ID_CACHE_MARKER" ]]; then
    EXCLUDED_QUERY_COUNT="$(grep -vc '^#' "$EXCLUDED_QUERY_FILE" || true)"
    mapfile -t QUERY_IDS < <(grep -E '^-?[0-9]+$' "$QUERY_ID_FILE")

    if (( ${#QUERY_IDS[@]} > 0 )); then
      log "reusing cached query ids: ${#QUERY_IDS[@]} valid queries, ${EXCLUDED_QUERY_COUNT} excluded"
      return
    fi
  fi

  log "building query id cache from nytimes_256_angular_test/answer"

  # Queries whose vector is effectively invalid/null must be excluded.
  # They do not participate in vector indexing, and they should not be used
  # as ANN queries either. In this dataset, those cases appear as answer rows
  # with NULL neighbor_distance, so we keep only ids with at least one valid
  # ground-truth distance and record the excluded ids separately.
  tmp_test_ids_file="$(mktemp /tmp/nytimes_test_ids.XXXXXX)"
  tmp_valid_ids_file="$(mktemp /tmp/nytimes_valid_ids.XXXXXX)"

  log "fetching all test query ids"
  sql_capture "
    SELECT id
      FROM nytimes_256_angular_test
     ORDER BY id;
  " \
    | awk -F'|' '/^-?[0-9]+([|].*)?$/ {print $1}' > "$tmp_test_ids_file"

  if [[ ! -s "$tmp_test_ids_file" ]]; then
    rm -f "$tmp_test_ids_file" "$tmp_valid_ids_file"
    printf 'failed to fetch query ids from nytimes_256_angular_test\n' >&2
    exit 1
  fi

  log "fetching valid query ids from nytimes_256_angular_answer"
  sql_capture "
    SELECT id
      FROM nytimes_256_angular_answer
     WHERE neighbor_distance IS NOT NULL
     GROUP BY id
     ORDER BY id;
  " \
    | awk -F'|' '/^-?[0-9]+([|].*)?$/ {print $1}' > "$tmp_valid_ids_file"

  if [[ ! -s "$tmp_valid_ids_file" ]]; then
    rm -f "$tmp_test_ids_file" "$tmp_valid_ids_file"
    printf 'failed to fetch valid query ids from nytimes_256_angular_answer\n' >&2
    exit 1
  fi

  log "writing cached valid/excluded query id lists"

  {
    printf '# Excluded nytimes queries\n'
    printf '# reason: null/invalid vector queries are excluded from indexing and should also be excluded from ANN recall evaluation.\n'
    printf '# criterion: no non-NULL ground-truth neighbor_distance exists in nytimes_256_angular_answer.\n'
    awk 'NR == FNR {valid[$1] = 1; next} !($1 in valid) {print $1}' \
      "$tmp_valid_ids_file" \
      "$tmp_test_ids_file"
  } > "$EXCLUDED_QUERY_FILE"

  cp "$tmp_valid_ids_file" "$QUERY_ID_FILE"
  rm -f "$tmp_test_ids_file" "$tmp_valid_ids_file"

  if [[ ! -s "$QUERY_ID_FILE" ]]; then
    printf 'failed to fetch valid query ids from nytimes_256_angular_test\n' >&2
    exit 1
  fi

  touch "$QUERY_ID_CACHE_MARKER"

  EXCLUDED_QUERY_COUNT="$(grep -vc '^#' "$EXCLUDED_QUERY_FILE" || true)"
  log "excluded queries: ${EXCLUDED_QUERY_COUNT} (saved to $EXCLUDED_QUERY_FILE)"
  log "cached valid query ids to $QUERY_ID_FILE"

  mapfile -t QUERY_IDS < "$QUERY_ID_FILE"

  if (( ${#QUERY_IDS[@]} == 0 )); then
    printf 'no query ids to process\n' >&2
    exit 1
  fi

  log "valid queries selected for recall: ${#QUERY_IDS[@]}"
}

measure_recall() {
  local total_hits=0
  local processed=0
  local start_ns
  local end_ns
  local elapsed_ns
  local elapsed_sec
  local recall
  local qps
  local ann_sql_file
  local ann_out_file
  local summary
  local qid
  local total_queries

  total_queries="${#QUERY_IDS[@]}"
  log "measuring recall@${TOPK} for ${total_queries} queries (hnsw_ef_search=$HNSW_EF_SEARCH)"
  start_ns="$(date +%s%N)"
  ann_sql_file="$(mktemp /tmp/nytimes_measure_ann_sql.XXXXXX)"
  ann_out_file="$(mktemp /tmp/nytimes_measure_ann_out.XXXXXX)"

  {
    printf "SET SYSTEM PARAMETERS 'hnsw_ef_search=%s';\n" "$HNSW_EF_SEARCH"
    cat <<EOF
PREPARE ann FROM '
  SELECT 1, @qid, id
    FROM (
      SELECT /*+ recompile no_parallel_heap_scan */ id
        FROM nytimes_256_angular_train
       ORDER BY vec <c> cast(@v as vector)
       LIMIT ${TOPK}
    ) ann
';
EOF

    for ((i = 0; i < total_queries; i++)); do
      qid="${QUERY_IDS[i]}"
      printf "SET @qid = %s;\n" "$qid"
      printf "SET @v = (SELECT vec FROM nytimes_256_angular_test WHERE id = @qid);\n"
      printf "EXECUTE ann;\n"
    done
  } > "$ann_sql_file"

  log "running ANN queries for ${total_queries} test vectors"
  run_csql_input_with_perf \
    "$ann_sql_file" \
    "$ann_out_file" \
    "query_ann_ef${HNSW_EF_SEARCH}_topk${TOPK}_q${total_queries}"

  log "aggregating ANN results against ground truth"
  while IFS='|' read -r line_type line_processed line_hits; do
    case "$line_type" in
      P)
        partial="$(awk -v hits="$line_hits" -v total="$((line_processed * TOPK))" 'BEGIN { printf "%.6f", hits / total }')"
        log "progress: ${line_processed}/${total_queries} queries, partial recall@${TOPK}=${partial}"
        ;;
      S)
        processed="$line_processed"
        total_hits="$line_hits"
        ;;
    esac
  done < <(
    awk -F'|' -v topk="$TOPK" -v progress_every="$PROGRESS_EVERY" '
      function flush_query(   hits, i, n, key) {
        if (curr_qid == "") {
          return
        }

        hits = 0
        n = split(ann_list, ann_arr, " ")
        for (i = 1; i <= n; i++) {
          key = curr_qid SUBSEP ann_arr[i]
          if (ann_arr[i] != "" && (key in gt_hits)) {
            hits++
          }
        }

        total_hits += hits
        processed++

        ann_list = ""
        curr_qid = ""
      }

      FILENAME == gt_file && $1 == 2 && $2 ~ /^-?[0-9]+$/ && $3 ~ /^-?[0-9]+$/ {
        qid = $2 + 0
        rid = $3 + 0
        gt_hits[qid SUBSEP rid] = 1
      }

      FILENAME == ann_file && $1 == 1 && $2 ~ /^-?[0-9]+$/ && $3 ~ /^-?[0-9]+$/ {
        qid = $2 + 0
        rid = $3 + 0

        if (curr_qid != "" && qid != curr_qid) {
          flush_query()
        }

        if (curr_qid == "") {
          curr_qid = qid
        }

        ann_list = ann_list " " rid
      }

      END {
        flush_query()
        printf "S|%d|%d\n", processed, total_hits
      }
    ' ann_file="$ann_out_file" gt_file="$GT_CACHE_FILE" "$GT_CACHE_FILE" "$ann_out_file"
  )

  rm -f "$ann_sql_file" "$ann_out_file"

  end_ns="$(date +%s%N)"
  elapsed_ns=$((end_ns - start_ns))
  elapsed_sec="$(awk -v ns="$elapsed_ns" 'BEGIN { printf "%.6f", ns / 1000000000 }')"
  recall="$(awk -v hits="$total_hits" -v total="$((processed * TOPK))" 'BEGIN { printf "%.6f", hits / total }')"
  qps="$(awk -v queries="$processed" -v ns="$elapsed_ns" 'BEGIN { printf "%.3f", queries / (ns / 1000000000) }')"

  LAST_QUERIES="$processed"
  LAST_TOTAL_HITS="$total_hits"
  LAST_RECALL="$recall"
  LAST_ELAPSED_SEC="$elapsed_sec"
  LAST_QPS="$qps"

  printf '\n'
  printf 'dataset=%s\n' "$DATASET_NAME"
  printf 'queries=%d\n' "$processed"
  printf 'topk=%d\n' "$TOPK"
  printf 'hnsw_m=%d\n' "$HNSW_M"
  printf 'hnsw_ef_construction=%d\n' "$HNSW_EF_CONSTRUCTION"
  printf 'hnsw_ef_search=%d\n' "$HNSW_EF_SEARCH"
  printf 'excluded_queries=%s\n' "${EXCLUDED_QUERY_COUNT:-0}"
  printf 'total_hits=%d\n' "$total_hits"
  printf 'recall@%d=%s\n' "$TOPK" "$recall"
  printf 'elapsed_sec=%s\n' "$elapsed_sec"
  printf 'qps=%s\n' "$qps"
}

write_result_csv_header() {
  cat > "$RESULT_CSV" <<EOF
dataset,queries,excluded_queries,topk,hnsw_m,hnsw_ef_construction,hnsw_ef_search,total_hits,recall,elapsed_sec,qps
EOF
}

append_result_csv() {
  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$DATASET_NAME" \
    "$LAST_QUERIES" \
    "${EXCLUDED_QUERY_COUNT:-0}" \
    "$TOPK" \
    "$HNSW_M" \
    "$HNSW_EF_CONSTRUCTION" \
    "$HNSW_EF_SEARCH" \
    "$LAST_TOTAL_HITS" \
    "$LAST_RECALL" \
    "$LAST_ELAPSED_SEC" \
    "$LAST_QPS" >> "$RESULT_CSV"
}

render_result_svg() {
  python3 - "$RESULT_CSV" "$RESULT_SVG" "$DATASET_NAME" <<'PY'
import csv
import math
import sys

csv_path, svg_path, dataset_name = sys.argv[1], sys.argv[2], sys.argv[3]
rows = []
with open(csv_path, newline="") as f:
    for row in csv.DictReader(f):
        rows.append({
            "ef_search": int(row["hnsw_ef_search"]),
            "recall": float(row["recall"]),
            "qps": float(row["qps"]),
        })

if not rows:
    raise SystemExit("no rows to plot")

rows.sort(key=lambda r: r["ef_search"])
efs = [r["ef_search"] for r in rows]
recalls = [r["recall"] for r in rows]
qpss = [r["qps"] for r in rows]

width, height = 980, 620
left, right, top, bottom = 90, 90, 50, 70
plot_w = width - left - right
plot_h = height - top - bottom

min_x = min(efs)
max_x = max(efs)
min_recall = min(recalls)
max_recall = max(recalls)
min_qps = min(qpss)
max_qps = max(qpss)

if min_x == max_x:
    max_x += 1
if min_recall == max_recall:
    max_recall += 0.01
if min_qps == max_qps:
    max_qps += 1.0

recall_pad = max(0.005, (max_recall - min_recall) * 0.08)
qps_pad = max(1.0, (max_qps - min_qps) * 0.08)
min_recall = max(0.0, min_recall - recall_pad)
max_recall = min(1.0, max_recall + recall_pad)
min_qps = max(0.0, min_qps - qps_pad)
max_qps = max_qps + qps_pad

def x_of(v):
    return left + (v - min_x) / (max_x - min_x) * plot_w

def y_recall(v):
    return top + plot_h - (v - min_recall) / (max_recall - min_recall) * plot_h

def y_qps(v):
    return top + plot_h - (v - min_qps) / (max_qps - min_qps) * plot_h

def line_path(values, y_fn):
    return " ".join(
        ("M" if i == 0 else "L") + f" {x_of(x):.2f} {y_fn(y):.2f}"
        for i, (x, y) in enumerate(values)
    )

recall_path = line_path(list(zip(efs, recalls)), y_recall)
qps_path = line_path(list(zip(efs, qpss)), y_qps)

grid_lines = []
for i in range(6):
    y = top + plot_h * i / 5
    grid_lines.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}" stroke="#d7dde5" stroke-width="1"/>')

x_ticks = []
for ef in efs:
    x = x_of(ef)
    x_ticks.append(f'<line x1="{x:.2f}" y1="{top + plot_h}" x2="{x:.2f}" y2="{top + plot_h + 6}" stroke="#333"/>')
    x_ticks.append(f'<text x="{x:.2f}" y="{top + plot_h + 24}" text-anchor="middle" font-size="12" fill="#222">{ef}</text>')

left_ticks = []
for i in range(6):
    value = min_recall + (max_recall - min_recall) * (5 - i) / 5
    y = top + plot_h * i / 5
    left_ticks.append(f'<line x1="{left - 6}" y1="{y:.2f}" x2="{left}" y2="{y:.2f}" stroke="#333"/>')
    left_ticks.append(f'<text x="{left - 10}" y="{y + 4:.2f}" text-anchor="end" font-size="12" fill="#0f5c2e">{value:.3f}</text>')

right_ticks = []
for i in range(6):
    value = min_qps + (max_qps - min_qps) * (5 - i) / 5
    y = top + plot_h * i / 5
    right_ticks.append(f'<line x1="{left + plot_w}" y1="{y:.2f}" x2="{left + plot_w + 6}" y2="{y:.2f}" stroke="#333"/>')
    right_ticks.append(f'<text x="{left + plot_w + 10}" y="{y + 4:.2f}" text-anchor="start" font-size="12" fill="#8a4b00">{value:.1f}</text>')

recall_points = "\n".join(
    f'<circle cx="{x_of(r["ef_search"]):.2f}" cy="{y_recall(r["recall"]):.2f}" r="4" fill="#17803d"/>'
    for r in rows
)
qps_points = "\n".join(
    f'<circle cx="{x_of(r["ef_search"]):.2f}" cy="{y_qps(r["qps"]):.2f}" r="4" fill="#d97706"/>'
    for r in rows
)

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<rect width="100%" height="100%" fill="#fcfcfb"/>
<text x="{left}" y="28" font-size="22" font-weight="700" fill="#111">{dataset_name} ef_search sweep</text>
<text x="{left}" y="46" font-size="13" fill="#555">Recall and QPS by hnsw_ef_search</text>
{''.join(grid_lines)}
<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" stroke="#333" stroke-width="1.2"/>
<line x1="{left + plot_w}" y1="{top}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="#333" stroke-width="1.2"/>
<line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="#333" stroke-width="1.2"/>
{''.join(x_ticks)}
{''.join(left_ticks)}
{''.join(right_ticks)}
<path d="{recall_path}" fill="none" stroke="#17803d" stroke-width="3"/>
<path d="{qps_path}" fill="none" stroke="#d97706" stroke-width="3"/>
{recall_points}
{qps_points}
<text x="{left + plot_w / 2:.2f}" y="{height - 20}" text-anchor="middle" font-size="14" fill="#222">hnsw_ef_search</text>
<text x="24" y="{top + plot_h / 2:.2f}" text-anchor="middle" font-size="14" fill="#0f5c2e" transform="rotate(-90 24 {top + plot_h / 2:.2f})">Recall@K</text>
<text x="{width - 20}" y="{top + plot_h / 2:.2f}" text-anchor="middle" font-size="14" fill="#8a4b00" transform="rotate(90 {width - 20} {top + plot_h / 2:.2f})">QPS</text>
<rect x="{left + 10}" y="{top + 10}" width="14" height="14" fill="#17803d"/>
<text x="{left + 32}" y="{top + 22}" font-size="13" fill="#222">Recall</text>
<rect x="{left + 110}" y="{top + 10}" width="14" height="14" fill="#d97706"/>
<text x="{left + 132}" y="{top + 22}" font-size="13" fill="#222">QPS</text>
</svg>'''

with open(svg_path, "w", encoding="utf-8") as f:
    f.write(svg)
PY
}

run_ef_search_experiments() {
  local ef

  write_result_csv_header

  for ef in $HNSW_EF_SEARCH_VALUES; do
    HNSW_EF_SEARCH="$ef"
    measure_recall
    append_result_csv
  done

  render_result_svg
  log "saved ef_search results to $RESULT_CSV"
  log "saved ef_search graph to $RESULT_SVG"
  log "saved excluded query list to $EXCLUDED_QUERY_FILE"
}

main() {
  require_cmd cubrid
  require_cmd csql
  require_cmd awk
  require_cmd perl
  require_cmd rg
  require_cmd python3

  if (( PERF_ENABLE == 1 )); then
    require_cmd perf
    require_cmd ps
    log "perf capture enabled: output_dir=$PERF_OUTPUT_DIR, stat_enable=$PERF_STAT_ENABLE, targets=[$PERF_TARGETS], record_targets=[$PERF_RECORD_TARGETS], record_profile=$PERF_RECORD_PROFILE"
  fi

  run_stage "reset db" setup_demo_db
  run_stage "bulk load" load_dataset

  if run_stage "validate dataset classes" validate_required_classes; then
    :
  else
    log "dataset validation failed; clearing caches and retrying bulk load once"
    invalidate_dataset_cache
    run_stage "reset db (retry)" setup_demo_db
    run_stage "bulk load (retry)" load_dataset
    run_stage "validate dataset classes (retry)" validate_required_classes
  fi

  run_stage "prepare query ids" prepare_query_ids
  run_stage "prepare ground truth" prepare_ground_truth_cache
  run_stage "build index" build_index
  run_stage "ef_search sweep" run_ef_search_experiments
}

main "$@"
