# 개인 Fork 레포지토리 테스트 가이드

워크플로우(`tc-branch-sync.yml`, `tc-branch-finalize.yml`)를 `tw-kang` fork 환경에서 검증하기 위한 환경 구성 및 시나리오별 테스트 절차이다.

---

## 레포지토리 및 브랜치 구성

| 역할 | 레포지토리 | 브랜치 |
|------|-----------|--------|
| 엔진 (Control) | `tw-kang/cubrid` | 개발: `cubridqa-1320` / 테스트 base: `test/sync-tc` |
| TC 공개 | `tw-kang/cubrid-testcases` | `develop` |
| TC 비공개 | `tw-kang/cubrid-testcases-private-ex` | `develop` |

### 브랜치 역할 구분

- **`cubridqa-1320`**: 워크플로우 파일 개발 브랜치. 수정 및 커밋은 이 브랜치에서 한다.
- **`test/sync-tc`**: 테스트 전용 base 브랜치. 이 브랜치를 대상으로 PR을 열어 워크플로우를 트리거한다.
  `cubridqa-1320`에서 분기하며, 워크플로우 파일이 그대로 포함된다.

```
cubridqa-1320  ──(분기)──▶  test/sync-tc   ← 테스트 PR이 이 브랜치를 base로 사용
      ↑
  워크플로우 개발
```

---

## 목차

1. [초기 셋업](#1-초기-셋업)
2. [GitHub App 생성 및 설치](#2-github-app-생성-및-설치)
3. [Secrets 등록](#3-secrets-등록)
4. [테스트 시나리오](#4-테스트-시나리오)
   - [Scenario A: PR open → TC 브랜치 생성](#scenario-a-pr-open--tc-브랜치-생성)
   - [Scenario B: PR merge → TC 브랜치 develop 머지 + 삭제](#scenario-b-pr-merge--tc-브랜치-develop-머지--삭제)
   - [Scenario C: PR close (reject) → TC 브랜치 삭제](#scenario-c-pr-close-reject--tc-브랜치-삭제)
   - [Scenario D: TC 브랜치 이미 존재 (skip)](#scenario-d-tc-브랜치-이미-존재-skip)
   - [Scenario E: App 미설치 → PR 코멘트 실패 알림](#scenario-e-app-미설치--pr-코멘트-실패-알림)
5. [테스트 후 정리](#5-테스트-후-정리)
6. [주의사항](#6-주의사항)

---

## 1. 초기 셋업

### 1-1. TC fork에 `develop` 브랜치 존재 확인

워크플로우는 TC 레포의 `develop` 브랜치를 기점으로 `tc/pr-<N>` 브랜치를 생성하고, PR 머지 시 `develop`에 push한다.

```bash
gh api repos/tw-kang/cubrid-testcases/branches/develop --jq '.name' 2>/dev/null \
  && echo "OK" || echo "develop 브랜치 없음"

gh api repos/tw-kang/cubrid-testcases-private-ex/branches/develop --jq '.name' 2>/dev/null \
  && echo "OK" || echo "develop 브랜치 없음"
```

`develop` 브랜치가 없으면 upstream에서 동기화한다.

```bash
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

### 1-2. `test/sync-tc` 브랜치 생성 및 push

`test/sync-tc`는 테스트 PR의 base 브랜치이자, 워크플로우 파일이 실제로 읽히는 브랜치다.
`cubridqa-1320`에서 분기하여 `tw-kang/cubrid`에 push한다.

```bash
cd cubrid   # tw-kang/cubrid 로컬 디렉터리

git checkout cubridqa-1320
git pull twkang cubridqa-1320

git checkout -b test/sync-tc
git push twkang test/sync-tc
```

> **이후 워크플로우 파일을 수정하면 `cubridqa-1320`에 커밋하고, `test/sync-tc`에 merge하여 반영한다.**

```bash
# 워크플로우 수정 후 반영 패턴
git checkout cubridqa-1320
# ... 파일 수정 및 커밋 ...
git checkout test/sync-tc
git merge cubridqa-1320
git push twkang test/sync-tc
```

### 1-3. 현재 워크플로우 파일 상태 확인

워크플로우 파일(`tc-branch-sync.yml`, `tc-branch-finalize.yml`)은 이미 `tw-kang` fork 기준으로 수정되어 있다.

수정된 내용:
- `on.branches`에 `test/sync-tc` 추가 (테스트 트리거)
- `TC_PUB_REPO`: `tw-kang/cubrid-testcases`
- `TC_PRIV_REPO`: `tw-kang/cubrid-testcases-private-ex`
- `owner`: `tw-kang`

---

## 2. GitHub App 생성 및 설치

### 2-1. 개인 계정용 GitHub App 생성
1. **https://github.com/settings/apps** 접속
2. **New GitHub App** 클릭
3. 기본 정보 입력
   - App name: `tc-branch-manager-test` (임의)
   - Homepage URL: `https://github.com/tw-kang` (임의)
   - Webhook: **Active 해제**
4. **Repository permissions** → `Contents`: **Read and write** 선택
5. **Where can this GitHub App be installed?**: `Only on this account`
6. **Create GitHub App** 클릭
> 생성 후 페이지 상단의 **App ID** (숫자)를 복사해 둔다.

### 2-2. Private Key 생성

앱 설정 페이지 하단 **Private keys** → **Generate a private key** 클릭.
`.pem` 파일이 다운로드된다.
### 2-3. App을 두 TC fork에 설치

앱 설정 페이지 좌측 **Install App** → `tw-kang` 계정 선택 → **Only select repositories** →
`cubrid-testcases`, `cubrid-testcases-private-ex` 두 개 선택 → **Install**.

> 엔진 fork(`tw-kang/cubrid`)에는 설치 불필요.

---

## 3. Secrets 등록

`tw-kang/cubrid`의 **Settings → Secrets and variables → Actions → New repository secret** 에서 추가한다.
| Secret 이름 | 값 |
|------------|-----|
| `TC_APP_ID` | 2-1에서 복사한 App ID (숫자만) |
| `TC_APP_PRIVATE_KEY` | `.pem` 파일 전체 내용 (헤더/푸터 포함) |
```
-----BEGIN RSA PRIVATE KEY-----
MIIEow...
-----END RSA PRIVATE KEY-----
```

> fork 레포의 Actions Secrets는 upstream과 독립적이다. upstream의 secrets는 상속되지 않는다.

---

## 4. 테스트 시나리오

모든 시나리오에서 **`tw-kang/cubrid`에 `test/sync-tc`를 base로 PR을 생성**하는 것이 트리거다.

> **⚠️ PR 생성 시 반드시 `--repo tw-kang/cubrid`와 `--base test/sync-tc`를 지정한다.**
> 지정하지 않으면 upstream(`CUBRID/cubrid`)으로 PR이 생성될 수 있다.

---

### Scenario A: PR open → TC 브랜치 생성

**목적**: `pull_request_target: opened` 트리거 검증. `tc/pr-<N>` 브랜치가 두 TC fork에 생성되는지 확인.

**절차**

```bash
cd cubrid
git checkout cubridqa-1320
git pull twkang cubridqa-1320
git checkout -b feature/test-scenario-a
echo "test" >> dummy.txt
git add dummy.txt
git commit -m "test: scenario A"
git push twkang feature/test-scenario-a

gh pr create \
  --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/test-scenario-a \
  --title "test: scenario A" \
  --body "TC 브랜치 생성 테스트"
```

**기록해 둘 것**: 출력에서 PR 번호 확인 (예: `#7`)

**검증**

```bash
# Actions 실행 확인 (완료까지 약 30~60초 소요)
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml
# TC fork에 브랜치 생성 여부 확인
gh api repos/tw-kang/cubrid-testcases/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'

gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'
```

**기대 결과**
- `tc-branch-sync.yml` 워크플로우 `success`
- `tw-kang/cubrid-testcases` 브랜치 목록: `tc/pr-7` 존재
- `tw-kang/cubrid-testcases-private-ex` 브랜치 목록: `tc/pr-7` 존재

---

### Scenario B: PR merge → TC 브랜치 develop 머지 + 삭제

**목적**: `pull_request_target: closed` (merged=true) 트리거 검증. TC 브랜치가 `develop`에 `--no-ff` 머지되고 삭제되는지 확인.

**사전 조건**: Scenario A가 완료되어 `tc/pr-<N>` 브랜치가 이미 존재해야 한다.

**선택: TC 브랜치에 커밋 추가 (머지 흔적 확인용)**

```bash
git clone https://github.com/tw-kang/cubrid-testcases.git tc-pub-clone
cd tc-pub-clone
git checkout tc/pr-7
echo "tc change for pr-7" >> testcase.txt
git add testcase.txt
git commit -m "test: add TC for engine PR #7"
git push twkang tc/pr-7
cd ..
```

**PR 머지**

```bash
gh pr merge 7 \
  --repo tw-kang/cubrid \
  --merge \
  --delete-branch
```

**검증**

```bash
gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml

# develop 최신 커밋 확인 (--no-ff merge commit 있어야 함)
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop" \
  --jq '.[0:3] | .[] | {sha: .sha[0:7], message: .commit.message}'
gh api repos/tw-kang/cubrid-testcases/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'

gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'
```

**기대 결과**
- `tc-branch-finalize.yml` 워크플로우 `success`
- `tw-kang/cubrid-testcases` `develop` 최신 커밋: `Merge TC branch 'tc/pr-7' into develop (Engine PR #7)`
- `tc/pr-7` 브랜치: 두 TC fork 모두 삭제됨

---

### Scenario C: PR close (reject) → TC 브랜치 삭제

**목적**: `pull_request_target: closed` (merged=false) 트리거 검증. TC 브랜치가 `develop` 머지 없이 삭제되는지 확인.

**절차**

```bash
cd cubrid
git checkout cubridqa-1320 && git pull twkang cubridqa-1320
git checkout -b feature/test-scenario-c
echo "test" >> dummy2.txt
git add dummy2.txt
git commit -m "test: scenario C"
git push twkang feature/test-scenario-c
gh pr create \
  --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/test-scenario-c \
  --title "test: scenario C (will be rejected)" \
  --body "PR 거절 시 TC 브랜치 삭제 테스트"
```

`tc-branch-sync.yml`이 실행되어 `tc/pr-<N>` 브랜치가 생성되면, PR을 Close한다.

```bash
gh pr list --repo tw-kang/cubrid
gh pr close <PR_NUMBER> --repo tw-kang/cubrid
```

**검증**

```bash
gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml

gh api repos/tw-kang/cubrid-testcases/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'

gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'
# develop 커밋 이력 변화 없음 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'
```

**기대 결과**
- `tc-branch-finalize.yml` 워크플로우 `success`
- `develop`에 새 머지 커밋 없음
- `tc/pr-<N>` 브랜치: 두 TC fork 모두 삭제됨
---

### Scenario D: TC 브랜치 이미 존재 (skip)

**목적**: 브랜치가 이미 있을 때 중복 생성 없이 skip하는지 검증.

**절차**

열려 있는 PR에 새 커밋을 push하여 `synchronize` 이벤트를 발생시킨다.

```bash
cd cubrid
git checkout feature/test-scenario-a
echo "update" >> dummy.txt
git add dummy.txt
git commit -m "test: trigger synchronize"
git push twkang feature/test-scenario-a
```

**검증**

Actions 탭 → 방금 실행된 `tc-branch-sync.yml` 로그 → **"Sync TC branch"** 스텝 확인:

```
[info] Branch 'tc/pr-7' already exists in tw-kang/cubrid-testcases, skipping
[info] Branch 'tc/pr-7' already exists in tw-kang/cubrid-testcases-private-ex, skipping
```

**기대 결과**
- 워크플로우 `success`
- TC fork에 새 브랜치 생성 없음, 기존 브랜치 내용 유지
---

### Scenario E: App 미설치 → PR 코멘트 실패 알림

**목적**: GitHub App 토큰 발급 실패 시 PR에 코멘트가 게시되는지 검증.

**절차**
1. **https://github.com/settings/apps/tc-branch-manager-test/installations** 접속
2. 설치 항목에서 `cubrid-testcases` 레포를 접근 목록에서 제거한다.
   (Installation → Repository access → `cubrid-testcases` 제거 → Save)
3. 새 PR을 생성해 `tc-branch-sync.yml`을 트리거한다.
```bash
git checkout -b feature/test-scenario-e
echo "e" >> dummy3.txt
git add dummy3.txt && git commit -m "test: scenario E"
git push twkang feature/test-scenario-e
gh pr create \
  --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/test-scenario-e \
  --title "test: scenario E (app error)" \
  --body "App 미설치 실패 케이스 테스트"
```

**기대 결과**
- `tc-branch-sync.yml` 워크플로우 `failure`
- PR에 아래 코멘트 자동 게시:
  ```
  ⚠️ TC Branch Sync Failed (PR #N)
  ...
  Workflow logs: https://github.com/tw-kang/cubrid/actions/runs/...
  ```

**복구**: 테스트 후 App 설치에서 `cubrid-testcases`를 다시 추가한다.

---

## 5. 테스트 후 정리

### TC fork 브랜치 정리

```bash
# cubrid-testcases fork의 tc/ 브랜치 일괄 삭제
for branch in $(gh api repos/tw-kang/cubrid-testcases/branches \
    --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'); do
  gh api --method DELETE "repos/tw-kang/cubrid-testcases/git/refs/heads/${branch}"
  echo "Deleted: ${branch}"
done
# cubrid-testcases-private-ex fork의 tc/ 브랜치 일괄 삭제
for branch in $(gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
    --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'); do
  gh api --method DELETE "repos/tw-kang/cubrid-testcases-private-ex/git/refs/heads/${branch}"
  echo "Deleted: ${branch}"
done
```

### 엔진 fork PR 및 테스트 브랜치 정리

```bash
# 열려 있는 테스트 PR 모두 close
gh pr list --repo tw-kang/cubrid --json number --jq '.[].number' | \
  xargs -I{} gh pr close {} --repo tw-kang/cubrid

# 테스트용 로컬 및 원격 브랜치 삭제
for branch in feature/test-scenario-a feature/test-scenario-c feature/test-scenario-e; do
  git push twkang --delete "$branch" 2>/dev/null && echo "Deleted remote: $branch"
  git branch -D "$branch" 2>/dev/null && echo "Deleted local: $branch"
done
```

### `test/sync-tc` 브랜치 삭제 (테스트 완전 종료 시)

```bash
git push twkang --delete test/sync-tc
git branch -D test/sync-tc
```

### GitHub App 삭제 (테스트 완전 종료 시)

```
https://github.com/settings/apps/tc-branch-manager-test
→ Advanced → Delete GitHub App
```

### fork sync 복구

테스트 후 TC fork의 `develop` 브랜치를 upstream으로 재동기화한다.
(`--no-ff` 머지로 인해 fork `develop`이 upstream과 커밋 이력이 달라졌을 수 있다.)

```bash
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

---

## 6. 주의사항

### 워크플로우는 `test/sync-tc` 기준으로 실행된다

`pull_request_target`은 PR의 **base 브랜치**에 있는 워크플로우 파일을 읽는다.
워크플로우 파일 수정 후 **`test/sync-tc`에 반영해야** 테스트 PR에 적용된다.

```bash
# 워크플로우 수정 반영 패턴
git checkout cubridqa-1320
# ... 수정 및 커밋 ...
git checkout test/sync-tc
git merge cubridqa-1320
git push twkang test/sync-tc
```

### PR 생성 시 upstream 방지

`--repo`와 `--base`를 항상 명시한다.

```bash
# 올바른 예
gh pr create --repo tw-kang/cubrid --base test/sync-tc ...

# 위험: upstream(CUBRID/cubrid)으로 PR이 생성될 수 있음
gh pr create --base test/sync-tc ...
```

### fork의 Actions Secrets는 upstream과 독립

`CUBRID/cubrid`의 secrets는 fork에 상속되지 않는다.
`tw-kang/cubrid`의 **Settings → Secrets**에 별도로 등록해야 한다.

### TC fork `develop` 브랜치 보호 설정 비활성화 권장
Branch Protection이 활성화된 경우 `tc-branch-finalize.yml`의 `git push twkang develop`이 실패한다.  
테스트 단계에서는 비활성화하는 것을 권장한다.

```
https://github.com/tw-kang/cubrid-testcases → Settings → Branches
→ develop 규칙이 있으면 비활성화 또는 삭제
```
