# CBRD-26959 영역 1 — 하이브리드 페이지 샘플링 + NDV 추정 설계

기준 브랜치: feature/CBRD-26959-histogram-collector (develop f7432203a).
정책 결정: 2026-07-15 사용자 합의. 기준 구현은 전부 PostgreSQL (커스텀 휴리스틱 금지).

## 확정 정책

1. **표본 크기**: PG식 행 수 기준 — 목표 표본 행수 `SAMPLE_ROWS = 300 × bucket_target`
   (bucket_target 기본 = PRM_ID_DEFAULT_HISTOGRAM_BUCKET_COUNT). 현행 리저버 상한
   (HISTOGRAM_MAX_SAMPLE_ROWS=300,000)은 유지.
2. **NDV 외삽**: 순수 Duj1 (PG compute_distinct_stats, Haas–Stokes):
   `D̂ = n·d / (n − f1 + f1·n/N)`  (n=표본 행수, d=표본 distinct, f1=표본 싱글턴 수,
   N=모집단 non-null 행수 추정). f1 합산 불가 → 병합된 최종 표본에서 1회 계산
   (기존 group_counts() 재사용, 스캔 핫루프 비용 0).
   표본이 전부 distinct(d==n)면 PG와 동일하게 unique 취급 → D̂ = N.
3. **적용 범위**: 비FULLSCAN 전체 (UPDATE STATISTICS + ANALYZE ... UPDATE HISTOGRAM).
   WITH FULLSCAN = 현행 전수 리저버 (불변). 힙 수집기에 with_fullscan 플래그 전달 신설.
4. **파라미터 2개 신설** (system_parameter.c):
   - `statistics_sampling_threshold_pages` (전환 임계; 표본 필요 페이지가 테이블
     페이지보다 크거나 테이블이 임계 이하 → 풀스캔)
   - `statistics_sample_rows` (목표 표본 행수 상한; 0 = 300×bucket_target 자동)
5. **버킷 수 자동** (노옵션): PG 방식 — bucket_target은 상한이고, 실제 저장 버킷 수는
   MCV 제외 잔여 distinct 수에 맞춰 자동 축소. `WITH n BUCKETS` 명시 시 n을 target으로.

## 주입 지점 (코드 맵: Explore 2026-07-15)

- 페이지 선택: `histogram_sampler_sr.cpp reserve_and_split()` (:816) — ftab 섹터 집합
  구축 직후, 전체 페이지 수와 필요 표본 페이지 수(= SAMPLE_ROWS / avg_rows_per_page,
  heap_get_num_objects 기반)를 비교해 샘플 모드 결정. 페이지 채택은
  `ftab_page_walker::next_data_vpid()` (:618)에서 결정적 해시
  `hash(vpid, seed) < p·2^64` 로 필터 (페이지 리스트 실체화 없음, 워커 독립,
  재현 가능 — 리저버 seed와 동일한 고정 seed 정책).
- 확장 계수: 표본 페이지 비율 p → `N̂_nn = seen_nn / p`. 기존
  `stats_estimate_ndv_from_sample(sampling_weight, total_nn_rows)` 인자와
  build_blob의 `scale_nn` (:338)에 연결. total_rows도 `seen / p`로 추정 저장.
- Duj1 계산: 병합 후 `group_counts()` 산출물(d, f1)로 `statistics_ndv.c`에 함수 추가
  (PG analyze.c compute_distinct_stats 대응 주석). NDV-only 경로
  (`xstats_collect_ndv_by_fullscan_reservoir`)는 HLL만 있으므로 샘플 모드에서는
  기존 STATS_NDV_RESERVOIR_ROWS(30k) 리저버로 f1 확보.
- HLL: 샘플 모드에서도 유지(표본 페이지 전체 행 distinct). 풀스캔 경로의 정확
  NDV 소스로 계속 사용. 샘플 모드 Duj1과는 독립 (결정 2: 클램프 없이 순수 Duj1).
- UNIQUE/PK 컬럼: 현행 non_null_rows 직접 사용 → 샘플 모드에선 N̂_nn.

## 알려진 한계 (PG 동일 노출, 문서화만)

- 페이지 내 클러스터링: 같은 페이지의 행 상관(적재 순서 등)으로 행-SRS 가정이
  근사가 됨 — PG ANALYZE와 동일한 2단(페이지→행) 표본 구조로 동일 수준.
- Duj1은 심한 zipf에서 과소추정 경향 — PG와 동일. AC의 오차 실측
  (uniform/zipf/클러스터링)으로 수치 확인 후 목표치 합의.

## 검증 계획 (AC 대응)

- 오차 실측: uniform / zipf(s=1) / 클러스터링(정렬 적재) 3분포 × 대형 1천만+ 행,
  NDV 참값 대비 Duj1 오차 및 develop(구 샘플링)·풀스캔 대비 수집 시간 비교표.
- WITH FULLSCAN 결과 불변 회귀 확인, JOB 113 플랜 무회귀 확인.
- 빌드/측정은 JOB 재측정 등 다른 측정과 상호 배타 (release 빌드가 install을
  갈아치우므로 측정 중 빌드 금지).

## 안정화 결정 (2026-07-15 사용자 합의, PR #7476 범위)

PR 목표 재정의: 히스토그램/샘플링 스캔 자체의 **구조적 안정화** — 느린 부분, 시스템 영향, 정책 불안정 전부 이 PR에서 수정 (후속 이월 금지 항목 명시됨).

| # | 항목 | 결정 |
|---|---|---|
| 1 | NDV 전용 경로 풀스캔 | **이 PR에서 샘플링+Duj1 배선** |
| 2 | 파티션 클래스·시리얼 폴백 풀스캔 | **이 PR에서 샘플링 확장** (후속 금지) |
| 3 | 샘플 페이지 내 전 행 처리 | **이 PR에서 행 솎기(Vitter류) 구현** (후속 금지) |
| 4 | 버퍼 풀 오염 | pgbuf에 aging/비승격 메커니즘 있는지 **조사 후 적용** — 운영 워킹셋 밀어내기 불가 원칙 |
| 5 | 워커 풀 점유 | 코드 변경 없음 — **매뉴얼 기재**로 처리 |
| 6 | 메모리 상한 | **수집 전 메모리 사전 계산 + 상한** 적용 |
| 7 | 스캔 완료 페이지 | **처리 직후 즉시 강등/해제** 가능한지 조사 후 적용 (4와 연계) |
| 8 | 취소 반응성 | **페이지 단위 인터럽트 확인 추가** |
| 9 | 표본 시드 | **고정 시드 디폴트(불변)** + `/*+ rand_seed */`류 힌트로 회차별 랜덤 전환 |
| 10 | npages 정확도 | heap_get_num_objects vs file_get_num_user_pages **검증** |
| 11 | relocated/overflow 이중 계수 | heap_next_1page 동작 **확인** |
| 12 | 버킷 수 자동 축소 | 기합의 — 이 PR 후속 커밋 |

구현 순서: 1 → 2 → 3 → 8 → 6 → 9 → 4·7(조사 결과에 따라) → 10·11 검증 → 12.
