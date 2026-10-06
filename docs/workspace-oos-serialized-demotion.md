# Workspace OOS: 직렬화된 값의 직접 변환 설계

상태: 사용자 설계 확정 및 구현 승인 완료. 구현·검증 진행 중이며, 채택된 OOS 명세를 변경하지 않는다.
조사 기준: `90f3bd79e` (기존 PR #7925 작업 브랜치).
용어: [GLOSSARY.md](../GLOSSARY.md).

## 합의된 문제와 방향

현재 standalone workspace 저장 경로는 직렬화된 레코드를 속성 값으로 읽고 다시 직렬화하여 OOS를 적용한다.
이번 대화의 목표는 이 경로에서 불필요한 `DB_VALUE` decode/re-encode를 제거하는 것이다.
사용자는 클래스 표현 정보를 이용한 속성·저장 정책 조회와, 각 행의 값을 `DB_VALUE`로 변환하는 작업을 구분했다.
클래스 표현 정보를 조회하는 호출 자체가 매번 스키마를 디스크에서 새로 읽는다는 뜻은 아니다.

제안된 변환은 원래 레코드의 직렬화된 속성 바이트를 OOS 저장에 사용하고, 선택한 속성 위치에
OOS inline stub을 기록한 새 레코드를 만든다. 이후 속성의 offset과 OOS 표시를 재계산해야 한다.
메모리 workspace 객체를 변경하거나 객체 참조를 다시 부여하는 방식은 목표가 아니다.

## 확인된 코드 근거

- `src/transaction/locator_sr.c`: `locator_oos_demote_workspace_record`는 최종 저장 대상을 결정한 뒤
  `heap_attrinfo_start`, `heap_attrinfo_read_dbvalues_without_oid`,
  `locator_allocate_copy_area_by_attr_info`를 호출한다.
- `src/storage/heap_file.c`: `heap_classrepr_get`는 클래스 OID로 캐시를 먼저 조회한다.
  `heap_attrinfo_insert_to_oos`는 직렬화된 payload를 만든 뒤 `heap_oos_insert_serialized_values`에 전달한다.
- `src/storage/oos_file.hpp`: `oos_buffer`는 바이트 범위이다. OOS 저장 API가 `DB_VALUE`를 직접 요구하지는 않는다.
- 최신 HEAD는 기존 workspace CLI 회귀를 GoogleTest로 전환했다. 이전 Python 테스트를 기준으로 구현하지 않는다.
- 현재 `heap_attrinfo_determine_disk_layout`의 크기 기준은 `DB_PAGESIZE / 4`이다. 이는 OOS 명세의
  물리적 네 레코드 수용량 기준과 다른 기존 구현 차이이며, 이번 최적화에서 조용히 변경하지 않는다.
- `OR_ATTRIBUTE`는 저장 정책과 속성 위치를 제공하므로 metadata 조회를 위해 행별 `HEAP_ATTRVALUE`가 필수는 아니다.
- `transform_cl.c`의 `put_varinfo`/`put_attributes`는 가변 값 영역을 정렬한다. VOT로 얻는 범위에는 타입 인코딩과
  패딩이 포함될 수 있다. NULL과 빈 값을 구분하고 압축 VARCHAR, collection, JSON, 객체 참조, LOB locator를 검증해야 한다.
- `heap_oos.cpp`의 `heap_oos_compute_layout`은 기존 역방향 변환의 구조 분리 참고 자료이다.
  `heap_oos_begin_insert_publication`의 OID/LSA 상태 초기화와 force 상위 작업의 물리적 rollback 계약을 유지해야 한다.

## 설계 결정 트리

1. 적용 범위 — 사용자 확인 완료.
   - 사용자의 관심은 loaddb가 OOS를 사용하도록 만든 과정에서 추가된 `DB_VALUE` 왕복 비용이다.
     이 PR의 SA workspace 변환 최적화로 한정한다. 일반 SQL/CS 경로는 결과 비교 대상이다.
   - 공용 정책 선택기의 필요성과 책임 경계는 기존 동작을 보존하는 범위에서 코드로 검토한다.
2. 완료 기준 — 사용자 확인 완료.
   - 동일 데이터의 변경 전후 loaddb 적재 시간/CPU를 비교한다.
   - CS/SA의 SELECT 결과와 oos_read로 읽은 값의 바이트가 일치해야 한다.
     OID와 트랜잭션 정보의 일치는 요구하지 않는다.
   - 어떤 컬럼을 OOS로 보내는지도 CS/SA와 기존 방식 사이에서 같아야 한다.
     따라서 VOT 폭 유지로 정책 선택이 달라지는 결과는 허용하지 않는다.
   - 데이터 구성과 반복 횟수는 현재 테스트 기반 및 비용 조사 후 구체화한다.
3. 원시 바이트 변환의 정확성 — 사실 확인 중, 선택은 다음 문답에서 확정한다.
   - 현재 OOS/LAST 플래그는 offset 하위 비트를 사용하며 1/2/4바이트 접근자가 있다.
     축소된 레코드가 기존 폭을 유지하는 것 자체가 금지되지는 않는다.
     그러나 기존 planner는 폭을 다시 계산하므로, 기존 폭 유지는 크기 경계에서 OOS 선택 수나 bigone 판정을 바꿀 수 있다.
     기존 planner처럼 크기와 폭을 계산하여 OOS 선택 결과를 보존한다.
   - NULL/빈 값, 정렬 패딩, 압축 값, 객체 참조 및 외부 LOB locator의 바이트 범위를 확인한다.
   - 과거 representation이 실제 이 경로에 들어오는지 먼저 조사한다. 일반 heap 변환기가 과거 표현을 지원한다는
     사실만으로 workspace 전용 경로에 fallback을 요구하지 않는다.
     `tf_disk_to_mem`은 과거 행을 `get_old`로 읽고 현재 메모리 객체로 변환하며, `tf_mem_to_disk`는
     현재 클래스의 `class_->repid`를 기록한다. `partition_find_partition_for_record`는 저장 대상 파티션의
     representation ID로 변경하며, 파티션 사이에서는 ID 외 레이아웃이 동일하다는 기존 계약을 명시한다.
     `schema_manager.c`의 구조 변경 경로는 `needrep`일 때 기존 인스턴스를 flush/decache한 뒤 새 구조를 설치한다.
     조사한 정상 SA 호출 경로에서 과거 representation 입력은 발견하지 못했다.
     입력 계약은 현재 저장 대상 클래스 형식으로 직렬화·partition pruning이 끝난 workspace 레코드로 한정한다.
     representation 검사와 DDL/partition 회귀를 추가하되, 범용 과거 레코드 변환 기능은 추가하지 않는다.
     이 결론은 정적 경로 조사이며 아직 실행 검증 결과는 아니다.
   - 기존 OOS 배치 저장·실패 정리·상위 작업의 rollback 계약을 확인한다.

## 유지해야 하는 동작과 검증 후보

- 변환 전에 최종 파티션을 결정하고 해당 파티션의 OOS 파일에 저장한다.
- 기존 largest-first, 컬럼 저장 정책, OOS+bigone 거부 동작을 보존한다.
- OOS는 기존 overflow 저장 전체를 대체하지 않는다. 비-OOS 큰 레코드는 별도로 취급한다.
- CHN, 객체 참조, 외부 LOB 파일의 소유권을 보존한다.
- 작은 값/NULL/빈 값, 여러 가변 속성, 여러 OOS 조각, partition 이동을 확인한다.
- INSERT/UPDATE rollback, 무시한 unique 오류 및 전체 적재 실패 뒤 OOS 소유권을 확인한다.
- 검증용 테스트에서 기존 경로와 결과를 비교하는 것과, 운영 저장 경로에서 변환을 반복하는 것은 다르다.

아직 성능 개선이나 테스트 통과를 주장하지 않는다. 결정 근거는
[직렬화된 workspace 값의 직접 OOS 변환](adr/0001-workspace-oos-serialized-values.md)에 기록한다.
