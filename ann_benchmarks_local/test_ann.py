#!/usr/bin/env python3

import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple
from urllib.request import build_opener, install_opener, urlretrieve


SCRIPT_DIR = Path(__file__).resolve().parent
DATA_DIR = SCRIPT_DIR
VENDOR_DIR = SCRIPT_DIR / "vendor"
VENDOR_PYTHON_PACKAGES = VENDOR_DIR / "python-packages"
VENDOR_CUBRID_PYTHON = VENDOR_DIR / "cubrid-python"

for extra_path in (VENDOR_PYTHON_PACKAGES, VENDOR_CUBRID_PYTHON):
    if extra_path.exists():
        sys.path.insert(0, str(extra_path))

try:
    import h5py
except ImportError as exc:
    raise SystemExit(
        f"failed to import h5py from {VENDOR_PYTHON_PACKAGES}; install vendor dependencies first"
    ) from exc

try:
    import CUBRIDdb
except ImportError as exc:
    raise SystemExit(
        f"failed to import CUBRIDdb from {VENDOR_CUBRID_PYTHON}; build the vendored cubrid-python driver first"
    ) from exc


def env_str(name: str, default: str = "") -> str:
    value = os.getenv(name)
    if value is None or value == "":
        return default
    return value


def env_int(name: str, default: int) -> int:
    return int(env_str(name, str(default)))


DB_NAME = env_str("DB_NAME", "demodb")
DB_USER = env_str("DB_USER", "dba")
DATASET_HDF5 = Path(env_str("DATASET_HDF5", str(SCRIPT_DIR / "nytimes-256-angular.hdf5")))
DATASET_DOWNLOAD_URL = env_str("DATASET_DOWNLOAD_URL", "")
TOPK = 10
PROGRESS_EVERY = env_int("PROGRESS_EVERY", 100)
TRAIN_ROW_LIMIT = env_str("TRAIN_ROW_LIMIT", "")
HNSW_M = env_int("HNSW_M", 24)
HNSW_EF_CONSTRUCTION = env_int("HNSW_EF_CONSTRUCTION", 200)
HNSW_EF_SEARCH_VALUES = [int(v) for v in env_str("HNSW_EF_SEARCH_VALUES", "200 400").split()]

DATASET_FILENAME = DATASET_HDF5.name
DATASET_NAME = DATASET_HDF5.stem
DATASET_FILE_STEM = DATASET_NAME.replace("-", "_")
SCHEMA_FILE = DATA_DIR / f"{DATASET_FILE_STEM}_schema"
OBJECT_FILE = DATA_DIR / f"{DATASET_FILE_STEM}_object"
OBJECT_NAN_MARKER = SCRIPT_DIR / f"{DATASET_FILE_STEM}_object.nan_sanitized"
OBJECT_LOAD_MARKER = SCRIPT_DIR / f"{DATASET_FILE_STEM}_object.load_sanitized"
RESULT_CSV = SCRIPT_DIR / f"{DATASET_NAME}_ef_search_results.csv"
RESULT_SVG = SCRIPT_DIR / f"{DATASET_NAME}_ef_search_results.svg"
EXCLUDED_QUERY_FILE = SCRIPT_DIR / f"{DATASET_NAME}_excluded_queries.txt"
QUERY_ID_FILE = SCRIPT_DIR / f"{DATASET_NAME}_query_ids.txt"
QUERY_ID_CACHE_MARKER = SCRIPT_DIR / f"{DATASET_NAME}_query_ids.cache_ready"
GT_CACHE_FILE = SCRIPT_DIR / f"{DATASET_NAME}_gt_topk{TOPK}.out"
GT_CACHE_MARKER = SCRIPT_DIR / f"{DATASET_NAME}_gt_topk{TOPK}.cache_ready"


@dataclass
class BenchmarkResult:
    queries: int
    total_hits: int
    recall: float
    elapsed_sec: float
    qps: float
    ef_search: int


NATIVE_VECTOR_BIND_SUPPORTED: Optional[bool] = None


def log(message: str) -> None:
    print(f"[{datetime.now().strftime('%F %T')}] {message}", flush=True)


def run_stage(stage_name: str, fn) -> None:
    log(f"stage start: {stage_name}")
    start_ts = time.time()
    try:
        fn()
    except Exception:
        elapsed = int(time.time() - start_ts)
        log(f"stage failed: {stage_name} ({elapsed}s)")
        raise
    elapsed = int(time.time() - start_ts)
    log(f"stage done: {stage_name} ({elapsed}s)")


def require_cmd(name: str) -> None:
    if shutil.which(name) is None:
        raise SystemExit(f"missing command: {name}")


def run_cmd(args: Sequence[str], *, cwd: Optional[Path] = None) -> None:
    subprocess.run(list(args), cwd=str(cwd) if cwd else None, check=True)


def run_capture(args: Sequence[str]) -> str:
    completed = subprocess.run(list(args), check=True, capture_output=True, text=True)
    return completed.stdout


def get_dataset_download_url() -> str:
    if DATASET_DOWNLOAD_URL:
        return DATASET_DOWNLOAD_URL
    return f"https://ann-benchmarks.com/{DATASET_FILENAME}"


def download_file_with_python(source_url: str, destination_path: Path) -> None:
    destination_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_file = Path(tempfile.mktemp(prefix=".dataset_download.", dir=str(destination_path.parent)))
    try:
        opener = build_opener()
        opener.addheaders = [("User-agent", "Mozilla/5.0")]
        install_opener(opener)
        urlretrieve(source_url, str(tmp_file))
        tmp_file.replace(destination_path)
    finally:
        if tmp_file.exists():
            tmp_file.unlink()


def ensure_dataset_hdf5() -> bool:
    if DATASET_HDF5.exists():
        return True

    dataset_url = get_dataset_download_url()
    log(f"dataset hdf5 not found: {DATASET_HDF5}")
    log(f"downloading dataset from {dataset_url}")
    try:
        download_file_with_python(dataset_url, DATASET_HDF5)
    except Exception as exc:
        print(f"failed to download dataset: {dataset_url} -> {DATASET_HDF5}: {exc}", file=sys.stderr)
        return False

    log(f"downloaded dataset to {DATASET_HDF5}")
    return True


def get_cub_server_pid() -> str:
    output = run_capture(["ps", "-eo", "pid=,comm=,args="])
    for line in output.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3:
            continue
        pid, comm, args = parts
        if comm == "cub_server" and DB_NAME in args:
            return pid
    return ""


def csql_available() -> bool:
    return subprocess.run(
        ["csql", "-u", DB_USER, "-q", "-N", DB_NAME, "-c", "SELECT 1;"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    ).returncode == 0


def ensure_db_access() -> None:
    if csql_available():
        return

    log(f"database access check failed for {DB_NAME}; attempting to restore cub_master/server access")
    subprocess.run(["cubrid", "server", "start", DB_NAME], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    if csql_available():
        return

    server_pid = get_cub_server_pid()
    if server_pid:
        log(f"restarting orphaned cub_server pid={server_pid} for {DB_NAME}")
        subprocess.run(["kill", "-TERM", server_pid], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2)

    subprocess.run(["cubrid", "server", "start", DB_NAME], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    if not csql_available():
        raise SystemExit(f"failed to restore database access for {DB_NAME}")


def connect_db():
    ensure_db_access()
    return CUBRIDdb.connect(f"CUBRID:localhost:33000:{DB_NAME}:::", DB_USER, "")


def sanitize_schema_file() -> None:
    if not SCHEMA_FILE.exists():
        return
    text = SCHEMA_FILE.read_text(encoding="utf-8")
    if "/nytimes_256_angular_" not in text:
        return
    log(f"normalizing schema file: {SCHEMA_FILE}")
    text = text.replace("CREATE TABLE /nytimes_256_angular_train", "CREATE TABLE nytimes_256_angular_train")
    text = text.replace("CREATE TABLE /nytimes_256_angular_test", "CREATE TABLE nytimes_256_angular_test")
    text = text.replace("CREATE TABLE /nytimes_256_angular_answer", "CREATE TABLE nytimes_256_angular_answer")
    SCHEMA_FILE.write_text(text, encoding="utf-8")


def write_load_schema_file(load_schema_file: Path) -> None:
    removed = False
    with SCHEMA_FILE.open("r", encoding="utf-8") as src, load_schema_file.open("w", encoding="utf-8") as dst:
        for line in src:
            stripped = line.lstrip()
            if stripped.startswith("CREATE VECTOR INDEX "):
                removed = True
                continue
            dst.write(line)
    if removed:
        print("removed CREATE VECTOR INDEX statements from load schema", file=sys.stderr)


def sanitize_object_file() -> None:
    if not OBJECT_FILE.exists():
        return

    text = OBJECT_FILE.read_text(encoding="utf-8")
    if "/nytimes_256_angular_" in text:
        log(f"normalizing object file header: {OBJECT_FILE}")
        text = text.replace("%id /nytimes_256_angular_train", "%id nytimes_256_angular_train")
        text = text.replace("%id /nytimes_256_angular_test", "%id nytimes_256_angular_test")
        text = text.replace("%id /nytimes_256_angular_answer", "%id nytimes_256_angular_answer")
        text = text.replace("%class /nytimes_256_angular_train", "%class nytimes_256_angular_train")
        text = text.replace("%class /nytimes_256_angular_test", "%class nytimes_256_angular_test")
        text = text.replace("%class /nytimes_256_angular_answer", "%class nytimes_256_angular_answer")
        OBJECT_FILE.write_text(text, encoding="utf-8")

    if not OBJECT_NAN_MARKER.exists() or OBJECT_NAN_MARKER.stat().st_mtime < OBJECT_FILE.stat().st_mtime:
        if "nan" in OBJECT_FILE.read_text(encoding="utf-8").lower():
            log(f"replacing nan values with NULL in {OBJECT_FILE}")
            OBJECT_FILE.write_text(
                OBJECT_FILE.read_text(encoding="utf-8").replace("nan", "NULL").replace("NaN", "NULL"),
                encoding="utf-8",
            )
        OBJECT_NAN_MARKER.touch()

    if not OBJECT_LOAD_MARKER.exists() or OBJECT_LOAD_MARKER.stat().st_mtime < OBJECT_FILE.stat().st_mtime:
        log(f"dropping malformed vector rows in {OBJECT_FILE}")
        output_lines: List[str] = []
        current_class = ""
        with OBJECT_FILE.open("r", encoding="utf-8") as src:
            for line in src:
                if line.startswith("%class "):
                    parts = line.strip().split()
                    current_class = parts[1] if len(parts) >= 2 else ""
                    output_lines.append(line)
                    continue
                if current_class in {"nytimes_256_angular_train", "nytimes_256_angular_test"}:
                    stripped = line.strip()
                    if stripped.endswith(" NULL") or ("NULL" in stripped and stripped.startswith(tuple("0123456789"))):
                        continue
                output_lines.append(line)
        OBJECT_FILE.write_text("".join(output_lines), encoding="utf-8")
        OBJECT_LOAD_MARKER.touch()


def write_load_object_file(load_object_file: Path) -> Path:
    if not TRAIN_ROW_LIMIT:
        return OBJECT_FILE

    limit = int(TRAIN_ROW_LIMIT)
    log(f"limiting nytimes_256_angular_train rows to {limit} for load")
    train_count = 0
    current_class = ""
    with OBJECT_FILE.open("r", encoding="utf-8") as src, load_object_file.open("w", encoding="utf-8") as dst:
        for line in src:
            if line.startswith("%class "):
                parts = line.strip().split()
                current_class = parts[1] if len(parts) >= 2 else ""
                dst.write(line)
                continue
            if current_class == "nytimes_256_angular_train" and line[:1].isdigit():
                if train_count >= limit:
                    continue
                train_count += 1
            dst.write(line)
    return load_object_file


def query_rows(query: str, args: Optional[Sequence] = None) -> List[Tuple]:
    conn = connect_db()
    cur = conn.cursor()
    try:
        cur.execute(query, args)
        return cur.fetchall()
    finally:
        cur.close()
        conn.close()


def validate_required_classes() -> None:
    missing = []
    for class_name in (
        "nytimes_256_angular_train",
        "nytimes_256_angular_test",
        "nytimes_256_angular_answer",
    ):
        rows = query_rows(f"SELECT class_name FROM db_class WHERE class_name = '{class_name}'")
        found = rows[0][0] if rows else None
        if found != class_name:
            missing.append(class_name)
    if missing:
        raise RuntimeError(f"required dataset classes are missing in {DB_NAME}: {' '.join(missing)}")


def invalidate_dataset_cache() -> None:
    log(f"invalidating cached query/results artifacts for {DATASET_NAME}")
    for path in (
        QUERY_ID_FILE,
        EXCLUDED_QUERY_FILE,
        QUERY_ID_CACHE_MARKER,
        GT_CACHE_FILE,
        GT_CACHE_MARKER,
        RESULT_CSV,
        RESULT_SVG,
    ):
        if path.exists():
            path.unlink()


def setup_demo_db() -> None:
    if "CUBRID" not in os.environ:
        raise SystemExit("CUBRID environment variable is not set.")

    cubrid_conf = Path(os.environ["CUBRID"]) / "conf" / "cubrid.conf"
    if "stored_procedure=no" not in cubrid_conf.read_text(encoding="utf-8"):
        with cubrid_conf.open("a", encoding="utf-8") as fp:
            fp.write("stored_procedure=no\n")

    subprocess.run(["cubrid", "server", "stop", DB_NAME], check=False)
    subprocess.run(["cubrid", "deletedb", DB_NAME], check=False)

    run_cmd(["./make_cubrid_demo.sh"], cwd=Path(os.environ["CUBRID"]) / "demo")
    run_cmd(["cubrid", "server", "start", DB_NAME])


def load_dataset() -> None:
    def load_schema_and_object_files() -> None:
        sanitize_schema_file()
        sanitize_object_file()
        load_schema_file = Path(tempfile.mktemp(prefix="nytimes_load_schema.", dir="/tmp"))
        load_object_file = Path(tempfile.mktemp(prefix="nytimes_load_object.", dir="/tmp"))
        try:
            write_load_schema_file(load_schema_file)
            object_input_file = write_load_object_file(load_object_file)
            log(f"loading dataset from {SCHEMA_FILE} / {OBJECT_FILE}")
            run_cmd(
                [
                    "cubrid",
                    "loaddb",
                    "-s",
                    str(load_schema_file),
                    "-d",
                    str(object_input_file),
                    "-C",
                    "-u",
                    DB_USER,
                    DB_NAME,
                    "-v",
                    "--no-statistics",
                ]
            )
        finally:
            load_schema_file.unlink(missing_ok=True)
            load_object_file.unlink(missing_ok=True)

    if SCHEMA_FILE.exists() and OBJECT_FILE.exists():
        load_schema_and_object_files()
        return

    if ensure_dataset_hdf5():
        log(f"converting hdf5 dataset to loaddb files from {DATASET_HDF5}")
        run_cmd(["cubrid", "loaddb", "-h", str(DATASET_HDF5), "-C", "-u", DB_USER, DB_NAME, "--no-statistics"])
        if SCHEMA_FILE.exists() and OBJECT_FILE.exists():
            load_schema_and_object_files()
            return
        raise SystemExit(f"dataset conversion did not produce expected files: {SCHEMA_FILE}, {OBJECT_FILE}")

    raise SystemExit(f"dataset not found or download failed. checked: {DATASET_HDF5}, {SCHEMA_FILE}, {OBJECT_FILE}")


def load_hdf5_dataset():
    if not ensure_dataset_hdf5():
        raise SystemExit(f"dataset not found or download failed: {DATASET_HDF5}")
    return h5py.File(DATASET_HDF5, "r")


def build_index() -> None:
    log(f"creating vector index (M={HNSW_M}, ef_construction={HNSW_EF_CONSTRUCTION})")
    conn = connect_db()
    cur = conn.cursor()
    try:
        cur.execute(
            "CREATE VECTOR INDEX vidx_nytimes_train ON nytimes_256_angular_train "
            f"(vec COSINE) WITH (M = {HNSW_M}, ef_construction = {HNSW_EF_CONSTRUCTION})"
        )
    finally:
        cur.close()
        conn.close()


def format_vector_literal(values: Iterable[float]) -> str:
    return "[" + ",".join(f"{float(value):.9g}" for value in values) + "]"


def vector_query_rows(cur, vector_values: Sequence[float]) -> List[Tuple[int]]:
    global NATIVE_VECTOR_BIND_SUPPORTED

    sql = (
        "SELECT /*+ recompile no_parallel_heap_scan */ id "
        "FROM nytimes_256_angular_train "
        "ORDER BY vec <c> ? "
        f"LIMIT {TOPK}"
    )

    if NATIVE_VECTOR_BIND_SUPPORTED is not False:
        try:
            cur.execute(sql, (CUBRIDdb.Vector(vector_values),))
            if NATIVE_VECTOR_BIND_SUPPORTED is None:
                log("native vector bind is available through vendored cubrid-python")
                NATIVE_VECTOR_BIND_SUPPORTED = True
            return cur.fetchall()
        except Exception as exc:
            if NATIVE_VECTOR_BIND_SUPPORTED is None:
                log(f"native vector bind failed; falling back to vector literals: {exc}")
                NATIVE_VECTOR_BIND_SUPPORTED = False
            else:
                raise

    literal_sql = (
        "SELECT /*+ recompile no_parallel_heap_scan */ id "
        "FROM nytimes_256_angular_train "
        f"ORDER BY vec <c> cast('{format_vector_literal(vector_values)}' as vector) "
        f"LIMIT {TOPK}"
    )
    cur.execute(literal_sql)
    return cur.fetchall()


def write_result_csv_header() -> None:
    RESULT_CSV.write_text(
        "dataset,queries,excluded_queries,topk,hnsw_m,hnsw_ef_construction,hnsw_ef_search,total_hits,recall,elapsed_sec,qps\n",
        encoding="utf-8",
    )


def append_result_csv(result: BenchmarkResult, excluded_query_count: int) -> None:
    with RESULT_CSV.open("a", encoding="utf-8") as fp:
        fp.write(
            ",".join(
                [
                    DATASET_NAME,
                    str(result.queries),
                    str(excluded_query_count),
                    str(TOPK),
                    str(HNSW_M),
                    str(HNSW_EF_CONSTRUCTION),
                    str(result.ef_search),
                    str(result.total_hits),
                    f"{result.recall:.6f}",
                    f"{result.elapsed_sec:.6f}",
                    f"{result.qps:.3f}",
                ]
            )
            + "\n"
        )


def render_result_svg() -> None:
    import csv

    rows = []
    with RESULT_CSV.open("r", encoding="utf-8", newline="") as fp:
        for row in csv.DictReader(fp):
            rows.append(
                {
                    "ef_search": int(row["hnsw_ef_search"]),
                    "recall": float(row["recall"]),
                    "qps": float(row["qps"]),
                }
            )

    if not rows:
        raise RuntimeError("no rows to plot")

    rows.sort(key=lambda r: r["ef_search"])
    efs = [r["ef_search"] for r in rows]
    recalls = [r["recall"] for r in rows]
    qpss = [r["qps"] for r in rows]

    width, height = 980, 620
    left, right, top, bottom = 90, 90, 50, 70
    plot_w = width - left - right
    plot_h = height - top - bottom

    min_x, max_x = min(efs), max(efs)
    min_recall, max_recall = min(recalls), max(recalls)
    min_qps, max_qps = min(qpss), max(qpss)

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
    max_qps += qps_pad

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
        grid_lines.append(
            f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}" stroke="#d7dde5" stroke-width="1"/>'
        )

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
        right_ticks.append(
            f'<line x1="{left + plot_w}" y1="{y:.2f}" x2="{left + plot_w + 6}" y2="{y:.2f}" stroke="#333"/>'
        )
        right_ticks.append(
            f'<text x="{left + plot_w + 10}" y="{y + 4:.2f}" text-anchor="start" font-size="12" fill="#8a4b00">{value:.1f}</text>'
        )

    recall_points = "\n".join(
        f'<circle cx="{x_of(r["ef_search"]):.2f}" cy="{y_recall(r["recall"]):.2f}" r="4" fill="#17803d"/>'
        for r in rows
    )
    qps_points = "\n".join(
        f'<circle cx="{x_of(r["ef_search"]):.2f}" cy="{y_qps(r["qps"]):.2f}" r="4" fill="#d97706"/>'
        for r in rows
    )

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<rect width="100%" height="100%" fill="#fcfcfb"/>
<text x="{left}" y="28" font-size="22" font-weight="700" fill="#111">{DATASET_NAME} ef_search sweep</text>
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
</svg>"""

    RESULT_SVG.write_text(svg, encoding="utf-8")


def prepare_query_ids() -> Tuple[List[int], int]:
    if QUERY_ID_FILE.exists() and EXCLUDED_QUERY_FILE.exists() and QUERY_ID_CACHE_MARKER.exists():
        query_ids = [int(line.strip()) for line in QUERY_ID_FILE.read_text().splitlines() if line.strip()]
        excluded_count = sum(1 for line in EXCLUDED_QUERY_FILE.read_text().splitlines() if line and not line.startswith("#"))
        if query_ids:
            log(f"reusing cached query ids: {len(query_ids)} valid queries, {excluded_count} excluded")
            return query_ids, excluded_count

    log(f"building query id cache directly from {DATASET_HDF5}")

    valid_ids: List[int] = []
    excluded_ids: List[int] = []

    with load_hdf5_dataset() as dataset:
        for qid, row in enumerate(dataset["distances"]):
            has_valid_distance = any(not math.isnan(float(value)) for value in row)
            if has_valid_distance:
                valid_ids.append(qid)
            else:
                excluded_ids.append(qid)

    EXCLUDED_QUERY_FILE.write_text(
        "# Excluded nytimes queries\n"
        "# reason: no valid ground-truth distances in hdf5 distances dataset.\n"
        + "\n".join(str(qid) for qid in excluded_ids)
        + ("\n" if excluded_ids else ""),
        encoding="utf-8",
    )
    QUERY_ID_FILE.write_text("".join(f"{qid}\n" for qid in valid_ids), encoding="utf-8")
    QUERY_ID_CACHE_MARKER.touch()

    log(f"excluded queries: {len(excluded_ids)} (saved to {EXCLUDED_QUERY_FILE})")
    log(f"cached valid query ids to {QUERY_ID_FILE}")
    return valid_ids, len(excluded_ids)


def prepare_ground_truth_cache(query_ids: Sequence[int]) -> Dict[int, set]:
    if GT_CACHE_FILE.exists() and GT_CACHE_MARKER.exists() and GT_CACHE_MARKER.stat().st_mtime >= QUERY_ID_FILE.stat().st_mtime:
        log(f"reusing cached ground-truth neighbors from {GT_CACHE_FILE}")
        return load_ground_truth_cache()

    log(f"building ground-truth cache directly from {DATASET_HDF5}")
    with load_hdf5_dataset() as dataset, GT_CACHE_FILE.open("w", encoding="utf-8") as fp:
        neighbors = dataset["neighbors"]
        distances = dataset["distances"]
        for qid in query_ids:
            wrote = 0
            for rid, dist in zip(neighbors[qid], distances[qid]):
                if math.isnan(float(dist)):
                    continue
                fp.write(f"2|{qid}|{int(rid)}\n")
                wrote += 1
                if wrote >= TOPK:
                    break

    GT_CACHE_MARKER.touch()
    log(f"cached ground-truth neighbors to {GT_CACHE_FILE}")
    return load_ground_truth_cache()


def load_ground_truth_cache() -> Dict[int, set]:
    gt_hits: Dict[int, set] = {}
    with GT_CACHE_FILE.open("r", encoding="utf-8") as fp:
        for line in fp:
            parts = line.strip().split("|")
            if len(parts) != 3 or parts[0] != "2":
                continue
            qid = int(parts[1])
            rid = int(parts[2])
            gt_hits.setdefault(qid, set()).add(rid)
    return gt_hits


def measure_recall(query_ids: Sequence[int], gt_hits: Dict[int, set], excluded_query_count: int, ef_search: int) -> BenchmarkResult:
    total_queries = len(query_ids)
    log(f"measuring recall@{TOPK} for {total_queries} queries (hnsw_ef_search={ef_search})")
    start_time = time.perf_counter()

    conn = connect_db()
    cur = conn.cursor()
    try:
        cur.execute(f"SET SYSTEM PARAMETERS 'hnsw_ef_search={ef_search}'")

        total_hits = 0
        processed = 0

        with load_hdf5_dataset() as dataset:
            test_vectors = dataset["test"]
            for qid in query_ids:
                rows = vector_query_rows(cur, test_vectors[qid])
                ann_ids = [int(row[0]) for row in rows if row and row[0] is not None]
                gt = gt_hits.get(qid, set())
                total_hits += sum(1 for rid in ann_ids if rid in gt)
                processed += 1

                if processed % PROGRESS_EVERY == 0:
                    partial = total_hits / float(processed * TOPK)
                    log(f"progress: {processed}/{total_queries} queries, partial recall@{TOPK}={partial:.6f}")
    finally:
        cur.close()
        conn.close()

    elapsed_sec = time.perf_counter() - start_time
    recall = total_hits / float(processed * TOPK)
    qps = processed / elapsed_sec

    print()
    print(f"dataset={DATASET_NAME}")
    print(f"queries={processed}")
    print(f"topk={TOPK}")
    print(f"hnsw_m={HNSW_M}")
    print(f"hnsw_ef_construction={HNSW_EF_CONSTRUCTION}")
    print(f"hnsw_ef_search={ef_search}")
    print(f"excluded_queries={excluded_query_count}")
    print(f"total_hits={total_hits}")
    print(f"recall@{TOPK}={recall:.6f}")
    print(f"elapsed_sec={elapsed_sec:.6f}")
    print(f"qps={qps:.3f}")

    return BenchmarkResult(
        queries=processed,
        total_hits=total_hits,
        recall=recall,
        elapsed_sec=elapsed_sec,
        qps=qps,
        ef_search=ef_search,
    )


def run_ef_search_experiments(query_ids: Sequence[int], gt_hits: Dict[int, set], excluded_query_count: int) -> None:
    write_result_csv_header()
    for ef_search in HNSW_EF_SEARCH_VALUES:
        result = measure_recall(query_ids, gt_hits, excluded_query_count, ef_search)
        append_result_csv(result, excluded_query_count)
    render_result_svg()
    log(f"saved ef_search results to {RESULT_CSV}")
    log(f"saved ef_search graph to {RESULT_SVG}")
    log(f"saved excluded query list to {EXCLUDED_QUERY_FILE}")


def main() -> None:
    require_cmd("cubrid")
    require_cmd("csql")
    require_cmd("awk")
    require_cmd("perl")
    require_cmd("rg")
    require_cmd("python3")

    run_stage("reset db", setup_demo_db)
    run_stage("bulk load", load_dataset)

    try:
        run_stage("validate dataset classes", validate_required_classes)
    except Exception:
        log("dataset validation failed; clearing caches and retrying bulk load once")
        invalidate_dataset_cache()
        run_stage("reset db (retry)", setup_demo_db)
        run_stage("bulk load (retry)", load_dataset)
        run_stage("validate dataset classes (retry)", validate_required_classes)

    query_ids: List[int] = []
    excluded_query_count = 0
    gt_hits: Dict[int, set] = {}

    def stage_prepare_query_ids() -> None:
        nonlocal query_ids, excluded_query_count
        query_ids, excluded_query_count = prepare_query_ids()

    def stage_prepare_gt() -> None:
        nonlocal gt_hits
        gt_hits = prepare_ground_truth_cache(query_ids)

    run_stage("prepare query ids", stage_prepare_query_ids)
    run_stage("prepare ground truth", stage_prepare_gt)
    run_stage("build index", build_index)
    run_stage("ef_search sweep", lambda: run_ef_search_experiments(query_ids, gt_hits, excluded_query_count))


if __name__ == "__main__":
    main()
