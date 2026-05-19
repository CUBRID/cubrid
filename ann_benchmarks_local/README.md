# Local ANN Benchmarks

This directory documents how to run the local ANN recall benchmark using `ann_benchmarks_local/test_ann.sh`.

## Script

Benchmark script:

- `ann_benchmarks_local/test_ann.sh`

## Prerequisites

- `CUBRID` must point to the local CUBRID install.
- `cubrid`, `csql`, `awk`, `perl`, `rg`, and `python3` must be available.
- Dataset files are resolved in this order:
  - `ann_benchmarks_local/nytimes_256_angular_schema`
  - `ann_benchmarks_local/nytimes_256_angular_object`
  - `ann_benchmarks_local/nytimes-256-angular.hdf5`
  - if the `.hdf5` file is missing, the script downloads `https://ann-benchmarks.com/nytimes-256-angular.hdf5` automatically

## Basic Run

From the repository root:

```bash
./ann_benchmarks_local/test_ann.sh
```

This does the full flow:

- recreate `demodb`
- load `nytimes-256-angular`
- build the HNSW index
- measure recall
- write CSV/SVG results

This script only supports the full path above. Reusing an existing DB, skipping bulk load, or skipping index build is intentionally not supported.

## Quick Guide

If you are just running the benchmark:

- change `DATASET_HDF5` only when you want a different dataset
- change `HNSW_EF_SEARCH_VALUES` first
- change `HNSW_M` and `HNSW_EF_CONSTRUCTION` when you want to compare build settings
- change `TOPK` only if you need a different recall target

If you also want perf:

- set `PERF_ENABLE=1`
- set `PERF_STAT_ENABLE=0` if you want only `perf record` attribution without `perf stat`
- keep `PERF_RECORD_TARGETS="cub_server"` unless you specifically need `csql`
- choose one `PERF_RECORD_PROFILE` per run:
  - `hot`
  - `instructions`
  - `branch`
  - `cache`

## Common Commands

Default run:

```bash
./ann_benchmarks_local/test_ann.sh
```

Run only `ef_search=200,400` explicitly:

```bash
HNSW_EF_SEARCH_VALUES="200 400" ./ann_benchmarks_local/test_ann.sh
```

Use a different dataset `.hdf5` base name. The script derives the matching schema/object files and output file names automatically:

```bash
DATASET_HDF5=./ann_benchmarks_local/glove-100-angular.hdf5 ./ann_benchmarks_local/test_ann.sh
```

Override the dataset download URL when the default `ann-benchmarks.com/<dataset>.hdf5` location is not what you want:

```bash
DATASET_DOWNLOAD_URL=https://ann-benchmarks.com/glove-100-angular.hdf5 \
DATASET_HDF5=./ann_benchmarks_local/glove-100-angular.hdf5 \
./ann_benchmarks_local/test_ann.sh
```

Tune build/search parameters:

```bash
HNSW_M=24 HNSW_EF_CONSTRUCTION=200 HNSW_EF_SEARCH_VALUES="200 400 800" ./ann_benchmarks_local/test_ann.sh
```

Collect `perf stat` for both `csql` and `cub_server` during the full build/query flow:

```bash
PERF_ENABLE=1 HNSW_EF_SEARCH_VALUES="200" ./ann_benchmarks_local/test_ann.sh
```

Collect `perf stat` plus call-graph samples for `cub_server`:

```bash
PERF_ENABLE=1 PERF_RECORD_TARGETS="cub_server" HNSW_EF_SEARCH_VALUES="200" ./ann_benchmarks_local/test_ann.sh
```

Collect hot-function attribution:

```bash
PERF_ENABLE=1 PERF_RECORD_TARGETS="cub_server" PERF_RECORD_PROFILE=hot HNSW_EF_SEARCH_VALUES="200" ./ann_benchmarks_local/test_ann.sh
```

Collect instruction-count attribution:

```bash
PERF_ENABLE=1 PERF_RECORD_TARGETS="cub_server" PERF_RECORD_PROFILE=instructions HNSW_EF_SEARCH_VALUES="200" ./ann_benchmarks_local/test_ann.sh
```

Collect branch-miss attribution:

```bash
PERF_ENABLE=1 PERF_RECORD_TARGETS="cub_server" PERF_RECORD_PROFILE=branch HNSW_EF_SEARCH_VALUES="200" ./ann_benchmarks_local/test_ann.sh
```

Collect cache-miss attribution:

```bash
PERF_ENABLE=1 PERF_STAT_ENABLE=0 PERF_RECORD_TARGETS="cub_server" PERF_RECORD_PROFILE=cache HNSW_EF_SEARCH_VALUES="200" ./ann_benchmarks_local/test_ann.sh
```

Generate flame graphs from `perf record` data. If `ann_benchmarks_local/flame_graph` is missing, the script downloads FlameGraph tools automatically:

```bash
PERF_ENABLE=1 \
PERF_RECORD_TARGETS="cub_server csql" \
HNSW_EF_SEARCH_VALUES="200" \
./ann_benchmarks_local/test_ann.sh
```

## Environment Variables

Only user-facing variables are listed here. The script derives per-dataset file names and the per-run single `ef_search` value internally.

### Benchmark settings

Usually important:

- `DATASET_HDF5`
  Dataset identity. The script uses this file name to derive:
  - schema file name
  - object file name
  - result CSV/SVG names
  - query-id cache names
  - perf output directory name
  If the `.hdf5` file is missing and schema/object files are also absent, the script tries to download it from `https://ann-benchmarks.com/<dataset>.hdf5`.
  Hyphens in the dataset base name are converted to underscores for schema/object/perf paths.

- `DATASET_DOWNLOAD_URL`
  Overrides the default dataset download URL used when `DATASET_HDF5` is missing.

- `HNSW_EF_SEARCH_VALUES`
  Controls which `ef_search` values are measured during recall testing.
  Default: `200 400`
- `HNSW_M`
  HNSW graph degree used at build time.
  Default: `24`
- `HNSW_EF_CONSTRUCTION`
  HNSW build-time search breadth.
  Default: `200`
- `TOPK`
  Recall@K target used during evaluation.
  Default: `10`

Sometimes important:

- `DB_NAME`
  Database name used for the full rebuild.
  Default: `demodb`
- `DB_USER`
  CSQL/loaddb user.
  Default: `dba`
- `PROGRESS_EVERY`
  Progress log interval during query evaluation.
  Default: `100`

### Perf settings

Usually important:

- `PERF_ENABLE`
  Turns on perf collection.
  Default: `0`
- `PERF_STAT_ENABLE`
  Turns `perf stat` on or off.
  Default: `1`
- `PERF_RECORD_TARGETS`
  Which processes also get `perf record`.
  Common choice: `cub_server`
- `PERF_RECORD_PROFILE`
  What kind of attribution to collect with `perf record`.
  Values:
  `hot` for CPU hot functions
  `instructions` for instruction-count attribution
  `branch` for branch-miss attribution
  `cache` for cache-miss attribution
  `custom` when using `PERF_RECORD_EVENT`

Sometimes important:

- `PERF_TARGETS`
  Which processes get `perf stat` when `PERF_STAT_ENABLE=1`.
  Default: `csql cub_server`
- `PERF_STAT_EVENTS`
  Event list for `perf stat`
- `PERF_RECORD_FREQ`
  Sampling frequency for `perf record`
- `PERF_RECORD_PERIOD`
  Period-based sampling. If set, it overrides `PERF_RECORD_FREQ`
- `PERF_CALL_GRAPH`
  Call-graph mode, usually `fp`

Advanced / rare:

- `PERF_RECORD_EVENT`
  Custom perf record event when `PERF_RECORD_PROFILE=custom`
- `PERF_FLAMEGRAPH`
  Enables flame graph generation
- `FLAMEGRAPH_DIR`
- `STACKCOLLAPSE_PERF`
- `FLAMEGRAPH_PL`

## Recommended Configurations

### Benchmark only

Change these first:

- `DATASET_HDF5` when you want a different dataset
- `HNSW_EF_SEARCH_VALUES`
- `HNSW_M`
- `HNSW_EF_CONSTRUCTION`
- `TOPK`

Recommended examples:

```bash
./ann_benchmarks_local/test_ann.sh
```

```bash
HNSW_EF_SEARCH_VALUES="200 400 800" ./ann_benchmarks_local/test_ann.sh
```

```bash
HNSW_M=32 HNSW_EF_CONSTRUCTION=400 HNSW_EF_SEARCH_VALUES="200 400" ./ann_benchmarks_local/test_ann.sh
```

### Benchmark with perf

Change these first:

- `DATASET_HDF5` when you want a different dataset
- `PERF_ENABLE=1`
- `PERF_STAT_ENABLE=0` if you want only function-level attribution
- `PERF_RECORD_TARGETS="cub_server"`
- `PERF_RECORD_PROFILE`
- `HNSW_EF_SEARCH_VALUES`

Recommended examples:

Hot functions:

```bash
PERF_ENABLE=1 \
PERF_RECORD_TARGETS="cub_server" \
PERF_RECORD_PROFILE=hot \
HNSW_EF_SEARCH_VALUES="200" \
./ann_benchmarks_local/test_ann.sh
```

Instruction attribution:

```bash
PERF_ENABLE=1 \
PERF_RECORD_TARGETS="cub_server" \
PERF_RECORD_PROFILE=instructions \
HNSW_EF_SEARCH_VALUES="200" \
./ann_benchmarks_local/test_ann.sh
```

Branch-miss attribution:

```bash
PERF_ENABLE=1 \
PERF_RECORD_TARGETS="cub_server" \
PERF_RECORD_PROFILE=branch \
HNSW_EF_SEARCH_VALUES="200" \
./ann_benchmarks_local/test_ann.sh
```

Cache-miss attribution:

```bash
PERF_ENABLE=1 \
PERF_STAT_ENABLE=0 \
PERF_RECORD_TARGETS="cub_server" \
PERF_RECORD_PROFILE=cache \
HNSW_EF_SEARCH_VALUES="200" \
./ann_benchmarks_local/test_ann.sh
```

Practical guidance:

- Use `hot` first to find the main hot path.
- Use `instructions` when you want “which functions retire the most work”.
- Use `branch` only when branch-miss rates already look suspicious in `perf stat`.
- Use `cache` when `cache-miss` rates are high and you need function-level attribution.
- Prefer separate runs for `hot`, `instructions`, `branch`, and `cache`. Mixing them in one run makes attribution noisier and adds overhead.

## Outputs

The script writes:

- `ann_benchmarks_local/<dataset-name>_ef_search_results.csv`
- `ann_benchmarks_local/<dataset-name>_ef_search_results.svg`
- `ann_benchmarks_local/<dataset-name>_excluded_queries.txt`
- `ann_benchmarks_local/<dataset-name>_query_ids.txt`
- `ann_benchmarks_local/perf_<dataset_name>/*.stat.csv` when `PERF_ENABLE=1`
- `ann_benchmarks_local/perf_<dataset_name>/*.data` when the target is included in `PERF_RECORD_TARGETS`
- `ann_benchmarks_local/perf_<dataset_name>/*.flamegraph.svg` when `PERF_RECORD_TARGETS` is set and FlameGraph tools are available
- `ann_benchmarks_local/perf_<dataset_name>/build_index_*` for full index build profiling
- `ann_benchmarks_local/perf_<dataset_name>/query_ef*` for full-query-set profiling per `hnsw_ef_search`
- `instructions`/`branch`/`cache` profiles add `.instructions`/`.branch`/`.cache` to `perf record` artifact names

## Notes

- Query ids are cached and reused if the cache files already exist.
- Excluded queries are queries with no valid non-NULL ground-truth distance.
- If you changed HNSW implementation code, rebuild CUBRID first, then rerun the script.
- `perf` collection is attached to the full `CREATE VECTOR INDEX` execution and to the full query run for each `hnsw_ef_search` value.
- `perf` may require a permissive `/proc/sys/kernel/perf_event_paranoid` setting or elevated privileges depending on the host.
- Flame graph generation provisions tools into `ann_benchmarks_local/flame_graph` on demand. It prefers `git clone` and falls back to downloading the GitHub tarball with `curl` or `wget`.
- Flame graph generation uses `ann_benchmarks_local/flame_graph` by default and also supports `STACKCOLLAPSE_PERF`, `FLAMEGRAPH_PL`, `FLAMEGRAPH_DIR`, `../FlameGraph`, or `PATH`.
