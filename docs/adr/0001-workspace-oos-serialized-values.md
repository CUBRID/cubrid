---
status: accepted
---

# 직렬화된 workspace 값의 직접 OOS 변환

SA workspace OOS 저장에서 이미 직렬화된 값을 DB_VALUE로 읽고 다시 쓰는 비용을 제거한다.
최종 파티션 결정 이후의 기존 force 경계에서 원래 속성 바이트를 OOS에 저장하고, OOS inline stub과 VOT를 재구성한다.
DB_VALUE 기반 writer와 OOS 선택·크기 계산을 공유하여 저장 정책이 달라지지 않게 한다.

직렬화 계층으로 OOS 쓰기를 옮기면 파티션 선택과 rollback 경계가 달라지므로 기존 저장 경계를 유지한다.
이 경로의 입력은 현재 저장 대상 클래스 형식의 workspace 레코드로 한정하며, 범용 과거 레코드 변환 기능을 추가하지 않는다.
VOT 폭을 무조건 유지하는 단순 구현은 경계값에서 OOS 대상 선택을 바꿀 수 있으므로 채택하지 않는다.

검증은 기존 변환 경로와의 속성별 OOS 선택 및 payload 바이트 비교, 실제 loader/CSQL 회귀,
CS/SA 결과 비교와 동일 데이터의 적재 시간·CPU 비교를 포함한다. 물리적 OID와 트랜잭션 식별 정보는 비교 대상에서 제외한다.
