# CBRD-26937 병합 충돌 해결 결정 보고서

작성일: 2026-06-29

대상 병합:

- 현재 브랜치: `CBRD-26937-oos-bigone-error`
- 병합 대상: `origin/feat/oos`
- 병합 커밋: `5e1150749`

## 요약

`origin/feat/oos`를 병합하는 과정에서 다음 4개 파일에 충돌이 발생했다.

- `src/base/error_code.h`
- `msg/en_US.utf8/cubrid.msg`
- `msg/ko_KR.utf8/cubrid.msg`
- `unit_tests/oos/sql/CMakeLists.txt`

핵심 결정은 `origin/feat/oos`에 이미 추가된 에러 코드를 유지하고, CBRD-26937에서 추가한
`ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE`를 다음 빈 번호로 이동하는 것이었다.

## 충돌 원인

CBRD-26937 브랜치는 OOS + bigone 조합을 사용자 에러로 거부하기 위해 새 에러 코드를 추가했다.
이 브랜치가 갈라진 시점에는 `-1375`가 비어 있었기 때문에 다음과 같이 배정되어 있었다.

```c
#define ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE            -1375
#define ER_LAST_ERROR                               -1376
```

하지만 병합 대상인 `origin/feat/oos`에는 그 사이 다음 에러 코드가 먼저 추가되어 있었다.

```c
#define ER_VACUUM_MASTER_DAEMON_NOT_AVAILABLE       -1375
#define ER_HEAP_OOS_BAD_INLINE_HEADER               -1376
#define ER_LAST_ERROR                               -1377
```

따라서 CBRD-26937의 기존 번호 `-1375`를 그대로 유지하면 `origin/feat/oos`의
`ER_VACUUM_MASTER_DAEMON_NOT_AVAILABLE`와 번호가 중복된다.

## 해결 결정

최종 결정은 다음과 같다.

```c
#define ER_VACUUM_MASTER_DAEMON_NOT_AVAILABLE       -1375
#define ER_HEAP_OOS_BAD_INLINE_HEADER               -1376
#define ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE            -1377
#define ER_LAST_ERROR                               -1378
```

메시지 카탈로그도 동일한 번호 체계에 맞추었다.

- `1375`: Vacuum master daemon is not running
- `1376`: OOS inline header corruption internal error
- `1377`: OOS + bigone rejection user error
- `1378`: Last Error

`unit_tests/oos/sql/CMakeLists.txt` 충돌은 양쪽 변경이 모두 유효한 테스트 추가였기 때문에,
CBRD-26937의 `test_oos_sql_bigone`과 `origin/feat/oos`의 `test_oos_sql_eager_cleanup`,
`test_oos_sql_storage`를 모두 유지했다. 또한 `origin/feat/oos`가 추가한 `TIMEOUT 30` 설정도
유지했다.

## 결정 이유

### 1. 병합 대상 브랜치의 에러 번호를 우선 보존해야 한다

`origin/feat/oos`는 병합 대상 브랜치이며, 그 브랜치에 이미 존재하는 에러 번호는 이후 변경들이
기준으로 삼는 상태다. 병합 과정에서 대상 브랜치의 `-1375`, `-1376`을 밀어내면, 이미 병합된
코드와 메시지 카탈로그, 테스트, 문서의 의미가 바뀔 수 있다.

따라서 기존 대상 브랜치 번호를 보존하고, 현재 기능 브랜치에서 추가한 에러를 다음 빈 번호로
이동하는 방식이 가장 충돌 범위가 작다.

### 2. 숫자보다 symbolic error name이 안정적인 계약이다

코드와 테스트는 `ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE`라는 symbolic name을 사용한다. 이 이름이
유지되면 호출부의 의미는 변하지 않는다.

반대로 숫자 `-1375` 자체는 전역 에러 테이블 내 배정값이므로, 병합 대상 브랜치에 새 에러가
먼저 들어오면 이동될 수 있다. 이번 병합에서는 symbolic name을 유지하고 숫자만 재배정하는
것이 더 안전하다.

### 3. 중복 에러 번호는 런타임 메시지 해석을 깨뜨릴 수 있다

`error_code.h`의 에러 번호와 `cubrid.msg`의 메시지 번호는 일대일로 맞아야 한다. 같은 번호가
두 의미를 가지면, `er_set()`으로 설정한 에러가 잘못된 메시지로 표시되거나 메시지 카탈로그
조회가 모호해질 수 있다.

그래서 `ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE`를 `-1377`로 옮기고, 영어/한국어 메시지 카탈로그의
`1377`에 동일한 의미의 메시지를 배치했다.

### 4. OOS + bigone 동작 의미는 변경하지 않았다

CBRD-26937의 기능적 의미는 그대로 유지했다.

- OOS demotion 이후에도 레코드가 bigone threshold를 넘는 경우 거부한다.
- OOS 레코드를 쓰기 전에 사용자 에러를 반환한다.
- 일반 bigone 레코드는 영향을 받지 않는다.
- 4KB 이상이지만 bigone threshold 이하인 OOS 레코드는 허용한다.

변경된 것은 에러 번호 배정뿐이며, `heap_file.c`의 거부 조건과 테스트 의도는 유지되었다.

### 5. 테스트 등록 충돌은 additive merge가 맞다

`unit_tests/oos/sql/CMakeLists.txt`에서는 양쪽 브랜치가 서로 다른 테스트를 추가했다.

- CBRD-26937: `test_oos_sql_bigone`
- `origin/feat/oos`: `test_oos_sql_eager_cleanup`, `test_oos_sql_storage`

서로 배타적인 변경이 아니므로 어느 한쪽을 제거할 이유가 없었다. 따라서 세 테스트를 모두
등록했고, `origin/feat/oos`에서 추가한 `TIMEOUT 30`도 전체 OOS SQL 테스트 속성에 유지했다.

## 해결한 파일

| 파일 | 해결 내용 |
| --- | --- |
| `src/base/error_code.h` | upstream `-1375`, `-1376` 유지, CBRD-26937 에러를 `-1377`로 이동, `ER_LAST_ERROR`를 `-1378`로 갱신 |
| `msg/en_US.utf8/cubrid.msg` | `1377`에 OOS + bigone rejection 메시지 배치, `1378 Last Error`로 갱신 |
| `msg/ko_KR.utf8/cubrid.msg` | `1377`에 한국어 OOS + bigone rejection 메시지 배치, `1378 마지막 에러`로 갱신 |
| `unit_tests/oos/sql/CMakeLists.txt` | 양쪽 브랜치의 OOS SQL 테스트를 모두 유지하고 `TIMEOUT 30` 유지 |

## 검증 결과

병합 후 다음 검증을 수행했다.

```bash
just build
just ctest
```

결과:

- `just build`: 성공
- `just ctest`: 23/23 통과
- `test_oos_sql_bigone`: 통과

## 참고 사항

기존 OOS 컨텍스트 문서에는 CBRD-26937 에러가 `-1375`라고 적혀 있었지만, 최신
`origin/feat/oos`를 병합한 이후에는 `-1375`가 이미 다른 에러에 사용된다. 따라서 현재 병합
결과에서는 `ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE = -1377`이 올바른 배정이다.

향후에는 에러 번호 자체보다 symbolic name인 `ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE`를 기준으로
코드, 테스트, 문서를 작성하는 것이 안전하다.
