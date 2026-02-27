# TC Branch Automation - Event Scenarios & Diagrams

이 문서는 TC 브랜치 자동화 시스템에서 발생할 수 있는 모든 예상 시나리오에 대한 이벤트 다이어그램을 포함합니다.

---

## 1. 기본 개념 및 상태 정의

### 1.1 시스템 구성 요소

| 구성 요소 | 설명 |
|-----------|------|
| **Engine Repo** | `CUBRID/cubrid` - 엔진 코드 저장소, 워크플로우 실행 위치 |
| **TC Public** | `CUBRID/cubrid-testcases` - 공개 테스트 케이스 |
| **TC Private** | `CUBRID/cubrid-testcases-private-ex` - 비공개 테스트 케이스 |
| **TC Branch** | `tc/pr-{N}` - PR 번호 기반 TC 브랜치 |
| **GitHub App** | TC 저장소에 쓰기 권한을 가진 인증 메커니즘 |

### 1.2 상태 정의

```
[NO_BRANCH] ──► [CREATED] ──► [TESTING] ──► [MERGED_TO_DEVELOP] ──► [DELETED]
                    │              │              │
                    ▼              ▼              ▼
              [REVERTED]    [CONFLICT]     [MERGE_FAILED]
```

| 상태 | 설명 |
|------|------|
| `NO_BRANCH` | TC 브랜치가 아직 생성되지 않음 |
| `CREATED` | `tc/pr-N` 브랜치가 develop에서 생성됨 |
| `TESTING` | CI가 TC 브랜치를 사용하여 테스트 실행 중 |
| `MERGED_TO_DEVELOP` | TC 브랜치가 develop에 squash merge됨 |
| `DELETED` | TC 브랜치가 삭제됨 |
| `REVERTED` | Revert PR에 의해 TC 변경사항이 revert됨 |
| `CONFLICT` | TC 브랜치와 develop 간 충돌 발생 |
| `MERGE_FAILED` | Merge 작업 실패 |

---

## 2. 시나리오 분류

### Category A: Happy Path (정상 흐름)
| 시나리오 ID | 설명 |
|-------------|------|
| **A1** | 일반 PR 열림 → TC 브랜치 생성 → TC 머지 → 엔진 PR 머지 → TC 브랜치 삭제 |
| **A2** | Revert PR 열림 → 원본 TC 커밋 Revert → Revert PR 머지 → TC Revert 머지 |

### Category B: Edge Cases (경계 조건)
| 시나리오 ID | 설명 |
|-------------|------|
| **B1** | TC 브랜치가 이미 존재함 (재시도/재오픈) |
| **B2** | 원본 PR에 TC 변경사항이 없었음 (Revert 시나리오) |
| **B3** | PR이 머지되지 않고 닫힘 (TC 브랜치만 삭제) |
| **B4** | 엔진 PR이 머지된 후 TC 머지가 실패함 |

### Category C: Error Scenarios (오류 상황)
| 시나리오 ID | 설명 |
|-------------|------|
| **C1** | GitHub App 토큰 생성 실패 |
| **C2** | TC 저장소 접근 권한 없음 |
| **C3** | develop 브랜치가 존재하지 않음 |
| **C4** | TC 브랜치 생성 중 충돌/오류 |
| **C5** | TC squash merge 충돌 |
| **C6** | Revert 작업 충돌 |
| **C7** | 브랜치 삭제 실패 (보호 규칙) |

### Category D: Race Conditions (동시성)
| 시나리오 ID | 설명 |
|-------------|------|
| **D1** | 두 엔진 PR이 동시에 머지 시도 (TC develop 충돌) |
| **D2** | PR이 빠르게 재오픈/닫힘 반복 (sync/finalize 경쟁) |
| **D3** | Revert PR이 원본 TC 머지 전에 열림 |
| **D4** | 동일 TC 저장소에 동시 작업 (Public/Private 병렬 처리) |

### Category E: Synchronize Scenarios (PR 업데이트)
| 시나리오 ID | 설명 |
|-------------|------|
| **E1** | 일반 PR이 Revert PR로 변경됨 (body 수정) |
| **E2** | Revert PR이 일반 PR로 변경됨 (body 수정) |
| **E3** | PR 제목 변경 (헤더 업데이트) |
| **E4** | PR이 synchronize되었으나 TC 브랜치는 이미 존재 |

---

## 3. 이벤트 다이어그램

### 시나리오 A1: 정상 흐름 - 일반 PR 라이프사이클

```mermaid
sequenceDiagram
    actor Dev as 개발자
    participant Engine as CUBRID/cubrid
    participant Workflow_Sync as tc-branch-sync.yml
    participant TC_Pub as cubrid-testcases
    participant TC_Priv as cubrid-testcases-private-ex
    participant CI as CI 시스템
    participant Workflow_Finalize as tc-branch-finalize.yml

    %% PR Opened
    Dev->>Engine: PR #123 열림 (제목: [CBRD-1234] Fix bug)
    Engine->>Workflow_Sync: pull_request_target (opened)
    Workflow_Sync->>Workflow_Sync: PR 타입 감지 (Normal)
    par TC Public 브랜치 생성
        Workflow_Sync->>TC_Pub: tc/pr-123 브랜치 생성 (from develop)
        TC_Pub-->>Workflow_Sync: 브랜치 생성 완료
    and TC Private 브랜치 생성
        Workflow_Sync->>TC_Priv: tc/pr-123 브랜치 생성 (from develop)
        TC_Priv-->>Workflow_Sync: 브랜치 생성 완료
    end
    Workflow_Sync-->>Engine: 완료 (PR에 코멘트 없음 - 성공)

    %% CI Testing Phase
    Engine->>CI: CI 테스트 트리거
    CI->>TC_Pub: tc/pr-123 브랜치 사용
    CI->>TC_Priv: tc/pr-123 브랜치 사용
    CI-->>Engine: 테스트 결과

    %% PR Merged
    Dev->>Engine: PR #123 머지 (develop)
    Engine->>Workflow_Finalize: pull_request_target (closed, merged=true)
    
    par TC Public 머지
        Workflow_Finalize->>TC_Pub: tc/pr-123 → develop squash merge
        Note over Workflow_Finalize,TC_Pub: 커밋 메시지: [CBRD-1234] Merge TC branch 'tc/pr-123' into develop
        TC_Pub-->>Workflow_Finalize: 머지 완료
        Workflow_Finalize->>TC_Pub: tc/pr-123 브랜치 삭제
        TC_Pub-->>Workflow_Finalize: 삭제 완료
    and TC Private 머지
        Workflow_Finalize->>TC_Priv: tc/pr-123 → develop squash merge
        TC_Priv-->>Workflow_Finalize: 머지 완료
        Workflow_Finalize->>TC_Priv: tc/pr-123 브랜치 삭제
        TC_Priv-->>Workflow_Finalize: 삭제 완료
    end
    Workflow_Finalize-->>Engine: 완료
```

---

### 시나리오 A2: 정상 흐름 - Revert PR 라이프사이클

```mermaid
sequenceDiagram
    actor Dev as 개발자
    participant Engine as CUBRID/cubrid
    participant Workflow_Sync as tc-branch-sync.yml
    participant TC_Pub as cubrid-testcases
    participant Workflow_Finalize as tc-branch-finalize.yml

    %% Step 1: Original PR merged (pre-condition)
    Note over Engine,TC_Pub: 전제조건: 원본 PR #100이 이미 머지됨<br/>TC 저장소 develop에 tc/pr-100의 squash merge 커밋 존재

    %% Step 2: Revert PR Opened
    Dev->>Engine: Revert PR #101 열림 (제목: [CBRD-1235] Revert "Fix bug", Body: Reverts #100)
    Engine->>Workflow_Sync: pull_request_target (opened)
    Workflow_Sync->>Workflow_Sync: PR 타입 감지 (Revert, 원본 PR #100)
    
    par TC Public Revert 처리
        Workflow_Sync->>TC_Pub: clone & checkout develop
        Workflow_Sync->>TC_Pub: "tc/pr-100" squash merge 커밋 검색
        TC_Pub-->>Workflow_Sync: 커밋 SHA: abc1234
        Workflow_Sync->>TC_Pub: tc/pr-101 브랜치 생성 (from develop)
        Workflow_Sync->>TC_Pub: git revert abc1234
        Note over Workflow_Sync,TC_Pub: 커밋 메시지: [CBRD-1235] Revert "Merge TC branch 'tc/pr-100'..."
        Workflow_Sync->>TC_Pub: git push origin tc/pr-101
        TC_Pub-->>Workflow_Sync: push 완료
    and TC Private Revert 처리
        Workflow_Sync->>TC_Priv: 동일한 revert 작업 수행
        TC_Priv-->>Workflow_Sync: push 완료
    end
    Workflow_Sync-->>Engine: 완료

    %% Step 3: Revert PR Merged
    Dev->>Engine: Revert PR #101 머지
    Engine->>Workflow_Finalize: pull_request_target (closed, merged=true)
    Workflow_Finalize->>TC_Pub: tc/pr-101 → develop squash merge
    Workflow_Finalize->>TC_Pub: tc/pr-101 브랜치 삭제
    Workflow_Finalize->>TC_Priv: 동일한 작업 수행
    Workflow_Finalize-->>Engine: 완료
    Note over Engine,TC_Pub: 결과: develop에서 원본 TC 변경사항이 revert됨
```

---

### 시나리오 B1: TC 브랜치가 이미 존재함 (멱등성)

```mermaid
sequenceDiagram
    actor Dev as 개발자
    participant Engine as CUBRID/cubrid
    participant Workflow_Sync as tc-branch-sync.yml
    participant TC_Pub as cubrid-testcases

    Dev->>Engine: PR #200 열림 (이전에 열었다가 닫힘)
    Note over Engine: tc/pr-200 브랜치가 이미 존재함
    Engine->>Workflow_Sync: pull_request_target (opened)
    Workflow_Sync->>Workflow_Sync: PR 타입 감지 (Normal)
    Workflow_Sync->>TC_Pub: tc/pr-200 브랜치 존재 확인
    TC_Pub-->>Workflow_Sync: 브랜치 존재함 (HTTP 200)
    Note over Workflow_Sync: 스킵 조건 충족 - 아무것도 하지 않음
    Workflow_Sync->>Engine: "::notice::Branch already exists. Skipping."
    Engine-->>Dev: 성공 (변경 없음)
```

---

### 시나리오 B2: 원본 PR에 TC 변경사항 없음 (Revert 시 빈 브랜치)

```mermaid
sequenceDiagram
    participant Workflow_Sync as tc-branch-sync.yml
    participant TC_Pub as cubrid-testcases

    Note over Workflow_Sync: Revert PR #105 열림 (원본 PR #104)
    Workflow_Sync->>TC_Pub: clone & checkout develop
    Workflow_Sync->>TC_Pub: "tc/pr-104" squash merge 커밋 검색
    TC_Pub-->>Workflow_Sync: 검색 결과 없음 (원본 PR에 TC 변경 없음)
    
    Note over Workflow_Sync: 실패 대신 빈 브랜치 생성
    Workflow_Sync->>TC_Pub: tc/pr-105 브랜치 생성 (from develop)
    Workflow_Sync->>TC_Pub: git push origin tc/pr-105
    
    Note over Workflow_Sync: "::notice::No TC merge commit found. Creating empty revert branch."
    Workflow_Sync-->>Workflow_Sync: 성공 처리 (exit 0)
```

---

### 시나리오 B3: PR이 머지되지 않고 닫힘

```mermaid
sequenceDiagram
    actor Dev as 개발자
    participant Engine as CUBRID/cubrid
    participant Workflow_Finalize as tc-branch-finalize.yml
    participant TC_Pub as cubrid-testcases
    participant TC_Priv as cubrid-testcases-private-ex

    Dev->>Engine: PR #300 닫힘 (merged=false)
    Note over Engine: PR이 머지되지 않음 (단순 닫기 또는 reject)
    Engine->>Workflow_Finalize: pull_request_target (closed, merged=false)
    
    Note over Workflow_Finalize: squash merge 단계 스킵됨 (if: merged == true)
    
    par TC Public 브랜치 삭제
        Workflow_Finalize->>TC_Pub: tc/pr-300 브랜치 존재 확인
        TC_Pub-->>Workflow_Finalize: 존재함
        Workflow_Finalize->>TC_Pub: DELETE /git/refs/heads/tc/pr-300
        TC_Pub-->>Workflow_Finalize: 삭제 완료
    and TC Private 브랜치 삭제
        Workflow_Finalize->>TC_Priv: tc/pr-300 브랜치 삭제
        TC_Priv-->>Workflow_Finalize: 삭제 완료
    end
    
    Workflow_Finalize-->>Engine: 완료
    Note over Engine: TC 브랜치만 정리됨 (develop에는 변경 없음)
```

---

### 시나리오 C1-C2: 인증/권한 실패

```mermaid
sequenceDiagram
    participant Workflow as Workflow Job
    participant GitHub as GitHub API
    participant PR as Engine PR

    Workflow->>Workflow: GitHub App Token 생성 시도
    alt 인증 실패 (C1: Invalid credentials)
        GitHub-->>Workflow: 401 Unauthorized
        Workflow->>Workflow: "::error::Failed to create GitHub App token"
        Workflow->>PR: "⚠️ **TC Branch Sync Failed**..." 코멘트
        Note over Workflow: exit 1 (workflow 실패)
    else 권한 없음 (C2: Repository access denied)
        GitHub-->>Workflow: 403 Forbidden
        Workflow->>Workflow: "::error::Repository access denied"
        Workflow->>PR: "⚠️ **TC Branch Sync Failed**..." 코멘트
        Note over Workflow: exit 1 (workflow 실패)
    end
```

---

### 시나리오 C5: TC Squash Merge 충돌

```mermaid
sequenceDiagram
    participant Workflow_Finalize as tc-branch-finalize.yml
    participant TC_Pub as cubrid-testcases
    participant PR as Engine PR

    Note over Workflow_Finalize: PR #400 머지 시도
    Workflow_Finalize->>TC_Pub: clone develop
    Workflow_Finalize->>TC_Pub: git checkout develop
    Workflow_Finalize->>TC_Pub: git fetch tc/pr-400
    Workflow_Finalize->>TC_Pub: git merge --squash tc/pr-400
    
    Note over TC_Pub: 충돌 발생!<br/>develop에 다른 TC 변경이 먼저 머지됨
    TC_Pub-->>Workflow_Finalize: CONFLICT (exit code ≠ 0)
    
    Workflow_Finalize->>Workflow_Finalize: git merge --abort
    Workflow_Finalize->>PR: "⚠️ **TC Branch Finalize Failed**..." 코멘트
    Workflow_Finalize->>PR: "Manual recovery:" + 복구 명령어 제공
    Note over Workflow_Finalize: exit 1 (workflow 실패)
    
    Note over PR: 개발자가 수동으로:<br/>1. TC 저장소에서 수동 merge<br/>2. Workflow 재실행 또는<br/>3. 브랜치 수동 삭제
```

---

### 시나리오 C6: Revert 충돌

```mermaid
sequenceDiagram
    participant Workflow_Sync as tc-branch-sync.yml
    participant TC_Pub as cubrid-testcases
    participant PR as Engine PR

    Note over Workflow_Sync: Revert PR #500 열림 (원본 PR #499)
    Workflow_Sync->>TC_Pub: clone & checkout develop
    Workflow_Sync->>TC_Pub: 원본 TC merge 커밋 abc1234 발견
    Workflow_Sync->>TC_Pub: git checkout -b tc/pr-500 develop
    
    Workflow_Sync->>TC_Pub: git revert abc1234
    Note over TC_Pub: 충돌 발생!<br/>tc/pr-499 브랜치가 수정한 파일이<br/>develop에서 이미 변경됨
    TC_Pub-->>Workflow_Sync: CONFLICT
    
    Workflow_Sync->>Workflow_Sync: git revert --abort
    Workflow_Sync->>PR: "⚠️ **TC Revert Failed**..." 코멘트
    Workflow_Sync->>PR: Manual steps: 1. Clone... 2. Find... 3. Push
    Note over Workflow_Sync: exit 1 (workflow 실패)
```

---

### 시나리오 C7: 브랜치 삭제 실패 (보호 규칙)

```mermaid
sequenceDiagram
    participant Workflow_Finalize as tc-branch-finalize.yml
    participant TC_Pub as cubrid-testcases
    participant PR as Engine PR

    Note over Workflow_Finalize: PR #600 머지 완료, 삭제 단계
    Workflow_Finalize->>TC_Pub: squash merge 성공
    Workflow_Finalize->>TC_Pub: DELETE tc/pr-600
    
    Note over TC_Pub: 브랜치 보호 규칙 적용됨!<br/>관리자가 tc/pr-* 패턴 보호 설정
    TC_Pub-->>Workflow_Finalize: 403 Forbidden (Protected branch)
    
    Workflow_Finalize->>PR: "⚠️ **TC Branch Finalize Failed**..." 코멘트
    Note over Workflow_Finalize: exit 1 (workflow 실패)
    
    Note over PR: 관리자가 수동으로:<br/>1. 보호 규칙 일시 해제<br/>2. 브랜치 수동 삭제<br/>3. 또는 workflow 강제 통과
```

---

### 시나리오 D1: 두 엔진 PR 동시 머지 (Race Condition)

```mermaid
sequenceDiagram
    participant Dev1 as 개발자 A
    participant Dev2 as 개발자 B
    participant Engine as CUBRID/cubrid
    participant WF1 as finalize-#700
    participant WF2 as finalize-#701
    participant TC_Pub as cubrid-testcases

    Note over Dev1,Dev2: 동시에 또는 근접한 시간에 머지 클릭
    
    Dev1->>Engine: PR #700 머지 클릭
    Dev2->>Engine: PR #701 머지 클릭
    
    Engine->>WF1: finalize workflow 시작
    Engine->>WF2: finalize workflow 시작
    
    par PR #700 처리
        WF1->>TC_Pub: git clone develop (상태: commit A)
        WF1->>TC_Pub: tc/pr-700 squash merge
        WF1->>TC_Pub: git push origin develop
        Note over WF1,TC_Pub: 성공! (commit B 생성)
    and PR #701 처리 (경쟁)
        WF2->>TC_Pub: git clone develop (상태: 여전히 commit A)
        WF2->>TC_Pub: tc/pr-701 squash merge
        WF2->>TC_Pub: git push origin develop
        Note over WF2,TC_Pub: 실패! (non-fast-forward)<br/>이미 commit B가 존재
        WF2-->>Engine: "⚠️ **TC Branch Finalize Failed**..."
    end
    
    Note over Engine: 결과:<br/>- PR #700: TC 머지 성공<br/>- PR #701: TC 머지 실패 (수동 복구 필요)
```

**해결책:** per-repo concurrency 추가 고려
```yaml
concurrency:
  group: tc-develop-${{ matrix.tc-repo }}
  cancel-in-progress: false
```

---

### 시나리오 D2: PR 빠르게 재오픈/닫힘 반복

```mermaid
sequenceDiagram
    participant Dev as 개발자
    participant Engine as CUBRID/cubrid
    participant Sync as sync workflow
    participant Finalize as finalize workflow
    participant TC_Pub as cubrid-testcases

    Dev->>Engine: PR #800 열림
    Engine->>Sync: sync #1 실행 (tc/pr-800 생성)
    Sync->>TC_Pub: 브랜치 생성
    
    Dev->>Engine: PR #800 즉시 닫힘
    Engine->>Finalize: finalize #1 실행 (tc/pr-800 삭제)
    Finalize->>TC_Pub: 브랜치 삭제
    
    Dev->>Engine: PR #800 즉시 재오픈
    Engine->>Sync: sync #2 실행
    Note over Sync: sync #1가 아직 완료되지 않음?<br/>또는 finalize #1가 아직 실행 중?
    
    alt sync #2가 삭제 전에 실행됨
        Sync->>TC_Pub: 브랜치 존재 확인
        TC_Pub-->>Sync: 존재함 (finalize #1가 아직 안 끝남)
        Note over Sync: 스킵 처리됨!<br/>tc/pr-800가 곧 삭제될 예정
    else sync #2가 삭제 후에 실행됨
        Sync->>TC_Pub: 브랜치 존재 확인
        TC_Pub-->>Sync: 없음
        Sync->>TC_Pub: 새로운 tc/pr-800 생성
    end
    
    Note over Engine: concurrency: tc-branch-sync-PR#800 설정으로<br/>sync #1과 sync #2는 순차 실행됨
```

---

### 시나리오 D3: Revert PR이 원본 TC 머지 전에 열림

```mermaid
sequenceDiagram
    participant Dev as 개발자
    participant Engine as CUBRID/cubrid
    participant Workflow_Sync as tc-branch-sync.yml
    participant TC_Pub as cubrid-testcases
    participant PR_Orig as 원본 PR #900
    participant PR_Revert as Revert PR #901

    Note over Engine,TC_Pub: 원본 PR #900가 아직 열려있음<br/>TC 브랜치 tc/pr-900는 존재하나<br/>develop에는 아직 머지되지 않음
    
    Dev->>Engine: Revert PR #901 열림 (Reverts #900)
    Engine->>Workflow_Sync: sync 실행
    Workflow_Sync->>Workflow_Sync: Revert PR #900 감지
    
    Workflow_Sync->>TC_Pub: "tc/pr-900" 커밋 검색 in develop
    TC_Pub-->>Workflow_Sync: 검색 결과 없음 (아직 머지 안 됨)
    
    Note over Workflow_Sync: 처리 방식:<br/>"No TC merge commit found" →<br/>empty branch 생성
    Workflow_Sync->>TC_Pub: tc/pr-901 생성 (빈 브랜치)
    
    Note over Dev,TC_Pub: ⚠️ 경고: 이 상황은 정상이 아님!<br/>Revert PR은 원본 PR이 머지된 후에만 의미 있음<br/>개발자에게 코멘트 안내 필요할 수 있음
```

---

### 시나리오 E1-E2: PR 타입 변경 (일반 ↔ Revert)

```mermaid
sequenceDiagram
    participant Dev as 개발자
    participant Engine as CUBRID/cubrid
    participant Workflow_Sync as tc-branch-sync.yml
    participant TC_Pub as cubrid-testcases

    %% E1: Normal → Revert
    Note over Engine: 시나리오 E1: 일반 PR → Revert PR 변경
    Dev->>Engine: PR #1000 열림 (일반 PR)
    Engine->>Workflow_Sync: sync #1 실행
    Workflow_Sync->>TC_Pub: tc/pr-1000 생성
    
    Dev->>Engine: PR #1000 Body 수정: "Reverts #999" 추가
    Engine->>Workflow_Sync: sync #2 (synchronize) 실행
    Workflow_Sync->>Workflow_Sync: 타입 변경 감지 (Normal → Revert)
    
    Note over Workflow_Sync: tc/pr-1000이 이미 존재함<br/>새로운 처리 불가 (스킵됨)
    Workflow_Sync->>TC_Pub: 브랜치 존재 확인
    TC_Pub-->>Workflow_Sync: 존재함
    Workflow_Sync->>Engine: "::notice::Branch already exists. Skipping."
    
    Note over Dev,TC_Pub: ⚠️ 문제: Revert 로직이 실행되지 않음!<br/>tc/pr-1000는 일반 브랜치 상태 유지<br/>개발자가 수동으로 revert 작업 필요
    
    ---
    
    %% E2: Revert → Normal
    Note over Engine: 시나리오 E2: Revert PR → 일반 PR 변경
    Dev->>Engine: PR #1001 열림 (Revert PR)
    Engine->>Workflow_Sync: sync #1 실행
    Workflow_Sync->>TC_Pub: tc/pr-1001 생성 + 원본 커밋 revert
    
    Dev->>Engine: PR #1001 Body 수정: "Reverts #..." 제거
    Engine->>Workflow_Sync: sync #2 (synchronize) 실행
    Workflow_Sync->>Workflow_Sync: 타입 변경 감지 (Revert → Normal)
    
    Workflow_Sync->>TC_Pub: 브랜치 존재 확인
    TC_Pub-->>Workflow_Sync: 존재함 (revert 커밋 포함)
    Workflow_Sync->>Engine: "::notice::Branch already exists. Skipping."
    
    Note over Dev,TC_Pub: 결과: tc/pr-1001는 revert된 상태로 유지됨<br/>개발자가 의도한 것과 다른 결과 가능
```

---

## 4. 상태 전이 표

### TC 브랜치 수명 주기 상태 전이

| 현재 상태 | 이벤트 | 다음 상태 | 조건 |
|-----------|--------|-----------|------|
| **NO_BRANCH** | PR Opened (Normal) | CREATED | 브랜치 생성 성공 |
| **NO_BRANCH** | PR Opened (Normal) | NO_BRANCH | 브랜치 이미 존재 |
| **NO_BRANCH** | PR Opened (Revert) | CREATED | Revert 성공 |
| **NO_BRANCH** | PR Opened (Revert) | CREATED (empty) | 원본 커밋 없음 |
| **CREATED** | PR Synchronize | CREATED | 브랜치 존재 (스킵) |
| **CREATED** | CI Test | TESTING | CI 트리거됨 |
| **TESTING** | CI Complete | TESTING | 결과无关 |
| **CREATED/TESTING** | PR Closed (not merged) | NO_BRANCH | 브랜치 삭제 |
| **CREATED/TESTING** | PR Closed (merged) | MERGED_TO_DEVELOP | squash merge 성공 |
| **CREATED/TESTING** | PR Closed (merged) | MERGE_FAILED | 충돌 발생 |
| **MERGED_TO_DEVELOP** | Delete Step | DELETED | 삭제 성공 |
| **MERGED_TO_DEVELOP** | Delete Step | MERGED_TO_DEVELOP | 삭제 실패 |
| **CREATED** | Revert PR Processed | REVERTED | Revert 커밋 푸시됨 |

---

## 5. 오류 복구 매뉴얼

### 시나리오별 복구 절차

#### C5: TC Merge 충돌 복구
```bash
# 1. 수동 머지 수행
BRANCH="tc/pr-1234"
git clone -b develop https://github.com/CUBRID/cubrid-testcases.git
cd cubrid-testcases
git fetch origin "${BRANCH}:${BRANCH}"
git merge --squash "${BRANCH}"
# 충돌 해결 후
git commit -m "[CBRD-XXXX] Merge TC branch '${BRANCH}' into develop"
git push origin develop

# 2. 브랜치 삭제
git push origin --delete "${BRANCH}"
```

#### C6: Revert 충돌 복구
```bash
# 1. Revert 브랜치 수동 생성
BRANCH="tc/pr-1234"
ORIGINAL_PR="123"
git clone -b develop https://github.com/CUBRID/cubrid-testcases.git
cd cubrid-testcases

# 2. 원본 TC merge 커밋 찾기
git log --oneline --grep="tc/pr-${ORIGINAL_PR}" develop
# 커밋 SHA 확인 (예: abc1234)

# 3. Revert 브랜치 생성 및 revert 수행
git checkout -b "${BRANCH}" develop
git revert -m 1 abc1234  # 또는 git revert abc1234 (squash인 경우)
# 충돌 해결 후
git push origin "${BRANCH}"
```

#### C7: 보호된 브랜치 삭제
```bash
# 관리자 권한으로 API 호출
curl -X DELETE \
  -H "Authorization: Bearer ${ADMIN_TOKEN}" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/CUBRID/cubrid-testcases/git/refs/heads/tc/pr-1234"
```

---

## 6. 이벤트 흐름 요약

### 전체 시스템 이벤트 다이어그램

```mermaid
flowchart TB
    subgraph "PR Lifecycle"
        PR_OPEN["🔵 PR Opened"]
        PR_SYNC["🟡 PR Synchronize"]
        PR_CLOSE_MERGED["🟢 PR Closed (merged)"]
        PR_CLOSE_NOT_MERGED["🔴 PR Closed (not merged)"]
    end

    subgraph "tc-branch-sync.yml"
        DETECT["Detect PR Type"]
        CREATE_BRANCH["Create TC Branch"]
        REVERT_BRANCH["Revert TC Commit"]
        SKIP_EXISTING["Skip (exists)"]
    end

    subgraph "tc-branch-finalize.yml"
        CHECK_MERGED{"merged?"}
        SQUASH_MERGE["Squash Merge to develop"]
        DELETE_BRANCH["Delete TC Branch"]
        DIRECT_DELETE["Direct Delete"]
    end

    subgraph "TC Repositories"
        TC_BRANCH[(tc/pr-N)]
        TC_DEVELOP[(develop)]
    end

    PR_OPEN --> DETECT
    PR_SYNC --> DETECT
    
    DETECT -->|Normal| CREATE_BRANCH
    DETECT -->|Revert| REVERT_BRANCH
    DETECT -->|Type Change| SKIP_EXISTING
    
    CREATE_BRANCH --> TC_BRANCH
    REVERT_BRANCH --> TC_BRANCH
    SKIP_EXISTING --> TC_BRANCH
    
    TC_BRANCH --> PR_CLOSE_MERGED
    TC_BRANCH --> PR_CLOSE_NOT_MERGED
    
    PR_CLOSE_MERGED --> CHECK_MERGED
    PR_CLOSE_NOT_MERGED --> CHECK_MERGED
    
    CHECK_MERGED -->|true| SQUASH_MERGE
    CHECK_MERGED -->|false| DIRECT_DELETE
    
    SQUASH_MERGE --> TC_DEVELOP
    SQUASH_MERGE --> DELETE_BRANCH
    DIRECT_DELETE --> TC_BRANCH
    DELETE_BRANCH --> TC_BRANCH
    
    TC_DEVELOP -->|after merge| DELETE_BRANCH
```

---

## 7. 체크포인트 및 검증

### 각 단계별 검증 항목

| 단계 | 검증 항목 | 성공 기준 |
|------|-----------|-----------|
| **PR 파싱** | `is_revert`, `original_pr_number`, `pr_header` 추출 | 모든 출력값이 예상 범위 내 |
| **토큰 생성** | GitHub App 인증 | 200 OK, 유효한 토큰 반환 |
| **브랜치 생성** | tc/pr-N 생성 | HTTP 201, refs/heads/tc/pr-N 존재 |
| **Revert** | 원본 커밋 발견 및 revert | Revert 커밋이 tc/pr-N에 존재 |
| **Squash Merge** | tc/pr-N → develop | develop에 새 커밋 생성 |
| **브랜치 삭제** | tc/pr-N 제거 | HTTP 204, refs/heads/tc/pr-N 없음 |
| **코멘트** | 실패 시 PR 코멘트 | PR에 에러 메시지 표시됨 |

---

## 8. 빈도 및 우선순위

### 시나리오 발생 빈도 추정

| 시나리오 | 예상 빈도 | 우선순위 |
|----------|-----------|----------|
| A1 (정상 일반 PR) | **매우 높음 (80%)** | P0 |
| A2 (정상 Revert PR) | **낮음 (5%)** | P1 |
| B1 (브랜치 이미 존재) | **중간 (10%)** | P1 |
| B2 (TC 없는 Revert) | **낮음 (3%)** | P2 |
| B3 (닫힘 without 머지) | **낮음 (2%)** | P1 |
| C1-C2 (인증 실패) | **매우 낮음 (<1%)** | P0 (치명적) |
| C5 (Merge 충돌) | **낮음 (1%)** | P1 |
| C6 (Revert 충돌) | **매우 낮음 (<1%)** | P2 |
| C7 (삭제 실패) | **매우 낮음 (<1%)** | P2 |
| D1 (동시 머지) | **매우 낮음 (<1%)** | P1 |
| E1-E2 (타입 변경) | **매우 낮음 (<1%)** | P2 |

---

*문서 버전: 1.0*
*생성일: 2026-02-27*
*적용 워크플로우: tc-branch-sync.yml, tc-branch-finalize.yml*
