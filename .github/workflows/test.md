# Fork 환경 테스트 가이드

**테스트 환경**

| 항목 | 값 |
|------|-----|
| 엔진 fork | `tw-kang/cubrid` |
| TC 공개 fork | `tw-kang/cubrid-testcases` |
| TC 비공개 fork | `tw-kang/cubrid-testcases-private-ex` |
| 개발 브랜치 (default) | `cubridqa-1320` |
| 테스트 base 브랜치 | `test/sync-tc` |

> `pull_request_target`은 PR base가 아닌 **default branch(`cubridqa-1320`)**에서 워크플로우 파일을 읽는다.  
> 워크플로우 수정 후 반드시 `cubridqa-1320`에 push하고 `test/sync-tc`에 merge해야 한다.

---

## 1. 사전 준비

```bash
# 워크플로우 파일을 default branch에 push 후 test/sync-tc에 반영
git checkout cubridqa-1320 && git pull twkang cubridqa-1320
git checkout test/sync-tc && git merge cubridqa-1320 && git push twkang test/sync-tc

# TC fork develop 복원 (필요 시)
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

Secrets 확인: `tw-kang/cubrid` Settings → Secrets → `TC_APP_ID`(실제 App ID), `TC_APP_PRIVATE_KEY`(PEM)

---

## 2. Scenario F: Revert PR → TC 자동 revert

**목적**: `tc-branch-sync.yml`의 Revert 감지 로직 및 TC 자동 revert 흐름 검증.

### F-1. 원본 PR 생성 및 TC 변경 추가

```bash
git checkout cubridqa-1320 && git checkout -b feature/test-revert-base
echo "revert base" >> dummy_revert.txt
git add dummy_revert.txt && git commit -m "test: base commit for revert scenario"
git push twkang feature/test-revert-base

gh pr create --repo tw-kang/cubrid --base test/sync-tc --head feature/test-revert-base \
  --title "test: revert scenario base PR" \
  --body "TC 브랜치 생성 후 revert 테스트용 원본 PR"
```

`tc-branch-sync.yml` 실행 후 `tc/pr-<N>` 브랜치가 생성되면 TC 변경을 추가한다.

```bash
BASE_PR=<PR_NUMBER>

git clone --depth=1 --branch tc/pr-${BASE_PR} \
  https://github.com/tw-kang/cubrid-testcases.git tc-pub-revert
cd tc-pub-revert
echo "TC change for revert test" >> testcase_revert.txt
git add testcase_revert.txt
git commit -m "test: add TC change for engine PR #${BASE_PR}"
git push origin tc/pr-${BASE_PR}
cd ..
```

### F-2. 원본 PR 머지

```bash
gh pr merge ${BASE_PR} --repo tw-kang/cubrid --merge --delete-branch

# tc-branch-finalize.yml 완료 후 TC develop 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'
# 기대값: "Merge TC branch 'tc/pr-${BASE_PR}' into develop (Engine PR #${BASE_PR})"
```

### F-3. Revert PR 생성

- GitHub UI에서 원본 PR 페이지 → **Revert** 버튼 클릭 (권장).  
  자동으로 body 에 `Reverts tw-kang/cubrid#N` 이 포함된다.

CLI 로 생성 시:

```bash
git checkout cubridqa-1320 && git checkout -b revert/pr-${BASE_PR}
# 주의: base 와 diff 가 있어야 함 (코드 수정 포함)
git push twkang revert/pr-${BASE_PR}

gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head revert/pr-${BASE_PR} \
  --title "Revert \"test: revert scenario base PR\"" \
  --body "Reverts tw-kang/cubrid#${BASE_PR}"
```

### F-4. 검증

```bash
REVERT_PR=<REVERT_PR_NUMBER>

# tc-branch-sync.yml 실행 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 3

# tc/pr-<REVERT_PR> 브랜치 존재 확인
gh api repos/tw-kang/cubrid-testcases/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'

# revert 커밋 메시지 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=tc/pr-${REVERT_PR}" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'

# PR 코멘트 확인
gh pr view ${REVERT_PR} --repo tw-kang/cubrid --comments
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| `tc-branch-sync.yml` 결론 | `success` |
| `tc/pr-<REVERT_PR>` 최신 커밋 | `Revert TC changes for engine PR #<BASE_PR> (reverted by engine PR #<REVERT_PR>)` |
| Revert PR 코멘트 | `ℹ️ TC Revert Applied (PR #<REVERT_PR>)` |

---

## 3. 테스트 후 정리 (기본 정리 시나리오)

```bash
# TC fork tc/ 브랜치 삭제
for branch in $(gh api repos/tw-kang/cubrid-testcases/branches \
    --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'); do
  gh api --method DELETE "repos/tw-kang/cubrid-testcases/git/refs/heads/${branch}"
done

for branch in $(gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
    --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'); do
  gh api --method DELETE "repos/tw-kang/cubrid-testcases-private-ex/git/refs/heads/${branch}"
done

# 열린 PR close
gh pr list --repo tw-kang/cubrid --json number --jq '.[].number' | \
  xargs -I{} gh pr close {} --repo tw-kang/cubrid

# 테스트 브랜치 삭제
git checkout cubridqa-1320
git push twkang --delete feature/test-revert-base revert/pr-${BASE_PR} 2>/dev/null
git branch -D feature/test-revert-base revert/pr-${BASE_PR} 2>/dev/null

# TC fork develop 재동기화
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop

# 로컬 clone 정리
rm -rf tc-pub-revert
```

---

## 4. 추가 시나리오: finalize/delete 안전성 (G 시나리오)

### G-1. 동시 머지 시 finalize 직렬화 확인

**목적**: `tc-branch-finalize.yml` 의 `concurrency` 설정으로 TC develop 머지가 직렬화되는지 확인.

```bash
# PR 2개를 거의 동시에 머지
PR1=<PR_NUMBER_1>
PR2=<PR_NUMBER_2>

gh pr merge ${PR1} --repo tw-kang/cubrid --merge --delete-branch &
gh pr merge ${PR2} --repo tw-kang/cubrid --merge --delete-branch &
wait

# 두 PR 모두에 대해 tc-branch-finalize.yml 실행 로그 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 5
```

**기대 결과**

- 두 run 이 동시에 트리거되더라도, 실제 머지/푸시 시점은 직렬로 수행되어 TC develop 히스토리가 깨지지 않는다.

### G-2. 비머지 PR close 시 TC 브랜치 삭제 실패 처리

**목적**: `Delete TC branch (PR closed without merge)` 단계 실패 시 PR 코멘트가 생성되는지 확인.

1. intentionally 잘못된 권한/레포 설정으로 TC 삭제가 실패하도록 조작한다. (예: 테스트용 fork 에서 App 권한을 일시적으로 제거)
2. 엔진 PR 을 open → TC 브랜치 생성 후, **merge 없이 close** 한다.

```bash
REJECT_PR=<PR_NUMBER>
gh pr close ${REJECT_PR} --repo tw-kang/cubrid
```

**기대 결과**

- `tc-branch-finalize.yml` run 이 `failure` 로 끝나고, PR 타임라인에 `⚠️ TC Branch Delete Failed` 코멘트가 추가된다.

### G-3. 머지 성공 후 브랜치 삭제 실패 처리

**목적**: 머지는 성공했지만 브랜치 삭제(`git push --delete` 또는 REST API DELETE)가 실패하는 경우, 에러 메시지가 머지/삭제를 포괄하도록 동작하는지 확인.

1. TC 브랜치에 대해 삭제 권한이 없도록 일시적으로 설정하거나, 삭제 시점에 브랜치를 다른 세션에서 이미 삭제해 race 를 유도한다.
2. 엔진 PR 을 merge 하여 `tc-branch-finalize.yml` 을 실행한다.

**기대 결과**

- 워크플로우 실패 시 PR 타임라인에 `❌ TC Branch Finalize Failed` 코멘트가 추가되고, 메시지 내용이 "merge 실패" 가 아닌 "finalize (merge and/or delete) 실패" 로 표시된다.

---

## 5. 종합 테스트 시나리오 (TC-S/R/F/D/C)

아래 TC 들은 리팩토링된 `tc-branch-sync.yml`, `tc-branch-finalize.yml` 전체 동작을 촘촘하게 검증하기 위한 시나리오이다.  
각 TC 는 **Setup → Trigger → Verify → Cleanup** 4단계로 구성되며, 에이전트가 그대로 실행 가능한 bash 명령과 기대 결과를 포함한다.

### 5.1 TC-S: tc-branch-sync (Normal PR)

#### TC-S01: PR opened → tc/pr-N 양쪽 TC 브랜치 생성

- **Setup**
  - `cubridqa-1320` 기준에서 새 기능 브랜치 생성.
  - 워크플로우 파일이 `cubridqa-1320` / `test/sync-tc` 에 반영되어 있어야 한다.
- **Trigger**

```bash
git checkout cubridqa-1320
git checkout -b feature/tc-s01-normal-open
echo "tc-s01" >> tc_s01.txt
git add tc_s01.txt
git commit -m "test: TC-S01 normal PR open"
git push twkang feature/tc-s01-normal-open

gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/tc-s01-normal-open \
  --title "test: TC-S01 normal open" \
  --body "TC-S01: verify tc/pr-N creation on PR open"
```

- **Verify**

```bash
PR_NUMBER=$(gh pr view --repo tw-kang/cubrid --json number --jq '.number')
BRANCH="tc/pr-${PR_NUMBER}"

gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 3

gh api repos/tw-kang/cubrid-testcases/branches \
  --jq "[.[].name | select(. == \"${BRANCH}\")]"

gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
  --jq "[.[].name | select(. == \"${BRANCH}\")]"
```

- **기대 결과**
  - `tc-branch-sync.yml` run 이 `success` 로 끝난다.
  - 두 TC repo 모두에 `tc/pr-<PR_NUMBER>` 브랜치가 존재한다.

- **Cleanup**
  - 이후 TC-S02, TC-S03 등에서 같은 PR 을 재사용할 수 있으므로 PR 을 즉시 닫지 않는다.

#### TC-S02: PR synchronize → 기존 tc/pr-N skip (idempotent)

- **Setup**
  - TC-S01 에서 생성된 동일 PR 을 사용한다.
- **Trigger**

```bash
PR_NUMBER=$(gh pr view --repo tw-kang/cubrid --json number --jq '.number')

git checkout feature/tc-s01-normal-open
echo "tc-s02" >> tc_s02.txt
git add tc_s02.txt
git commit -m "test: TC-S02 synchronize"
git push twkang feature/tc-s01-normal-open
```

- **Verify**

```bash
BRANCH="tc/pr-${PR_NUMBER}"

gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 3

gh api repos/tw-kang/cubrid-testcases/branches \
  --jq "[.[].name | select(. == \"${BRANCH}\")]"

gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
  --jq "[.[].name | select(. == \"${BRANCH}\")]"
```

- **기대 결과**
  - `tc-branch-sync.yml` run 은 `success`.
  - 두 TC repo 의 `tc/pr-N` 브랜치는 그대로 유지되며, 새로 생성되거나 삭제되지 않는다 (idempotent).

#### TC-S03: PR reopened → 기존 tc/pr-N skip

- **Setup**
  - TC-S01/02 에서 사용한 PR 을 `close` 한 뒤 다시 `reopen` 한다.
- **Trigger**

```bash
PR_NUMBER=$(gh pr view --repo tw-kang/cubrid --json number --jq '.number')

gh pr close "${PR_NUMBER}" --repo tw-kang/cubrid
gh pr reopen "${PR_NUMBER}" --repo tw-kang/cubrid
```

- **Verify**

```bash
BRANCH="tc/pr-${PR_NUMBER}"

gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 3

gh api repos/tw-kang/cubrid-testcases/branches \
  --jq "[.[].name | select(. == \"${BRANCH}\")]"

gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
  --jq "[.[].name | select(. == \"${BRANCH}\")]"
```

- **기대 결과**
  - 워크플로우가 성공하며, 기존 `tc/pr-N` 브랜치는 삭제되지 않는다.

- **Cleanup**

```bash
gh pr close "${PR_NUMBER}" --repo tw-kang/cubrid
```

#### TC-S04: Sync 실패 (App 권한 없음) → PR 에 에러 코멘트 게시

- **Setup**
  - 테스트용으로 App 권한을 제거하거나, 잘못된 `TC_APP_ID` / `TC_APP_PRIVATE_KEY` 를 설정한 테스트 레포를 사용한다.  
    (본 레포에서는 실제로 권한을 변경하지 말고, 별도 sandbox 에 적용하는 것을 권장.)
- **Trigger**
  - TC-S01 과 동일 방식으로 PR open.
- **Verify**

```bash
PR_NUMBER=<PR_NUMBER_WITH_BROKEN_APP>

gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 3
gh pr view "${PR_NUMBER}" --repo tw-kang/cubrid --comments
```

- **기대 결과**
  - 워크플로우 run 이 `failure`.
  - PR 타임라인에 `⚠️ TC Branch Sync Failed` 코멘트가 추가되고, pub/priv repo 각각의 실패 status 가 포함된다.

#### TC-S05: Concurrency → 동일 PR 에 연속 synchronize 발생 시 이전 run 취소

- **Setup**
  - 정상 동작하는 GitHub App 환경.
  - 새 PR 을 생성하고 `tc/pr-N` 브랜치를 이미 생성해 둔다.
- **Trigger**

```bash
PR_NUMBER=<EXISTING_PR_FOR_CONCURRENCY>

git checkout feature/tc-s01-normal-open
echo "tc-s05-a" >> tc_s05.txt
git add tc_s05.txt
git commit -m "test: TC-S05 sync A"
git push twkang feature/tc-s01-normal-open &

sleep 2

echo "tc-s05-b" >> tc_s05.txt
git add tc_s05.txt
git commit -m "test: TC-S05 sync B"
git push twkang feature/tc-s01-normal-open &

wait
```

- **Verify**

```bash
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 5 --json status,conclusion,headBranch,runNumber
```

- **기대 결과**
  - 같은 PR 번호에 대해 오래된 run 은 `cancelled`, 마지막 run 만 `success` 가 된다.

### 5.2 TC-R: tc-branch-sync (Revert PR)

기본 흐름은 상단 **Scenario F** 를 따른다. 각 TC 는 F-1~F-4 의 절차를 재사용하되, 검증 관점만 세분화한다.

#### TC-R01: Revert PR body 에서 `Reverts #N` 패턴 감지

- **Setup & Trigger**
  - Scenario F-1, F-2 를 따라 원본 PR 과 TC 변경을 준비한다.
  - Scenario F-3 에서 Revert PR 생성 시, body 에 `Reverts tw-kang/cubrid#<BASE_PR>` 가 포함되도록 한다.
- **Verify**

```bash
REVERT_PR=<REVERT_PR_NUMBER>

gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 3
gh run view --repo tw-kang/cubrid <RUN_ID> --log  # 필요 시 로그에서 original_pr 출력 확인
```

- **기대 결과**
  - `Detect revert PR` step 이 `is_revert=true`, `original_pr=<BASE_PR>` 로 판단하여 Revert 흐름을 탄다.

#### TC-R02: Revert 적용 + 성공 코멘트

- **Setup & Trigger**
  - Scenario F 전체(F-1~F-3)를 그대로 수행한다.
- **Verify**
  - Scenario F-4 의 검증 명령을 사용한다.
- **기대 결과**
  - `tc/pr-<REVERT_PR>` 브랜치 최신 커밋 메시지:
    - `Revert TC changes for engine PR #<BASE_PR> (reverted by engine PR #<REVERT_PR>)`
  - PR 코멘트에 `ℹ️ TC Revert Applied (PR #<REVERT_PR>)` 가 존재한다.

#### TC-R03: 원본 PR 에 TC 변경 없음 → silent skip

- **Setup**
  - 원본 PR 을 생성하되, TC 브랜치에는 아무 변경도 머지하지 않는다.
- **Trigger & Verify**
  - Scenario F-3, F-4 와 동일하게 Revert PR 을 생성하고, 워크플로우 run 과 코멘트를 확인한다.
- **기대 결과**
  - 워크플로우 run 은 성공하더라도, TC 쪽에는 `no_tc_changes` 로 기록되어 Revert 커밋/브랜치 생성이 없다.
  - PR 타임라인에 TC Revert 관련 코멘트가 추가되지 않는다.

#### TC-R04: Revert conflict → 수동 처리 요청 코멘트

- **Setup**
  - 원본 TC 브랜치에서 일부 파일을 이후 develop 에서 다른 내용으로 수정하여, revert 시 conflict 가 발생하도록 한다.
- **Trigger**
  - Scenario F 와 동일한 방식으로 Revert PR 생성.
- **Verify**

```bash
REVERT_PR=<REVERT_PR_NUMBER>

gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 3
gh pr view "${REVERT_PR}" --repo tw-kang/cubrid --comments
```

- **기대 결과**
  - 워크플로우 로그에서 `conflict` 상태가 기록된다.
  - PR 코멘트에 `⚠️ TC Revert Conflict` 가 포함된 메시지가 추가된다.

#### TC-R05: 기존 revert 브랜치 존재 → force-push

- **Setup**
  - 동일한 Revert PR 을 여러 번 만들어 `tc/pr-<REVERT_PR>` 가 이미 존재하는 상태에서 다시 revert 를 수행한다.
- **Trigger**
  - 동일 BASE_PR 에 대해 Revert PR 을 새로 만들고, 워크플로우를 다시 실행.
- **Verify**

```bash
REVERT_PR=<REVERT_PR_NUMBER>
BRANCH="tc/pr-${REVERT_PR}"

gh api "repos/tw-kang/cubrid-testcases/commits?sha=${BRANCH}" \
  --jq ".[0] | {sha: .sha[0:7], message: .commit.message}"
```

- **기대 결과**
  - 기존 브랜치가 강제로 overwrite 되어 최신 커밋 메시지가 기대 메시지로 갱신된다.

#### TC-R06: PR title 헤더 유지

- **Setup**
  - 원본 PR 과 Revert PR 의 title 에 `[CBRD-XXXX]` 형식 헤더를 포함한다.
- **Trigger**
  - Scenario F 와 동일.
- **Verify**

```bash
REVERT_PR=<REVERT_PR_NUMBER>
BRANCH="tc/pr-${REVERT_PR}"

gh api "repos/tw-kang/cubrid-testcases/commits?sha=${BRANCH}" \
  --jq ".[0].commit.message"
```

- **기대 결과**
  - 커밋 메시지 앞에 `[CBRD-XXXX]` 헤더가 유지된다.

#### TC-R07: Revert 실패 (App 오류) → 에러 코멘트

- **Setup**
  - TC-S04 와 같이 App 을 잘못 설정한 테스트 환경에서 Revert PR 을 생성한다.
- **Verify**

```bash
REVERT_PR=<PR_NUMBER_WITH_BROKEN_APP>

gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 3
gh pr view "${REVERT_PR}" --repo tw-kang/cubrid --comments
```

- **기대 결과**
  - 워크플로우 run 이 실패하고, PR 코멘트에 `⚠️ TC Revert Failed` 메시지가 추가된다.

### 5.3 TC-F: tc-branch-finalize (Merged)

여기서는 `tc-branch-finalize.yml` 의 loop 기반 merge/delete 동작을 검증한다.

#### TC-F01: PR merged → squash merge + 브랜치 삭제

- **Setup**
  - 정상 App 환경.
  - 임의 PR 을 열어 `tc/pr-N` 브랜치를 생성하고, TC 브랜치에 간단한 커밋을 추가한다.
- **Trigger**

```bash
PR_NUMBER=<PR_NUMBER_FOR_FINALIZE>

gh pr merge "${PR_NUMBER}" --repo tw-kang/cubrid --merge --delete-branch
```

- **Verify**

```bash
BRANCH="tc/pr-${PR_NUMBER}"

gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 3

gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'

gh api repos/tw-kang/cubrid-testcases/branches \
  --jq "[.[].name | select(. == \"${BRANCH}\")]"
```

- **기대 결과**
  - develop 최신 커밋 메시지에 `Merge TC branch 'tc/pr-<PR_NUMBER>' into develop (Engine PR #<PR_NUMBER>)` 가 포함된다.
  - `tc/pr-N` 브랜치는 두 TC repo 모두에서 삭제된다.

#### TC-F02: PR merged, TC 브랜치 없음 → skip

- **Setup**
  - TC 브랜치를 생성하지 않고 엔진 PR 을 merge 한다.
- **Verify**
  - `tc-branch-finalize.yml` run 이 `success` 로 표시되고, 로그에 `Branch 'tc/pr-N' not found` 메시지가 출력된다.

#### TC-F03: pub 만 TC 변경 → pub 만 merge, priv skip

- **Setup**
  - 공개 TC repo 에만 `tc/pr-N` 을 생성하고, 비공개 repo 에는 브랜치를 만들지 않는다.
- **Trigger & Verify**
  - PR merge 후 finalize 워크플로우 로그를 확인한다.
- **기대 결과**
  - `cubrid-testcases` 는 `merged_deleted`, `cubrid-testcases-private-ex` 는 `skipped` 상태가 된다.

#### TC-F04: 헤더 추출 → squash 커밋 메시지에 [CBRD-1234] 포함

- **Setup**
  - PR title 을 `[CBRD-1234] some message` 형식으로 설정.
- **Trigger & Verify**
  - TC-F01 과 동일한 방법으로 merge 후, develop 최신 커밋 메시지를 확인한다.
- **기대 결과**
  - 커밋 메시지에 `[CBRD-1234]` 가 prefix 로 포함된다.

#### TC-F05: merge conflict → finalize failed 코멘트

- **Setup**
  - TC 브랜치와 develop 에 충돌하는 변경을 만들어 squash merge 가 실패하도록 한다.
- **Trigger & Verify**
  - PR 을 merge 하여 finalize 워크플로우를 실행하고, run 이 `failure` 인지와 PR 코멘트에 `❌ TC Branch Finalize Failed` 가 추가되었는지 확인한다.

#### TC-F06: merge 성공 + 브랜치 삭제 실패 → finalize failed 코멘트

- **Setup**
  - merge 는 성공하되, 브랜치 삭제 시점에 다른 세션에서 브랜치를 먼저 삭제하거나 권한을 제한하여 삭제 실패를 유도한다.
- **Trigger & Verify**
  - finalize run 이 실패로 끝나고, 코멘트 메시지에 "merge and/or delete" 실패로 표시되는지 확인한다.

#### TC-F07: Concurrency → 동시 머지 2개 직렬 실행

- **Setup & Trigger**
  - 상단 G-1 시나리오를 그대로 따른다.
- **Verify**
  - 여러 PR 머지에 대해 `tc-branch-finalize.yml` run 의 실행 순서를 확인하고, develop 히스토리가 깨지지 않는지(순차 커밋) 확인한다.

### 5.4 TC-D: tc-branch-finalize (PR closed without merge)

#### TC-D01: PR close (reject) → REST API 로 tc/pr-N 삭제

- **Setup**
  - 정상 App 환경에서 PR open → `tc/pr-N` 생성.
- **Trigger**

```bash
PR_NUMBER=<PR_NUMBER_FOR_REJECT>
gh pr close "${PR_NUMBER}" --repo tw-kang/cubrid
```

- **Verify**

```bash
BRANCH="tc/pr-${PR_NUMBER}"

gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 3

gh api repos/tw-kang/cubrid-testcases/branches \
  --jq "[.[].name | select(. == \"${BRANCH}\")]"

gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
  --jq "[.[].name | select(. == \"${BRANCH}\")]"
```

- **기대 결과**
  - 두 TC repo 에서 브랜치가 삭제되며, run 은 `success`.

#### TC-D02: PR close, TC 브랜치 없음 → skip

- **Setup**
  - TC 브랜치를 생성하지 않은 PR 을 close.
- **Verify**
  - finalize run 로그에서 `Branch 'tc/pr-N' not found ... skipping` 메시지가 출력되고, run 이 성공으로 끝난다.

#### TC-D03: 삭제 실패 → delete failed 코멘트

- **Setup**
  - G-2 시나리오와 동일하게 App 권한을 제거하여 삭제가 실패하도록 만든다.
- **Trigger & Verify**
  - PR close 후 finalize run 을 확인하고, PR 코멘트에 `⚠️ TC Branch Delete Failed` 메시지가 추가되는지 확인한다.

### 5.5 TC-C: 공통 검증

#### TC-C01: APP_TOKEN 로그 마스킹

- **방법**
  - `tc-branch-sync.yml`, `tc-branch-finalize.yml` 의 run 로그를 확인하여, `APP_TOKEN` 실제 값이 로그에 노출되지 않는지 확인한다.

#### TC-C02: timeout-minutes 설정

- **방법**
  - 각 워크플로우 정의에서 job 수준 `timeout-minutes: 10` 이 설정되어 있는지, 그리고 의도적으로 긴 작업을 만들어 timeout 시 run 이 실패하는지(옵션) 확인한다.

#### TC-C03: loop 내 per-repo 결과가 GITHUB_OUTPUT 에 기록됨

- **방법**
  - `steps.sync-tc.outputs.pub_sync_status`, `steps.sync-tc.outputs.priv_sync_status`,  
    `steps.revert-tc.outputs.pub_status`, `steps.merge-tc.outputs.pub_status` 등 출력 변수가 코멘트 메시지에 올바르게 반영되는지 시나리오 전반에서 확인한다.

---

## 6. TODO 기반 TC 목록

빌드시 에이전트가 개별 TC 를 선택적으로 실행할 수 있도록 TODO 형태의 목록을 제공한다.  
각 TODO 항목은 상기 시나리오의 TC 하나와 정확히 매핑된다.

- [ ] **TC-S01**: PR opened → tc/pr-N 양쪽 TC 브랜치 생성
- [ ] **TC-S02**: PR synchronize → 기존 tc/pr-N skip (idempotent)
- [ ] **TC-S03**: PR reopened → 기존 tc/pr-N skip
- [ ] **TC-S04**: Sync 실패 (App 권한 없음) → PR 에 에러 코멘트 게시
- [ ] **TC-S05**: Concurrency → 동일 PR 에 연속 synchronize 발생 시 이전 run 취소
- [ ] **TC-R01**: Revert PR body 에서 `Reverts #N` 패턴 감지
- [ ] **TC-R02**: Revert 적용 + 성공 코멘트
- [ ] **TC-R03**: 원본 PR 에 TC 변경 없음 → silent skip
- [ ] **TC-R04**: Revert conflict → 수동 처리 요청 코멘트
- [ ] **TC-R05**: 기존 revert 브랜치 존재 → force-push
- [ ] **TC-R06**: Revert PR title 헤더 유지
- [ ] **TC-R07**: Revert 실패 (App 오류) → 에러 코멘트 게시
- [ ] **TC-F01**: PR merged → squash merge + 브랜치 삭제
- [ ] **TC-F02**: PR merged, TC 브랜치 없음 → skip
- [ ] **TC-F03**: pub 만 TC 변경 → pub 만 merge, priv skip
- [ ] **TC-F04**: 헤더 추출 → squash 커밋 메시지에 [CBRD-1234] 포함
- [ ] **TC-F05**: merge conflict → finalize failed 코멘트
- [ ] **TC-F06**: merge 성공 + 브랜치 삭제 실패 → finalize failed 코멘트
- [ ] **TC-F07**: Concurrency → 동시 머지 2개 직렬 실행
- [ ] **TC-D01**: PR close (reject) → REST API 로 tc/pr-N 삭제
- [ ] **TC-D02**: PR close (reject), TC 브랜치 없음 → skip
- [ ] **TC-D03**: PR close (reject), 삭제 실패 → delete failed 코멘트
- [ ] **TC-C01**: APP_TOKEN 로그 마스킹 확인
- [ ] **TC-C02**: Job timeout-minutes: 10 설정 확인
- [ ] **TC-C03**: loop 내 per-repo 결과가 GITHUB_OUTPUT 에 정확히 기록됨

---

## 7. 기존 테스트 결과 요약

리팩토링 이전 `tw-kang` fork 환경에서 이미 수행된 통합 시나리오 결과는 아래와 같다.  
리팩토링 후에도 동일한 시나리오가 통과해야 한다.

| 시나리오 ID | 설명 | 결과 | 엔진 PR | Run ID |
|------------|------|------|--------|--------|
| A | PR open → TC 브랜치 생성 | ✅ PASS | #22 | `22306576636` |
| B | PR merge → develop 머지 + 삭제 | ✅ PASS | #22 | `22306870071` |
| C | PR close (reject) → 브랜치 삭제 | ✅ PASS | #26 | `22307785553` |
| D | TC 브랜치 이미 존재 (skip) | ✅ PASS | #22 synchronize | `22306793474` |
| E | App 미설치 → 실패 코멘트 게시 | ✅ PASS | #27 | `22307844958` |
| F | Revert PR → TC 자동 revert | ✅ PASS | #28/#29 | `22343057933` |

# Fork 환경 테스트 가이드

**테스트 환경**

| 항목 | 값 |
|------|-----|
| 엔진 fork | `tw-kang/cubrid` |
| TC 공개 fork | `tw-kang/cubrid-testcases` |
| TC 비공개 fork | `tw-kang/cubrid-testcases-private-ex` |
| 개발 브랜치 (default) | `cubridqa-1320` |
| 테스트 base 브랜치 | `test/sync-tc` |

> `pull_request_target`은 PR base가 아닌 **default branch(`cubridqa-1320`)**에서 워크플로우 파일을 읽는다.
> 워크플로우 수정 후 반드시 `cubridqa-1320`에 push하고 `test/sync-tc`에 merge해야 한다.

---

## 1. 사전 준비

```bash
# 워크플로우 파일을 default branch에 push 후 test/sync-tc에 반영
git checkout cubridqa-1320 && git pull twkang cubridqa-1320
git checkout test/sync-tc && git merge cubridqa-1320 && git push twkang test/sync-tc

# TC fork develop 복원 (필요 시)
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

Secrets 확인: `tw-kang/cubrid` Settings → Secrets → `TC_APP_ID`(실제 App ID), `TC_APP_PRIVATE_KEY`(PEM)

---

## 2. Scenario F: Revert PR → TC 자동 revert

**목적**: `tc-branch-sync.yml`의 Revert 감지 로직 검증.

### F-1. 원본 PR 생성 및 TC 변경 추가

```bash
git checkout cubridqa-1320 && git checkout -b feature/test-revert-base
echo "revert base" >> dummy_revert.txt
git add dummy_revert.txt && git commit -m "test: base commit for revert scenario"
git push twkang feature/test-revert-base
gh pr create --repo tw-kang/cubrid --base test/sync-tc --head feature/test-revert-base \
  --title "test: revert scenario base PR" --body "TC 브랜치 생성 후 revert 테스트용 원본 PR"
```

`tc-branch-sync.yml` 실행 후 `tc/pr-<N>` 브랜치가 생성되면 TC 변경을 추가한다.

```bash
BASE_PR=<PR_NUMBER>
git clone --depth=1 --branch tc/pr-${BASE_PR} https://github.com/tw-kang/cubrid-testcases.git tc-pub-revert
cd tc-pub-revert
echo "TC change for revert test" >> testcase_revert.txt
git add testcase_revert.txt && git commit -m "test: add TC change for engine PR #${BASE_PR}"
git push origin tc/pr-${BASE_PR}
cd ..
```

### F-2. 원본 PR 머지

```bash
gh pr merge ${BASE_PR} --repo tw-kang/cubrid --merge --delete-branch

# tc-branch-finalize.yml 완료 후 TC develop 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'
# 기대값: "Merge TC branch 'tc/pr-${BASE_PR}' into develop (Engine PR #${BASE_PR})"
```

### F-3. Revert PR 생성

GitHub UI에서 원본 PR 페이지 → **Revert** 버튼 클릭 (권장). 자동으로 body에 `Reverts tw-kang/cubrid#N` 포함됨.

CLI로 생성 시:

```bash
git checkout cubridqa-1320 && git checkout -b revert/pr-${BASE_PR}
# 주의: base와 diff가 있어야 함 (코드 수정 포함)
git push twkang revert/pr-${BASE_PR}
gh pr create --repo tw-kang/cubrid --base test/sync-tc --head revert/pr-${BASE_PR} \
  --title "Revert \"test: revert scenario base PR\"" \
  --body "Reverts tw-kang/cubrid#${BASE_PR}"
```

### F-4. 검증

```bash
REVERT_PR=<REVERT_PR_NUMBER>
# tc-branch-sync.yml 실행 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 3
# tc/pr-<REVERT_PR> 브랜치 존재 확인
gh api repos/tw-kang/cubrid-testcases/branches \
  --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'
# revert 커밋 메시지 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=tc/pr-${REVERT_PR}" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'

# PR 코멘트 확인
gh pr view ${REVERT_PR} --repo tw-kang/cubrid --comments
```

**기대 결과**
| 항목 | 기대값 |
|------|--------|
| `tc-branch-sync.yml` 결론 | `success` |
| `tc/pr-<REVERT_PR>` 최신 커밋 | `Revert TC changes for engine PR #<BASE_PR> (reverted by engine PR #<REVERT_PR>)` |
| Revert PR 코멘트 | `ℹ️ TC Revert Applied (PR #<REVERT_PR>)` |
---

## 3. 테스트 후 정리

```bash
# TC fork tc/ 브랜치 삭제
for branch in $(gh api repos/tw-kang/cubrid-testcases/branches \
    --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'); do
  gh api --method DELETE "repos/tw-kang/cubrid-testcases/git/refs/heads/${branch}"
done
for branch in $(gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
    --jq '[.[] | select(.name | startswith("tc/"))] | .[].name'); do
  gh api --method DELETE "repos/tw-kang/cubrid-testcases-private-ex/git/refs/heads/${branch}"
done

# 열린 PR close
gh pr list --repo tw-kang/cubrid --json number --jq '.[].number' | \
  xargs -I{} gh pr close {} --repo tw-kang/cubrid
# 테스트 브랜치 삭제
git checkout cubridqa-1320
git push twkang --delete feature/test-revert-base revert/pr-${BASE_PR} 2>/dev/null
git branch -D feature/test-revert-base revert/pr-${BASE_PR} 2>/dev/null

# TC fork develop 재동기화
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop

# 로컈 clone 정리
rm -rf tc-pub-revert
```

---

## 4. 추가 시나리오: finalize/delete 안전성

### G-1. 동시 머지 시 finalize 직렬화 확인

**목적**: `tc-branch-finalize.yml`의 `concurrency` 설정으로 TC develop 머지가 직렬화되는지 확인.

```bash
# PR 2개를 거의 동시에 머지
PR1=<PR_NUMBER_1>
PR2=<PR_NUMBER_2>

gh pr merge ${PR1} --repo tw-kang/cubrid --merge --delete-branch &
gh pr merge ${PR2} --repo tw-kang/cubrid --merge --delete-branch &
wait

# 두 PR 모두에 대해 tc-branch-finalize.yml 실행 로그 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 5
```

**기대 결과**

- 두 run이 동시에 트리거되지만, 실제 머지/푸시 시점은 직렬로 수행되어 TC develop 히스토리가 깨지지 않는다.

### G-2. 비머지 PR close 시 TC 브랜치 삭제 실패 처리

**목적**: `Delete TC branch (PR closed without merge)` 단계 실패 시 PR 코멘트가 생성되는지 확인.

1. intentionally 잘못된 권한/레포 설정으로 TC 삭제가 실패하도록 조작한다. (예: 테스트용 fork에서 App 권한을 일시적으로 제거)
2. 엔진 PR을 open → TC 브랜치 생성 후, **merge 없이 close** 한다.

```bash
REJECT_PR=<PR_NUMBER>
gh pr close ${REJECT_PR} --repo tw-kang/cubrid
```

**기대 결과**

- `tc-branch-finalize.yml` run이 `failure`로 끝나고, PR 타임라인에 `⚠️ TC Branch Delete Failed` 코멘트가 추가된다.

### G-3. 머지 성공 후 브랜치 삭제 실패 처리

**목적**: 머지는 성공했지만 `git push --delete`가 실패하는 경우, 에러 메시지가 머지/삭제를 포괄하도록 동작하는지 확인.

1. TC 브랜치에 대해 삭제 권한이 없도록 일시적으로 설정하거나, 삭제 시점에 브랜치를 다른 세션에서 이미 삭제해 race를 유도한다.
2. 엔진 PR을 merge하여 `tc-branch-finalize.yml`을 실행한다.

**기대 결과**

- 워크플로우 실패 시 PR 타임라인에 `❌ TC Branch Finalize Failed` 코멘트가 추가되고, 메시지 내용이 \"merge 실패\"가 아닌 \"finalize (merge and/or delete) 실패\"로 표시된다.
