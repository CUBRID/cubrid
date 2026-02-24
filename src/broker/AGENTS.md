# src/broker/ — Connection Broker & CAS

## OVERVIEW

CUBRID broker manages client connections, dispatches to CAS (CUBRID Application Server) processes. 138 files — largest module by file count.

## KEY FILES

| File | Lines | Purpose |
|------|-------|---------|
| `cas_execute.c` | 10k | CAS statement execution |
| `broker.c` | | Broker main loop |
| `cas.c` | | CAS process entry point |
| `broker_shm.c` | | Shared memory for broker↔CAS |
| `shard_*.c` | | Database sharding support |

## CODEOWNER

All files → @beyondykk9

## NOTES

- Broker is a separate process from the DB server
- CAS = one process per client connection (process pool model)
- Configuration via `cubrid_broker.conf`
