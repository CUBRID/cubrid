# QUERY_CACHE TTL 정책설계 구성자료

**작성일:** 2026-05-07
**버전:** CUBRID 11.4.5.1875-7f88a2a (TTL + CACHE SQL LOG 빌드)

---

## 1. 개요

### 배경
CUBRID QUERY_CACHE 힌트는 DML 커밋 시 캐시를 즉시 무효화하는 단일 정책만 지원.
DML이 빈번한 환경에서는 캐시 히트율이 급락하여 성능 향상 효과가 제한적.

### 목표
TTL(Time-To-Live) 파라미터를 추가하여, DML 커밋과 무관하게 지정 시간 동안 캐시를 유지하는 정책 제공.

### SQL 힌트 문법
```sql
-- 기존: DML 커밋 시 캐시 무효화
SELECT /*+ QUERY_CACHE */ ...

-- 신규: N초 동안 캐시 유지 (DML 무효화 무시)
SELECT /*+ QUERY_CACHE(N) */ ...
```

---

## 2. 아키텍처

### 캐시 동작 흐름

```
[클라이언트]                [CAS 브로커]              [CUBRID 서버]
    |                           |                         |
    |--- SQL 요청 ------------>|--- SQL 전달 ----------->|
    |                           |                         |
    |                           |    [캐시 조회]          |
    |                           |    히트? ──YES──> 캐시 결과 반환
    |                           |      |                  |  (is_result_cached=true)
    |                           |     NO                  |
    |                           |      |                  |
    |                           |    [SQL 실행]           |
    |                           |    결과 캐시 저장       |
    |                           |    결과 반환            |
    |                           |                         |
    |<-- 결과 ------------------|<-- 결과 + 캐시정보 ----|
    |                           |                         |
    |                  SQL LOG에 (CACHE) 마커 기록        |
```

### TTL 정책 분기

```
QUERY_CACHE 힌트 감지
    |
    +-- 파라미터 없음 (QUERY_CACHE)
    |       → cache_policy = 0
    |       → DML 커밋 시 xcache_invalidate_qcaches() 호출 → 캐시 삭제
    |
    +-- 파라미터 있음 (QUERY_CACHE(N))
            → cache_policy = 1, ttl_seconds = N
            → DML 커밋 시 무효화 시도
            → TTL 만료 전이면 캐시 유지 (무효화 건너뜀)
            → TTL 만료 후 다음 조회 시 캐시 갱신
```

---

## 3. 구현 상세

### 수정 파일

| 파일 | 수정 내용 |
|------|----------|
| `src/object/object_representation.c` | `or_pack_listid`/`or_unpack_listid`/`or_listid_length`에 `is_result_cached` 필드 추가 |

### 핵심 코드 변경 (object_representation.c)

#### or_pack_listid — 서버→CAS 캐시 히트 정보 전달
```c
OR_PUT_INT (ptr, listid->type_list.type_cnt);
ptr += OR_INT_SIZE;
OR_PUT_INT (ptr, listid->is_result_cached ? 1 : 0);  // 추가
ptr += OR_INT_SIZE;
```

#### or_unpack_listid — CAS에서 캐시 히트 정보 수신
```c
listid->type_list.type_cnt = OR_GET_INT (ptr);
ptr += OR_INT_SIZE;
listid->is_result_cached = (OR_GET_INT (ptr) != 0);  // 추가
ptr += OR_INT_SIZE;
```

#### or_listid_length — 프로토콜 길이 계산
```c
/* 9 fixed items (기존 8 + is_result_cached 1) */
length += OR_INT_SIZE * 9;
```

### 데이터 흐름

```
[서버] qmgr_execute_query()
  → cache hit → list_id->is_result_cached = true
  → or_pack_listid()  ← is_result_cached를 INT로 pack
  → 네트워크 전송

[CAS] or_unpack_listid()
  → is_result_cached 수신
  → execute_statement.c: statement->flag.use_query_cache = 1
  → db_vdb.c: db_get_cacheinfo() → srv_handle->use_query_cache = true
  → cas_function.c: SQL LOG에 " (CACHE)" 출력
```

### JDBC 호환성
`or_pack_listid`/`or_unpack_listid`는 서버↔CAS 간 통신에만 사용.
JDBC 드라이버는 CAS 프로토콜을 사용하므로 영향 없음.
단, 서버와 브로커는 반드시 같은 빌드로 배포해야 함.

---

## 4. 성능 테스트 결과

### 테스트 환경

| 항목 | 값 |
|------|-----|
| 서버 | Intel Xeon Silver 4216 @ 2.10GHz, 64 cores, 188GB RAM |
| DB | demodb (약 128MB) |
| Workers | 100 동시 연결 |
| 반복 | 워커당 100회 (총 70,000 쿼리/시나리오) |
| DML | INSERT 5개 테이블, 쿼리 테스트 전 먼저 시작 |
| 테스트 쿼리 | 7개 (3~4 Table JOIN, GROUP BY, CONNECT BY, REGEXP 등) |

### SQL별 실행시간 비교 (SQL LOG elapsed time, avg ms)

#### DML 1초 간격

| Query | NoHint | QUERY_CACHE | QC(2) TTL | QC(2)향상 |
|-------|-------:|----------:|------:|------:|
| Q1: 3-Table JOIN | 384.71 | 80.46 | **36.32** | **10.59x** |
| Q2: 4-Table JOIN | 1673.19 | 306.75 | **234.02** | **7.15x** |
| Q3: Medal Count | 1419.21 | 201.14 | **125.47** | **11.31x** |
| Q4: Stadium Avg | 55.93 | 4.12 | **3.74** | **14.95x** |
| Q5: CONNECT BY | 4.91 | 3.44 | 3.45 | 1.42x |
| Q6: Top 5 Events | 13.17 | 4.01 | 3.86 | 3.41x |
| Q7: REGEXP+CAST | 175.65 | 50.31 | **19.80** | **8.87x** |

#### DML 0.5초 간격 (캐시 무효화 2배 빈번)

| Query | NoHint | QUERY_CACHE | QC(2) TTL | QC(2)향상 |
|-------|-------:|----------:|------:|------:|
| Q1: 3-Table JOIN | 336.15 | 137.38 | **28.71** | **11.71x** |
| Q2: 4-Table JOIN | 1500.30 | **1577.21** | **194.95** | **7.70x** |
| Q3: Medal Count | 1145.84 | **1056.04** | **101.32** | **11.31x** |
| Q4: Stadium Avg | 40.48 | 7.53 | **3.59** | **11.28x** |
| Q5: CONNECT BY | 5.69 | 3.07 | 3.29 | 1.73x |
| Q6: Top 5 Events | 17.43 | 4.16 | 3.73 | 4.67x |
| Q7: REGEXP+CAST | 189.09 | 83.77 | **18.10** | **10.45x** |

### DML 빈도별 캐시 히트율 및 총 소요시간

| DML 간격 | 시나리오 | 소요시간 | 캐시 히트율 |
|---------|----------|------:|------:|
| 1초 | NoHint | 378s | 0% |
| 1초 | QUERY_CACHE | 71s | 88.0% |
| 1초 | QC(2) TTL | **49s** | **93.5%** |
| 0.5초 | NoHint | 329s | 0% |
| 0.5초 | QUERY_CACHE | **292s** | **57.6%** |
| 0.5초 | QC(2) TTL | **41s** | **94.5%** |

### 리소스 사용률 (DML 1초)

| 항목 | NoHint | QUERY_CACHE | QC(2) TTL |
|------|------:|------:|------:|
| CPU idle | 2.7% | 7.6% | **10.8%** |
| cub_server CPU | 4,926% | 5,291% | 5,031% |
| cub_server MEM | 1,713MB | 2,025MB | 2,061MB |
| cub_cas CPU | 65% | 438% | **596%** |

- 캐시 사용 시 메모리 +300MB, CPU idle 증가 (캐시 히트로 쿼리 실행 건너뜀)
- 64코어 서버 기준 cub_server CPU 4000~5000%는 약 8~9% 수준

---

## 5. 핵심 결론

### DML 빈도에 따른 정책 비교

| | QUERY_CACHE | QUERY_CACHE(N) TTL |
|---|---|---|
| DML 1초 | 히트율 88%, 71s | 히트율 93.5%, **49s** |
| DML 0.5초 | 히트율 **57.6%**, **292s** | 히트율 **94.5%**, **41s** |
| DML 영향 | **큰 영향** (히트율 급락) | **영향 없음** (TTL 보호) |

### TTL 코드의 기존 기능 영향
- NoHint 성능: 두 버전 동일 (오버헤드 없음)
- QUERY_CACHE 성능: 두 버전 동일 (TTL 코드가 비TTL 경로에 영향 없음)

### 권장 사용

| 시나리오 | 권장 힌트 | 이유 |
|----------|----------|------|
| 데이터 정합성 최우선 | `QUERY_CACHE` | DML 커밋 시 즉시 무효화 |
| 읽기 비율 높은 서비스 | `QUERY_CACHE(N)` | TTL N초간 캐시 유지 |
| DML 빈번한 서비스 | `QUERY_CACHE(2)` | 짧은 TTL로 stale 최소화 + 성능 향상 |
| 통계/리포트 | `QUERY_CACHE(60)` | 장기 TTL로 극대화 |

---

## 6. (CACHE) SQL LOG 마커

### 기능
캐시 히트 시 SQL LOG의 execute 라인에 `(CACHE)` 마커 표시.

### 동작 예시
```
-- 캐시 미스 (첫 실행)
execute 0 tuple 1 time 0.043

-- 캐시 히트
execute 0 tuple 1 time 0.000 (CACHE)
```

### 활용
- 운영 중 캐시 히트율 모니터링
- SQL LOG 분석으로 캐시 효과 측정
- grep "(CACHE)" 로 히트 건수 집계 가능
