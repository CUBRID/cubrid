# PostgreSQL Histogram Optimizer Study Notes

이 문서는 JOB 벤치마크 결과를 보면서 CUBRID `src/optimizer`의 선택도, 카디널리티, 비용 로직을 보정할 때 참고하기 위해 PostgreSQL의 히스토그램 기반 추정 방식을 정리한 노트이다.

참고 기준:

- PostgreSQL 문서: [Statistics Used by the Planner](https://www.postgresql.org/docs/current/planner-stats.html)
- PostgreSQL 문서: [Row Estimation Examples](https://www.postgresql.org/docs/current/row-estimation-examples.html)
- PostgreSQL 문서: [`pg_stats`](https://www.postgresql.org/docs/current/view-pg-stats.html)
- PostgreSQL 소스: `src/backend/utils/adt/selfuncs.c`
- PostgreSQL 소스: `src/backend/optimizer/path/clausesel.c`
- PostgreSQL 소스: `src/backend/optimizer/path/costsize.c`

## 요약

PostgreSQL은 히스토그램만으로 선택도를 계산하지 않는다. 기본 단위는 다음 통계들의 조합이다.

- `reltuples`, `relpages`: table/index base cardinality와 page 수
- `null_frac`: NULL 비율
- `n_distinct`: distinct 값 개수 또는 row 수 대비 distinct 비율
- `most_common_vals`, `most_common_freqs`: MCV와 그 빈도
- `histogram_bounds`: MCV와 NULL을 제외한 나머지 값들의 등빈도 bucket 경계
- `correlation`: heap 물리 순서와 컬럼 논리 순서의 상관도
- extended statistics: multi-column dependencies, ndistinct, MCV

핵심 철학은 다음과 같다.

1. 자주 나오는 값은 MCV로 직접 맞춘다.
2. MCV가 아닌 나머지 값의 분포만 히스토그램으로 추정한다.
3. 히스토그램이 작거나 신뢰하기 어려우면 default selectivity와 섞거나 clamp한다.
4. 여러 조건은 기본적으로 독립이라고 보고 곱하지만, extended statistics가 있으면 이를 보정한다.
5. 조인 cardinality는 특정 join method 비용보다 먼저 estimate된다.

## `pg_stats`의 히스토그램 의미

`histogram_bounds`는 컬럼 값을 같은 row 수를 갖는 bucket들로 나누는 경계값 배열이다. 중요한 점은 `most_common_vals`에 포함된 값은 히스토그램에서 제외된다는 것이다.

예를 들어 `histogram_bounds`가 11개 값이면 bucket은 10개이다. `x < const` 같은 range 조건은 const가 어느 bucket에 들어가는지 찾고, 이전 bucket 수와 현재 bucket 안의 선형 보간 비율을 더해 선택도를 구한다.

공식 문서의 단순 예시는 다음 형태다.

```text
selectivity = (full_buckets + fraction_inside_current_bucket) / num_buckets
rows = rel_cardinality * selectivity
```

이 모델은 bucket 내부 값을 균등하다고 가정한다.

## Equality 선택도

PostgreSQL의 equality restriction은 `eqsel` 계열로 들어가고, `selfuncs.c`의 `var_eq_const`가 핵심이다.

상수 equality의 흐름은 다음과 같다.

1. 상수가 NULL이면 strict operator 가정하에 선택도 0.
2. unique index 또는 DISTINCT/GROUP BY 정보로 unique가 확인되면 `1 / reltuples`.
3. 상수가 MCV 목록에 있으면 해당 `most_common_freqs` 값을 그대로 사용한다.
4. MCV에 없으면 나머지 non-null, non-MCV population을 나머지 distinct 값에 균등 분배한다고 본다.

비MCV equality의 기본식은 다음에 가깝다.

```text
selectivity = (1 - sum(mcv_freqs) - null_frac) / (n_distinct - num_mcv)
```

추가로 비MCV 선택도가 가장 낮은 MCV frequency보다 커지지 않도록 제한한다. 이 점은 skew가 있는 컬럼에서 non-MCV 값을 과대평가하지 않기 위한 guardrail이다.

## Inequality와 Range 선택도

`<`, `<=`, `>`, `>=` 같은 scalar inequality는 `scalarltsel`, `scalarlesel`, `scalargtsel`, `scalargesel` wrapper를 거쳐 `scalarineqsel`로 들어간다.

range estimate의 핵심은 `ineq_histogram_selectivity`이다.

흐름은 다음과 같다.

1. MCV 목록을 먼저 평가해 조건을 만족하는 MCV frequency를 합산한다.
2. 히스토그램에서 const가 들어갈 bucket을 binary search로 찾는다.
3. bucket 내부 위치는 `convert_to_scalar`로 숫자 scale로 바꾼 뒤 선형 보간한다.
4. histogram selectivity는 MCV와 NULL을 제외한 population에 대한 비율로 계산된다.
5. 최종 선택도는 `mcv_selectivity + hist_selectivity * (1 - null_frac - sum_mcv)`로 합친다.

공식 예시의 결합식은 다음과 같다.

```text
selectivity = mcv_selectivity + histogram_selectivity * histogram_fraction
histogram_fraction = 1 - null_frac - sum(mcv_freqs)
```

히스토그램 endpoint가 오래됐을 수 있으므로, PostgreSQL은 첫/마지막 bucket 근처에서는 실제 현재 min/max를 얻으려 시도한다. 그럴 수 없으면 극단적으로 작은 값이나 1에 가까운 값을 그대로 믿지 않고 histogram resolution 기반 cutoff로 clamp한다.

## Generic Histogram Selectivity

`histogram_selectivity`는 정렬 의미를 특별히 모르는 operator에도 쓸 수 있는 일반 루틴이다.

이 함수는 히스토그램 bucket을 정렬된 범위로 해석하기보다, 히스토그램 값들을 representative sample처럼 보고 operator를 직접 적용한다.

```text
result = matching_histogram_values / usable_histogram_values
```

특징은 다음과 같다.

- 작은 히스토그램은 신뢰하지 않고 `-1`을 반환한다.
- 보통 `min_hist_size = 10`, `n_skip = 1` 같은 방식으로 양 끝 outlier를 제외한다.
- 히스토그램 크기가 10 이상 100 미만이면 default selectivity와 가중 평균으로 섞는다.
- 결과가 너무 0 또는 1에 가까우면 clamp한다.

이 방식은 CUBRID에서 특정 operator나 LIKE 변형처럼 정밀한 모델이 없지만 histogram sample을 이용하고 싶을 때 참고할 만하다.

## Join Equality 선택도

PostgreSQL의 equality join은 `eqjoinsel` 계열로 들어간다. 일반 inner join에서는 `eqjoinsel_inner`가 핵심이다.

두 join column 모두 MCV가 있으면 다음을 한다.

1. 양쪽 MCV 목록을 실제 equality operator로 비교해 match pair를 찾는다.
2. match된 MCV들의 frequency product를 known selectivity로 더한다.
3. unmatched MCV와 non-MCV population은 `n_distinct` 기반으로 균등 분포 가정을 적용한다.
4. relation 1 관점과 relation 2 관점의 estimate를 각각 계산한 뒤 작은 값을 사용한다.

양쪽 MCV가 충분하지 않으면 기본식은 다음과 같다.

```text
join_selectivity =
  (1 - null_frac1) * (1 - null_frac2) / max(ndistinct1, ndistinct2)
```

문서 예시도 join rows는 다음 순서로 계산된다고 설명한다.

```text
rows = outer_cardinality * inner_cardinality * join_selectivity
```

중요한 점은 join cardinality estimate가 특정 nested-loop/index-scan plan의 `outer rows * inner lookup rows`에서 나온 것이 아니라, join relation 크기로 먼저 계산된다는 것이다. 이후 join method 비용 계산이 그 cardinality를 사용한다.

## Correlation의 역할

`pg_stats.correlation`은 컬럼 값의 논리 순서와 heap 물리 순서의 상관도이다. 값이 `1` 또는 `-1`에 가까우면 index scan이 heap을 비교적 순차적으로 읽을 가능성이 높다고 보고 random page cost를 낮게 본다.

즉 correlation은 선택도를 바꾸는 통계라기보다, 선택도가 같을 때 index scan의 실제 IO cost를 보정하는 통계에 가깝다.

CUBRID에서 JOB 결과상 index scan 선택 자체는 맞지만 실제 실행 시간이 크게 빗나가는 경우, histogram/selectivity뿐 아니라 heap fetch locality 또는 clustering/correlation에 해당하는 비용 보정이 필요한지 확인할 수 있다.

## Extended Statistics

PostgreSQL은 단일 컬럼 통계만으로는 여러 조건의 상관관계를 알 수 없다고 보고, `CREATE STATISTICS`로 필요한 컬럼 조합에 대해 extended statistics를 수집한다.

지원 축은 다음과 같다.

- `dependencies`: 함수 종속성. `a = const AND b = const`에서 b가 a에 의해 결정되면 독립 곱셈으로 과소추정하지 않게 한다.
- `ndistinct`: 여러 컬럼 조합의 distinct 개수. GROUP BY, DISTINCT, join size estimate에 도움을 준다.
- `mcv`: 여러 컬럼 조합의 most common values. 상관된 multi-column predicate의 실제 빈도를 직접 반영한다.

JOB 벤치마크처럼 join graph가 크고 데이터 분포가 skewed한 경우, 단일 컬럼 히스토그램만으로는 충분하지 않을 수 있다. 특히 `(movie_id, company_type_id)` 같은 조합이 자주 같이 쓰인다면 multi-column MCV나 dependency 성격의 보정이 필요할 수 있다.

## CUBRID에 적용할 때 볼 포인트

CUBRID `src/optimizer`에서 PG 방식과 비교하며 볼 위치는 다음이다.

- `query_planner_selectivity.c`: 단일 predicate 선택도 계산
- `query_graph.c`: term 분석, index 가능 여부, selectivity 저장
- `query_planner.c`: join cardinality 계산, MCV/hot-key guard, `hit_prob`, join method 비용
- `query_planner_constants.h`: 선택도/비용 guardrail 상수

향후 JOB 결과를 볼 때는 다음 순서로 의심하면 좋다.

1. 단일 테이블 range/equality row estimate가 틀렸는지 본다.
2. MCV에 해당하는 hot key가 일반 NDV 공식으로 희석됐는지 본다.
3. MCV와 histogram population을 분리하지 않아 range estimate가 왜곡됐는지 본다.
4. 여러 predicate를 독립 곱셈해 과소추정했는지 본다.
5. join equality에서 `1 / max(ndv1, ndv2)`류의 기본식이 skew를 놓쳤는지 본다.
6. 선택도는 맞는데 index scan 비용이 틀렸다면 correlation/heap locality에 해당하는 보정이 필요한지 본다.
7. LIMIT가 있는 nested-loop에서 `hit_prob` 또는 fanout 추정이 실제보다 지나치게 낙관적인지 본다.

## CUBRID 보정 아이디어 후보

다음은 바로 구현하자는 뜻이 아니라, 벤치마크 결과와 실제 plan dump를 보고 검토할 수 있는 후보들이다.

- Equality selectivity에서 MCV hit와 non-MCV uniform estimate를 명확히 분리한다.
- Range selectivity에서 MCV contribution과 histogram contribution을 합산하는 구조를 검토한다.
- 히스토그램 bucket 내부는 선형 보간하되, bucket 경계/작은 histogram에는 clamp를 둔다.
- Join equality는 양쪽 MCV가 있으면 MCV pair matching 기반으로 skew를 반영하고, 없으면 `1 / max(ndv1, ndv2)` 하한 모델을 쓴다.
- Hot-key join guard는 현재처럼 selectivity를 사후 상향하는 방식보다, MCV pair 또는 large-side MCV frequency를 더 직접적으로 반영할 수 있는지 본다.
- 여러 조건이 같은 컬럼군에 반복될 때 독립 곱셈으로 과소추정하지 않도록 extended-statistics류의 경량 대안을 검토한다.
- Index scan 비용에는 선택도뿐 아니라 heap fetch locality 또는 clustering 정도를 반영할 수 있는지 본다.

## 기억할 결론

PostgreSQL의 히스토그램 최적화는 “bucket으로 range selectivity를 구한다”가 전부가 아니다. 실제 플래너 정확도는 MCV를 먼저 정확히 반영하고, 히스토그램을 non-MCV population에만 적용하며, NULL/NDV/correlation/extended statistics와 결합하는 데서 나온다.

CUBRID 보정도 단순히 histogram selectivity 공식을 바꾸기보다, “MCV 분리”, “join skew”, “조건 독립성”, “index lookup locality” 중 어느 축이 JOB 오차를 만들었는지 먼저 분해해서 봐야 한다.
