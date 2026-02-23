# CUBRID Engine PR ↔ TC 브랜치 트랜잭션 자동화

## 배경
엔진 PR은 항상 TC repo의 `develop` 브랜치를 기준으로 테스트된다.
다른 개발자의 TC 변경이 먼저 `develop`에 머지되면 관계없는 엔진 PR에서도 테스트 실패가 발생하는 문제가 있었다.
이를 해결하기 위해 **엔진 PR 번호 기반의 독립 TC 브랜치**를 자동으로 관리하는 구조를 도입한다.

---

## 대상 레포지토리

| 역할 | 레포지토리 |
|------|-----------|
| 엔진 (Control) | `CUBRID/cubrid` |
| TC 공개 | `CUBRID/cubrid-testcases` |
| TC 비공개 | `CUBRID/cubrid-testcases-private-ex` |

---

## 상태 전이 구조

```
[Engine PR Opened / Reopened / Synchronize]
            ↓
[tc/pr-<N> 브랜치 자동 생성 - 두 TC repo]
            ↓
   [CI: tc/pr-<N> 브랜치로 테스트]
            ↓
    ┌───────────────────┐
    │ merged == true    │  → tc/pr-<N>를 develop에 머지 후 브랜치 삭제
    │ merged == false   │  → tc/pr-<N> 브랜치 삭제만
    └───────────────────┘
```

---

## 구현된 파일

### 1. `tc-branch-sync.yml` — TC 브랜치 생성 및 유지

- **트리거**: `pull_request_target` (opened, reopened, synchronize)
- **대상 브랜치**: `develop`, `release/11.**`, `feature/**`
- **동작**:
  - 엔진 PR이 열리거나 갱신될 때 두 TC repo에 `tc/pr-<PR_NUMBER>` 브랜치가 없으면 `develop` 기준으로 생성
  - 이미 존재하면 skip (개발자가 TC를 이미 수정 중일 수 있으므로)
  - `cubrid-testcases`와 `cubrid-testcases-private-ex`를 독립적으로 처리 (`continue-on-error`)
  - 어느 한 쪽이라도 실패하면 전체 실패로 간주하고 엔진 PR에 코멘트 게시
- **concurrency**: 같은 PR 번호에 대한 중복 실행은 이전 것을 취소하고 최신 실행만 유지

### 2. `tc-branch-finalize.yml` — TC 브랜치 머지 및 삭제

- **트리거**: `pull_request_target` (closed)
- **대상 브랜치**: `develop`, `release/11.**`, `feature/**`
- **동작 (PR이 merge된 경우)**:
  - 두 TC repo에서 `tc/pr-<N>`을 `develop`에 `--no-ff` 머지
  - 머지 완료 후 `tc/pr-<N>` 브랜치 삭제
  - `tc/pr-<N>` 브랜치가 없으면 (TC 변경 없음) skip
  - 머지 실패(conflict 등) 시 엔진 PR에 수동 처리 요청 코멘트 게시
- **동작 (PR이 reject/close된 경우)**:
  - 두 TC repo에서 `tc/pr-<N>` 브랜치 삭제만 수행
  - 브랜치 삭제는 `git push --delete` 대신 **GitHub REST API** (`DELETE /repos/.../git/refs/heads/...`) `curl` 호출 방식 사용 (로컬 git repo context 불필요)
  - 브랜치가 없으면 skip

---

## 인증 설정 (필수)

두 워크플로우 모두 TC repo에 push/delete 권한이 필요하며, **GitHub App**을 통해 인증한다.
워크플로우 내에서 `actions/create-github-app-token@v1` 액션으로 일회용 설치 토큰을 발급한다.

### GitHub App 생성 및 설정 절차

1. GitHub Organization(`CUBRID`) 설정에서 GitHub App을 생성한다.
2. App에 아래 권한을 부여한다.
   | 권한 항목 | 수준 |
   |---------|------|
   | Contents | Write |
3. App을 두 TC repo에 설치한다.
   - `CUBRID/cubrid-testcases`
   - `CUBRID/cubrid-testcases-private-ex`
4. 엔진 repo(`CUBRID/cubrid`)의 **Settings → Secrets → Actions**에 아래 시크릿 두 개를 추가한다.
   | Secret 이름 | 값 |
   |------------|-----|
   | `TC_APP_ID` | GitHub App의 App ID (숫자) |
   | `TC_APP_PRIVATE_KEY` | GitHub App의 Private Key (PEM 전체 내용) |

---

## 보안 고려사항

두 워크플로우 모두 `pull_request_target`을 사용한다.
이 트리거는 fork PR에서도 시크릿에 접근할 수 있어 보안 위험이 있으나,
**PR 코드를 checkout하거나 실행하지 않고** `git ls-remote`, `git clone`, `git push` 등의 검증된 git 명령어만 사용하므로 안전하다.

---

## 테스트 결과 (`tw-kang` fork, 2026-02-23)

**테스트 환경**

| 항목 | 값 |
|------|-----|
| 엔진 fork | `tw-kang/cubrid` |
| TC 공개 fork | `tw-kang/cubrid-testcases` |
| TC 비공개 fork | `tw-kang/cubrid-testcases-private-ex` |
| 개발 브랜치 | `cubridqa-1320` (default branch) |
| 테스트 base 브랜치 | `test/sync-tc` |

**결과 요약**

| 시나리오 | 결과 | PR | Run ID |
|---------|------|-----|--------|
| A: PR open → TC 브랜치 생성 | ✅ PASS | #22 | `22306576636` |
| D: TC 브랜치 이미 존재 (skip) | ✅ PASS | #22 (synchronize) | `22306793474` |
| B: PR merge → develop 머지 + 삭제 | ✅ PASS | #22 merged | `22306870071` |
| C: PR close (reject) → 브랜치 삭제 | ✅ PASS | #26 closed | `22307785553` |
| E: App 미설치 → PR 코멘트 실패 알림 | ✅ PASS | #27 | `22307844958` |

---

## 테스트 중 발견된 버그 및 수정

### Bug #1: `git push --delete` — 로컬 repo context 없음

- **파일**: `tc-branch-finalize.yml`
- **증상**: PR closed (without merge) 이벤트 시 `fatal: not a git repository` 오류로 `tc/pr-N` 브랜치 삭제 실패
- **원인**: `git push <url> --delete <branch>`는 로컬 git repo context를 요구하나, 해당 스텝은 checkout 없이 실행됨
- **수정**: GitHub REST API `DELETE /repos/.../git/refs/heads/...` 를 `curl`로 직접 호출하는 방식으로 교체 (커밋 `b569f22de`)

### 중요 발견: `pull_request_target` 워크플로우 파일 읽기 동작

`pull_request_target` 이벤트는 PR의 **base 브랜치가 아닌 레포지토리의 default branch**에서 워크플로우 파일을 읽는다.

- ❌ 잘못된 이해: `test/sync-tc` (PR base branch) 에서 읽음
- ✅ 실제 동작: `cubridqa-1320` (default branch) 에서 읽음

따라서 워크플로우 파일을 수정할 때는 **반드시 default branch에 push**해야 변경이 반영된다.

---

## CI 수정 사항 (별도 작업 필요)

CI 워크플로우에서 TC repo를 checkout할 때 `tc/pr-<N>` 브랜치가 있으면 해당 브랜치를, 없으면 `develop`을 fallback으로 사용하도록 수정이 필요하다.

추가로, TC 브랜치에 conflict가 있을 때 엔진 PR 머지 자체를 차단하려면 CI에 아래 dry-run 체크를 추가해야 한다.
GitHub Branch Protection의 "Require status checks to pass"와 연계하면, CI 실패 시 자동으로 머지가 차단된다.
자세한 코드 샘플은 `summary.txt`의 **REMAINING CI WORK** 섹션을 참고한다.

---

## 미구현 항목 (향후 과제)

| 항목 | 설명 |
|------|------|
| develop 자동 동기화 | 개발자가 엔진 PR에 develop을 머지할 때, 대응 TC 브랜치에도 develop을 자동 머지 |
| Revert 자동화 | 머지된 엔진 PR이 revert될 때 TC repo에도 자동으로 revert 처리 또는 알림 코멘트 게시 |