# Partition UPDATE 후 SELECT 시 cub_server SIGSEGV — 원인 및 수정

- 브랜치: `oos-refactor-replace-oids`
- 발견 경위: `sql/_01_object/_09_partition/_004_manipulation/cases/1042.sql` 실행 중 1042번에서 NOK → 후속 1043번 JDBC 연결 실패
- 분류: 서버 크래시 (메모리 손상 → `mspace_free`에서 SIGSEGV)
- 영향 범위: OOS 리팩토링 브랜치 한정. `master`/`develop`에는 영향 없음

---

## 1. 최소 재현 (Minimal Repro)

```sql
DROP TABLE IF EXISTS list_test;

CREATE TABLE list_test (
    id int NOT NULL,
    test_int int,
    PRIMARY KEY (id, test_int)
)
PARTITION BY LIST (test_int) (
    PARTITION p0 VALUES IN (1, 3, 5, 7, 9)
);

INSERT INTO list_test VALUES (1, 1);
UPDATE list_test SET test_int = 7 WHERE test_int = 1;
SELECT * FROM list_test;   -- ← 여기서 cub_server SIGSEGV
```

재현 조건:
- 행 1개로 충분 (반드시 여러 행 필요 없음)
- 파티션 키를 바꾸는 UPDATE가 반드시 필요
- 후속 SELECT가 **풀 힙 스캔**이어야 함 (`SELECT *`, WHERE 없음)
- WHERE로 PK 인덱스 스캔을 유도하면 (`WHERE id < 100` 등) 크래시 안 함

---

## 2. 크래시 시그니처

```
mspace_free                                  malloc_2_8_3.c:4992 (unlink_chunk 내부)
heap_scancache_block_deallocate              heap_file.c:27169
single_block_allocator::~single_block_allocator
heap_scancache::end_area                     heap_file.c:27194
heap_scancache_quick_end                     heap_file.c:7345
heap_scancache_end_internal / _end           heap_file.c:7369 / 7387
scan_end_scan                                scan_manager.c:4790
qexec_init_next_partition                    query_executor.c:8809
qexec_next_scan_block                        query_executor.c:8003
qexec_next_scan_block_iterations             query_executor.c:8084
qexec_intprt_fnc                             query_executor.c:9274
qexec_execute_mainblock_internal             query_executor.c:15917
qexec_execute_query                          query_executor.c:16527
```

해석: `mspace_free` → `unlink_chunk`에서 죽는 것은 dlmalloc의 freelist bin 포인터가 손상되었다는 신호.
즉 **어딘가에서 인접 청크 경계를 넘는 쓰기가 일어났고**, 손상은 그 청크를 해제하려는 순간에야 드러난 것.

해제 대상 블록은 `heap_scancache`의 recdes 버퍼(`b.ptr = 0x3dce5e8`, `dim = 32688` ≈ `2 * DB_PAGESIZE`).
파티션 이터레이션 경계(`qexec_init_next_partition`)에서 스캔캐시를 정리할 때 노출됨.

---

## 3. 근본 원인

OOS 리팩토링 브랜치는 다음 두 가지를 도입함:

1. **OOS 확장 라이터** (`heap_record_replace_oos_oids`, `heap_file.c:7973–8234`)
   - 이전에는 핫픽스로 `return S_SUCCESS`만 하던 자리에, 실제 VOT 기반 expansion 구현이 들어옴
   - SELECT 시 레코드에 `OR_MVCC_FLAG_HAS_OOS`가 켜져 있으면 호출됨
   - VOT를 걸어가며 inline OOS slot을 OOS 파일에서 읽은 실제 값으로 치환

2. **UPDATE 시 HAS_OOS 플래그 재계산 코드** (`heap_update_adjust_recdes_header`, `heap_file.c:21609 원본)

```c
// 문제의 코드
bool has_oos = heap_recdes_check_has_oos (update_context->recdes_p);

if (has_oos)
  repid_and_flag_bits |= (OR_MVCC_FLAG_HAS_OOS << OR_MVCC_FLAG_SHIFT_BITS);
else
  repid_and_flag_bits &= ~(OR_MVCC_FLAG_HAS_OOS << OR_MVCC_FLAG_SHIFT_BITS);
```

`heap_recdes_check_has_oos`(`heap_file.c:27852–27901`)는 레코드의 VOT(Variable Offset Table)를 걸어가며 OOS 비트(`OR_VAR_BIT_OOS = 0x1`)가 있는 항목이 있는지 검사함.

문제는 — **레코드에 변수 컬럼이 없으면(n_variable == 0) VOT 자체가 존재하지 않는다는 것**.
그런데 함수는 recdes만으로 호출되기 때문에 n_variable 을 알 수 없고, 헤더 바로 뒤의 바이트들(고정 컬럼 데이터 + bound-bit bitmap)을 VOT 항목으로 해석해 버림.

재현 테이블의 컬럼 구성:
```
id        int  not null   (4 byte, fixed)
test_int  int             (4 byte, fixed)
```
→ `n_variable == 0`, 즉 VOT가 없음.

UPDATE 가 만든 새 레코드 헤더 직후 9 바이트(`id` 4 + `test_int` 4 + bound-bit 1)에서:
- bit 0 (`OR_VAR_BIT_OOS`)가 켜진 바이트가 어디든 하나라도 있으면
- `heap_recdes_check_has_oos`가 `true` 반환
- 그 결과 `OR_MVCC_FLAG_HAS_OOS`가 잘못 켜짐 (false positive)

테이블에는 OOS 가 전혀 없는데도 플래그가 켜진 상태로 디스크에 기록됨.

이후 `SELECT *` (풀 힙 스캔)이 실행되면:
- `heap_get_record_data_when_all_ready` → `heap_record_replace_oos_oids` 호출
- `heap_recdes_contains_oos` (단순 플래그 비트 확인) → `true` 반환
- expansion 라이터가 **존재하지 않는 VOT**를 걸어가며 size 계산
- `new_length` 가 엉뚱하게 잡힘 → `heap_scan_cache_allocate_recdes_data` 가 잘못된 크기로 재할당
- `std::memcpy` 가 스캔캐시 블록 경계를 넘어 인접 mspace 청크의 메타데이터를 덮어씀
- 손상은 이후 파티션 경계에서 스캔캐시를 해제할 때 `mspace_free` 가 감지 → SIGSEGV

### 왜 인덱스 스캔(WHERE 절)에서는 멀쩡한가

`scan_manager.c:6311`의 인덱스 룩업 경로는 `heap_get_visible_version_skip_oos_expand` 를 사용함 (`context.expand_oos = false`).
→ expansion 라이터가 호출되지 않으므로 손상된 플래그가 있더라도 메모리를 건드리지 않음.

풀 힙 스캔만 `heap_get_record_data_when_all_ready` 를 거치면서 expansion 라이터를 무조건 호출 → 폭발.

### 왜 master 에는 영향 없는가

master 의 `heap_record_replace_oos_oids_with_values_if_exists`(과거 이름)는 핫픽스로 함수 첫줄에서 `return S_SUCCESS` 하고 있었음.
즉 expansion 자체가 비활성. 같은 false-positive 가 발생해도 메모리 손상까지는 안 갔음.

이번 브랜치(`oos-refactor-replace-oids`)가 expansion 을 실제로 켜면서 잠재 버그가 드러난 것.

---

## 4. 수정 (Fix)

### 핵심 원리

UPDATE 진입 시점의 recdes 는 이미 상류(`heap_attrinfo_transform_header_to_disk`, `heap_file.c:12780`)에서 `has_oos` 인자에 따라 헤더 비트를 올바르게 스탬프해 둔 상태임.
즉 **재계산이 애초에 불필요했고, INSERT 경로(`heap_insert_adjust_recdes_header`, `heap_file.c:21472`)는 이미 그렇게 하고 있었음**:

```c
// INSERT 경로 — 기존 플래그를 그대로 신뢰
bool has_oos = (mvcc_flags & OR_MVCC_FLAG_HAS_OOS) != 0;
```

UPDATE 도 동일하게 바꿈.

### 패치 (heap_file.c:21609)

```c
/* Trust the HAS_OOS bit already stamped into the recdes by the upstream builder
 * (heap_attrinfo_transform_header_to_disk, heap_file.c:12780). Recomputing it here by walking
 * the VOT is unsafe for classes with no variable attributes — without a VOT in the on-disk
 * record, the walk reads fixed-attribute / bound-bitmap bytes as VOT entries and false-
 * positives on bit-0 of any byte, then sets HAS_OOS on a record that has no OOS, which trips
 * the OOS-expansion writer on the next SELECT and corrupts the scancache buffer. The INSERT
 * variant (heap_insert_adjust_recdes_header above) likewise trusts the flag from the builder. */
bool has_oos = (mvcc_flags & OR_MVCC_FLAG_HAS_OOS) != 0;

#if !defined (NDEBUG)
/* Debug-only sanity check: verify the upstream builder stamped HAS_OOS consistently with the
 * actual on-disk VOT contents. Skipped for classes with n_variable == 0 (no VOT to walk).
 * If this assert ever fires, some recdes-producing path is forgetting to set/clear HAS_OOS. */
{
  int classrepr_cacheindex = -1;
  OR_CLASSREP *classrepr =
    heap_classrepr_get (thread_p, &update_context->class_oid, NULL, NULL_REPRID, &classrepr_cacheindex);
  if (classrepr != NULL)
    {
      if (classrepr->n_variable > 0)
        {
          bool walked_has_oos = heap_recdes_check_has_oos (update_context->recdes_p);
          assert (walked_has_oos == has_oos);
        }
      heap_classrepr_free_and_init (classrepr, &classrepr_cacheindex);
    }
}
#endif
```

### 수정의 의미

- **릴리즈 빌드**: VOT 산책 자체를 안 함 → false-positive 자체가 사라짐. `heap_classrepr_get` 같은 부가 작업도 없음 (오버헤드 0).
- **디버그 빌드**: 상류가 플래그를 빠뜨리는 경로가 있으면 assert로 즉시 발각.
  - 만약 SA 모드 등에서 빌더가 플래그를 놓치는 경로가 있다면 디버그 빌드 테스트로 잡힘.
  - n_variable == 0 인 클래스는 sanity check 자체를 건너뜀 (VOT 없으므로 walk 불가).

### 영향 범위

- UPDATE 시 추가 비용: 릴리즈 0, 디버그에서만 cached classrepr_get 1회 + VOT walk 1회.
- 정확성: INSERT 경로가 이미 같은 방식으로 동작하고 있었으므로 위험 동등.
- 호환성: 디스크 포맷 변경 없음, 로그 포맷 변경 없음.

---

## 5. 검증

| 케이스 | 수정 전 | 수정 후 |
|---|---|---|
| Minimal repro (1행, 풀 스캔) | SIGSEGV | 정상 (1행 반환) |
| 1042.sql 전체 (10행, ORDER BY 풀 스캔) | SIGSEGV | 정상 (10행, expected answer 와 일치) |
| `SELECT * WHERE id < 100` (인덱스 스캔) | 정상 | 정상 (회귀 없음) |

CI 회귀 검증:
- `/run sql medium` 으로 SQL 테스트 + medium-grade 시나리오 회귀 확인 예정.

---

## 6. 후속 과제 (out of scope for this fix)

1. **`heap_recdes_check_has_oos` 자체의 견고화**
   - 현재 함수는 단독으로는 안전하게 사용할 수 없음 (n_variable 필요).
   - `static` 헬퍼로 유지하되, 호출 직전에 `n_variable > 0` 보장 의무를 호출자에게 명시(주석/타입)로 옮기는 것이 깔끔.
   - 또는 `n_variable` 파라미터를 강제(필수 인자) 받도록 시그니처 변경.

2. **`heap_recdes_get_oos_oids`(line 27752–27850)도 동일한 패턴**
   - 단, 현재 호출자는 모두 `heap_recdes_contains_oos == true` (즉 플래그 비트가 켜진 경우)일 때만 호출.
   - 이번 수정으로 플래그가 거짓으로 켜지는 일이 사라지므로 실질 영향은 없음.
   - 그래도 방어적으로 같은 보호 추가를 검토.

3. **`heap_attrinfo_transform_to_disk` 류 경로 검증**
   - SA 모드 / bulk insert / 시스템 클래스 갱신 등 빌더를 우회하는 경로가 있다면 디버그 assert 로 잡힘.
   - 잡히면 그 경로에서 빌더를 거치도록 보정.

---

## 7. 변경 파일

- `src/storage/heap_file.c` (1 hunk, +20 / -2)

다른 파일 변경 없음.
