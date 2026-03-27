# COUNT(*) PK/Unique Index Optimization — 처리 시퀀스 문서

## 개요

CUBRID는 **unique index(PK 포함)를 가진 테이블**에 대해 **조건 없는 `COUNT(*)` 쿼리**를 실행할 때, 실제 테이블/인덱스를 스캔하지 않고 **B-tree 루트 헤더의 통계 정보(num_oids)**로부터 직접 결과를 가져오는 최적화를 수행한다.

이 최적화는 MVCC 환경에서 **스냅샷 시점의 일관된 통계**를 사용하기 위해, 클라이언트(파서/컴파일)에서 서버(실행기)까지 여러 단계에 걸친 협업으로 이루어진다.

```
SQL: SELECT COUNT(*) FROM t;  -- t에 PK 또는 unique index 존재

결과: 인덱스 헤더의 num_oids 값 → COUNT(*) 결과
      (테이블 스캔 없음, 인덱스 키 순회 없음)
```

---

## 전체 파이프라인 요약

```
[Phase 1] 파서/컴파일 (클라이언트)
  pt_class_pre_fetch()
    └→ pt_find_lck_classes()           -- 트리 순회로 SELECT 노드 방문
        └→ pt_select_eligible_count_optim_lock_hint()  -- 최적화 대상 판정
            └→ pt_add_lock_class(flags=LC_PREF_FLAG_COUNT_OPTIM)
    └→ locator_lockhint_classes()      -- 서버에 lock hint + 플래그 전달

[Phase 2] 서버: Lock Hint 처리 (스냅샷 이전)
  xlocator_find_lockhint_class_oids()
    └→ logtb_tran_prepare_count_optim_classes()  -- COS_TO_LOAD 마킹

[Phase 3] XASL 생성 (클라이언트)
  pt_to_buildvalue_proc()
    └→ pt_to_aggregate()
        └→ pt_to_aggregate_node()     -- COUNT(*) + unique index → BTID 기록 + agg_optimized=true

[Phase 4] XASL 직렬화 → 서버 전달 → 역직렬화

[Phase 5] 쿼리 실행 (서버)
  qexec_execute_query()
    ├→ qexec_reset_count_star_agg_optimized_flags_from_xasl()  -- XASL 캐시 재사용 시 플래그 복원
    ├→ qexec_prepare_count_optim_classes_from_xasl()           -- COS_TO_LOAD 마킹 (실행기 경로)
    └→ qexec_intprt_fnc()  (BUILDVALUE_PROC)
        └→ qexec_evaluate_aggregates_optimize()
            ├→ COS 상태 확인 (COS_LOADED 여부)
            ├→ 스냅샷 획득 → logtb_load_global_statistics_to_tran()  -- 통계 로드
            └→ qdata_evaluate_aggregate_optimize()
                └→ btree_get_unique_statistics_for_count()  -- 메모리 통계 읽기
                    └→ accumulator.curr_cnt = num_oids      -- 결과 설정
```

---

## Phase 1: 파서/컴파일 단계 (클라이언트)

### 1.1 최적화 대상 판정

**파일**: `src/parser/compile.c`

`pt_class_pre_fetch()` 함수는 쿼리 실행 전에 관련 클래스들의 선행 조회(prefetch)를 수행한다. 이 과정에서 `parser_walk_tree()`로 파스 트리를 순회하며 `pt_find_lck_classes()`를 호출한다.

`pt_find_lck_classes()`는 각 `PT_SELECT` 노드에 대해 `pt_select_eligible_count_optim_lock_hint()`를 호출하여 **COUNT(\*) 최적화 대상 여부를 판정**한다.

#### 판정 조건 (`pt_select_eligible_count_optim_lock_hint`)

다음 조건을 **모두** 만족해야 최적화 대상:

| 조건 | 설명 |
|------|------|
| 단일 테이블 FROM | `from->next == NULL`, entity_name이 `PT_NAME` |
| WHERE 절 없음 | `sel->where == NULL` |
| GROUP BY 없음 | `sel->group_by == NULL` |
| HAVING 없음 | `sel->having == NULL` |
| CONNECT BY / START WITH 없음 | `sel->connect_by == NULL && sel->start_with == NULL` |
| 윈도우 함수 없음 | `PT_SELECT_INFO_HAS_ANALYTIC` 플래그 미설정 |
| SELECT list에 `COUNT(*)` 정확히 1개 | `n_count_star == 1` |
| SELECT list에 다른 집계 함수 없음 | `pt_is_aggregate_function()` 인 항목이 없어야 함 |
| 스칼라 식 내 중첩 집계 없음 | `pt_find_non_count_star_aggregate()` walker로 검사 |

**핵심**: 이 판정은 **UNION ALL의 각 SELECT 브랜치에 대해 개별적으로** 수행된다. `parser_walk_tree()`가 트리를 재귀 순회하므로, UNION 아래의 각 SELECT 노드가 독립적으로 판정된다.

### 1.2 Lock Hint에 플래그 부착

최적화 대상으로 판정되면:

```
pt_find_lck_classes()
  → pt_add_lock_class(parser, lcks, from, LC_PREF_FLAG_COUNT_OPTIM)
```

- `lcks->flags[i]`에 `LC_PREF_FLAG_COUNT_OPTIM` (= `0x00000002`) 설정
- lock은 `NA_LOCK` (잠금 불필요 — 통계만 미리 준비하면 됨)
- 같은 클래스가 이미 등록되어 있으면 기존 플래그에 OR 연산

### 1.3 서버로 전달

```
pt_class_pre_fetch()
  → locator_lockhint_classes(num_classes, classes, locks, only_all, flags, ...)
```

클라이언트 `locator_cl.c`의 `locator_lockhint_classes()`가 서버의 `xlocator_find_lockhint_class_oids()`를 RPC로 호출하며, `flags[]` 배열이 서버로 전달된다.

**플래그 정의** (`src/transaction/locator.h`):

```c
enum lc_prefetch_flags
{
  LC_PREF_FLAG_LOCK        = 0x00000001,
  LC_PREF_FLAG_COUNT_OPTIM = 0x00000002
};
```

---

## Phase 2: 서버 Lock Hint 처리 (스냅샷 이전)

**파일**: `src/transaction/locator_sr.c`, `src/transaction/log_tran_table.c`

### 2.1 COS(Count Optimization State) 마킹

서버의 `xlocator_find_lockhint_class_oids()` 끝에서:

```
logtb_tran_prepare_count_optim_classes(thread_p, classnames, flags, n_classes)
```

이 함수는 `LC_PREF_FLAG_COUNT_OPTIM` 플래그가 설정된 각 클래스에 대해:

1. 클래스 OID를 찾음
2. 트랜잭션의 COS 해시에서 해당 클래스 항목을 조회/생성
3. `count_state`가 `COS_LOADED`가 아니면 → **`COS_TO_LOAD`로 설정**

### 2.2 COS 상태 머신

**정의** (`src/transaction/log_impl.h`):

```c
enum count_optim_state
{
  COS_NOT_LOADED = 0,  /* 글로벌 통계 미로드 */
  COS_TO_LOAD    = 1,  /* 스냅샷 획득 시 통계를 로드해야 함 */
  COS_LOADED     = 2   /* 통계 로드 완료 */
};
```

상태 전이:

```
COS_NOT_LOADED ──(prefetch 또는 실행기 마킹)──→ COS_TO_LOAD
COS_TO_LOAD    ──(스냅샷 획득 시 통계 로드)──→ COS_LOADED
COS_LOADED     ──(스냅샷 무효화, RC)──→ COS_NOT_LOADED  (리셋)
```

### 2.3 트랜잭션별 COS 해시

**구조** (`src/transaction/log_impl.h`):

```c
struct log_tran_class_cos
{
  OID class_oid;
  COUNT_OPTIM_STATE count_state;
};

struct log_tran_update_stats
{
  MHT_TABLE *classes_cos_hash;      /* 클래스별 COS 해시 */
  MHT_TABLE *unique_stats_hash;     /* 인덱스별 유니크 통계 해시 */
  ...
};
```

각 트랜잭션(`LOG_TDES`)은 자체 `log_upd_stats` 구조를 가지며, 여기에 클래스별 COS 상태와 인덱스별 유니크 통계가 해시로 관리된다.

---

## Phase 3: XASL 생성 단계 (클라이언트)

**파일**: `src/parser/xasl_generation.c`

### 3.1 BUILDVALUE_PROC 생성

조건 없는 단일 테이블 `COUNT(*)` 쿼리는 `BUILDVALUE_PROC` 타입의 XASL 노드로 생성된다:

```
pt_to_buildvalue_proc()
  → pt_to_aggregate()         -- AGGREGATE_TYPE 리스트 생성
    → pt_to_aggregate_node()  -- 개별 집계 노드 변환
  → buildvalue->agg_list = aggregate;
```

### 3.2 집계 최적화 플래그 설정

`pt_to_aggregate()`에서 최적화 조건 판정:

```c
if (pt_is_single_tuple(parser, select_node))
{
  if (where == NULL && pt_length_of_list(from) == 1
      && pt_length_of_list(from->info.spec.flat_entity_list) == 1
      && from->info.spec.only_all != PT_ALL)
  {
    info.class_name = from->info.spec.entity_name->info.name.original;
    info.flag_agg_optimize = true;   // ← 최적화 가능
  }
}
```

### 3.3 유니크 인덱스 BTID 부착

`pt_to_aggregate_node()`에서 `flag_agg_optimize == true`이고 `PT_COUNT_STAR`일 때:

```c
if (aggregate_list->function == PT_COUNT_STAR)
{
  need_unique_index = true;
  btid = sm_find_index(classop, NULL, 0, need_unique_index, false, &aggregate_list->btid);
  if (btid != NULL)
  {
    aggregate_list->flag.agg_optimized = true;  // ← 최적화 활성화
  }
}
```

- `sm_find_index()`: 스키마 매니저에서 해당 클래스의 **유니크 인덱스**를 찾음
- 찾은 인덱스의 **BTID**를 `aggregate_list->btid`에 기록
- `flag.agg_optimized = true` 설정

### 3.4 AGGREGATE_TYPE 구조 (`src/xasl/xasl_aggregate.hpp`)

```c
struct aggregate_list_node
{
  aggregate_list_node *next;
  FUNC_CODE function;       // PT_COUNT_STAR, PT_COUNT, PT_MIN, PT_MAX, ...
  BTID btid;                // 최적화에 사용할 B-tree ID
  aggregate_accumulator accumulator;  // 실행 시 누적 값 (curr_cnt 등)
  struct {
    bool agg_optimized;     // COUNT(*) 최적화 가능 여부
    bool min_max_optimized; // MIN/MAX 최적화 가능 여부
    ...
  } flag;
};
```

---

## Phase 4: XASL 직렬화 및 전달

**파일**: `src/query/xasl_to_stream.c` (클라이언트), `src/query/stream_to_xasl.c` (서버)

XASL 트리가 바이트 스트림으로 직렬화되어 클라이언트에서 서버로 전송된다. `AGGREGATE_TYPE`의 `btid`, `flag.agg_optimized` 등이 함께 직렬화/역직렬화된다.

서버에서 역직렬화된 XASL은 **XASL 캐시**에 저장되어 재사용될 수 있다.

---

## Phase 5: 쿼리 실행 단계 (서버)

**파일**: `src/query/query_executor.c`, `src/query/query_aggregate.cpp`, `src/storage/btree.c`

### 5.1 실행 진입점

```
qexec_execute_query()
  ├→ xasl_state.count_optim_statement_root = xasl;       // 루트 XASL 기록
  ├→ qexec_reset_count_star_agg_optimized_flags_from_xasl(xasl)  // [5.2]
  ├→ qexec_prepare_count_optim_classes_from_xasl(thread_p, xasl)  // [5.3]
  ├→ (RR 이상이면 스냅샷 선점)
  └→ qexec_execute_mainblock() → ... → qexec_intprt_fnc()  // [5.5]
```

### 5.2 agg_optimized 플래그 복원

`qexec_reset_count_star_agg_optimized_flags_from_xasl()`

XASL 캐시에서 재사용될 때, 이전 실행에서 `agg_optimized = false`로 비활성화된 플래그를 **원래 상태로 복원**한다.

```c
if (agg_ptr->function == PT_COUNT_STAR)
{
  agg_ptr->flag.agg_optimized = !BTID_IS_NULL(&agg_ptr->btid);
}
```

BTID가 유효하면(NULL이 아니면) `agg_optimized = true`로 복원. UNION_PROC의 left/right도 재귀 순회한다.

### 5.3 실행기 경로 COS 마킹

`qexec_prepare_count_optim_classes_from_xasl()`

XASL 트리를 DFS 순회하며 `BUILDVALUE_PROC` 노드마다 `qexec_mark_count_optim_class_for_buildvalue()`를 호출한다.

```c
// UNION_PROC의 left/right도 재귀
if (xasl->type == UNION_PROC || ...)
{
  qexec_prepare_count_optim_classes_from_xasl(thread_p, xasl->proc.union_.left);
  qexec_prepare_count_optim_classes_from_xasl(thread_p, xasl->proc.union_.right);
}
```

`qexec_mark_count_optim_class_for_buildvalue()`:

```c
class_cos = logtb_tran_find_class_cos(thread_p, &ACCESS_SPEC_CLS_OID(specp), true);
if (class_cos != NULL && class_cos->count_state != COS_LOADED)
{
  class_cos->count_state = COS_TO_LOAD;  // 스냅샷 시 통계 로드 예약
}
```

### 5.4 스냅샷 획득 시 통계 로드

**파일**: `src/transaction/mvcc_table.cpp`, `src/transaction/log_tran_table.c`

스냅샷을 획득하는 시점(`logtb_get_mvcc_snapshot()` → `mvcctable::build_mvcc_info()`)에서:

```c
// mvcc_table.cpp - build_mvcc_info() 내부
logtb_load_global_statistics_to_tran(thread_p);
```

이 함수는 COS 해시의 **모든** 항목을 순회하며:

1. `count_state == COS_TO_LOAD`인 클래스만 처리
2. 해당 클래스의 모든 유니크 인덱스에 대해 `logtb_get_global_unique_stats()`로 **B-tree 루트 헤더**에서 글로벌 통계(`num_oids`, `num_nulls`, `num_keys`)를 읽음
3. 트랜잭션의 `unique_stats_hash`에 저장
4. `count_state = COS_LOADED`로 변경

**핵심**: 이 로드가 **스냅샷 획득과 동일한 크리티컬 섹션**에서 수행되므로, 통계와 스냅샷의 **일관성이 보장**된다.

### 5.5 BUILDVALUE_PROC 실행 — 최적화 평가

`qexec_intprt_fnc()`에서 `BUILDVALUE_PROC` 처리:

```c
if (xasl->type == BUILDVALUE_PROC)
{
  error = qexec_evaluate_aggregates_optimize(thread_p, buildvalue->agg_list,
                                             xasl->spec_list, &is_scan_needed, xasl_state);
  if (!is_scan_needed)
  {
    return S_SUCCESS;  // ← 스캔 없이 바로 반환!
  }
  // 최적화 실패 시 → 일반 스캔 경로로 진행
}
```

### 5.6 집계 최적화 평가

`qexec_evaluate_aggregates_optimize()`:

```
각 agg_ptr에 대해:
  1. agg_optimized == false → is_scan_needed = true, 스킵
  2. COUNT_STAR이면:
     a. COS 항목 조회 (logtb_tran_find_class_cos)
     b. 스냅샷이 유효한 경우:
        - COS_LOADED → 통계 사용 가능
        - COS_LOADED 아님 (RC 환경) → 스냅샷 무효화 + COS 재마킹 + 재시도
     c. 스냅샷이 아직 없는 경우:
        - COS_TO_LOAD로 설정 → logtb_get_mvcc_snapshot() 호출

모든 agg_ptr가 최적화 가능하면:
  → qdata_evaluate_aggregate_optimize() 호출
```

RC(Read Committed) 환경에서의 **재시도 로직**:

```c
if (!retried_after_invalidate && isolation < TRAN_REPEATABLE_READ
    && xasl_state->count_optim_statement_root != NULL)
{
  logtb_invalidate_snapshot_data(thread_p);        // 스냅샷 무효화
  qexec_prepare_count_optim_classes_from_xasl(...); // 전체 XASL 재마킹
  retried_after_invalidate = true;
  goto count_optim_snapshot_retry;                  // 재시도
}
```

### 5.7 인덱스 통계에서 COUNT 결과 읽기

`qdata_evaluate_aggregate_optimize()` (`src/query/query_aggregate.cpp`):

```c
btree_get_unique_statistics_for_count(thread_p, &agg_p->btid,
                                      &oid_count, &null_count, &key_count);

switch (agg_p->function)
{
  case PT_COUNT_STAR:
    agg_p->accumulator.curr_cnt = oid_count;  // ← 최종 결과
    break;
  case PT_COUNT:
    // Q_ALL: oid_count - null_count
    // DISTINCT: key_count
    break;
}
```

### 5.8 B-tree 유니크 통계 읽기

`btree_get_unique_statistics_for_count()` (`src/storage/btree.c`):

```c
unique_stats = logtb_tran_find_btid_stats(thread_p, btid, true);
*oid_cnt  = unique_stats->tran_stats.num_oids  + unique_stats->global_stats.num_oids;
*key_cnt  = unique_stats->tran_stats.num_keys  + unique_stats->global_stats.num_keys;
*null_cnt = unique_stats->tran_stats.num_nulls + unique_stats->global_stats.num_nulls;
```

- `global_stats`: 스냅샷 시점에 B-tree 루트 헤더에서 로드한 값
- `tran_stats`: 현재 트랜잭션에서 INSERT/DELETE로 인한 델타
- 두 값을 합산하여 **현재 트랜잭션 관점의 정확한 COUNT** 산출

B-tree 루트 헤더 (`src/storage/btree_load.h`):

```c
struct btree_root_header
{
  BTREE_NODE_HEADER node;
  INT64 num_oids;   // B-tree에 저장된 OID 총 수 → COUNT(*)에 사용
  INT64 num_nulls;  // NULL 수 (저장되지 않음)
  INT64 num_keys;   // 유니크 키 수
  ...
};
```

---

## UNION ALL에서의 처리

### 문제 (CBRD-26571)

UNION ALL 구문에서 두 번째 브랜치의 COUNT(*)에 최적화가 적용되지 않았던 문제.

```sql
SELECT 't1', COUNT(*) FROM t1
UNION ALL
SELECT 't2', COUNT(*) FROM t2;
```

기존에는 첫 번째 SELECT만 COS_LOADED 상태로 진입하고, 두 번째는 스냅샷 이후에 COS가 준비되지 않아 일반 스캔으로 폴백되었다.

### 해결

1. **파서 단계**: `pt_select_eligible_count_optim_lock_hint()`가 `parser_walk_tree()`를 통해 **UNION 하위의 각 SELECT 브랜치를 개별 방문**하므로, t1과 t2 모두 `LC_PREF_FLAG_COUNT_OPTIM`이 부착된다.

2. **실행기 단계**: `qexec_prepare_count_optim_classes_from_xasl()`이 **UNION_PROC의 left/right를 재귀 순회**하여 양쪽 브랜치의 BUILDVALUE_PROC 모두에 COS_TO_LOAD를 마킹한다.

3. **UNION 실행**: `qexec_execute_union()`은 left/right에 대해 각각 `qexec_execute_mainblock()`을 호출하며, `count_optim_statement_root`가 보존되므로 RC 재시도 경로도 정상 동작한다.

4. **중첩 mainblock 보호**: `qexec_execute_mainblock_nested()`는 `count_optim_statement_root`를 임시로 NULL로 설정하여, 서브쿼리 등 중첩 실행에서 상위 스냅샷과의 충돌을 방지한다. UNION 브랜치는 이 nested 경로를 타지 않고 직접 `qexec_execute_mainblock()`을 호출한다.

---

## 격리 수준별 동작

| 격리 수준 | 동작 |
|-----------|------|
| READ COMMITTED (RC) | 스냅샷이 문장 단위로 유효. COS_LOADED가 아니면 스냅샷 무효화 후 재시도 가능 |
| REPEATABLE READ (RR) 이상 | 트랜잭션 시작 시 스냅샷 선점. `logtb_invalidate_snapshot_data()`가 무시됨. 재시도 불가 |

---

## 관련 파일 요약

| 파일 | 역할 |
|------|------|
| `src/parser/compile.c` | 최적화 대상 판정 + lock hint 플래그 부착 |
| `src/parser/xasl_generation.c` | AGGREGATE_TYPE에 BTID 부착 + agg_optimized 설정 |
| `src/xasl/xasl_aggregate.hpp` | AGGREGATE_TYPE 구조 정의 |
| `src/transaction/locator.h` | `LC_PREF_FLAG_COUNT_OPTIM` 정의 |
| `src/transaction/locator_sr.c` | 서버 lock hint 처리 → COS 마킹 연동 |
| `src/transaction/log_impl.h` | COS 상태/구조 정의 |
| `src/transaction/log_tran_table.c` | COS 해시 관리, 통계 로드, 스냅샷 무효화 |
| `src/transaction/mvcc_table.cpp` | 스냅샷 획득 시 `logtb_load_global_statistics_to_tran()` 호출 |
| `src/query/query_executor.c` | 실행기: 최적화 평가, COS 마킹, UNION 재귀 |
| `src/query/query_aggregate.cpp` | `qdata_evaluate_aggregate_optimize()` — 통계로 집계 값 설정 |
| `src/storage/btree.c` | `btree_get_unique_statistics_for_count()` — 메모리 통계 합산 |
| `src/storage/btree_load.h` | B-tree 루트 헤더 구조 (num_oids, num_keys) |
| `src/object/schema_manager.c` | `sm_find_index()` — 유니크 인덱스 탐색 |

---

## 핵심 함수 호출 시퀀스 다이어그램

```
CLIENT (파서/컴파일)                              SERVER
========================                          ========================

pt_class_pre_fetch()
  └ parser_walk_tree(pt_find_lck_classes)
      └ pt_select_eligible_count_optim_lock_hint()
          → LC_PREF_FLAG_COUNT_OPTIM
  └ locator_lockhint_classes()
      ──── RPC ────────────────────────────→ xlocator_find_lockhint_class_oids()
                                                └ logtb_tran_prepare_count_optim_classes()
                                                    └ class_cos->count_state = COS_TO_LOAD

pt_to_buildvalue_proc()
  └ pt_to_aggregate()
      └ pt_to_aggregate_node()
          └ sm_find_index(unique=true)
              → agg->btid = <PK BTID>
              → agg->flag.agg_optimized = true

xasl_to_stream() ──── 전송 ────────────→ stream_to_xasl()

                                          qexec_execute_query()
                                            └ qexec_reset_count_star_agg_optimized_flags_from_xasl()
                                            └ qexec_prepare_count_optim_classes_from_xasl()
                                                └ COS_TO_LOAD 마킹 (각 BUILDVALUE_PROC)
                                            └ logtb_get_mvcc_snapshot()
                                                └ build_mvcc_info()
                                                    └ logtb_load_global_statistics_to_tran()
                                                        └ btree 루트 헤더 → global_stats 로드
                                                        └ COS_TO_LOAD → COS_LOADED
                                            └ qexec_intprt_fnc() [BUILDVALUE_PROC]
                                                └ qexec_evaluate_aggregates_optimize()
                                                    └ COS_LOADED 확인
                                                    └ qdata_evaluate_aggregate_optimize()
                                                        └ btree_get_unique_statistics_for_count()
                                                            → global + tran 델타 합산
                                                            → curr_cnt = num_oids
                                                └ is_scan_needed == false
                                                └ return S_SUCCESS  ← 스캔 없이 완료!
```
