# unloaddb VIEW `NA` 필드와 문자열 리터럴 비교 시 loaddb 오류 원인/수정 포인트

## 배경

구버전 `unloaddb`가 VIEW(`VCLASS`) 쿼리를 덤프할 때, 실제 컬럼 대신 `NA` 필드를 생성/사용하는 경우가 있다.
이 상태에서 `loaddb`가 스키마를 재적용할 때, `NA`가 포함된 비교식(특히 `START WITH`, `CONNECT BY`의 조건식)에서
문자열 리터럴과의 타입 강제 변환(coercion)이 실패할 수 있다.

재현 예:

```sql
CREATE VCLASS [v_info];

ALTER VCLASS ADD ATTRIBUTE (
        code VARCHAR(20),
        pcode VARCHAR(20)
);

ALTER VCLASS [v_info] ADD QUERY
SELECT NA,NA
FROM (
    SELECT NA, NA
    UNION
    SELECT NA, NA
    FROM [code_tbl]
) a (code, pcode)
START WITH a.pcode = '10000000'
CONNECT BY PRIOR a.code = a.pcode
ORDER SIBLINGS BY 1;
```

오류:

```text
ERROR: before '
 CONNECT BY PRIOR a.code = a.pcode
 ORDER SIBLINGS BY 1; '
Cannot coerce '10000000' to type unknown data type.
```

## 원인 분석

### 1) `NA`가 의미하는 타입 상태

- `NA`는 실제 스키마 타입이 확정되지 않은 placeholder 성격으로 파서 내부에서 `PT_TYPE_NA`로 다뤄진다.
- `PT_TYPE_NA`는 일반적인 스키마 타입(`VARCHAR`, `INT` 등)과 달리 coercion 대상 타입으로 바로 쓰기 어렵다.

### 2) 비교식 타입 결정 경로에서 발생한 문제

- 문제 구간은 `src/parser/type_checking.c`의 비교/범위 비교 연산 타입 보정 로직이다.
- 기존 로직은 **컬럼(NAME) vs 상수(VALUE)** 비교에서, 기본적으로 `arg1_eq_type = arg2_eq_type = arg1_type`(또는 반대축 `arg2_type`)으로 맞춘다.
- 이때 컬럼 타입이 `PT_TYPE_NA`이면, 상수 `'10000000'`도 결과적으로 `PT_TYPE_NA`에 맞춰 캐스팅하려고 시도한다.
- 이후 에러 메시지 생성 시 `PT_TYPE_NA`가 사용자 표시 타입으로 해석되지 못해 `unknown data type`로 노출된다.
  - 관련 함수: `src/parser/parse_tree_cl.c`의 `pt_show_type_enum()` (`PT_TYPE_NONE` 이하/비정상 범위를 `unknown data type`으로 표시)

결과적으로 `"문자열 리터럴" -> "NA(미정 타입)"` 강제 변환이 실패하여
`Cannot coerce '10000000' to type unknown data type.`가 발생한다.

## 수정 포인트

핵심은 **`PT_TYPE_NA`를 coercion 목표 타입으로 고정하지 않도록** 비교식 타입 보정 규칙을 조정하는 것이다.

### A. 비교식에서 `NA` 컬럼 vs 상수 처리 보강 (`src/parser/type_checking.c`)

비교/범위 비교 타입 보정 시 다음 분기를 추가/적용:

- `NAME` 쪽 타입이 `PT_TYPE_NA`이고,
- 공통 추론 타입(`common_type`)이 `PT_TYPE_NA`/`PT_TYPE_NULL`이 아닌 경우,
- `arg*_eq_type`을 `NA`가 아닌 `common_type`으로 설정

의도:

- `a.pcode = '10000000'` 같은 식에서, `'10000000'`의 문자열 타입 추론 결과를 활용해 coercion을 정상화한다.
- 즉 "NA에 맞추는" 대신 "추론 가능한 실제 타입에 맞춘다".

### B. 문자열 비교에 대한 보완 분기 (`src/parser/type_checking.c`)

아래 케이스를 명시적으로 허용:

- `arg1_type == PT_TYPE_NA` + `arg2_type`이 문자열 계열 + `arg1`이 NAME 노드
- 대칭 케이스(`arg2_type == PT_TYPE_NA`)

의도:

- `NA` 필드가 문자열 상수/표현식과 비교될 때 불필요한 타입 실패를 방지한다.

### C. 함수 인자 타입검사에서의 과도한 우회 제거 (`src/parser/func_type.cpp`)

초기 대응으로 `PT_TYPE_NA` 인자를 광범위하게 통과시키는 우회가 들어갔으나,
이 방식은 타입 검사 품질을 낮출 수 있어 이후 정리되었다.

- 방향: `NA`를 무조건 skip 하지 않고,
- 실제 오류 경로(비교식 coercion)를 `type_checking.c`에서 정밀 보정

## 관련 커밋 흐름 (CBRD-26845 브랜치)

- `2363c53a9`: `NA` 필드 타입체크 우회 성격의 1차 대응
- `9107c8da6`: 우회 대신 비교식 coercion 로직 보정(핵심 수정)
- `cceca04c6`: `NA` 타입검사 skip 제거 정리

최종적으로는 **"전역적인 NA 우회"가 아니라, "문제가 발생한 coercion 포인트의 국소 수정"**으로 수렴했다.

## 기대 효과

- 구버전 `unloaddb`가 생성한 `NA` 기반 VIEW 쿼리도 `loaddb` 시점에서 문자열 리터럴 비교를 정상 처리
- `START WITH`/`CONNECT BY` 포함 VIEW 정의 로딩 실패 감소
- 타입검사 자체를 약화시키지 않고, 문제 케이스만 정확히 수용

## 참고 파일

- `src/parser/type_checking.c`
- `src/parser/func_type.cpp`
- `src/parser/parse_tree_cl.c`
