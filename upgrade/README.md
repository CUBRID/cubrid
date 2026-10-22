# System metadata upgrade scripts

`cubrid upgradedb` brings a database's system metadata (system catalog,
`information_schema`, …) up to the running binary's version, in place. The target
version is `SYSTEM_METADATA_VERSION` (`src/object/system_metadata_version.h`).

Scripts carry catalog **class** changes only. The catalog vclasses (`db_*`,
`information_schema.*`) are rebuilt from the running binary once the scripts have run.

Two kinds of script live here:

| | Versioned | Internal (debug builds only) |
|---|---|---|
| File | `v<N>_to_v<N+1>.sql` | `internal/cbrd<NNNNN>.sql` |
| Run by | `cubrid upgradedb <db>` | `cubrid upgradedb --apply-script-list <list> <db>` |
| Role | released upgrade step | dev staging, later folded into a versioned script |

## Filename rules

- **Versioned** `v<N>_to_v<N+1>.sql` — the chain must start at `v1_to_v2`, be
  contiguous, and end at `SYSTEM_METADATA_VERSION`; the build fails otherwise. `N` is
  the source version, `N+1` the target.
- **Internal** `cbrd<NNNNN>.sql` — ticket number only, no free-form topic. If one
  ticket needs several, suffix them `cbrd<NNNNN>_<seq>.sql`. Apply order comes from
  the script-list file, not the filename.

## Workflow A — add an internal script (during development)

When a PR changes system metadata:

1. Make the metadata change as usual.
2. Add `internal/cbrd<NNNNN>.sql` that applies the same change to an existing
   database. Skip if the change was only to a vclass definition.
3. On a debug build, list it in a script-list file and run
   `cubrid upgradedb --apply-script-list <list> <db>`.
4. Check the upgraded metadata matches a freshly created database.
5. Commit the script with the PR.

## Workflow B — consolidate into a versioned script (before feature merge / release)

1. Fold the accumulated internal scripts, in order, into one `v<N>_to_v<N+1>.sql`
   (`N` = current `SYSTEM_METADATA_VERSION`).
2. Bump `SYSTEM_METADATA_VERSION` to `N+1`, in the same commit — the build fails
   while the two disagree.
3. Rebuild.
4. Verify: a version-`N` database is refused at boot, `cubrid upgradedb` takes it
   to `N+1`, and its metadata converges with a freshly created `N+1` database.
5. Remove the internal scripts the versioned script now covers.
