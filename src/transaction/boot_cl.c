/*
 * Copyright 2008 Search Solution Corporation
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
 * boot_cl.c - Boot management in the client
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#if !defined(WINDOWS)
#include <sys/time.h>
#endif /* WINDOWS */

#if !defined(WINDOWS)
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#endif /* !WINDOWS */

#include <assert.h>

#include "porting.h"
#if !defined(HPUX)
#include "util_func.h"
#endif /* !HPUX */
#include "boot_cl.h"
#include "memory_alloc.h"
#include "area_alloc.h"
#include "storage_common.h"
#include "oid.h"
#include "error_manager.h"
#include "authenticate.h"
#include "work_space.h"
#include "schema_manager.h"
#include "trigger_manager.h"
#include "db.h"
#if !defined(WINDOWS)
#include "dynamic_load.h"
#endif /* !WINDOWS */
#include "transaction_cl.h"
#include "log_comm.h"
#include "server_interface.h"
#include "release_string.h"
#include "system_parameter.h"
#include "locator_cl.h"
#include "databases_file.h"
#include "db_query.h"
#include "language_support.h"
#include "message_catalog.h"
#include "parser.h"
#include "perf_monitor.h"
#include "set_object.h"
#include "cnv.h"
#include "environment_variable.h"
#include "locator.h"
#include "transform.h"
#include "jansson.h"
#include "jsp_cl.h"
#include "client_support.h"
#include "es.h"
#include "tsc_timer.h"
#include "show_meta.h"
#include "tz_support.h"
#include "dbtype.h"
#include "object_primitive.h"
#include "connection_globals.h"
#include "host_lookup.h"
#include "schema_system_catalog.hpp"

#include "authenticate_context.hpp"

#if defined(CS_MODE)
#include "network.h"
#include "connection_cl.h"
#endif /* CS_MODE */
#include "network_interface_cl.h"

#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

/* TODO : Move .h */
#if defined(SA_MODE)
extern bool catcls_Enable;
extern int catcls_compile_catalog_classes (THREAD_ENTRY * thread_p);
#endif /* SA_MODE */

#define BOOT_FORMAT_MAX_LENGTH 500

/* for optional capability check */
#define BOOT_NO_OPT_CAP                 0
#define BOOT_CHECK_HA_DELAY_CAP         NET_CAP_HA_REPL_DELAY

static BOOT_SERVER_CREDENTIAL boot_Server_credential = {
  /* db_full_name */ NULL, /* host_name */ NULL, /* lob_path */ NULL,
  /* process_id */ -1,
  /* root_class_oid */ {NULL_PAGEID, NULL_SLOTID, NULL_VOLID},
  /* root_class_hfid */ {{NULL_FILEID, NULL_VOLID}, NULL_PAGEID},
  /* data page_size */ -1, /* log page_size */ -1,
  /* disk_compatibility */ 0.0,
  /* ha_server_state */ HA_SERVER_STATE_NA,
  /* server_session_key */ {(char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF,
			    (char) 0xFF},
  INTL_CODESET_NONE,
  NULL
};

static const char *boot_Client_no_user_string = "(nouser)";
static const char *boot_Client_id_unknown_string = "(unknown)";

static char boot_Client_id_buffer[L_cuserid + 1];
static char boot_Db_path_buf[PATH_MAX];
static char boot_Log_path_buf[PATH_MAX];
static char boot_Lob_path_buf[PATH_MAX];
static char boot_Db_host_buf[CUB_MAXHOSTNAMELEN + 1];

/* Volume assigned for new files/objects (e.g., heap files) */
VOLID boot_User_volid = 0;	/* todo: boot_User_volid looks deprecated */
#if defined(CS_MODE)
/* Server host connected */
char boot_Host_connected[CUB_MAXHOSTNAMELEN] = "";
#endif /* CS_MODE */
char boot_Host_name[CUB_MAXHOSTNAMELEN] = "";
char boot_Ip_address[16] = { 0 };

static char boot_Volume_label[PATH_MAX] = " ";
static bool boot_Is_client_all_final = true;
static bool boot_Set_client_at_exit = false;
static int boot_Process_id = -1;

static int boot_client (int tran_index, int lock_wait, TRAN_ISOLATION tran_isolation);
static void boot_shutdown_client_at_exit (void);
#if defined(CS_MODE)
static int boot_client_initialize_css (DB_INFO * db, int client_type, bool check_capabilities, int opt_cap,
				       bool discriminative, int connect_order, bool is_preferred_host);
#endif /* CS_MODE */
#if defined(CS_MODE)
static int boot_check_locales (BOOT_CLIENT_CREDENTIAL * client_credential);
#endif /* CS_MODE */
#if defined(CS_MODE)
static int boot_check_timezone_checksum (BOOT_CLIENT_CREDENTIAL * client_credential);
#endif
static int boot_client_find_and_cache_class_oids (void);

/*
 * boot_client () -
 *
 * return :
 *
 *   tran_index(in) : transaction index
 *   lock_wait(in) :
 *   tran_isolation(in):
 *
 * Note: macros that find if the cubrid client is restarted
 */
static int
boot_client (int tran_index, int lock_wait, TRAN_ISOLATION tran_isolation)
{
  tran_cache_tran_settings (tran_index, lock_wait, tran_isolation);

  if (boot_Set_client_at_exit)
    {
      return NO_ERROR;
    }

  boot_Set_client_at_exit = true;
  boot_Process_id = getpid ();
  atexit (boot_shutdown_client_at_exit);

  return NO_ERROR;
}

/*
 * boot_initialize_client () -
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   client_credential(in): Contains database access information such as :
 *                          database name, user name and password, client type
 *   db_path_info(in) : Directory where the database is created. It allows you
 *                      to specify the exact pathname of a directory in which
  *                     to create the new database.
 *   db_overwrite(in) : Wheater to overwrite the database if it already exist.
 *   file_addmore_vols(in): More volumes are created during the initialization
 *                      process.
 *   npages(in)       : Total number of pages to allocate for the database.
 *   db_desired_pagesize(in): Desired pagesize for the new database.
 *                      The given size must be power of 2 and greater or
 *                      equal than 512.
 *   log_npages(in)   : Number of log pages. If log_npages <=0, default value
 *                      of system parameter is used.
 *   db_desired_log_page_size(in):
 *   lang_charset(in): language and charset to set on DB
 *
 * Note:
 *              The first step of any CUBRID application is to initialize a
 *              database. A database is composed of data volumes (or Unix file
 *              system files), database backup files, and log files. A data
 *              volume contains information on attributes, classes, indexes,
 *              and objects created in the database. A database backup is a
 *              fuzzy snapshot of the entire database. The backup is fuzzy
 *              since it can be taken online when other transactions are
 *              updating the database. The logs contain records that reflect
 *              changes to the database. The log and backup files are used by
 *              the system to recover committed and uncommitted transactions
 *              in the event of system and media crashes. Logs are also used
 *              to support user-initiated rollbacks. This function also
 *              initializes the database with built-in CUBRID classes.
 *
 *              The rest of this function is identical to the restart. The
 *              transaction for the current client session is automatically
 *              started.
 */
int
boot_initialize_client (BOOT_CLIENT_CREDENTIAL * client_credential, BOOT_DB_PATH_INFO * db_path_info, bool db_overwrite,
			const char *file_addmore_vols, DKNPAGES npages, PGLENGTH db_desired_pagesize,
			DKNPAGES log_npages, PGLENGTH db_desired_log_page_size, const char *lang_charset)
{
  OID rootclass_oid;		/* Oid of root class */
  HFID rootclass_hfid;		/* Heap for classes */
  int tran_index;		/* Assigned transaction index */
  TRAN_ISOLATION tran_isolation;	/* Desired client Isolation level */
  int tran_lock_wait_msecs;	/* Default lock waiting */
  unsigned int length;
  int error_code = NO_ERROR;
  DB_INFO *db = NULL;
#if !defined(WINDOWS)
  bool dl_initialized = false;
#endif /* !WINDOWS */
  const char *hosts[2];
#if defined (CS_MODE)
  char format[BOOT_FORMAT_MAX_LENGTH];
#endif

  assert (client_credential != NULL);
  assert (db_path_info != NULL);

  /* If the client is restarted, shutdown the client */
  if (BOOT_IS_CLIENT_RESTARTED ())
    {
      (void) boot_shutdown_client (true);
    }

  if (!boot_Is_client_all_final)
    {
      boot_client_all_finalize (true);
    }

#if defined(WINDOWS)
  /* set up the WINDOWS stream emulations */
  pc_init ();
#endif /* WINDOWS */

  /*
   * initialize language parameters  */
  if (lang_init () != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOC_INIT, 1, "Failed to initialize language module");
	}
      error_code = ER_LOC_INIT;
      goto error_exit;
    }

  if (lang_set_charset_lang (lang_charset) != NO_ERROR)
    {
      error_code = ER_LOC_INIT;
      goto error_exit;
    }

  /* database name must be specified */
  if (client_credential->db_name.empty ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1, "(null)");
      error_code = ER_BO_UNKNOWN_DATABASE;
      goto error_exit;
    }

  /* open the system message catalog, before prm_ ? */
  if (msgcat_init () != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG, 0);
      error_code = ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG;
      goto error_exit;
    }

  /* initialize system parameters */
  if (sysprm_load_and_init_client (client_credential->get_db_name (), NULL) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANT_LOAD_SYSPRM, 0);
      error_code = ER_BO_CANT_LOAD_SYSPRM;
      goto error_exit;
    }

  /* initialize the "areas" memory manager */
  area_init ();
  locator_initialize_areas ();

  (void) db_set_page_size (db_desired_pagesize, db_desired_log_page_size);

  /* If db_path and/or log_path are NULL find the defaults */

  if (db_path_info->db_path == NULL)
    {
      db_path_info->db_path = getcwd (boot_Db_path_buf, PATH_MAX);
      if (db_path_info->db_path == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CWD_FAIL, 0);
	  error_code = ER_BO_CWD_FAIL;
	  goto error_exit;
	}
    }
  if (db_path_info->log_path == NULL)
    {
      /* assign the data volume directory */
      strcpy (boot_Log_path_buf, db_path_info->db_path);
      db_path_info->log_path = boot_Log_path_buf;
    }
  if (db_path_info->lob_path == NULL)
    {
      /* assign the data volume directory */
      snprintf (boot_Lob_path_buf, sizeof (boot_Lob_path_buf), "%s%s%clob", LOB_PATH_DEFAULT_PREFIX,
		db_path_info->db_path, PATH_SEPARATOR);
      db_path_info->lob_path = boot_Lob_path_buf;
    }
  else
    {
      ES_TYPE es_type = es_get_type (db_path_info->lob_path);

      switch (es_type)
	{
	case ES_NONE:
	  /* prepend default prefix */
	  snprintf (boot_Lob_path_buf, sizeof (boot_Lob_path_buf), "%s%s", LOB_PATH_DEFAULT_PREFIX,
		    db_path_info->lob_path);
	  db_path_info->lob_path = boot_Lob_path_buf;
	  break;
#if !defined (CUBRID_OWFS)
	case ES_OWFS:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, db_path_info->lob_path);
	  error_code = ER_ES_INVALID_PATH;
	  goto error_exit;
#endif /* !CUBRID_OWFS */
	default:
	  break;
	}
    }

  /* make sure that the full path for the database is not too long */
  length = (unsigned int) (client_credential->db_name.length () + strlen (db_path_info->db_path) + 2);
  if (length > (unsigned) PATH_MAX)
    {
      /* db_path + db_name is too long */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG, 3, db_path_info->db_path,
	      client_credential->get_db_name (), length, PATH_MAX);

      error_code = ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG;
      goto error_exit;
    }

  /* If a host was not given, assume the current host */
  if (db_path_info->db_host == NULL)
    {
      strcpy (boot_Db_host_buf, "localhost");
      db_path_info->db_host = boot_Db_host_buf;
    }

  /* make new DB_INFO */
  hosts[0] = db_path_info->db_host;
  hosts[1] = NULL;
  db =
    cfg_new_db (client_credential->get_db_name (), db_path_info->db_path, db_path_info->log_path,
		db_path_info->lob_path, hosts);
  if (db == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1, client_credential->get_db_name ());
      error_code = ER_BO_UNKNOWN_DATABASE;
      goto error_exit;
    }

  /* Get the absolute path name */
  COMPOSE_FULL_NAME (boot_Volume_label, sizeof (boot_Volume_label), db_path_info->db_path,
		     client_credential->get_db_name ());

  er_clear ();

  /* Get the user name */
  if (client_credential->db_user.empty ())
    {
      char *user_name = strdup (Au_user_name);
      int upper_case_name_size;
      char *upper_case_name;

      if (user_name != NULL)
	{
	  upper_case_name_size = intl_identifier_upper_string_size (user_name);
	  upper_case_name = (char *) malloc (upper_case_name_size + 1);
	  if (upper_case_name == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      (size_t) (upper_case_name_size + 1));
	    }
	  else
	    {
	      intl_identifier_upper (user_name, upper_case_name);
	      client_credential->db_user = upper_case_name;
	    }
	  free_and_init (user_name);
	}
      upper_case_name = NULL;

      if (client_credential->db_user.empty ())
	{
	  client_credential->db_user = boot_Client_no_user_string;
	}
    }
  /* Get the login name, host, and process identifier */
  if (client_credential->login_name.empty ())
    {
      if (getuserid (boot_Client_id_buffer, L_cuserid) != (char *) NULL)
	{
	  client_credential->login_name = boot_Client_id_buffer;
	}
      else
	{
	  client_credential->login_name = boot_Client_id_unknown_string;
	}
    }

  if (client_credential->host_name.empty ())
    {
      client_credential->host_name = boot_get_host_name ();
    }

  /*
   * Initialize the dynamic loader. Don't care about failures. If dynamic
   * loader fails, methods will fail when they are invoked
   */
#if !defined(WINDOWS)
#if !defined (SOLARIS) && !defined(LINUX) && !defined(AIX)
  (void) dl_initiate_module (client_credential->get_program_name ());
#else /* !SOLARIS && !LINUX && !AIX */
  (void) dl_initiate_module ();
#endif /* !SOLARIS && !LINUX && !AIX */
  dl_initialized = true;
#endif /* !WINDOWS */

#if defined(CS_MODE)
  /* Initialize the communication subsystem */
  error_code =
    boot_client_initialize_css (db, client_credential->client_type, false, BOOT_NO_OPT_CAP, false,
				DB_CONNECT_ORDER_SEQ, false);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }
#endif /* CS_MODE */
  boot_User_volid = 0;
  tran_isolation = (TRAN_ISOLATION) prm_get_integer_value (PRM_ID_LOG_ISOLATION_LEVEL);
  tran_lock_wait_msecs = prm_get_integer_value (PRM_ID_LK_TIMEOUT_SECS);

  /* this must be done before the init_server because recovery steps may need domains. */
  error_code = tp_init ();
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  /* Initialize tsc-timer */
  tsc_init ();

  if (tran_lock_wait_msecs > 0)
    {
      tran_lock_wait_msecs = tran_lock_wait_msecs * 1000;
    }

  error_code = perfmon_initialize (MAX_NTRANS);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error_exit;
    }

  /* Initialize the disk and the server part */
  tran_index =
    boot_initialize_server (client_credential, db_path_info, db_overwrite, file_addmore_vols, npages,
			    db_desired_pagesize, log_npages, db_desired_log_page_size, &rootclass_oid, &rootclass_hfid,
			    tran_lock_wait_msecs, tran_isolation);

  if (tran_index == NULL_TRAN_INDEX)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      if (error_code == NO_ERROR)
	{
	  error_code = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	}
      goto error_exit;
    }

  // create session
  (void) db_find_or_create_session (client_credential->get_db_user (), client_credential->get_program_name ());

  oid_set_root (&rootclass_oid);
  OID_INIT_TEMPID ();

  error_code = ws_init ();

  if (error_code == NO_ERROR)
    {
      /* Create system classes such as the root and authorization classes */

      sm_create_root (&rootclass_oid, &rootclass_hfid);
      au_init ();

      /* Create authorization classes and enable authorization */
      error_code = au_install ();
      if (error_code == NO_ERROR)
	{
	  error_code = au_start ();
	}
      if (error_code == NO_ERROR)
	{
	  tr_init ();
	  error_code = tr_install ();
	  if (error_code == NO_ERROR)
	    {
	      catcls_init ();
	      error_code = catcls_install ();
	      if (error_code == NO_ERROR)
		{
		  /*
		   * mark all classes created during the initialization as "system"
		   * classes,
		   */
		  sm_mark_system_classes ();
		  error_code = tran_commit (false);
		}
	    }
	}
    }

  if (error_code != NO_ERROR)
    {
      (void) boot_shutdown_client (false);
    }
  else
    {
      boot_client (tran_index, tran_lock_wait_msecs, tran_isolation);
#if defined (CS_MODE)
      /* print version string */
      strncpy_bufsize (format, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_GENERAL,
					       MSGCAT_GENERAL_DATABASE_INIT));
      (void) fprintf (stdout, format, rel_name ());
#endif /* CS_MODE */
    }

  if (db != NULL)
    {
      cfg_free_directory (db);
      db = NULL;
    }
  return error_code;

error_exit:
  if (db != NULL)
    {
      cfg_free_directory (db);
      db = NULL;
    }

  if (BOOT_IS_CLIENT_RESTARTED ())
    {
      er_log_debug (ARG_FILE_LINE, "boot_initialize_client: unregister client { tran %d }\n", tm_Tran_index);
      boot_shutdown_client (false);
    }
  else
    {
      if (boot_Server_credential.db_full_name)
	{
	  db_private_free_and_init (NULL, boot_Server_credential.db_full_name);
	}
      if (boot_Server_credential.host_name)
	{
	  db_private_free_and_init (NULL, boot_Server_credential.host_name);
	}

      showstmt_metadata_final ();
      tran_free_savepoint_list ();
      set_final ();
      tr_final ();
      au_final ();
      sm_final ();
      ws_final ();
      es_final ();
      tp_final ();

#if !defined(WINDOWS)
      if (dl_initialized == true)
	{
	  (void) dl_destroy_module ();
	  dl_initialized = false;
	}
#endif /* !WINDOWS */

      locator_free_areas ();
      sysprm_final ();
      area_final ();

      lang_final ();
      tz_unload ();
      perfmon_finalize ();

#if defined(WINDOWS)
      pc_final ();
#endif /* WINDOWS */

      memset (&boot_Server_credential, 0, sizeof (boot_Server_credential));
      memset (boot_Server_credential.server_session_key, 0xFF, SERVER_SESSION_KEY_SIZE);
    }

  return error_code;
}

/*
 * boot_restart_client () - restart client
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   client_credential(in) : Information required to start as client, such as:
 *                           database name, user name and password, client
 *                           type.
 *
 * Note:
 *              An application must restart the database system with the
 *              desired database (the database must have already been created)
 *              before the application start invoking the CUBRID functional
 *              interface. This function restarts the CUBRID client. It also
 *              initializes all client modules for the execution of the client
 *              interface. A transaction for the current client session is
 *              automatically started.
 *
 *              It is very important that the application check for success
 *              of this function before calling any other CUBRID function.
 */

int
boot_restart_client (BOOT_CLIENT_CREDENTIAL * client_credential)
{
  int tran_index;
  TRAN_ISOLATION tran_isolation;
  int tran_lock_wait_msecs;
  TRAN_STATE transtate;
  int error_code = NO_ERROR;
  DB_INFO *db = NULL;
#if !defined(WINDOWS)
  bool dl_initialized = false;
#endif /* !WINDOWS */
  char *ptr;
#if defined(CS_MODE)
  const char *hosts[2];

  char **ha_hosts;
  int num_hosts;
  int i, optional_cap;
  char *ha_node_list = NULL;
  bool check_capabilities;
  bool skip_preferred_hosts = false;
  bool skip_db_info = false;
#endif /* CS_MODE */

  assert (client_credential != NULL);

  /* If the client is restarted, shutdown the client */
  if (BOOT_IS_CLIENT_RESTARTED ())
    {
      (void) boot_shutdown_client (true);
    }

  if (!boot_Is_client_all_final)
    {
      boot_client_all_finalize (true);
    }

#if defined(WINDOWS)
  /* set up the WINDOWS stream emulations */
  pc_init ();
#endif /* WINDOWS */

  /* initialize language parameters */
  if (lang_init () != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOC_INIT, 1, "Failed to initialize language module");
	}
      return ER_LOC_INIT;
    }

  /* initialize time zone data - optional module */
  if (tz_load () != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_LOAD_ERROR, 1, "Failed to initialize timezone module");
	}
      error_code = ER_TZ_LOAD_ERROR;
      goto error;
    }

  /* database name must be specified */
  if (client_credential->get_db_name () == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1, "(null)");
      error_code = ER_BO_UNKNOWN_DATABASE;
      goto error;
    }

  /* open the system message catalog, before prm_ ? */
  if (msgcat_init () != NO_ERROR)
    {
      error_code = ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error;
    }

  /* initialize system parameters */
  if (sysprm_load_and_init_client (client_credential->get_db_name (), NULL) != NO_ERROR)
    {
      error_code = ER_BO_CANT_LOAD_SYSPRM;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error;
    }

  // reload with update file name
  if (er_init (prm_get_string_value (PRM_ID_ER_LOG_FILE), prm_get_integer_value (PRM_ID_ER_EXIT_ASK)) != NO_ERROR)
    {
      assert_release (false);
      goto error;
    }

  pr_Enable_string_compression = prm_get_bool_value (PRM_ID_ENABLE_STRING_COMPRESSION);

  /* initialize the "areas" memory manager, requires prm_ */
  area_init ();
  locator_initialize_areas ();

  error_code = perfmon_initialize (1);	/* 1 transaction for SA_MODE */
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }

  ptr = (char *) strstr (client_credential->get_db_name (), "@");
  if (ptr == NULL)
    {
      /* Find the location of the database and the log from the database.txt */
      db = cfg_find_db (client_credential->get_db_name ());
#if defined(CS_MODE)
      if (db == NULL)
	{
	  /* if not found, use secondary host lists */
	  db = cfg_new_db (client_credential->get_db_name (), NULL, NULL, NULL, NULL);
	}

      if (db == NULL
	  || (db->num_hosts > 1
	      && (BOOT_ADMIN_CLIENT_TYPE (client_credential->client_type)
		  || BOOT_LOG_REPLICATOR_TYPE (client_credential->client_type)
		  || BOOT_CSQL_CLIENT_TYPE (client_credential->client_type))))
	{
	  error_code = ER_NET_NO_EXPLICIT_SERVER_HOST;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	  goto error;
	}
#endif /* CS_MODE */
    }
  else
    {
      /* db_name@host_name */
#if defined(CS_MODE)
      *ptr = '\0';		/* screen 'db@host' */
      if (BOOT_BROKER_AND_DEFAULT_CLIENT_TYPE (client_credential->client_type))
	{
	  ha_node_list = ptr + 1;
	  ha_hosts = cfg_get_hosts (ha_node_list, &num_hosts, false);

	  db = cfg_new_db (client_credential->get_db_name (), NULL, NULL, NULL, (const char **) ha_hosts);

	  if (ha_hosts)
	    {
	      cfg_free_hosts (ha_hosts);
	    }
	}
      else
	{
	  hosts[0] = ptr + 1;
	  hosts[1] = NULL;

	  db = cfg_new_db (client_credential->get_db_name (), NULL, NULL, NULL, hosts);
	}
      *ptr = (char) '@';
#else /* CS_MODE */
      error_code = ER_NOT_IN_STANDALONE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, client_credential->get_db_name ());
      goto error;
#endif /* !CS_MODE */
    }

  if (db == NULL)
    {
      error_code = ER_BO_UNKNOWN_DATABASE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, client_credential->get_db_name ());
      goto error;
    }

  er_clear ();

  /* Get the user name */
  if (client_credential->db_user.empty ())
    {
      if (au_has_user_name ())
	{
	  const char *name = au_get_current_user_name ();	// while establishing a connection, never use db_get_user_name.
	  if (name != NULL)
	    {
	      client_credential->db_user = name;
	      ws_free_string (name);
	    }
	}
      else
	{
	  // default is PUBLIC
	  client_credential->db_user = AU_PUBLIC_USER_NAME;
	}
    }
  /* Get the login name, host, and process identifier */
  if (client_credential->login_name.empty ())
    {
      if (getuserid (boot_Client_id_buffer, L_cuserid) != (char *) NULL)
	{
	  client_credential->login_name = boot_Client_id_buffer;
	}
      else
	{
	  client_credential->login_name = boot_Client_id_unknown_string;
	}
    }
  if (client_credential->host_name.empty ())
    {
      client_credential->host_name = boot_get_host_name ();
    }

  client_credential->process_id = getpid ();

  if (client_credential->client_ip_addr.empty ())
    {
      client_credential->client_ip_addr = boot_get_ip ();
    }

  /*
   * Initialize the dynamic loader. Don't care about failures. If dynamic
   * loader fails, methods will fail when they are invoked
   */
#if !defined(WINDOWS)
#if !defined (SOLARIS) && !defined(LINUX) && !defined(AIX)
  (void) dl_initiate_module (client_credential->get_program_name ());
#else /* !SOLARIS && !LINUX && !AIX */
  (void) dl_initiate_module ();
#endif /* !SOLARIS && !LINUX && !AIX */
  dl_initialized = true;
#endif /* !WINDOWS */

  /* read only mode? */
  if (prm_get_bool_value (PRM_ID_READ_ONLY_MODE) || BOOT_READ_ONLY_CLIENT_TYPE (client_credential->client_type))
    {
      db_disable_modification ();
    }

#if defined(CS_MODE)
  /* Initialize the communication subsystem */
  db_clear_host_status ();

  for (i = 0; i < 2; i++)
    {
      if (BOOT_IS_PREFERRED_HOSTS_SET (client_credential) && skip_preferred_hosts == false)
	{
	  char **hosts;
	  DB_INFO *tmp_db;

	  check_capabilities = true;

	  if (i == 0)		/* first */
	    {
	      optional_cap = BOOT_CHECK_HA_DELAY_CAP;
	    }
	  else			/* second */
	    {
	      if (!BOOT_REPLICA_ONLY_BROKER_CLIENT_TYPE (client_credential->client_type)
		  && BOOT_NORMAL_CLIENT_TYPE (client_credential->client_type))
		{
		  check_capabilities = false;
		}

	      optional_cap = BOOT_NO_OPT_CAP;
	    }

	  hosts = util_split_string (client_credential->preferred_hosts, ":");
	  if (hosts == NULL)
	    {
	      error_code = ER_GENERIC_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	      goto error;
	    }

	  tmp_db = cfg_new_db (db->name, NULL, NULL, NULL, (const char **) hosts);
	  if (tmp_db == NULL)
	    {
	      util_free_string_array (hosts);
	      error_code = ER_BO_UNKNOWN_DATABASE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1, db->name);
	      goto error;
	    }

	  boot_Host_connected[0] = '\0';

	  /* connect to preferred hosts in a sequential order even though a user sets CONNECT_ORDER to RANDOM */
	  error_code =
	    boot_client_initialize_css (tmp_db, client_credential->client_type, check_capabilities,
					optional_cap, false, DB_CONNECT_ORDER_SEQ, true);

	  if (error_code != NO_ERROR)
	    {
	      if (error_code == ER_NET_SERVER_HAND_SHAKE)
		{
		  er_log_debug (ARG_FILE_LINE,
				"boot_restart_client: boot_client_initialize_css () ER_NET_SERVER_HAND_SHAKE\n");

		  boot_Host_connected[0] = '\0';
		}
	      else
		{
		  skip_preferred_hosts = true;
		}
	    }

	  util_free_string_array (hosts);
	  cfg_free_directory (tmp_db);
	}

      if (skip_db_info == true)
	{
	  continue;
	}

      if (BOOT_IS_PREFERRED_HOSTS_SET (client_credential) && error_code == NO_ERROR)
	{
	  /* connected to any preferred hosts successfully */
	  break;
	}
      else if (BOOT_REPLICA_ONLY_BROKER_CLIENT_TYPE (client_credential->client_type)
	       || client_credential->client_type == DB_CLIENT_TYPE_SLAVE_ONLY_BROKER)

	{
	  check_capabilities = true;
	  if (i == 0)		/* first */
	    {
	      optional_cap = BOOT_CHECK_HA_DELAY_CAP;
	    }
	  else			/* second */
	    {
	      optional_cap = BOOT_NO_OPT_CAP;
	    }

	  error_code =
	    boot_client_initialize_css (db, client_credential->client_type, check_capabilities,
					optional_cap, false, client_credential->connect_order, false);
	}
      else if (BOOT_CSQL_CLIENT_TYPE (client_credential->client_type))
	{
	  assert (!BOOT_IS_PREFERRED_HOSTS_SET (client_credential));

	  check_capabilities = false;
	  optional_cap = BOOT_NO_OPT_CAP;

	  error_code =
	    boot_client_initialize_css (db, client_credential->client_type, check_capabilities,
					optional_cap, false, DB_CONNECT_ORDER_SEQ, false);
	  break;		/* dont retry */
	}
      else if (BOOT_NORMAL_CLIENT_TYPE (client_credential->client_type))
	{
	  if (i == 0)		/* first */
	    {
	      check_capabilities = true;
	      optional_cap = BOOT_CHECK_HA_DELAY_CAP;
	    }
	  else			/* second */
	    {
	      check_capabilities = false;
	      optional_cap = BOOT_NO_OPT_CAP;
	    }

	  error_code =
	    boot_client_initialize_css (db, client_credential->client_type, check_capabilities,
					optional_cap, false, client_credential->connect_order, false);

	}
      else
	{
	  assert (!BOOT_IS_PREFERRED_HOSTS_SET (client_credential));

	  check_capabilities = false;
	  optional_cap = BOOT_NO_OPT_CAP;
	  error_code =
	    boot_client_initialize_css (db, client_credential->client_type, check_capabilities,
					optional_cap, false, client_credential->connect_order, false);
	  break;		/* dont retry */
	}

      if (error_code == NO_ERROR)
	{
	  if (BOOT_IS_PREFERRED_HOSTS_SET (client_credential))
	    {
	      db_set_host_status (boot_Host_connected, DB_HS_NON_PREFFERED_HOSTS);
	    }
	  break;
	}
      else if (error_code == ER_NET_SERVER_HAND_SHAKE)
	{
	  er_log_debug (ARG_FILE_LINE, "boot_restart_client: boot_client_initialize_css () ER_NET_SERVER_HAND_SHAKE\n");
	}
      else
	{
	  skip_db_info = true;
	}
    }

  if (error_code != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "boot_restart_client: boot_client_initialize_css () error %d\n", error_code);
      goto error;
    }

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_BO_CONNECTED_TO, 5,
	  client_credential->get_program_name (), client_credential->process_id,
	  client_credential->get_db_name (), boot_Host_connected, prm_get_integer_value (PRM_ID_TCP_PORT_ID));

  /* tune some client parameters with the value from the server */
  sysprm_tune_client_parameters ();
#else /* CS_MODE */
#if defined(WINDOWS)
  css_windows_startup ();
#endif /* WINDOWS */
#endif /* !CS_MODE */

  /* Free the information about the database */
  cfg_free_directory (db);
  db = NULL;

  /* this must be done before the register_client because recovery steps may need domains. */
  error_code = tp_init ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* Initialize tsc-timer */
  tsc_init ();

  error_code = ws_init ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /*
   * At this moment, we should use the default isolation level and wait
   * timeout, since the client fetches objects during the restart process.
   * This values are reset at a later point, once the client has been fully
   * restarted.
   */

  tran_isolation = TRAN_DEFAULT_ISOLATION_LEVEL ();

  tran_lock_wait_msecs = TRAN_LOCK_INFINITE_WAIT;

  er_log_debug (ARG_FILE_LINE,
		"boot_restart_client: register client { type %d db %s user %s password %s "
		"program %s login %s host %s pid %d }\n", client_credential->client_type,
		client_credential->get_db_name (), client_credential->get_db_user (),
		client_credential->db_password.empty ()? "(null)" : client_credential->get_db_password (),
		client_credential->get_program_name (),
		client_credential->get_login_name (), client_credential->get_host_name (),
		client_credential->process_id);

  tran_index =
    boot_register_client (client_credential, tran_lock_wait_msecs, tran_isolation, &transtate, &boot_Server_credential);

  if (tran_index == NULL_TRAN_INDEX)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      goto error;
    }

#if defined(CS_MODE)
  if (lang_set_charset ((INTL_CODESET) boot_Server_credential.db_charset) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      goto error;
    }
  if (lang_set_language (boot_Server_credential.db_lang) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      goto error;
    }

  /* Reset the pagesize according to server.. */
  if (db_set_page_size (boot_Server_credential.page_size, boot_Server_credential.log_page_size) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      goto error;
    }

  /* Reset the disk_level according to server.. */
  if (rel_disk_compatible () != boot_Server_credential.disk_compatibility)
    {
      rel_set_disk_compatible (boot_Server_credential.disk_compatibility);
    }
#endif /* CS_MODE */
  if (sysprm_init_intl_param () != NO_ERROR)
    {
      error_code = er_errid ();
      goto error;
    }

  /* Initialize client modules for execution */
  boot_client (tran_index, tran_lock_wait_msecs, tran_isolation);

  oid_set_root (&boot_Server_credential.root_class_oid);
  OID_INIT_TEMPID ();

  sm_init (&boot_Server_credential.root_class_oid, &boot_Server_credential.root_class_hfid);
  au_init ();			/* initialize authorization globals */

  /* start authorization and make sure the logged in user has access */
  error_code = au_start ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = boot_client_find_and_cache_class_oids ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  (void) db_find_or_create_session (client_credential->get_db_user (), client_credential->get_program_name ());

#if defined(CS_MODE)
  error_code = boot_check_locales (client_credential);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  error_code = boot_check_timezone_checksum (client_credential);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
#endif /* CS_MODE */

  tr_init ();			/* initialize trigger manager */

  /* TODO: how about to call es_init() only for normal client? */
  if (boot_Server_credential.lob_path[0] != '\0')
    {
      error_code = es_init (boot_Server_credential.lob_path);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_ES_NO_LOB_PATH, 0);
    }
  /* Does not care if was committed/aborted .. */
  (void) tran_commit (false);

  /*
   * If there is a need to change the isolation level and the lock wait,
   * do it at this moment
   */

  tran_isolation = (TRAN_ISOLATION) prm_get_integer_value (PRM_ID_LOG_ISOLATION_LEVEL);
  tran_lock_wait_msecs = prm_get_integer_value (PRM_ID_LK_TIMEOUT_SECS);
  if (tran_isolation != TRAN_DEFAULT_ISOLATION_LEVEL ())
    {
      error_code = tran_reset_isolation (tran_isolation, TM_TRAN_ASYNC_WS ());
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }
  if (tran_lock_wait_msecs >= 0)
    {
      (void) tran_reset_wait_times (tran_lock_wait_msecs * 1000);
    }

  error_code = showstmt_metadata_init ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  json_set_alloc_funcs (malloc, free);

  return error_code;

error:

  /* Protect against falsely returning NO_ERROR to caller */
  if (error_code == NO_ERROR)
    {
      error_code = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
    }

  if (db != NULL)
    {
      cfg_free_directory (db);
    }

  if (BOOT_IS_CLIENT_RESTARTED ())
    {
      er_log_debug (ARG_FILE_LINE, "boot_restart_client: unregister client { tran %d }\n", tm_Tran_index);
      boot_shutdown_client (false);
    }
  else
    {
      if (boot_Server_credential.db_full_name)
	{
	  db_private_free_and_init (NULL, boot_Server_credential.db_full_name);
	}
      if (boot_Server_credential.host_name)
	{
	  db_private_free_and_init (NULL, boot_Server_credential.host_name);
	}

      showstmt_metadata_final ();
      tran_free_savepoint_list ();
      set_final ();
      tr_final ();
      au_final ();
      sm_final ();
      ws_final ();
      es_final ();
      tp_final ();

#if !defined(WINDOWS)
      if (dl_initialized == true)
	{
	  (void) dl_destroy_module ();
	  dl_initialized = false;
	}
#endif /* !WINDOWS */

      locator_free_areas ();
      sysprm_final ();
      area_final ();

      lang_final ();
      tz_unload ();

#if defined(WINDOWS)
      pc_final ();
#endif /* WINDOWS */

      memset (&boot_Server_credential, 0, sizeof (boot_Server_credential));
      memset (boot_Server_credential.server_session_key, 0xFF, SERVER_SESSION_KEY_SIZE);
    }

  return error_code;
}

/*
 * boot_shutdown_client () - shutdown client
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   is_er_final(in) :
 *
 * Note:
 *              This function should be called before the CUBRID
 *              application is finished. This function will notify the
 *              recovery manager that the application has finished and will
 *              terminate all client modules (e.g., allocation of memory is
 *              deallocated).If there are active transactions, they are either
 *              committed or aborted according to the commit_on_shutdown
 *              system parameter.
 */

int
boot_shutdown_client (bool is_er_final)
{
  if (BOOT_IS_CLIENT_RESTARTED ())
    {
      /*
       * wait for other server request to finish.
       * if db_shutdown() is called by signal handler or atexit handler,
       * the server request may be running.
       */
      tran_wait_server_active_trans ();

      /*
       * Either Abort or commit the current transaction depending upon the value
       * of the commit_on_shutdown system parameter.
       */
      if (tran_is_active_and_has_updated ())
	{
	  if (prm_get_bool_value (PRM_ID_COMMIT_ON_SHUTDOWN) != false)
	    {
	      (void) tran_commit (false);
	    }
	  else
	    {
	      (void) tran_abort ();
	    }
	}

      /*
       * Make sure that we are still up. For example, if the server died, we do
       * not need to call the following stuff any longer.
       */

      if (BOOT_IS_CLIENT_RESTARTED ())
	{
	  (void) boot_unregister_client (tm_Tran_index);
#if defined(CS_MODE)
	  (void) net_client_final ();
#else /* CS_MODE */
#if defined(WINDOWS)
	  css_windows_shutdown ();
#endif /* WINDOWS */
#endif /* !CS_MODE */
	}

      boot_client_all_finalize (is_er_final);
    }

  return NO_ERROR;
}

/*
 * boot_shutdown_client_at_exit () - make sure that the client is shutdown at exit
 *
 * return : nothing
 *
 * Note:
 *       This function is called when the invoked program terminates
 *       normally. This function make sure that the client is shutdown
 *       in a nice way.
 */
static void
boot_shutdown_client_at_exit (void)
{
  if (BOOT_IS_CLIENT_RESTARTED () && boot_Process_id == getpid ())
    {
      /* Avoid infinite looping if someone calls exit during shutdown */
      boot_Process_id++;

      if (!er_is_initialized ())
	{
	  // we need error manager initialized
	  er_init (NULL, ER_NEVER_EXIT);
	}

      (void) boot_shutdown_client (true);
    }
}

/*
 * boot_donot_shutdown_client_at_exit: do not shutdown client at exist.
 *
 * return : nothing
 *
 * This function must be called when the system needs to exit
 *  without shutting down the client (e.g., in case of fatal
 *  failure).
 */
void
boot_donot_shutdown_client_at_exit (void)
{
  if (BOOT_IS_CLIENT_RESTARTED () && boot_Process_id == getpid ())
    {
      boot_Process_id++;
    }
}

/*
 * boot_server_die_or_reject: shutdown client when the server is dead
 *
 * return : nothing
 *
 * Note: The server has been terminated for circumstances beyond the client
 *       control. All active client transactions have been unilaterally
 *       aborted as a consequence of the termination of server.
 */
void
boot_server_die_or_changed (void)
{
  /*
   * If the client is restarted, abort the active transaction in the client and
   * terminate the client modules
   */
  if (BOOT_IS_CLIENT_RESTARTED ())
    {
      (void) tran_abort_only_client (true);
      boot_client (NULL_TRAN_INDEX, TM_TRAN_WAIT_MSECS (), TM_TRAN_ISOLATION ());
      boot_Is_client_all_final = false;
#if defined(CS_MODE)
      css_terminate (true);
#endif /* !CS_MODE */
      if (prm_get_bool_value (PRM_ID_TEST_MODE))
	{
	  er_print_callstack (ARG_FILE_LINE, "boot_server_die_or_changed() terminated\n");
	}
    }
}

/*
 * boot_client_all_finalize () - terminate every single client
 *
 * return : nothing
 *
 *   is_er_final(in): Terminate the error module..
 *
 *
 * Note: Terminate every single module of the client. This function is called
 *       during the shutdown of the client.
 */
void
boot_client_all_finalize (bool is_er_final)
{
  if (BOOT_IS_CLIENT_RESTARTED () || boot_Is_client_all_final == false)
    {
      if (boot_Server_credential.db_full_name)
	{
	  db_private_free_and_init (NULL, boot_Server_credential.db_full_name);
	}
      if (boot_Server_credential.host_name)
	{
	  db_private_free_and_init (NULL, boot_Server_credential.host_name);
	}
      if (boot_Server_credential.lob_path)
	{
	  db_private_free_and_init (NULL, boot_Server_credential.lob_path);
	}
      if (boot_Server_credential.db_lang)
	{
	  db_private_free_and_init (NULL, boot_Server_credential.db_lang);
	}

      showstmt_metadata_final ();
      tran_free_savepoint_list ();
      sm_flush_static_methods ();
      set_final ();
      parser_final ();
      tr_final ();
      au_final ();
      sm_final ();
      ws_final ();
      es_final ();
      tp_final ();

#if !defined(WINDOWS)
      (void) dl_destroy_module ();
#endif /* !WINDOWS */

      locator_free_areas ();
      sysprm_final ();
      perfmon_finalize ();
      area_final ();

      msgcat_final ();
      if (is_er_final)
	{
	  er_final (ER_ALL_FINAL);
	}
      lang_final ();
      tz_unload ();

      /* adj_arrays & lex buffers in the cnv formatting library. */
      cnv_cleanup ();

#if defined(WINDOWS)
      pc_final ();
#endif /* WINDOWS */

      /* Clean up stuff allocated by the utilities library too. Not really necessary but avoids warnings from memory
       * tracking tools that customers might be using. */
      co_final ();

      memset (&boot_Server_credential, 0, sizeof (boot_Server_credential));
      memset (boot_Server_credential.server_session_key, 0xFF, SERVER_SESSION_KEY_SIZE);

      boot_client (NULL_TRAN_INDEX, TRAN_LOCK_INFINITE_WAIT, TRAN_DEFAULT_ISOLATION_LEVEL ());
      boot_Is_client_all_final = true;
    }

}

#if defined(CS_MODE)
/*
 * boot_client_initialize_css () - Attempts to connect to hosts
 *                                          in list
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   db(in) : host information
 *   connect_order(in): whether to randomly or sequentially traverse host list
 *   opt_cap(in): optional capability
 *   discriminative(in): deprecated
 *
 * Note: This function will try an initialize the communications with the hosts
 *       in hostlist until success or the end of list is reached.
 */
static int
boot_client_initialize_css (DB_INFO * db, int client_type, bool check_capabilities, int opt_cap, bool discriminative,
			    int connect_order, bool is_preferred_host)
{
  int error = ER_NET_NO_SERVER_HOST;
  int hn, n;
  char *hostlist[MAX_NUM_DB_HOSTS];
  char strbuf[(CUB_MAXHOSTNAMELEN + 1) * MAX_NUM_DB_HOSTS];
  bool cap_error = false, boot_host_connected_exist = false;
  int max_num_delayed_hosts_lookup;

  assert (db != NULL);
  assert (db->num_hosts > 0);

  if (db->hosts == NULL)
    {
      db->hosts = cfg_get_hosts (NULL, &db->num_hosts, false);
      if (db->hosts == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
    }

  max_num_delayed_hosts_lookup = db_get_max_num_delayed_hosts_lookup ();
  if (is_preferred_host == false && max_num_delayed_hosts_lookup == 0 && (opt_cap & BOOT_CHECK_HA_DELAY_CAP))
    {
      /* if max_num_delayed_hosts_lookup is zero, move on to 2nd try */
      return ER_NET_SERVER_HAND_SHAKE;
    }

  memset (hostlist, 0, sizeof (hostlist));
  hn = 0;
  /* try the connected host first */
  if (boot_Host_connected[0] != '\0')
    {
      boot_host_connected_exist = true;
      hostlist[hn++] = boot_Host_connected;
    }
  for (n = 0; hn < MAX_NUM_DB_HOSTS && n < db->num_hosts; n++)
    {
      hostlist[hn++] = db->hosts[n];
    }

  if (connect_order == DB_CONNECT_ORDER_RANDOM)
    {
      if (boot_Host_connected[0] != '\0')
	{
	  /* leave boot_Host_connected at the front and shuffle the others */
	  util_shuffle_string_array (hostlist + 1, hn - 1);
	}
      else
	{
	  util_shuffle_string_array (hostlist, hn);
	}
    }

  db_clear_delayed_hosts_count ();

  for (n = 0; n < hn; n++)
    {
      if (css_check_server_alive_fn != NULL)
	{
	  if (css_check_server_alive_fn (db->name, hostlist[n]) == false)
	    {
	      er_log_debug (ARG_FILE_LINE, "skip '%s@%s'\n", db->name, hostlist[n]);
	      db_set_host_status (hostlist[n], DB_HS_UNUSABLE_DATABASES);
	      continue;
	    }
	}

      er_log_debug (ARG_FILE_LINE, "trying to connect '%s@%s'\n", db->name, hostlist[n]);
      error = net_client_init (db->name, hostlist[n]);
      if (error != NO_ERROR)
	{
	  if (error == ERR_CSS_TCP_CONNECT_TIMEDOUT)
	    {
	      db_set_host_status (hostlist[n], DB_HS_CONN_TIMEOUT | DB_HS_CONN_FAILURE);
	    }
	  else
	    {
	      db_set_host_status (hostlist[n], DB_HS_CONN_FAILURE);
	    }
	}
      else
	{
	  /* save the hostname for the use of calling functions */
	  if (boot_Host_connected != hostlist[n])
	    {
	      strncpy_bufsize (boot_Host_connected, hostlist[n]);
	    }
	  db_set_connected_host_status (hostlist[n]);

	  er_log_debug (ARG_FILE_LINE, "ping server with handshake\n");
	  /* ping to validate availability and to check compatibility */
	  er_clear ();
	  error = net_client_ping_server_with_handshake (client_type, check_capabilities, opt_cap);
	  if (error != NO_ERROR)
	    {
	      css_terminate (false);
	    }
	}

      /* connect error to the db at the host */
      switch (error)
	{
	case NO_ERROR:
	  return NO_ERROR;

	case ER_NET_SERVER_HAND_SHAKE:
	case ER_NET_HS_UNKNOWN_SERVER_REL:
	  cap_error = true;
	  /* FALLTHRU */
	case ER_NET_DIFFERENT_RELEASE:
	case ER_NET_NO_SERVER_HOST:
	case ER_NET_CANT_CONNECT_SERVER:
	case ER_NET_NO_MASTER:
	case ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER:
	case ERR_CSS_TCP_CONNECT_TIMEDOUT:
	case ERR_CSS_ERROR_FROM_SERVER:
	case ER_CSS_CLIENTS_EXCEEDED:
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_BO_CONNECT_FAILED, 2, db->name, hostlist[n]);
	  /* try to connect to next host */
	  er_log_debug (ARG_FILE_LINE, "error %d. try to connect to next host\n", error);
	  break;
	default:
	  /* ?? */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CONNECT_FAILED, 2, db->name, hostlist[n]);
	}

      if (error == ER_NET_SERVER_HAND_SHAKE && is_preferred_host == false && (opt_cap & BOOT_CHECK_HA_DELAY_CAP)
	  && max_num_delayed_hosts_lookup > 0)
	{
	  /* do not count delayed boot_Host_connected */
	  if (boot_host_connected_exist == true && n == 0)
	    {
	      db_clear_delayed_hosts_count ();
	    }

	  if (db_get_delayed_hosts_count () >= max_num_delayed_hosts_lookup)
	    {
	      hn = n + 1;
	      break;
	    }
	}
    }				/* for (tn) */

  /* failed to connect all hosts; write an error message */
  strbuf[0] = '\0';
  for (n = 0; n < hn - 1 && n < (MAX_NUM_DB_HOSTS - 1); n++)
    {
      strncat (strbuf, hostlist[n], CUB_MAXHOSTNAMELEN);
      strcat (strbuf, ":");
    }
  strncat (strbuf, hostlist[n], CUB_MAXHOSTNAMELEN);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CONNECT_FAILED, 2, db->name, strbuf);

  if (check_capabilities == true && cap_error == true)
    {
      /*
       * There'a a live host which has cause handshake error,
       * so adjust the return value
       */
      error = ER_NET_SERVER_HAND_SHAKE;
    }

  return (error);
}
#endif /* CS_MODE */

#if defined (SA_MODE)
#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * boot_build_catalog_classes :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   dbname(in) :
 */
int
boot_build_catalog_classes (const char *dbname)
{
  int error_code = NO_ERROR;

  /* check if an old version database */
  if (locator_find_class (CT_CLASS_NAME) != NULL)
    {
      fprintf (stdout, "Database %s already has system catalog class/vclass\n", dbname);
      return 1;
    }
  else
    {
      bool cc_save;

      /* save and catcls_Enable */
      cc_save = catcls_Enable;
      catcls_Enable = false;

      error_code = catcls_class_install ();
      if (error_code == NO_ERROR)
	{
	  error_code = catcls_vclass_install ();
	}
      if (error_code == NO_ERROR)
	{
	  /* add method to db_authorization */
	  au_add_method_check_authorization ();

	  /* mark catalog class/view as a system class */
	  sm_mark_system_class_for_catalog ();

	  if (!tf_Metaclass_class.n_variable)
	    {
	      tf_compile_meta_classes ();
	    }
	  if (catcls_Enable != true)
	    {
	      error_code = catcls_compile_catalog_classes (NULL);
	      if (error_code == NO_ERROR)
		{
		  error_code = sm_force_write_all_classes ();
		  if (error_code == NO_ERROR)
		    {
		      error_code = au_force_write_new_auth ();
		    }
		}
	    }
	}
      /* restore catcls_Enable */
      catcls_Enable = cc_save;
    }

  return error_code;
}

/*
 * boot_destroy_catalog_classes :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   dbname(in) :
 *
 * Note: destroy catalog by reverse order of building
 *
 */
int
boot_destroy_catalog_classes (void)
{
  int error_code = NO_ERROR;
  bool cc_save, save;

  int i;
  MOP classmop;
  const char *classes[] = {
    CT_CLASS_NAME,
    CT_ATTRIBUTE_NAME,
    CT_DOMAIN_NAME,
    CT_METHOD_NAME,
    CT_METHSIG_NAME,
    CT_METHARG_NAME,
    CT_METHFILE_NAME,
    CT_QUERYSPEC_NAME,
    CT_INDEX_NAME,
    CT_INDEXKEY_NAME,
    CT_CLASSAUTH_NAME,
    CT_DATATYPE_NAME,
    CT_PARTITION_NAME,
    CT_STORED_PROC_NAME,
    CT_STORED_PROC_ARGS_NAME,
    CTV_CLASS_NAME,
    CTV_SUPER_CLASS_NAME,
    CTV_VCLASS_NAME,
    CTV_ATTRIBUTE_NAME,
    CTV_ATTR_SD_NAME,
    CTV_METHOD_NAME,
    CTV_METHARG_NAME,
    CTV_METHARG_SD_NAME,
    CTV_METHFILE_NAME,
    CTV_INDEX_NAME,
    CTV_INDEXKEY_NAME,
    CTV_AUTH_NAME,
    CTV_TRIGGER_NAME,
    CTV_PARTITION_NAME,
    CTV_STORED_PROC_NAME,
    CTV_STORED_PROC_ARGS_NAME,
    CT_COLLATION_NAME,
    CT_DB_SERVER_NAME,
    CTV_DB_SERVER_NAME,
    CT_SYNONYM_NAME,
    CTV_SYNONYM_NAME,
    NULL
  };

  /* check if catalog exists */
  if (locator_find_class (CT_CLASS_NAME) == NULL)
    {
      /* catalog does not exists */
      return NO_ERROR;
    }

  /* save and off catcls_Enable */
  cc_save = catcls_Enable;
  catcls_Enable = false;

  AU_DISABLE (save);

  /* drop method of db_authorization */
  error_code = db_drop_class_method (locator_find_class ("db_authorization"), "check_authorization");
  /* error checking */
  if (error_code != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* drop catalog class/vclass */
  for (i = 0; classes[i] != NULL; i++)
    {
      classmop = locator_find_class (classes[i]);
      if (!classmop)
	{
	  continue;		/* not found */
	}
      /* for vclass, revoke before drop */
      if (db_is_vclass (classmop))
	{
	  error_code = db_revoke (Au_public_user, classmop, AU_SELECT);
	  if (error_code != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /* drop class/view */
      error_code = db_drop_class (classmop);
      if (error_code == ER_OBJ_INVALID_ARGUMENTS)
	{
	  continue;
	}

      /* error checking */
      if (error_code != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

exit_on_error:

  AU_ENABLE (save);

  /* restore catcls_Enable */
  catcls_Enable = cc_save;

  return error_code;
}

/*
 * boot_rebuild_catalog_classes :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   dbname(in) :
 */
int
boot_rebuild_catalog_classes (const char *dbname)
{
  int error_code = NO_ERROR;

  error_code = boot_destroy_catalog_classes ();

  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return boot_build_catalog_classes (dbname);
}
#endif /* ENABLE_UNUSED_FUNCTION */
#endif /* SA_MODE */

#if defined(CS_MODE)
char *
boot_get_host_connected (void)
{
  return boot_Host_connected;
}

#if defined (ENABLE_UNUSED_FUNCTION)
HA_SERVER_STATE
boot_get_ha_server_state (void)
{
  return boot_Server_credential.ha_server_state;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * boot_get_lob_path - return the lob path which is received from the server
 */
const char *
boot_get_lob_path (void)
{
  return boot_Server_credential.lob_path;
}
#endif /* CS_MODE */

/*
 * boot_clear_host_connected () -
 */
void
boot_clear_host_connected (void)
{
#if defined(CS_MODE)
  boot_Host_connected[0] = '\0';
#endif
}

char *
boot_get_host_name (void)
{
  if (boot_Host_name[0] == '\0')
    {
      if (GETHOSTNAME (boot_Host_name, CUB_MAXHOSTNAMELEN) != 0)
	{
	  strcpy (boot_Host_name, boot_Client_id_unknown_string);
	}
      boot_Host_name[CUB_MAXHOSTNAMELEN - 1] = '\0';	/* bullet proof */
    }

  return boot_Host_name;
}

char *
boot_get_ip (void)
{
  struct hostent *hp = NULL;
  if (boot_Host_name[0] == '\0')
    {
      boot_get_host_name ();
    }

  if ((hp = gethostbyname_uhost (boot_Host_name)) != NULL)
    {
      char *ip = inet_ntoa (*(struct in_addr *) *hp->h_addr_list);
      memcpy (boot_Ip_address, ip, 15);
    }

  return boot_Ip_address;
}

#if defined(CS_MODE)
/*
 * boot_check_locales () - checks that client locales are compatible with
 *                         server locales
 *
 *  return : error code
 *
 */
static int
boot_check_locales (BOOT_CLIENT_CREDENTIAL * client_credential)
{
  int error_code = NO_ERROR;
  LANG_COLL_COMPAT *server_collations = NULL;
  LANG_LOCALE_COMPAT *server_locales = NULL;
  int server_coll_cnt, server_locales_cnt;
  char cli_text[PATH_MAX];
  char srv_text[DB_MAX_IDENTIFIER_LENGTH + 10];

  error_code = boot_get_server_locales (&server_collations, &server_locales, &server_coll_cnt, &server_locales_cnt);
  if (error_code != NO_ERROR)
    {
      goto exit;
    }

  (void) basename_r (client_credential->get_program_name (), cli_text, sizeof (cli_text));
  snprintf (srv_text, sizeof (srv_text) - 1, "server '%s'", client_credential->get_db_name ());

  error_code = lang_check_coll_compat (server_collations, server_coll_cnt, cli_text, srv_text);
  if (error_code != NO_ERROR)
    {
      goto exit;
    }

  error_code = lang_check_locale_compat (server_locales, server_locales_cnt, cli_text, srv_text);

exit:
  if (server_collations != NULL)
    {
      free_and_init (server_collations);
    }
  if (server_locales != NULL)
    {
      free_and_init (server_locales);
    }

  return error_code;
}
#endif /* CS_MODE */

/*
 * boot_get_server_session_key () -
 */
char *
boot_get_server_session_key (void)
{
  return boot_Server_credential.server_session_key;
}

/*
 * boot_set_server_session_key () -
 */
void
boot_set_server_session_key (const char *key)
{
  memcpy (boot_Server_credential.server_session_key, key, SERVER_SESSION_KEY_SIZE);
}


#if defined(CS_MODE)
/*
 * boot_check_timezone_checksum () - checks that client timezone library is
 *	                             compatible with server timezone library
 *
 *  return : error code
 *
 */
static int
boot_check_timezone_checksum (BOOT_CLIENT_CREDENTIAL * client_credential)
{
  int error_code = NO_ERROR;
  char timezone_checksum[TZ_CHECKSUM_SIZE + 1];
  const TZ_DATA *tzd;
  char cli_text[PATH_MAX];
  char srv_text[DB_MAX_IDENTIFIER_LENGTH + 10];

  error_code = boot_get_server_timezone_checksum (timezone_checksum);
  if (error_code != NO_ERROR)
    {
      goto exit;
    }

  (void) basename_r (client_credential->get_program_name (), cli_text, sizeof (cli_text));
  snprintf (srv_text, sizeof (srv_text) - 1, "server '%s'", client_credential->get_db_name ());

  tzd = tz_get_data ();
  assert (tzd != NULL);
  error_code = check_timezone_compat (tzd->checksum, timezone_checksum, cli_text, srv_text);
exit:
  return error_code;
}
#endif /* CS_MODE */

/*
 * boot_client_find_and_cache_class_oids () - Cache class OID's on client for
 *					      fast class mop identifying.
 *
 * return    : Error code.
 */
static int
boot_client_find_and_cache_class_oids (void)
{
  MOP class_mop = NULL;
  int error;

  class_mop = sm_find_class (CT_SERIAL_NAME);
  if (class_mop == NULL)
    {
      error = er_errid ();
      if (error != NO_ERROR)
	{
	  return error;
	}
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }
  oid_set_cached_class_oid (OID_CACHE_SERIAL_CLASS_ID, &class_mop->oid_info.oid);

  class_mop = sm_find_class (CT_HA_APPLY_INFO_NAME);
  if (class_mop == NULL)
    {
      error = er_errid ();
      if (error != NO_ERROR)
	{
	  return error;
	}

      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }
  oid_set_cached_class_oid (OID_CACHE_HA_APPLY_INFO_CLASS_ID, &class_mop->oid_info.oid);
  return NO_ERROR;
}
