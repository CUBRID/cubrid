# Workflow tokens

Where the workflows use the GitHub App `cubrid-tc-sync-bot`, and which `GITHUB_TOKEN` permissions the testcase
and gha-ci workflows (`tc-*.yml`, `gha-ci*.yml`) ask for.
A pull request that changes one of these tokens or its permissions changes this table too.

Rules for `tc-*.yml` and `gha-ci*.yml`:

- Work inside `CUBRID/cubrid` (comments, check runs, commit statuses, the `tc-pin` branch) uses `GITHUB_TOKEN`,
  with only the permissions the job needs. `GITHUB_TOKEN` reaches this repository only.
- Work in the testcase repositories uses the GitHub App `cubrid-tc-sync-bot`
  (secrets `TC_APP_ID` · `TC_APP_PRIVATE_KEY`, installed on `cubrid-testcases` · `cubrid-testcases-private-ex` ·
  `cubrid-testcases-private`), and a job names the repositories and the permissions it needs.
  Without `permission-*` inputs the token gets all of the App's permissions: the rows that say "all" do not follow
  this rule yet.

## App `cubrid-tc-sync-bot`

| Workflow | Job | Repositories | Permissions | For |
|---|---|---|---|---|
| `gha-ci.yml` | `plan` | `cubrid-testcases-private-ex` | `contents: read` | fetch the testcases |
| `gha-ci-suite.yml` | `shard` | `cubrid-testcases-private-ex` | `contents: read` | fetch the testcases |
| `submodule-bump-receiver.yml` | `bump` | `cubrid-testcases-private-ex` | `contents: read` | fetch the testcases for CI |
| `tc-branch-sync.yml` | `sync-tc-branch` · `revert-tc-branch` | one per matrix entry (`cubrid-testcases` · `cubrid-testcases-private-ex`) | all | create `tc/pr-<N>`, open the TC PR |
| `tc-branch-sync.yml` | `comment-on-engine-pr` · `comment-on-engine-pr-revert` | all installed | all | find the TC PRs to link |
| `tc-branch-finalize.yml` | `finalize-tc-branch` | one per matrix entry (`cubrid-testcases` · `cubrid-testcases-private-ex`) | all | close the TC PR, delete `tc/pr-<N>` |
| `tc-merge-gate.yml` | `check-tc-prs` · `resweep` | `cubrid-testcases` · `cubrid-testcases-private-ex` | all | read the TC PRs |
| `tc-pin.yml` | `pin` | `cubrid-testcases` · `cubrid-testcases-private-ex` · `cubrid-testcases-private` | `contents: read` · `pull-requests: read` | branch tips, merged TC PRs |
| `tc-sync-bot-trial.yml` | `try` | the chosen one of `cubrid-testcases` · `cubrid-testcases-private-ex` | `contents: write` · `pull-requests: write`, and `statuses: write` for `status` | try the bot on the branch `tc-sync-bot-trial` |

## `GITHUB_TOKEN` in `tc-*.yml` and `gha-ci*.yml`

| Workflow | Job | Permissions | For |
|---|---|---|---|
| `gha-ci.yml` | `gate` | `contents: read` · `pull-requests: read` · `actions: read` | read the PR, the comment and earlier runs |
| `gha-ci.yml` | `nightly` | `actions: write` · `statuses: read` | dispatch the nightly runs |
| `gha-ci.yml` | `status_pending` · `build_status` · `status_cancelled` | `statuses: write` (`build_status` also `actions: read`) | commit statuses `gha-ci: …` |
| `gha-ci.yml` → `gha-ci-suite.yml` | `suite` → `collect` | `actions: read` | read the shard jobs |
| `gha-ci.yml` → `gha-ci-suite.yml` | `suite` → `status` | `statuses: write` | commit statuses `gha-ci: …` |
| `tc-branch-sync.yml` | `detect-pr-type` | none (`permissions: {}`) | read the PR's commits and a reverted commit's PR (public reads) |
| `tc-branch-sync.yml` | `sync-tc-branch` · `revert-tc-branch` | `pull-requests: write` | not used: their steps use the App token |
| `tc-branch-sync.yml` | `comment-on-engine-pr` · `comment-on-engine-pr-revert` | `pull-requests: write` | comment on the engine PR |
| `tc-branch-finalize.yml` | `finalize-tc-branch` | `pull-requests: write` | comment on the engine PR |
| `tc-merge-gate.yml` | `check-tc-prs` | `actions: read` · `checks: write` · `contents: read` · `pull-requests: write` | check run `Check TC PRs`, comment |
| `tc-merge-gate.yml` | `resweep` | `actions: write` · `checks: write` · `pull-requests: read` | the scheduled sweep of open engine PRs |
| `tc-pin.yml` | `pin` | `contents: write` · `pull-requests: read` | push the `tc-pin` branch, find the PR of an engine commit |
