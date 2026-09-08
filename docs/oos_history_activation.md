# OOS history format activation

This is the compatibility foundation for CBRD-26939. Durable OOS image
publication and decoding are subsequent integration work; this change alone
does not provide complete CDC or flashback support for OOS values. Keep these
changes on the integration branch until that work and its regressions pass.

## Database states

| Database | Upgraded engine | Baseline engine | Supplemental OOS writes |
| --- | --- | --- | --- |
| Existing, inactive (disk compatibility 11.5) | Opens without activation | Can still open | Activation-required error |
| Existing, explicitly activated | Opens | Rejects before recovery | Requires the subsequent durable-image implementation |
| Newly created by the upgraded engine | Current format immediately | Rejects before recovery | Requires the subsequent durable-image implementation |

The integration branch assigns disk compatibility **11.6** to the OOS history
format. This is a disk-format identifier, not a change to the product release
string. Release integration must retain this reservation consistently across
engines, readers, and supported distribution branches. The baseline is PR
#6864 commit `2940b1cfbc3c2d4d0fac3f9244a960350debd380`.

## Activate an existing database

1. Upgrade all engines and historical log readers that can access the database
   or its copied logs. For HA, coordinate this across primary, replicas, log
   copiers, and appliers before enabling new-format writes.
2. Take a recoverable backup and cleanly stop the database server. If the
   database previously crashed, restart it to complete recovery, then stop it
   cleanly. Retain the ordinary database file locking configuration.
3. With the upgraded installation selected, run:

   ```sh
   cubrid activatehistorydb database_name
   ```

4. Restart using the upgraded engine. Take a new backup for subsequent
   recovery. Do not restart an older engine against the activated database.

The utility requires filesystem access to the database, like other offline
administration utilities. It does not log in as a database user or start
recovery. It takes the exclusive active-log lock and refuses an unclean log
header. Activation is one-way and idempotent: repeating it after a clean
shutdown succeeds. There is no deactivate option. New databases need no
activation command.

With supplemental logging disabled, inactive databases retain normal OOS
writes. With it enabled, an insert, update, or delete requiring an OOS history
image fails with `ER_OOS_HISTORY_ACTIVATION_REQUIRED` (-1385). This includes
an update whose old image contains OOS even when its new value fits inline.
Non-OOS changes remain usable. Turning supplemental logging off after
activation does not restore the old compatibility level.

## Durability and recovery

Activation changes only the compatibility field in the active-log header.
It preserves data pages, existing log records, and the ordinary recovery
format. It flushes the marker before releasing the lock and reporting success;
`suppress_fsync` cannot bypass that flush. Current-format startup also performs
a checked flush before recovery, covering interruption after the activation
write reached the OS cache but before synchronization completed.

If activation is interrupted or reports an I/O error, correct the I/O problem
and retry the command. It may already have established the current format, so
use the upgraded engine for subsequent access. The regression covers process
termination before/after write and synchronization, plus write/sync errors.
Physical media corruption still requires normal backup and recovery procedures.

Backups record the database's actual compatibility level. An inactive backup
remains restorable by the baseline engine. A current-format backup requires
the upgraded engine. Restoring an older backup is a recovery operation with
that backup's history boundary, not a way to downgrade current-format logs.

Existing non-OOS supplemental history remains readable across activation.
Activation does not reconstruct legacy OOS values whose referenced storage
has already been reclaimed. Explicit rejection of that legacy history belongs
to the subsequent historical-image reader work.

## HA and reader boundary

The upgraded copied-log reader validates disk compatibility before interpreting
the active log. This applies to `applylogdb` and the copied-log view of
`applyinfo`. Engine-open rejection does **not** fence an already installed old
independent applier: the baseline reader lacks this check, and archive headers
do not carry the compatibility field. Coordinated replacement and stopping of
old readers is therefore a rollout requirement. Do not treat successful engine
startup as proof that every remote reader was upgraded.

## Regression

Run the public lifecycle regression with separate baseline and current
installations. On Linux it uses private user, mount, PID, IPC and network
namespaces, private Unix sockets, and copied installations/databases:

```sh
bash unit_tests/oos/scripts/test_history_compatibility.sh \
  /path/to/baseline/install /path/to/current/install
```

The script retains logs and databases under the printed evidence directory.
It requires `unshare`, `mount`, `ip`, `timeout`, a C compiler, and working
installations including their Java runtime. Set `HISTORY_TEST_ROOT` to choose
the evidence directory. Use `HISTORY_PAGE_SIZE=4K`, `8K`, or `16K` to exercise
matching data/log page sizes. The fault injector observes filesystem syscalls;
it does not depend on private database header offsets or numeric marker tests.
