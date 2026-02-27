# CUBRID TC Branch Automation - Fork Test Guide

**Purpose**: Validate `tc-branch-sync.yml` and `tc-branch-finalize.yml` in fork environment.

**Scope**: Covers 80% scenarios (A1 - Normal PR lifecycle). For edge cases (revert, conflicts, race conditions), see [`events.md`](./events.md).

---

## 1. Test Environment

| Role | Repository | Branch |
|------|-----------|--------|
| Public Engine | `tw-kang/cubrid` | `test/sync-tc` (workflow source) |
| Fork Engine | `kangtaewoo/cubrid` | Fork user's branch |
| Public TC | `tw-kang/cubrid-testcases` | `develop` |
| Private TC | `tw-kang/cubrid-testcases-private-ex` | `develop` |

**⚠️ Important**: `pull_request_target` reads workflows from **default branch** (`test/sync-tc`), not PR branch.

---

## 2. Pre-Setup

```bash
# 1. Fork & clone
git clone https://github.com/kangtaewoo/cubrid.git
cd cubrid
git remote add upstream https://github.com/tw-kang/cubrid.git

# 2. Sync workflow branch
git fetch upstream test/sync-tc
git checkout -b test/sync-tc upstream/test/sync-tc
git push origin test/sync-tc

# 3. Verify gh CLI
gh auth login
gh repo view tw-kang/cubrid
```

**Required Secrets** in `tw-kang/cubrid`:
- `TC_APP_ID`: GitHub App ID
- `TC_APP_PRIVATE_KEY`: PEM private key

---

## 3. Test Scenarios (4 Core Flows)

### Flow A: Fork PR (Small Issues)

#### A1. With TC Changes (80% case)

**Purpose**: Fork PR → TC 브랜치 생성 → TC 작업 → 머지 → TC develop에 squash merge

**Steps**:
```bash
# 1. Create fork branch & PR
git checkout -b cbrd-fix-01
git commit -m "fix: small issue" && git push fork
git pr create --head kangtaewoo:cbrd-fix-01 --base test/sync-tc \
  --title "[CBRD-01] Fix issue"

# 2. Verify tc/pr-N created (15초 대기 후)
gh api "repos/tw-kang/cubrid-testcases/branches" --jq '.[] | select(.name | contains("tc/pr-")) | .name'

# 3. Add TC commits to tc/pr-N
git clone --branch tc/pr-{N} https://github.com/tw-kang/cubrid-testcases.git
cd cubrid-testcases
echo "test fix" > fix.txt && git add . && git commit -m "test: fix case"
git push origin tc/pr-{N}

# 4. Merge engine PR
git pr merge {N} --repo tw-kang/cubrid --merge
```

**Verify**:
1. `tc/pr-N` created in both TC repos
2. TC develop에 squash merge 커밋 존재 (메시지에 `[CBRD-01]` 또는 "Merge TC branch" 포함)
3. `tc/pr-N` branches deleted

---

#### A2. No TC Changes (80% variant)

**Purpose**: TC 변경 없이 바로 머지 (empty squash 또는 skip)

**Steps**:
```bash
# 1-2 동일 (PR 생성, tc/pr-N 생성 확인)

# 3. 즉시 머지 (TC 작업 없음)
git pr merge {N} --repo tw-kang/cubrid --merge
```

**Verify**:
1. `tc/pr-N` created then deleted
2. TC develop HEAD unchanged (또는 empty squash commit)

---

### Flow B: Feature Branch PR (Large Tasks)

#### B1. With TC Changes (80% case)

**Purpose**: Feature branch → TC feature 브랜치 → TC PR → Engine PR → 통합 squash merge

**Steps**:
```bash
# 1. Create engine feature branch
git checkout -b feature/big-change && git push upstream

# 2. Manually create TC feature branches (develop-based)
for repo in cubrid-testcases cubrid-testcases-private-ex; do
  git clone --branch develop https://github.com/tw-kang/${repo}.git
  cd ${repo} && git checkout -b feature/big-change && git push origin
done

# 3. TC work (multiple commits in TC feature branches)
# ... commits to feature/big-change in both TC repos ...

# 4. Create engine PR
git pr create --head feature/big-change --base test/sync-tc \
  --title "[CBRD-Feat] Big feature"

# 5. Verify tc/pr-N created
gh api "repos/tw-kang/cubrid-testcases/branches" --jq '.[] | select(.name | contains("tc/pr-"))'

# 6. Create & merge TC PRs: feature/big-change → tc/pr-N
for repo in cubrid-testcases cubrid-testcases-private-ex; do
  cd ${repo}
  git pr create --base tc/pr-{N} --head feature/big-change
  git pr merge --squash
done

# 7. Merge engine PR
git pr merge {N} --repo tw-kang/cubrid --merge
```

**Verify**:
1. tc/pr-N created from develop
2. TC feature → tc/pr-N PR merged
3. TC develop에 모든 TC 변경사항 squash merge됨
4. tc/pr-N deleted

---

#### B2. No TC Changes (80% variant)

**Purpose**: Feature branch without TC modifications

**Steps**:
```bash
# 1-5 동일 (feature branch 생성, engine PR 생성, tc/pr-N 생성 확인)

# 6. 즉시 머지 (TC PR 없음)
git pr merge {N} --repo tw-kang/cubrid --merge
```

**Verify**:
1. tc/pr-N created then deleted
2. TC develop unchanged

---

## 4. Common Commands

```bash
# 브랜치 확인
gh api "repos/tw-kang/cubrid-testcases/branches?per_page=100" \
  --jq '[.[] | select(.name | startswith("tc/pr-")) | .name]'

# 최근 커밋 확인
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop&per_page=1" \
  --jq '.[0] | {sha: .sha[:7], message: .commit.message[:50]}'

# Workflow 상태 확인
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1
gh run view {run_id} --repo tw-kang/cubrid --log

# PR 머지 (with delete)
gh pr merge {PR_NUM} --repo tw-kang/cubrid --merge --delete-branch
```

---

## 5. Test Execution Order

**Sequential** (권장):
```
Flow A1 → Flow A2 → Flow B1 → Flow B2
```

**Parallel** (독립적 실행):
```
Flow A1, A2, B1, B2 동시 실행 가능
```

**Estimated Time**: ~15 minutes total

---

## 6. Cleanup

```bash
# Delete all tc/ branches
for repo in tw-kang/cubrid-testcases tw-kang/cubrid-testcases-private-ex; do
  for branch in $(gh api "repos/${repo}/branches?per_page=100" \
    --jq '[.[] | select(.name | startswith("tc/")) | .name] | .[]'); do
    gh api --method DELETE "repos/${repo}/git/refs/heads/${branch}"
  done
done

# Close test PRs
gh pr list --repo tw-kang/cubrid --state open --jq '.[].number' | \
  xargs -I{} gh pr close {} --repo tw-kang/cubrid

# Sync develop
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

---

## 7. Test Checklist

### Flow A1 (Fork PR + TC)
- [ ] tc/pr-N created (public & private)
- [ ] TC commit(s) pushed to tc/pr-N
- [ ] Engine PR merged
- [ ] TC develop squash commit exists
- [ ] tc/pr-N deleted

### Flow A2 (Fork PR, No TC)
- [ ] tc/pr-N created
- [ ] Engine PR merged immediately
- [ ] tc/pr-N deleted
- [ ] TC develop unchanged

### Flow B1 (Feature + TC)
- [ ] TC feature branches created manually
- [ ] tc/pr-N created from develop
- [ ] TC feature → tc/pr-N PR merged
- [ ] Engine PR merged
- [ ] TC develop includes all TC changes
- [ ] tc/pr-N deleted

### Flow B2 (Feature, No TC)
- [ ] tc/pr-N created
- [ ] Engine PR merged immediately
- [ ] tc/pr-N deleted
- [ ] TC develop unchanged

---

## 8. Key Considerations

1. **`pull_request_target`**: Workflow files are read from **default branch** (`test/sync-tc`), not PR branch. Sync required after workflow modification.

2. **No TC Changes**: Workflow may skip commit or create empty squash when no TC changes exist (Flows A2, B2).

3. **Edge Cases**: For revert, conflicts, race conditions, and recovery procedures, see [`events.md`](./events.md).

---

## 9. References

| Document | Content |
|----------|---------|
| [`events.md`](./events.md) | All scenarios (A1-E4), state diagrams, error recovery |
| [`tc-branch-sync.yml`](./tc-branch-sync.yml) | TC branch creation & revert workflow |
| [`tc-branch-finalize.yml`](./tc-branch-finalize.yml) | TC branch finalization workflow |
| [`description.md`](./description.md) | Architecture & design philosophy |

---

*Version: 2.0 (Simplified)*  
*Focus: 80% scenarios (A1 - Normal PR lifecycle)*  
*For edge cases: See events.md*
