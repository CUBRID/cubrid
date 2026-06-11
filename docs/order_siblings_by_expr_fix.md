# ORDER SIBLINGS BY 표현식 처리 개선 정리

## 배경

`CONNECT BY` 계층 질의에서 `ORDER SIBLINGS BY`는 형제 노드 정렬을 수행한다.
기존 구현은 정렬 키를 `SELECT` 컬럼 위치와 단순 매핑하는 경향이 강해, 일반 `ORDER BY`에서 허용되는 일부 케이스가 `ORDER SIBLINGS BY`에서는 에러로 처리되었다.

이번 브랜치에서는 `ORDER BY expr`와 동일한 사용성을 목표로, `ORDER SIBLINGS BY`에서도 표현식/컬럼위치 기반 정렬을 정상 처리하도록 보완했다.

---

## 수정된 오류 유형 (2가지)

### 1) `ORDER SIBLINGS BY n`에서 n번째 `SELECT` 항목이 식(expr)인 경우

예시:

```sql
SELECT code || 'yy', pcode
FROM code_tbl a
START WITH a.pcode = '10000000'
CONNECT BY PRIOR a.code = a.pcode
ORDER SIBLINGS BY 1;
```

- 기존: `1`이 식 컬럼(`code || 'yy'`)을 가리킬 때 정렬 키 생성/매핑 과정에서 실패 가능.
- 개선: `pt_check_order_by()` 결과(`pos_descr.pos_no`)를 기준으로 실제 `SELECT` 항목(식 포함)을 정렬 키로 해석.

### 2) `ORDER SIBLINGS BY <임의 표현식>`인 경우

예시:

```sql
SELECT code || 'yy', pcode
FROM code_tbl a
START WITH a.pcode = '10000000'
CONNECT BY PRIOR a.code = a.pcode
ORDER SIBLINGS BY substring(code, 3, 2);
```

- 기존: `SELECT` 목록과 직접 매칭되지 않는 식은 정렬 키로 처리하지 못해 에러 가능.
- 개선: 식 자체를 `regu_variable`로 생성하고, 필요 시 CONNECT BY 튜플에 trailing 사용자 컬럼으로 materialize하여 정렬 키로 사용.

---

## 핵심 수정 코드

주요 변경 파일: `src/parser/xasl_generation.c`

### A. 정렬 키 해석

- `pt_order_siblings_sort_key_expr()`
  - `ORDER SIBLINGS BY n`이면 `pos_descr.pos_no`를 따라 `SELECT` 항목을 직접 찾음.
  - 그렇지 않으면 sort spec expression 자체를 사용.
  - 즉, ordinal 기반과 expression 기반을 동일한 경로로 수렴.

### B. 정렬 키 도메인 결정

- `pt_order_siblings_sort_key_domain()`
  - 정렬 키 도메인을 `regu -> orderby pos_descr.dom -> sort_spec pos_descr.dom` 순서로 안전하게 선택.
  - 도메인 계산 로직 분산을 제거하고 일관성 확보.

### C. 기존 CONNECT BY 컬럼 재사용 vs 식 materialize

- `pt_order_siblings_base_val_pos()`
  - 정렬 키가 base CONNECT BY 컬럼 참조(`TYPE_CONSTANT`)면 기존 val_list 슬롯 index를 재사용.
- `pt_to_connect_by_extend_for_order_siblings()`
  - 재사용 불가(복합 식)인 경우:
    - `pt_alloc_order_siblings_val_slot()`로 val_list 슬롯 추가
    - `pt_append_pos_regu_to_list()`로 `regu_list_rest/prior_regu_list_rest/after_cb_regu_list_rest` 갱신
    - CONNECT BY outptr/prior_outptr에 정렬 키 컬럼 삽입
    - `orderby->pos_descr.pos_no`를 새 위치로 remap

### D. 보조 정리(컴팩트화)

- `pt_qproc_val_list_tail()` 추가로 val_list tail 탐색 중복 제거
- pos regu append 동작을 공통 헬퍼(`pt_append_pos_regu_to_list`)로 통일
- 조건/도메인 처리 분기를 헬퍼화하여 가독성 및 유지보수성 개선

---

## 동작 결과

다음 두 유형 모두 정상 처리:

1. `ORDER SIBLINGS BY n` (n번째 `SELECT` 항목이 식)
2. `ORDER SIBLINGS BY <expression>` (예: `substring(...)`)

결과적으로 계층 질의에서도 `ORDER BY expr`와 유사한 사용자 경험을 제공한다.

