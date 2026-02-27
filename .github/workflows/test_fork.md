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

**Purpose**: Fork PR → TC branch created → TC work → merge → TC develop receives squash merge

**Steps**:
```bash
# 1. Create fork branch & PR
git checkout -b cbrd-fix-01
echo "fix: small issue" > fix.txt
git add .
git commit -m "fix: small issue"
git push origin cbrd-fix-01

gh pr create --head kangtaewoo:cbrd-fix-01 --base tw-kang:test/sync-tc \
  --title "[CBRD-01] Fix issue"

# 2. Verify tc/pr-N created (wait ~15 seconds)
gh api "repos/tw-kang/cubrid-testcases/branches" --jq '.[] | select(.name | contains("tc/pr-")) | .name'

# 3. Add TC commits to tc/pr-N (replace N with actual PR number)
git clone --branch tc/pr-N https://github.com/tw-kang/cubrid-testcases.git
cd cubrid-testcases
echo "test fix" > fix.txt
git add .
git commit -m "test: fix case"
git push origin tc/pr-N

# 4. Merge engine PR (replace N with actual PR number)
gh pr merge N --repo tw-kang/cubrid --merge
```

**Verify**:
1. `tc/pr-N` created in both TC repos
2. Draft PR created in TC repos (develop ← tc/pr-N) with title containing `[CBRD-01]`
3. CircleCI tests run on TC branch
4. After engine PR merge, draft PR is closed and branch deleted automatically
1. `tc/pr-N` created in both TC repos
2. TC develop has squash merge commit (message contains `[CBRD-01]` or "Merge TC branch")
3. `tc/pr-N` branches deleted

---

#### A2. No TC Changes (80% variant)

**Purpose**: Merge without TC changes (empty squash or skip)

**Steps**:
```bash
# 1-2 Same as A1 (create PR, verify tc/pr-N created)
git checkout -b cbrd-fix-02
echo "fix: another issue" > fix2.txt
git add .
git commit -m "fix: another issue"
git push origin cbrd-fix-02

gh pr create --head kangtaewoo:cbrd-fix-02 --base tw-kang:test/sync-tc \
  --title "[CBRD-02] Another fix"

# Wait ~15 seconds for tc/pr-N creation
gh api "repos/tw-kang/cubrid-testcases/branches" --jq '.[] | select(.name | contains("tc/pr-")) | .name'

# 3. Merge immediately (no TC work)
gh pr merge N --repo tw-kang/cubrid --merge
```

**Verify**:
1. `tc/pr-N` created then deleted
2. TC develop HEAD unchanged (or empty squash commit)

---

### Flow B: Feature Branch PR (Large Tasks)

#### B1. With TC Changes (80% case)

**Purpose**: Feature branch → TC feature branch → TC PR → Engine PR → integrated squash merge

**Steps**:
```bash
# 1. Create engine feature branch
git checkout -b feature/big-change
echo "big feature" > feature.txt
git add .
git commit -m "feat: big change"
git push origin feature/big-change

# 2. Manually create TC feature branches (develop-based)
for repo in cubrid-testcases cubrid-testcases-private-ex; do
  git clone --branch develop https://github.com/tw-kang/${repo}.git
  cd ${repo}
  git checkout -b feature/big-change
  echo "tc feature work" > feature.txt
  git add .
  git commit -m "test: feature work"
  git push origin feature/big-change
  cd ..
done

# 3. Create engine PR
gh pr create --head kangtaewoo:feature/big-change --base tw-kang:test/sync-tc \
  --title "[CBRD-Feat] Big feature"

# 4. Verify tc/pr-N created (wait ~15 seconds, replace N with actual PR number)
gh api "repos/tw-kang/cubrid-testcases/branches" --jq '.[] | select(.name | contains("tc/pr-")) | .name'

# 5. Create & merge TC PRs: feature/big-change → tc/pr-N
for repo in cubrid-testcases cubrid-testcases-private-ex; do
  cd ${repo}
  gh pr create --head feature/big-change --base tc/pr-N --repo tw-kang/${repo}
  # Get the TC PR number
  TC_PR=$(gh pr list --repo tw-kang/${repo} --head feature/big-change --json number --jq '.[0].number')
  gh pr merge ${TC_PR} --repo tw-kang/${repo} --squash
  cd ..
done

# 6. Merge engine PR
gh pr merge N --repo tw-kang/cubrid --merge
```

**Verify**:
1. tc/pr-N created from develop
2. TC feature → tc/pr-N PR merged
3. TC develop includes all TC changes via squash merge
4. tc/pr-N deleted

---

#### B2. No TC Changes (80% variant)

**Purpose**: Feature branch without TC modifications

**Steps**:
```bash
# 1-4 Same as B1 (create feature branch, engine PR, verify tc/pr-N)
git checkout -b feature/another-change
echo "another feature" > feature2.txt
git add .
git commit -m "feat: another change"
git push origin feature/another-change

gh pr create --head kangtaewoo:feature/another-change --base tw-kang:test/sync-tc \
  --title "[CBRD-Feat2] Another feature"

# Wait ~15 seconds for tc/pr-N creation
gh api "repos/tw-kang/cubrid-testcases/branches" --jq '.[] | select(.name | contains("tc/pr-")) | .name'

# 5. Merge immediately (no TC PR)
gh pr merge N --repo tw-kang/cubrid --merge
```

**Verify**:
1. tc/pr-N created then deleted
2. TC develop unchanged

---

## 4. Common Commands

```bash
# List all tc/pr-* branches
gh api "repos/tw-kang/cubrid-testcases/branches?per_page=100" \
  --jq '[.[] | select(.name | startswith("tc/pr-")) | .name]'

# Check latest commit on develop
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop&per_page=1" \
  --jq '.[0] | {sha: .sha[:7], message: .commit.message[:50]}'

# Check workflow status
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1
gh run view {run_id} --repo tw-kang/cubrid --log

# Merge PR with branch deletion
gh pr merge {PR_NUM} --repo tw-kang/cubrid --merge --delete-branch
```

---

## 5. Test Execution Order

**Sequential** (recommended):
```
Flow A1 → Flow A2 → Flow B1 → Flow B2
```

**Parallel** (independent execution):
```
Flow A1, A2, B1, B2 can run simultaneously
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

*Version: 2.1 (Corrected)*  
*Focus: 80% scenarios (A1 - Normal PR lifecycle)*  
*All commands are 100% copy-pasteable with correct gh/git syntax*  
*For edge cases: See events.md*
