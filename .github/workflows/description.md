# CUBRID Engine PR ↔ TC 브랜치 자동화
## 배경

엔진 PR 테스트 시 다른 개발자의 TC 변경이 `develop`에 먼저 머지되면 관계없는 PR에서도 테스트 실패가 발생한다.
엔진 PR 번호 기반의 독립 TC 브랜치(`tc/pr-<N>`)를 자동 관리하여 방지한다.

---

## 대상 레포지토리
| 역할 | 레포지토리 |
|------|-----------|
| 엔진 | `CUBRID/cubrid` |
| TC 공개 | `CUBRID/cubrid-testcases` |
| TC 비공개 | `CUBRID/cubrid-testcases-private-ex` |
---

## 상태 전이

```
PR opened/reopened/synchronize
  → tc/pr-<N> 생성 (일반 PR)
  → TC revert 커밋 push (Revert PR)

PR closed (merged)   → tc/pr-<N> → develop 머지 후 삭제
PR closed (rejected) → tc/pr-<N> 삭제
```

---

## 구현 파일

### `tc-branch-sync.yml`
- **트리거**: `pull_request_target` (opened, reopened, synchronize)
- **동작**:
  - PR body에서 `Reverts #N` 패턴 감지 → 일반 PR / Revert PR 분기
  - **일반 PR**: 두 TC repo에 대해 단일 step 내 loop으로 `tc/pr-<N>` 생성 (이미 존재하면 skip)
  - **Revert PR**: 두 TC repo에 대해 단일 step 내 loop으로 원본 PR의 TC merge commit을 `git revert -m 1`으로 취소 후 `tc/pr-<N>`에 push
    - 브랜치 존재 시 force-push, 없으면 신규 생성
    - 원본 PR에 TC 변경 없으면 silently skip
    - conflict 시 PR에 수동 처리 요청 코멘트 게시
  - 실패 시 PR에 에러 코멘트 게시 (일반 PR/Revert PR 모두 loop 결과를 요약하여 코멘트)
- **concurrency**: 동일 PR 번호 중복 실행 시 이전 것 취소 (`tc-branch-sync.yml`의 `concurrency.group = tc-branch-sync-<PR>` 사용)

### `tc-branch-finalize.yml`
- **트리거**: `pull_request_target` (closed)
- **merged == true**:
  - 두 TC repo에 대해 단일 step 내 loop으로 `tc/pr-<N>` → develop `--no-ff` 머지 후 브랜치 삭제
  - 머지 성공 후 브랜치 삭제 실패 시 워크플로우가 실패로 표시되고, PR에 **TC Branch Finalize Failed** 코멘트로 수동 정리 요청
- **merged == false**:
  - `tc/pr-<N>` 브랜치만 삭제 (GitHub REST API `DELETE /repos/.../git/refs/heads/...` 사용)
  - 삭제 실패 시 워크플로우가 실패로 표시되고, PR에 **TC Branch Delete Failed** 코멘트로 수동 정리 요청
- 브랜치 없으면 skip

### 공통 설정

- **timeout**:
  - `tc-branch-sync.yml`, `tc-branch-finalize.yml` 모두 job 레벨에 `timeout-minutes: 10`을 설정하여, 네트워크 hang 등으로 워크플로우가 장시간 점유되지 않도록 제한한다.
- **GitHub App 토큰 마스킹**:
  - `actions/create-github-app-token@v1`가 생성하는 동적 토큰은 기본 마스킹 대상이 아니므로, APP 토큰을 사용하는 모든 step의 스크립트 상단에 `echo "::add-mask::${APP_TOKEN}"`을 추가하여 로그에 노출되지 않도록 한다.

---

## 인증 설정 (필수)

`actions/create-github-app-token@v1`로 GitHub App 일회용 토큰을 발급한다.

1. CUBRID 조직에 GitHub App 생성 — 권한: **Contents: Write**
2. 두 TC repo에 App 설치
3. `CUBRID/cubrid` Secrets에 추가:

| Secret | 값 |
|--------|---|
| `TC_APP_ID` | App의 숫자 ID |
| `TC_APP_PRIVATE_KEY` | PEM 전체 내용 |

---

## 보안

`pull_request_target` 사용. fork PR에서도 시크릿 접근 가능하나, PR 코드를 checkout/실행하지 않고 `git ls-remote`, `git clone`, `git push` 등의 검증된 명령만 사용하므로 안전하다.

---

## CI 연동 (별도 작업 필요)

CI에서 TC repo checkout 시 `tc/pr-<N>` 브랜치가 있으면 사용, 없으면 `develop` fallback:

```bash
PR_BRANCH="tc/pr-${GITHUB_PULL_REQUEST_NUMBER}"
if git ls-remote --exit-code --heads \
     https://github.com/CUBRID/cubrid-testcases.git \
     "${PR_BRANCH}" 2>/dev/null; then
  TC_BRANCH="${PR_BRANCH}"
else
  TC_BRANCH="develop"
fi
git clone -b "${TC_BRANCH}" https://github.com/CUBRID/cubrid-testcases.git
```

TC conflict 시 엔진 PR 머지 차단 (branch protection 연계):

```bash
git fetch origin develop:develop
git merge --no-commit --no-ff develop || { git merge --abort; exit 1; }
git merge --abort
```

---

## 미구현 항목
| 항목 | 설명 |
|------|------|
| develop 자동 동기화 | 엔진 PR에 develop이 머지될 때 TC 브랜치에도 develop 자동 머지 |

---

## 발견된 버그 및 수정

### Bug: `git push --delete` — git repo context 없음
- **파일**: `tc-branch-finalize.yml`
- **증상**: PR closed (without merge) 시 `fatal: not a git repository` 오류로 브랜치 삭제 실패
- **수정**: GitHub REST API `DELETE /repos/.../git/refs/heads/...` curl 호출로 교체 (커밋 `b569f22de`)

### Hardening: TC finalize/delete safety

- **파일**: `tc-branch-finalize.yml`
- **개선점**:
  - TC develop 동시 머지 경합 방지: 워크플로우 전체에 `concurrency.group = tc-branch-finalize`, `cancel-in-progress: false`를 설정하여 TC 머지를 직렬화
  - 머지 후 삭제 실패 구분: 머지 성공 후 브랜치 삭제만 실패하는 경우를 감지하여, PR에 \"TC Branch Finalize Failed\" 코멘트로 머지/삭제 실패를 모두 포괄하는 메시지로 안내
  - 비머지 PR close 시 삭제 실패 처리: `Delete TC branch` step 실패를 감지하고, \"TC Branch Delete Failed\" 코멘트로 수동 정리 필요를 명시

### Note: `pull_request_target` 워크플로우 파일 읽기 위치

`pull_request_target`은 PR base 브랜치가 아닌 **default branch**에서 워크플로우 파일을 읽는다.
수정 후 반드시 default branch(`cubridqa-1320`)에 push해야 반영된다.

---

## 테스트 결과 (`tw-kang` fork)
| 시나리오 | 결과 | PR | Run ID |
|---------|------|-----|--------|
| A: PR open → TC 브랜치 생성 | ✅ PASS | #22 | `22306576636` |
| B: PR merge → develop 머지 + 삭제 | ✅ PASS | #22 | `22306870071` |
| C: PR close (reject) → 브랜치 삭제 | ✅ PASS | #26 | `22307785553` |
| D: TC 브랜치 이미 존재 (skip) | ✅ PASS | #22 synchronize | `22306793474` |
| E: App 미설치 → 실패 코멘트 게시 | ✅ PASS | #27 | `22307844958` |
| F: Revert PR → TC 자동 revert | ✅ PASS | #28/#29 | `22343057933` |
