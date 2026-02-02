# DBLINK 2PC 분산 트랜잭션 Recovery 설계

## 1. 개요

`dblink_auto_commit`이 OFF인 트랜잭션에서 dblink DML을 사용할 때, coordinator 크래시 복구를 위해 `_db_global_tran` 시스템 카탈로그 테이블과 `send_2pc_decision_daemon`을 사용한다.

### 1.1 용어 정의
| 용어 | 설명 |
|------|------|
| Coordinator | dblink 쿼리를 포함하는 DB 서버 (2PC의 조정자) |
| Participant | dblink 쿼리를 실제 실행하는 원격 DB 서버 |
| 2PC | Two-Phase Commit (2단계 커밋 프로토콜) |
| gtrid | Global Transaction ID (전역 트랜잭션 식별자) |
| bqual | Branch Qualifier (participant별 식별자, conn_handle 값 사용) |

### 1.2 적용 대상
- `dblink_auto_commit = OFF`인 트랜잭션
- participant가 1개 이상인 경우 2PC 프로토콜 적용
- participant는 PREPARE 상태까지 자체 recovery로 복원 가능

---

## 2. 아키텍처

### 2.1 Recovery 흐름도

```
[Coordinator]                                      [Participant]
     |                                                   |
     |-- (1) INSERT _db_global_tran (state='P') -------->|
     |      (server transaction)                         |
     |                                                   |
     |-- (2) SEND XA PREPARE --------------------------->|
     |<-------------------------- PREPARE OK/FAIL -------|
     |                                                   |
     |-- (3) UPDATE _db_global_tran (state='C'/'A') ---->|
     |      (server transaction)                         |
     |                                                   |
     |-- (4) Enqueue to daemon ------------------------->|
     |                                                   |
[Daemon]                                                 |
     |-- (5) SEND XA COMMIT/ROLLBACK ------------------->|
     |<-------------------------- COMMIT/ROLLBACK OK ----|
     |                                                   |
     |-- (6) DELETE _db_global_tran (server transaction) |
```

### 2.2 크래시 시나리오별 Recovery

| 크래시 시점 | _db_global_tran 상태 | Recovery 동작 |
|------------|---------------------|---------------|
| (1) 이전 | 없음 | Participant 자체 rollback |
| (1)~(2) 사이 | state='P' | Daemon이 ABORT 전송 후 DELETE |
| (2)~(3) 사이 | state='P' | Daemon이 ABORT 전송 후 DELETE |
| (3)~(5) 사이 | state='C' 또는 'A' | Daemon이 해당 decision 전송 후 DELETE |
| (5)~(6) 사이 | state='C' 또는 'A' | Daemon이 decision 재전송 후 DELETE |

---

## 3. _db_global_tran 카탈로그 테이블

### 3.1 스키마

| 컬럼 | 타입 | 설명 |
|------|------|------|
| gtrid | INT | Global Transaction ID |
| bqual | INT | Branch Qualifier (conn_handle) |
| conn_url | VARCHAR(512) | Participant 연결 URL |
| user | VARCHAR(32) | Participant 사용자명 |
| password | VARCHAR(32) | Participant 비밀번호 |
| state | CHAR(1) | 트랜잭션 상태 ('P', 'A', 'C') |
| created_date | DATETIME | 레코드 생성 시각 |
| updated_date | DATETIME | 레코드 갱신 시각 |

### 3.2 State 값 의미

| State | 의미 | Recovery 동작 |
|-------|------|---------------|
| 'P' | Prepare 전 상태 | ABORT decision 전송 |
| 'A' | Abort 결정됨 | ABORT decision 전송 |
| 'C' | Commit 결정됨 | COMMIT decision 전송 |

### 3.3 Server Transaction 처리

`_db_global_tran`에 대한 모든 DML(INSERT/UPDATE/DELETE)은 **server transaction** (`log_sysop_start`/`log_sysop_commit`)으로 처리된다. 이는 coordinator 트랜잭션과 독립적으로 커밋되어 크래시 시에도 recovery 정보가 보존되도록 보장한다.

---

## 4. Coordinator 동작 (log_2pc.c)

### 4.1 Phase 1: Prepare 단계 (`log_2pc_commit_first_phase`)

```c
// Step 1: _db_global_tran INSERT (state='P') - server transaction
log_sysop_start(thread_p);
for (each participant) {
    dblink_global_tran_insert_row(..., DBLINK_2PC_STATE_PREPARE);
}
log_sysop_commit(thread_p);

// Step 2: Send XA PREPARE to all participants
decision = log_2pc_send_prepare(...);

// Step 3: _db_global_tran UPDATE (state='C' or 'A') - server transaction
log_sysop_start(thread_p);
for (each participant) {
    dblink_global_tran_update_state(..., decision ? 'C' : 'A');
}
log_sysop_commit(thread_p);

// Step 4: Enqueue to daemon
dblink_2pc_daemon_enqueue(..., decision ? 'C' : 'A', ...);
```

### 4.2 Phase 2: Decision 단계 (`log_2pc_commit_second_phase`)

- Daemon에 의해 비동기적으로 decision 전송 처리
- `_db_global_tran` update 및 enqueue는 Phase 1에서 완료됨

### 4.3 LOG_2PC_START 로그 미사용

CCI_XA 모드에서는 `_db_global_tran` 테이블이 recovery 정보를 관리하므로, 기존 `LOG_2PC_START` 로그 레코드는 생성하지 않는다.

---

## 5. Daemon 동작 (dblink_2pc_daemon.c)

### 5.1 구조

```c
typedef struct global_tran_queue_entry {
    int gtrid;
    char state;           // 'P', 'A', 'C'
    int num_participants;
    DBLINK_CONN_INFO *participants;
} GLOBAL_TRAN_QUEUE_ENTRY;
```

### 5.2 시작 시 Recovery

서버 시작 시 `dblink_2pc_daemon_recovery_with_thread()` 함수가 호출되어:

1. `_db_global_tran` 테이블을 스캔 (state = 'P', 'A', 'C')
2. 각 레코드에 대해:
   - state='P': ABORT decision 전송
   - state='A': ABORT decision 전송
   - state='C': COMMIT decision 전송
3. 전송 성공 시 해당 레코드 DELETE (server transaction)

### 5.3 Queue 처리

Coordinator로부터 enqueue된 데이터 처리:

```c
if (state == DBLINK_2PC_STATE_PREPARE) {
    // Prepare 상태에서는 ABORT 전송 (recovery 시나리오)
    dblink_2pc_daemon_send_decision(..., DBLINK_2PC_STATE_ABORT, ...);
} else {
    // 'A' 또는 'C' 상태: 해당 decision 전송
    dblink_2pc_daemon_send_decision(..., state, ...);
}
```

---

## 6. 카탈로그 접근 API (dblink_global_tran_catalog.c)

### 6.1 제공 함수

| 함수 | 설명 |
|------|------|
| `dblink_global_tran_insert_row()` | 새 participant 레코드 INSERT |
| `dblink_global_tran_update_state()` | state 컬럼 UPDATE |
| `dblink_global_tran_delete_row()` | 레코드 DELETE |
| `dblink_global_tran_scan_for_recovery()` | Recovery를 위한 테이블 스캔 |

### 6.2 구현 특징

- `locator_*` 및 `heap_*` API 사용 (SQL 미사용)
- Server transaction 내에서 호출되어야 함
- `CCI_XA` 매크로로 조건부 컴파일

---

## 7. 구현 파일 목록

| 파일 | 역할 |
|------|------|
| `compat/dbtype_def.h` | `DB_OBJECT_GLOBAL_TRAN` 타입 정의 |
| `object/schema_system_catalog_constants.h` | `CT_GLOBAL_TRAN_NAME` 상수 |
| `object/schema_system_catalog_install.cpp` | 테이블 스키마 정의 및 등록 |
| `object/schema_system_catalog.cpp` | 시스템 클래스 이름 등록 |
| `query/dblink_2pc_daemon.h` | Daemon 헤더 |
| `query/dblink_2pc_daemon.c` | Daemon 구현, queue 처리, recovery |
| `query/dblink_global_tran_catalog.h` | 카탈로그 API 헤더 |
| `query/dblink_global_tran_catalog.c` | 카탈로그 INSERT/UPDATE/DELETE/SCAN 구현 |
| `query/dblink_2pc.h` | 2PC 관련 상수 및 함수 헤더 |
| `query/dblink_2pc.c` | `dblink_2pc_send_decision_one_participant()` 구현 |
| `transaction/log_2pc.c` | Coordinator 2PC 로직, daemon enqueue |
| `transaction/log_recovery.c` | 서버 시작 시 `dblink_2pc_daemon_start()` 호출 |

---

## 8. 빌드 설정

다음 CMakeLists.txt 파일에 소스 추가:
- `cubrid/CMakeLists.txt`: QUERY_SOURCES에 `dblink_2pc_daemon.c`, `dblink_global_tran_catalog.c`
- `sa/CMakeLists.txt`: QUERY_SOURCES에 `dblink_2pc_daemon.c`, `dblink_global_tran_catalog.c`

조건부 컴파일: `CCI_XA` 매크로 정의 시에만 관련 코드 활성화
