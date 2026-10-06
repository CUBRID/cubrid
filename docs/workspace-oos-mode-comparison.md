# Collection 인코딩의 기존 경로 차이

2026-10-06, PR #7925 원시 바이트 변환 검증 중 확인했다. 이는 이번 최적화로 생긴 차이가 아니다.

두 테이블은 각각 `a SEQUENCE OF INTEGER STORAGE FORCE_OUTLINE` 하나의 컬럼을 가진다.
동일한 객체 파일 값 `{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}`을
`cubrid loaddb -S`와 실제 서버에 연결하는 `cubrid loaddb -C`로 적재했다.

| 저장 경로 | SELECT 값 | Oos_num_recs | Oos_recs_sumlen |
|---|---|---:|---:|
| 변경 전 `90f3bd79e`, SA loader | 동일한 16개 원소 | 1 | 100 |
| 원시 바이트 변환, SA loader | 동일한 16개 원소 | 1 | 100 |
| 원시 바이트 변환 빌드, CS loader | 동일한 16개 원소 | 1 | 236 |

`SELECT s.a=c.a AS equal_value FROM t_sa s, t_cs c`의 결과는 `1`이다.
`Oos_recs_sumlen`은 OOS chunk header를 포함하므로 이 숫자를 payload 길이라고 부르지 않는다.
원시 payload의 크기가 같지 않다는 증거로 사용한다.

GoogleTest의 동일 workspace 입력 비교에서는 기존 decode/re-encode와 새 변환의 OOS 선택과
`oos_read` payload가 일치했다. 추가한 일반 SQL writer와의 비교는 collection에서 실패했다.
다른 20개 바이트/경계/DDL 비교는 통과했다. collection의 타입 정보 포함 여부와 원소 표현이 경로마다 다르다.

소스 근거는 `object_primitive.c`의 `mr_data_writemem_set`, `mr_data_writeval_set`,
`mr_data_readval_set`이다. 기존 workspace 경로의 readval은 원래 collection disk image를 보존한다.
따라서 그 경로의 DB_VALUE 왕복을 제거하는 것만으로 CS/SA collection 인코딩이 통일되지는 않는다.

현재 완료 기준의 “기존 저장 결과 보존”과 “CS/SA 원시 바이트 완전 일치”를 이 자료형까지 동시에
요구할지는 사용자에게 확인 중이다. 테스트를 숨기거나 인코딩을 임의로 변경하지 않는다.

로컬 실행 증거:

- `build_preset_debug_gcc/workspace-mode-comparison/`: 스키마, 객체 파일, 실제 SA/CS 적재 및 SELECT/SHOW 출력.
- `build_preset_release_gcc/workspace-mode-baseline/`: 보존한 변경 전 Release 설치로 실행한 SA 재현.
- `/tmp/cbrd27424-focused-ctest.log`: 기존 변환/새 변환 및 일반 SQL writer 비교 결과.

## Publication validation snapshot

The user requested committing, pushing, and running PR CI with these findings still open.
The collection comparison remains enabled and failing; its acceptance criterion has not been relaxed.
Debug and Release builds passed. The latest focused CTest run passed 5 of 6 entries, including
fixture setup/cleanup; the workspace byte executable passed 20 of 21 GoogleTest cases.
The existing loader regression executable, including the new bigone case, passed all 13 cases.

The shell benchmark used fresh databases, identical deterministic fixtures, and three runs per workload.
Only loaddb was timed. These are local sequential measurements, not evidence of a speedup or a causal diagnosis.

| Workload | Baseline elapsed (s) | Candidate elapsed (s) | Baseline user + system CPU (s) | Candidate user + system CPU (s) |
|---|---|---|---|---|
| 100,000 rows, 48 bytes | 2.17, 2.16, 2.13 | 2.13, 2.16, 2.12 | 1.04, 1.03, 1.01 | 1.00, 1.03, 1.00 |
| 10,000 rows, 5,000 bytes | 5.15, 5.05, 5.11 | 5.66, 5.60, 5.86 | 3.50, 3.44, 3.47 | 3.89, 4.03, 4.07 |

Baseline: `90f3bd79e`, Release. Candidate: the raw workspace record implementation in this change, Release.
Large-value median elapsed time increased from 5.11 to 5.66 seconds (10.8%); median CPU increased
from 3.47 to 4.03 seconds (16.1%). Performance investigation remains necessary.
Reproduce with `bash unit_tests/oos/scripts/benchmark_workspace_oos.sh INSTALL OUTPUT LABEL 3`.
