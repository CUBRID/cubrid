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
#include "work_space.h"
#include "schema_manager.h"
#include "authenticate.h"
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

typedef int (*DEF_FUNCTION) ();
typedef int (*DEF_CLASS_FUNCTION) (MOP);

typedef struct column COLUMN;
struct column
{
  const char *name;
  const char *type;
};


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
static int boot_define_class (MOP class_mop);
static int boot_define_attribute (MOP class_mop);
static int boot_define_domain (MOP class_mop);
static int boot_define_method (MOP class_mop);
static int boot_define_meth_sig (MOP class_mop);
static int boot_define_meth_argument (MOP class_mop);
static int boot_define_meth_file (MOP class_mop);
static int boot_define_query_spec (MOP class_mop);
static int boot_define_index (MOP class_mop);
static int boot_define_index_key (MOP class_mop);
static int boot_define_class_authorization (MOP class_mop);
static int boot_define_partition (MOP class_mop);
static int boot_add_data_type (MOP class_mop);
static int boot_define_data_type (MOP class_mop);
static int boot_define_stored_procedure (MOP class_mop);
static int boot_define_stored_procedure_arguments (MOP class_mop);
static int boot_define_serial (MOP class_mop);
static int boot_define_ha_apply_info (MOP class_mop);
static int boot_define_collations (MOP class_mop);
static int boot_add_charsets (MOP class_mop);
static int boot_define_charsets (MOP class_mop);
static int boot_define_dual (MOP class_mop);
static int boot_define_db_server (MOP class_mop);
static int boot_define_synonym (MOP class_mop);
static int boot_define_view_class (void);
static int boot_define_view_super_class (void);
static int boot_define_view_vclass (void);
static int boot_define_view_attribute (void);
static int boot_define_view_attribute_set_domain (void);
static int boot_define_view_method (void);
static int boot_define_view_method_argument (void);
static int boot_define_view_method_argument_set_domain (void);
static int boot_define_view_method_file (void);
static int boot_define_view_index (void);
static int boot_define_view_index_key (void);
static int boot_define_view_authorization (void);
static int boot_define_view_trigger (void);
static int boot_define_view_partition (void);
static int boot_define_view_stored_procedure (void);
static int boot_define_view_stored_procedure_arguments (void);
static int boot_define_view_db_collation (void);
static int boot_define_view_synonym (void);
static int catcls_class_install (void);
static int catcls_vclass_install (void);
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
#if 0				/* use Unix-domain socket for localhost */
      if (GETHOSTNAME (db_host_buf, CUB_MAXHOSTNAMELEN) != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNABLE_TO_FIND_HOSTNAME, 1, db_host_buf);
	  error_code = ER_BO_UNABLE_TO_FIND_HOSTNAME;
	  goto error_exit;
	}
      db_host_buf[CUB_MAXHOSTNAMELEN] = '\0';
#else
      strcpy (boot_Db_host_buf, "localhost");
#endif
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
      char *user_name = au_user_name_dup ();
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
	      error_code = catcls_class_install ();
	      if (error_code == NO_ERROR)
		{
		  error_code = catcls_vclass_install ();
		}
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
	  const char *name = au_user_name ();	// while establishing a connection, never use db_get_user_name.
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

/*
 * boot_define_class :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_class (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *index1_col_names[2] = { "unique_name", NULL };
  const char *index2_col_names[3] = { "class_name", "owner", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", "object", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* unique name */
  error_code = smt_add_attribute (def, "unique_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* class name */
  error_code = smt_add_attribute (def, "class_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_system_class", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "inst_attr_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_attr_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "shared_attr_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "inst_meth_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_meth_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "collation_id", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "tde_algorithm", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_CLASS_NAME);

  error_code = smt_add_attribute (def, "sub_classes", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "super_classes", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_ATTRIBUTE_NAME);

  error_code = smt_add_attribute (def, "inst_attrs", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_attrs", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "shared_attrs", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_METHOD_NAME);

  error_code = smt_add_attribute (def, "inst_meths", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_meths", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_METHFILE_NAME);

  error_code = smt_add_attribute (def, "meth_files", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_QUERYSPEC_NAME);

  error_code = smt_add_attribute (def, "query_specs", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_INDEX_NAME);

  error_code = smt_add_attribute (def, "indexes", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(2048)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_PARTITION_NAME);

  error_code = smt_add_attribute (def, "partition", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* 
   *  Define the index name so that it always has the same name as the macro variable (CATCLS_INDEX_NAME)
   *  in src/storage/catalog_class.c.
   * 
   *  _db_class must not have a primary key or a unique index. In the btree_key_insert_new_key function
   *  in src/storage/btree.c, it becomes assert (false) in the code below.
   * 
   *    CREATE TABLE t1 (c1 INT);
   *    RENAME CLASS t1 AS t2;
   * 
   *    assert ((btree_is_online_index_loading (insert_helper->purpose)) || !BTREE_IS_UNIQUE (btid_int->unique_pk)
   *            || log_is_in_crash_recovery () || btree_check_locking_for_insert_unique (thread_p, insert_helper));
   * 
   *  All others should be false, and !BTREE_IS_UNIQUE (btid_int->unique_pk) should be true. However,
   *  if there is a primary key or a unique index, !BTREE_IS_UNIQUE (btid_int->unique_pk) also becomes false,
   *  and all are false. In the btree_key_insert_new_key function, analysis should be added to the operation
   *  of the primary key and unique index.
   * 
   *  Currently, it is solved by creating only general indexes, not primary keys or unique indexes.
   */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, "i__db_class_unique_name", index1_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index2_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_attribute :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_attribute (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *index_col_names[4] = { "class_of", "attr_name", "attr_type", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "attr_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "attr_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "from_class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "from_attr_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "def_order", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "data_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "default_value", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_DOMAIN_NAME);

  error_code = smt_add_attribute (def, "domains", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_nullable", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_domain :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 *
 * Note:
 *
 */
static int
boot_define_domain (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *index_col_names1[2] = { "object_of", NULL };
  const char *index_col_names2[2] = { "data_type", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "object_of", "object", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "data_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "prec", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "scale", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "code_set", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "collation_id", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "enumeration", "sequence of character varying", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_DOMAIN_NAME);

  error_code = smt_add_attribute (def, "set_domains", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "json_schema", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names1, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names2, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_method :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_method (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *names[3] = { "class_of", "meth_name", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "meth_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "meth_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "from_class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "from_meth_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_METHSIG_NAME);

  error_code = smt_add_attribute (def, "signatures", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_meth_sig :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_meth_sig (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *names[2] = { "meth_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "meth_of", CT_METHOD_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "func_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "arg_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_METHARG_NAME);

  error_code = smt_add_attribute (def, "return_value", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "arguments", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_meth_argument :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_meth_argument (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "meth_sig_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "meth_sig_of", CT_METHSIG_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "data_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "index_of", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_DOMAIN_NAME);

  error_code = smt_add_attribute (def, "domains", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_meth_file :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_meth_file (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "class_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "from_class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "path_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_query_spec :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_query_spec (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "class_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "spec", "varchar(1073741823)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_index :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_index (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "class_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "index_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_unique", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "key_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_INDEXKEY_NAME);

  error_code = smt_add_attribute (def, "key_attrs", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_reverse", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_primary_key", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_foreign_key", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "filter_expression", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "have_function", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "status", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_meth_argument :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_index_key (MOP class_mop)
{
  SM_TEMPLATE *def;
  DB_VALUE prefix_default;
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "index_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = smt_add_attribute (def, "index_of", CT_INDEX_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "key_attr_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "key_order", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "asc_desc", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "key_prefix_length", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  db_make_int (&prefix_default, -1);

  error_code = smt_set_attribute_default (def, "key_prefix_length", 0, &prefix_default, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "func", "varchar(1023)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_class_authorization :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_class_authorization (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "grantee", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "grantor", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "grantee", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "auth_type", "varchar(7)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_grantable", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_partition :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_partition (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[] = { "class_of", "pname", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "pname", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "ptype", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "pexpr", "varchar(2048)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "pvalues", "sequence of", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_add_data_type :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 *
 * Note:
 *
 */
static int
boot_add_data_type (MOP class_mop)
{
  DB_OBJECT *obj;
  DB_VALUE val;
  int i;

  const char *names[DB_TYPE_LAST] = {
    "INTEGER", "FLOAT", "DOUBLE", "STRING", "OBJECT",
    "SET", "MULTISET", "SEQUENCE", "ELO", "TIME",
    "TIMESTAMP", "DATE", "MONETARY", NULL /* VARIABLE */ , NULL /* SUB */ ,
    NULL /* POINTER */ , NULL /* ERROR */ , "SHORT", NULL /* VOBJ */ ,
    NULL /* OID */ ,
    NULL /* VALUE */ , "NUMERIC", "BIT", "VARBIT", "CHAR",
    "NCHAR", "VARNCHAR", NULL /* RESULTSET */ , NULL /* MIDXKEY */ ,
    NULL /* TABLE */ ,
    "BIGINT", "DATETIME",
    "BLOB", "CLOB", "ENUM",
    "TIMESTAMPTZ", "TIMESTAMPLTZ", "DATETIMETZ", "DATETIMELTZ",
    "JSON"
  };

  for (i = 0; i < DB_TYPE_LAST; i++)
    {

      if (names[i] != NULL)
	{
	  obj = db_create_internal (class_mop);
	  if (obj == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }

	  db_make_int (&val, i + 1);
	  db_put_internal (obj, "type_id", &val);

	  db_make_varchar (&val, 16, names[i], strlen (names[i]), LANG_SYS_CODESET, LANG_SYS_COLLATION);
	  db_put_internal (obj, "type_name", &val);
	}
    }

  return NO_ERROR;
}

/*
 * boot_define_data_type :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_data_type (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "type_id", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* TODO : DB migration tool */
  error_code = smt_add_attribute (def, "type_name", "varchar(16)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = boot_add_data_type (class_mop);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_stored_procedure :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_stored_procedure (MOP class_mop)
{
  SM_TEMPLATE *def;
  char args_string[64];
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "sp_name", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "sp_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "sp_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "return_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "arg_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (args_string, "sequence of %s", CT_STORED_PROC_ARGS_NAME);
  error_code = smt_add_attribute (def, "args", args_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "lang", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "target", "varchar(4096)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_UNIQUE, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_stored_procedure_arguments :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_stored_procedure_arguments (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "sp_name", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "sp_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "index_of", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "arg_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "data_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "mode", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_serial :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_serial (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  unsigned char num[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  DB_VALUE default_value;
  int error_code = NO_ERROR;
  const char *index1_col_names[] = { "unique_name", NULL };
  const char *index2_col_names[] = { "name", "owner", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "unique_name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "numeric(%d,0)", DB_MAX_NUMERIC_PRECISION);
  numeric_coerce_int_to_num (1, num);
  db_make_numeric (&default_value, num, DB_MAX_NUMERIC_PRECISION, 0);

  error_code = smt_add_attribute (def, "current_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "current_val", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "increment_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "increment_val", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "max_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "min_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  db_make_int (&default_value, 0);

  error_code = smt_add_attribute (def, "cyclic", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "cyclic", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "started", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "started", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "att_name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_class_method (def, "change_serial_owner", "au_change_serial_owner_method");
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "cached_num", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "cached_num", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code =
    db_add_constraint (class_mop, DB_CONSTRAINT_PRIMARY_KEY, "pk_db_serial_unique_name", index1_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_UNIQUE, NULL, index2_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "current_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "increment_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "max_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "min_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_ha_apply_info :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_ha_apply_info (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[] = { "db_name", "copied_log_path", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "db_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "db_creation_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "copied_log_path", "varchar(4096)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "committed_lsa_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "committed_lsa_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "committed_rep_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "committed_rep_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "append_lsa_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "append_lsa_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "eof_lsa_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "eof_lsa_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "final_lsa_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "final_lsa_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "required_lsa_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "required_lsa_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "log_record_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "log_commit_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "last_access_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "status", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "insert_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "update_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "delete_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "schema_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "commit_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "fail_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "start_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add constraints */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_UNIQUE, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "db_name", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "copied_log_path", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "committed_lsa_pageid", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "committed_lsa_offset", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "required_lsa_pageid", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "required_lsa_offset", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_add_collations :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 *
 * Note:
 *
 */
int
boot_add_collations (MOP class_mop)
{
  int i;
  int count_collations;
  int found_coll = 0;

  count_collations = lang_collation_count ();

  for (i = 0; i < LANG_MAX_COLLATIONS; i++)
    {
      LANG_COLLATION *lang_coll = lang_get_collation (i);
      DB_OBJECT *obj;
      DB_VALUE val;

      assert (lang_coll != NULL);

      if (i != 0 && lang_coll->coll.coll_id == LANG_COLL_DEFAULT)
	{
	  /* iso88591 binary collation added only once */
	  continue;
	}
      found_coll++;

      obj = db_create_internal (class_mop);
      if (obj == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      assert (lang_coll->coll.coll_id == i);

      db_make_int (&val, i);
      db_put_internal (obj, CT_DBCOLL_COLL_ID_COLUMN, &val);

      db_make_varchar (&val, 32, lang_coll->coll.coll_name, strlen (lang_coll->coll.coll_name), LANG_SYS_CODESET,
		       LANG_SYS_COLLATION);
      db_put_internal (obj, CT_DBCOLL_COLL_NAME_COLUMN, &val);

      db_make_int (&val, (int) (lang_coll->codeset));
      db_put_internal (obj, CT_DBCOLL_CHARSET_ID_COLUMN, &val);

      db_make_int (&val, lang_coll->built_in);
      db_put_internal (obj, CT_DBCOLL_BUILT_IN_COLUMN, &val);

      db_make_int (&val, lang_coll->coll.uca_opt.sett_expansions ? 1 : 0);
      db_put_internal (obj, CT_DBCOLL_EXPANSIONS_COLUMN, &val);

      db_make_int (&val, lang_coll->coll.count_contr);
      db_put_internal (obj, CT_DBCOLL_CONTRACTIONS_COLUMN, &val);

      db_make_int (&val, (int) (lang_coll->coll.uca_opt.sett_strength));
      db_put_internal (obj, CT_DBCOLL_UCA_STRENGTH, &val);

      assert (strlen (lang_coll->coll.checksum) == 32);
      db_make_varchar (&val, 32, lang_coll->coll.checksum, 32, LANG_SYS_CODESET, LANG_SYS_COLLATION);
      db_put_internal (obj, CT_DBCOLL_CHECKSUM_COLUMN, &val);
    }

  assert (found_coll == count_collations);

  return NO_ERROR;
}

/*
 * boot_define_collations :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_collations (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, CT_DBCOLL_COLL_ID_COLUMN, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_COLL_NAME_COLUMN, "varchar(32)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_CHARSET_ID_COLUMN, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_BUILT_IN_COLUMN, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_EXPANSIONS_COLUMN, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_CONTRACTIONS_COLUMN, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_UCA_STRENGTH, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_CHECKSUM_COLUMN, "varchar(32)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = boot_add_collations (class_mop);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

#define CT_DBCHARSET_CHARSET_ID		  "charset_id"
#define CT_DBCHARSET_CHARSET_NAME	  "charset_name"
#define CT_DBCHARSET_DEFAULT_COLLATION	  "default_collation"
#define CT_DBCHARSET_CHAR_SIZE		  "char_size"

/*
 * boot_add_charsets :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 *
 * Note:
 *
 */
static int
boot_add_charsets (MOP class_mop)
{
  int i;
  int count_collations;

  count_collations = lang_collation_count ();

  for (i = INTL_CODESET_BINARY; i <= INTL_CODESET_LAST; i++)
    {
      DB_OBJECT *obj;
      DB_VALUE val;
      char *charset_name;

      obj = db_create_internal (class_mop);
      if (obj == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      db_make_int (&val, i);
      db_put_internal (obj, CT_DBCHARSET_CHARSET_ID, &val);

      charset_name = (char *) lang_charset_cubrid_name ((INTL_CODESET) i);
      if (charset_name == NULL)
	{
	  return ER_LANG_CODESET_NOT_AVAILABLE;
	}

      db_make_varchar (&val, 32, charset_name, strlen (charset_name), LANG_SYS_CODESET, LANG_SYS_COLLATION);
      db_put_internal (obj, CT_DBCHARSET_CHARSET_NAME, &val);

      db_make_int (&val, LANG_GET_BINARY_COLLATION (i));
      db_put_internal (obj, CT_DBCHARSET_DEFAULT_COLLATION, &val);

      db_make_int (&val, INTL_CODESET_MULT (i));
      db_put_internal (obj, CT_DBCHARSET_CHAR_SIZE, &val);
    }

  return NO_ERROR;
}

/*
 * boot_define_charsets :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_charsets (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, CT_DBCHARSET_CHARSET_ID, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCHARSET_CHARSET_NAME, "varchar(32)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCHARSET_DEFAULT_COLLATION, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCHARSET_CHAR_SIZE, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = boot_add_charsets (class_mop);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

#define CT_DUAL_DUMMY   "dummy"

  /*
   * boot_define_dual :
   *
   * returns : NO_ERROR if all OK, ER_ status otherwise
   *
   *   class(IN) :
   */

static int
boot_define_dual (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  DB_OBJECT *obj;
  DB_VALUE val;
  const char *dummy = "X";

  def = smt_edit_class_mop (class_mop, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = smt_add_attribute (def, CT_DUAL_DUMMY, "varchar(1)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  obj = db_create_internal (class_mop);
  if (obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }
  error_code = db_make_varchar (&val, 1, dummy, strlen (dummy), LANG_SYS_CODESET, LANG_SYS_COLLATION);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_put_internal (obj, CT_DUAL_DUMMY, &val);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

static int
boot_define_synonym (MOP class_mop)
{
  SM_TEMPLATE *def;
  DB_VALUE default_value;
  int error_code = NO_ERROR;
  const char *primary_key_col_names[] = { "unique_name", NULL };
  const char *index1_col_names[] = { "name", "owner", "is_public", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "unique_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_public", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  db_make_int (&default_value, 0);

  error_code = smt_set_attribute_default (def, "is_public", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "target_unique_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "target_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "target_owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(2048)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add constraints */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_PRIMARY_KEY, NULL, primary_key_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index1_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "name", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "owner", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "is_public", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "target_unique_name", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "target_name", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "target_owner", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_db_server :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_db_server (MOP class_mop)
{
  SM_TEMPLATE *def;
  char args_string[64];
  int error_code = NO_ERROR;
  const char *index_col_names[3] = { "link_name", "owner", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "link_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "host", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "port", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "db_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "user_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "password", "string", (DB_DOMAIN *) 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "properties", "varchar(2048)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_PRIMARY_KEY, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * catcls_class_install :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
catcls_class_install (void)
{
  // *INDENT-OFF*
  struct catcls_function
  {
    const char *name;
    const DEF_CLASS_FUNCTION function;
  }
  clist[] =
  {
    {CT_CLASS_NAME, boot_define_class},
    {CT_ATTRIBUTE_NAME, boot_define_attribute},
    {CT_DOMAIN_NAME, boot_define_domain},
    {CT_METHOD_NAME, boot_define_method},
    {CT_METHSIG_NAME, boot_define_meth_sig},
    {CT_METHARG_NAME, boot_define_meth_argument},
    {CT_METHFILE_NAME, boot_define_meth_file},
    {CT_QUERYSPEC_NAME, boot_define_query_spec},
    {CT_INDEX_NAME, boot_define_index},
    {CT_INDEXKEY_NAME, boot_define_index_key},
    {CT_DATATYPE_NAME, boot_define_data_type},
    {CT_CLASSAUTH_NAME, boot_define_class_authorization},
    {CT_PARTITION_NAME, boot_define_partition},
    {CT_STORED_PROC_NAME, boot_define_stored_procedure},
    {CT_STORED_PROC_ARGS_NAME, boot_define_stored_procedure_arguments},
    {CT_SERIAL_NAME, boot_define_serial},
    {CT_HA_APPLY_INFO_NAME, boot_define_ha_apply_info},
    {CT_COLLATION_NAME, boot_define_collations},
    {CT_CHARSET_NAME, boot_define_charsets},
    {CT_DUAL_NAME, boot_define_dual},
    {CT_DB_SERVER_NAME, boot_define_db_server},
    {CT_SYNONYM_NAME, boot_define_synonym}
  };
  // *INDENT-ON*

  MOP class_mop[sizeof (clist) / sizeof (clist[0])];
  int i, save;
  int error_code = NO_ERROR;
  int num_classes = sizeof (clist) / sizeof (clist[0]);

  AU_DISABLE (save);

  for (i = 0; i < num_classes; i++)
    {
      class_mop[i] = db_create_class (clist[i].name);
      if (class_mop[i] == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  goto end;
	}
      sm_mark_system_class (class_mop[i], 1);
    }

  for (i = 0; i < num_classes; i++)
    {
      error_code = (clist[i].function) (class_mop[i]);
      if (error_code != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  goto end;
	}
    }

end:
  AU_ENABLE (save);

  return error_code;
}

/*
 * boot_define_view_class :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 */
static int
boot_define_view_class (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"class_type", "varchar(6)"},
    {"is_system_class", "varchar(3)"},
    {"tde_algorithm", "varchar(32)"},
    {"partitioned", "varchar(3)"},
    {"is_reuse_oid_class", "varchar(3)"},
    {"collation", "varchar(32)"},
    {"comment", "varchar(2048)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_CLASS_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  sprintf (stmt,
	   "SELECT [c].[class_name], CAST([c].[owner].[name] AS VARCHAR(255)),"
	   " CASE [c].[class_type] WHEN 0 THEN 'CLASS' WHEN 1 THEN 'VCLASS' ELSE 'UNKNOW' END,"
	   " CASE WHEN MOD([c].[is_system_class], 2) = 1 THEN 'YES' ELSE 'NO' END,"
	   " CASE [c].[tde_algorithm] WHEN 0 THEN 'NONE' WHEN 1 THEN 'AES' WHEN 2 THEN 'ARIA' END,"
	   " CASE WHEN [c].[sub_classes] IS NULL THEN 'NO' ELSE NVL((SELECT 'YES'"
	   " FROM [%s] [p] WHERE [p].[class_of] = [c] and [p].[pname] IS NULL), 'NO') END,"
	   " CASE WHEN MOD([c].[is_system_class] / 8, 2) = 1 THEN 'YES' ELSE 'NO' END,"
	   " [coll].[coll_name], [c].[comment] FROM [%s] [c], [%s] [coll]"
	   " WHERE [c].[collation_id] = [coll].[coll_id] AND (CURRENT_USER = 'DBA' OR"
	   " {[c].[owner].[name]} SUBSETEQ (SELECT SET{CURRENT_USER} + COALESCE(SUM(SET{[t].[g].[name]}), SET{})"
	   " FROM [%s] [u], TABLE([groups]) AS [t]([g]) WHERE [u].[name] = CURRENT_USER) OR {[c]} SUBSETEQ ("
	   " SELECT SUM(SET{[au].[class_of]}) FROM [%s] [au] WHERE {[au].[grantee].[name]} SUBSETEQ ("
	   " SELECT SET{CURRENT_USER} + COALESCE(SUM(SET{[t].[g].[name]}), SET{})"
	   " FROM [%s] [u], TABLE([groups]) AS [t]([g]) WHERE [u].[name] = CURRENT_USER) AND"
	   " [au].[auth_type] = 'SELECT'))", CT_PARTITION_NAME, CT_CLASS_NAME, CT_COLLATION_NAME, AU_USER_CLASS_NAME,
	   CT_CLASSAUTH_NAME, AU_USER_CLASS_NAME);

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_super_class :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_super_class (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"super_class_name", "varchar(255)"},
    {"super_owner_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_SUPER_CLASS_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[c].[class_name] AS [class_name], "
	  "[c].[owner].[name] AS [owner_name], "
	  "[s].[class_name] AS [super_class_name], "
	  "[s].[owner].[name] AS [super_owner_name] "
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], TABLE ([c].[super_classes]) AS [t] ([s]) "
	"WHERE "
	  "CURRENT_USER = 'DBA' "
	  "OR {[c].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[c]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_vclass :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_vclass (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"vclass_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"vclass_def", "varchar(4096)"},
    {"comment", "varchar(2048)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_VCLASS_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[q].[class_of].[class_name] AS [vclass_name], "
	  "[q].[class_of].[owner].[name] AS [owner_name], "
	  "[q].[spec] AS [vclass_def], "
	  "[c].[comment] AS [comment] "
	"FROM "
	  /* CT_QUERYSPEC_NAME */
	  "[%s] AS [q], "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c] "
	"WHERE "
	  "[q].[class_of].[unique_name] = [c].[unique_name] "
	  "AND ("
	      "CURRENT_USER = 'DBA' "
	      "OR {[q].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[q].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_QUERYSPEC_NAME,
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_attribute :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_attribute (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"attr_name", "varchar(255)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"attr_type", "varchar(8)"},
    {"def_order", "integer"},
    {"from_class_name", "varchar(255)"},
    {"from_owner_name", "varchar(255)"},
    {"from_attr_name", "varchar(255)"},
    {"data_type", "varchar(9)"},
    {"prec", "integer"},
    {"scale", "integer"},
    {"charset", "varchar(32)"},
    {"collation", "varchar(32)"},
    {"domain_class_name", "varchar(255)"},
    {"domain_owner_name", "varchar(255)"},
    {"default_value", "varchar(255)"},
    {"is_nullable", "varchar(3)"},
    {"comment", "varchar(1024)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_ATTRIBUTE_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[a].[attr_name] AS [attr_name], "
	  "[c].[class_name] AS [class_name], "
	  "[c].[owner].[name] AS [owner_name], "
	  "CASE "
	    "WHEN [a].[attr_type] = 0 THEN 'INSTANCE' "
	    "WHEN [a].[attr_type] = 1 THEN 'CLASS' "
	    "ELSE 'SHARED' "
	    "END AS [attr_type], "
	  "[a].[def_order] AS [def_order], "
	  "[a].[from_class_of].[class_name] AS [from_class_name], "
	  "[a].[from_class_of].[owner].[name] AS [from_owner_name], "
	  "[a].[from_attr_name] AS [from_attr_name], "
	  "[t].[type_name] AS [data_type], "
	  "[d].[prec] AS [prec], "
	  "[d].[scale] AS [scale], "
	  "IF ("
	      "[a].[data_type] IN (4, 25, 26, 27, 35), "
	      /* CT_CHARSET_NAME */
	      "(SELECT [ch].[charset_name] FROM [%s] AS [ch] WHERE [d].[code_set] = [ch].[charset_id]), "
	      "'Not applicable'"
	    ") AS [charset], "
	  "IF ("
	      "[a].[data_type] IN (4, 25, 26, 27, 35), "
	      /* CT_COLLATION_NAME */
	      "(SELECT [coll].[coll_name] FROM [%s] AS [coll] WHERE [d].[collation_id] = [coll].[coll_id]), "
	      "'Not applicable'"
	    ") AS [collation], "
	  "[d].[class_of].[class_name] AS [domain_class_name], "
	  "[d].[class_of].[owner].[name] AS [domain_owner_name], "
	  "[a].[default_value] AS [default_value], "
	  "CASE WHEN [a].[is_nullable] = 1 THEN 'YES' ELSE 'NO' END AS [is_nullable], "
	  "[a].[comment] AS [comment] "
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], "
	  /* CT_ATTRIBUTE_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [t] "
	"WHERE "
	  "[a].[class_of] = [c] "
	  "AND [d].[object_of] = [a] "
	  "AND [d].[data_type] = [t].[type_id] "
	  "AND ("
	      "CURRENT_USER = 'DBA' "
	      "OR {[c].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_CHARSET_NAME,
	CT_COLLATION_NAME,
	CT_CLASS_NAME,
	CT_ATTRIBUTE_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_attribute_set_domain :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_attribute_set_domain (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"attr_name", "varchar(255)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"attr_type", "varchar(8)"},
    {"data_type", "varchar(9)"},
    {"prec", "integer"},
    {"scale", "integer"},
    {"code_set", "integer"},
    {"domain_class_name", "varchar(255)"},
    {"domain_owner_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_ATTR_SD_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[a].[attr_name] AS [attr_name], "
	  "[c].[class_name] AS [class_name], "
	  "[c].[owner].[name] AS [owner_name], "
	  "CASE "
	    "WHEN [a].[attr_type] = 0 THEN 'INSTANCE' "
	    "WHEN [a].[attr_type] = 1 THEN 'CLASS' "
	    "ELSE 'SHARED' "
	    "END AS [attr_type], "
	  "[et].[type_name] AS [data_type], "
	  "[e].[prec] AS [prec], "
	  "[e].[scale] AS [scale], "
	  "[e].[code_set] AS [code_set], "
	  "[e].[class_of].[class_name] AS [domain_class_name], "
	  "[e].[class_of].[owner].[name] AS [domain_owner_name] "
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], "
	  /* CT_ATTRIBUTE_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], TABLE ([d].[set_domains]) AS [t] ([e]), "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [et] "
	"WHERE "
	  "[a].[class_of] = [c] "
	  "AND [d].[object_of] = [a] "
	  "AND [e].[data_type] = [et].[type_id] "
	  "AND ("
	      "CURRENT_USER = 'DBA' "
	      "OR {[c].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_CLASS_NAME,
	CT_ATTRIBUTE_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_method :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_method (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"meth_name", "varchar(255)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"meth_type", "varchar(8)"},
    {"from_class_name", "varchar(255)"},
    {"from_owner_name", "varchar(255)"},
    {"from_meth_name", "varchar(255)"},
    {"func_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_METHOD_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[m].[meth_name] AS [meth_name], "
	  "[m].[class_of].[class_name] AS [class_name], "
	  "[m].[class_of].[owner].[name] AS [owner_name], "
	  "CASE "
	    "WHEN [m].[meth_type] = 0 THEN 'INSTANCE' "
	    "ELSE 'CLASS' "
	    "END AS [meth_type], "
	  "[m].[from_class_of].[class_name] AS [from_class_name], "
	  "[m].[from_class_of].[owner].[name] AS [from_owner_name], "
	  "[m].[from_meth_name] AS [from_meth_name], "
	  "[s].[func_name] AS [func_name] "
	"FROM "
	  /* CT_METHOD_NAME */
	  "[%s] AS [m], "
	  /* CT_METHSIG_NAME */
	  "[%s] AS [s] "
	"WHERE "
	  "[s].[meth_of] = [m] "
	  "AND ("
	      "CURRENT_USER = 'DBA' "
	      "OR {[m].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[m].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_METHOD_NAME,
	CT_METHSIG_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_method_argument :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_method_argument (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"meth_name", "varchar(255)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"meth_type", "varchar(8)"},
    {"index_of", "integer"},
    {"data_type", "varchar(9)"},
    {"prec", "integer"},
    {"scale", "integer"},
    {"code_set", "integer"},
    {"domain_class_name", "varchar(255)"},
    {"domain_owner_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_METHARG_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[meth_of].[meth_name] AS [meth_name], "
	  "[s].[meth_of].[class_of].[class_name] AS [class_name], "
	  "[s].[meth_of].[class_of].[owner].[name] AS [owner_name], "
	  "CASE "
	    "WHEN [s].[meth_of].[meth_type] = 0 THEN 'INSTANCE' "
	    "ELSE 'CLASS' "
	    "END AS [meth_type], "
	  "[a].[index_of] AS [index_of], "
	  "[t].[type_name] AS [data_type], "
	  "[d].[prec] AS [prec], "
	  "[d].[scale] AS [scale], "
	  "[d].[code_set] AS [code_set], "
	  "[d].[class_of].[class_name] AS [domain_class_name], "
	  "[d].[class_of].[owner].[name] AS [domain_owner_name] "
	"FROM "
	  /* CT_METHSIG_NAME */
	  "[%s] AS [s], "
	  /* CT_METHARG_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [t] "
	"WHERE "
	  "[a].[meth_sig_of] = [s] "
	  "AND [d].[object_of] = [a] "
	  "AND [d].[data_type] = [t].[type_id] "
	  "AND ("
	      "CURRENT_USER = 'DBA' "
	      "OR {[s].[meth_of].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[s].[meth_of].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_METHSIG_NAME,
	CT_METHARG_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_method_argument_set_domain :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 * Note:
 *
 */
static int
boot_define_view_method_argument_set_domain (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"meth_name", "varchar(255)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"meth_type", "varchar(8)"},
    {"index_of", "integer"},
    {"data_type", "varchar(9)"},
    {"prec", "integer"},
    {"scale", "integer"},
    {"code_set", "integer"},
    {"domain_class_name", "varchar(255)"},
    {"domain_owner_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_METHARG_SD_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[meth_of].[meth_name] AS [meth_name], "
	  "[s].[meth_of].[class_of].[class_name] AS [class_name], "
	  "[s].[meth_of].[class_of].[owner].[name] AS [owner_name], "
	  "CASE "
	    "WHEN [s].[meth_of].[meth_type] = 0 THEN 'INSTANCE' "
	    "ELSE 'CLASS' "
	    "END AS [meth_type], "
	  "[a].[index_of] AS [index_of], "
	  "[et].[type_name] AS [data_type], "
	  "[e].[prec] AS [prec], "
	  "[e].[scale] AS [scale], "
	  "[e].[code_set] AS [code_set], "
	  "[e].[class_of].[class_name] AS [domain_class_name], "
	  "[e].[class_of].[owner].[name] AS [domain_owner_name] "
	"FROM "
	  /* CT_METHSIG_NAME */
	  "[%s] AS [s], "
	  /* CT_METHARG_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], TABLE ([d].[set_domains]) AS [t] ([e]), "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [et] "
	"WHERE "
	  "[a].[meth_sig_of] = [s] "
	  "AND [d].[object_of] = [a] "
	  "AND [e].[data_type] = [et].[type_id] "
	  "AND ("
	      "CURRENT_USER = 'DBA' "
	      "OR {[s].[meth_of].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[s].[meth_of].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_METHSIG_NAME,
	CT_METHARG_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_method_file :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_method_file (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"path_name", "varchar(255)"},
    {"from_class_name", "varchar(255)"},
    {"from_owner_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_METHFILE_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[f].[class_of].[class_name] AS [class_name], "
	  "[f].[class_of].[owner].[name] AS [owner_name], "
	  "[f].[path_name] AS [path_name], "
	  "[f].[from_class_of].[class_name] AS [from_class_name], "
	  "[f].[from_class_of].[owner].[name] AS [from_owner_name] "
	"FROM "
	  /* CT_METHFILE_NAME */
	  "[%s] AS [f] "
	"WHERE "
	  "CURRENT_USER = 'DBA' "
	  "OR {[f].[class_of].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[f].[class_of]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	CT_METHFILE_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_index :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_index (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"index_name", "varchar(255)"},
    {"is_unique", "varchar(3)"},
    {"is_reverse", "varchar(3)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"key_count", "integer"},
    {"is_primary_key", "varchar(3)"},
    {"is_foreign_key", "varchar(3)"},
    {"filter_expression", "varchar(255)"},
    {"have_function", "varchar(3)"},
    {"comment", "varchar(1024)"},
    {"status", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_INDEX_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  sprintf (stmt,
	   "SELECT [i].[index_name], CASE WHEN [i].[is_unique] = 0 THEN 'NO' ELSE 'YES' END,"
	   " CASE WHEN [i].[is_reverse] = 0 THEN 'NO' ELSE 'YES' END, [i].[class_of].[class_name], [i].[class_of].[owner].[name], [i].[key_count],"
	   " CASE WHEN [i].[is_primary_key] = 0 THEN 'NO' ELSE 'YES' END,"
	   " CASE WHEN [i].[is_foreign_key] = 0 THEN 'NO' ELSE 'YES' END, [i].[filter_expression],"
	   " CASE WHEN [i].[have_function] = 0 THEN 'NO' ELSE 'YES' END, [i].[comment],"
	   " CASE WHEN [i].[status] = 0 THEN 'NO_INDEX' "
	   " WHEN [i].[status] = 1 THEN 'NORMAL INDEX' "
	   " WHEN [i].[status] = 2 THEN 'INVISIBLE INDEX'"
	   " WHEN [i].[status] = 3 THEN 'INDEX IS IN ONLINE BUILDING' "
	   " ELSE 'NULL' END "
	   " FROM [%s] [i]"
	   " WHERE CURRENT_USER = 'DBA' OR {[i].[class_of].[owner].[name]} SUBSETEQ ("
	   " SELECT SET{CURRENT_USER} + COALESCE(SUM(SET{[t].[g].[name]}), SET{})"
	   " FROM [%s] [u], TABLE([groups]) AS [t]([g]) WHERE [u].[name] = CURRENT_USER) OR"
	   " {[i].[class_of]} SUBSETEQ (SELECT SUM(SET{[au].[class_of]}) FROM [%s] [au]"
	   " WHERE {[au].[grantee].[name]} SUBSETEQ ("
	   " SELECT SET{CURRENT_USER} + COALESCE(SUM(SET{[t].[g].[name]}), SET{})"
	   " FROM [%s] [u], TABLE([groups]) AS [t]([g]) WHERE [u].[name] = CURRENT_USER) AND"
	   " [au].[auth_type] = 'SELECT')", CT_INDEX_NAME, AU_USER_CLASS_NAME, CT_CLASSAUTH_NAME, AU_USER_CLASS_NAME);

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_index_key :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_index_key (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"index_name", "varchar(255)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"key_attr_name", "varchar(255)"},
    {"key_order", "integer"},
    {"asc_desc", "varchar(4)"},
    {"key_prefix_length", "integer"},
    {"func", "varchar(1023)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_INDEXKEY_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  sprintf (stmt,
	   "SELECT [k].[index_of].[index_name], [k].[index_of].[class_of].[class_name], [k].[index_of].[class_of].[owner].[name],"
	   " [k].[key_attr_name], [k].[key_order], CASE [k].[asc_desc] WHEN 0 THEN 'ASC' WHEN 1 THEN 'DESC'"
	   " ELSE 'UNKN' END, [k].[key_prefix_length], [k].[func] FROM [%s] [k]"
	   " WHERE CURRENT_USER = 'DBA' OR {[k].[index_of].[class_of].[owner].[name]} SUBSETEQ ("
	   " SELECT SET{CURRENT_USER} + COALESCE(SUM(SET{[t].[g].[name]}), SET{})"
	   " FROM [%s] [u], TABLE([groups]) AS [t]([g]) WHERE [u].[name] = CURRENT_USER) OR"
	   " {[k].[index_of].[class_of]} SUBSETEQ (SELECT SUM(SET{[au].[class_of]}) FROM [%s] [au]"
	   " WHERE {[au].[grantee].[name]} SUBSETEQ ("
	   " SELECT SET{CURRENT_USER} + COALESCE(SUM(SET{[t].[g].[name]}), SET{})"
	   " FROM [%s] [u], TABLE([groups]) AS [t]([g]) WHERE [u].[name] = CURRENT_USER) AND"
	   " [au].[auth_type] = 'SELECT')", CT_INDEXKEY_NAME, AU_USER_CLASS_NAME, CT_CLASSAUTH_NAME,
	   AU_USER_CLASS_NAME);

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_authorization :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_authorization (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"grantor_name", "varchar(255)"},
    {"grantee_name", "varchar(255)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"auth_type", "varchar(7)"},
    {"is_grantable", "varchar(3)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_AUTH_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  sprintf (stmt,
	   "SELECT CAST([a].[grantor].[name] AS VARCHAR(255)),"
	   " CAST([a].[grantee].[name] AS VARCHAR(255)), [a].[class_of].[class_name], [a].[class_of].[owner].[name], [a].[auth_type],"
	   " CASE WHEN [a].[is_grantable] = 0 THEN 'NO' ELSE 'YES' END FROM [%s] [a]"
	   " WHERE CURRENT_USER = 'DBA' OR {[a].[class_of].[owner].[name]} SUBSETEQ ("
	   " SELECT SET{CURRENT_USER} + COALESCE(SUM(SET{[t].[g].[name]}), SET{})"
	   " FROM [%s] [u], TABLE([groups]) AS [t]([g]) WHERE [u].[name] = CURRENT_USER) OR"
	   " {[a].[class_of]} SUBSETEQ (SELECT SUM(SET{[au].[class_of]}) FROM [%s] [au]"
	   " WHERE {[au].[grantee].[name]} SUBSETEQ ("
	   " SELECT SET{CURRENT_USER} + COALESCE(SUM(SET{[t].[g].[name]}), SET{})"
	   " FROM [%s] [u], TABLE([groups]) AS [t]([g]) WHERE [u].[name] = CURRENT_USER) AND"
	   " [au].[auth_type] = 'SELECT')", CT_CLASSAUTH_NAME, AU_USER_CLASS_NAME, CT_CLASSAUTH_NAME,
	   AU_USER_CLASS_NAME);

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_trigger :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_trigger (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"trigger_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"target_class_name", "varchar(255)"},
    {"target_owner_name", "varchar(255)"},
    {"target_attr_name", "varchar(255)"},
    {"target_attr_type", "varchar(8)"},
    {"action_type", "integer"},
    {"action_time", "integer"},
    {"comment", "varchar(1024)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_TRIGGER_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  /* Why? {[c]} SUBSETEQ (SELECT SUM(SET{[au].[class_of]}) FROM ... */
  /* {[c]} -> {[t].[target_class]} ? */
  sprintf (stmt,
	   "SELECT CAST([t].[name] AS VARCHAR(255)), [t].[owner].[name], [c].[class_name], [c].[owner].[name], CAST([t].[target_attribute] AS VARCHAR(255)),"
	   " CASE [t].[target_class_attribute] WHEN 0 THEN 'INSTANCE' ELSE 'CLASS' END,"
	   " [t].[action_type], [t].[action_time], [t].[comment]"
	   " FROM [%s] [t] LEFT OUTER JOIN [%s] [c] ON [t].[target_class] = [c].[class_of]"
	   " WHERE CURRENT_USER = 'DBA' OR {[t].[owner].[name]} SUBSETEQ (SELECT SET{CURRENT_USER} +"
	   " COALESCE(SUM(SET{[t].[g].[name]}), SET{}) FROM [%s] [u], TABLE([groups]) AS [t]([g])"
	   " WHERE [u].[name] = CURRENT_USER ) OR {[c]} SUBSETEQ (SELECT SUM(SET{[au].[class_of]}) FROM [%s] [au]"
	   " WHERE {[au].[grantee].[name]} SUBSETEQ (SELECT SET{CURRENT_USER} +"
	   " COALESCE(SUM(SET{[t].[g].[name]}), SET{}) FROM [%s] [u], TABLE([groups]) AS [t]([g])"
	   " WHERE [u].[name] = CURRENT_USER) AND [au].[auth_type] = 'SELECT')", TR_CLASS_NAME, CT_CLASS_NAME,
	   AU_USER_CLASS_NAME, CT_CLASSAUTH_NAME, AU_USER_CLASS_NAME);

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_partition :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_partition (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"partition_name", "varchar(255)"},
    {"partition_class_name", "varchar(255)"},
    {"partition_type", "varchar(32)"},
    {"partition_expr", "varchar(2048)"},
    {"partition_values", "sequence of"},
    {"comment", "varchar(1024)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_PARTITION_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[pp].[super_class_name] AS [class_name], "
	  "[pp].[super_owner_name] AS [owner_name], "
	  "[p].[pname] AS [partition_name], "
	  "CONCAT ([pp].[super_class_name], '__p__', [p].[pname]) AS [partition_class_name], "
	  "CASE "
	      "WHEN [p].[ptype] = 0 THEN 'HASH' "
	      "WHEN [p].[ptype] = 1 THEN 'RANGE' "
	      "ELSE 'LIST' "
	      "END AS [partition_type], "
	   "TRIM (SUBSTRING ([pi].[pexpr] FROM 8 FOR (POSITION (' FROM ' IN [pi].[pexpr]) - 8))) AS [partition_expr], "
	   "[p].[pvalues] AS [partition_values], "
	   "[p].[comment] AS [comment] "
	"FROM "
	  /* CT_PARTITION_NAME */
	  "[%s] [p], "
	  "( "
	    "SELECT "
	      "* "
	    "FROM "
	      /* CTV_SUPER_CLASS_NAME */
	      "[%s] [sc], "
	      /* CT_PARTITION_NAME */
	      "[%s] [sp] "
	    "WHERE "
	      "[sc].[class_name] = [sp].[class_of].[class_name] "
	      "AND [sc].[owner_name] = [sp].[class_of].[owner].[name] "
	  ") [pp], "
	  "( "
	    "SELECT "
	      "[tt].[ss].[pexpr] AS [pexpr], "
	      "[ss].[class_name] AS [class_name], "
	      "[ss].[owner].[name] AS [owner_name] "
	    "FROM "
	      /* CT_CLASS_NAME */
	      "[%s] [ss], TABLE ([ss].[partition]) AS [tt] ([ss]) "
	  ") [pi] "
	"WHERE "
	  "[pp].[class_name] = [p].[class_of].[class_name] "
	  "AND [pp].[owner_name] = [p].[class_of].[owner].[name] "
	  "AND [pi].[class_name] = [pp].[super_class_name] "
	  "AND [pi].[owner_name] = [pp].[super_owner_name] "
	  "AND ( "
	      "CURRENT_USER = 'DBA' "
	      "OR {[p].[class_of].[owner].[name]} SUBSETEQ ( "
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET{[t].[g].[name]}), SET{})"
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] [u], TABLE ([groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER "
		") "
	      "OR {[p].[class_of]} SUBSETEQ ( "
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ( "
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET{})"
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] [u], TABLE ([groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER "
		      ") "
		    "AND [au].[auth_type] = 'SELECT' "
		") "
	  ")",
	CT_PARTITION_NAME,
	CTV_SUPER_CLASS_NAME,
	CT_PARTITION_NAME,
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_stored_procedure :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_stored_procedure (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"sp_name", "varchar(255)"},
    {"sp_type", "varchar(16)"},
    {"return_type", "varchar(16)"},
    {"arg_count", "integer"},
    {"lang", "varchar(16)"},
    {"target", "varchar(4096)"},
    {"owner", "varchar(256)"},
    {"comment", "varchar(1024)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_STORED_PROC_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  sprintf (stmt,
	   "SELECT [sp].[sp_name], CASE [sp].[sp_type] WHEN 1 THEN 'PROCEDURE' ELSE 'FUNCTION' END,"
	   " CASE WHEN [sp].[return_type] = 0 THEN 'void' WHEN [sp].[return_type] = 28 THEN 'CURSOR'"
	   " ELSE (SELECT [dt].[type_name] FROM [%s] [dt] WHERE [sp].[return_type] = [dt].[type_id]) END,"
	   " [sp].[arg_count], CASE [sp].[lang] WHEN 1 THEN 'JAVA' ELSE '' END,"
	   " [sp].[target], [sp].[owner].[name], [sp].[comment] FROM [%s] [sp]", CT_DATATYPE_NAME, CT_STORED_PROC_NAME);

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_stored_procedure_arguments :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_stored_procedure_arguments (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"sp_name", "varchar(255)"},
    {"index_of", "integer"},
    {"arg_name", "varchar(256)"},
    {"data_type", "varchar(16)"},
    {"mode", "varchar(6)"},
    {"comment", "varchar(1024)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_STORED_PROC_ARGS_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  sprintf (stmt,
	   "SELECT [sp].[sp_name], [sp].[index_of], [sp].[arg_name], CASE [sp].[data_type]"
	   " WHEN 28 THEN 'CURSOR'"
	   " ELSE (SELECT [dt].[type_name] FROM [%s] [dt] WHERE [sp].[data_type] = [dt].[type_id]) END, CASE"
	   " WHEN [sp].[mode] = 1 THEN 'IN' WHEN [sp].[mode] = 2 THEN 'OUT' ELSE 'INOUT' END,"
	   " [sp].[comment] FROM [%s] [sp] ORDER BY [sp].[sp_name], [sp].[index_of]", CT_DATATYPE_NAME,
	   CT_STORED_PROC_ARGS_NAME);

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_db_collation :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_db_collation (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"coll_id", "integer"},
    {"coll_name", "varchar(255)"},
    {"charset_name", "varchar(255)"},
    {"is_builtin", "varchar(3)"},
    {"has_expansions", "varchar(3)"},
    {"contractions", "integer"},
    {"uca_strength", "varchar(255)"}
  };

  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_DB_COLLATION_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  sprintf (stmt,
	   "SELECT [c].[coll_id], [c].[coll_name], [ch].[charset_name], CASE [c].[built_in] WHEN 0 THEN 'No'"
	   " WHEN 1 THEN 'Yes' ELSE 'ERROR' END, CASE [c].[expansions] WHEN 0 THEN 'No'"
	   " WHEN 1 THEN 'Yes' ELSE 'ERROR' END, [c].[contractions], CASE [c].[uca_strength]"
	   " WHEN 0 THEN 'Not applicable' WHEN 1 THEN 'Primary' WHEN 2 THEN 'Secondary'"
	   " WHEN 3 THEN 'Tertiary' WHEN 4 THEN 'Quaternary' WHEN 5 THEN 'Identity' ELSE 'Unknown' END"
	   " FROM [%s] [c] JOIN [%s] [ch] ON [c].[charset_id] = [ch].[charset_id] ORDER BY [c].[coll_id]",
	   CT_COLLATION_NAME, CT_CHARSET_NAME);

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_db_charset :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_db_charset (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {CT_DBCHARSET_CHARSET_ID, "integer"},
    {CT_DBCHARSET_CHARSET_NAME, "varchar(32)"},
    {CT_DBCHARSET_DEFAULT_COLLATION, "varchar(32)"},
    {CT_DBCHARSET_CHAR_SIZE, "int"}
  };

  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_DB_CHARSET_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  sprintf (stmt,
	   "SELECT [ch].[charset_id], [ch].[charset_name], [coll].[coll_name], [ch].[char_size] "
	   "FROM [%s] [ch] JOIN [%s] [coll] ON [ch].[default_collation] = [coll].[coll_id] "
	   "ORDER BY [ch].[charset_id]", CT_CHARSET_NAME, CT_COLLATION_NAME);

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

static int
boot_define_view_synonym (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"synonym_name", "varchar(255)"},
    {"synonym_owner_name", "varchar(255)"},
    {"is_public_synonym", "varchar(3)"},	/* access_modifier */
    {"target_name", "varchar(255)"},
    {"target_owner_name", "varchar(255)"},
    {"comment", "varchar(2048)"}
  };

  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  /* Initialization */
  memset (stmt, '\0', sizeof (char) * 2048);

  class_mop = db_create_vclass (CTV_SYNONYM_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[name] AS [synonym_name], "
	  "CAST ([s].[owner].[name] AS VARCHAR(255)) AS [synonym_owner_name], "
	  "CASE WHEN [s].[is_public] = 1 THEN 'YES' ELSE 'NO' END AS [is_public_synonym], "
	  "[s].[target_name] AS [target_name], "
	  "CAST ([s].[target_owner].[name] AS VARCHAR(255)) AS [target_owner_name], "
	  "[s].[comment] AS [comment] "
	"FROM "
	  /* CT_SYNONYM_NAME */
	  "[%s] [s] "
	"WHERE "
	  "CURRENT_USER = 'DBA' "
	  "OR [s].[is_public] = 1 "
	  "OR ( "
	      "[s].[is_public] = 0 "
	      "AND {[s].[owner].[name]} SUBSETEQ ( "
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] [u], TABLE([groups]) AS [t]([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER "
		") "
	    ") ",
	CT_SYNONYM_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_db_server :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_db_server (void)
{
  MOP class_mop;
  COLUMN columns[] = {
    {"link_name", "varchar(255)"},
    {"host", "varchar(255)"},
    {"port", "integer"},
    {"db_name", "varchar(255)"},
    {"user_name", "varchar(255)"},
    //{"password", "varchar(256)"}
    {"properties", "varchar(2048)"},
    {"owner", "varchar(256)"},
    {"comment", "varchar(1024)"}

  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;


  class_mop = db_create_vclass (CTV_DB_SERVER_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name, columns[i].type, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  sprintf (stmt,
	   "SELECT [ds].[link_name], [ds].[host], [ds].[port], [ds].[db_name], [ds].[user_name], [ds].[properties],"
	   "       [ds].[owner].[name], [ds].[comment]"
	   " FROM [%s] [ds] WHERE CURRENT_USER = 'DBA' "
	   "  OR {[ds].[owner].[name]} SUBSETEQ (SELECT SET{CURRENT_USER} + COALESCE(SUM(SET{[t].[g].[name]}), SET{})"
	   "        FROM [%s] [u], TABLE([groups]) AS [t]([g]) WHERE [u].[name] = CURRENT_USER)"
	   "  OR {[ds]} SUBSETEQ ( SELECT SUM(SET{[au].[class_of]}) FROM [%s] [au] WHERE {[au].[grantee].[name]} "
	   "        SUBSETEQ ( SELECT SET{CURRENT_USER} + COALESCE(SUM(SET{[t].[g].[name]}), SET{}) "
	   "                   FROM [%s] [u], TABLE([groups]) AS [t]([g])"
	   "                   WHERE [u].[name] = CURRENT_USER) AND [au].[auth_type] = 'SELECT')", CT_DB_SERVER_NAME,
	   CT_USER_NAME, CT_CLASSAUTH_NAME, CT_USER_NAME);

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * catcls_vclass_install :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
catcls_vclass_install (void)
{
  // *INDENT-OFF*
  struct catcls_function
  {
    const DEF_FUNCTION function;
  }
  clist[] =
  {
    {boot_define_view_class},  /* CTV_CLASS_NAME */
    {boot_define_view_super_class}, /* CTV_SUPER_CLASS_NAME */
    {boot_define_view_vclass}, /* CTV_VCLASS_NAME */
    {boot_define_view_attribute}, /* CTV_ATTRIBUTE_NAME */
    {boot_define_view_attribute_set_domain}, /* CTV_ATTR_SD_NAME */
    {boot_define_view_method}, /* CTV_METHOD_NAME */
    {boot_define_view_method_argument}, /* CTV_METHARG_NAME */
    {boot_define_view_method_argument_set_domain}, /* CTV_METHARG_SD_NAME */
    {boot_define_view_method_file}, /* CTV_METHFILE_NAME */
    {boot_define_view_index}, /* CTV_INDEX_NAME */
    {boot_define_view_index_key}, /* CTV_INDEXKEY_NAME */
    {boot_define_view_authorization}, /* CTV_AUTH_NAME */
    {boot_define_view_trigger}, /* CTV_TRIGGER_NAME */
    {boot_define_view_partition}, /* CTV_PARTITION_NAME */
    {boot_define_view_stored_procedure}, /* CTV_STORED_PROC_NAME */
    {boot_define_view_stored_procedure_arguments}, /* CTV_STORED_PROC_ARGS_NAME */
    {boot_define_view_db_collation}, /* CTV_DB_COLLATION_NAME */
    {boot_define_view_db_charset}, /* CTV_DB_CHARSET_NAME */
    {boot_define_view_db_server}, /* CTV_DB_SERVER_NAME */
    {boot_define_view_synonym} /* CTV_SYNONYM_NAME */
  };
  // *INDENT-ON*

  int save;
  size_t i;
  size_t num_vclasses = sizeof (clist) / sizeof (clist[0]);
  int error_code = NO_ERROR;

  AU_DISABLE (save);

  for (i = 0; i < num_vclasses; i++)
    {
      error_code = (clist[i].function) ();
      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }

end:
  AU_ENABLE (save);

  return error_code;
}

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

  if ((hp = gethostbyname (boot_Host_name)) != NULL)
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
