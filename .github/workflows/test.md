# 개인 레포지토리 테스트 가이드

워크플로우(`tc-branch-sync.yml`, `tc-branch-finalize.yml`)를 개인 fork 계정에서 검증하기 위한 환경 구성 및 시나리오별 테스트 절차이다.

---

## 전제 조건

아래 세 레포지토리가 개인 계정에 이미 fork되어 있다고 가정한다.

| 역할 | upstream | 개인 fork |
|------|----------|-----------|
| 엔진 (Control) | `CUBRID/cubrid` | `<YOUR_USERNAME>/cubrid` |
| TC 공개 | `CUBRID/cubrid-testcases` | `<YOUR_USERNAME>/cubrid-testcases` |
| TC 비공개 | `CUBRID/cubrid-testcases-private-ex` | `<YOUR_USERNAME>/cubrid-testcases-private-ex` |

이하 본 문서에서 `<YOUR_USERNAME>`은 실제 GitHub 계정명으로 대체한다.

---

## 목차

1. [fork 레포 상태 확인](#1-fork-레포-상태-확인)
2. [워크플로우 파일 추가 및 수정](#2-워크플로우-파일-추가-및-수정)
3. [GitHub App 생성 및 설치](#3-github-app-생성-및-설치)
4. [Secrets 등록](#4-secrets-등록)
5. [테스트 시나리오](#5-테스트-시나리오)
   - [Scenario A: PR open → TC 브랜치 생성](#scenario-a-pr-open--tc-브랜치-생성)
   - [Scenario B: PR merge → TC 브랜치 develop 머지 + 삭제](#scenario-b-pr-merge--tc-브랜치-develop-머지--삭제)
   - [Scenario C: PR close (reject) → TC 브랜치 삭제](#scenario-c-pr-close-reject--tc-브랜치-삭제)
   - [Scenario D: TC 브랜치 이미 존재 (skip)](#scenario-d-tc-브랜치-이미-존재-skip)
   - [Scenario E: App 미설치 → PR 코멘트 실패 알림](#scenario-e-app-미설치--pr-코멘트-실패-알림)
6. [테스트 후 정리](#6-테스트-후-정리)
7. [주의사항](#7-주의사항)

---

## 1. fork 레포 상태 확인

### 1-1. TC fork에 `develop` 브랜치 존재 확인

워크플로우는 TC 레포의 `develop` 브랜치를 기점으로 `tc/pr-<N>` 브랜치를 생성하고, PR 머지 시 `develop`에 push한다. fork에 `develop` 브랜치가 반드시 존재해야 한다.

```bash
# develop 브랜치 존재 여부 확인
gh api repos/<YOUR_USERNAME>/cubrid-testcases/branches/develop --jq '.name' 2>/dev/null \
  && echo "OK" || echo "develop 브랜치 없음"

gh api repos/<YOUR_USERNAME>/cubrid-testcases-private-ex/branches/develop --jq '.name' 2>/dev/null \
  && echo "OK" || echo "develop 브랜치 없음"
```

`develop` 브랜치가 없을 경우 upstream에서 동기화한다.

```bash
# cubrid-testcases fork 동기화
gh repo sync <YOUR_USERNAME>/cubrid-testcases --branch develop

# cubrid-testcases-private-ex fork 동기화
gh repo sync <YOUR_USERNAME>/cubrid-testcases-private-ex --branch develop
```

### 1-2. TC fork의 Default Branch 확인

GitHub 웹 UI에서 아래 경로를 확인한다. Default Branch가 `develop`이 아니면 워크플로우 push 후 UI 표시가 어색할 수 있다 (기능 동작에는 무관).

```
https://github.com/<YOUR_USERNAME>/cubrid-testcases → Settings → General → Default branch
```

---

## 2. 워크플로우 파일 추가 및 수정

### 2-1. 엔진 fork를 로컬에 클론 (이미 클론되어 있다면 skip)

```bash
gh repo clone <YOUR_USERNAME>/cubrid
cd cubrid
git checkout develop
git pull origin develop
```

### 2-2. 워크플로우 파일 추가

이 작업 레포(`CUBRID/cubrid`)에서 작성된 워크플로우 파일을 엔진 fork의 `develop` 브랜치에 추가한다.

```bash
# tc-branch-sync.yml, tc-branch-finalize.yml 이 이미 .github/workflows/ 에 있다면 이 단계 skip
ls .github/workflows/tc-branch-sync.yml .github/workflows/tc-branch-finalize.yml
```

파일이 없으면 복사한다.

```bash
cp /path/to/CUBRID-cubrid/.github/workflows/tc-branch-sync.yml    .github/workflows/
cp /path/to/CUBRID-cubrid/.github/workflows/tc-branch-finalize.yml .github/workflows/
```

### 2-3. 워크플로우 파일 수정

파일 내에 CUBRID 조직명과 레포 이름이 하드코딩되어 있으므로 개인 fork 기준으로 교체한다.  
수정 대상은 두 파일 모두 동일하다.

#### `tc-branch-sync.yml` 수정

```yaml
# 수정 전
env:
  TC_PUB_REPO: CUBRID/cubrid-testcases
  TC_PRIV_REPO: CUBRID/cubrid-testcases-private-ex
  TC_BRANCH_PREFIX: tc/pr-

      - name: Generate GitHub App token
        uses: actions/create-github-app-token@v1
        with:
          app-id: ${{ secrets.TC_APP_ID }}
          private-key: ${{ secrets.TC_APP_PRIVATE_KEY }}
          owner: CUBRID
          repositories: "cubrid-testcases,cubrid-testcases-private-ex"
```

```yaml
# 수정 후
env:
  TC_PUB_REPO: <YOUR_USERNAME>/cubrid-testcases
  TC_PRIV_REPO: <YOUR_USERNAME>/cubrid-testcases-private-ex
  TC_BRANCH_PREFIX: tc/pr-

      - name: Generate GitHub App token
        uses: actions/create-github-app-token@v1
        with:
          app-id: ${{ secrets.TC_APP_ID }}
          private-key: ${{ secrets.TC_APP_PRIVATE_KEY }}
          owner: <YOUR_USERNAME>
          repositories: "cubrid-testcases,cubrid-testcases-private-ex"
```

#### `tc-branch-finalize.yml` 수정

동일하게 `env` 블록과 `create-github-app-token` 스텝을 수정한다.

```yaml
env:
  TC_PUB_REPO: <YOUR_USERNAME>/cubrid-testcases
  TC_PRIV_REPO: <YOUR_USERNAME>/cubrid-testcases-private-ex
  TC_BRANCH_PREFIX: tc/pr-

          owner: <YOUR_USERNAME>
          repositories: "cubrid-testcases,cubrid-testcases-private-ex"
```

### 2-4. 수정 완료 후 엔진 fork의 `develop` 브랜치에 push

> **중요**: `pull_request_target`은 PR의 head 브랜치가 아닌 **base 브랜치** 기준으로 워크플로우를 실행한다.  
> 워크플로우 파일은 반드시 **`develop` 브랜치**에 push해야 실제 트리거 시 반영된다.

```bash
git add .github/workflows/tc-branch-sync.yml .github/workflows/tc-branch-finalize.yml
git commit -m "test: adapt tc-branch workflows for personal fork"
git push origin develop
```

---

## 3. GitHub App 생성 및 설치

### 3-1. 개인 계정용 GitHub App 생성

1. **https://github.com/settings/apps** 접속
2. **New GitHub App** 클릭
3. 기본 정보 입력
   - App name: `tc-branch-manager-test` (임의)
   - Homepage URL: `https://github.com/<YOUR_USERNAME>` (임의)
   - Webhook: **Active 해제** (테스트 불필요)
4. **Repository permissions** → `Contents`: **Read and write** 선택
5. **Where can this GitHub App be installed?**: `Only on this account`
6. **Create GitHub App** 클릭

> 생성 완료 후 페이지 상단의 **App ID** (숫자)를 복사해 둔다.

### 3-2. Private Key 생성

같은 앱 설정 페이지 하단 **Private keys** → **Generate a private key** 클릭.  
`.pem` 파일이 다운로드된다.

### 3-3. App을 두 TC fork에 설치

앱 설정 페이지 좌측 **Install App** → 개인 계정 선택 → **Only select repositories** →  
`cubrid-testcases`, `cubrid-testcases-private-ex` (fork) 두 개 선택 → **Install**.

> 엔진 fork(`cubrid`)에는 설치 불필요. 두 TC fork에만 설치하면 된다.

---

## 4. Secrets 등록

엔진 fork(`<YOUR_USERNAME>/cubrid`)의 **Settings → Secrets and variables → Actions → New repository secret** 에서 아래 두 시크릿을 추가한다.

| Secret 이름 | 값 |
|------------|-----|
| `TC_APP_ID` | 3-1에서 복사한 App ID (숫자만) |
| `TC_APP_PRIVATE_KEY` | `.pem` 파일 전체 내용 (헤더/푸터 포함) |

```
-----BEGIN RSA PRIVATE KEY-----
MIIEow...
-----END RSA PRIVATE KEY-----
```

> fork 레포의 Actions Secrets는 upstream과 독립적이다. upstream의 secrets는 상속되지 않는다.

---

## 5. 테스트 시나리오

모든 시나리오에서 **엔진 fork(`<YOUR_USERNAME>/cubrid`)에 PR을 생성**하는 것이 트리거다.  
각 시나리오 후 **Actions 탭**과 **TC fork 브랜치 목록**에서 결과를 확인한다.

> **⚠️ PR 생성 시 반드시 `--repo <YOUR_USERNAME>/cubrid`를 지정한다.**  
> 지정하지 않으면 `gh` CLI가 upstream(`CUBRID/cubrid`)으로 PR을 생성할 수 있다.

---

### Scenario A: PR open → TC 브랜치 생성

**목적**: `pull_request_target: opened` 트리거 검증. `tc/pr-<N>` 브랜치가 두 TC fork에 생성되는지 확인.

**절차**

```bash
cd cubrid   # 엔진 fork 로컬 디렉터리
git checkout develop
git pull origin develop

git checkout -b feature/test-scenario-a
echo "test" >> dummy.txt
git add dummy.txt
git commit -m "test: scenario A"
git push origin feature/test-scenario-a

# PR 생성 (base: develop, 대상: 개인 fork)
gh pr create \
  --repo <YOUR_USERNAME>/cubrid \
  --base develop \
  --head feature/test-scenario-a \
  --title "test: scenario A" \
  --body "TC 브랜치 생성 테스트"
```

**기록해 둘 것**: 출력에서 PR 번호 확인 (예: `#5`)

**검증**

```bash
# Actions 실행 확인 (완료까지 약 30~60초 소요)
gh run list --repo <YOUR_USERNAME>/cubrid --workflow tc-branch-sync.yml

# TC fork에 브랜치 생성 여부 확인
gh api repos/<YOUR_USERNAME>/cubrid-testcases/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'

gh api repos/<YOUR_USERNAME>/cubrid-testcases-private-ex/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'
```

**기대 결과**
- `tc-branch-sync.yml` 워크플로우 `success`
- `cubrid-testcases` 브랜치 목록: `tc/pr-5` 존재
- `cubrid-testcases-private-ex` 브랜치 목록: `tc/pr-5` 존재

---

### Scenario B: PR merge → TC 브랜치 develop 머지 + 삭제

**목적**: `pull_request_target: closed` (merged=true) 트리거 검증. TC 브랜치가 `develop`에 `--no-ff` 머지되고 삭제되는지 확인.

**사전 조건**: Scenario A가 완료되어 `tc/pr-<N>` 브랜치가 이미 존재해야 한다.

**선택: TC 브랜치에 커밋 추가 (머지 흔적 확인용)**

```bash
git clone https://github.com/<YOUR_USERNAME>/cubrid-testcases.git tc-pub-clone
cd tc-pub-clone
git checkout tc/pr-5
echo "tc change for pr-5" >> testcase.txt
git add testcase.txt
git commit -m "test: add TC for engine PR #5"
git push origin tc/pr-5
cd ..
```

**PR 머지**

```bash
gh pr merge 5 \
  --repo <YOUR_USERNAME>/cubrid \
  --merge \
  --delete-branch
```

**검증**

```bash
# Actions 실행 확인
gh run list --repo <YOUR_USERNAME>/cubrid --workflow tc-branch-finalize.yml

# develop 브랜치 최신 커밋 확인 (--no-ff merge commit 이 있어야 함)
gh api "repos/<YOUR_USERNAME>/cubrid-testcases/commits?sha=develop" \
  --jq '.[0:3] | .[] | {sha: .sha[0:7], message: .commit.message}'

# 브랜치 삭제 확인 (tc/ 로 시작하는 브랜치가 없어야 함)
gh api repos/<YOUR_USERNAME>/cubrid-testcases/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'

gh api repos/<YOUR_USERNAME>/cubrid-testcases-private-ex/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'
```

**기대 결과**
- `tc-branch-finalize.yml` 워크플로우 `success`
- `cubrid-testcases` `develop` 최신 커밋 메시지: `Merge TC branch 'tc/pr-5' into develop (Engine PR #5)`
- `tc/pr-5` 브랜치: 두 TC fork 모두 삭제됨

---

### Scenario C: PR close (reject) → TC 브랜치 삭제

**목적**: `pull_request_target: closed` (merged=false) 트리거 검증. TC 브랜치가 `develop` 머지 없이 삭제되는지 확인.

**절차**

```bash
cd cubrid
git checkout develop && git pull origin develop

git checkout -b feature/test-scenario-c
echo "test" >> dummy2.txt
git add dummy2.txt
git commit -m "test: scenario C"
git push origin feature/test-scenario-c

gh pr create \
  --repo <YOUR_USERNAME>/cubrid \
  --base develop \
  --head feature/test-scenario-c \
  --title "test: scenario C (will be rejected)" \
  --body "PR 거절 시 TC 브랜치 삭제 테스트"
```

Scenario A와 동일하게 `tc-branch-sync.yml`이 실행되어 `tc/pr-<N>` 브랜치가 생성된다.  
생성 확인 후 PR을 **Close (머지 없이)** 한다.

```bash
# PR 번호 확인
gh pr list --repo <YOUR_USERNAME>/cubrid

# close
gh pr close <PR_NUMBER> --repo <YOUR_USERNAME>/cubrid
```

**검증**

```bash
gh run list --repo <YOUR_USERNAME>/cubrid --workflow tc-branch-finalize.yml

# 브랜치 삭제 확인 (빈 결과여야 함)
gh api repos/<YOUR_USERNAME>/cubrid-testcases/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'

gh api repos/<YOUR_USERNAME>/cubrid-testcases-private-ex/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'

# develop 커밋 이력 변화 없음 확인
gh api "repos/<YOUR_USERNAME>/cubrid-testcases/commits?sha=develop" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'
```

**기대 결과**
- `tc-branch-finalize.yml` 워크플로우 `success`
- `develop` 브랜치에 새 머지 커밋 없음
- `tc/pr-<N>` 브랜치: 두 TC fork 모두 삭제됨

---

### Scenario D: TC 브랜치 이미 존재 (skip)

**목적**: 브랜치가 이미 있을 때 중복 생성 없이 skip하는지 검증.

**절차**

열려 있는 PR(Scenario A에서 사용한 PR, 또는 새로 생성한 PR)에 새 커밋을 push하여 `synchronize` 이벤트를 발생시킨다.

```bash
cd cubrid
git checkout feature/test-scenario-a
echo "update" >> dummy.txt
git add dummy.txt
git commit -m "test: trigger synchronize"
git push origin feature/test-scenario-a
```

**검증**

Actions 탭 → 방금 실행된 `tc-branch-sync.yml` 로그 → **"Sync TC branch"** 스텝 확인:

```
[info] Branch 'tc/pr-5' already exists in <YOUR_USERNAME>/cubrid-testcases, skipping
[info] Branch 'tc/pr-5' already exists in <YOUR_USERNAME>/cubrid-testcases-private-ex, skipping
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
git push origin feature/test-scenario-e

gh pr create \
  --repo <YOUR_USERNAME>/cubrid \
  --base develop \
  --head feature/test-scenario-e \
  --title "test: scenario E (app error)" \
  --body "App 미설치 실패 케이스 테스트"
```

**기대 결과**
- `tc-branch-sync.yml` 워크플로우 `failure`
- 엔진 PR에 아래 코멘트 자동 게시:
  ```
  ⚠️ TC Branch Sync Failed (PR #N)
  ...
  Workflow logs: https://github.com/<YOUR_USERNAME>/cubrid/actions/runs/...
  ```

**복구**: 테스트 후 App 설치에서 `cubrid-testcases`를 다시 추가한다.

---

## 6. 테스트 후 정리

### TC fork 브랜치 정리

```bash
# cubrid-testcases fork의 tc/ 브랜치 일괄 삭제
for branch in $(gh api repos/<YOUR_USERNAME>/cubrid-testcases/branches \
    --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'); do
  gh api --method DELETE "repos/<YOUR_USERNAME>/cubrid-testcases/git/refs/heads/${branch}"
  echo "Deleted: ${branch}"
done

# cubrid-testcases-private-ex fork의 tc/ 브랜치 일괄 삭제
for branch in $(gh api repos/<YOUR_USERNAME>/cubrid-testcases-private-ex/branches \
    --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'); do
  gh api --method DELETE "repos/<YOUR_USERNAME>/cubrid-testcases-private-ex/git/refs/heads/${branch}"
  echo "Deleted: ${branch}"
done
```

### 엔진 fork PR 및 테스트 브랜치 정리

```bash
# 열려 있는 테스트 PR 모두 close
gh pr list --repo <YOUR_USERNAME>/cubrid --json number --jq '.[].number' | \
  xargs -I{} gh pr close {} --repo <YOUR_USERNAME>/cubrid

# 로컬 및 원격 테스트 브랜치 삭제
for branch in feature/test-scenario-a feature/test-scenario-c \
              feature/test-scenario-e; do
  git push origin --delete "$branch" 2>/dev/null && echo "Deleted remote: $branch"
  git branch -D "$branch" 2>/dev/null && echo "Deleted local: $branch"
done
```

### 워크플로우 파일 원복 (선택)

테스트용으로 수정한 워크플로우 파일을 되돌리려면 아래 명령어를 사용한다.

```bash
git checkout develop
git revert HEAD --no-edit   # 수정 커밋을 revert
git push origin develop
```

또는 수정 커밋을 그대로 유지하고 실제 배포 시 다시 CUBRID 조직 값으로 교체한다.

### GitHub App 삭제 (테스트 완전 종료 시)

```
https://github.com/settings/apps/tc-branch-manager-test
→ Advanced → Delete GitHub App
```

---

## 7. 주의사항

### fork에서 PR 생성 시 upstream 방지

`gh pr create` 명령어는 `--repo` 없이 실행하면 현재 디렉터리의 remote 설정에 따라 upstream(`CUBRID/cubrid`)으로 PR을 생성할 수 있다.  
**반드시 `--repo <YOUR_USERNAME>/cubrid`를 명시**한다.

```bash
# 올바른 예
gh pr create --repo <YOUR_USERNAME>/cubrid --base develop ...

# 위험: upstream으로 PR이 생성될 수 있음
gh pr create --base develop ...
```

### `pull_request_target` 트리거의 base 브랜치 원칙

워크플로우는 PR의 **base 브랜치**(`develop`)에 있는 워크플로우 파일로 실행된다.  
feature 브랜치에만 파일을 수정하면 이전 버전이 실행된다.  
**워크플로우 수정은 반드시 `develop`에 push 후 테스트한다.**

### fork의 Actions Secrets는 upstream과 독립

`CUBRID/cubrid`의 secrets(`TC_APP_ID`, `TC_APP_PRIVATE_KEY`)는 fork에 상속되지 않는다.  
fork(`<YOUR_USERNAME>/cubrid`)의 **Settings → Secrets**에 별도로 등록해야 한다.

### TC fork `develop` 브랜치 보호 설정 비활성화 권장

Branch Protection이 활성화된 경우 `tc-branch-finalize.yml`의 `git push origin develop`이 실패한다.  
테스트 단계에서는 Branch Protection 없이 진행하는 것을 권장한다.

```
https://github.com/<YOUR_USERNAME>/cubrid-testcases → Settings → Branches
→ develop 규칙이 있으면 비활성화 또는 삭제
```

### fork sync 상태 유의

TC fork의 `develop` 브랜치가 upstream보다 뒤처져 있어도 테스트에는 지장 없다.  
단, 워크플로우가 `tc/pr-<N>`을 `develop`에 머지할 때 fast-forward가 아닌 `--no-ff` 머지를 사용하므로, fork `develop`의 커밋 이력과 upstream `develop`이 달라질 수 있다. 테스트 종료 후 fork를 upstream으로 재동기화하는 것을 권장한다.

```bash
gh repo sync <YOUR_USERNAME>/cubrid-testcases --branch develop
gh repo sync <YOUR_USERNAME>/cubrid-testcases-private-ex --branch develop
```
