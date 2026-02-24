# CUBRID TC Branch Automation - Fork Environment Test Guide

## 1. Overview

This document provides **7-scenario chain** based integration test guide for validating `tc-branch-sync.yml` and `tc-branch-finalize.yml` workflows in a fork environment similar to production.

**Differences from test.md**
- **test.md**: Unit-level functional testing of workflows (22 independent scenarios)
- **test_fork.md**: Production-like scenario-based integration testing (7 chains, each with complete lifecycle)

**Key Features**
- Cross-repo PR flow using Fork repo (`kangtaewoo/cubrid`)
- Two development flow simulation (Fork PR vs Feature Branch PR)
- Diverse scenarios with/without TC modifications
- Revert handling verification
- Workflow files: [`tc-branch-sync.yml`](./tc-branch-sync.yml), [`tc-branch-finalize.yml`](./tc-branch-finalize.yml)

---

## 2. Test Environment

### 2.1 Repository Setup

| Role | Repository | Purpose |
|------|-----------|---------|
| Public Engine | `tw-kang/cubrid` | Main engine repo (default: `test/sync-tc`) |
| Fork Engine | `kangtaewoo/cubrid` | Forked engine repo (for Fork PR) |
| Public TC | `tw-kang/cubrid-testcases` | Public test cases |
| Private TC | `tw-kang/cubrid-testcases-private-ex` | Private test cases |

### 2.2 Branch Configuration

| Item | Value | Description |
|------|-------|-------------|
| Engine default | `test/sync-tc` | Branch where `pull_request_target` reads workflow files |
| TC branch prefix | `tc/pr-` | Auto-created per engine PR (e.g., `tc/pr-123`) |
| TC develop | `develop` | Target branch for TC changes merge |

### 2.3 Important: `pull_request_target` Behavior

`pull_request_target` reads workflow files from **default branch(`test/sync-tc`)**, not PR base branch.

After workflow modification, must perform:
```bash
git checkout test/sync-tc && git pull upstream test/sync-tc && git push upstream test/sync-tc
```

---

## 3. Pre-Setup (Fork User)

### 3.1 Fork Repository Creation and Configuration

```bash
# 1. Fork tw-kang/cubrid to kangtaewoo/cubrid via GitHub UI

# 2. Clone fork locally
git clone https://github.com/kangtaewoo/cubrid.git
cd cubrid

# 3. Configure Git remotes
git remote add upstream https://github.com/tw-kang/cubrid.git
git remote rename origin fork

# 4. Sync test/sync-tc branch
git fetch upstream test/sync-tc
git checkout -b test/sync-tc upstream/test/sync-tc
git push fork test/sync-tc
```

### 3.2 gh CLI Authentication and Permissions

```bash
# 1. gh CLI authentication (must access both fork and upstream repos)
gh auth login

# 2. Verify token permissions (repo, read:org, write:org needed)
gh auth status

# 3. Verify fork repo access
gh repo view kangtaewoo/cubrid
gh repo view tw-kang/cubrid
```

### 3.3 Secrets and GitHub App Verification

In `tw-kang/cubrid` Settings → Secrets:
- `TC_APP_ID`: GitHub App numeric ID
- `TC_APP_PRIVATE_KEY`: PEM format Private Key full content

GitHub App must be installed in both TC repositories (`tw-kang/cubrid-testcases`, `tw-kang/cubrid-testcases-private-ex`) with Contents: Write permission.

### 3.4 TC Repository develop Branch Initialization

```bash
# Sync public TC develop
gh repo sync tw-kang/cubrid-testcases --branch develop

# Sync private TC develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

---

## 4. Two Development Flows

### 4.1 Fork PR Flow (Small Issues)

**Scenario**: When handling small issues like `cbrd-1234`

1. Create `cbrd-xxxx` branch in `kangtaewoo/cubrid`
2. Create PR to `tw-kang/cubrid` `test/sync-tc` branch
3. Workflow auto-creates `tc/pr-N` in TC repos (develop-based)
4. If needed, commit directly to `tc/pr-N` (multiple commits possible)
5. Merge engine PR
6. Workflow squash merges `tc/pr-N` to develop and deletes it

**Flow Diagram**
```
kangtaewoo/cubrid (cbrd-1234) --> fork PR
                                     |
                              tw-kang/cubrid (test/sync-tc)
                                     |
                              tc-branch-sync.yml
                              (tc/pr-N creation)
                                     |
                         tw-kang/cubrid-testcases
                              tc/pr-N (direct commits)
                                     |
                         [Engine PR merge]
                                     |
                              tc-branch-finalize.yml
                          (tc/pr-N squash merge + delete)
                                     |
                           develop (squash commit)
```

### 4.2 Feature Branch PR Flow (Large Tasks)

**Scenario**: When handling large tasks requiring public repo work

1. Create `feature/*` branch in `tw-kang/cubrid` (large work unit in public repo)
2. **User manually** creates identical `feature/*` branch in both TC repos (develop-based)
3. **User** performs TC work with multiple commits in TC `feature/*`
4. Create engine `feature/*` -> `test/sync-tc` PR
5. Workflow auto-creates `tc/pr-N` in TC repos (develop-based)
6. **User** creates `feature/*` -> `tc/pr-N` PR in both TC repos and merges
7. Merge engine PR
8. Workflow squash merges `tc/pr-N` to develop (includes all TC changes)

**Flow Diagram**
```
tw-kang/cubrid (feature/xxx)
       |
   [User manual creation]
       |
tw-kang/cubrid-testcases (feature/xxx)
tw-kang/cubrid-testcases-private-ex (feature/xxx)
       |
   [User TC work: multiple commits]
       |
Engine feature/* -> test/sync-tc PR creation
       |
tc-branch-sync.yml
(tc/pr-N creation, develop-based)
       |
[User: feature/* -> tc/pr-N PR merge]
       |
Engine PR merge
       |
tc-branch-finalize.yml
(tc/pr-N squash merge + delete)
       |
develop (squash commit with all TC changes)
```

---

## 5. Test Scenarios (7 Chains)

### 5.1 Chain 1: FORK-TC-REVISION -- Fork PR + Direct TC Modification

**Purpose**: Create Fork PR with small issue, fix test failure via multiple commits to `tc/pr-N`, merge engine PR, verify squash merge

**Pre-requisites**
- Fork repo configured
- TC develop initialized

**Execution Steps**

```bash
# Step 1: Create issue branch in fork repo
cd /path/to/fork-cubrid
git checkout -b cbrd-fork-01
echo "# Fix for cbrd-fork-01" > cbrd_fork_01.txt
git add cbrd_fork_01.txt
git commit -m "engine: Fix issue cbrd-fork-01"
git push fork cbrd-fork-01

# Step 2: Create PR in upstream
PR_NUM=$(gh pr create --repo tw-kang/cubrid \
  --head kangtaewoo:cbrd-fork-01 \
  --base test/sync-tc \
  --title "[CBRD-fork-01] Fix small issue" \
  --body "Small issue fix via fork PR" \
  --json number --jq '.number')
echo "Created PR: ${PR_NUM}"
```

**Verification (Step 1): tc/pr-N Creation**

```bash
sleep 15
BRANCH="tc/pr-${PR_NUM}"

# Check public TC repo branch
PUB_BRANCHES=$(gh api "repos/tw-kang/cubrid-testcases/branches?per_page=100" --jq "[.[] | select(.name == \"${BRANCH}\") | .name]")
echo "Public TC: ${PUB_BRANCHES}"

# Check private TC repo branch
PRIV_BRANCHES=$(gh api "repos/tw-kang/cubrid-testcases-private-ex/branches?per_page=100" --jq "[.[] | select(.name == \"${BRANCH}\") | .name]")
echo "Private TC: ${PRIV_BRANCHES}"

# Expected: Both ["tc/pr-<PR_NUM>"]
```

**Execution Steps (Continued): TC Modification**

```bash
# Step 3: Clone tc/pr-N and make commits
BRANCH="tc/pr-${PR_NUM}"

# Public TC repo
git clone --depth=1 --branch "${BRANCH}" \
  https://github.com/tw-kang/cubrid-testcases.git tc-fork-01-pub
cd tc-fork-01-pub
echo "# Test fix commit 1" > tc_fix_01.txt
git add tc_fix_01.txt
git commit -m "test: Fix test case for cbrd-fork-01 (commit 1)"
git push origin "${BRANCH}"
cd ..

# Step 4: Merge engine PR
sleep 5
gh pr merge "${PR_NUM}" --repo tw-kang/cubrid --merge --delete-branch
```

**Verification (Step 2): TC Squash Merge**

```bash
sleep 20

# Check TC develop latest commit (squash commit)
LATEST_COMMIT=$(gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop&per_page=1" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}')
echo "Public TC latest commit: ${LATEST_COMMIT}"

# Expected:
# - Commit message: "[CBRD-fork-01] Merge TC branch 'tc/pr-<PR_NUM>' into develop (Engine PR #<PR_NUM>)"
# - Or at least includes "Merge TC branch"

# Verify tc/pr-N branch deletion
BRANCH_COUNT=$(gh api "repos/tw-kang/cubrid-testcases/branches?per_page=100" \
  --jq "[.[] | select(.name == \"${BRANCH}\") | .name] | length")
echo "Remaining branches with name ${BRANCH}: ${BRANCH_COUNT}"

# Expected: 0 (deleted)
```

**Expected Result**

| Item | Expected Value |
|------|--------|
| tc/pr-N both created | ✅ |
| TC develop squash commit | ✅ |
| Squash message header | `[CBRD-fork-01]` included |
| tc/pr-N branches deleted | ✅ |

---

### 5.2 Chain 2: FORK-TC-NOCHANGE -- Fork PR + No TC Changes

**Purpose**: Create Fork PR, no TC changes needed (test passes), merge engine PR, verify close-only behavior

**Pre-requisites**
- Chain 1 completed

**Execution Steps**

```bash
# Step 1: Create another issue branch in fork repo
git checkout test/sync-tc
git pull fork test/sync-tc
git checkout -b cbrd-fork-02
echo "# Fix for cbrd-fork-02 (no TC change needed)" > cbrd_fork_02.txt
git add cbrd_fork_02.txt
git commit -m "engine: Fix issue cbrd-fork-02 (no TC change)"
git push fork cbrd-fork-02

# Step 2: Create PR in upstream
PR_NUM=$(gh pr create --repo tw-kang/cubrid \
  --head kangtaewoo:cbrd-fork-02 \
  --base test/sync-tc \
  --title "[CBRD-fork-02] Fix without TC changes" \
  --body "No TC changes needed" \
  --json number --jq '.number')
echo "Created PR: ${PR_NUM}"

sleep 15

# Step 3: Merge immediately without TC changes
gh pr merge "${PR_NUM}" --repo tw-kang/cubrid --merge --delete-branch
```

**Verification**

```bash
sleep 20

BRANCH="tc/pr-${PR_NUM}"

# Verify tc/pr-N branch deletion
PUB_DELETED=$(gh api "repos/tw-kang/cubrid-testcases/branches?per_page=100" \
  --jq "[.[] | select(.name == \"${BRANCH}\") | .name] | length")
echo "Public TC branches after merge: ${PUB_DELETED}"

# Expected: 0 (deleted or never created)

# Verify TC develop HEAD unchanged
CURRENT_DEVELOP_HEAD=$(gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop&per_page=1" --jq '.[0].sha[0:7]')
echo "Public TC develop HEAD: ${CURRENT_DEVELOP_HEAD}"
```

**Expected Result**

| Item | Expected Value |
|------|--------|
| tc/pr-N branch | Deleted or not created (0) |
| TC develop changed | ❌ (No changes) |
| Workflow conclusion | `success` or `failure` (commit_failed) |

**Note**: Current workflow may result in `commit_failed` status when no TC changes exist. Report workflow conclusion.

---

### 5.3 Chain 3: FORK-PR-CLOSE -- Fork PR Rejection

**Purpose**: Verify tc/pr-N deletion when Fork PR is closed without merge

**Pre-requisites**
- Chain 1, 2 completed

**Execution Steps**

```bash
# Step 1: Create issue branch
git checkout test/sync-tc
git pull fork test/sync-tc
git checkout -b cbrd-fork-03
echo "# Fix for cbrd-fork-03 (will reject)" > cbrd_fork_03.txt
git add cbrd_fork_03.txt
git commit -m "engine: Fix issue cbrd-fork-03"
git push fork cbrd-fork-03

# Step 2: Create PR in upstream
PR_NUM=$(gh pr create --repo tw-kang/cubrid \
  --head kangtaewoo:cbrd-fork-03 \
  --base test/sync-tc \
  --title "[CBRD-fork-03] Rejected PR" \
  --body "This PR will be rejected" \
  --json number --jq '.number')
echo "Created PR: ${PR_NUM}"

sleep 15

# Step 3: Close PR without merge
gh pr close "${PR_NUM}" --repo tw-kang/cubrid
```

**Verification**

```bash
sleep 15

BRANCH="tc/pr-${PR_NUM}"

# Verify tc/pr-N branch deletion
PUB_DELETED=$(gh api "repos/tw-kang/cubrid-testcases/branches?per_page=100" \
  --jq "[.[] | select(.name == \"${BRANCH}\") | .name] | length")
PRIV_DELETED=$(gh api "repos/tw-kang/cubrid-testcases-private-ex/branches?per_page=100" \
  --jq "[.[] | select(.name == \"${BRANCH}\") | .name] | length")
echo "Public TC: ${PUB_DELETED}, Private TC: ${PRIV_DELETED}"

# Expected: 0 (both deleted)
```

**Expected Result**

| Item | Expected Value |
|------|--------|
| tc/pr-N public | Deleted (0) |
| tc/pr-N private | Deleted (0) |
| TC develop changed | ❌ (No changes) |

---

### 5.4 Chain 4: FEAT-TC-REVISION -- Feature Branch PR + TC feature Branch PR Merge

**Purpose**: Complete feature branch flow verification. User manually creates TC repo `feature/*` branch, performs work, then merges to `tc/pr-N` after engine PR creation.

**Pre-requisites**
- Chain 1-3 completed

**Execution Steps (Stage 1: Engine feature branch creation)**

```bash
# Step 1: Create feature branch in public engine repo
git checkout -b feature/feat-tc-revision
echo "# Feature work" > feature_work.txt
git add feature_work.txt
git commit -m "engine: Add feature feat-tc-revision"
git push upstream feature/feat-tc-revision

# Step 2: Manually create feature branch in public TC repo (develop-based)
git clone --depth=1 --branch develop \
  https://github.com/tw-kang/cubrid-testcases.git tc-feature-pub
cd tc-feature-pub
git checkout -b feature/feat-tc-revision
echo "# TC work for feature" > tc_feature_work.txt
git add tc_feature_work.txt
git commit -m "test: Add TC for feature feat-tc-revision (commit 1)"
git push origin feature/feat-tc-revision
cd ..

# Step 3: Manually create feature branch in private TC repo
git clone --depth=1 --branch develop \
  https://github.com/tw-kang/cubrid-testcases-private-ex.git tc-feature-priv
cd tc-feature-priv
git checkout -b feature/feat-tc-revision
echo "# TC work for feature (private)" > tc_feature_work_priv.txt
git add tc_feature_work_priv.txt
git commit -m "test: Add private TC for feature feat-tc-revision"
git push origin feature/feat-tc-revision
cd ..

# Step 4: Create engine PR
cd /path/to/public-cubrid
PR_NUM=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/feat-tc-revision \
  --title "[CBRD-feat-01] Add big feature with TC changes" \
  --body "Feature work with TC changes" \
  --json number --jq '.number')
echo "Created engine PR: ${PR_NUM}"
```

**Verification (Stage 1): tc/pr-N Creation**

```bash
sleep 15

BRANCH="tc/pr-${PR_NUM}"

# Check workflow execution
RUN_STATUS=$(gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1 --json status,conclusion --jq '.[0]')
echo "Workflow status: ${RUN_STATUS}"

# Verify tc/pr-N creation
PUB_BRANCHES=$(gh api "repos/tw-kang/cubrid-testcases/branches?per_page=100" --jq "[.[] | select(.name == \"${BRANCH}\") | .name]")
echo "Public TC tc/pr-N: ${PUB_BRANCHES}"

# Expected: ["tc/pr-<PR_NUM>"] (develop-based)
```

**Execution Steps (Stage 2: TC feature PR merge)**

```bash
BRANCH="tc/pr-${PR_NUM}"

# Step 5: Create and merge feature -> tc/pr-N PR in public TC
git clone --depth=1 --branch "${BRANCH}" \
  https://github.com/tw-kang/cubrid-testcases.git tc-feature-pr-pub
cd tc-feature-pr-pub

TC_PR_NUM=$(gh pr create --repo tw-kang/cubrid-testcases \
  --base "${BRANCH}" \
  --head feature/feat-tc-revision \
  --title "[CBRD-feat-01] TC changes for big feature" \
  --body "Merge TC feature into tc/pr-N" \
  --json number --jq '.number')
echo "Created TC public PR: ${TC_PR_NUM}"

sleep 5

# Squash merge
gh pr merge "${TC_PR_NUM}" --repo tw-kang/cubrid-testcases --squash

cd ..

# Step 6: Create and merge feature -> tc/pr-N PR in private TC
git clone --depth=1 --branch "${BRANCH}" \
  https://github.com/tw-kang/cubrid-testcases-private-ex.git tc-feature-pr-priv
cd tc-feature-pr-priv

TC_PR_NUM=$(gh pr create --repo tw-kang/cubrid-testcases-private-ex \
  --base "${BRANCH}" \
  --head feature/feat-tc-revision \
  --title "[CBRD-feat-01] TC changes for big feature" \
  --body "Merge TC feature into tc/pr-N" \
  --json number --jq '.number')
echo "Created TC private PR: ${TC_PR_NUM}"

sleep 5

# Squash merge
gh pr merge "${TC_PR_NUM}" --repo tw-kang/cubrid-testcases-private-ex --squash

cd ..

# Step 7: Merge engine PR
sleep 5
gh pr merge "${PR_NUM}" --repo tw-kang/cubrid --merge --delete-branch
```

**Verification (Stage 2): TC develop Squash Merge**

```bash
sleep 20

# Check TC develop latest commit
LATEST_COMMIT=$(gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop&per_page=1" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}')
echo "Public TC latest commit: ${LATEST_COMMIT}"

# Expected: squash commit with "[CBRD-feat-01]" header

# Verify tc/pr-N branch deletion
BRANCH_COUNT=$(gh api "repos/tw-kang/cubrid-testcases/branches?per_page=100" \
  --jq "[.[] | select(.name == \"${BRANCH}\") | .name] | length")
echo "Remaining tc/pr-N branches: ${BRANCH_COUNT}"

# Expected: 0 (deleted)
```

**Expected Result**

| Item | Expected Value |
|------|--------|
| tc/pr-N both created | ✅ |
| TC feature -> tc/pr-N PR merged | ✅ |
| TC develop squash commit | ✅ |
| Squash message header | `[CBRD-feat-01]` included |
| tc/pr-N branches deleted | ✅ |

---

### 5.5 Chain 5: FEAT-TC-NOCHANGE -- Feature Branch PR + No TC Changes

**Purpose**: Feature branch PR without TC changes (no TC feature branch created), verify close-only behavior

**Pre-requisites**
- Chain 1-4 completed

**Execution Steps**

```bash
# Step 1: Create feature branch in public engine repo
git checkout test/sync-tc
git pull upstream test/sync-tc
git checkout -b feature/feat-no-tc
echo "# Feature without TC changes" > feature_no_tc.txt
git add feature_no_tc.txt
git commit -m "engine: Add feature with no TC changes"
git push upstream feature/feat-no-tc

# Step 2: Create engine PR (no TC feature branch created)
PR_NUM=$(gh pr create --repo tw-kang/cubrid \
  --base test/sync-tc \
  --head feature/feat-no-tc \
  --title "[CBRD-feat-02] Feature without TC changes" \
  --body "No TC changes needed" \
  --json number --jq '.number')
echo "Created PR: ${PR_NUM}"

sleep 15

# Step 3: Merge immediately without TC changes
gh pr merge "${PR_NUM}" --repo tw-kang/cubrid --merge --delete-branch
```

**Verification**

```bash
sleep 20

BRANCH="tc/pr-${PR_NUM}"

# Verify tc/pr-N branch deletion
PUB_DELETED=$(gh api "repos/tw-kang/cubrid-testcases/branches?per_page=100" \
  --jq "[.[] | select(.name == \"${BRANCH}\") | .name] | length")
echo "Public TC branches: ${PUB_DELETED}"

# Expected: 0
```

**Expected Result**

| Item | Expected Value |
|------|--------|
| tc/pr-N branch | Deleted (0) |
| TC develop changed | ❌ (No changes) |

---

### 5.6 Chain 6: FORK-REVERT -- Fork PR Revert (After Multiple Merges)

**Purpose**: Revert specific Fork PR after multiple merges, verify only that PR's TC changes are reverted

**Pre-requisite**: Chain 1 (PR-A) already merged

**Execution Steps (Stage 1: Additional Fork PR merge)**

```bash
# Step 1: Create and merge another Fork PR (PR-B)
git checkout test/sync-tc
git pull fork test/sync-tc
git checkout -b cbrd-fork-04
echo "# Different fix for cbrd-fork-04" > cbrd_fork_04.txt
git add cbrd_fork_04.txt
git commit -m "engine: Fix issue cbrd-fork-04"
git push fork cbrd-fork-04

PR_B=$(gh pr create --repo tw-kang/cubrid \
  --head kangtaewoo:cbrd-fork-04 \
  --base test/sync-tc \
  --title "[CBRD-fork-04] Another fix" \
  --body "Another issue fix" \
  --json number --jq '.number')
echo "Created PR-B: ${PR_B}"

sleep 15

# Add TC changes to PR-B
BRANCH_B="tc/pr-${PR_B}"
git clone --depth=1 --branch "${BRANCH_B}" \
  https://github.com/tw-kang/cubrid-testcases.git tc-fork-04-pub
cd tc-fork-04-pub
echo "# Different test fix" > tc_fix_04.txt
git add tc_fix_04.txt
git commit -m "test: Fix test case for cbrd-fork-04"
git push origin "${BRANCH_B}"
cd ..

sleep 5

# Merge PR-B
gh pr merge "${PR_B}" --repo tw-kang/cubrid --merge --delete-branch

sleep 20

# Step 2: Create revert PR for PR-A (from Chain 1)
# Use actual PR number from Chain 1
PR_A=<actual Chain 1 PR number>
echo "Reverting PR-A: ${PR_A}"

git checkout test/sync-tc
git pull upstream test/sync-tc
git checkout -b revert/pr-${PR_A}
git push upstream revert/pr-${PR_A}

REVERT_PR=$(gh pr create --repo tw-kang/cubrid \
  --head revert/pr-${PR_A} \
  --base test/sync-tc \
  --title "Revert \"[CBRD-fork-01] Fix small issue\"" \
  --body "Reverts tw-kang/cubrid#${PR_A}" \
  --json number --jq '.number')
echo "Created Revert PR: ${REVERT_PR}"
```

**Verification (Stage 1): Revert Applied**

```bash
sleep 15

# Check workflow log for revert detection
RUN_ID=$(gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1 --json databaseId --jq '.[0].databaseId')
gh run view "${RUN_ID}" --repo tw-kang/cubrid --log | grep -i "revert\|applied" || echo "Check logs manually"

# Verify tc/pr-<REVERT_PR> exists with revert commit
REVERT_BRANCH="tc/pr-${REVERT_PR}"
LATEST_REVERT=$(gh api "repos/tw-kang/cubrid-testcases/commits?sha=${REVERT_BRANCH}&per_page=1" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}')
echo "Revert commit: ${LATEST_REVERT}"

# Expected: revert commit message included
```

**Execution Steps (Stage 2: Revert PR merge)**

```bash
# Step 3: Merge revert PR
sleep 5
gh pr merge "${REVERT_PR}" --repo tw-kang/cubrid --merge --delete-branch
```

**Verification (Stage 2): TC develop Revert Squash Merge**

```bash
sleep 20

# Check recent commits in TC develop
COMMITS=$(gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop&per_page=5" \
  --jq '.[] | {sha: .sha[0:7], message: .commit.message}')
echo "Recent commits in TC develop:"
echo "${COMMITS}"

# Expected:
# - Latest commit: revert squash commit
# - PR-A TC changes removed
# - PR-B TC changes preserved
```

**Expected Result**

| Item | Expected Value |
|------|--------|
| Revert detection | `Reverts tw-kang/cubrid#<PR_A>` pattern |
| tc/pr-<REVERT_PR> created | ✅ |
| Revert commit exists | ✅ |
| TC develop revert merge | ✅ |
| PR-A TC changes removed | ✅ |
| PR-B TC changes preserved | ✅ |

---

### 5.7 Chain 7: FEAT-REVERT -- Feature Branch PR Revert

**Purpose**: Feature branch PR revert (TC feature -> tc/pr-N PR merge included), verify TC revert behavior

**Pre-requisite**: Chain 4 (Feature PR) already merged

**Execution Steps**

```bash
# Step 1: Create revert PR for Feature PR (from Chain 4)
# Use actual PR number from Chain 4
FEAT_PR=<actual Chain 4 PR number>
echo "Reverting Feature PR: ${FEAT_PR}"

git checkout test/sync-tc
git pull upstream test/sync-tc
git checkout -b revert/pr-${FEAT_PR}
git push upstream revert/pr-${FEAT_PR}

REVERT_PR=$(gh pr create --repo tw-kang/cubrid \
  --head revert/pr-${FEAT_PR} \
  --base test/sync-tc \
  --title "Revert \"[CBRD-feat-01] Add big feature with TC changes\"" \
  --body "Reverts tw-kang/cubrid#${FEAT_PR}" \
  --json number --jq '.number')
echo "Created Revert PR: ${REVERT_PR}"
```

**Verification (Stage 1): Revert Applied**

```bash
sleep 15

# Check revert comments
COMMENTS=$(gh pr view "${REVERT_PR}" --repo tw-kang/cubrid --comments --json body --jq '.body')
echo "PR comments:"
echo "${COMMENTS}"

# Expected: "TC Revert Applied" or "TC Revert Conflict" comment

# Verify tc/pr-<REVERT_PR> revert commit
REVERT_BRANCH="tc/pr-${REVERT_PR}"
LATEST_REVERT=$(gh api "repos/tw-kang/cubrid-testcases/commits?sha=${REVERT_BRANCH}&per_page=1" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}')
echo "Revert commit: ${LATEST_REVERT}"
```

**Execution Steps (Stage 2: Revert PR merge)**

```bash
# Step 2: Merge revert PR
sleep 5
gh pr merge "${REVERT_PR}" --repo tw-kang/cubrid --merge --delete-branch
```

**Verification (Stage 2): TC develop Revert Squash Merge**

```bash
sleep 20

# Check TC develop latest commit
LATEST_COMMIT=$(gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop&per_page=1" \
  --jq '.[0] | {sha: .sha[0:7], message: .commit.message}')
echo "Latest commit in TC develop: ${LATEST_COMMIT}"

# Expected: revert squash commit
```

**Expected Result**

| Item | Expected Value |
|------|--------|
| Revert detection | `Reverts tw-kang/cubrid#<FEAT_PR>` pattern |
| tc/pr-<REVERT_PR> created | ✅ |
| TC Revert Applied comment | ✅ |
| TC develop revert merge | ✅ |

---

## 6. Verification Rules (Agent Guidelines)

### 6.1 Verification Standards

- **Exact comparison with expected values**: All verification strictly compares with specified expected values
- **FAIL immediately if mismatch**: Report "FAIL" immediately with actual value recorded
- **Workflow execution wait**: Maximum 120 seconds, 10-second intervals polling
- **No scenario modification**: Follow provided steps exactly, report results only
- **Revert chain preservation**: Do not cleanup between Chain 6, 7 as they depend on previous results

### 6.2 Workflow Execution Verification

```bash
# Check recent run status
gh run list --repo tw-kang/cubrid --workflow tc-branch-sync.yml --limit 1 \
  --json status,conclusion,createdAt

# View run logs
RUN_ID=<run_id>
gh run view "${RUN_ID}" --repo tw-kang/cubrid --log
```

### 6.3 Git Commit Message Verification

```bash
# Verify latest commit message
gh api "repos/tw-kang/cubrid-testcases/commits?sha=develop&per_page=1" \
  --jq '.[0].commit.message'

# Verify specific branch latest commit
gh api "repos/tw-kang/cubrid-testcases/commits?sha=<branch>&per_page=1" \
  --jq '.[0].commit.message'
```

---

## 7. Test Execution Order

### 7.1 Dependency Graph

```
Chain 1 (FORK-TC-REVISION)
    ↓
Chain 6 (FORK-REVERT) - requires Chain 1 result
    ↓
Complete

Chain 4 (FEAT-TC-REVISION)
    ↓
Chain 7 (FEAT-REVERT) - requires Chain 4 result
    ↓
Complete

Chain 2, 3, 5: Independent (any order)
```

### 7.2 Recommended Execution Order

1. **Sequential Execution (Dependency Aware)**
   ```
   Chain 1 → Chain 2 → Chain 3 → Chain 4 → Chain 5 → Chain 6 → Chain 7
   ```

2. **Parallel Execution (Independent Chains)**
   ```
   Chain 1, 4, 2, 3, 5: Run simultaneously
   Chain 6: After Chain 1 complete
   Chain 7: After Chain 4 complete
   ```

### 7.3 Estimated Time Per Chain

- Chain 1: ~5 minutes
- Chain 2: ~3 minutes
- Chain 3: ~2 minutes
- Chain 4: ~8 minutes
- Chain 5: ~3 minutes
- Chain 6: ~6 minutes
- Chain 7: ~5 minutes

**Total Estimate**: Sequential ~35 minutes, Parallel ~13 minutes

---

## 8. Cleanup

After test completion:

### 8.1 TC Branch Cleanup

```bash
# Delete all tc/ prefix branches in public TC
for branch in $(gh api repos/tw-kang/cubrid-testcases/branches?per_page=100 \
    --jq '[.[] | select(.name | startswith("tc/")) | .name] | .[]'); do
  echo "Deleting ${branch}..."
  gh api --method DELETE "repos/tw-kang/cubrid-testcases/git/refs/heads/${branch}"
done

# Delete all tc/ prefix branches in private TC
for branch in $(gh api repos/tw-kang/cubrid-testcases-private-ex/branches?per_page=100 \
    --jq '[.[] | select(.name | startswith("tc/")) | .name] | .[]'); do
  echo "Deleting ${branch}..."
  gh api --method DELETE "repos/tw-kang/cubrid-testcases-private-ex/git/refs/heads/${branch}"
done
```

### 8.2 Feature Branch Cleanup (TC repos)

```bash
# Delete feature/* branches in public TC
for branch in $(gh api repos/tw-kang/cubrid-testcases/branches?per_page=100 \
    --jq '[.[] | select(.name | startswith("feature/")) | .name] | .[]'); do
  echo "Deleting ${branch}..."
  gh api --method DELETE "repos/tw-kang/cubrid-testcases/git/refs/heads/${branch}"
done

# Delete feature/* branches in private TC
for branch in $(gh api repos/tw-kang/cubrid-testcases-private-ex/branches?per_page=100 \
    --jq '[.[] | select(.name | startswith("feature/")) | .name] | .[]'); do
  echo "Deleting ${branch}..."
  gh api --method DELETE "repos/tw-kang/cubrid-testcases-private-ex/git/refs/heads/${branch}"
done
```

### 8.3 Engine Repository PR and Branch Cleanup

```bash
# Close all open test PRs
gh pr list --repo tw-kang/cubrid --state open \
  --jq '.[] | select(.headRefName | startswith("cbrd-fork-") or startswith("revert/") or startswith("feature/feat-")) | .number' | \
  xargs -I{} sh -c 'gh pr close {} --repo tw-kang/cubrid'

# Delete test branches from public repo
for branch in $(git branch -r --remote=upstream | grep -E "cbrd-fork-|feature/feat-|revert/" | sed 's|upstream/||'); do
  git push upstream --delete "${branch}"
done

# Delete test branches from fork repo
for branch in $(git branch -r --remote=fork | grep -E "cbrd-fork-|feature/feat-|revert/" | sed 's|fork/||'); do
  git push fork --delete "${branch}"
done
```

### 8.4 Local Clone Cleanup

```bash
cd /path/to/cubrid
rm -rf tc-fork-* tc-feature-* tc-revert-*
```

### 8.5 TC Repository develop Resync

```bash
gh repo sync tw-kang/cubrid-testcases --branch develop
gh repo sync tw-kang/cubrid-testcases-private-ex --branch develop
```

---

## 9. Test Checklist

### Chain 1: FORK-TC-REVISION

- [ ] tc/pr-N public repo created ✅
- [ ] tc/pr-N private repo created ✅
- [ ] TC develop squash commit exists ✅
- [ ] Squash message header `[CBRD-fork-01]` ✅
- [ ] tc/pr-N both branches deleted ✅

### Chain 2: FORK-TC-NOCHANGE

- [ ] tc/pr-N created or deleted ✅
- [ ] TC develop unchanged ✅
- [ ] Workflow status recorded ✅

### Chain 3: FORK-PR-CLOSE

- [ ] tc/pr-N both branches deleted ✅
- [ ] TC develop unchanged ✅

### Chain 4: FEAT-TC-REVISION

- [ ] tc/pr-N public repo created ✅
- [ ] tc/pr-N private repo created ✅
- [ ] TC feature -> tc/pr-N PR merged ✅
- [ ] TC develop squash commit exists ✅
- [ ] Squash message header `[CBRD-feat-01]` ✅
- [ ] tc/pr-N both branches deleted ✅

### Chain 5: FEAT-TC-NOCHANGE

- [ ] tc/pr-N created or deleted ✅
- [ ] TC develop unchanged ✅

### Chain 6: FORK-REVERT

- [ ] PR-B merge successful ✅
- [ ] Revert PR created (`Reverts tw-kang/cubrid#<PR_A>`) ✅
- [ ] tc/pr-<REVERT_PR> revert commit exists ✅
- [ ] PR-A TC changes removed ✅
- [ ] PR-B TC changes preserved ✅
- [ ] TC develop revert squash merge ✅

### Chain 7: FEAT-REVERT

- [ ] Revert PR created (`Reverts tw-kang/cubrid#<FEAT_PR>`) ✅
- [ ] tc/pr-<REVERT_PR> revert commit exists ✅
- [ ] TC Revert Applied comment ✅
- [ ] TC develop revert squash merge ✅

---

## 10. Key Considerations

1. **pull_request_target Characteristic**: Workflow file is read from **default branch(`test/sync-tc`)**, not PR base branch. Must sync after modification.

2. **TC No Changes Workflow Behavior**: Current workflow may result in `commit_failed` when no TC changes exist. Report this in Chain 2, 5.

3. **TC develop Concurrent Access**: Risk of conflict when multiple reverts execute simultaneously. `concurrency.group = tc-branch-finalize` setting serializes execution.

4. **Fork PR head Specification**: Use `gh pr create --head kangtaewoo:<branch>` format to explicitly specify fork repo branch.

5. **Feature Branch Manual TC Creation**: Feature branch flow requires user manually create TC repo `feature/*` branch. Outside workflow automation scope.

---

## 11. References and Syntax Notes

### File References

- [`tc-branch-sync.yml`](./tc-branch-sync.yml): TC branch creation on PR open/sync and revert handling
- [`tc-branch-finalize.yml`](./tc-branch-finalize.yml): TC branch finalization on PR merge/close
- [`description.md`](./description.md): Architecture and design documentation
- [`test.md`](./test.md): Unit-level workflow functional testing (22 scenarios)

### gh CLI Command Key Syntax

**gh api**: No `--limit` option. Add `?per_page=N` parameter to URL instead:

```bash
# Correct
gh api "repos/OWNER/REPO/branches?per_page=100" --jq '...'
gh api "repos/OWNER/REPO/commits?sha=BRANCH&per_page=1" --jq '...'

# Incorrect
gh api repos/OWNER/REPO/branches --limit 100  # ❌ No --limit
```

**gh pr merge**: Combine `--merge` (merge commit), `--squash` (squash merge), `--delete-branch` (delete) options as needed
