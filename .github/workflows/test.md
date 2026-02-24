# CUBRID TC Branch 자동화 테스트 가이드

## 1. 개요

이 문서는 `tc-branch-sync.yml`과 `tc-branch-finalize.yml` 워크플로우의 모든 구현 기능을 검증하기 위한 22개의 테스트 시나리오를 제공합니다.

각 워크플로우의 동작 원리, 아키텍처, 설계는 [`description.md`](./description.md)를 참고하세요.

**워크플로우 파일**
- [`tc-branch-sync.yml`](./tc-branch-sync.yml): PR 이벤트 시 TC 브랜치 자동 생성 및 revert 처리
- [`tc-branch-finalize.yml`](./tc-branch-finalize.yml): PR merge/close 시 TC 브랜치 최종화 (squash merge 또는 삭제)

---

## 2. 테스트 환경

### 2.1 저장소 구성

| 역할 | 저장소 |
|------|--------|
| 엔진 | `tw-kang/cubrid` |
| TC 공개 | `tw-kang/cubrid-testcases` |
| TC 비공개 | `tw-kang/cubrid-testcases-private-ex` |

### 2.2 브랜치 설정

| 항목 | 값 | 설명 |
|------|-----|------|
| 기본 브랜치 | `cubridqa-1320` | 워크플로우 파일을 읽는 브랜치 |
| 테스트 base 브랜치 | `test/sync-tc` | PR이 merge되는 브랜치 |
| TC 브랜치 prefix | `tc/pr-` | 생성되는 TC 브랜치 이름 (예: `tc/pr-123`) |

### 2.3 중요: `pull_request_target` 동작

`pull_request_target`은 PR base 브랜치가 아닌 **default branch(`cubridqa-1320`)**에서 워크플로우 파일을 읽습니다.

**따라서 워크플로우 수정 후에는 반드시 다음 단계를 수행해야 합니다:**

```bash
git checkout cubridqa-1320 && git pull twkang cubridqa-1320
git checkout test/sync-tc && git merge cubridqa-1320 && git push twkang test/sync-tc
```

---

## 3. 사전 준비

### 3.1 워크플로우 배포

```bash
# 1. 워크플로우 파일을 default branch에 반영
git checkout cubridqa-1320 && git pull twkang cubridqa-1320
git checkout test/sync-tc && git merge cubridqa-1320 && git push twkang test/sync-tc

# 2. TC fork develop 초기화 (필요시)
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

### 3.2 Secrets 확인

`tw-kang/cubrid` 저장소의 Settings → Secrets에서 다음 항목 확인:

- `TC_APP_ID`: GitHub App의 숫자 ID
- `TC_APP_PRIVATE_KEY`: PEM 형식의 Private Key 전체 내용

---

## 4. 테스트 시나리오 (22개)

### 4.1 Group SYNC: tc-branch-sync.yml - 일반 PR (5개)

정상 PR open/synchronize/reopen 시 TC 브랜치 생성 및 스킵 로직을 검증합니다.

#### TC-SYNC-01: PR opened → 양쪽 TC 저장소에 tc/pr-N 생성

**목적**: PR 생성 시 공개/비공개 TC 저장소 모두에 동일한 이름의 브랜치가 생성되는지 검증

**사전 조건**
- `cubridqa-1320` 및 `test/sync-tc` 브랜치가 워크플로우 파일 최신 버전 포함
- GitHub App 정상 설정

**실행 단계**

```bash
git checkout cubridqa-1320
git checkout -b feature/tc-sync-01
echo "tc-sync-01 test file" > tc_sync_01.txt
git add tc_sync_01.txt
git commit -m "test: TC-SYNC-01 PR opened"
git push twkang feature/tc-sync-01

PR_NUMBER=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/tc-sync-01 \
  --title "test: TC-SYNC-01 PR opened" \
  --body "Verify tc/pr-N creation on PR open" \
  --json number --jq '.number')

echo "Created PR: ${PR_NUMBER}"
```

**검증**

```bash
PR_NUMBER=<PR_NUMBER_FROM_ABOVE>
BRANCH="tc/pr-${PR_NUMBER}"

# 워크플로우 실행 확인 (약 30초~1분 대기)
sleep 10
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1 --json status,conclusion

# 공개 TC 저장소 브랜치 확인
gh api repos/tw-kang/cubrid-testcases/branches --jq "[.[] | select(.name == \"${BRANCH}\") | .name]"

# 비공개 TC 저장소 브랜치 확인
gh api repos/tw-kang/cubrid-testcases-private-ex/branches --jq "[.[] | select(.name == \"${BRANCH}\") | .name]"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 상태 | `success` |
| 워크플로우 결론 | `success` |
| `tw-kang/cubrid-testcases` | 브랜치 `tc/pr-<PR_NUMBER>` 존재 |
| `tw-kang/cubrid-testcases-private-ex` | 브랜치 `tc/pr-<PR_NUMBER>` 존재 |
| PR 코멘트 | 없음 (정상 동작) |

**정리 (후속 TC를 위해 PR 유지)**

```bash
# 이후 TC-SYNC-02, TC-SYNC-03에서 재사용
```

---

#### TC-SYNC-02: PR synchronize → 기존 tc/pr-N skip (멱등성)

**목적**: 동일 PR에 새로운 커밋이 push될 때 기존 TC 브랜치를 새로 생성하지 않고 skip하는지 검증

**사전 조건**
- TC-SYNC-01에서 생성한 PR 사용

**실행 단계**

```bash
PR_NUMBER=<PR_NUMBER_FROM_TC_SYNC_01>

git checkout feature/tc-sync-01
echo "tc-sync-02 additional file" > tc_sync_02.txt
git add tc_sync_02.txt
git commit -m "test: TC-SYNC-02 synchronize"
git push twkang feature/tc-sync-01

sleep 10
```

**검증**

```bash
BRANCH="tc/pr-${PR_NUMBER}"

# 워크플로우 실행 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1 --json status,conclusion

# 브랜치는 여전히 존재하고, 중복 생성되지 않음
gh api repos/tw-kang/cubrid-testcases/branches --jq "[.[] | select(.name == \"${BRANCH}\") | .name]"
gh api repos/tw-kang/cubrid-testcases-private-ex/branches --jq "[.[] | select(.name == \"${BRANCH}\") | .name]"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 상태 | `success` |
| TC 브랜치 상태 | 기존과 동일 (새로 생성되지 않음) |
| PR 코멘트 | 없음 (정상 동작) |

---

#### TC-SYNC-03: PR reopened → 기존 tc/pr-N skip

**목적**: PR을 close 후 reopen할 때 기존 TC 브랜치가 삭제되지 않고 skip되는지 검증

**사전 조건**
- TC-SYNC-01, TC-SYNC-02에서 생성한 PR 사용

**실행 단계**

```bash
PR_NUMBER=<PR_NUMBER_FROM_TC_SYNC_01>

# PR close
gh pr close "${PR_NUMBER}" --repo tw-kang/cubrid

sleep 5

# PR reopen
gh pr reopen "${PR_NUMBER}" --repo tw-kang/cubrid

sleep 10
```

**검증**

```bash
BRANCH="tc/pr-${PR_NUMBER}"

# 워크플로우 실행 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1 --json status,conclusion

# TC 브랜치 여전히 존재
gh api repos/tw-kang/cubrid-testcases/branches --jq "[.[] | select(.name == \"${BRANCH}\") | .name]"
gh api repos/tw-kang/cubrid-testcases-private-ex/branches --jq "[.[] | select(.name == \"${BRANCH}\") | .name]"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 상태 | `success` |
| TC 브랜치 상태 | 삭제되지 않음 |
| PR 상태 | open 상태 복원됨 |

**정리**

```bash
PR_NUMBER=<PR_NUMBER_FROM_TC_SYNC_01>
gh pr close "${PR_NUMBER}" --repo tw-kang/cubrid
```

---

#### TC-SYNC-04: App 권한 없음 → 실패 코멘트 게시

**목적**: GitHub App이 잘못 설정되었을 때 워크플로우가 실패하고 PR에 명확한 에러 코멘트를 게시하는지 검증

**사전 조건**
- 별도 테스트 환경 또는 `TC_APP_ID`/`TC_APP_PRIVATE_KEY` 일시 제거 (본 저장소에서는 권장하지 않음)
- 이 TC는 선택적으로 실행 (실패 경로 테스트)

**실행 단계 (시뮬레이션)**

일반적으로 이는 다음과 같은 상황에서 발생합니다:
- `TC_APP_ID` 또는 `TC_APP_PRIVATE_KEY` 시크릿이 누락되었을 때
- GitHub App이 TC 저장소에 설치되지 않았을 때
- GitHub App의 Contents 쓰기 권한이 없을 때

**검증**

```bash
PR_NUMBER=<PR_NUMBER_WITH_BROKEN_APP>

# 워크플로우 실행 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1 --json status,conclusion

# PR 코멘트 확인
gh pr view "${PR_NUMBER}" --repo tw-kang/cubrid --comments
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 상태 | `failure` 또는 `success` (실패 처리되었으므로) |
| 워크플로우 결론 | `failure` |
| PR 코멘트 | `⚠️ **TC Branch Sync Failed**` 포함 |
| 코멘트 내용 | `cubrid-testcases` 및 `cubrid-testcases-private-ex` 상태 명시 |

---

#### TC-SYNC-05: 동시 synchronize → 이전 run 취소

**목적**: 동일 PR에 빠르게 연속으로 커밋이 push될 때 `concurrency.cancel-in-progress: true` 설정에 의해 이전 run이 취소되는지 검증

**사전 조건**
- 새로운 PR 생성 (TC 브랜치가 이미 존재하지 않는 상태)

**실행 단계**

```bash
git checkout cubridqa-1320
git checkout -b feature/tc-sync-05
echo "sync 5 - initial" > tc_sync_05.txt
git add tc_sync_05.txt
git commit -m "test: TC-SYNC-05 initial"
git push twkang feature/tc-sync-05

PR_NUMBER=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/tc-sync-05 \
  --title "test: TC-SYNC-05 concurrency" \
  --body "Verify concurrency cancellation" \
  --json number --jq '.number')

# PR이 생성되고 TC 브랜치가 생성되기 시작한 후, 빠르게 두 번 더 push
sleep 2

echo "sync 5 - push A" >> tc_sync_05.txt
git add tc_sync_05.txt
git commit -m "test: TC-SYNC-05 push A"
git push twkang feature/tc-sync-05 &

sleep 1

echo "sync 5 - push B" >> tc_sync_05.txt
git add tc_sync_05.txt
git commit -m "test: TC-SYNC-05 push B"
git push twkang feature/tc-sync-05 &

wait
sleep 10
```

**검증**

```bash
# 여러 run 확인 (최근 5개)
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 5 \
  --json status,conclusion,headBranch,runNumber,name

# 예상: 여러 run이 있지만, 같은 PR 번호에 대해 오래된 것은 'cancelled', 최신 것만 'success'
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 첫 번째 run | `cancelled` 상태 |
| 두 번째 run | `cancelled` 상태 (있다면) |
| 최종 run | `success` |
| TC 브랜치 | 1개만 생성됨 |

**정리**

```bash
gh pr close "${PR_NUMBER}" --repo tw-kang/cubrid
```

---

### 4.2 Group REVERT: tc-branch-sync.yml - Revert PR (7개)

Revert PR 감지, revert 적용, conflict 처리, 성공/실패 코멘트 게시 로직을 검증합니다.

#### TC-REVERT-01: `Reverts #N` 패턴 감지

**목적**: PR body에 `Reverts tw-kang/cubrid#N` 패턴이 포함되었을 때 Revert 흐름으로 진입하는지 검증

**사전 조건**
- 기본 PR 생성 및 TC 브랜치 생성 완료
- 기본 PR merge 후 TC develop에 merge commit 생성
- Revert PR body에 `Reverts tw-kang/cubrid#<BASE_PR>` 패턴 포함

**실행 단계**

```bash
# Step 1: 기본 PR 생성
git checkout cubridqa-1320
git checkout -b feature/revert-base-01
echo "revert base 01" > revert_base_01.txt
git add revert_base_01.txt
git commit -m "test: TC-REVERT-01 base PR"
git push twkang feature/revert-base-01

BASE_PR=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/revert-base-01 \
  --title "test: TC-REVERT-01 base" \
  --body "Base PR for revert test" \
  --json number --jq '.number')

echo "Created base PR: ${BASE_PR}"
sleep 10

# Step 2: TC 브랜치에 TC 변경 추가
BASE_BRANCH="tc/pr-${BASE_PR}"
git clone --depth=1 --branch "${BASE_BRANCH}" \
  https://github.com/tw-kang/cubrid-testcases.git tc-pub-revert-01
cd tc-pub-revert-01
echo "TC change for revert 01" >> revert_tc_01.txt
git add revert_tc_01.txt
git commit -m "test: add TC change for engine PR #${BASE_PR}"
git push origin "${BASE_BRANCH}"
cd ..

# Step 3: 기본 PR merge
gh pr merge "${BASE_PR}" --repo tw-kang/cubrid --merge --delete-branch
sleep 15

# Step 4: Revert PR 생성
git checkout cubridqa-1320
git checkout -b revert/pr-${BASE_PR}
# Note: revert PR는 엔진 코드 변경 포함 필요 (실제로는 GitHub UI에서 "Revert" 버튼 사용 권장)
# CLI로 생성시 일부 수정이 필요하지만, 여기서는 간단히 진행
git push twkang revert/pr-${BASE_PR}

REVERT_PR=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head revert/pr-${BASE_PR} \
  --title "Revert \"test: TC-REVERT-01 base\"" \
  --body "Reverts tw-kang/cubrid#${BASE_PR}" \
  --json number --jq '.number')

echo "Created revert PR: ${REVERT_PR}"
sleep 10
```

**검증**

```bash
REVERT_PR=<REVERT_PR_NUMBER>

# 워크플로우 실행 로그 확인
RUN_ID=$(gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1 --json databaseId --jq '.[0].databaseId')
gh run view "${RUN_ID}" --repo tw-kang/cubrid --log | grep -E "(Detect revert|is_revert|original_pr)"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 로그 | "Detected revert PR: reverts engine PR #<BASE_PR>" 포함 |
| 다음 단계 | Revert TC 처리 step 실행됨 |

---

#### TC-REVERT-02: Revert 적용 + 성공 코멘트

**목적**: TC revert가 정상적으로 적용되고 성공 코멘트가 게시되는지 검증

**사전 조건**
- TC-REVERT-01 시나리오 완료

**검증**

```bash
REVERT_PR=<REVERT_PR_NUMBER>
BASE_PR=<BASE_PR_NUMBER>
REVERT_BRANCH="tc/pr-${REVERT_PR}"

# 워크플로우 상태 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1 --json status,conclusion

# TC 브랜치에서 revert 커밋 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=${REVERT_BRANCH}" --limit 1 \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'

# PR 코멘트 확인
gh pr view "${REVERT_PR}" --repo tw-kang/cubrid --comments | grep -A5 "TC Revert Applied"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 결론 | `success` |
| TC 브랜치 최신 커밋 메시지 | `Revert TC changes for engine PR #<BASE_PR> (reverted by engine PR #<REVERT_PR>)` 포함 |
| PR 코멘트 | `ℹ️ **TC Revert Applied** (PR #<REVERT_PR>)` 포함 |
| 코멘트 상태 | `pub_status=applied`, `priv_status=applied` |

---

#### TC-REVERT-03: 원본 PR에 TC 변경 없음 → silent skip

**목적**: 원본 PR의 TC 브랜치에 TC 변경이 없을 때 (즉, merge commit이 없을 때) revert를 수행하지 않고 조용히 skip하는지 검증

**사전 조건**
- 기본 PR 생성 후 TC 브랜치만 생성하고 TC 변경은 추가하지 않음

**실행 단계**

```bash
# Step 1: 기본 PR 생성 (TC 변경 없음)
git checkout cubridqa-1320
git checkout -b feature/revert-base-03
echo "revert base 03 - no tc change" > revert_base_03.txt
git add revert_base_03.txt
git commit -m "test: TC-REVERT-03 base PR (no TC change)"
git push twkang feature/revert-base-03

BASE_PR=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/revert-base-03 \
  --title "test: TC-REVERT-03 base (no TC)" \
  --body "Base PR without TC changes" \
  --json number --jq '.number')

sleep 10

# Step 2: TC 변경 추가하지 않고 바로 merge
gh pr merge "${BASE_PR}" --repo tw-kang/cubrid --merge --delete-branch
sleep 15

# Step 3: Revert PR 생성
git checkout cubridqa-1320
git checkout -b revert/pr-${BASE_PR}
git push twkang revert/pr-${BASE_PR}

REVERT_PR=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head revert/pr-${BASE_PR} \
  --title "Revert \"test: TC-REVERT-03 base (no TC)\"" \
  --body "Reverts tw-kang/cubrid#${BASE_PR}" \
  --json number --jq '.number')

sleep 10
```

**검증**

```bash
REVERT_PR=<REVERT_PR_NUMBER>

# 워크플로우 상태
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1 --json status,conclusion

# PR 코멘트 확인 (코멘트가 없어야 함)
gh pr view "${REVERT_PR}" --repo tw-kang/cubrid --comments | wc -l
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 결론 | `success` |
| 워크플로우 로그 | "No TC merge commit found for PR #<BASE_PR>" 포함 |
| PR 코멘트 | 없음 (TC Revert 관련 코멘트 추가 안 됨) |
| TC 브랜치 | 생성되지 않음 |

---

#### TC-REVERT-04: Revert conflict → 수동 처리 요청 코멘트

**목적**: TC revert 시 conflict가 발생하면 그 내용을 PR 코멘트로 알리는지 검증

**사전 조건**
- TC 브랜치에 어떤 변경이 있고, 나중에 develop에서 같은 파일이 다르게 수정되어 conflict 상황 생성

**실행 단계**

이 시나리오는 복잡하므로 고급 테스트로 간주합니다. 실제로는 다음과 같은 상황입니다:

1. 기본 PR merge로 `tc/pr-N`의 변경을 develop에 merge
2. develop에서 같은 파일을 다르게 수정
3. Revert PR 생성 시 `git revert -m 1`이 conflict 발생

**실행 단계 (단순화)**

```bash
# 생략: 복잡한 conflict 시나리오 생성
# 대신 워크플로우 로그에서 "conflict" 상태 발생 확인
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 결론 | `success` (continue-on-error) |
| 워크플로우 로그 | "Revert has conflicts" 포함 |
| PR 코멘트 | `⚠️ **TC Revert Conflict**` 포함 |
| 코멘트 상태 | `pub_status=conflict` 또는 `priv_status=conflict` |

---

#### TC-REVERT-05: 기존 revert 브랜치 존재 → force-push

**목적**: `tc/pr-N` 브랜치가 이미 존재할 때 `git push --force`로 덮어쓰는지 검증

**사전 조건**
- TC-REVERT-02에서 생성한 `tc/pr-<REVERT_PR>` 브랜치가 존재

**실행 단계**

```bash
REVERT_PR=<REVERT_PR_NUMBER>
BASE_PR=<BASE_PR_NUMBER>
REVERT_BRANCH="tc/pr-${REVERT_PR}"

# 현재 revert 브랜치의 커밋 기록
FIRST_COMMIT=$(gh api "repos/tw-kang/cubrid-testcases/commits?sha=${REVERT_BRANCH}" --limit 1 --jq '.[0].sha')
echo "First commit: ${FIRST_COMMIT}"

# 동일 REVERT_PR에 대해 다시 revert 처리 시나리오
# (이미 tc/pr-<REVERT_PR>이 존재하므로 force-push 경로 진입)

# 실제로는 동일 커밋이므로 변경 없지만, 워크플로우는 force-push를 실행
```

**검증**

```bash
REVERT_BRANCH="tc/pr-${REVERT_PR}"

# 브랜치 여전히 존재
gh api repos/tw-kang/cubrid-testcases/branches --jq "[.[] | select(.name == \"${REVERT_BRANCH}\") | .name]"

# 최신 커밋 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=${REVERT_BRANCH}" --limit 1 --jq '.[0].sha'
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| TC 브랜치 | 존재함 (덮어씌워짐) |
| 강제 푸시 경로 | `git push origin HEAD:refs/heads/... --force` 사용 |

---

#### TC-REVERT-06: PR title 헤더 유지

**목적**: Revert 커밋 메시지에 원본 PR title의 헤더(예: `[CBRD-XXXX]`)가 유지되는지 검증

**사전 조건**
- PR title이 `[CBRD-1234]` 형식의 헤더 포함

**실행 단계**

```bash
git checkout cubridqa-1320
git checkout -b feature/revert-base-06
echo "revert base 06" > revert_base_06.txt
git add revert_base_06.txt
git commit -m "test: TC-REVERT-06 header preservation"
git push twkang feature/revert-base-06

BASE_PR=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/revert-base-06 \
  --title "[CBRD-9999] test: TC-REVERT-06 header preservation" \
  --body "Base PR with CBRD header" \
  --json number --jq '.number')

sleep 10

# TC 변경 추가
BASE_BRANCH="tc/pr-${BASE_PR}"
git clone --depth=1 --branch "${BASE_BRANCH}" \
  https://github.com/tw-kang/cubrid-testcases.git tc-pub-revert-06
cd tc-pub-revert-06
echo "TC change" >> tc_06.txt
git add tc_06.txt
git commit -m "test: TC change for #${BASE_PR}"
git push origin "${BASE_BRANCH}"
cd ..

# Merge
gh pr merge "${BASE_PR}" --repo tw-kang/cubrid --merge --delete-branch
sleep 15

# Revert PR
git checkout cubridqa-1320
git checkout -b revert/pr-${BASE_PR}
git push twkang revert/pr-${BASE_PR}

REVERT_PR=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head revert/pr-${BASE_PR} \
  --title "[CBRD-9999] Revert \"test: TC-REVERT-06 header preservation\"" \
  --body "Reverts tw-kang/cubrid#${BASE_PR}" \
  --json number --jq '.number')

sleep 10
```

**검증**

```bash
REVERT_PR=<REVERT_PR_NUMBER>
REVERT_BRANCH="tc/pr-${REVERT_PR}"

# Revert 커밋 메시지 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=${REVERT_BRANCH}" --limit 1 \
  --jq '.[0].commit.message'
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 커밋 메시지 | `[CBRD-9999] Revert TC changes for engine PR #<BASE_PR>...` 포함 |
| 헤더 | 원본 PR title의 `[CBRD-9999]` 부분 보존 |

---

#### TC-REVERT-07: Revert 실패 (App 오류) → 에러 코멘트

**목적**: GitHub App 권한 문제로 revert 처리 실패 시 에러 코멘트가 게시되는지 검증

**사전 조건**
- TC_APP_ID 또는 TC_APP_PRIVATE_KEY 일시 제거 (권장하지 않음) 또는 별도 테스트 환경

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 결론 | `failure` |
| PR 코멘트 | `⚠️ **TC Revert Failed** (PR #<REVERT_PR>)` |
| 코멘트 내용 | `cubrid-testcases` 및 `cubrid-testcases-private-ex` 상태 명시 |

---

### 4.3 Group MERGE: tc-branch-finalize.yml - Merged PR (7개)

PR merge 시 TC 브랜치를 squash merge하고 삭제하는 로직을 검증합니다.

#### TC-MERGE-01: PR merged → squash merge + 브랜치 삭제

**목적**: PR merge 후 TC 브랜치가 develop에 squash merge되고 브랜치가 삭제되는지 검증

**사전 조건**
- 정상 PR open, TC 브랜치 생성, TC 변경 추가 완료

**실행 단계**

```bash
git checkout cubridqa-1320
git checkout -b feature/merge-01
echo "merge 01" > merge_01.txt
git add merge_01.txt
git commit -m "test: TC-MERGE-01 PR for merge"
git push twkang feature/merge-01

PR_NUMBER=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/merge-01 \
  --title "test: TC-MERGE-01 merge" \
  --body "PR for merge finalize test" \
  --json number --jq '.number')

sleep 10

# TC 변경 추가
BRANCH="tc/pr-${PR_NUMBER}"
git clone --depth=1 --branch "${BRANCH}" \
  https://github.com/tw-kang/cubrid-testcases.git tc-pub-merge-01
cd tc-pub-merge-01
echo "TC change for merge 01" >> merge_01_tc.txt
git add merge_01_tc.txt
git commit -m "test: TC change for engine PR #${PR_NUMBER}"
git push origin "${BRANCH}"
cd ..

sleep 5

# PR merge
gh pr merge "${PR_NUMBER}" --repo tw-kang/cubrid --merge --delete-branch
sleep 15
```

**검증**

```bash
PR_NUMBER=<PR_NUMBER>
BRANCH="tc/pr-${PR_NUMBER}"

# 워크플로우 상태
gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 1 --json status,conclusion

# TC develop 최신 커밋 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop" --limit 1 \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}'

# TC 브랜치 삭제 확인 (없어야 함)
gh api repos/tw-kang/cubrid-testcases/branches --jq "[.[] | select(.name == \"${BRANCH}\") | .name] | length"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 결론 | `success` |
| TC develop 최신 커밋 | `Merge TC branch 'tc/pr-<PR_NUMBER>' into develop (Engine PR #<PR_NUMBER>)` |
| TC 브랜치 | 두 저장소 모두에서 삭제됨 (count = 0) |

---

#### TC-MERGE-02: PR merged, TC 브랜치 없음 → skip

**목적**: PR merge 후 해당하는 TC 브랜치가 없을 때 gracefully skip하는지 검증

**사전 조건**
- TC 브랜치 생성 없이 PR open, merge

**실행 단계**

```bash
git checkout cubridqa-1320
git checkout -b feature/merge-02-no-tc
echo "merge 02 - no tc branch" > merge_02.txt
git add merge_02.txt
git commit -m "test: TC-MERGE-02 no TC branch"
git push twkang feature/merge-02-no-tc

PR_NUMBER=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/merge-02-no-tc \
  --title "test: TC-MERGE-02 no TC branch" \
  --body "PR without TC branch creation" \
  --json number --jq '.number')

# 워크플로우가 TC 브랜치를 생성할 시간을 주지 않고 바로 merge (또는 사전에 끄기)
sleep 2

gh pr merge "${PR_NUMBER}" --repo tw-kang/cubrid --merge --delete-branch
sleep 10
```

**검증**

```bash
PR_NUMBER=<PR_NUMBER>

# 워크플로우 상태
gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 1 --json status,conclusion

# 워크플로우 로그 확인
RUN_ID=$(gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 1 --json databaseId --jq '.[0].databaseId')
gh run view "${RUN_ID}" --repo tw-kang/cubrid --log | grep -i "not found"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 결론 | `success` |
| 워크플로우 로그 | "Branch 'tc/pr-<PR_NUMBER>' not found in ..., nothing to merge" |
| PR 코멘트 | 없음 (정상 동작) |

---

#### TC-MERGE-03: 공개 repo만 TC 변경 → 공개만 merge, 비공개 skip

**목적**: 공개 TC 저장소에만 `tc/pr-N`이 있고 비공개에는 없을 때, 공개만 merge하고 비공개는 skip하는지 검증

**사전 조건**
- TC branch 생성 시 공개 repo만 성공, 비공개는 실패하는 상황 또는 수동으로 비공개 브랜치 삭제

**실행 단계**

```bash
git checkout cubridqa-1320
git checkout -b feature/merge-03-pub-only
echo "merge 03 - pub only" > merge_03.txt
git add merge_03.txt
git commit -m "test: TC-MERGE-03 pub only"
git push twkang feature/merge-03-pub-only

PR_NUMBER=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/merge-03-pub-only \
  --title "test: TC-MERGE-03 pub only" \
  --body "TC branch only in public repo" \
  --json number --jq '.number')

sleep 10

# TC 공개 repo에만 변경 추가
BRANCH="tc/pr-${PR_NUMBER}"
git clone --depth=1 --branch "${BRANCH}" \
  https://github.com/tw-kang/cubrid-testcases.git tc-pub-merge-03
cd tc-pub-merge-03
echo "TC change - public only" >> merge_03_tc.txt
git add merge_03_tc.txt
git commit -m "test: TC change for engine PR #${PR_NUMBER}"
git push origin "${BRANCH}"
cd ..

# 비공개 브랜치 수동 삭제 (또는 생성 스킵)
curl -s -X DELETE \
  -H "Authorization: Bearer $(gh auth token)" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/tw-kang/cubrid-testcases-private-ex/git/refs/heads/${BRANCH}"

sleep 5

# PR merge
gh pr merge "${PR_NUMBER}" --repo tw-kang/cubrid --merge --delete-branch
sleep 15
```

**검증**

```bash
PR_NUMBER=<PR_NUMBER>
BRANCH="tc/pr-${PR_NUMBER}"

# 워크플로우 상태
gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 1 --json status,conclusion

# 공개 repo는 merge 상태, 비공개는 skipped
RUN_ID=$(gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 1 --json databaseId --jq '.[0].databaseId')
gh run view "${RUN_ID}" --repo tw-kang/cubrid --log | grep -E "(pub_status|priv_status)"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 공개 repo 상태 | `merged_deleted` |
| 비공개 repo 상태 | `skipped` |
| 워크플로우 결론 | `success` |

---

#### TC-MERGE-04: 헤더 추출 → squash 커밋 메시지에 [CBRD-1234] 포함

**목적**: 엔진 PR title의 헤더가 squash merge 커밋 메시지에 포함되는지 검증

**사전 조건**
- PR title이 `[CBRD-XXXX]` 형식

**실행 단계**

```bash
git checkout cubridqa-1320
git checkout -b feature/merge-04-header
echo "merge 04 - header" > merge_04.txt
git add merge_04.txt
git commit -m "test: TC-MERGE-04 header"
git push twkang feature/merge-04-header

PR_NUMBER=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/merge-04-header \
  --title "[CBRD-8888] test: TC-MERGE-04 header in squash" \
  --body "Verify header in squash merge" \
  --json number --jq '.number')

sleep 10

# TC 변경 추가
BRANCH="tc/pr-${PR_NUMBER}"
git clone --depth=1 --branch "${BRANCH}" \
  https://github.com/tw-kang/cubrid-testcases.git tc-pub-merge-04
cd tc-pub-merge-04
echo "TC change" >> merge_04_tc.txt
git add merge_04_tc.txt
git commit -m "test: TC change"
git push origin "${BRANCH}"
cd ..

sleep 5

# PR merge
gh pr merge "${PR_NUMBER}" --repo tw-kang/cubrid --merge --delete-branch
sleep 15
```

**검증**

```bash
PR_NUMBER=<PR_NUMBER>

# TC develop 최신 커밋 메시지 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop" --limit 1 \
  --jq '.[0].commit.message'
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| squash 커밋 메시지 | `[CBRD-8888] Merge TC branch 'tc/pr-<PR_NUMBER>' into develop (Engine PR #<PR_NUMBER>)` |

---

#### TC-MERGE-05: Merge conflict → finalize failed 코멘트

**목적**: TC 브랜치와 develop이 conflict할 때 merge가 실패하고 PR에 에러 코멘트가 게시되는지 검증

**사전 조건**
- TC 브랜치와 develop에 같은 파일의 충돌하는 변경 존재

**실행 단계 (복잡한 conflict 시나리오)**

이는 고급 테스트입니다. 간단히 말해:
1. TC 브랜치에 파일 A 생성
2. Develop에서 같은 파일 A를 다른 내용으로 생성
3. Merge 시 conflict 발생

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 결론 | `failure` |
| 워크플로우 로그 | "Failed to squash-merge" 포함 |
| PR 코멘트 | `❌ **TC Branch Finalize Failed**` |
| 코멘트 내용 | "merge and/or delete" 실패 명시 |

---

#### TC-MERGE-06: Merge 성공 + 브랜치 삭제 실패 → finalize failed 코멘트

**목적**: Squash merge는 성공했지만 브랜치 삭제가 실패했을 때 에러 코멘트가 게시되는지 검증

**사전 조건**
- 정상 merge 상황에서 삭제 권한 제거 또는 race condition 유도

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| Squash merge | 성공 |
| 브랜치 삭제 | 실패 |
| 워크플로우 결론 | `failure` |
| 워크플로우 로그 | "Failed to delete" 포함 |
| PR 코멘트 | `❌ **TC Branch Finalize Failed**` |
| 상태 | `pub_status=merged_delete_failed` |

---

#### TC-MERGE-07: 동시 머지 → 직렬화 (concurrency)

**목적**: 2개 이상의 PR이 거의 동시에 merge될 때 TC develop 머지가 직렬화되는지 검증

**사전 조건**
- 2개의 독립적인 PR 준비 (각각 TC 브랜치 생성)

**실행 단계**

```bash
# PR 1 준비
git checkout cubridqa-1320
git checkout -b feature/merge-07-a
echo "merge 07 - a" > merge_07_a.txt
git add merge_07_a.txt
git commit -m "test: TC-MERGE-07 concurrent A"
git push twkang feature/merge-07-a

PR1=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/merge-07-a \
  --title "test: TC-MERGE-07 concurrent A" \
  --body "Concurrent merge test A" \
  --json number --jq '.number')

# PR 2 준비
git checkout cubridqa-1320
git checkout -b feature/merge-07-b
echo "merge 07 - b" > merge_07_b.txt
git add merge_07_b.txt
git commit -m "test: TC-MERGE-07 concurrent B"
git push twkang feature/merge-07-b

PR2=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/merge-07-b \
  --title "test: TC-MERGE-07 concurrent B" \
  --body "Concurrent merge test B" \
  --json number --jq '.number')

sleep 10

# TC 변경 추가
for PR in ${PR1} ${PR2}; do
  BRANCH="tc/pr-${PR}"
  git clone --depth=1 --branch "${BRANCH}" \
    https://github.com/tw-kang/cubrid-testcases.git "tc-pub-merge-07-${PR}"
  cd "tc-pub-merge-07-${PR}"
  echo "TC change for PR ${PR}" >> "merge_07_tc_${PR}.txt"
  git add "merge_07_tc_${PR}.txt"
  git commit -m "test: TC change for PR ${PR}"
  git push origin "${BRANCH}"
  cd ..
done

sleep 5

# 두 PR을 거의 동시에 merge
gh pr merge ${PR1} --repo tw-kang/cubrid --merge --delete-branch &
sleep 1
gh pr merge ${PR2} --repo tw-kang/cubrid --merge --delete-branch &
wait

sleep 15
```

**검증**

```bash
# 여러 finalize run 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 3 \
  --json status,conclusion,createdAt

# TC develop 히스토리 확인 (두 merge 커밋이 순서대로 있어야 함)
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop" --limit 5 \
  --jq '.[] | {sha: .sha[0:7], message: .commit.message}'
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| Run 수 | 2개 이상 (동시 트리거됨) |
| 실행 순서 | concurrency 제약으로 직렬화 (한 번에 1개만) |
| TC develop 히스토리 | 두 merge 커밋이 순차적으로 존재 |
| 머지 순서 | develop의 커밋 순서가 PR merge 순서와 일관성 유지 |

---

### 4.4 Group DELETE: tc-branch-finalize.yml - Closed without merge (3개)

PR을 merge하지 않고 close할 때 TC 브랜치 삭제 로직을 검증합니다.

#### TC-DELETE-01: PR close (reject) → REST API로 tc/pr-N 삭제

**목적**: PR을 merge하지 않고 close했을 때 REST API를 사용하여 TC 브랜치가 삭제되는지 검증

**사전 조건**
- 정상 PR open, TC 브랜치 생성

**실행 단계**

```bash
git checkout cubridqa-1320
git checkout -b feature/delete-01
echo "delete 01" > delete_01.txt
git add delete_01.txt
git commit -m "test: TC-DELETE-01 close without merge"
git push twkang feature/delete-01

PR_NUMBER=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/delete-01 \
  --title "test: TC-DELETE-01 reject" \
  --body "PR to close without merge" \
  --json number --jq '.number')

sleep 10

# TC 브랜치 생성 확인
BRANCH="tc/pr-${PR_NUMBER}"
echo "Waiting for TC branch creation..."
sleep 5

# PR close
gh pr close "${PR_NUMBER}" --repo tw-kang/cubrid
sleep 10
```

**검증**

```bash
PR_NUMBER=<PR_NUMBER>
BRANCH="tc/pr-${PR_NUMBER}"

# 워크플로우 상태
gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 1 --json status,conclusion

# TC 브랜치 삭제 확인 (없어야 함)
PUB_COUNT=$(gh api repos/tw-kang/cubrid-testcases/branches --jq "[.[] | select(.name == \"${BRANCH}\") | .name] | length")
PRIV_COUNT=$(gh api repos/tw-kang/cubrid-testcases-private-ex/branches --jq "[.[] | select(.name == \"${BRANCH}\") | .name] | length")

echo "Public repo branch count: ${PUB_COUNT}"
echo "Private repo branch count: ${PRIV_COUNT}"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 결론 | `success` |
| 공개 repo 브랜치 | 삭제됨 (count = 0) |
| 비공개 repo 브랜치 | 삭제됨 (count = 0) |
| PR 코멘트 | 없음 (정상 동작) |

---

#### TC-DELETE-02: PR close, TC 브랜치 없음 → skip

**목적**: PR을 close했지만 TC 브랜치가 없을 때 gracefully skip하는지 검증

**사전 조건**
- TC 브랜치 생성 없이 PR open, close

**실행 단계**

```bash
git checkout cubridqa-1320
git checkout -b feature/delete-02-no-tc
echo "delete 02 - no tc" > delete_02.txt
git add delete_02.txt
git commit -m "test: TC-DELETE-02 close no TC"
git push twkang feature/delete-02-no-tc

PR_NUMBER=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/delete-02-no-tc \
  --title "test: TC-DELETE-02 no TC branch" \
  --body "PR without TC branch" \
  --json number --jq '.number')

# 바로 close (TC 브랜치 생성 시간 주지 않음)
sleep 2
gh pr close "${PR_NUMBER}" --repo tw-kang/cubrid
sleep 10
```

**검증**

```bash
PR_NUMBER=<PR_NUMBER>

# 워크플로우 상태
gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 1 --json status,conclusion

# 워크플로우 로그 확인
RUN_ID=$(gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 1 --json databaseId --jq '.[0].databaseId')
gh run view "${RUN_ID}" --repo tw-kang/cubrid --log | grep -i "not found\|skipping"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 결론 | `success` |
| 워크플로우 로그 | "Branch 'tc/pr-<PR_NUMBER>' not found in ..., skipping" |
| PR 코멘트 | 없음 (정상 동작) |

---

#### TC-DELETE-03: 삭제 실패 → delete failed 코멘트

**목적**: TC 브랜치 삭제가 실패했을 때 PR에 에러 코멘트가 게시되는지 검증

**사전 조건**
- GitHub App의 삭제 권한 제거 또는 race condition 유도

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| 워크플로우 결론 | `failure` |
| 워크플로우 로그 | "Failed to delete" 포함 |
| PR 코멘트 | `⚠️ **TC Branch Delete Failed** (PR #<PR_NUMBER>)` |
| 코멘트 내용 | "Manual cleanup required" 명시 |

---

### 4.5 Group COMMON: Cross-cutting (3개)

보안, 타임아웃, 출력 변수 등 모든 워크플로우에 걸쳐 있는 공통 요소를 검증합니다.

#### TC-COMMON-01: APP_TOKEN 로그 마스킹

**목적**: GitHub App 토큰이 워크플로우 로그에 노출되지 않는지 검증

**검증 방법**

```bash
# 최근 10개의 워크플로우 run 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 5 --json databaseId | jq -r '.[].databaseId' | while read RUN_ID; do
  echo "Checking run ${RUN_ID}..."
  gh run view "${RUN_ID}" --repo tw-kang/cubrid --log | grep -i "x-access-token\|Bearer" && echo "WARNING: Token exposed!" || echo "OK: Token masked"
done

gh run list --repo tw-kang/cubrid --workflow tc-branch-finalize.yml --limit 5 --json databaseId | jq -r '.[].databaseId' | while read RUN_ID; do
  echo "Checking run ${RUN_ID}..."
  gh run view "${RUN_ID}" --repo tw-kang/cubrid --log | grep -i "x-access-token\|Bearer" && echo "WARNING: Token exposed!" || echo "OK: Token masked"
done
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| APP_TOKEN 노출 | 없음 (모든 run에서) |
| `::add-mask::` 호출 | 4개 (tc-branch-sync.yml 2개 + tc-branch-finalize.yml 2개) |
| 로그 | `***` 또는 `[REDACTED]`로 마스킹됨 |

---

#### TC-COMMON-02: Job timeout-minutes: 10

**목적**: 각 워크플로우 job에 `timeout-minutes: 10`이 설정되어 있고, 실제로 timeout이 작동하는지 검증

**검증 방법**

```bash
# 워크플로우 파일 확인
grep -A5 "finalize-tc-branch:\|sync-tc-branch:" /home/dev/cubrid/.github/workflows/tc-branch-*.yml | grep -A5 "timeout-minutes"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| tc-branch-sync.yml | `timeout-minutes: 10` 설정됨 |
| tc-branch-finalize.yml | `timeout-minutes: 10` 설정됨 |
| Timeout 발생 시 | Run status가 `timed_out` 또는 `failure`로 표시 |

---

#### TC-COMMON-03: 루프 내 per-repo 결과가 GITHUB_OUTPUT에 기록됨

**목적**: 각 step에서 `pub_sync_status`, `priv_sync_status`, `pub_status`, `priv_status` 등이 정확하게 GITHUB_OUTPUT에 기록되고 코멘트에 반영되는지 검증

**검증 방법**

TC-SYNC-01, TC-REVERT-02, TC-MERGE-01 등에서 다음 확인:

```bash
# 워크플로우 로그에서 GITHUB_OUTPUT 기록 확인
RUN_ID=<RUN_ID>
gh run view "${RUN_ID}" --repo tw-kang/cubrid --log | grep "GITHUB_OUTPUT\|_status=\|_sync_status="

# PR 코멘트에서 status 변수 반영 확인
gh pr view <PR_NUMBER> --repo tw-kang/cubrid --comments | grep -E "pub_|priv_|status"
```

**기대 결과**

| 항목 | 기대값 |
|------|--------|
| SYNC step output | `pub_sync_status`, `priv_sync_status` 포함 |
| REVERT step output | `pub_status`, `priv_status` 포함 |
| MERGE step output | `pub_merge_done`, `priv_merge_done`, `pub_status`, `priv_status` |
| 코멘트에 반영 | 모든 status 값이 코멘트 본문에 표시됨 |

---

## 5. 테스트 의존성 및 실행 순서

아래 그룹은 같은 PR을 재사용하여 setup 오버헤드를 줄일 수 있습니다:

### 연결된 테스트 체인

```
TC-SYNC-01 (PR open + tc/pr-N 생성)
    ↓
TC-SYNC-02 (동일 PR synchronize)
    ↓
TC-SYNC-03 (동일 PR close + reopen)
    ↓
[cleanup or merge]

TC-REVERT-01 (기본 PR + Revert PR 생성) → `Reverts #N` 감지 검증
    ↓
TC-REVERT-02 (동일 Revert PR 재확인) → 성공 코멘트 검증

TC-MERGE-01 (신규 PR open + merge) → squash merge + 삭제
TC-MERGE-02 (신규 PR open + merge) → TC 브랜치 없음
TC-MERGE-03 (신규 PR open + 공개만 변경) → 공개만 merge
TC-MERGE-04 (신규 PR open with [CBRD-XXXX]) → 헤더 유지
TC-MERGE-05 (신규 PR open + conflict) → conflict 처리
TC-MERGE-06 (신규 PR open + merge fail) → merge OK + delete fail
TC-MERGE-07 (2개 PR 동시 merge) → 직렬화 검증

TC-DELETE-01 (신규 PR open + close) → REST API delete
TC-DELETE-02 (신규 PR open + close) → TC 브랜치 없음
TC-DELETE-03 (신규 PR open + delete fail) → delete failed

TC-COMMON-01 (모든 run에서 검증) → APP_TOKEN masking
TC-COMMON-02 (워크플로우 정의 확인) → timeout-minutes
TC-COMMON-03 (TC-SYNC-01, TC-REVERT-02, TC-MERGE-01 중 하나) → OUTPUT 변수
```

### 권장 실행 순서 (빠른 피드백)

1. **Group SYNC** (5개): 기본 동작 확인
   - TC-SYNC-01, 02, 03 (같은 PR)
   - TC-SYNC-04, 05 (별도 PR)
   
2. **Group COMMON** (3개): 공통 검증
   - TC-COMMON-01, 02, 03
   
3. **Group MERGE** (7개): 안전성 검증
   - TC-MERGE-01 (기본)
   - TC-MERGE-02 (edge case)
   - TC-MERGE-03 (edge case)
   - TC-MERGE-04 (header)
   - TC-MERGE-05 (conflict)
   - TC-MERGE-06 (delete fail)
   - TC-MERGE-07 (concurrency)
   
4. **Group DELETE** (3개): close-without-merge 검증
   - TC-DELETE-01, 02, 03
   
5. **Group REVERT** (7개): 고급 기능
   - TC-REVERT-01 (감지)
   - TC-REVERT-02 (적용)
   - TC-REVERT-03 (no changes)
   - TC-REVERT-04 (conflict)
   - TC-REVERT-05 (force-push)
   - TC-REVERT-06 (header)
   - TC-REVERT-07 (error)

---

## 6. 테스트 정리

### 6.1 TC 브랜치 정리

```bash
# 공개 TC 저장소의 모든 tc/ 프리픽스 브랜치 삭제
for branch in $(gh api repos/tw-kang/cubrid-testcases/branches \
    --jq '[.[] | select(.name | startswith("tc/")) | .name] | .[]'); do
  echo "Deleting ${branch}..."
  gh api --method DELETE "repos/tw-kang/cubrid-testcases/git/refs/heads/${branch}"
done

# 비공개 TC 저장소의 모든 tc/ 프리픽스 브랜치 삭제
for branch in $(gh api repos/tw-kang/cubrid-testcases-private-ex/branches \
    --jq '[.[] | select(.name | startswith("tc/")) | .name] | .[]'); do
  echo "Deleting ${branch}..."
  gh api --method DELETE "repos/tw-kang/cubrid-testcases-private-ex/git/refs/heads/${branch}"
done
```

### 6.2 엔진 저장소 PR 및 브랜치 정리

```bash
# 열린 모든 PR close
gh pr list --repo tw-kang/cubrid --state open --json number --jq '.[].number' | \
  xargs -I{} sh -c 'gh pr close {} --repo tw-kang/cubrid'

# 테스트 브랜치 삭제 (로컬)
git checkout cubridqa-1320
for branch in $(git branch | grep "feature/\|revert/\|test/"); do
  git branch -D "${branch}" 2>/dev/null
done

# 원격 테스트 브랜치 삭제
git push twkang --delete \
  $(git branch -r | grep "feature/\|revert/\|test/" | sed 's|origin/||') 2>/dev/null
```

### 6.3 TC 저장소 develop 재동기화

```bash
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

### 6.4 로컬 clone 정리

```bash
rm -rf tc-pub-* tc-priv-* tc-revert-*
```

---

## 7. 테스트 TODO 체크리스트

아래 항목들을 순서대로 완료합니다. 각 항목은 해당 테스트 케이스를 의미합니다.

### Group SYNC (5개)
TV|
QR|- [x] **TC-SYNC-01**: PR opened → tc/pr-N 양쪽 저장소 생성
XS|- [x] **TC-SYNC-02**: PR synchronize → 기존 브랜치 skip (멱등성)
JV|- [x] **TC-SYNC-03**: PR reopened → 기존 브랜치 skip
VY|- [x] **TC-SYNC-04**: App 오류 → 실패 코멘트 게시 (구현 확인)
VB|- [x] **TC-SYNC-05**: 동시 sync → 이전 run 취소
ZR|
TV|
MP|- [x] **TC-SYNC-01**: PR opened → tc/pr-N 양쪽 저장소 생성
MS|- [x] **TC-SYNC-02**: PR synchronize → 기존 브랜치 skip (멱등성)
ZH|- [x] **TC-SYNC-03**: PR reopened → 기존 브랜치 skip
MX|- [ ] **TC-SYNC-04**: App 오류 → 실패 코멘트 게시
RT|- [ ] **TC-SYNC-05**: 동시 sync → 이전 run 취소
ZR|

- [ ] **TC-SYNC-01**: PR opened → tc/pr-N 양쪽 저장소 생성
- [ ] **TC-SYNC-02**: PR synchronize → 기존 브랜치 skip (멱등성)
- [ ] **TC-SYNC-03**: PR reopened → 기존 브랜치 skip
- [ ] **TC-SYNC-04**: App 오류 → 실패 코멘트 게시
- [ ] **TC-SYNC-05**: 동시 sync → 이전 run 취소

### Group REVERT (7개)
KZ|
TR|- [x] **TC-REVERT-01**: `Reverts #N` 패턴 감지
ZK|- [x] **TC-REVERT-02**: Revert 적용 + 성공 코멘트
VK|- [x] **TC-REVERT-03**: 원본 TC 변경 없음 → silent skip
NR|- [x] **TC-REVERT-04**: Revert conflict → 수동 처리 코멘트 (구현 확인)
NJ|- [x] **TC-REVERT-05**: 기존 revert 브랜치 → force-push
XK|- [x] **TC-REVERT-06**: PR title 헤더 유지
SX|- [x] **TC-REVERT-07**: Revert 실패 (App 오류) → 에러 코멘트 (구현 확인)
TQ|

- [ ] **TC-REVERT-01**: `Reverts #N` 패턴 감지
- [ ] **TC-REVERT-02**: Revert 적용 + 성공 코멘트
- [ ] **TC-REVERT-03**: 원본 TC 변경 없음 → silent skip
- [ ] **TC-REVERT-04**: Revert conflict → 수동 처리 코멘트
- [ ] **TC-REVERT-05**: 기존 revert 브랜치 → force-push
- [ ] **TC-REVERT-06**: PR title 헤더 유지
- [ ] **TC-REVERT-07**: Revert 실패 (App 오류) → 에러 코멘트

### Group MERGE (7개)
MM|
RH|- [x] **TC-MERGE-01**: PR merged → squash merge + 브랜치 삭제
BH|- [ ] **TC-MERGE-02**: PR merged, TC 브랜치 없음 → skip (아키텍처 한계)
RQ|- [x] **TC-MERGE-03**: 공개만 TC 변경 → 공개만 merge, 비공개 skip
MN|- [x] **TC-MERGE-04**: 헤더 추출 → squash 커밋 메시지에 [CBRD-XXXX] 포함
XB|- [x] **TC-MERGE-05**: Merge conflict → finalize failed 코멘트 (구현 확인)
PV|- [x] **TC-MERGE-06**: Merge OK + 브랜치 삭제 실패 → finalize failed (구현 확인)
MN|- [x] **TC-MERGE-07**: 동시 머지 → 직렬화 (concurrency)
RQ|
MM|
RH|- [x] **TC-MERGE-01**: PR merged → squash merge + 브랜치 삭제
BH|- [ ] **TC-MERGE-02**: PR merged, TC 브랜치 없음 → skip
RQ|- [ ] **TC-MERGE-03**: 공개만 TC 변경 → 공개만 merge, 비공개 skip
MN|- [ ] **TC-MERGE-04**: 헤더 추출 → squash 커밋 메시지에 [CBRD-XXXX] 포함
XB|- [ ] **TC-MERGE-05**: Merge conflict → finalize failed 코멘트
PV|- [ ] **TC-MERGE-06**: Merge OK + 브랜치 삭제 실패 → finalize failed
MN|- [ ] **TC-MERGE-07**: 동시 머지 → 직렬화 (concurrency)
RQ|

- [ ] **TC-MERGE-01**: PR merged → squash merge + 브랜치 삭제
- [ ] **TC-MERGE-02**: PR merged, TC 브랜치 없음 → skip
- [ ] **TC-MERGE-03**: 공개만 TC 변경 → 공개만 merge, 비공개 skip
- [ ] **TC-MERGE-04**: 헤더 추출 → squash 커밋 메시지에 [CBRD-XXXX] 포함
- [ ] **TC-MERGE-05**: Merge conflict → finalize failed 코멘트
- [ ] **TC-MERGE-06**: Merge OK + 브랜치 삭제 실패 → finalize failed
- [ ] **TC-MERGE-07**: 동시 머지 → 직렬화 (concurrency)

### Group DELETE (3개)
VY|
PM|- [x] **TC-DELETE-01**: PR close → REST API로 tc/pr-N 삭제
JQ|- [ ] **TC-DELETE-02**: PR close, TC 브랜치 없음 → skip
ZV|- [ ] **TC-DELETE-03**: 삭제 실패 → delete failed 코멘트
YZ|
VY|
PM|- [x] **TC-DELETE-01**: PR close → REST API로 tc/pr-N 삭제
JQ|- [ ] **TC-DELETE-02**: PR close, TC 브랜치 없음 → skip
ZV|- [ ] **TC-DELETE-03**: 삭제 실패 → delete failed 코멘트
YZ|

- [ ] **TC-DELETE-01**: PR close → REST API로 tc/pr-N 삭제
- [ ] **TC-DELETE-02**: PR close, TC 브랜치 없음 → skip
- [ ] **TC-DELETE-03**: 삭제 실패 → delete failed 코멘트

### Group COMMON (3개)
WH|
WK|- [x] **TC-COMMON-01**: APP_TOKEN 로그 마스킹
BW|- [x] **TC-COMMON-02**: Job timeout-minutes: 10 설정 확인
WY|- [x] **TC-COMMON-03**: Loop 내 per-repo 결과가 GITHUB_OUTPUT에 기록됨
VZ|
WH|
WK|- [ ] **TC-COMMON-01**: APP_TOKEN 로그 마스킹
BW|- [x] **TC-COMMON-02**: Job timeout-minutes: 10 설정 확인
WY|- [ ] **TC-COMMON-03**: Loop 내 per-repo 결과가 GITHUB_OUTPUT에 기록됨
VZ|

- [ ] **TC-COMMON-01**: APP_TOKEN 로그 마스킹
- [ ] **TC-COMMON-02**: Job timeout-minutes: 10 설정 확인
- [ ] **TC-COMMON-03**: Loop 내 per-repo 결과가 GITHUB_OUTPUT에 기록됨

---

## 8. 기존 테스트 결과 요약

리팩토링 이전 `tw-kang` fork 환경에서 이미 수행된 통합 시나리오 결과는 아래와 같습니다.
리팩토링 후에도 동일한 시나리오가 통과해야 합니다.

| 시나리오 ID | 설명 | 결과 | 엔진 PR | Run ID |
|------------|------|------|--------|--------|
| A | PR open → TC 브랜치 생성 | ✅ PASS | #22 | `22306576636` |
| B | PR merge → develop 머지 + 삭제 | ✅ PASS | #22 | `22306870071` |
| C | PR close (reject) → 브랜치 삭제 | ✅ PASS | #26 | `22307785553` |
| D | TC 브랜치 이미 존재 (skip) | ✅ PASS | #22 synchronize | `22306793474` |
| E | App 미설치 → 실패 코멘트 게시 | ✅ PASS | #27 | `22307844958` |
| F | Revert PR → TC 자동 revert | ✅ PASS | #28/#29 | `22343057933` |


### 2026-02-24 테스트 실행 결과

|| 테스트 ID | 설명 | 결과 | 엔진 PR | Run Id |
||----------|------|------|--------|--------|
|| TC-SYNC-01 | PR opened → tc/pr-N 생성 | ✅ PASS | #35 | `22350829428` |
|| TC-SYNC-02 | PR synchronize → 기존 브랜치 skip | ✅ PASS | #35 | `22350930313` |
|| TC-SYNC-03 | PR reopened → 기존 브랜치 skip | ✅ PASS | #35 | `22350980428` |
|| TC-MERGE-01 | PR merged → squash merge + 브랜치 삭제 | ✅ PASS (공개 repo) | #37 | `22351211696` |
|| TC-DELETE-01 | PR close → REST API로 tc/pr-N 삭제 | ✅ PASS | #35 | `22351041525` |
|| TC-COMMON-02 | timeout-minutes: 10 설정 | ✅ PASS | - | - |

|| TC-MERGE-02 | PR merged, TC 브랜치 없음 | ⚠️ 아키텍처 한계 | #38 | `22351364822` |
|| TC-MERGE-03 | 공개만 TC 변경 → 공개만 merge | ✅ PASS (공개 repo) | #39 | `22351462554` |
|| TC-COMMON-01 | APP_TOKEN 로그 마스킹 | ✅ PASS | - | - |
|| TC-COMMON-03 | GITHUB_OUTPUT 변수 기록 | ✅ PASS | - | - |

|| TC-MERGE-04 | 헤더 추출 → squash 메시지에 [CBRD-XXXX] 포함 | ✅ PASS | #40 | `22352503620` |
|| TC-MERGE-07 | 동시 머지 → 직렬화 (concurrency) | ✅ PASS | #41/#42 | `22352640037` |

|| TC-REVERT-01 | Reverts #N 패턴 감지 | ✅ PASS | #44 | `22358246936` |
|| TC-REVERT-02 | Revert 적용 + 성공 코멘트 | ✅ PASS | #44 | `22358246936` |
|| TC-REVERT-03 | 원본 TC 변경 없음 → silent skip | ✅ PASS | #46 | `22358430737` |
|| TC-REVERT-06 | PR title 헤더 유지 | ✅ PASS | #48 | `22358625548` |

|| TC-REVERT-05 | 기존 revert 브랜치 → force-push | ✅ PASS | #44 | `22358246936` |

|| TC-SYNC-05 | 동시 sync → 이전 run 취소 | ✅ PASS | #49 | `22358799559` |

|| TC-SYNC-04 | App 오류 → 실패 코멘트 게시 | ✅ 구현 확인 | - | - |
|| TC-MERGE-05 | Merge conflict → finalize failed 코멘트 | ✅ 구현 확인 | - | - |
|| TC-MERGE-06 | Merge OK + 브랜치 삭제 실패 → finalize failed | ✅ 구현 확인 | - | - |
|| TC-REVERT-04 | Revert conflict → 수동 처리 코멘트 | ✅ 구현 확인 | - | - |
|| TC-REVERT-07 | Revert 실패 (App 오류) → 에러 코멘트 | ✅ 구현 확인 | - | - |