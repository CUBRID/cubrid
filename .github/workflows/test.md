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
