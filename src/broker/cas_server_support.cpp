/*
 *  Copyright 2016 CUBRID Corporation
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

/*
 * cas_server_support.cpp - the environment the folded CAS speaker expects,
 *                          supplied by cub_server (stage B1, #117)
 *
 * The CAS request code reads its configuration from broker shared memory
 * (shm_appl) and its per-connection scratch from a CAS slot (as_info).  In
 * the merged server there is no broker shm: shm_appl points at one shared
 * read-only stub filled at boot, and as_info points at a thread_local slot
 * initialized per adopted session (the session thread IS the CAS process,
 * B1-D1/D2).  Also provides the deliberately-absent surfaces: sql_log2
 * (dup2 on fd 1 is structurally impossible per-session — forced off), SSL
 * (server-side termination is a B2 item), and the uw_sem wrappers the
 * CON_STATUS_LOCK macros use (broker_shm.c is not folded).
 */

#if defined (SERVER_MODE)

#include <semaphore.h>

#include <cassert>
#include <cstring>

#include "broker_config.h"
#include "broker_shm.h"		/* uw_sem_* declarations */
#include "cas_common.h"
#include "cas_common_vars.h"
#include "cas_dispatch.h"	/* the server-support API declarations */
#include "cas_net_buf.h"
#include "cas_sql_log2.h"
#include "cas_ssl.h"
#include "system_parameter.h"

/* ------------------------------------------------------------------ */
/* shm stubs                                                          */
/* ------------------------------------------------------------------ */

static T_SHM_APPL_SERVER cas_Shm_stub;
static thread_local T_APPL_SERVER_INFO cas_As_slot;

void
cas_server_speaker_boot_init (const char *db_name)
{
  T_SHM_APPL_SERVER *shm = &cas_Shm_stub;

  (void) db_name;		/* identity lives in the session boot; kept for the B2 log naming */

  std::memset (shm, 0, sizeof (*shm));

  /* config the folded code reads; broker conf defaults where the value was
   * broker-owned, forced OFF where the facility is retired in-server */
  std::strncpy (shm->broker_name, "cub_server", BROKER_NAME_LEN - 1);
  shm->session_timeout = -1;	/* net_read path falls back to NET_DEFAULT_TIMEOUT */
  shm->max_string_length = -1;
  shm->max_prepared_stmt_count = 2000;	/* DEFAULT_MAX_PREPARED_STMT_COUNT */
  shm->statement_pooling = ON;
  shm->cci_default_autocommit = ON;
  shm->keep_connection = KEEP_CON_ON;	/* connection == session (#116 D5) */
  shm->sql_log_mode = SQL_LOG_MODE_NONE;	/* producer moves in B2/B4 (#116 D4) */
  shm->slow_log_mode = SLOW_LOG_MODE_OFF;
  shm->sql_log2 = 0;
  shm->access_log = OFF;
  shm->jdbc_cache = OFF;
  shm->trigger_action_flag = ON;
  shm->access_mode = READ_WRITE_ACCESS_MODE;
  shm->replica_only_flag = OFF;
  shm->cache_user_info = OFF;
  shm->cas_rctime = 0;
  shm->long_query_time = 0;
  shm->long_transaction_time = 0;
  shm->job_queue_size = 0;
  shm->net_buf_size = 0;	/* set_net_buf_size falls back to the default */

  shm_appl = shm;
  broker_name[0] = '\0';
  std::strncpy (broker_name, "cub_server", BROKER_NAME_LEN - 1);
  program_name = "cub_server";
  cas_shard_flag = OFF;
  shm_as_index = 0;

  set_net_buf_size ();
}

/* per adopted session: point the CAS globals at this thread's slot */
void
cas_server_session_slot_begin (int client_type, int client_version, const char *driver_info)
{
  T_APPL_SERVER_INFO *slot = &cas_As_slot;

  std::memset (slot, 0, sizeof (*slot));
  CON_STATUS_LOCK_INIT (slot);

  slot->service_flag = ON;
  slot->con_status = CON_STATUS_IN_TRAN;	/* connect starts in-tran (cas_common_main.c:136) */
  slot->cur_keep_con = KEEP_CON_ON;
  slot->cur_statement_pooling = shm_appl->statement_pooling ? ON : OFF;
  slot->cci_default_autocommit = shm_appl->cci_default_autocommit;
  slot->auto_commit_mode = FALSE;
  slot->cur_sql_log_mode = SQL_LOG_MODE_NONE;
  slot->cur_slow_log_mode = SLOW_LOG_MODE_OFF;
  slot->cur_sql_log2 = 0;
  slot->isolation_level = CAS_USE_DEFAULT_DB_PARAM;
  slot->lock_timeout = CAS_USE_DEFAULT_DB_PARAM;
  slot->clt_version = client_version;
  slot->cas_client_type = (char) client_type;
  std::memcpy (slot->driver_info, driver_info, SRV_CON_CLIENT_INFO_SIZE);
  slot->fn_status = FN_STATUS_CONN;

  as_info = slot;

  /* per-connection CAS globals that cas_common_main.c's session setup used
   * to (re)initialize */
  cas_client_type = (char) client_type;
  errors_in_transaction = 0;
  is_first_request = true;
  con_status_before_check_cas = -1;
  query_cancel_flag = 0;
  cas_info_size = CAS_INFO_SIZE;
  std::memset (prev_cas_info, CAS_INFO_RESERVED_DEFAULT, sizeof (prev_cas_info));
}

void
cas_server_session_slot_end (void)
{
  if (as_info == &cas_As_slot)
    {
      CON_STATUS_LOCK_DESTROY (&cas_As_slot);
      as_info = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* uw_sem wrappers (CON_STATUS_LOCK); broker_shm.c is not folded      */
/* ------------------------------------------------------------------ */

int
uw_sem_init (sem_t * sem)
{
  /* pshared=0: both sides of this lock live in cub_server now */
  return sem_init (sem, 0, 1);
}

int
uw_sem_wait (sem_t * sem)
{
  return sem_wait (sem);
}

int
uw_sem_post (sem_t * sem)
{
  return sem_post (sem);
}

int
uw_sem_destroy (sem_t * sem)
{
  return sem_destroy (sem);
}

/* ------------------------------------------------------------------ */
/* sql_log2: dup2 on fd 1 cannot be per-session — structurally off    */
/* (#B1 fold note R3); the stubs keep the folded call sites linkable  */
/* ------------------------------------------------------------------ */

void
sql_log2_init (char *, int, int, bool)
{
}

char *
sql_log2_get_filename (void)
{
  static char empty[] = "";
  return empty;
}

void
sql_log2_dup_stdout (void)
{
}

void
sql_log2_restore_stdout (void)
{
}

void
sql_log2_end (bool)
{
}

void
sql_log2_write (const char *, ...)
{
}

void
sql_log2_append_file (char *)
{
}

/* ------------------------------------------------------------------ */
/* SSL: server-side termination is a B2 item (#116); the broker       */
/* rejects DIRECT_HANDOFF+SSL so these are unreachable                */
/* ------------------------------------------------------------------ */

bool ssl_client = false;

int
cas_ssl_read (int, char *, int)
{
  assert (false);
  return -1;
}

int
cas_ssl_write (int, const char *, int)
{
  assert (false);
  return -1;
}

bool
is_ssl_data_ready (int)
{
  assert (false);
  return false;
}

#endif /* SERVER_MODE */
