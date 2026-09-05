/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
 * (dup2 on fd 1 is structurally impossible per-session — retired, B2-D5)
 * and the uw_sem wrappers the CON_STATUS_LOCK macros use (broker_shm.c is
 * not folded).  SSL termination is the real cas_ssl.c since B2-D9.
 */

#if defined (SERVER_MODE)

#include <semaphore.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cassert>
#include <cctype>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "broker_util.h"	/* trim */

#include "broker_config.h"
#include "broker_filename.h"	/* get_cubrid_file, FID_* */
#include "broker_shm.h"		/* uw_sem_* declarations */
#include "environment_variable.h"	/* envvar_logdir_file */
#include "cas_common.h"
#include "cas_common_vars.h"
#include "cas_dispatch.h"	/* the server-support API declarations */
#include "cas_log.h"
#include "cas_net_buf.h"
#include "cas_sql_log2.h"
#include "cas_ssl.h"
#include "system_parameter.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* ------------------------------------------------------------------ */
/* shm stubs                                                          */
/* ------------------------------------------------------------------ */

static T_SHM_APPL_SERVER cas_Shm_stub;
static thread_local T_APPL_SERVER_INFO cas_As_slot;

/* ------------------------------------------------------------------ */
/* session slot ids (B2-D1): the CAS slot index (shm_as_index) names   */
/* the per-session SQL/slow/DDL log files and the query plan file, so  */
/* each adopted session takes the lowest free index for its lifetime — */
/* the same identity a CAS process got from the broker's slot table    */
/* ------------------------------------------------------------------ */

static std::mutex cas_Slot_index_mutex;
static std::vector<bool> cas_Slot_index_used;

static int
cas_slot_index_alloc (void)
{
  std::lock_guard<std::mutex> guard (cas_Slot_index_mutex);

  for (size_t i = 0; i < cas_Slot_index_used.size (); i++)
    {
      if (!cas_Slot_index_used[i])
	{
	  cas_Slot_index_used[i] = true;
	  return (int) i;
	}
    }
  cas_Slot_index_used.push_back (true);
  return (int) (cas_Slot_index_used.size () - 1);
}

static void
cas_slot_index_free (int slot)
{
  std::lock_guard<std::mutex> guard (cas_Slot_index_mutex);

  if (slot >= 0 && (size_t) slot < cas_Slot_index_used.size ())
    {
      cas_Slot_index_used[slot] = false;
    }
}

void
cas_server_speaker_boot_init (const char *db_name)
{
  T_SHM_APPL_SERVER *shm = &cas_Shm_stub;

  std::memset (shm, 0, sizeof (*shm));

  /* config the folded code reads; broker conf defaults where the value was
   * broker-owned, forced OFF where the facility is retired in-server.
   * Log production config (B2-D7) comes from the cas_* system parameters,
   * refreshed per session in cas_server_session_slot_begin. */
  std::strncpy (shm->broker_name, db_name, BROKER_NAME_LEN - 1);	/* log files: <dbname>_<slot+1>.* (B2-D2) */
  shm->session_timeout = -1;	/* net_read path falls back to NET_DEFAULT_TIMEOUT */
  shm->max_string_length = -1;
  shm->max_prepared_stmt_count = 2000;	/* DEFAULT_MAX_PREPARED_STMT_COUNT */
  shm->statement_pooling = ON;
  shm->cci_default_autocommit = ON;
  shm->keep_connection = KEEP_CON_ON;	/* connection == session (#116 D5) */
  shm->sql_log_mode = SQL_LOG_MODE_NONE;	/* per-session seed comes from PRM_ID_CAS_SQL_LOG */
  shm->slow_log_mode = SLOW_LOG_MODE_OFF;
  shm->sql_log2 = 0;		/* retired in-server: dup2 on fd 1 is process-wide (B2-D5) */
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

  /* the access log is one shared append-mode file: <dbname>.access under the
   * broker log root, mirroring get_access_log_file_name (broker_shm.c) */
  {
    char name_buf[BROKER_PATH_MAX];

    snprintf (name_buf, sizeof (name_buf), "broker/%s.access", db_name);
    envvar_logdir_file (shm->access_log_file, sizeof (shm->access_log_file), name_buf);
  }

  /* the broker created the log directories at its own startup; the server
   * does the same for the producers it now hosts (EEXIST is the norm) */
  {
    char dir_buf[BROKER_PATH_MAX];

    envvar_logdir_file (dir_buf, sizeof (dir_buf), "broker/");
    (void) mkdir (dir_buf, 0777);
    get_cubrid_file (FID_SQL_LOG_DIR, dir_buf, sizeof (dir_buf));
    (void) mkdir (dir_buf, 0777);
    get_cubrid_file (FID_CAS_TMP_DIR, dir_buf, sizeof (dir_buf));
    (void) mkdir (dir_buf, 0777);
  }

  shm_appl = shm;
  broker_name[0] = '\0';
  std::strncpy (broker_name, db_name, BROKER_NAME_LEN - 1);
  program_name = "cub_server";
  cas_shard_flag = OFF;

  set_net_buf_size ();
}

/* per session: refresh the CAS-owned config from the cas_* system parameters
 * (B2-D7, #116 D9 split) into this thread's own snapshot — every speaker
 * reads CAS_SHM_CFG(), so no session writes a field another session is
 * reading (PR 7837 review; dynamic changes take effect for sessions started
 * afterwards, as before). */
static void
cas_server_refresh_session_config (T_APPL_SERVER_INFO *slot)
{
  T_CAS_SESSION_CFG *cfg = &cas_session_cfg;

  slot->cur_sql_log_mode = (char) prm_get_integer_value (PRM_ID_CAS_SQL_LOG);
  slot->cur_slow_log_mode = prm_get_bool_value (PRM_ID_CAS_SLOW_LOG) ? SLOW_LOG_MODE_ON : SLOW_LOG_MODE_OFF;

  cfg->sql_log_max_size = prm_get_integer_value (PRM_ID_CAS_SQL_LOG_MAX_SIZE);
  cfg->access_log = prm_get_bool_value (PRM_ID_CAS_ACCESS_LOG) ? ON : OFF;
  cfg->access_log_max_size = prm_get_integer_value (PRM_ID_CAS_ACCESS_LOG_MAX_SIZE);
  cfg->long_query_time = prm_get_integer_value (PRM_ID_CAS_LONG_QUERY_TIME);
  cfg->long_transaction_time = prm_get_integer_value (PRM_ID_CAS_LONG_TRANSACTION_TIME);

  cfg->jdbc_cache = prm_get_bool_value (PRM_ID_CAS_JDBC_CACHE) ? ON : OFF;
  cfg->jdbc_cache_only_hint = prm_get_bool_value (PRM_ID_CAS_JDBC_CACHE_HINT_ONLY) ? ON : OFF;
  cfg->jdbc_cache_life_time = prm_get_integer_value (PRM_ID_CAS_JDBC_CACHE_LIFE_TIME);
  cfg->statement_pooling = prm_get_bool_value (PRM_ID_CAS_STATEMENT_POOLING) ? ON : OFF;
  cfg->cci_default_autocommit = prm_get_bool_value (PRM_ID_CAS_CCI_DEFAULT_AUTOCOMMIT) ? ON : OFF;
  cfg->max_prepared_stmt_count = prm_get_integer_value (PRM_ID_CAS_MAX_PREPARED_STMT_COUNT);
  cfg->session_timeout = prm_get_integer_value (PRM_ID_CAS_SESSION_TIMEOUT);
  cfg->max_string_length = prm_get_integer_value (PRM_ID_CAS_MAX_STRING_LENGTH);
  cfg->query_timeout = prm_get_integer_value (PRM_ID_CAS_MAX_QUERY_TIMEOUT);
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
  cas_server_refresh_session_config (slot);
  /* cas_common_main.c:486 equivalent — straight to TLS, never through the
   * shared read-only stub (concurrent slot_begins would race on it) */
  stripped_column_name = prm_get_bool_value (PRM_ID_CAS_STRIPPED_COLUMN_NAME) ? ON : OFF;
  slot->cur_statement_pooling = CAS_SHM_CFG (statement_pooling) ? ON : OFF;
  slot->cci_default_autocommit = CAS_SHM_CFG (cci_default_autocommit);
  slot->auto_commit_mode = FALSE;
  slot->cur_sql_log2 = 0;
  slot->isolation_level = CAS_USE_DEFAULT_DB_PARAM;
  slot->lock_timeout = CAS_USE_DEFAULT_DB_PARAM;
  slot->clt_version = client_version;
  slot->cas_client_type = (char) client_type;
  std::memcpy (slot->driver_info, driver_info, SRV_CON_CLIENT_INFO_SIZE);
  slot->fn_status = FN_STATUS_CONN;

  as_info = slot;
  shm_as_index = cas_slot_index_alloc ();	/* names this session's log files (B2-D1) */

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
      cas_slot_index_free (shm_as_index);
      shm_as_index = 0;
    }
}

/* ------------------------------------------------------------------ */
/* ACCESS_CONTROL (B2-D8, #116 D6): the CAS's db:dbuser:ip check moves  */
/* to session establishment.  The file keeps the broker's format —      */
/* [%BROKER] sections of "dbname:dbuser:ipfile[,ipfile]" lines, ip      */
/* files resolved against $CUBRID/conf — and the matcher mirrors        */
/* access_control_check_right_internal: '*' wildcards, case-blind       */
/* names, prefix-match ips (leading count byte, 0 = any), loopback      */
/* unconditionally allowed.  Dropped relative to the CAS: the           */
/* last-access-time shm write-back (#116 D10 forbids it) and the        */
/* local-broker-ip retry (D8 pins broker host = DB host).  A missing    */
/* or invalid file while the check is ON fails closed.                  */
/* ------------------------------------------------------------------ */

struct cas_acl_rule
{
  std::string dbname;
  std::string dbuser;
  std::vector<unsigned char> addresses;	/* IP_BYTE_COUNT bytes per entry */
};

static std::mutex cas_Acl_mutex;
/* key: broker section name, lower-cased */
static std::map<std::string, std::vector<cas_acl_rule>> cas_Acl_table;
static bool cas_Acl_loaded = false;
static bool cas_Acl_load_failed = false;

static std::string
cas_acl_lower (const char *s)
{
  std::string out (s);
  for (char &c : out)
    {
      c = (char) tolower ((unsigned char) c);
    }
  return out;
}

/* one ip list file, same accept rules as access_control_read_ip_info */
static int
cas_acl_read_ip_file (const char *path, std::vector<unsigned char> &addresses)
{
  FILE *fp = fopen (path, "r");
  char buf[LINE_MAX];

  if (fp == NULL)
    {
      return -1;
    }

  while (fgets (buf, (int) sizeof (buf), fp) != NULL)
    {
      char *p = strchr (buf, '#');
      char *save = NULL;
      unsigned char entry[IP_BYTE_COUNT] = { 0, 0, 0, 0, 0 };
      int i;

      if (p != NULL)
	{
	  *p = '\0';
	}
      trim (buf);
      if (buf[0] == '\0')
	{
	  continue;
	}

      char *token = strtok_r (buf, ".", &save);
      for (i = 0; i < 4; i++)
	{
	  if (token == NULL)
	    {
	      fclose (fp);
	      return -1;
	    }
	  if (strcmp (token, "*") == 0)
	    {
	      break;
	    }
	  char *end = NULL;
	  long adr = strtol (token, &end, 10);
	  if (end == token || *end != '\0' || adr < 0 || adr > 255)
	    {
	      fclose (fp);
	      return -1;
	    }
	  entry[1 + i] = (unsigned char) adr;
	  token = strtok_r (NULL, ".", &save);
	}
      entry[0] = (unsigned char) i;	/* number of significant octets; 0 = any */

      addresses.insert (addresses.end (), entry, entry + IP_BYTE_COUNT);
    }

  fclose (fp);
  return 0;
}

/* parse the whole ACCESS_CONTROL_FILE into the per-broker table */
static int
cas_acl_load_locked (void)
{
  const char *file = prm_get_string_value (PRM_ID_CAS_ACCESS_CONTROL_FILE);
  char buf[1024];
  char path_buf[BROKER_PATH_MAX];
  std::map<std::string, std::vector<cas_acl_rule>> table;
  std::string current_section;

  cas_Acl_table.clear ();
  cas_Acl_loaded = true;
  cas_Acl_load_failed = true;	/* until proven otherwise: fail closed */

  if (file == NULL || file[0] == '\0')
    {
      return -1;
    }

  FILE *fp = fopen (file, "r");
  if (fp == NULL)
    {
      return -1;
    }

  while (fgets (buf, (int) sizeof (buf), fp) != NULL)
    {
      char *p = strchr (buf, '#');
      if (p != NULL)
	{
	  *p = '\0';
	}
      trim (buf);
      if (buf[0] == '\0')
	{
	  continue;
	}

      size_t len = strlen (buf);
      if (strncmp (buf, "[%", 2) == 0 && buf[len - 1] == ']')
	{
	  buf[len - 1] = '\0';
	  current_section = cas_acl_lower (buf + 2);
	  continue;
	}
      if (current_section.empty ())
	{
	  goto parse_error;	/* an entry before any [%BROKER] section */
	}

      {
	char *save = NULL;
	char *dbname = strtok_r (buf, ":", &save);
	char *dbuser = (dbname != NULL) ? strtok_r (NULL, ":", &save) : NULL;
	char *ip_files = save;

	if (dbname == NULL || dbuser == NULL || ip_files == NULL || ip_files[0] == '\0')
	  {
	    goto parse_error;
	  }

	cas_acl_rule rule;
	rule.dbname = dbname;
	rule.dbuser = dbuser;

	char *fsave = NULL;
	for (char *files = ip_files;; files = NULL)
	  {
	    char *token = strtok_r (files, ",", &fsave);
	    if (token == NULL)
	      {
		break;
	      }
	    trim (token);
	    if (make_abs_path (path_buf, "conf", token, BROKER_PATH_MAX) < 0
		|| cas_acl_read_ip_file (path_buf, rule.addresses) < 0)
	      {
		goto parse_error;
	      }
	  }

	table[current_section].push_back (std::move (rule));
      }
    }

  fclose (fp);
  cas_Acl_table = std::move (table);
  cas_Acl_load_failed = false;
  return 0;

parse_error:
  fclose (fp);
  return -1;
}

void
cas_server_acl_reload (void)
{
  std::lock_guard<std::mutex> guard (cas_Acl_mutex);

  (void) cas_acl_load_locked ();
}

int
cas_server_acl_check (const char *broker, const char *dbname, const char *dbuser, const unsigned char *address)
{
  bool local_ip = (address[0] == 127 && address[1] == 0 && address[2] == 0 && address[3] == 1);

  if (!prm_get_bool_value (PRM_ID_CAS_ACCESS_CONTROL))
    {
      return 0;
    }

  std::lock_guard<std::mutex> guard (cas_Acl_mutex);

  if (!cas_Acl_loaded)
    {
      (void) cas_acl_load_locked ();
    }
  if (cas_Acl_load_failed)
    {
      return local_ip ? 0 : -1;	/* fail closed, loopback still allowed */
    }

  auto it = cas_Acl_table.find (cas_acl_lower (broker != NULL ? broker : ""));
  const std::vector<cas_acl_rule> *rules = (it != cas_Acl_table.end ()) ? &it->second : NULL;

  if ((rules == NULL || rules->empty ()) && prm_get_bool_value (PRM_ID_CAS_ACCESS_CONTROL_DEFAULT_ALLOW))
    {
      return 0;
    }

  /* dbname may carry an @host suffix; match only the name part */
  const char *at = strchr (dbname, '@');
  size_t dbname_len = (at != NULL) ? (size_t) (at - dbname) : strlen (dbname);

  if (rules != NULL)
    {
      for (const cas_acl_rule &rule : *rules)
	{
	  if (rule.dbname != "*"
	      && (rule.dbname.size () != dbname_len || strncasecmp (rule.dbname.c_str (), dbname, dbname_len) != 0))
	    {
	      continue;
	    }
	  if (rule.dbuser != "*" && strcasecmp (rule.dbuser.c_str (), dbuser) != 0)
	    {
	      continue;
	    }
	  for (size_t i = 0; i + IP_BYTE_COUNT <= rule.addresses.size (); i += IP_BYTE_COUNT)
	    {
	      unsigned char sig = rule.addresses[i];
	      if (sig == 0 || memcmp (&rule.addresses[i + 1], address, sig) == 0)
		{
		  return 0;
		}
	    }
	}
    }

  return local_ip ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* access log: one shared append-mode file for all sessions; appends   */
/* are atomic per fprintf, but the rotation path (ftell/rename/reopen) */
/* is read-modify-write — serialize the whole producer (B2-D6)         */
/* ------------------------------------------------------------------ */

static std::mutex cas_Access_log_mutex;

int
cas_server_access_log (struct timeval *start_time, int as_index, int client_ip_addr, char *dbname, char *dbuser,
		       int log_type)
{
  std::lock_guard<std::mutex> guard (cas_Access_log_mutex);

  return cas_access_log (start_time, as_index, client_ip_addr, dbname, dbuser, (ACCESS_LOG_TYPE) log_type);
}

/* ------------------------------------------------------------------ */
/* uw_sem wrappers (CON_STATUS_LOCK); broker_shm.c is not folded      */
/* ------------------------------------------------------------------ */

int
uw_sem_init (sem_t *sem)
{
  /* pshared=0: both sides of this lock live in cub_server now */
  return sem_init (sem, 0, 1);
}

int
uw_sem_wait (sem_t *sem)
{
  return sem_wait (sem);
}

int
uw_sem_post (sem_t *sem)
{
  return sem_post (sem);
}

int
uw_sem_destroy (sem_t *sem)
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

void
sql_log2_flush (void)
{
}

#endif /* SERVER_MODE */
