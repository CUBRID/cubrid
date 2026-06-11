# Non-Unique Index COUNT(*) Optimization

## 1. 개요

### 1.1 배경

CUBRID는 `SELECT COUNT(*) FROM t` 와 같이 조건 없는 COUNT(*) 쿼리에 대해, 테이블을 full scan하는 대신 B-tree 헤더에 저장된 통계(`num_oids`)를 직접 읽는 fast-path 최적화를 지원한다. 기존에는 이 최적화가 **unique 인덱스**가 있는 테이블에만 적용되었다.

### 1.2 목표

Non-unique 인덱스가 있는 테이블에서도 동일한 COUNT(*) 최적화를 지원한다.

- `num_oids` = 테이블의 전체 행 수 (NULL 포함)
- `num_nulls` = NULL 키를 가진 행 수
- `num_keys` = 고유 키 수 (non-unique에서는 distinct key 수)

### 1.3 핵심 설계 원칙

Non-unique 인덱스는 행당 하나의 OID를 저장하므로, `num_oids + num_nulls = total rows` 관계를 유지하면 COUNT(*) 최적화에 사용할 수 있다. NULL 키는 B-tree에 저장되지 않으므로 별도로 `num_nulls`를 추적해야 한다.

---

## 2. 변경 파일 목록

| 파일 | 역할 |
|------|------|
| `src/storage/btree.c` | B-tree 핵심 로직: 초기화, 삽입/삭제 통계 추적, undo/postpone 로그 |
| `src/storage/btree_load.c` | 인덱스 bulk load: NULL 카운트 추적, undo 로그 |
| `src/storage/btree_unique.cpp` | `btree_unique_stats::is_zero()` 버그 수정 |
| `src/object/schema_manager.c` | COUNT(*) 최적화에 사용할 인덱스 선택 로직 |
| `src/parser/xasl_generation.c` | XASL 생성 시 non-unique 인덱스 사용 |
| `src/transaction/log_tran_table.c` | 트랜잭션 스냅샷 시점에 non-unique 통계 로드 |
| `src/transaction/log_manager.c` | 트랜잭션 abort 시 non-unique 처리 crash 수정 |

---

## 3. 상세 변경 내용

### 3.1 B-tree 헤더 초기화 변경 (`src/storage/btree.c`: `xbtree_add_index`)

#### 변경 전
Non-unique 인덱스 생성 시 헤더 통계를 `-1`로 초기화하여 "통계 미지원(legacy)" 상태로 표시했다.

```c
// 변경 전
root_header->num_oids = -1;
root_header->num_nulls = -1;
root_header->num_keys = -1;
```

#### 변경 후
Non-unique 인덱스도 `0`으로 초기화하여 통계 추적을 활성화한다.

```c
// 변경 후
root_header->num_oids = 0;
root_header->num_nulls = 0;
root_header->num_keys = 0;
```

#### Legacy 인덱스 구분
기존에 생성된 인덱스(변경 전 생성)는 헤더에 `-1`이 저장되어 있다. 코드 전반에서 `num_oids >= 0` 조건으로 신규 포맷(통계 추적 지원)과 legacy 포맷을 구분한다.

---

### 3.2 통계 조회 함수 수정 (`src/storage/btree.c`)

#### `btree_get_unique_statistics`

```c
// 변경 전: unique 인덱스만 허용 (assert)
assert ((root_header->unique_pk & ...) != 0);
...
pgbuf_unfix_and_init (thread_p, root);

// 변경 후: non-unique 허용, legacy(-1)이면 ER_FAILED 반환
if (!BTREE_IS_UNIQUE (r_unique_pk) && (*oid_cnt < 0 || ...))
  {
    return ER_FAILED;  /* Legacy non-unique: skip */
  }
```

#### `btree_get_unique_statistics_for_count`

두 차례에 걸쳐 수정되었다.

**1차 수정**: 합산 결과가 음수이면 유효하지 않은 통계로 처리.

```c
if (*oid_cnt < 0 || *key_cnt < 0 || *null_cnt < 0)
  {
    return ER_FAILED;
  }
```

**2차 수정** (코드 중복 제거 및 잘못된 COUNT=0 방지):

`logtb_tran_find_btid_stats`의 `create` 파라미터를 `true` → `false`로 변경하고, `deleted` 플래그 체크를 추가했다.

```c
// 변경 전: create=true → entry가 없으면 {0,0,0}으로 새로 생성 → COUNT=0 반환 위험
unique_stats = logtb_tran_find_btid_stats (thread_p, btid, true);
if (unique_stats == NULL)
  return ER_FAILED;

// 변경 후: create=false → entry 없으면 ER_FAILED로 heap scan fallback
//          deleted 체크 → legacy non-unique도 안전하게 fallback
unique_stats = logtb_tran_find_btid_stats (thread_p, btid, false);
if (unique_stats == NULL || unique_stats->deleted)
  return ER_FAILED;
```

**변경 배경**: `logtb_create_unique_stats_from_repr`은 COUNT(*) 최적화 실행 전 반드시 호출되어 유효한 entry를 생성한다. entry가 없다는 것은 통계가 로드되지 않은 상태이므로, {0,0,0} entry를 새로 만들어 잘못된 COUNT=0을 반환하는 것보다 heap scan fallback이 정확하다.

#### `btree_reflect_global_unique_statistics`

```c
// 변경 전: unique만 허용 (assert)
assert_release (BTREE_IS_UNIQUE (root_header->unique_pk));

// 변경 후: num_nulls != -1이면 unique/non-unique 모두 업데이트
// (assert 제거, num_nulls >= 0 조건만으로 충분)
```

---

### 3.3 삽입/삭제 시 non-unique 통계 추적 (`src/storage/btree.c`)

#### Legacy 인덱스 가드: `BTREE_INSERT_HELPER.nonunique_oid_stats_valid`

`btree_fix_root_for_insert`에서 root 헤더를 최초 읽을 때 플래그를 설정한다.

```c
// BTREE_INSERT_HELPER 구조체에 추가
bool nonunique_oid_stats_valid;  // 기본값 false

// btree_fix_root_for_insert 내에서 설정
if (!BTREE_IS_UNIQUE (btid_int->unique_pk))
  {
    insert_helper->nonunique_oid_stats_valid = (root_header->num_oids >= 0);
  }
```

이 플래그는 legacy non-unique 인덱스(`num_oids == -1`)에 대해 통계 업데이트를 완전히 차단한다.

#### 논리적 삭제 / NULL 삽입 추적 (`btree_fix_root_for_insert`)

Unique 인덱스와 동일한 위치에서 non-unique도 처리한다.

```c
else if (insert_helper->nonunique_oid_stats_valid
         && !btree_is_online_index_loading (insert_helper->purpose)
         && (purpose == BTREE_OP_INSERT_MVCC_DELID
             || purpose == BTREE_OP_INSERT_MARK_DELETED
             || (btree_is_insert_object_purpose && insert_helper->is_null)))
  {
    btree_unique_stats incr;

    if (purpose == MVCC_DELID || purpose == MARK_DELETED)
      {
        if (insert_helper->is_null)
          incr.delete_null_and_row ();   // NULL 행 삭제
        else
          incr.delete_row ();            // 일반 행 삭제
      }
    else  /* NULL 키 삽입 */
      {
        incr.insert_null_and_row ();     // NULL 행은 B-tree에 저장되지 않으므로 여기서 추적
      }

    // tran_stats 업데이트
    logtb_tran_update_unique_stats (thread_p, *btid, incr, true);
  }
```

#### 일반 행 삽입 추적 (`btree_key_insert_new_key`, `btree_key_append_object_non_unique`)

```c
// 새 키와 함께 삽입 (btree_key_insert_new_key)
if (!BTREE_IS_UNIQUE (...) && !btree_is_online_index_loading (...)
    && insert_helper->nonunique_oid_stats_valid)
  {
    logtb_tran_update_unique_stats (thread_p, sys_btid, 1LL, 1LL, 0LL, true);
    // num_keys+1, num_oids+1
  }

// 기존 키에 OID 추가 (btree_key_append_object_non_unique)
if (!BTREE_IS_UNIQUE (...) && !btree_is_online_index_loading (...)
    && insert_helper->nonunique_oid_stats_valid)
  {
    logtb_tran_update_unique_stats (thread_p, sys_btid, 0LL, 1LL, 0LL, true);
    // num_oids+1 (키 추가 없음)
  }
```

#### 물리적 삭제(vacuum) 시 이중 카운팅 방지

Vacuum의 `btree_key_delete_remove_object`에서 non-unique 통계 업데이트 코드를 **제거**했다. 논리적 삭제(MVCC_DELID) 시점에 이미 통계가 감소하므로, 이후의 물리적 삭제에서 다시 감소시키면 이중 카운팅이 된다.

---

### 3.4 `btree_unique_stats::is_zero()` 버그 수정 (`src/storage/btree_unique.cpp`)

#### 변경 전
```c
bool btree_unique_stats::is_zero () const
{
  return m_keys == 0 && m_nulls == 0;
  // 버그: m_rows가 변해도 is_zero()가 true를 반환
}
```

Non-unique 인덱스에서 `delete_row()`는 `m_rows`만 감소시키고 `m_keys`, `m_nulls`는 변경하지 않는다. 기존 코드에서는 이 경우 `is_zero() == true`가 되어 통계 업데이트가 스킵되었다.

#### 변경 후
```c
bool btree_unique_stats::is_zero () const
{
  /* Non-unique may have row-only deltas where m_keys/m_nulls stay zero
   * while m_rows changes (delete_row). Include m_rows in the check. */
  return m_keys == 0 && m_nulls == 0 && m_rows == 0;
}
```

---

### 3.5 Bulk Load 시 NULL 카운트 추적 (`src/storage/btree_load.c`)

#### 변경 전
```c
if (DB_IS_NULL (key) || btree_multicol_key_is_null (...))
  {
    if (BTREE_IS_UNIQUE (m_unique_pk))  // unique만 카운트
      {
        ++m_insert_list.m_ignored_nulls_cnt;
      }
    return BATCH_CONTINUE;
  }
```

#### 변경 후
```c
if (DB_IS_NULL (key) || btree_multicol_key_is_null (...))
  {
    /* num_oids = num_keys + num_nulls = total rows: unique/non-unique 모두 카운트 */
    ++m_insert_list.m_ignored_nulls_cnt;
    return BATCH_CONTINUE;
  }
```

NULL 키는 B-tree에 저장되지 않으므로, `m_ignored_nulls_cnt`로 별도 집계한 뒤 최종 통계에 반영한다.

---

### 3.6 Online Index Build 시 통계 업데이트 확장 (`src/storage/btree.c`)

Online 인덱스 빌드 관련 함수들(`btree_key_online_index_IB_insert`, `btree_key_online_index_tran_insert`, `btree_key_online_index_tran_delete`, `btree_key_online_index_tran_insert_DF`)에서 `if (BTREE_IS_UNIQUE(...))` 조건을 제거하여 non-unique 인덱스도 통계를 업데이트한다.

```c
// 변경 전
if (error_code == NO_ERROR && BTREE_IS_UNIQUE (btid_int->unique_pk))
  {
    logtb_tran_update_unique_stats (...);
  }

// 변경 후
if (error_code == NO_ERROR)
  {
    logtb_tran_update_unique_stats (...);
  }
```

---

### 3.7 COUNT(*) 최적화 인덱스 선택 (`src/object/schema_manager.c`)

#### 새 함수: `sm_find_index_for_count_star_optimization`

```c
BTID *
sm_find_index_for_count_star_optimization (MOP classop, BTID * btid)
{
  /* 1순위: unique 인덱스 */
  if (sm_find_index (classop, NULL, 0, true, false, btid) != NULL)
    return btid;

  /* 2순위: non-unique 인덱스 (partial/filter/function 인덱스 제외) */
  for (con = class_->constraints; con != NULL; con = con->next)
    {
      if (!SM_IS_CONSTRAINT_INDEX_FAMILY (con->type)) continue;
      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (con->type)) continue;
      if (con->filter_predicate != NULL || con->func_index_info != NULL) continue;
      // ...
      BTID_COPY (btid, &con->index_btid);
      return btid;
    }
  return NULL;
}
```

**NOT NULL 제한 불필요**: NULL 행을 `num_nulls`에 별도 추적하므로 `num_oids`는 항상 전체 행 수와 같다. 따라서 기존에 구현된 `sm_constraint_non_unique_ok_for_count_star()` (NOT NULL 컬럼 체크)를 제거했다.

---

### 3.8 XASL 생성 변경 (`src/parser/xasl_generation.c`)

```c
// 변경 전: unique 인덱스만 사용
btid = sm_find_index (classop, NULL, 0, need_unique_index, false, &aggregate_list->btid);

// 변경 후: unique + non-unique 모두 고려
btid = sm_find_index_for_count_star_optimization (classop, &aggregate_list->btid);
```

---

### 3.9 트랜잭션 스냅샷 시 통계 로드 (`src/transaction/log_tran_table.c`)

#### `logtb_create_unique_stats_from_repr`

두 차례에 걸쳐 수정되었다.

**1차 수정**: non-unique 인덱스 처리를 별도 `else` 분기로 추가.

```c
// 기존: unique 인덱스만 처리
if (btree_is_unique_type (classrepr->indexes[idx].type))
  {
    // global stats 로드
  }
// else: non-unique 무시

// 1차 수정: non-unique도 별도 분기로 처리
else
  {
    if (btree_get_unique_statistics (...) != NO_ERROR)  // legacy 체크 (1회)
      continue;

    unique_stats = logtb_tran_find_btid_stats (..., true);
    logtb_get_global_unique_stats (...);  // 내부에서 btree_get_unique_statistics 재호출 (2회)
  }
```

**2차 수정** (코드 중복 제거 및 잘못된 COUNT=0 방지):

unique/non-unique 분기를 단일 루프로 통합하고, `btree_get_unique_statistics` 이중 호출 문제를 해결했다.

```
문제:
  non-unique 1차 코드 실행 순서
    1. btree_get_unique_statistics()       ← 직접 호출 (btree root page read #1)
    2. logtb_tran_find_btid_stats()
    3. logtb_get_global_unique_stats()
         → logtb_get_global_unique_stats_entry()
             → btree_get_unique_statistics()  ← 내부에서 재호출 (btree root page read #2)
```

또한 1차 수정의 legacy skip(`continue`)은 `unique_stats_hash`에 entry를 만들지 않는다. 그러면 이후 `btree_get_unique_statistics_for_count`가 `create=true`로 {0,0,0} entry를 생성해 **COUNT(*) = 0** 을 잘못 반환하는 문제가 있었다.

```c
// 2차 수정: 단일 루프, btree_get_unique_statistics 중복 호출 제거
for (idx = classrepr->n_indexes - 1; idx >= 0; idx--)
  {
    unique_stats = logtb_tran_find_btid_stats (..., true);

    error_code = logtb_get_global_unique_stats (...);
    // logtb_get_global_unique_stats_entry 내부에서 btree_get_unique_statistics 1회만 호출
    if (error_code != NO_ERROR)
      {
        if (btree_is_unique_type (...))
          goto exit_on_error;  // unique: 항상 fatal

        /* legacy non-unique: entry를 deleted로 마킹 →
         * btree_get_unique_statistics_for_count에서 deleted 체크 → heap scan fallback */
        unique_stats->deleted = true;
        er_clear ();
        error_code = NO_ERROR;
      }
  }
```

| | 1차 수정 | 2차 수정 |
|-|---------|---------|
| 코드 구조 | unique/non-unique 분기 | 단일 루프 |
| `btree_get_unique_statistics` 호출 | legacy 체크용 1회 + 내부 1회 = **2회** | 내부 1회만 |
| legacy 처리 | entry 미생성 → COUNT=0 위험 | `deleted=true` → fallback |

---

### 3.10 Undo/Postpone 로그 확장 (`src/storage/btree.c`, `src/storage/btree_load.c`)

#### 문제 배경

`log_Gl.unique_stats_table`에는 인덱스의 글로벌 통계 엔트리가 저장된다. 이 엔트리는 COUNT(*) 최적화를 사용하는 트랜잭션이 `logtb_create_unique_stats_from_repr`을 호출할 때 **lazy하게 생성**된다.

인덱스가 생성 후 abort되거나 삭제될 때, 이 엔트리를 정리하지 않으면 dangling 포인터가 생긴다. `btree_reflect_global_unique_statistics`가 체크포인트 시 이미 삭제된 btree의 root page를 접근하려 하면 crash가 발생한다.

#### `xbtree_add_index` — abort 시 undo 로그

```c
// 변경 전: unique 인덱스만 undo 로그 기록
if (unique_pk)
  {
    log_append_undo_data2 (thread_p, RVBT_REMOVE_UNIQUE_STATS, ...);
  }

// 변경 후: non-unique도 (num_oids=0으로 초기화하므로 항상 통계 추적 대상)
log_append_undo_data2 (thread_p, RVBT_REMOVE_UNIQUE_STATS, ...);
```

#### `xbtree_delete_index` — commit 시 postpone 로그

```c
// 변경 전: unique 인덱스만 postpone 로그 기록
ovfid = root_header->ovfid;
unique_pk = root_header->unique_pk;
pgbuf_unfix_and_init (thread_p, P);
if (unique_pk) { log_append_postpone (...); }

// 변경 후: num_oids >= 0인 인덱스 (unique + 신규 non-unique) 모두 처리
ovfid = root_header->ovfid;
unique_pk = root_header->unique_pk;
bool tracks_oid_stats = (root_header->num_oids >= 0);  // 페이지 해제 전에 읽기
pgbuf_unfix_and_init (thread_p, P);
if (unique_pk || tracks_oid_stats) { log_append_postpone (...); }
```

#### `xbtree_load_index` — abort 시 undo 로그

```c
// 변경 전: unique 인덱스만 undo 로그 기록
if (unique_pk)
  {
    log_append_undo_data2 (thread_p, RVBT_REMOVE_UNIQUE_STATS, ...);
  }

// 변경 후: 모든 인덱스 (non-unique도 lazy 엔트리가 생성될 수 있음)
log_append_undo_data2 (thread_p, RVBT_REMOVE_UNIQUE_STATS, ...);
```

---

### 3.11 Abort 시 Crash 수정 (`src/transaction/log_manager.c`)

#### 문제

`logtb_tran_update_stats_online_index_rb`는 트랜잭션 abort 시 `unique_stats_hash`의 모든 항목을 순회한다. 기존에는 unique 인덱스 항목만 있었지만, 우리 변경으로 non-unique 항목도 추가되었다.

`btree_get_class_oid_of_unique_btid`는 unique 인덱스인 경우에만 `class_oid`를 채운다. Non-unique btid에 대해서는 `NO_ERROR`를 반환하지만 `class_oid`는 null로 남는다. 이후 `assert (!OID_ISNULL (&class_oid))`에서 crash.

#### 수정

```c
error_code = btree_get_class_oid_of_unique_btid (thread_p, &unique_stats->btid, &class_oid);
if (error_code != NO_ERROR)
  {
    assert (false);
    return error_code;
  }

// 수정: non-unique는 online index build와 무관하므로 skip
if (OID_ISNULL (&class_oid))
  {
    return NO_ERROR;
  }
```

---

## 4. 통계 추적 흐름 요약

### 4.1 행 삽입 시

```
INSERT INTO t VALUES (v)
  → btree_fix_root_for_insert (1st try)
      → nonunique_oid_stats_valid = (root_header->num_oids >= 0)
      → is_null이면: insert_null_and_row() → tran_stats 업데이트
  → btree_key_insert_new_key (새 키)
      → nonunique_oid_stats_valid이면: +1 key, +1 oid → tran_stats
  → btree_key_append_object_non_unique (기존 키에 추가)
      → nonunique_oid_stats_valid이면: +1 oid → tran_stats
```

### 4.2 행 삭제 시 (MVCC)

```
DELETE FROM t WHERE ...
  → btree_fix_root_for_insert (MVCC_DELID)
      → nonunique_oid_stats_valid이면:
          is_null이면: delete_null_and_row() → tran_stats
          else:        delete_row()          → tran_stats
  → Vacuum (물리적 삭제): 통계 업데이트 없음 (이중 카운팅 방지)
```

### 4.3 트랜잭션 커밋 시

```
COMMIT
  → logtb_tran_update_all_global_unique_stats
      → 각 btid의 tran_stats delta를 log_Gl.unique_stats_table에 반영
      → RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT 로그 기록
  → 체크포인트 또는 후처리 시 btree_reflect_global_unique_statistics
      → global stats → btree root header 업데이트
```

### 4.4 COUNT(*) 쿼리 실행 시

```
SELECT COUNT(*) FROM t
  → pt_to_aggregate_node
      → sm_find_index_for_count_star_optimization
          → unique 인덱스 있으면 우선 선택
          → 없으면 non-unique 인덱스 (partial/filter/func 제외)
  → logtb_create_unique_stats_from_repr
      → 모든 인덱스를 단일 루프로 처리 (unique/non-unique 분기 없음)
      → logtb_get_global_unique_stats 실패 시:
          unique     → 에러 전파 (fatal)
          non-unique → entry.deleted = true, er_clear() (legacy graceful skip)
  → btree_get_unique_statistics_for_count
      → logtb_tran_find_btid_stats (create=false)
      → entry == NULL 또는 deleted == true → ER_FAILED → heap scan fallback
      → 유효한 entry: tran_stats + global_stats = 최종 num_oids 반환
```

---

## 5. Legacy 인덱스 호환성

변경 이전에 생성된 non-unique 인덱스는 헤더에 `num_oids = -1`이 저장되어 있다.

| 상황 | 처리 |
|------|------|
| `btree_get_unique_statistics` | `num_oids < 0` → `ER_FAILED` 반환, 호출자는 skip |
| `logtb_create_unique_stats_from_repr` | `logtb_get_global_unique_stats` 실패 → `deleted=true` 마킹 후 continue |
| `btree_get_unique_statistics_for_count` | `deleted=true` entry → `ER_FAILED` → heap scan fallback |
| `nonunique_oid_stats_valid` | `root_header->num_oids >= 0` → false → DML 시 통계 업데이트 스킵 |
| COUNT(*) 최적화 | Legacy 인덱스가 선택되더라도 `deleted` 체크로 fallback, 잘못된 COUNT=0 방지 |
| `btree_reflect_global_unique_statistics` | `num_nulls != -1` 조건으로 업데이트 여부 판단 (기존 로직 유지) |

---

## 6. 버그 수정 이력

| # | 증상 | 원인 | 수정 위치 |
|---|------|------|----------|
| 1 | `CREATE TABLE` 시 assertion fail (`mvcc_table.cpp:479`) | Legacy non-unique 인덱스가 `unique_stats_hash`에 추가되어 커밋 시 global stats 업데이트 실패 | `nonunique_oid_stats_valid` 플래그 추가 |
| 2 | 빌드 에러 (`logtb_tran_update_unique_stats` overload 불일치) | `*btid_int->sys_btid`로 역참조(BTID&) → `const BTID*` 불일치 | `btid_int->sys_btid`로 수정 |
| 3 | 논리적 삭제 시 `delete_row()`가 tran_stats에 반영 안 됨 | `is_zero()`가 `m_rows`를 체크하지 않아 delta=0으로 오판 | `is_zero()`에 `m_rows == 0` 조건 추가 |
| 4 | 트랜잭션 abort 시 crash (`log_manager.c:10618`) | `logtb_tran_update_stats_online_index_rb`가 non-unique btid에 대해 null `class_oid`로 assert | null `class_oid` 조기 반환 처리 |
| 5 | legacy non-unique 인덱스 선택 시 COUNT(*) = 0 반환 | `btree_get_unique_statistics_for_count`의 `create=true`가 {0,0,0} entry 생성 | `create=false` + `deleted` 체크 추가 |
| 6 | `logtb_create_unique_stats_from_repr`에서 btree root page 이중 읽기 | legacy pre-check용 `btree_get_unique_statistics` 호출 + 내부 호출이 중복 | unique/non-unique 분기 통합, pre-check 제거 |
