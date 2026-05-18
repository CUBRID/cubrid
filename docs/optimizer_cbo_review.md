# CBO 최적화 리뷰 문서

이 문서는 이번 PR에서 변경한 CUBRID optimizer 보정 로직을 리뷰하기 위한 설명 자료이다. 단순히 어떤 함수가 추가됐는지보다, 왜 이런 케이스가 발생하는지, CBO의 기존 수식이 어디서 깨지는지, MCV/cardinality/cost guard가 각각 무엇을 제한하는지에 초점을 둔다.

## 1. 이번 변경의 핵심 문제

CBO는 여러 후보 plan에 대해 예상 cardinality와 cost를 계산한 뒤 가장 싼 plan을 고른다.

단순화하면 nested-loop join의 비용은 다음과 같이 볼 수 있다.

```text
plan_cost
  = outer_cost
  + outer_cardinality * inner_lookup_cost
  + predicate_eval_cost
```

문제는 실제 실행 시간이 느린 plan이 CBO에서는 더 싸게 보이는 cost inversion이다.

```text
estimated_cost(bad_plan) < estimated_cost(good_plan)
actual_time(bad_plan)    > actual_time(good_plan)
```

이 PR에서 주로 다룬 케이스는 다음과 같다.

- 작은 dimension/filter 결과가 큰 fact/bridge table로 조인된다.
- equality join을 평균 selectivity로만 계산해서 fanout을 과소평가한다.
- `movie_id` 같은 bridge key에서 global MCV 비율은 낮지만 절대 fanout은 큰 경우가 있다.
- index lookup 이후 residual filter가 적용되어 많은 row를 읽고 나서야 줄어든다.
- `LIKE`, `OR`, string predicate의 CPU 비용이 scalar equality와 비슷하게 취급된다.

즉, 단일 문제라기보다 다음 세 영역의 추정 오차가 겹친다.

```text
1. cardinality error
2. predicate CPU cost error
3. repeated lookup/read-before-filter cost error
```

이번 변경은 이 세 영역을 분리해서 보정한다.

## 2. 기존 평균 selectivity 모델의 한계

기존 equality join cardinality는 대략 다음 형태에 가깝다.

```text
join_cardinality = left_card * right_card * join_selectivity
```

일반적으로 equality join selectivity는 NDV 기반 평균으로 추정된다.

```text
join_selectivity ~= 1 / max(NDV(left), NDV(right))
```

이 모델은 데이터가 균등할 때는 합리적이다. 그러나 실제 JOB 데이터는 균등하지 않다.

예를 들어 작은 prefix가 큰 fact table의 특정 key를 찌르는 경우를 보자.

```text
small_card = 10
large_card = 4,000,000
avg_frequency = 1 / 1,000,000 = 0.000001
```

평균 모델은 다음처럼 본다.

```text
avg_fanout = large_card * avg_frequency
           = 4,000,000 * 0.000001
           = 4

avg_join_card = small_card * avg_fanout
              = 10 * 4
              = 40
```

하지만 실제로는 특정 key가 평균보다 훨씬 많은 row를 가질 수 있다.

```text
max_mcv_frequency = 0.002

risk_fanout = large_card * max_mcv_frequency
            = 4,000,000 * 0.002
            = 8,000

risk_join_card = small_card * risk_fanout
               = 10 * 8,000
               = 80,000
```

평균 모델은 40 row를 예상하지만, MCV 기반 risk 모델은 80,000 row를 예상한다. 이런 차이가 누적되면 optimizer는 bridge/fact table을 너무 일찍 타는 plan을 고른다.

## 3. MCV 정보를 join term에 저장한 이유

이번 변경에서 equality join term에 다음 값을 저장한다.

```text
head_mcv_max_frequency
tail_mcv_max_frequency
```

이 값은 `query_graph.c`에서 equality join term을 만들 때 histogram에서 가져온다.

```text
histogram_get_max_mcv_frequency(lhs)
histogram_get_max_mcv_frequency(rhs)
```

중요한 점은 head/tail 방향을 맞춰 저장한다는 것이다. 이후 planner에서 join을 계산할 때 "작은 쪽이 head인지 tail인지"에 따라 반대편 큰 쪽의 MCV frequency를 사용해야 한다.

```text
if small side is head:
  effective_mcv = tail_mcv_max_frequency
else:
  effective_mcv = head_mcv_max_frequency
```

의미는 다음과 같다.

```text
작은 쪽의 한 row 또는 작은 prefix가
큰 쪽의 가장 흔한 key를 만났을 때
최대로 어느 정도 fanout이 생길 수 있는가?
```

이 값은 정확한 value-specific fanout은 아니다. 하지만 평균 selectivity 하나만 쓰는 것보다는 skew risk를 표현할 수 있다.

## 4. Small Side 판정

MCV guard는 모든 join에 적용하지 않는다. "작은 쪽이 큰 쪽을 찌르는" 모양에서만 의미가 있다.

현재 small side 판정은 두 조건 중 하나다.

```text
cardinality <= QO_MCV_GUARD_SMALL_CARD_ABS
```

또는

```text
cardinality / total_rows <= QO_MCV_GUARD_SMALL_CARD_RATIO
```

현재 값:

```text
QO_MCV_GUARD_SMALL_CARD_ABS   = 20.0
QO_MCV_GUARD_SMALL_CARD_RATIO = 0.001
```

즉, 절대적으로 row 수가 작거나 전체 row 대비 충분히 작으면 small side로 본다.

이 guard는 정확히 한쪽만 small일 때만 적용된다.

```text
head_small != tail_small
```

왜 이런 제한이 필요한가?

- small-large join: dimension/fact 또는 filtered-prefix/fact 형태라 fanout risk가 중요하다.
- large-large join: broad join이라 MCV guard를 적용하면 너무 공격적일 수 있다.
- small-small join: 양쪽 모두 작으면 큰 fanout risk보다 이미 join 자체가 작다.

그래서 MCV guard는 정확히 한쪽만 small인 케이스로 제한한다.

## 5. MCV Hot-Key Cardinality Guard

### 적용 조건

`qo_apply_mcv_hotkey_join_guard()`는 다음 조건을 만족할 때만 term selectivity를 조정한다.

```text
1. term이 equality join이어야 한다.
2. head_info/tail_info가 있어야 한다.
3. term_sel < QO_MCV_GUARD_MAX_BASE_SELECTIVITY
4. 정확히 한쪽만 small side여야 한다.
5. effective_mcv 또는 risk_fanout 신호가 있어야 한다.
```

여기서 broad join 제한은 다음이다.

```text
QO_MCV_GUARD_MAX_BASE_SELECTIVITY = 0.01
```

즉, 기존 selectivity가 이미 1% 이상이면 hot-key guard 대상이 아니다.

이 제한의 의미:

```text
이미 넓은 join은 MCV 보정 대상이 아니라
일반 join cardinality/cost 모델에 맡긴다.
```

### 수식

기본 입력:

```text
term_sel         = 기존 equality join selectivity
base_cardinality = join 전 base cardinality
small_card       = 작은 쪽 cardinality
large_card       = 큰 쪽 cardinality
effective_mcv    = 큰 쪽 max MCV frequency
```

fanout risk:

```text
risk_fanout = large_card * effective_mcv
```

예상 위험 cardinality:

```text
risk_card = small_card * risk_fanout
```

이를 selectivity로 변환:

```text
risk_sel = risk_card / base_cardinality
```

그 다음 기존 selectivity와 비교해 조정한다.

```text
term_sel := bounded(risk_sel)
```

## 6. Hot MCV와 Cold Fanout을 나눈 이유

기존 hot-key 기준은 frequency 자체가 큰 경우다.

```text
effective_mcv >= QO_MCV_GUARD_MIN_FREQUENCY
```

현재 값:

```text
QO_MCV_GUARD_MIN_FREQUENCY = 0.1
```

즉, 큰 쪽 table에서 특정 값 하나가 10% 이상이면 명확한 hot key다.

하지만 JOB의 bridge/fact join에서는 다른 문제가 있다. `movie_id` 같은 컬럼은 global frequency가 10%까지 크지 않다. 그래도 table이 워낙 크면 절대 fanout이 크다.

예:

```text
effective_mcv = 0.003
large_card    = 4,000,000

risk_fanout = 4,000,000 * 0.003
            = 12,000
```

frequency는 0.3%라 hot key는 아니지만, 한 key가 12,000 row를 만들 수 있다. join order 관점에서는 충분히 위험하다.

그래서 cold fanout 조건을 추가했다.

```text
if effective_mcv < QO_MCV_GUARD_MIN_FREQUENCY:
  guard 허용 조건 = risk_fanout >= QO_MCV_GUARD_MIN_RISK_FANOUT
```

현재 값:

```text
QO_MCV_GUARD_MIN_RISK_FANOUT = 10.0
```

의미:

```text
비율은 낮아도 절대 fanout이 10 row 이상이면
작은 prefix 입장에서는 의미 있는 fanout risk로 본다.
```

## 7. MCV Guard의 상한과 하한

MCV guard는 너무 강하게 움직이면 regression이 생긴다. 그래서 upward/downward 양쪽에 제한을 둔다.

### Upward cap

일반 hot-key:

```text
risk_sel <= term_sel * QO_MCV_GUARD_MAX_SELECTIVITY_MULTIPLIER
```

현재 값:

```text
QO_MCV_GUARD_MAX_SELECTIVITY_MULTIPLIER = 25.0
```

cold fanout:

```text
risk_sel <= term_sel * QO_MCV_GUARD_COLD_FANOUT_SELECTIVITY_MULTIPLIER
```

현재 값:

```text
QO_MCV_GUARD_COLD_FANOUT_SELECTIVITY_MULTIPLIER = 100.0
```

cold fanout에 더 큰 cap을 둔 이유:

```text
term_sel 자체가 매우 작기 때문이다.
예를 들어 term_sel = 1e-6이면 25배는 2.5e-5에 불과하다.
bridge/fact fanout을 뒤집기에는 부족할 수 있다.
```

그래서 cold fanout은 별도 cap을 둔다. 단, 여전히 small side/equality/narrow join/risk_fanout 조건을 모두 통과해야 하므로 무제한 적용은 아니다.

### Downward damping

MCV 기반 risk가 기존 term selectivity보다 작게 나올 수도 있다.

```text
risk_sel < term_sel
```

이때도 너무 많이 낮추지 않도록 제한한다.

```text
risk_sel = max(risk_sel,
               term_sel * QO_MCV_GUARD_MIN_SELECTIVITY_MULTIPLIER)
```

현재 값:

```text
QO_MCV_GUARD_MIN_SELECTIVITY_MULTIPLIER = 0.5
```

의미:

```text
한 번의 MCV guard 적용으로 selectivity를 기존의 절반 이하로 낮추지 않는다.
```

## 8. MCV Guard 전체 흐름

요약하면 다음 의사코드다.

```text
if not equality_join:
  return term_sel

if term_sel >= max_base_selectivity:
  return term_sel

if not exactly_one_side_small:
  return term_sel

effective_mcv = max_mcv_frequency(on large side)
risk_fanout = large_card * effective_mcv

if effective_mcv < min_frequency
   and risk_fanout < min_risk_fanout:
  return term_sel

risk_card = small_card * risk_fanout
if risk_card <= 1:
  return term_sel

risk_sel = risk_card / base_cardinality

if effective_mcv < min_frequency:
  max_multiplier = cold_fanout_multiplier
else:
  max_multiplier = hot_mcv_multiplier

risk_sel = min(risk_sel, term_sel * max_multiplier)

if risk_sel < term_sel:
  risk_sel = max(risk_sel, term_sel * min_multiplier)

return min(risk_sel, 1.0)
```

이 흐름에서 각각의 제한 의미는 다음과 같다.

```text
equality only:
  MCV frequency 기반 fanout은 equality join에서만 의미가 명확하다.

max_base_selectivity:
  이미 넓은 join은 건드리지 않는다.

exactly one small side:
  small-prefix -> large-side fanout 문제만 다룬다.

min_frequency:
  명확한 hot key 기준.

min_risk_fanout:
  frequency는 낮지만 절대 fanout이 큰 cold fanout 기준.

max_multiplier:
  upward 보정 상한.

min_multiplier:
  downward 보정 하한.
```

## 9. Residual Predicate Cost를 분리한 이유

Cardinality는 "몇 row가 살아남는가"이고, cost는 "그 row를 얻기 위해 얼마나 일하는가"이다.

예를 들어:

```sql
note LIKE '%abc%' OR note LIKE '%def%'
```

이 predicate의 selectivity가 낮더라도, DB는 row를 읽고 문자열 비교를 수행해야 한다. 즉 output row 수만 보면 CPU work를 과소평가한다.

그래서 predicate cost weight를 별도로 둔다.

```text
filter_cpu_cost =
  rows_evaluated * QO_CPU_WEIGHT * predicate_weight
```

## 10. Predicate Weight 수식

현재 주요 weight:

```text
QO_COST_WEIGHT_PRED_DEFAULT     = 1.00
QO_COST_WEIGHT_NUMERIC_COMPARE  = 1.00
QO_COST_WEIGHT_STRING_EQUAL     = 3.00
QO_COST_WEIGHT_STRING_RANGE     = 3.25
QO_COST_WEIGHT_LIKE_PREFIX      = 3.50
QO_COST_WEIGHT_LIKE_CONTAINS    = 10.00
QO_COST_WEIGHT_LIKE_COMPLEX     = 14.00
```

`qo_get_term_cost_weight()`는 `or_next` chain을 순회한다.

```text
term_weight = sum(weight(expr) for expr in or_next chain)
```

의미:

```text
a LIKE '%x%' OR a LIKE '%y%'
```

는 단일 `LIKE`보다 비싸다. 이전처럼 OR term 하나를 동일 비용으로 보면 복잡한 string residual filter plan이 과소평가된다.

## 11. Sequential Scan Filter CPU 보정

Sequential scan은 table 전체 row를 읽고 predicate를 평가한다.

기본 scan cost:

```text
base_cpu = table_cardinality * QO_CPU_WEIGHT
base_io  = table_pages
```

여기에 residual filter CPU를 추가한다.

```text
scan_rows = max(1, QO_NODE_NCARD(node))
residual_weight = sum(weights of node sargs)

filter_cpu =
  scan_rows
  * QO_CPU_WEIGHT
  * residual_weight
  * QO_SSCAN_FILTER_CPU_FACTOR
```

현재 값:

```text
QO_SSCAN_FILTER_CPU_FACTOR = 5.00
```

왜 필요한가?

```text
COUNT(*)처럼 row만 세는 scan과
LIKE/OR/NOT LIKE를 모든 row에 평가하는 scan은 CPU 시간이 다르다.
```

이 보정은 I/O를 올리는 것이 아니라 CPU 평가 비용만 올린다.

제한:

```text
sscan의 residual filter cost에만 적용된다.
index scan heap lookup cost는 건드리지 않는다.
```

## 12. Index Scan Predicate CPU 보정

Index scan에는 세 종류의 predicate가 있다.

```text
range terms:
  index range를 결정하는 조건

key-filter terms:
  index layer에서 추가 확인되는 조건

data-filter terms:
  row fetch 후 residual로 확인되는 조건
```

`qo_apply_scan_term_cpu_overhead()`는 다음 비용을 더한다.

```text
scan_term_cpu =
  scan_rows * QO_CPU_WEIGHT
  * (1.2 * range_weight
     + 1.0 * key_filter_weight
     + 0.8 * data_filter_weight)
```

왜 coefficient가 다른가?

```text
range term:
  index traversal/range 판단에 직접 관여하므로 약간 더 높게 본다.

key filter:
  index 쪽에서 평가되는 일반 필터 비용.

data filter:
  residual filter이지만 다른 비용 항목에도 일부 반영될 수 있으므로 낮게 둔다.
```

제한:

```text
이 보정은 CPU cost만 추가한다.
cardinality나 selectivity는 변경하지 않는다.
```

## 13. NL Join Term CPU 보정

Nested-loop join은 outer row마다 inner lookup을 반복한다. 이때 join predicate도 반복 평가된다.

추가 비용:

```text
join_term_cpu =
  guessed_result_cardinality
  * QO_CPU_WEIGHT
  * 0.5
  * sum(join_term_weight)
```

join term weight:

```text
QO_COST_WEIGHT_JOIN_DEFAULT      = 1.00
QO_COST_WEIGHT_JOIN_STRING_EQUAL = 1.10
QO_COST_WEIGHT_JOIN_STRING_RANGE = 1.25
```

왜 필요한가?

```text
join output cardinality만으로는
반복적으로 join term을 비교하는 CPU 비용이 충분히 표현되지 않는다.
```

제한:

```text
join term bitset이 유효할 때만 적용한다.
temporary inner-plan search 중 비어 있는 bitset에는 적용하지 않는다.
```

## 14. Delayed Sarg Lookup Penalty

### 문제

Nested-loop에서 inner가 index scan이고 residual filter가 남아 있으면 다음 형태가 된다.

```text
for each outer row:
  index lookup inner
  fetch candidate rows
  apply residual filter
```

output cardinality는 작아도 lookup과 residual evaluation은 반복된다.

### 수식

```text
delayed_component =
  residual_filter_weight
  * log10(max(10, guessed_outer_cardinality))
  * QO_DELAYED_SARG_PENALTY_FACTOR
```

상한:

```text
delayed_component <= QO_DELAYED_SARG_PENALTY_MAX
```

현재 값:

```text
QO_DELAYED_SARG_OUTER_CARD_THRESHOLD = 1.0
QO_DELAYED_SARG_PENALTY_FACTOR       = 0.25
QO_DELAYED_SARG_PENALTY_MAX          = 2.0
```

제한:

```text
NL join이 아니면 적용하지 않는다.
inner가 index scan이 아니면 적용하지 않는다.
inner residual filter weight가 0이면 적용하지 않는다.
outer cardinality가 작고 bridge fanout 신호도 없으면 적용하지 않는다.
```

## 15. Read-Before-Filter Penalty

### 문제

어떤 inner plan은 많은 row를 읽은 뒤 residual filter로 줄인다.

이를 다음 ratio로 본다.

```text
read_before_filter_ratio =
  inner_total_rows / inner_cardinality
```

예:

```text
inner_total_rows = 1,000,000
inner_cardinality = 10,000

ratio = 100
```

이는 output 10,000 row만 보면 실제 read work를 과소평가한다는 뜻이다.

### 수식

```text
read_before_filter_component =
  residual_filter_weight
  * log10(read_before_filter_ratio)
  * QO_READ_BEFORE_FILTER_PENALTY_FACTOR
```

현재 값:

```text
QO_READ_BEFORE_FILTER_RATIO_FLOOR    = 1.5
QO_READ_BEFORE_FILTER_PENALTY_FACTOR = 0.20
QO_READ_BEFORE_FILTER_PENALTY_MAX    = 1.0
```

제한:

```text
ratio <= 1.5이면 적용하지 않는다.
component는 1.0까지로 제한한다.
```

## 16. Bridge Fanout Penalty

### 문제

Bridge table을 통해 여러 fact table로 이어지는 join은 fanout이 연쇄적으로 커질 수 있다.

예:

```text
k -> movie_keyword -> complete_cast -> movie_info -> cast_info
```

각 단계가 평균 selectivity로 작아 보이면 CBO는 bridge를 일찍 타는 plan을 싸게 본다. 그러나 실제 실행에서는 중간 row 수가 크게 터질 수 있다.

### 수식

```text
bridge_fanout_component =
  log10(read_before_filter_ratio)
  * join_term_weight
  * QO_BRIDGE_FANOUT_PENALTY_FACTOR
```

현재 값:

```text
QO_BRIDGE_FANOUT_RATIO_FLOOR     = 4.0
QO_BRIDGE_FANOUT_PENALTY_FACTOR  = 0.15
QO_BRIDGE_FANOUT_PENALTY_MAX     = 2.0
```

제한:

```text
ratio <= 4.0이면 bridge fanout penalty를 적용하지 않는다.
component는 2.0까지로 제한한다.
```

## 17. Lookup Penalty의 최종 적용

Delayed sarg, read-before-filter, bridge fanout component를 합친 뒤 상한을 둔다.

```text
capped_component =
  min(max_allowed,
      delayed_component
      + read_before_filter_component
      + bridge_fanout_component)
```

최종 multiplier:

```text
lookup_penalty = 1.0 + capped_component
```

적용 위치:

```text
inner_cpu_cost *= lookup_penalty
inner_io_cost  *= lookup_penalty
```

의미:

```text
cardinality는 그대로 두고,
해당 plan shape의 반복 lookup 비용만 더 비싸게 본다.
```

이것이 cardinality guard와 다른 점이다.

## 18. Skew Uncertainty Penalty

### 문제

작은 dimension 값이 정확히 어떤 value인지 planner가 join 단계에서 직접 전파하지 못하면, value-specific fanout을 알 수 없다.

그러나 큰 쪽 FK-like 컬럼이 skew되어 있다면 평균 fanout만 보는 것은 위험하다.

### 수식

```text
avg_freq = QO_TERM_SELECTIVITY(term)
large_mcv_freq = max MCV frequency on large side

skew_ratio = large_mcv_freq / avg_freq
```

`skew_ratio`가 크다는 것은:

```text
가장 흔한 값의 fanout이 평균 fanout보다 훨씬 크다.
```

component:

```text
if skew_ratio > QO_SKEW_UNCERTAINTY_RATIO_FLOOR:
  component += log10(skew_ratio) * QO_SKEW_UNCERTAINTY_PENALTY_FACTOR
```

현재 값:

```text
QO_SKEW_UNCERTAINTY_RATIO_FLOOR    = 4.0
QO_SKEW_UNCERTAINTY_PENALTY_FACTOR = 0.20
QO_SKEW_UNCERTAINTY_PENALTY_MAX    = 1.0
```

제한:

```text
첫 dimension-to-fact lookup에는 적용하지 않는다.
outer prefix node count <= 1이면 return 1.0
```

왜 제한하는가?

```text
JOB 1a 같은 쿼리는 selective dimension에서 fact로 들어가는 시작 plan이 좋을 수 있다.
첫 lookup부터 uncertainty penalty를 주면 좋은 시작점까지 막을 수 있다.
```

따라서 이 penalty는 skewed FK-like 결과가 이미 join prefix에 들어온 뒤의 반복 lookup risk를 모델링한다.

## 19. 세 가지 보정 채널의 차이

이번 변경은 일부러 세 채널을 분리한다.

### Cardinality channel

대표 함수:

```text
qo_apply_mcv_hotkey_join_guard()
```

변경하는 것:

```text
join selectivity
join cardinality
downstream prefix cardinality
```

사용 이유:

```text
평균 selectivity가 fanout을 잘못 보는 경우.
```

### CPU cost channel

대표 함수:

```text
qo_apply_scan_term_cpu_overhead()
qo_get_nljoin_term_cpu_overhead()
QO_SSCAN_FILTER_CPU_FACTOR
```

변경하는 것:

```text
variable_cpu_cost
```

사용 이유:

```text
row 수는 맞더라도 predicate/join term 평가 비용이 과소평가되는 경우.
```

### Lookup risk channel

대표 함수:

```text
qo_get_delayed_sarg_lookup_penalty()
qo_get_skew_uncertainty_lookup_penalty()
```

변경하는 것:

```text
repeated inner lookup CPU/IO cost
```

사용 이유:

```text
NL join 구조상 많은 lookup을 한 뒤 filter가 늦게 적용되는 경우.
```

이 세 채널을 분리한 이유는 regression 분석을 쉽게 하기 위해서다.

```text
cardinality가 틀린 것인가?
CPU cost가 틀린 것인가?
반복 lookup shape가 위험한 것인가?
```

각 문제를 다른 수식과 다른 guard로 처리한다.

## 20. 1a/1c: `top 250 rank` 값 전파를 쿼리로 확인하는 방법

1a/1c의 핵심은 `it.info = 'top 250 rank'` 자체의 선택도가 아니라, 이 조건으로 정해지는 `info_type.id` 값이 `movie_info_idx.info_type_id`에서 얼마나 자주 등장하는지이다.

쿼리에는 다음 조건이 있다.

```sql
it.info = 'top 250 rank'
AND it.id = mi_idx.info_type_id
```

논리적으로는 다음 전파를 기대할 수 있다.

```text
it.info = 'top 250 rank'
  -> it.id = X
  -> mi_idx.info_type_id = X
  -> frequency(movie_info_idx.info_type_id = X)
```

하지만 일반적인 optimizer는 planning 중에 `it.info = 'top 250 rank'`를 실제 row lookup으로 실행해서 `it.id = X`를 literal로 치환하지 않는다. 따라서 CUBRID는 현재 이 값을 정확히 알기 어렵고, PostgreSQL도 원래 join 형태에서는 이 sparse fanout을 정확히 맞히지 못할 수 있다.

실제로 값을 확인하려면 먼저 dimension 값을 조회한다.

```sql
SELECT id, info
FROM info_type
WHERE info = 'top 250 rank';
```

예를 들어 결과가 다음과 같다고 하자.

```text
id = X
info = 'top 250 rank'
```

그 다음 fact-side frequency를 직접 확인한다.

```sql
SELECT COUNT(*) AS top250_mi_idx_rows
FROM movie_info_idx
WHERE info_type_id = X;
```

원 쿼리 형태와 같은 join으로도 확인할 수 있다.

```sql
SELECT COUNT(*) AS top250_mi_idx_rows
FROM info_type it
JOIN movie_info_idx mi_idx
  ON mi_idx.info_type_id = it.id
WHERE it.info = 'top 250 rank';
```

전체 rows와 평균 fanout을 비교하려면 다음처럼 본다.

```sql
SELECT
  COUNT(*) AS total_rows,
  COUNT(DISTINCT info_type_id) AS ndv_info_type_id,
  COUNT(*)::double precision / COUNT(DISTINCT info_type_id) AS avg_rows_per_value
FROM movie_info_idx;
```

그리고 `top 250 rank`의 실제 비율은 다음과 같이 계산한다.

```sql
WITH total AS (
  SELECT COUNT(*) AS total_rows
  FROM movie_info_idx
),
top250 AS (
  SELECT COUNT(*) AS value_rows
  FROM info_type it
  JOIN movie_info_idx mi_idx
    ON mi_idx.info_type_id = it.id
  WHERE it.info = 'top 250 rank'
)
SELECT
  top250.value_rows,
  total.total_rows,
  top250.value_rows::double precision / total.total_rows AS value_frequency
FROM total, top250;
```

이 값이 평균 `1 / NDV(info_type_id)`보다 훨씬 작으면 `top 250 rank`는 fact table에서 sparse value이다.

예상되는 비교는 다음 형태이다.

```text
평균 모델:
  selectivity ~= 1 / NDV(info_type_id)

값별 모델:
  selectivity ~= count(movie_info_idx.info_type_id = X) / count(movie_info_idx)
```

1a/1c에서 필요한 것은 후자이다.

PostgreSQL에서 두 형태의 plan estimate 차이를 확인하려면 다음 두 쿼리를 비교한다.

첫 번째는 원래 join 형태이다.

```sql
EXPLAIN
SELECT *
FROM info_type it
JOIN movie_info_idx mi_idx
  ON mi_idx.info_type_id = it.id
WHERE it.info = 'top 250 rank';
```

두 번째는 `id` 값을 직접 literal로 넣은 형태이다.

```sql
EXPLAIN
SELECT *
FROM movie_info_idx
WHERE info_type_id = X;
```

만약 두 번째 쿼리의 estimated rows가 실제 `top 250 rank` rows에 훨씬 가깝다면, PostgreSQL은 `movie_info_idx.info_type_id = X`처럼 값이 직접 주어진 경우에는 컬럼 MCV/frequency 통계를 더 잘 활용한다는 뜻이다.

반대로 첫 번째 join 형태의 estimated rows가 여전히 평균에 가깝다면, PostgreSQL도 `it.info = 'top 250 rank' -> it.id = X` 전파를 planning 단계에서 정확한 literal 치환으로 처리하지 못한다는 뜻이다.

### PostgreSQL이 이 over-estimation을 막지 않아도 빠를 수 있는 이유

PostgreSQL이 `top 250 rank`의 sparse fanout을 정확히 맞혀서 빠른 것은 아니다. 핵심은 cardinality estimate가 틀려도 실행 비용을 덜 망가뜨리는 access path와 join method가 있다는 점이다.

대표적으로 bitmap scan은 index hit를 바로 heap random lookup으로 하나씩 수행하지 않고, 먼저 bitmap으로 모은 뒤 heap page 단위로 접근한다.

```text
Bitmap Index Scan
  -> matching TID를 bitmap으로 수집
Bitmap Heap Scan
  -> heap page를 묶어서 방문
  -> page 안의 matching row 확인
```

이 구조의 의미는 다음과 같다.

```text
일반 index nested-loop lookup:
  outer row마다 inner index lookup
  lookup 결과마다 heap 접근이 반복될 수 있음

bitmap scan:
  여러 index hit를 먼저 모음
  같은 heap page에 있는 row들을 한 번에 처리
  random I/O와 반복 lookup 비용이 줄어듦
```

따라서 PostgreSQL은 `movie_info_idx.info_type_id = X`의 rows를 조금 크게 추정하더라도, 그 path가 곧바로 많은 random heap lookup으로 폭발하지 않는다. estimate가 다소 부정확해도 bitmap heap scan의 page grouping이 비용을 완충한다.

또 다른 차이는 join method 선택지이다.

```text
PostgreSQL:
  nested loop
  hash join
  merge join
  bitmap heap scan
  bitmap index combination

CUBRID 현재 문제 구간:
  nested-loop/index lookup shape의 영향이 더 큼
  많은 row를 읽고 나서 residual filter를 적용하는 plan이 싸게 보일 수 있음
```

즉 PostgreSQL은 sparse value를 정확히 알아서가 아니라, 다음과 같은 이유로 좋은 plan을 고를 수 있다.

```text
1. bitmap scan이 index hit를 page 단위 접근으로 바꿔 random lookup 비용을 낮춘다.
2. hash join 같은 plan shape가 큰 중간 결과에 대해 NL 반복 lookup보다 안정적이다.
3. 여러 access path의 cost 비교에서 full scan, bitmap scan, hash join이 NL index chain의 대안이 된다.
4. 그래서 cold value의 cardinality over-estimation이 있어도 실행 plan이 크게 망가지지 않을 수 있다.
```

이 관점에서 CUBRID가 `top 250 rank` 같은 coldpick을 cardinality에서 억지로 낮춰 잡는 것은 좋은 일반해가 아니다.

```text
bitmap/page-grouped access path가 있다면:
  coldpick cardinality를 굳이 낙관적으로 보정하지 않아도 됨
  잘못 커진 estimate가 access path에서 완충될 수 있음

bitmap/page-grouped access path가 약하다면:
  coldpick over-estimation 자체보다
  반복 lookup, read-before-filter, residual predicate cost가 plan 선택을 왜곡할 수 있음
```

따라서 현재 PR에서 더 안전한 방향은 다음과 같다.

```text
hot value:
  max MCV guard로 under-estimation을 방어

cold value:
  값별 frequency를 모르면 cardinality를 낮추는 보정을 하지 않음
  대신 cost-side guard와 access path 개선 방향을 검토
```

따라서 1a/1c에 대한 결론은 다음과 같다.

```text
정확히 필요한 정보:
  frequency(movie_info_idx.info_type_id = id('top 250 rank'))

현재 CUBRID가 쉽게 아는 정보:
  selectivity(info_type.info = 'top 250 rank')
  average selectivity(info_type.id = movie_info_idx.info_type_id)
  max MCV frequency(movie_info_idx.info_type_id)

부족한 정보:
  dimension predicate로 유도된 id 값의 fact-side frequency
```

이 때문에 `top 250 rank` 같은 cold/sparse value를 cardinality에서 직접 낮춰 잡는 보정은 위험하다. 같은 구조에서 `votes` 같은 hot value가 들어오면 반대로 평균보다 훨씬 클 수 있기 때문이다.

따라서 현재 PR에서는 다음 원칙이 더 안전하다.

```text
hotpick:
  max MCV/fanout guard로 과소추정 방어

coldpick:
  value-specific frequency 전파 없이는 cardinality를 낙관적으로 낮추지 않음
  필요하면 cost-side uncertainty 또는 access path 개선으로 다룸
```

## 21. B: `movie_id` bridge overlap/fanout 추정 부족

해당되는 JOB 케이스는 주로 30b, 11c, 16c이다.

이 문제는 단일 dimension value의 sparse/hot fanout 문제와 다르다. 여기서는 `movie_id`를 공유하는 bridge/fact table들이 여러 번 이어지면서, 테이블 간 overlap과 fanout이 평균 selectivity보다 크게 나타난다.

예를 들어 다음과 같은 plan shape가 문제를 만든다.

```text
small filter
  -> movie_keyword
  -> complete_cast
  -> cast_info
  -> movie_info
```

CBO의 평균 모델은 각 equality join을 대략 다음처럼 본다.

```text
join_selectivity ~= 1 / max(NDV(left.movie_id), NDV(right.movie_id))
```

이 모델은 `movie_id` 값들이 균등하게 퍼져 있고, bridge table 간 overlap이 독립적일 때는 합리적이다. 그러나 JOB 데이터에서는 인기 영화, 많이 연결된 영화, 여러 bridge table에 동시에 많이 등장하는 영화가 존재한다.

실제 문제는 다음 형태이다.

```text
평균 모델:
  movie_keyword에서 줄어든 row가 complete_cast에서도 평균 fanout만 만든다고 봄
  complete_cast에서 나온 row가 cast_info/movie_info에서도 평균 fanout만 만든다고 봄

실제 실행:
  같은 movie_id 집합이 여러 bridge/fact table에서 겹침
  bridge-to-bridge overlap이 평균 독립 가정보다 큼
  중간 cardinality가 누적해서 커짐
```

따라서 이 케이스는 단순히 특정 predicate 하나의 selectivity를 고치는 문제가 아니다. `movie_id`라는 join key를 통해 여러 table이 연결될 때, prefix가 만드는 실제 movie set과 다음 bridge table의 overlap을 알아야 한다.

현재 CUBRID가 쉽게 아는 정보는 다음 정도이다.

```text
각 table의 cardinality
각 movie_id 컬럼의 NDV
각 movie_id 컬럼의 max MCV frequency
join term의 평균 selectivity
```

하지만 정확히 필요한 정보는 다음에 가깝다.

```text
prefix가 만든 movie_id 집합
  -> 다음 bridge/fact table에서 그 movie_id 집합이 만드는 row 수
```

즉 다음과 같은 set-level overlap 통계가 필요하다.

```text
frequency(next_table.movie_id IN prefix_movie_id_set)
```

현재 PR은 이 값을 직접 계산하지 않는다. 대신 평균 모델이 bridge/fact fanout을 너무 싸게 보는 문제를 다음 guard로 완화한다.

```text
1. MCV hot-key cardinality guard
   - 큰 쪽 movie_id 컬럼의 max MCV frequency로 hot fanout 과소추정을 방어

2. cold fanout extension
   - MCV 비율은 낮아도 large_card * mcv_frequency의 절대 fanout이 크면 위험 신호로 봄

3. bridge fanout/read-before-filter penalty
   - 많은 row를 읽은 뒤 filter가 늦게 적용되는 NL/index lookup shape를 cost에서 더 비싸게 봄

4. NL join fanout expansion overhead
   - 작은 outer prefix가 inner index lookup 뒤 큰 output으로 확장되는 plan에 CPU overhead를 추가
```

중요한 점은 이 보정들이 `movie_id`라는 이름을 직접 인식하는 rule이 아니라는 것이다. 조건은 다음처럼 CBO 지표로만 표현된다.

```text
small side 여부
large side cardinality
join selectivity
max MCV frequency
estimated output/input fanout ratio
read-before-filter ratio
```

따라서 30b, 11c, 16c의 해석은 다음과 같다.

```text
원인:
  bridge-to-bridge/fact overlap을 평균 선택도로 과소추정
  small bridge/filter를 늦게 적용
  중간 결과 fanout이 실제보다 작게 보임

현재 보정:
  hot/fanout guard로 cardinality 과소추정을 일부 방어
  delayed lookup/read-before-filter/bridge fanout penalty로 위험한 plan shape를 비싸게 봄

남은 한계:
  prefix movie_id set과 다음 table의 실제 overlap 통계는 아직 없음
```

## 22. C: contains `LIKE` 적용 위치 문제

해당되는 JOB 케이스는 주로 6d, 9a, 9b, 19a이다.

이 문제는 `LIKE '%...%'` predicate가 매우 선택적이지만, 일반 B-tree index의 시작 조건으로 쓰기 어렵다는 데서 시작한다. prefix `LIKE 'abc%'`와 달리 contains `LIKE '%abc%'`는 앞부분이 wildcard라서 index range scan start가 되기 어렵다.

문제 shape는 다음과 같다.

```text
좋은 방향:
  name/note table을 먼저 scan
  contains LIKE로 강하게 줄임
  줄어든 row로 join 시작

나쁜 방향:
  다른 index join chain을 먼저 탐
  많은 PK/index lookup을 수행
  마지막에 LIKE residual filter로 버림
```

CBO가 나쁜 방향을 고르는 이유는 두 가지가 겹친다.

첫째, contains `LIKE`는 indexable start predicate가 아니므로 시작점 후보에서 불리하다.

```text
n.name LIKE '%Downey%Robert%'
mc.note LIKE '%(Japan)%'
```

이런 조건은 실제로 매우 선택적일 수 있지만, index range로 바로 좁히기 어렵다. 따라서 full scan start를 해야 하는데, full scan은 기본 cost가 커 보인다.

둘째, 늦게 적용되는 residual `LIKE`의 CPU/lookup 비용이 작게 보이면, 많은 row를 읽고 나중에 버리는 plan이 싸게 평가된다.

```text
estimated:
  PK lookup chain cost가 싸게 보임
  residual LIKE evaluation cost가 작게 보임

actual:
  많은 row에 대해 contains LIKE를 반복 평가
  PK/index lookup 후 대부분 버림
  CPU와 random lookup 비용이 커짐
```

현재 PR의 관련 보정은 다음이다.

```text
1. LIKE cost weight
   - prefix LIKE보다 contains/complex LIKE를 더 비싸게 봄

2. OR chain predicate weight
   - OR 내부 predicate 비용을 합산하여 복잡한 string filter를 싸게 보지 않음

3. sequential scan filter CPU factor
   - full scan에서 residual filter를 평가하는 CPU 비용을 반영

4. index scan predicate CPU overhead
   - index range/key-filter/data-filter 단계의 predicate 평가 비용을 반영

5. NL join term CPU overhead
   - join 중 반복 평가되는 predicate 비용을 반영

6. delayed sarg/read-before-filter penalty
   - filter가 늦게 적용되어 많은 row를 읽은 뒤 버리는 shape를 비싸게 봄
```

다만 `LIKE` 케이스에서 중요한 균형이 있다.

```text
full scan start를 너무 싸게 보면:
  모든 LIKE 쿼리가 full scan으로 기울 수 있음

full scan start를 너무 비싸게 보면:
  6d, 9a, 9b, 19a처럼 선택적인 contains LIKE 시작점도 회피함
```

그래서 이번 PR은 contains `LIKE`의 selectivity를 무리하게 더 낮추는 방향보다, 실제 실행에서 빠지는 비용을 cost channel에 분리해서 반영한다.

```text
cardinality:
  LIKE가 얼마나 줄이는지는 기존 selectivity 모델을 크게 흔들지 않음

CPU cost:
  contains/complex LIKE 평가 비용을 더 크게 봄

lookup cost:
  늦게 적용되는 residual LIKE로 인해 read-before-filter가 큰 plan을 비싸게 봄
```

따라서 6d, 9a, 9b, 19a의 해석은 다음과 같다.

```text
원인:
  선택적인 contains LIKE가 index start로 쓰이지 못함
  또는 full scan start가 유리한데 CBO가 회피함
  LIKE가 늦게 PK lookup residual로 적용되어 많은 row를 읽고 버림

현재 보정:
  contains/complex LIKE CPU weight 증가
  residual predicate evaluation cost 반영
  delayed sarg/read-before-filter penalty로 늦은 filter 적용 plan을 비싸게 봄

남은 한계:
  contains LIKE를 빠르게 시작할 수 있는 전용 access path는 아직 없음
  예를 들면 trigram/inverted index/bitmap-like access path가 있으면 cardinality 보정보다 안정적으로 해결 가능
```

## 23. 리뷰 시 확인할 포인트

리뷰어는 다음 질문으로 코드를 보면 된다.

```text
1. 이 변경은 cardinality를 바꾸는가, cost만 바꾸는가?
2. 적용 조건이 너무 넓지 않은가?
3. equality join에만 적용되어야 하는 보정이 다른 predicate에 적용되지 않는가?
4. small side 판정이 broad join을 막고 있는가?
5. upward/downward 보정에 cap이 있는가?
6. 첫 selective dimension lookup을 막지 않는가?
7. residual filter 비용이 selectivity와 섞이지 않는가?
8. query/table 이름에 의존하지 않는가?
```

이번 변경의 방향은 rule-based optimizer가 아니라 CBO 모델 보강이다. 특정 JOB query를 직접 인식하지 않고, cardinality, selectivity, MCV frequency, fanout, predicate weight, lookup shape로 plan cost를 조정한다.

## 24. 아직 남은 한계

이번 변경은 value-specific fanout을 정확히 계산하지 않는다.

예를 들어:

```sql
it.info = 'votes'
it.id = mi_idx.info_type_id
```

에서 `'votes'`라는 값이 fact table에서 얼마나 많은 fanout을 만드는지 정확히 전파하지 않는다.

현재는 다음 근사로 방어한다.

```text
max MCV frequency
cold fanout risk
skew uncertainty
bounded penalty
```

더 이상적인 후속 작업은 다음이다.

```text
known dimension value -> FK column value-specific frequency lookup
```

즉, dimension filter가 단일 값으로 확정된 경우 fact-side histogram/MCV에서 해당 value의 frequency를 직접 찾는 것이다. 현재 PR은 그 전 단계로, 평균 selectivity 하나만 쓰던 모델을 MCV/fanout/cost guard로 보강한 것이다.

# Optimizer CBO Review Notes

This document reviews the optimizer changes in this branch from the perspective of CBO behavior. It focuses on the local changes made for selectivity, cardinality, and cost estimation, and explains which guard rails limit each optimization.

## 1. What The CBO Is Trying To Compare

The optimizer compares alternative plans using estimated cardinality and cost. A simplified nested-loop plan comparison looks like this:

```text
plan_cost =
  outer_cost
  + outer_cardinality * inner_lookup_cost
  + predicate_evaluation_cost
```

The main failure mode addressed here is cost inversion:

```text
estimated_cost(plan A) < estimated_cost(plan B)
actual_time(plan A)    > actual_time(plan B)
```

The problematic plans usually have one or more of these patterns:

- A small filtered prefix joins into a large fact or bridge table.
- Average join selectivity hides hot-key or cold-fanout risk.
- A plan reads many rows through an index before residual predicates reduce the result.
- String predicates, especially `LIKE` and `OR` chains, are treated too close to cheap equality predicates.

The changes in this branch do not force a specific join order. They modify the cost and selectivity model so that risky plans become naturally more expensive during CBO comparison.

## 2. Changed Components

### Planner Constant Split

The new `query_planner_constants.h` collects model constants that were previously embedded in planner logic or introduced during this work. This makes tuning explicit and reviewable.

Important constant groups:

```text
predicate weights
sequential scan residual filter factor
MCV/hot-key join cardinality guard
delayed residual lookup penalty
read-before-filter and bridge fanout penalty
skew uncertainty penalty
default selectivities
```

The intent is to keep the formulas stable while allowing the guard rails to be calibrated.

### Selectivity Refactoring

Selectivity logic was split into `query_planner_selectivity.c` and shared declarations in `query_planner_internal.h`.

This reduces the size of `query_planner.c` and separates two responsibilities:

- `query_planner_selectivity.c`: estimate predicate selectivity.
- `query_planner.c`: enumerate plans, compute operator costs, and compare alternatives.

The refactor is structural. The CBO changes below are the behavioral parts.

## 3. Predicate Cost Weights

### Problem

The old CPU model treated predicate evaluation too uniformly. This is not accurate for string filters:

```text
numeric equality < string equality < LIKE prefix < LIKE contains/complex
```

A full scan with residual `LIKE` predicates can spend much more CPU per row than a simple equality filter.

### Model

Each term receives a relative evaluation weight:

```text
w(default)          = 1.00
w(numeric compare)  = 1.00
w(string equality)  = 3.00
w(string range)     = 3.25
w(LIKE prefix)      = 3.50
w(LIKE contains)    = 10.00
w(LIKE complex)     = 14.00
```

For `OR` chains, `qo_get_term_cost_weight()` walks `or_next` and sums the weight of each expression:

```text
term_weight = sum(w(expr_i) for expr_i in OR-chain)
```

This matters for predicates like:

```sql
title LIKE '%Freddy%' OR title LIKE '%Jason%' OR title LIKE 'Saw%'
```

The optimizer now sees that this is more expensive than a single scalar comparison.

### What Limits It

The weights affect CPU cost only. They do not change selectivity by themselves.

This is important because:

- Predicate cost should affect "how expensive it is to evaluate rows".
- Predicate selectivity should affect "how many rows survive".

Keeping them separate prevents the model from using CPU cost as a hidden cardinality correction.

## 4. Sequential Scan Residual Filter CPU

### Problem

Sequential scan cost previously approximated base row access:

```text
sscan_cpu = table_rows * QO_CPU_WEIGHT
sscan_io  = table_pages
```

But for filtered full scans, the engine evaluates residual predicates for every scanned row. The output cardinality may be small, but evaluation work happens before the filter discards rows.

### Model

In `qo_sscan_cost()`:

```text
scan_rows = max(1, table_cardinality)
residual_weight = sum(predicate_weights for node sargs)

sscan_filter_cpu =
  scan_rows
  * QO_CPU_WEIGHT
  * residual_weight
  * QO_SSCAN_FILTER_CPU_FACTOR
```

Current factor:

```text
QO_SSCAN_FILTER_CPU_FACTOR = 5.00
```

### What Limits It

This factor applies only to residual filter CPU in sequential scan. It does not multiply heap page I/O. That keeps the adjustment focused on predicate evaluation rather than making all table scans globally more expensive.

The assignment:

```text
planp->info->scan_rows = max(1, QO_NODE_NCARD(node))
```

is kept as the "rows evaluated by this scan" value. For sequential scan, this is table cardinality, not filtered output cardinality.

## 5. Index Scan Predicate CPU Overhead

### Problem

Index scan plans can contain different predicate classes:

- range terms used for the index range
- key-filter terms checked by the index layer
- data-filter terms left as residual sargs

The previous cost model underrepresented the CPU work required to evaluate these predicates.

### Model

`qo_apply_scan_term_cpu_overhead()` adds:

```text
scan_term_cpu =
  scan_rows * QO_CPU_WEIGHT
  * (1.2 * range_weight
     + 1.0 * key_filter_weight
     + 0.8 * data_filter_weight)
```

The coefficients are relative:

- `range_weight`: slightly higher because it participates in index range evaluation.
- `key_filter_weight`: baseline index-side predicate check.
- `data_filter_weight`: residual data-side check already partly reflected elsewhere, so it is lower.

### What Limits It

This is additive CPU cost. It does not change index selectivity or heap lookup count. Therefore it can penalize expensive predicates without changing join cardinality.

## 6. Join Term CPU Overhead

### Problem

Nested-loop joins repeatedly evaluate join terms. The old model mainly scaled inner access cost but did not explicitly account for join predicate evaluation complexity.

### Model

`qo_get_nljoin_term_cpu_overhead()` computes:

```text
join_cpu =
  guessed_result_cardinality
  * QO_CPU_WEIGHT
  * 0.5
  * sum(join_term_weight)
```

Join term weights distinguish default joins from string joins:

```text
w(join default)      = 1.00
w(join string eq)    = 1.10
w(join string range) = 1.25
```

### What Limits It

The overhead is added only if join term bitsets are valid. Temporary inner-plan search structures may have empty term bitsets, and the function returns zero in that case.

This avoids penalizing incomplete temporary plans used during search.

## 7. MCV Hot-Key Join Cardinality Guard

### Problem

Average equality selectivity is often:

```text
term_sel ~= 1 / max(NDV(left), NDV(right))
```

This works for uniform distributions but underestimates a join when a small filtered side hits a high-frequency value on a large side.

Example pattern:

```text
small dimension/prefix -> large fact/bridge table
```

The average model estimates:

```text
avg_card = base_cardinality * term_sel
```

But a safer risk estimate is:

```text
risk_fanout = large_card * large_side_max_mcv_frequency
risk_card   = small_card * risk_fanout
risk_sel    = risk_card / base_cardinality
```

### Model

The planner stores per-join-side max MCV frequency in `QO_TERM`:

```text
head_mcv_max_frequency
tail_mcv_max_frequency
```

`query_graph.c` fills these values for equality join terms using histogram data and keeps the head/tail direction aligned with the join term.

`qo_apply_mcv_hotkey_join_guard()` adjusts equality join selectivity only when:

```text
term is equality
term_sel < QO_MCV_GUARD_MAX_BASE_SELECTIVITY
exactly one side is small
MCV/fanout signal is present
```

The adjusted selectivity is:

```text
risk_sel = risk_card / base_cardinality
risk_sel = min(risk_sel, term_sel * max_multiplier)
risk_sel = min(risk_sel, 1.0)
```

### What Limits It

The guard is intentionally bounded:

```text
QO_MCV_GUARD_MAX_BASE_SELECTIVITY = 0.01
```

Broad joins are skipped. This prevents the guard from modifying already-broad joins where MCV-based correction would hide useful plans.

Small side detection is also bounded:

```text
small if cardinality <= QO_MCV_GUARD_SMALL_CARD_ABS
small if cardinality / total_rows <= QO_MCV_GUARD_SMALL_CARD_RATIO
```

Current values:

```text
QO_MCV_GUARD_SMALL_CARD_ABS   = 20.0
QO_MCV_GUARD_SMALL_CARD_RATIO = 0.001
```

The guard applies only when exactly one side is small:

```text
head_small != tail_small
```

This avoids applying dimension/fact logic to symmetric joins.

## 8. Cold Fanout Extension

### Problem

Some columns are not "hot" by percentage but still have large absolute fanout because the table is large.

For example:

```text
max_mcv_frequency = 0.005
large_card        = 4,000,000
risk_fanout       = 20,000
```

The frequency is below the hot-key threshold, but the expected fanout is still large enough to affect join order.

### Model

The guard now accepts a cold fanout case when:

```text
effective_mcv_max_frequency < QO_MCV_GUARD_MIN_FREQUENCY
and
risk_fanout >= QO_MCV_GUARD_MIN_RISK_FANOUT
```

Current values:

```text
QO_MCV_GUARD_MIN_FREQUENCY   = 0.1
QO_MCV_GUARD_MIN_RISK_FANOUT = 10.0
```

Cold fanout uses a separate upper cap:

```text
QO_MCV_GUARD_COLD_FANOUT_SELECTIVITY_MULTIPLIER = 100.0
```

while regular hot-key MCV uses:

```text
QO_MCV_GUARD_MAX_SELECTIVITY_MULTIPLIER = 25.0
```

### What Limits It

Cold fanout still requires:

- equality join
- small side vs large side
- narrow base join selectivity
- positive MCV data
- bounded selectivity multiplier

It is not a blanket penalty for all `movie_id` or bridge joins.

## 9. Downward Damping

### Problem

MCV evidence can sometimes indicate the average model is too pessimistic. Allowing selectivity to move downward can improve plans, but unrestricted downward correction is risky.

### Model

If:

```text
risk_sel < term_sel
```

then:

```text
risk_sel = max(risk_sel, term_sel * QO_MCV_GUARD_MIN_SELECTIVITY_MULTIPLIER)
```

Current value:

```text
QO_MCV_GUARD_MIN_SELECTIVITY_MULTIPLIER = 0.5
```

### What Limits It

The guard can reduce selectivity by at most 50% in one application. This prevents one MCV estimate from collapsing a join cardinality too aggressively.

## 10. Delayed Residual Lookup Penalty

### Problem

Nested-loop index lookup can look cheap if the model sees only final output cardinality. But if each outer row probes an inner index and then applies residual filters, the repeated lookup work can dominate.

### Model

The model targets nested-loop joins where the inner is an index scan and still has residual sargs:

```text
inner is iscan
residual_filter_weight > 0
```

The delayed component is:

```text
delayed_component =
  residual_filter_weight
  * log10(max(10, guessed_outer_cardinality))
  * QO_DELAYED_SARG_PENALTY_FACTOR
```

Current values:

```text
QO_DELAYED_SARG_OUTER_CARD_THRESHOLD = 1.0
QO_DELAYED_SARG_PENALTY_FACTOR       = 0.25
QO_DELAYED_SARG_PENALTY_MAX          = 2.0
```

### What Limits It

The component is capped:

```text
delayed_component <= QO_DELAYED_SARG_PENALTY_MAX
```

The whole function returns `1.0` if:

- plan is not nested-loop
- inner is not index scan
- inner has no residual filter weight
- outer cardinality is below threshold and there is no strong fanout signal

This keeps the penalty focused on repeated lookup after delayed filtering.

## 11. Read-Before-Filter And Bridge Fanout

### Problem

Some plans read many rows before residual filters reduce output:

```text
read_before_filter_ratio = total_rows / cardinality
```

When this ratio is high, final cardinality alone understates work.

### Model

The read-before-filter component is:

```text
read_before_filter_component =
  residual_filter_weight
  * log10(read_before_filter_ratio)
  * QO_READ_BEFORE_FILTER_PENALTY_FACTOR
```

The bridge fanout component is:

```text
bridge_fanout_component =
  join_term_weight
  * log10(read_before_filter_ratio)
  * QO_BRIDGE_FANOUT_PENALTY_FACTOR
```

Current values:

```text
QO_READ_BEFORE_FILTER_RATIO_FLOOR    = 1.5
QO_READ_BEFORE_FILTER_PENALTY_FACTOR = 0.20
QO_READ_BEFORE_FILTER_PENALTY_MAX    = 1.0

QO_BRIDGE_FANOUT_RATIO_FLOOR         = 4.0
QO_BRIDGE_FANOUT_PENALTY_FACTOR      = 0.15
QO_BRIDGE_FANOUT_PENALTY_MAX         = 2.0
```

### What Limits It

Both components use ratio floors and caps. The final lookup penalty is:

```text
lookup_penalty =
  1.0
  + min(total_component, max_allowed_component)
```

It is applied to repeated inner lookup CPU and I/O:

```text
inner_cpu_cost *= lookup_penalty
inner_io_cost  *= lookup_penalty
```

This is intentionally cost-side only. It does not change join output cardinality.

## 12. Skew Uncertainty Penalty

### Problem

When a join value is unknown at planning time, exact value-specific fanout cannot be used. But if a fact-side foreign-key-like column is skewed, average selectivity can be unsafe.

### Model

For a small side joined to a large side:

```text
skew_ratio = large_mcv_frequency / avg_frequency
```

If:

```text
skew_ratio > QO_SKEW_UNCERTAINTY_RATIO_FLOOR
```

then:

```text
component += log10(skew_ratio) * QO_SKEW_UNCERTAINTY_PENALTY_FACTOR
```

Current values:

```text
QO_SKEW_UNCERTAINTY_RATIO_FLOOR    = 4.0
QO_SKEW_UNCERTAINTY_PENALTY_FACTOR = 0.20
QO_SKEW_UNCERTAINTY_PENALTY_MAX    = 1.0
```

### What Limits It

The penalty is not applied to the first dimension-to-fact lookup:

```text
outer prefix node count <= 1 -> no penalty
```

This protects good plans that intentionally start from a selective dimension value. The uncertainty penalty is aimed at later repeated lookups after a skewed result has already entered the join prefix.

The function also requires:

- nested-loop join
- valid join term bitset
- equality join term
- exactly one small side
- small side not above `QO_MCV_GUARD_SMALL_CARD_ABS`
- positive MCV and average frequency

## 13. How The Pieces Interact

The CBO now has three separate correction channels:

### Cardinality Channel

Used by:

```text
qo_apply_mcv_hotkey_join_guard()
```

It changes join selectivity when MCV evidence shows average fanout is unsafe.

This affects:

```text
estimated join cardinality
downstream prefix cardinality
future join costs
```

### Operator CPU Channel

Used by:

```text
qo_apply_scan_term_cpu_overhead()
qo_get_nljoin_term_cpu_overhead()
QO_SSCAN_FILTER_CPU_FACTOR
```

It changes how expensive it is to evaluate predicates or join terms.

This affects:

```text
scan variable_cpu_cost
join variable_cpu_cost
```

but does not directly change cardinality.

### Lookup Risk Channel

Used by:

```text
qo_get_delayed_sarg_lookup_penalty()
qo_get_skew_uncertainty_lookup_penalty()
```

It multiplies repeated inner lookup CPU/I/O when the plan shape is risky.

This affects:

```text
nested-loop inner repeated access cost
```

but does not directly change selectivity.

Keeping these channels separate is important. It prevents a single heuristic from doing too many jobs and makes regressions easier to reason about.

## 14. Why The Guard Rails Matter

The model intentionally uses floors and caps because optimizer estimates are approximate.

The main guard rails are:

```text
QO_MCV_GUARD_MAX_BASE_SELECTIVITY
  prevents broad joins from being over-adjusted

QO_MCV_GUARD_SMALL_CARD_ABS / RATIO
  limits MCV guard to small-side joins

QO_MCV_GUARD_MAX_SELECTIVITY_MULTIPLIER
  caps regular hot-key upward movement

QO_MCV_GUARD_COLD_FANOUT_SELECTIVITY_MULTIPLIER
  caps cold-fanout upward movement separately

QO_MCV_GUARD_MIN_SELECTIVITY_MULTIPLIER
  caps downward selectivity movement

QO_DELAYED_SARG_PENALTY_MAX
  caps repeated lookup residual-filter penalty

QO_READ_BEFORE_FILTER_PENALTY_MAX
  caps read-before-filter penalty

QO_BRIDGE_FANOUT_PENALTY_MAX
  caps bridge fanout penalty

QO_SKEW_UNCERTAINTY_PENALTY_MAX
  caps unknown-value skew uncertainty
```

These are not query-specific conditions. They are model boundaries that keep the CBO stable while accounting for previously missing costs.

## 15. Review Checklist

When reviewing this PR, check the following:

- Does a change affect cardinality, CPU cost, or repeated lookup cost?
- Is the correction bounded by a threshold or cap?
- Does the logic avoid broad symmetric joins?
- Does it avoid penalizing the first selective dimension-to-fact lookup?
- Does it depend on table/query names? It should not.
- Does a residual predicate cost change accidentally alter selectivity? It should not.
- Does an MCV selectivity change accidentally apply to non-equality joins? It should not.

## 16. Practical Interpretation For JOB Queries

The intended behavior is:

- Plans that start from selective dimensions should remain possible.
- Plans that join a small prefix into a large skewed/bridge table should not be under-costed by average selectivity alone.
- Plans that read many rows before applying residual `LIKE` or `IN` filters should pay for that work.
- Plans with complex string predicates should reflect higher CPU cost.
- Faster forced plans should become closer to the naturally chosen plan without adding query-specific rules.

This is an incremental CBO improvement. It does not solve exact value-specific fanout. A future improvement would use known filtered dimension values to estimate join fanout more directly.