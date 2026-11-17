# Workflow tokens

Every token the workflows in `.github/workflows/` use, and what for.
A pull request that changes a workflow's token or its permissions changes this table too.

Rules:

- Work inside `CUBRID/cubrid` (comments, check runs, commit statuses, the `tc-pin` branch) uses `GITHUB_TOKEN`,
  with only the permissions the job needs. `GITHUB_TOKEN` reaches this repository only.
- Work in the testcase repositories uses the GitHub App `cubrid-tc-sync-bot`
  (secrets `TC_APP_ID` · `TC_APP_PRIVATE_KEY`, installed on `cubrid-testcases` · `cubrid-testcases-private-ex` ·
  `cubrid-testcases-private`). A job names the repositories and the permissions it needs.
  Without `permission-*` inputs the token gets all of the App's permissions.

## `GITHUB_TOKEN`

| Workflow | Job | Permissions | For |
|---|---|---|---|
| `check.yml` | `cppcheck` | repository default | comment on the PR |
| `comment_trigger.yml` | `run_test` | `pull-requests: read` | read the PR |
| `gha-ci.yml` | `gate` | `contents: read` · `pull-requests: read` · `actions: read` | read the PR, the comment and earlier runs |
| `gha-ci.yml` | `nightly` | `actions: write` · `statuses: read` | dispatch the nightly runs |
| `gha-ci.yml` | `status_pending` · `build_status` · `status_cancelled` | `statuses: write` (`build_status` also `actions: read`) | commit statuses `gha-ci: …` |
| `gha-ci.yml` → `gha-ci-suite.yml` | `suite` → `collect` · `status` | `actions: read` · `statuses: write` | read the shard jobs, commit statuses |
| `submodule-bump-receiver.yml` | `bump` (Run CI) | `contents: write` · `pull-requests: write` · `actions: write` (workflow level) | start CI on the bump PR |
| `tc-branch-sync.yml` | `comment-on-engine-pr` · `comment-on-engine-pr-revert` | `pull-requests: write` | comment on the engine PR |
| `tc-branch-finalize.yml` | `finalize-tc-branch` | `pull-requests: write` | comment on the engine PR |
| `tc-merge-gate.yml` | `check-tc-prs` | `actions: read` · `checks: write` · `contents: read` · `pull-requests: write` | check run `Check TC PRs`, comment |
| `tc-merge-gate.yml` | `resweep` | `actions: write` · `checks: write` · `pull-requests: read` | the scheduled sweep of open engine PRs |
| `tc-pin.yml` | `pin` | `contents: write` · `pull-requests: read` | push the `tc-pin` branch, find the PR of an engine commit |

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

## App `cubrid-submodule-bot`

| Workflow | Job | Repositories | Permissions | For |
|---|---|---|---|---|
| `submodule-bump-receiver.yml` | `plan` · `bump` | `CUBRID/cubrid` (no `owner`, no `repositories`) | all | push `bump/<submodule>`, open the bump PR |
