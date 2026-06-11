# LB Proxy 설계 문서

**작성일**: 2026-06-11  
**버전**: 0.3 (초안)  
**대상 버전**: CUBRID 11.5.x 이후

---

## 1. 개요

### 1.1 목적

현재 CUBRID Broker/CAS 구조에 **로드 밸런싱 프록시(LB Proxy)** 를 추가하여 다음을 달성한다.

- 클라이언트(JDBC/CCI)가 RO/RW 쿼리를 자동으로 인식하여 각각 적합한 DB 서버에 직접 접속
- Proxy를 데이터 경로에서 완전히 배제 — Proxy 오버헤드 Zero
- DB 서버 구성(RO/RW 서버 목록)을 Proxy가 중앙 관리하여 클라이언트에 제공

### 1.2 설계 방향 결정 배경

이전 검토에서 다음 두 가지 relay 방식의 문제점이 확인되었다.

| 방식 | 문제 |
|------|------|
| 순수 Relay (Shard Proxy 방식) | Proxy가 모든 쿼리/응답을 복사 → 대용량 응답에서 오버헤드 과다 |
| FD Bounce (SCM_RIGHTS 왕복) | SCM_RIGHTS는 동일 호스트 간에만 동작 → RO/RW 서버가 다른 머신에 있는 일반 구성에 적용 불가 |

따라서 **Proxy를 데이터 경로에서 완전히 제거**하는 방향으로 설계를 전환한다.
클라이언트 드라이버(JDBC/CCI)가 RO/RW를 판단하고 직접 서버에 접속한다.

### 1.3 요구사항

| 번호 | 요구사항 |
|------|---------|
| R-01 | Proxy는 RO/RW 서버 목록을 관리하고 클라이언트 요청 시 제공한다 |
| R-02 | 클라이언트(JDBC/CCI 드라이버)는 Proxy로부터 받은 서버 목록을 캐싱하고 RO/RW 쿼리를 직접 라우팅한다 |
| R-03 | Proxy는 서버 alive 여부를 주기적으로 감시하여 장애 서버를 목록에서 제외한다 |
| R-04 | 쿼리 처리 시 Proxy를 경유하지 않는다 — Proxy 오버헤드 Zero |
| R-05 | JDBC/CCI 드라이버 수정이 필요하다 (클라이언트 응용 코드 수정은 최소화) |

---

## 2. 아키텍처

### 2.1 전체 구조

```
┌──────────────────────────────────────────────────────────────────┐
│                    LB Proxy Process                               │
│  (토폴로지 서비스 + 헬스 모니터 — 데이터 경로에 없음)              │
│                                                                   │
│  [Topology Server]  ←── TCP (LB_PROXY_PORT)                      │
│    클라이언트 요청에 RO/RW 서버 목록 응답                           │
│                                                                   │
│  [Health Monitor Thread]                                          │
│    RO/RW 서버들에 주기적 heartbeat                                  │
│    장애 서버 감지 → alive 목록에서 제거                             │
│    복구 감지 → alive 목록 복귀                                      │
│                                                                   │
│  [Server Registry]                                                │
│    ro_servers[]: {host, port, state}  ← Slave Broker 목록        │
│    rw_servers[]: {host, port, state}  ← Master Broker 목록       │
└──────────────────────────────────────────────────────────────────┘
         │ heartbeat (TCP)         │ heartbeat (TCP)
         ▼                         ▼
  Broker (Slave DB)         Broker (Master DB)
  CAS × N                   CAS × M


클라이언트 접속 흐름:

  1. 최초 접속 시 (또는 TTL 만료 시):
     JDBC/CCI → LB Proxy: 서버 목록 요청
     LB Proxy → JDBC/CCI: {ro_servers, rw_servers, ttl}

  2. 이후 쿼리 (Proxy 완전 배제):
     SELECT  → JDBC/CCI가 ro_servers 중 하나 선택 → Broker 직접 접속 → 쿼리 실행
     INSERT  → JDBC/CCI가 rw_servers 중 하나 선택 → Broker 직접 접속 → 쿼리 실행
```

### 2.2 기존 방식과의 비교

| 항목 | Shard Proxy (relay) | **LB Proxy (본 설계)** |
|------|--------------------|-----------------------|
| Proxy의 데이터 경로 역할 | 모든 쿼리/응답 중개 | **없음** |
| Proxy 장애 시 영향 | 전체 서비스 중단 | **캐싱된 목록으로 계속 동작** |
| 쿼리 오버헤드 | Proxy relay + memcpy | **Zero** |
| RO/RW 분류 위치 | Proxy | **클라이언트 드라이버** |
| 드라이버 수정 | 불필요 | **필요** |
| 연결 관리 | Proxy pool | **드라이버 내 connection pool** |

---

## 3. 토폴로지 Discovery 프로토콜

### 3.1 프로토콜 개요

클라이언트가 Proxy에 접속하여 서버 목록을 요청하는 **단순 요청/응답 프로토콜**이다.
연결 즉시 요청하고 응답 수신 후 연결을 닫는다. 기존 쿼리 프로토콜(CAS protocol)과 완전히 독립적이다.

### 3.2 요청 패킷

```
┌───────────────────┬───────────────────┬────────────────────┐
│ magic: 4B         │ version: 4B       │ db_name_len: 4B    │
│ "CBLB"            │ 0x00000001        │                    │
├───────────────────┴───────────────────┴────────────────────┤
│ db_name: db_name_len bytes                                  │
└─────────────────────────────────────────────────────────────┘
```

### 3.3 응답 패킷

```
┌──────────────────────────────────────────────────────────┐
│ total_size: 4B                                            │
│ status: 4B      (0=OK, 1=DB_NOT_FOUND, 2=NO_SERVER)      │
│ ttl: 4B         (seconds, 클라이언트 캐시 유효 시간)        │
│ ro_count: 4B                                              │
│   [ro_count 반복]                                         │
│   ├── host_len: 4B                                        │
│   ├── host: host_len bytes                                │
│   └── port: 4B                                            │
│ rw_count: 4B                                              │
│   [rw_count 반복]                                         │
│   ├── host_len: 4B                                        │
│   ├── host: host_len bytes                                │
│   └── port: 4B                                            │
└──────────────────────────────────────────────────────────┘
```

- `ttl`: 클라이언트가 이 목록을 재사용할 수 있는 시간(초). 만료 후 재요청.
- `ro_count == 0`: RO 서버 없음. 드라이버는 RO 쿼리를 RW 서버로 라우팅.
- `rw_count == 0`: RW 서버 없음. 드라이버는 에러 반환.

### 3.4 Proxy 서버 구현 (lb_proxy.c)

```
Topology Server Thread:
  listen(LB_PROXY_PORT)
  accept(client_fd):
    read request (magic 검증, db_name 파싱)
    server_registry에서 alive RO/RW 서버 목록 조회
    응답 패킷 직렬화 후 전송
    close(client_fd)
```

요청 처리가 매우 단순하므로 단일 스레드 또는 소규모 스레드 풀로 충분하다.

---

## 4. 클라이언트 드라이버 변경 (JDBC / CCI)

### 4.1 새로운 Connection URL 형식

```
# JDBC
jdbc:cubrid:lb://proxy_host:proxy_port/db_name?property=value

# CCI (C API)
"lb:proxy_host:proxy_port:db_name:user:password"
```

기존 URL 형식(`jdbc:cubrid:host:port/db_name`)은 그대로 유지. `lb:` prefix가 있을 때만 LB 모드로 동작한다.

### 4.2 드라이버 내 토폴로지 관리

```
ConnectionManager (드라이버 내부):
  server_list_cache:
    ro_servers[]: {host, port}   ← Proxy로부터 수신한 alive RO 서버 목록
    rw_servers[]: {host, port}   ← Proxy로부터 수신한 alive RW 서버 목록
    fetched_at: timestamp
    ttl: seconds

  get_server_list(db_name):
    if (cache is valid):
      return cached list
    TCP connect → proxy_host:proxy_port
    send topology request (magic + db_name)
    recv topology response → 파싱
    cache 업데이트
    return server_list

  on_server_failure(host, port):
    해당 서버를 로컬 캐시에서 임시 제거
    get_server_list() 강제 재요청 (cache 무효화)
```

### 4.3 RO/RW 분류 (드라이버 측)

SQL 첫 유효 토큰만 검사한다 (Proxy 설계와 동일한 로직):

```java
// JDBC 예시 (Java)
static QueryType classify(String sql) {
    String token = firstToken(sql);  // 공백·주석 스킵 후 첫 단어
    switch (token.toUpperCase()) {
        case "SELECT": case "WITH": case "SHOW": case "EXPLAIN":
            return QueryType.RO;
        case "START":
            return isReadOnlyTran(sql) ? QueryType.RO : QueryType.RW;
        default:
            return QueryType.RW;  // INSERT/UPDATE/DELETE/DDL/CALL/MERGE
    }
}
```

### 4.4 연결 풀 구조

드라이버 내에 두 개의 connection pool을 유지한다:

```
LBConnectionPool:
  ├── ro_pool: ConnectionPool → RO 서버들에 대한 연결 집합
  │     서버 선택 전략: round-robin 또는 least-connections
  └── rw_pool: ConnectionPool → RW 서버들에 대한 연결 집합
        서버 선택 전략: primary 우선 (rw_servers[0])
```

### 4.5 쿼리 라우팅 흐름

```
connection.execute(sql):
  1. classify(sql) → RO 또는 RW
  2. if (IN_TRANSACTION):
       현재 트랜잭션의 connection 유지 (pool 교체 금지)
  3. else:
       RO → ro_pool에서 connection 획득
       RW → rw_pool에서 connection 획득
  4. connection.send(sql)
  5. 응답 수신
  6. if (autocommit=ON or END_TRAN):
       connection을 pool에 반환

connection 획득 실패 (서버 장애):
  on_server_failure() 호출 → 목록 갱신
  다른 서버로 재시도
```

### 4.6 트랜잭션 처리

트랜잭션 내에서 connection을 교체할 수 없으므로, **트랜잭션 시작 시 첫 쿼리**로 RO/RW를 결정하고 해당 connection을 트랜잭션이 끝날 때까지 고정한다.

| 상황 | 라우팅 |
|------|--------|
| autocommit=ON + SELECT/WITH/SHOW | ro_pool |
| autocommit=ON + INSERT/UPDATE/DELETE/DDL | rw_pool |
| `START TRANSACTION READ ONLY` | ro_pool |
| `START TRANSACTION` / `BEGIN` | rw_pool |
| autocommit=OFF (암묵적 트랜잭션) | rw_pool (보수적) |
| 트랜잭션 중 RO connection에 DML | 에러 반환 (트랜잭션 내 재라우팅 불가) |

---

## 5. Proxy — 헬스 모니터

### 5.1 동작 방식

Proxy는 쿼리를 처리하지 않으므로 헬스 모니터가 핵심 기능이다.

```
Health Monitor Thread (주기: LB_PROXY_HEARTBEAT_INTERVAL, 기본 1000ms):

  for each server in (ro_servers + rw_servers):
    TCP connect → server.host:server.port
    send CAS_FC_CHECK_CAS (func_code=32, 기존 Broker 프로토콜 활용)
    응답 성공 → state = ALIVE, fail_count = 0
    응답 실패 → state = DEAD, fail_count++
                 backoff: min(2^fail_count, 30)초 후 재시도

  state 변경 시:
    alive server 목록 갱신
    → 이후 클라이언트 topology 요청에 갱신된 목록 반환
```

`CAS_FC_CHECK_CAS`(code 32)는 CAS가 기존에 지원하는 alive 확인용 코드다.

### 5.2 장애 전파 시나리오

```
1. Slave DB 서버 장애 발생
   → Proxy Health Monitor: 해당 RO 서버를 DEAD 처리
   → 이후 topology 응답에서 해당 서버 제외

2. 클라이언트 TTL 만료 또는 연결 실패 시 topology 재요청
   → 갱신된 목록(장애 서버 제외) 수신
   → 해당 서버로의 신규 연결 시도 중단

3. Slave DB 서버 복구
   → Proxy Health Monitor: ALIVE 감지
   → topology 응답에 다시 포함
   → 클라이언트 다음 재요청 시 반영
```

### 5.3 Proxy 자체 장애 시

클라이언트는 캐싱된 서버 목록으로 계속 동작한다. TTL이 만료되어 Proxy에 재요청하더라도 Proxy가 응답 불가이면 이전 캐시를 그대로 사용하며 에러를 발생시키지 않는다.

```
get_server_list():
  try {
    TCP connect → proxy
    fetch new list
    update cache
  } catch (connection failed) {
    if (cache exists) → 기존 캐시 계속 사용, 경고 로그
    else              → 에러 반환 (최초 접속 시에만 필수)
  }
```

---

## 6. 구현 범위

### 6.1 LB Proxy (신규 프로세스)

| 파일 | 역할 |
|------|------|
| `src/broker/lb_proxy.c` | 메인 프로세스, Topology Server (TCP listener), 시작/종료 |
| `src/broker/lb_proxy_health.c` | Health Monitor 스레드, 서버 alive 상태 관리 |
| `src/broker/lb_proxy_registry.c` | Server Registry (RO/RW 서버 목록, 상태 CRUD) |
| `src/broker/lb_proxy_protocol.c` | Topology 요청/응답 패킷 직렬화/역직렬화 |
| `src/broker/lb_proxy.h` | 공유 타입, 상수, magic 정의 |

### 6.2 Broker 설정 (수정)

| 파일 | 수정 내용 |
|------|---------|
| `src/broker/broker_config.h` | `T_BROKER_INFO`에 `lb_proxy_*` 필드 추가 |
| `src/broker/broker_config.c` | `LB_PROXY_*` 설정 키워드 파싱 추가 |
| `src/broker/broker.c` | 시작 시 `lb_proxy_on` 확인 → LB Proxy 컴포넌트 기동 |
| `CMakeLists.txt` | `cub_lbproxy` 빌드 타겟 추가 |
| `conf/cubrid_broker.conf.in` | LB_PROXY 설정 예시 추가 |

### 6.3 JDBC 드라이버 (cubrid-jdbc, 수정)

| 변경 위치 | 내용 |
|-----------|------|
| `CUBRIDDriver.java` | `lb://` URL scheme 파싱 추가 |
| `LBConnectionManager.java` (신규) | Topology 요청, 서버 목록 캐싱, TTL 관리 |
| `LBConnectionPool.java` (신규) | RO/RW 이중 connection pool |
| `LBStatement.java` (신규) | SQL 분류(RO/RW) → 적절한 pool에서 connection 선택 |
| `QueryClassifier.java` (신규) | 첫 토큰 기반 RO/RW 분류 |

### 6.4 CCI 드라이버 (cubrid-cci, 수정)

| 변경 위치 | 내용 |
|-----------|------|
| `cci_connection.c` | `lb:` prefix 파싱, Topology 요청 로직 추가 |
| `lb_pool.c` (신규) | RO/RW connection pool (C 구현) |
| `lb_classify.c` (신규) | SQL 첫 토큰 RO/RW 분류 |

### 6.5 재활용 가능한 기존 코드

| 기존 파일 | 재활용 부분 |
|-----------|------------|
| `cas_protocol.h` | `CAS_FC_CHECK_CAS` 상수 (헬스체크용) |
| `broker_config.c` | 설정 파싱 패턴 |
| `shard_proxy_io.c` | TCP listener 초기화 패턴 참고 |

---

## 7. 설정 파일

### 7.1 형식 원칙

LB Proxy 설정은 기존 `[BrokerN]` 섹션에 통합된다.
`LB_PROXY = ON` 항목이 있을 때만 해당 Broker에서 LB Proxy가 활성화되며,
`LB_PROXY_` prefix 항목들은 `LB_PROXY = ON`일 때만 유효하다.

### 7.2 동작 모드

| `LB_PROXY` 값 | 동작 |
|--------------|------|
| `OFF` (기본값) | 기존 Broker 동작. `LB_PROXY_` 항목 전체 무시 |
| `ON` | 기존 Broker(`PORT`)와 LB Proxy(`LB_PROXY_PORT`) 동시 운영 |

### 7.3 설정 항목

```ini
[Broker1]
# ── 기존 Broker 설정 (변경 없음) ─────────────────────────────
PORT                              = 30000
MIN_NUM_APPL_SERVER               = 5
MAX_NUM_APPL_SERVER               = 40

# ── LB Proxy 확장 설정 ────────────────────────────────────────
LB_PROXY                          = ON    # OFF(기본): 기존 Broker만, ON: LB Proxy 추가

# 아래 항목들은 LB_PROXY=ON 일 때만 유효
LB_PROXY_PORT                     = 33100   # 클라이언트의 topology 요청 수신 포트
LB_PROXY_HEARTBEAT_INTERVAL       = 1000    # ms, 서버 alive 체크 주기
LB_PROXY_TTL                      = 30      # 초, 클라이언트 topology 캐시 유효 시간
LB_PROXY_RO_POOL_SIZE             = 10      # 드라이버 RO connection pool 권장 크기 (참고용)
LB_PROXY_RW_POOL_SIZE             = 5       # 드라이버 RW connection pool 권장 크기 (참고용)
LB_PROXY_RO_FALLBACK_TO_RW        = ON      # RO 서버 전멸 시 RW 서버로 fallback

# Slave DB 호스트들의 Broker IP:PORT (RO 쿼리 라우팅 대상)
LB_PROXY_RO_BROKER_LIST           = 192.168.1.11:30000,192.168.1.12:30000

# Master DB 호스트의 Broker IP:PORT (RW 쿼리 라우팅 대상)
LB_PROXY_RW_BROKER_LIST           = 192.168.1.10:30000
```

> **주의**: `LB_PROXY_RO_BROKER_LIST` / `LB_PROXY_RW_BROKER_LIST`의 포트는 각 DB 호스트에서 실행 중인 **Broker의 포트**다. CAS는 독립 TCP 포트가 없으므로 직접 접근 불가.

### 7.4 클라이언트(JDBC) 연결 예시

```java
// LB 모드: Proxy에서 서버 목록 받아 직접 라우팅
String url = "jdbc:cubrid:lb://proxy_host:33100/testdb";
Connection conn = DriverManager.getConnection(url, "dba", "");

// 이후 SQL 실행은 드라이버가 자동으로 RO/RW 판단 후 적절한 서버로 직접 전송
conn.createStatement().executeQuery("SELECT ...");  // RO 서버로
conn.createStatement().executeUpdate("INSERT ..."); // RW 서버로
```

---

## 8. 알려진 제한사항 및 향후 과제

| 항목 | 현황 | 향후 계획 |
|------|------|---------|
| 드라이버 수정 필요 | JDBC/CCI 모두 수정 | 초기 구현 필수 범위 |
| RO connection에서 DML (트랜잭션 내) | 에러 반환 | 트랜잭션 시작 전 RO/RW 힌트 API 제공 |
| `CALL` (Stored Procedure) 분류 | 항상 RW로 라우팅 | SP 메타데이터 기반 분류 |
| XA 트랜잭션 | 미지원 | 향후 추가 |
| Proxy 이중화 | 단일 Proxy SPOF | 다중 Proxy 지원, DNS 라운드로빈 활용 |
| 토폴로지 push 알림 | 클라이언트가 TTL 만료 후 pull | Proxy→클라이언트 push 방식으로 빠른 전파 |
| Python/PHP/Perl 드라이버 | 미반영 | JDBC/CCI 구현 후 포팅 |
| 모니터링/통계 | 미구현 | `cubrid lbproxy status` 명령 추가 |

---

## 9. 참고

### 9.1 관련 소스 파일

| 파일 | 내용 |
|------|------|
| `src/broker/cas_protocol.h` | `CAS_FC_CHECK_CAS` 등 프로토콜 상수 |
| `src/broker/broker_config.h/c` | 설정 파싱 구조 (T_BROKER_INFO, 키워드 테이블) |
| `src/broker/broker.c` | Broker 시작 루틴 |
| `src/broker/shard_proxy_io.c` | epoll TCP listener 패턴 참고 |
| `cubrid-jdbc/src/...` | JDBC 드라이버 구현 위치 |
| `cubrid-cci/src/...` | CCI 드라이버 구현 위치 |

### 9.2 용어 정리

| 용어 | 설명 |
|------|------|
| Topology Discovery | 클라이언트가 Proxy로부터 RO/RW 서버 목록을 조회하는 과정 |
| Server Registry | Proxy가 관리하는 RO/RW 서버 목록과 각 서버의 alive 상태 |
| TTL | 클라이언트가 서버 목록 캐시를 유효하게 사용할 수 있는 시간 |
| RO Pool | 드라이버 내 Slave DB 서버들에 대한 connection pool |
| RW Pool | 드라이버 내 Master DB 서버에 대한 connection pool |
| LB_PROXY_RO_BROKER_LIST | Slave DB 호스트들의 Broker IP:PORT 목록 (CAS IP 아님) |
| LB_PROXY_RW_BROKER_LIST | Master DB 호스트의 Broker IP:PORT (CAS IP 아님) |
