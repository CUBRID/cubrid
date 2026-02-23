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

> **중요**: `pull_request_target` 이벤트는 PR base 브랜치가 아닌 **default branch(`cubridqa-1320`)**에서 워크플로우 파일을 읽는다.
> 워크플로우 수정 후 반드시 `cubridqa-1320`에 push하고, `test/sync-tc`에도 merge해야 한다.

---

## 1. 초기 셋업

### 1-1. 워크플로우 파일 반영

워크플로우 파일을 `cubridqa-1320`에 push하고 `test/sync-tc`에 반영한다.

```bash
cd cubrid

# cubridqa-1320에 최신 워크플로우 push (이미 완료된 경우 skip)
git checkout cubridqa-1320
git pull twkang cubridqa-1320

# test/sync-tc에 반영
git checkout test/sync-tc
git merge cubridqa-1320
git push twkang test/sync-tc
```

### 1-2. TC fork `develop` 브랜치 상태 확인

```bash
gh api repos/tw-kang/cubrid-testcases/branches/develop --jq '.name' 2>/dev/null \
  && echo "OK" || echo "develop 브랜치 없음"

gh api repos/tw-kang/cubrid-testcases-private-ex/branches/develop --jq '.name' 2>/dev/null \
  && echo "OK" || echo "develop 브랜치 없음"
```

없으면 upstream에서 동기화한다.

```bash
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

### 1-3. Secrets 확인

`tw-kang/cubrid`의 **Settings → Secrets → Actions**에 아래 시크릿이 정상값으로 등록되어 있는지 확인한다.

| Secret | 상태 확인 |
|--------|---------|
| `TC_APP_ID` | 실제 App ID (숫자) — 이전 테스트 후 `9999999`로 변경됐다면 복원 필요 |
| `TC_APP_PRIVATE_KEY` | PEM 전체 내용 |

---

## 2. 테스트 시나리오

> **주의**: 모든 PR 생성 시 `--repo tw-kang/cubrid`와 `--base test/sync-tc`를 반드시 지정한다.

---

### Scenario F: Revert PR → TC 자동 revert 적용

**목적**: `tc-branch-sync.yml`의 Revert 감지 로직 검증.
원본 PR이 머지될 때 TC merge commit이 develop에 생성되고, Revert PR이 생성될 때 해당 커밋이 자동으로 revert되어 `tc/pr-<REVERT_PR_NUMBER>` 브랜치에 반영되는지 확인한다.

#### F-1. 원본 PR 생성 및 TC 변경 추가

```bash
cd cubrid
git checkout cubridqa-1320 && git pull twkang cubridqa-1320
git checkout -b feature/test-revert-base
echo "revert base" >> dummy_revert.txt
git add dummy_revert.txt
git commit -m "test: base commit for revert scenario"
git push twkang feature/test-revert-base

gh pr create \
  --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/test-revert-base \
  --title "test: revert scenario base PR" \
  --body "TC 브랜치 생성 후 revert 테스트용 원본 PR"
```

`tc-branch-sync.yml`이 실행되어 `tc/pr-<N>` 브랜치가 생성되면, TC 브랜치에 커밋을 추가한다.

```bash
# 출력된 PR 번호 확인 후 BASE_PR에 설정
BASE_PR=<PR_NUMBER>

git clone https://github.com/tw-kang/cubrid-testcases.git tc-pub-revert
cd tc-pub-revert
git checkout tc/pr-${BASE_PR}
echo "TC change for revert test" >> testcase_revert.txt
git add testcase_revert.txt
git commit -m "test: add TC change for engine PR #${BASE_PR}"
git push origin tc/pr-${BASE_PR}
cd ..
```

#### F-2. 원본 PR 머지

```bash
gh pr merge ${BASE_PR} \
  --repo tw-kang/cubrid \
  --merge \
  --delete-branch
```

`tc-branch-finalize.yml` 실행 완료 후 TC develop에 merge commit이 생성됐는지 확인한다.

```bash
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'
```

기대 결과: `"Merge TC branch 'tc/pr-${BASE_PR}' into develop (Engine PR #${BASE_PR})"`

#### F-3. Revert PR 생성

GitHub UI 또는 CLI로 원본 PR을 revert한다.
GitHub CLI로 Revert PR을 생성하려면 PR body에 반드시 `Reverts tw-kang/cubrid#<BASE_PR>` 패턴이 포함되어야 한다.

```bash
# 방법 1: GitHub UI에서 원본 PR 페이지 → "Revert" 버튼 클릭 (권장)
# → 자동으로 body에 "Reverts tw-kang/cubrid#N" 포함

# 방법 2: CLI로 직접 생성
git checkout cubridqa-1320 && git pull twkang cubridqa-1320
git checkout -b revert/pr-${BASE_PR}
git push twkang revert/pr-${BASE_PR}

gh pr create \
  --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head revert/pr-${BASE_PR} \
  --title "Revert \"test: revert scenario base PR\"" \
  --body "Reverts tw-kang/cubrid#${BASE_PR}"
```

#### F-4. 검증

```bash
# tc-branch-sync.yml 실행 확인 (revert 경로)
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml

# Revert PR 번호 확인
REVERT_PR=<REVERT_PR_NUMBER>

# tc/pr-<REVERT_PR> 브랜치 존재 확인
gh api repos/tw-kang/cubrid-testcases/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'

# tc/pr-<REVERT_PR> 브랜치 최신 커밋 메시지 확인 (revert commit 있어야 함)
gh api "repos/tw-kang/cubrid-testcases/commits?sha=tc/pr-${REVERT_PR}" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'

# Revert PR에 자동 코멘트 게시 확인
gh pr view ${REVERT_PR} --repo tw-kang/cubrid --comments
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| `tc-branch-sync.yml` 결론 | `success` |
| `tc/pr-<REVERT_PR>` 브랜치 최신 커밋 | `Revert TC changes for engine PR #<BASE_PR> (reverted by engine PR #<REVERT_PR>)` |
| Revert PR 코멘트 | `ℹ️ TC Revert Applied (PR #<REVERT_PR>)` |

---

## 3. 테스트 후 정리 (초기화)

### TC fork 브랜치 정리

```bash
for branch in $(gh api repos/tw-kang/cubrid-testcases/branches \
    --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'); do
  gh api --method DELETE "repos/tw-kang/cubrid-testcases/git/refs/heads/${branch}"
  echo "Deleted: ${branch}"
done

for branch in $(gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
    --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'); do
  gh api --method DELETE "repos/tw-kang/cubrid-testcases-private-ex/git/refs/heads/${branch}"
  echo "Deleted: ${branch}"
done
```

### 엔진 fork PR 및 테스트 브랜치 정리

```bash
# 열려 있는 테스트 PR close
gh pr list --repo tw-kang/cubrid --json number --jq '.[].number' | \
  xargs -I{} gh pr close {} --repo tw-kang/cubrid

# 테스트용 원격 브랜치 삭제
for branch in feature/test-revert-base revert/pr-${BASE_PR}; do
  git push twkang --delete "$branch" 2>/dev/null && echo "Deleted remote: $branch"
  git branch -D "$branch" 2>/dev/null && echo "Deleted local: $branch"
done
```

### TC fork `develop` 복구

테스트로 인해 fork develop이 upstream과 달라졌으므로 재동기화한다.

```bash
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

### 로컬 clone 정리

```bash
rm -rf tc-pub-revert
```
