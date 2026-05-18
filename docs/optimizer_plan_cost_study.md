# CUBRID Optimizer Plan/Cost Study Notes

이 문서는 `src/optimizer` 영역 중 비트셋 구현 세부보다, 후보 플랜을 만들고 비용을 계산한 뒤 최적 플랜을 고르는 흐름을 중심으로 정리한 노트이다.

현재 브랜치 기준으로 비용 계산 로직은 별도 `query_planner_cost.c`가 아니라 대부분 `src/optimizer/query_planner.c`에 있다.

## 큰 흐름

1. `query_graph.c`에서 파서 트리를 옵티마이저 그래프로 바꾼다.
2. `query_planner.c`에서 그래프 노드/조인 조합별 후보 플랜을 열거하고, 비용과 규칙으로 후보를 줄인다.
3. 최종 선택된 `QO_PLAN`은 `plan_generation.c`에서 XASL 생성으로 넘어간다.

주요 진입점은 다음과 같다.

- `query_graph.c`: `qo_optimize_query`, `build_query_graph`, `qo_discover_edges`, `qo_assign_eq_classes`, `qo_discover_indexes`, `qo_discover_partitions`
- `query_planner.c`: `qo_planner_search`, `qo_search_planner`, `qo_search_partition`, `qo_search_partition_join`, `planner_permutate`, `planner_visit_node`
- `plan_generation.c`: `qo_to_xasl`, `preserve_info`

## 핵심 자료구조

`QO_PLAN`은 하나의 실행 후보를 나타낸다. scan, sort, join, follow 같은 plan type을 가지고, 비용은 네 개 필드로 분리된다.

- `fixed_cpu_cost`, `fixed_io_cost`: 조인 위치와 무관하게 한 번 드는 비용
- `variable_cpu_cost`, `variable_io_cost`: 조인 위치나 반복 횟수에 따라 증폭될 수 있는 비용
- `order`: 이 플랜이 만들어내는 정렬 순서
- `sarged_terms`, `subqueries`: 이 플랜에서 평가하거나 고정한 조건/서브쿼리

`QO_INFO`는 특정 노드 집합, 즉 특정 테이블 부분집합을 만드는 방법을 memoize하는 단위이다.

- `nodes`: 이 info가 포함하는 테이블 집합
- `terms`: 이미 처리한 조건 집합
- `cardinality`: 이 집합의 결과 row 추정치
- `scan_rows`: access cost 계산에 쓰는 스캔 row 추정치
- `hit_prob`: LIMIT가 있는 nested-loop 비용에서 fanout 비슷하게 쓰는 hit 확률
- `best_no_order`: 순서를 고려하지 않는 최선 후보들
- `planvec`: interesting order별 후보들

`QO_PLANVEC`는 후보 플랜을 최대 `NPLANS`개 보관한다. 현재 `NPLANS`는 4이다. 완전히 하나만 남기지 않는 이유는 비용만으로 즉시 우열을 판단하기 어려운 후보나, fixed/variable 비용 비율이 다른 후보를 잠시 유지하기 위해서다.

## 비용 계산 모델

모든 플랜 생성 함수는 결국 `qo_plan_compute_cost` 또는 각 plan type의 cost function을 통해 비용을 채운다. `QO_PLAN_VTBL`에 plan type별 `cost_fn`이 연결되어 있다.

`qo_plan_compute_cost`의 순서는 다음과 같다.

1. 플랜에 pin된 서브쿼리 비용을 `qo_plan_compute_subquery_cost`로 합산한다.
2. `plan->vtbl->cost_fn(plan)`을 호출해 scan/join/sort 자체 비용을 계산한다.
3. 서브쿼리 비용을 `plan->info->scan_rows`만큼 곱해 variable cost에 더한다.

비용 비교에서 기본 총 비용은 대체로 다음 형태다.

```text
total = fixed_cpu + fixed_io + variable_cpu + variable_io
```

다만 plan vector에서 특정 반복 계수 `n`을 넣어 고를 때는 다음을 쓴다.

```text
cost = fixed + n * variable
```

## Scan 비용

`qo_sscan_cost`는 sequential scan 비용을 계산한다.

- `scan_rows = max(1, QO_NODE_NCARD(node))`
- CPU는 row 수에 `QO_CPU_WEIGHT`를 곱한다.
- SARG 조건 평가 비용은 `qo_sum_bitset_term_cost_weights`로 더하고 `scan_rows * QO_CPU_WEIGHT`에 곱해 추가한다.
- IO는 `QO_NODE_TCARD(node)`를 사용한다.

`qo_iscan_cost`는 index scan 비용을 계산한다.

- range term 선택도와 key filter 선택도를 곱한다.
- partial-key 통계(`cum_stats.pkeys`) 또는 전체 key 수로 선택도 하한을 잡는다.
- index IO는 btree height와 leaf 접근 수로 추정한다.
- covering index면 heap 접근을 거의 제거한다.
- non-covering이면 `object_IO`, `heap_access`를 선택도 기반으로 계산한다.
- 최종 `scan_rows`는 `NCARD * sel * filter_sel` 기반으로 갱신된다.
- range/key filter/data filter CPU overhead는 `qo_apply_scan_term_cpu_overhead`가 더한다.

술어 CPU 가중치는 `qo_get_term_cost_weight`에서 정한다. 숫자 비교보다 문자열 비교, LIKE contains/complex 패턴이 더 비싸게 잡힌다. 상수는 `query_planner_constants.h`에 있다.

## Join 비용

조인 후보는 `planner_visit_node`가 현재 방문한 노드 집합과 새 tail node 사이의 term을 분류한 뒤 생성한다. 주요 후보 생성 함수는 다음과 같다.

- `qo_examine_idx_join`: correlated index nested-loop 후보
- `qo_examine_nl_join`: 일반 nested-loop 후보
- `qo_examine_merge_join`: merge join 후보
- `qo_examine_hash_join`: hash join 후보, 현재 기본은 `TEST_HASH_JOIN_ENABLE`에 의해 제한된다
- `qo_examine_follow`: path term follow 후보

`qo_nljoin_cost`는 nested-loop 비용을 계산한다.

- fixed 비용은 outer와 inner fixed 비용 합이다.
- inner variable 비용은 outer 결과 row 추정치만큼 반복된다.
- LIMIT가 있으면 `guessed_result_cardinality`를 줄여서 inner 반복 비용을 낮춘다.
- inner가 index scan이면 IO hit ratio를 반영해 `1 - ISCAN_IO_HIT_RATIO`만큼만 반복 IO를 잡는다.
- inner가 seq scan이면 너무 낙관적이지 않도록 `SSCAN_DEFAULT_CARD`를 더한다.
- inner index lookup 뒤 data filter가 늦게 적용되는 형태는 `qo_get_delayed_sarg_lookup_penalty`로 페널티를 줄 수 있다.
- 조인 조건 자체의 CPU 비용은 `qo_get_nljoin_term_cpu_overhead`가 추가한다.

`qo_mjoin_cost`는 merge join 비용을 계산한다.

- outer/inner fixed와 variable 비용을 합친다.
- `(outer_cardinality + inner_cardinality) * QO_CPU_WEIGHT * MJ_CPU_OVERHEAD_FACTOR`를 CPU overhead로 더한다.
- 필요한 ordering이 없으면 `qo_find_best_plan_on_info(..., order, ...)` 과정에서 sort plan이 만들어질 수 있다.

`qo_hjoin_cost`는 hash join 비용을 계산한다.

- 양쪽 입력 비용을 합친다.
- inner build, outer build 두 경우의 build/probe CPU 비용을 계산한다.
- inner join이면 둘 중 싼 build 방향을 고른다.
- left/right outer join은 보존 방향 때문에 build 방향이 고정된다.

## 카디널리티와 선택도

선택도 자체는 `query_planner_selectivity.c`의 `qo_expr_selectivity` 계열에서 계산된다. `query_graph.c`가 term을 분석해 `QO_TERM_SELECTIVITY`에 연결하고, `query_planner.c`의 `planner_visit_node`가 조인 결과 cardinality를 만든다.

조인 조합의 기본 추정은 다음 흐름이다.

1. `cardinality = head_info->cardinality * tail_info->cardinality`
2. sarged join term들의 selectivity를 곱한다.
3. outer join이면 preserved side cardinality를 하한으로 둔다.
4. `qo_apply_mcv_hotkey_join_guard`가 등가 조인에서 hot key 위험이 큰 경우 선택도를 상향 보정할 수 있다.
5. `qo_get_term_hit_prob`가 NDV 비율로 head/tail hit probability를 계산해 LIMIT nested-loop 추정에 사용한다.

이 영역은 플랜 선택에 매우 큰 영향을 준다. 잘못 낮은 cardinality는 nested-loop + index lookup을 과하게 좋게 만들고, 잘못 높은 cardinality는 sort/merge 또는 seq scan을 과하게 선호하게 만든다.

## 후보 보관과 비교

후보 플랜은 `qo_check_plan_on_info`를 통해 `QO_INFO`에 들어간다.

흐름은 다음과 같다.

1. 현재 전역 best info가 있으면 같은 order의 best와 먼저 비교해 가지치기한다.
2. unordered plan이면 `qo_check_new_best_plan_on_info`가 `best_no_order`에 넣을지 판단한다.
3. ordered plan이면 해당 equivalence class의 `planvec`에 넣을지 판단한다.
4. 새 unordered best가 생기면 interesting order를 만족하기 위한 sort 후보도 생성해 본다.

`qo_check_planvec`는 새 후보와 기존 후보들을 `qo_plan_cmp`로 비교한다.

- 새 후보가 기존 후보보다 명확히 좋으면 기존 후보를 교체한다.
- 기존 후보가 새 후보보다 명확히 좋으면 새 후보를 버린다.
- 우열이 불명확하면 여유 슬롯에 보관한다.
- 슬롯이 꽉 차면 best variable cost, best total cost 후보는 보존하려 하고, worst total/variable 후보를 밀어낸다.

최종적으로 하나를 꺼낼 때는 `qo_find_best_plan_on_planvec`가 `fixed + n * variable`이 가장 작은 플랜을 반환한다.

## `qo_plan_cmp`의 의미

`qo_plan_cmp`는 단순히 총 비용만 비교하지 않는다. 먼저 비용 차이가 충분히 크면 비용으로 결정하지만, 애매한 구간에서는 규칙 기반 선호가 끼어든다.

중요한 규칙은 다음과 같다.

- LIMIT가 있으면 `RBO_CHECK_LIMIT_RATIO`를 쓰고, 일반 비교는 `RBO_CHECK_COST`, `RBO_CHECK_RATIO`를 쓴다.
- SORT-LIMIT plan은 같은 subplan 위에 있으면 선호될 수 있다.
- multi-range optimization, covering index, order-by skip, group-by skip 같은 특수 index plan을 선호한다.
- 같은 테이블의 index scan끼리는 range/filter term 수, partial-key 통계, index page/leaf 수 등을 비교한다.
- 그래도 결정하지 못하면 비용 비교(`cost_cmp`)로 간다.

즉 이 옵티마이저는 CBO 중심이지만, 비용 차이가 크지 않은 영역에서는 RBO 성격의 tie-breaker가 꽤 중요하다.

## Join Order 탐색

`qo_search_partition_join`은 하나의 join graph partition에 대해 탐색한다. 노드 수가 작으면 전체 탐색에 가깝게 가고, 크면 `join_unit`으로 부분 탐색 크기를 제한한다.

```text
nodes <= 25  -> join_unit = min(8, nodes)
nodes <= 37  -> join_unit = 3
else         -> join_unit = 2
```

단, path term이나 `ORDERED` 힌트가 있으면 `join_unit = nodes_cnt`로 두어 전체 순서 제약을 더 직접적으로 따른다.

`planner_permutate`는 head 후보를 순회하면서 dependency, outer join dependency, ordered hint를 확인한다. 각 시도는 `planner_visit_node`로 들어가며, 여기서 새 `QO_INFO`를 만들고 가능한 join method 후보들을 등록한다.

부분 탐색일 때는 현재 best plan 비용에 아직 방문하지 않은 rest node들의 대략 비용(`planner_nodeset_join_cost`)을 더해 다음 outermost node를 고른다.

## 최종 플랜과 XASL

`qo_search_partition`은 partition의 best info에서 `qo_find_best_plan_on_info`를 호출하고, `qo_plan_finalize`로 winner를 잡는다. 여러 partition이 있으면 partition plan들을 결합한다.

`plan_generation.c`의 `preserve_info`는 최종 plan 비용과 cardinality를 `QO_SUMMARY`에 저장한다. 이 값은 서브쿼리 비용 계산에서 다시 사용된다.

## 읽을 때의 기준점

플랜 선택 이슈를 볼 때는 다음 순서로 보는 것이 좋다.

1. `query_graph.c`에서 term class, selectivity, index 가능 여부가 어떻게 잡혔는지 본다.
2. `planner_visit_node`에서 해당 조합의 cardinality가 어떻게 계산됐는지 본다.
3. 원하는 join method 후보가 `qo_examine_*`에서 생성됐는지 본다.
4. 생성된 플랜의 `*_cost` 함수가 비용을 어떻게 채웠는지 본다.
5. `qo_check_plan_on_info`와 `qo_plan_cmp`에서 후보가 버려졌는지, rule tie-breaker에 밀렸는지 본다.
6. 마지막으로 `qo_find_best_plan_on_planvec`가 fixed/variable 비용으로 어떤 플랜을 골랐는지 확인한다.

