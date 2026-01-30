# DBLINK 2PC 분산 트랜잭션 Recovery 설계

## 개요
dblink_auto_commit이 off인 트랜잭션에서 dblink DML을 사용할 때 coordinator 복구를 위해
`_db_global_tran` 카탈로그 테이블과 send_2pc_decision_daemon을 사용한다.

## 대상
- (1) dblink_auto_commit이 off인 트랜잭션
- (2) dblink 쿼리를 포함하는 DB 서버 = coordinator
- (3) dblink 쿼리를 실제 실행하는 DB 서버들 = participant
- (4) participant가 1개 이상이면 2PC transaction
- (5) participant는 PREPARE 상태까지 recovery로 복원 가능

## Coordinator Recovery 대비

### 1. _db_global_tran 카탈로그 테이블
| 컬럼 | 타입 | 설명 |
|------|------|------|
| gtrid | int | Global transaction id |
| bqual | int | Branch qualifier (participant id, e.g. conn_handle at prepare) |
| conn_url | varchar(512) | Participant connection URL |
| user | varchar(32) | Participant user |
| password | varchar(32) | Participant password |
| state | varchar(1) | 'P'=Prepare 전, 'A'=Abort 결정, 'C'=Commit 결정 |
| created_date | datetime | 생성 시각 |
| updated_date | datetime | 갱신 시각 |

- state 'P': prepare 전 상태 (insert 시점)
- state 'A': prepare 후 Abort로 결정
- state 'C': prepare 후 Commit으로 결정

### 2. send_2pc_decision_daemon
- **global_tran_queue**: coordinator → daemon. participant 정보(insert/update용) 전달.
- **send_decision_queue**: _db_global_tran 기록 후 send decision 실행을 위한 내부 큐/시그널.

**동작:**
1. **초기 recovery**: _db_global_tran에서 state in ('A','C')인 행을 읽고, 각 participant에게 abort/commit decision 전송. 성공 시 해당 행 삭제, 실패 시 유지 후 반복 전송.
2. **이후 대기**: coordinator로부터 데이터를 받기 위해 cond_wait.
3. **coordinator에서 데이터 수신**:
   - prepare 전: daemon에 participant 데이터 전송 → daemon이 _db_global_tran에 insert(gtrid, bqual, ..., 'P', ...).
   - prepare 후 decision 단계: daemon에 participant 데이터 전송 → daemon이 _db_global_tran update(..., 'A' or 'C', ...).
   - 기록 후 daemon이 send abort/commit decision 실행. 성공 시 행 삭제, 실패 시 재시도(또는 recovery에서 처리).

### 3. Coordinator 연동
- participant에 **prepare 보내기 전**: coordinator가 daemon으로 participant 데이터 전송 → daemon이 _db_global_tran에 insert(..., 'P').
- **prepare 후 abort/commit decision 단계**: coordinator가 daemon으로 participant 데이터 전송 → daemon이 _db_global_tran update(..., 'A' or 'C'), 이어서 send decision 실행.

## 구현 파일
- `schema_system_catalog_constants.h`: CT_GLOBAL_TRAN_NAME
- `schema_system_catalog_install.cpp`: _db_global_tran 테이블 정의 및 등록
- `object/schema_system_catalog.cpp`: sm_system_class_names에 CT_GLOBAL_TRAN_NAME 추가
- `query/dblink_2pc_daemon.c`, `query/dblink_2pc_daemon.h`: daemon, global_tran_queue, recovery
- `query/dblink_2pc.c`, `query/dblink_2pc.h`: dblink_2pc_send_decision_one_participant (recovery용)
- `transaction/log_2pc.c`: prepare 전/decision 단계에서 dblink_2pc_daemon_enqueue 호출
- `transaction/log_recovery.c`: recovery 완료 후 dblink_2pc_daemon_start() 호출

## 남은 작업 (TODO)
1. **_db_global_tran 카탈로그 접근**: `dblink_2pc_daemon.c`의 다음 함수 구현
   - `dblink_2pc_daemon_insert_global_tran_prepare`: state 'P' 행 insert (participant별 1행)
   - `dblink_2pc_daemon_recovery`: _db_global_tran scan (state 'A'/'C'), send decision, 성공 시 delete
   - (선택) decision 전송 전/후 _db_global_tran update (state 'A'/'C') 및 delete
2. **서버 종료 시**: shutdown 경로에서 `dblink_2pc_daemon_stop()` 호출 추가
3. **빌드**: 새 소스 `dblink_2pc_daemon.c`를 해당 디렉터리 Makefile/빌드 스크립트에 추가
