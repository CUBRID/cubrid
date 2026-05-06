# 코드 리뷰: Parallel Hash Join Probe 도입

## 컨텍스트
- **Jira**: [CBRD-26719](https://jira.cubrid.org/browse/CBRD-26719)
- **GitHub PR**: [#7068 — Improve hash join probe phase with parallel execution support](https://github.com/CUBRID/cubrid/pull/7068)
- **베이스 브랜치**: `develop`
- **헤드 브랜치**: `CBRD-26719`
- **상태**: OPEN

## 개요
PR #7068은 hash join의 probe 단계를 병렬 워커로 분산 처리하도록 확장. 기존 partition 기반 parallel hash join이 적용되지 않는 단일 컨텍스트에서도 sector-based page scan으로 워커들이 probe input을 분담.

`upstream/develop` 대비 src/ 변경 요약:
- **추가**: 482줄 / **삭제**: 569줄 / **순 감소**: 87줄
- 신규 모듈: `src/query/parallel/px_hash_join/` (probe_task, spawn_manager, task_manager)
- 기존 정리: `worker_pool_manager`, `entry_manager` 제거 → 공용 `parallel_query::worker_manager` 재사용
- 통계: per-worker min/max range 수집 (probe phase) + dump 출력 확장

## 최근 후속 커밋

### cfee2e25a — Split hashjoin range stats into per-phase types (2026-05-06)
**문제**: PR 7068 초기 구현이 `HASHJOIN_INPUT_STATS`에 `range_*` 필드 4개를 추가했는데, 이 구조체가 `xasl_node` proc 공용체 안의 `HASHJOIN_PROC_NODE`에 인라인으로 들어 있어 **모든** XASL 노드 크기가 부풀음. cbrd_20149_filter 회귀 테스트에서 XASL Cache Usage Percent가 0.23% → 0.24%로 상승.

**해결**: 통계 타입을 phase별로 분리.
| 타입 | 멤버 | 사용 phase |
|---|---|---|
| `HASHJOIN_INPUT_STATS` | 스칼라만 (POD) | split, parallel |
| `HASHJOIN_BUILD_STATS : HASHJOIN_INPUT_STATS` | + `range_elapsed_time` | build |
| `HASHJOIN_PROBE_STATS : HASHJOIN_INPUT_STATS` | + `HASHJOIN_RANGE_STATS range` (4 필드) | probe |
| `HASHJOIN_RANGE_STATS` | range 4필드 묶음 (POD) | probe, SHARED_PROBE_INFO |

**제약 조건 — Union 트랩**: `HASHJOIN_PROC_NODE`가 `xasl_node`의 anonymous union 멤버이므로 transitive 멤버는 모두 trivially constructible 유지 필수. 따라서 통계 타입에 사용자 정의 ctor 추가 금지 → POD + `_INITIALIZER` 매크로로 명시.

**결과**: 메모리 절감으로 Usage Percent 0.23% 회복 (testcases 측 [tc/pr-7068 0b304b6c](https://github.com/CUBRID/cubrid-testcases-private-ex/commit/0b304b6c)에서 답안 복원).

## 주요 변경사항

### 1. ✅ `parallel_query::hash_join::worker_pool_manager` 제거
**변경**: `parallel_query::hash_join::worker_pool_manager` → `parallel_query::worker_manager` 사용

**장점**:
- 중복 코드 제거
- 기존 `parallel_query::worker_manager` 재사용으로 일관성 향상
- 코드베이스 단순화

**파일**:
- `px_hash_join.hpp`: `worker_pool_manager` 클래스 제거
- `px_hash_join.cpp`: `worker_pool_manager` 구현 제거
- `query_hash_join.h`: Forward declaration 변경
- `query_hash_join.c`: `parallel_query::worker_manager::try_reserve_workers()` 사용

### 2. ✅ `entry_manager` 제거
**변경**: `parallel_query::hash_join::entry_manager` 클래스 완전 제거

**이전 역할**:
- Worker thread 생성 시 main thread 정보 복사
- Resource tracking 시작/종료

**대체 방법**: `task_execution_guard`로 이동 (아래 참조)

**파일**:
- `px_hash_join.hpp`: `entry_manager` 클래스 선언 제거
- `px_hash_join.cpp`: `entry_manager` 구현 제거

### 3. ✅ `task_execution_guard` 추가 (RAII 패턴)
**변경**: 새로운 RAII 헬퍼 클래스 추가

**구현**:
```cpp
class task_execution_guard {
  // 생성자: main thread 정보 복사 + resource tracking 시작
  // 소멸자: resource tracking 종료
};
```

**장점**:
- 예외 안전성 보장
- 리소스 누수 방지
- 코드 가독성 향상

**사용 위치**:
- `split_task::execute()`: 시작 부분에 guard 생성
- `join_task::execute()`: 시작 부분에 guard 생성

**검토 사항**:
- ✅ 생성자에서 `main_thread_ref.tran_index` 직접 접근 (참조이므로 NULL 체크 불필요)
- ✅ 소멸자에서 리소스 정리
- ✅ `inline` 함수로 성능 최적화

### 4. ✅ `task_manager` 생성자 변경
**변경 전**:
```cpp
task_manager(cubthread::entry_workpool *worker_pool, cuberr::context &main_error_context)
```

**변경 후**:
```cpp
task_manager(worker_manager *worker_manager, cubthread::entry &main_thread_ref)
```

**장점**:
- `main_thread_ref` 직접 전달로 더 명확한 의도
- `main_error_context`는 `main_thread_ref.get_error_context()`로 자동 추출
- `worker_manager` 직접 사용으로 추상화 레이어 감소

**멤버 변수 변경**:
- `m_worker_pool` → `m_worker_manager`
- `m_main_thread_ref` 추가 (참조)
- `m_main_error_context`는 `main_thread_ref`에서 추출

### 5. ✅ `placement_new` 사용으로 통일
**변경**: `new (ptr) T()` → `placement_new<T>(ptr, ...)`

**변경 위치**:
- `px_hash_join_spawn_manager.cpp`: `spawn_manager`, `cubxasl::spawner` 생성
- `query_hash_join.c`: `std::mutex` 생성

**장점**:
- `#undef new` / `#define new` 매크로 처리 불필요
- 코드 일관성 향상
- 템플릿 함수로 타입 안전성 향상

**주의사항**:
- ✅ 템플릿 타입 명시: `placement_new<std::mutex>`
- ✅ 포인터 캐스팅: `(spawn_manager *) raw_memory`

### 6. ✅ 에러 처리 개선
**변경**:
- `has_error()`: `std::memory_order_acquire` 사용
- `handle_error()`: `std::memory_order_acq_rel` 사용
- `stop_execution()` 제거 (불필요)

**장점**:
- 메모리 순서 명시로 성능 최적화
- 불필요한 메서드 제거

### 7. ✅ `worker_manager` API 변경
**변경**:
- `try_reserve_workers()`: 반환값이 `int` → `worker_manager *`
- `release_workers()`: 인자 제거 (멤버 변수 사용)

**장점**:
- Factory 패턴으로 일관성 향상
- 리소스 관리 단순화

### 8. ✅ 코드 정리
**제거된 항목**:
- `stop_execution()` 호출 제거 (불필요)
- `main_thread_p` 변수 제거 (직접 사용)
- Try-catch 블록 제거 (불필요한 방어적 프로그래밍)

**개선된 항목**:
- 인덴트 정리
- 주석 개선
- 변수명 일관성

## 잠재적 문제점 및 제안

### 1. ⚠️ `task_execution_guard` 소멸자
**현재 코드**:
```cpp
~task_execution_guard() {
  m_thread_ref.conn_entry = nullptr;
  m_thread_ref.on_trace = false;
  m_thread_ref.pop_resource_tracks();
}
```

**검토 필요**:
- `conn_entry = nullptr` 설정이 적절한지 확인 필요
- 다른 곳에서 `conn_entry`를 사용하는지 확인

### 2. ✅ `m_index` const 변경
**변경**: `int m_index` → `const int m_index`

**장점**: 불변성 보장, 실수 방지

### 3. ✅ 메모리 순서 최적화
**변경**: `std::atomic` 연산에 메모리 순서 명시

**장점**: 성능 최적화, 의도 명확화

## 테스트 권장사항

1. **기능 테스트**:
   - Parallel hash join 정상 동작 확인
   - Worker thread 생성/해제 확인
   - 에러 처리 확인

2. **성능 테스트**:
   - 메모리 순서 최적화 효과 확인
   - `inline` 함수 성능 확인

3. **예외 안전성 테스트**:
   - `task_execution_guard` 예외 상황 테스트
   - 리소스 누수 확인

## 전체 평가

### ✅ 긍정적인 변경
1. 코드 단순화 (87줄 감소)
2. 중복 코드 제거
3. RAII 패턴으로 예외 안전성 향상
4. 메모리 순서 최적화
5. 일관성 향상

### ⚠️ 주의 필요
1. `task_execution_guard` 소멸자의 `conn_entry = nullptr` 설정 검토
2. `worker_manager` API 변경에 따른 다른 사용처 확인 필요

### 📝 권장사항
1. `task_execution_guard` 소멸자 로직 재검토
2. 통합 테스트 강화
3. 문서화 업데이트

## 결론
전반적으로 코드 품질이 향상되었습니다. 주요 개선사항:
- 코드 단순화 및 중복 제거
- RAII 패턴 도입으로 안전성 향상
- 성능 최적화 (메모리 순서, inline)
- 일관성 향상

몇 가지 세부사항만 검토하면 좋을 것 같습니다.
