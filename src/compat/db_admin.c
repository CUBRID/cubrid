/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * db_admin.c - CUBRID Application Program Interface.
 *      Functions related to database creation and administration.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include "porting.h"
#include "system_parameter.h"
#include "storage_common.h"
#include "environment_variable.h"
#include "db.h"
#include "class_object.h"
#include "object_print.h"
#include "server_interface.h"
#include "boot_cl.h"
#include "locator_cl.h"
#include "schema_manager.h"
#include "schema_template.h"
#include "object_accessor.h"
#include "set_object.h"
#include "virtual_object.h"
#include "parser.h"
#include "memory_alloc.h"
#include "execute_schema.h"
#include "network_interface_cl.h"
#if defined(SA_MODE)
#include "jsp_sr.h"
#endif /* SA_MODE */
#include "jsp_cl.h"
#include "execute_statement.h"
#include "connection_support.h"
#include "trigger_manager.h"
#if !defined(CS_MODE)
#include "session.h"
#endif

#include "dbval.h"		/* this must be the last header file included!!! */

#if !defined(WINDOWS)
void (*prev_sigfpe_handler) (int) = SIG_DFL;
#endif /* !WINDOWS */

/* Some like to assume that the db_ layer is able to recognize that a
 database has not been successfully restarted.  For now, check every
 time.  We'll want another functional layer for esql that doesn't
 do all this checking.
 The macros for testing this variable were moved to db.h so the query
 interface functions can use them as well. */

char db_Database_name[DB_MAX_IDENTIFIER_LENGTH + 1];
char db_Program_name[PATH_MAX];

static char *db_Preferred_hosts = NULL;
static int db_Connect_order = DB_CONNECT_ORDER_SEQ;
static int db_Max_num_delayed_hosts_lookup = 0;
static int db_Delayed_hosts_count = 0;

/* a list of abnormal host status */
static DB_HOST_STATUS_LIST db_Host_status_list;

static DB_HOST_STATUS *db_add_host_status (char *hostname, int status);
static DB_HOST_STATUS *db_find_host_status (char *hostname);

static void install_static_methods (void);
static int fetch_set_internal (DB_SET * set, DB_FETCH_MODE purpose, int quit_on_error);
#if !defined(WINDOWS)
void sigfpe_handler (int sig);
#endif

/*
 * install_static_methods() - Installs the static method definitions for the
 *        system defined classes. This may change depending upon the product
 *        build configuration.
 * return  : none
 *
 */
static void
install_static_methods (void)
{
  au_link_static_methods ();	/* Authorization classes */
}

/*
 * db_init() - This will create a database file and associated log files and
 *    install the authorization objects and other required system objects.
 *    This is kept only for temporary compatibility.  The creation of
 *    databases will ultimately be done only by a specially written utility
 *    function that will ensure all of the various configuration options are
 *    applied.
 *
 * return           : Error Indicator.
 * program(in)      : the program name from argv[0]
 * print_version(in): a flag enabling an initial "herald" message
 * dbname(in)	    : the name of the database (server name)
 * db_path(in)	    :
 * vol_path(in)     :
 * log_path(in)     :
 * lob_path(in)     :
 * host_name(in)    :
 * overwrite(in)    :
 * comments(in)     : additional comments to be added to the label
 * addmore_vols_file(in):
 * npages(in)       : the initial page allocation
 * desired_pagesize(in):
 * log_npages(in):
 * desired_log_page_size(in):
 * lang_charset(in): string for language and charset (ko_KR.utf8)
 *
 */

int
db_init (const char *program, int print_version, const char *dbname, const char *db_path, const char *vol_path,
	 const char *log_path, const char *lob_path, const char *host_name, const bool overwrite, const char *comments,
	 const char *addmore_vols_file, int npages, int desired_pagesize, int log_npages, int desired_log_page_size,
	 const char *lang_charset)
{
#if defined (CUBRID_DEBUG)
  int value;
  const char *env_value;
  char more_vol_info_temp_file[L_tmpnam];
#endif
  const char *more_vol_info_file = NULL;
  int error = NO_ERROR;
  BOOT_CLIENT_CREDENTIAL client_credential;
  BOOT_DB_PATH_INFO db_path_info;

  db_Connect_status = DB_CONNECTION_STATUS_CONNECTED;

#if defined (CUBRID_DEBUG)
  if (addmore_vols_file == NULL)
    {
      /* Added for debugging of multivols using old test programs/scripts What to do with volumes. */
      env_value = envvar_get ("BOSR_SPLIT_INIT_VOLUME");
      if (env_value != NULL)
	{
	  value = atoi (env_value);
	}
      else
	{
	  value = 0;
	}

      if (value != 0)
	{
	  FILE *more_vols_fp;
	  DKNPAGES db_npages;

	  db_npages = npages / 4;

	  if (tmpnam (more_vol_info_temp_file) != NULL && (more_vols_fp = fopen (more_vol_info_temp_file, "w")) != NULL)
	    {
	      fprintf (more_vols_fp, "%s %s %s %d", "PURPOSE", "DATA", "NPAGES", db_npages);
	      fprintf (more_vols_fp, "%s %s %s %d", "PURPOSE", "INDEX", "NPAGES", db_npages);
	      fprintf (more_vols_fp, "%s %s %s %d", "PURPOSE", "TEMP", "NPAGES", db_npages);
	      fclose (more_vols_fp);

	      if ((db_npages * 4) != npages)
		{
		  npages = npages - (db_npages * 4);
		}
	      else
		{
		  npages = db_npages;
		}

	      addmore_vols_file = more_vol_info_file = more_vol_info_temp_file;
	    }
	}
    }
#endif /* CUBRID_DEBUG */

  if (desired_pagesize > 0)
    {
      if (desired_pagesize < IO_MIN_PAGE_SIZE)
	{
	  desired_pagesize = IO_MIN_PAGE_SIZE;
	}
      else if (desired_pagesize > IO_MAX_PAGE_SIZE)
	{
	  desired_pagesize = IO_MAX_PAGE_SIZE;
	}
    }
  else
    {
      desired_pagesize = IO_DEFAULT_PAGE_SIZE;
    }

  if (desired_log_page_size > 0)
    {
      if (desired_log_page_size < IO_MIN_PAGE_SIZE)
	{
	  desired_log_page_size = IO_MIN_PAGE_SIZE;
	}
      else if (desired_log_page_size > IO_MAX_PAGE_SIZE)
	{
	  desired_log_page_size = IO_MAX_PAGE_SIZE;
	}
    }
  else
    {
      desired_log_page_size = desired_pagesize;
    }

  client_credential.client_type = BOOT_CLIENT_ADMIN_UTILITY;
  client_credential.client_info = NULL;
  client_credential.db_name = (char *) dbname;
  client_credential.db_user = NULL;
  client_credential.db_password = NULL;
  client_credential.program_name = (char *) program;
  client_credential.login_name = NULL;
  client_credential.host_name = NULL;
  client_credential.process_id = -1;

  db_path_info.db_path = (char *) db_path;
  db_path_info.vol_path = (char *) vol_path;
  db_path_info.log_path = (char *) log_path;
  db_path_info.lob_path = (char *) lob_path;
  db_path_info.db_host = (char *) host_name;
  db_path_info.db_comments = (char *) comments;

  error =
    boot_initialize_client (&client_credential, &db_path_info, (bool) overwrite, addmore_vols_file, npages,
			    (PGLENGTH) desired_pagesize, log_npages, (PGLENGTH) desired_log_page_size, lang_charset);

  if (more_vol_info_file != NULL)
    {
      remove (more_vol_info_file);
    }

  if (error != NO_ERROR)
    {
      db_Connect_status = DB_CONNECTION_STATUS_NOT_CONNECTED;
    }
  else
    {
      db_Connect_status = DB_CONNECTION_STATUS_CONNECTED;
      /* should be part of boot_initialize_client when we figure out what this does */
      install_static_methods ();
    }

  return (error);
}

/*
 * db_add_volume() - Add a volume extension to the database. The addition of
 *       the volume is a system operation that will be either aborted in case
 *       of failure or committed in case of success, independently on the
 *       destiny of the current transaction. The volume becomes immediately
 *       available to other transactions.
 *
 *    return : Error code
 *    ext_path: Directory where the volume extension is created.
 *                    If NULL, is given, it defaults to the system parameter.
 *    ext_name: Name of the volume extension
 *                    If NULL, system generates one like "db".ext"volid" where
 *                    "db" is the database name and "volid" is the volume
 *                    identifier to be assigned to the volume extension.
 *                    Most of the times, NULL is given by the application.
 *    ext_comments: Comments which are included in the volume extension
 *                    header.
 *    ext_npages: Number of pages
 *    ext_purpose: The purpose of the volume extension. One of the following:
 *                  - DB_PERMANENT_DATA_PURPOSE,
 *                  - DB_TEMPORARY_DATA_PURPOSE
 */
int
db_add_volume (const char *ext_path, const char *ext_name, const char *ext_comments, const int ext_npages,
	       const DB_VOLPURPOSE ext_purpose)
{
  DBDEF_VOL_EXT_INFO ext_info;

  ext_info.path = ext_path;
  ext_info.name = ext_name;
  ext_info.comments = ext_comments;
  ext_info.max_npages = ext_npages;
  ext_info.max_writesize_in_sec = 0;
  ext_info.purpose = ext_purpose;
  ext_info.overwrite = false;

  return db_add_volume_ex (&ext_info);
}

/*
 * db_add_volume_ex() - Add a volume extension to the database. The addition of
 *       the volume is a system operation that will be either aborted in case
 *       of failure or committed in case of success, independently on the
 *       destiny of the current transaction. The volume becomes immediately
 *       available to other transactions.
 *
 *    return : Error code
 *    ext_info : volume info
 *
 */
int
db_add_volume_ex (DBDEF_VOL_EXT_INFO * ext_info)
{
  VOLID volid;
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();

  assert (ext_info != NULL);

  if (Au_dba_user != NULL && !au_is_dba_group_member (Au_user))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_DBA_ONLY, 1, "db_add_volume");
      return er_errid ();
    }

  ext_info->overwrite = false;
  volid = boot_add_volume_extension (ext_info);
  if (volid == NULL_VOLID)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return error;
}

/*
 * db_num_volumes() - Find the number of permanent volumes in the database.
 *
 * return : the number of permanent volumes in database.
 */
int
db_num_volumes (void)
{
  int retval;

  CHECK_CONNECT_ZERO ();

  retval = boot_find_number_permanent_volumes ();

  return ((int) retval);
}

/*
 * db_last_volumes() - Find the last id of permanent volumes in the database.
 *
 * return : the number of permanent volumes in database.
 */
int
db_last_volume (void)
{
  int retval;

  CHECK_CONNECT_ZERO ();

  retval = boot_find_last_permanent ();

  return retval;
}

/*
 * db_vol_label() - Find the name of the volume associated with given volid.
 *   return : vol_fullname or NULL in case of failure.
 *   volid(in)       : Permanent volume identifier.
 *                     If NULL_VOLID is given, the total information of all
 *                     volumes is requested.
 *   vol_fullname(out): Address where the name of the volume is placed. The size
 *                     must be at least PATH_MAX (SET AS A SIDE EFFECT)
 */
char *
db_vol_label (int volid, char *vol_fullname)
{
  char *retval;

  CHECK_CONNECT_ZERO_TYPE (char *);

  retval = disk_get_fullname ((VOLID) volid, vol_fullname);

  return (retval);
}

/*
 * db_get_database_name() - Returns a C string containing the name of
 *    the active database.
 * return : name of the currently active database.
 *
 * note : The string is copied and must be freed by the db_string_free()
 *        function when it is no longer required.
 */
char *
db_get_database_name (void)
{
  const char *name = NULL;

  CHECK_CONNECT_NULL ();

  if (strlen (db_Database_name))
    {
      name = ws_copy_string ((const char *) db_Database_name);
    }

  return ((char *) name);
}

/*
 * db_get_database_version() - Returns a C string containing the version of
 *    the active database server.
 * return : release version of the currently active server.
 *
 * note : The string is allocated and must be freed with the db_string_free()
 *        function when it is no longer required.
 */
char *
db_get_database_version (void)
{
  char *name = NULL;
  name = ws_copy_string (rel_build_number ());
  return name;
}

/*
 * db_get_database_comments() - returns a C string containing the comments
 *       field of the database.
 * return : comment string for currently active database.
 *
 * note : This string must be freed with db_string_free() function
 *        when no longer needed.
 */
const char *
db_get_database_comments (void)
{
  char *remarks = NULL;
  const char *comment = NULL;

  CHECK_CONNECT_NULL ();

  remarks = disk_get_remarks (0);
  if (remarks != NULL)
    {
      comment = ws_copy_string (remarks);
      free_and_init (remarks);
    }

  return (comment);
}

int
db_get_client_type (void)
{
  return db_Client_type;
}

void
db_set_client_type (int client_type)
{
  if (client_type > DB_CLIENT_TYPE_MAX || client_type < DB_CLIENT_TYPE_DEFAULT)
    {
      db_Client_type = DB_CLIENT_TYPE_DEFAULT;
    }
  else
    {
      db_Client_type = client_type;
    }
}

void
db_set_preferred_hosts (const char *hosts)
{
  if (db_Preferred_hosts)
    {
      free_and_init (db_Preferred_hosts);
    }

  if (hosts)
    {
      db_Preferred_hosts = strdup (hosts);
    }
}

void
db_set_connect_order (int connect_order)
{
  db_Connect_order = connect_order;
}

void
db_set_max_num_delayed_hosts_lookup (int max_num_delayed_hosts_lookup)
{
  db_Max_num_delayed_hosts_lookup = max_num_delayed_hosts_lookup;
}

int
db_get_max_num_delayed_hosts_lookup (void)
{
  return db_Max_num_delayed_hosts_lookup;
}

int
db_get_delayed_hosts_count (void)
{
  return db_Delayed_hosts_count;
}

void
db_clear_delayed_hosts_count (void)
{
  db_Delayed_hosts_count = 0;
}

/*
 * db_enable_trigger() - enable all trigger actions
 *   return : previous state
 *
 *   NOTE: recommend to be called after db_restart is called
 */
bool
db_enable_trigger (void)
{
  return tr_set_execution_state (true);
}

/*
 * db_disable_trigger() - disable all trigger actions
 *   return :previous state
 *
 *   NOTE: recommend to be called after db_restart is called
 */
bool
db_disable_trigger (void)
{
  return tr_set_execution_state (false);
}

/*
 * db_clear_host_status() - clear db_Host_status_list
 *   return :
 */
void
db_clear_host_status (void)
{
  int i = 0;

  for (i = 0; i < DIM (db_Host_status_list.hostlist); i++)
    {
      db_Host_status_list.hostlist[i].hostname[0] = '\0';
      db_Host_status_list.hostlist[i].status = DB_HS_NORMAL;
    }
  db_Host_status_list.connected_host_status = NULL;
  db_Host_status_list.last_host_idx = -1;

  return;
}

/*
 * db_find_host_status() - Find host status with a given hostname
 *                         in db_Host_status_list
 *   return : host status found
 */
DB_HOST_STATUS *
db_find_host_status (char *hostname)
{
  DB_HOST_STATUS *host_status;
  int i;

  for (i = 0; i <= db_Host_status_list.last_host_idx; i++)
    {
      host_status = &db_Host_status_list.hostlist[i];
      if (strcmp (hostname, host_status->hostname) == 0)
	{
	  return host_status;
	}
    }

  return NULL;
}

/*
 * db_add_host_status() - add host status into db_Host_status_list
 *   return: added host status
 *
 *   hostname(in)       :
 *   status(in)         :
 */
DB_HOST_STATUS *
db_add_host_status (char *hostname, int status)
{
  DB_HOST_STATUS *host_status;
  int idx;

  assert (db_Host_status_list.last_host_idx + 1 < (int) DIM (db_Host_status_list.hostlist));

  idx = ++db_Host_status_list.last_host_idx;
  host_status = &db_Host_status_list.hostlist[idx];

  strncpy (host_status->hostname, hostname, sizeof (host_status->hostname) - 1);
  host_status->status |= status;

  return host_status;
}

/*
 * db_set_host_status() - set host status to given status.
 *      it adds new host status if not exists.
 *      it adds status into existing one if exists.
 *   return :
 *   hostname(in)       :
 *   status(in)         :
 */
void
db_set_host_status (char *hostname, int status)
{
  bool found = false;
  DB_HOST_STATUS *host_status;

  host_status = db_find_host_status (hostname);

  if (host_status != NULL)
    {
      host_status->status |= status;
    }
  else
    {
      db_add_host_status (hostname, status);
    }

  if (status & DB_HS_HA_DELAYED)
    {
      db_Delayed_hosts_count++;
    }

  return;
}

/*
 * db_set_host_connected() - set the currently connected host. if
 *   currently connected host does not exists, add it to host status list.
 *
 *   return :
 *   host_connected(in): hostname that a client is currently connected to

 */
void
db_set_connected_host_status (char *host_connected)
{
  DB_HOST_STATUS *connected_host_status;

  connected_host_status = db_find_host_status (host_connected);
  if (connected_host_status != NULL)
    {
      db_Host_status_list.connected_host_status = connected_host_status;
    }
  else
    {
      db_Host_status_list.connected_host_status = db_add_host_status (host_connected, DB_HS_NORMAL);
    }

  return;
}

/*
 * db_need_reconnect() - check if reconnection is required.
 *   return : whether reconnection is needed or not
 *
 *    NOTE: it checks db_Host_status_list and determines whether to
 *    reconnect or not. if the currently connected host's status matches
 *    DB_HS_RECONNECT_INDICATOR, then reconnection is required.
 *    Also, if any hosts previously attempted to connect to were reported delayed,
 *    then reconnection is required.
 */
bool
db_need_reconnect (void)
{
  int i;
  DB_HOST_STATUS *host_status;

  if (db_does_connected_host_have_status (DB_HS_RECONNECT_INDICATOR))
    {
      return true;
    }

  /* if any previous attempt to connect failed due to HA replication delay */
  for (i = 0; i <= db_Host_status_list.last_host_idx; i++)
    {
      host_status = &db_Host_status_list.hostlist[i];
      if (host_status->status & DB_HS_HA_DELAYED)
	{
	  return true;
	}
    }

  return false;
}

/*
 * db_need_ignore_repl_delay() - check if the current host's replication is delayed.
 *   return : whether to ignore HA delay or not.
 *
 *   NOTE: it checks if the currently connected host is delayed. if it is true,
 *   it means that all the other hosts' replication is also delayed.
 *   Therefore, a client should notify a server that HA replication delay should be
 *   ignored when it is true. (if it is not notified, then the server will keep
 *   resetting the connection)
 */
bool
db_need_ignore_repl_delay (void)
{
  if (db_Host_status_list.connected_host_status != NULL)
    {
      return ((db_Host_status_list.connected_host_status->status & DB_HS_HA_DELAYED) != 0);
    }

  return false;
}


bool
db_does_connected_host_have_status (int status)
{
  if (db_Host_status_list.connected_host_status != NULL)
    {
      if (db_Host_status_list.connected_host_status->status & status)
	{
	  return true;
	}
    }

  return false;
}

/*
 * db_get_host_list_with_given_status()
 *              - get a list of hostnames with a given status.
 *   return : the number of matching hosts
 *   hostlist(in/out): a resulting list of hostnames
 *   status(in): status that a caller is looking for
 */
int
db_get_host_list_with_given_status (char **hostlist, int list_size, int status)
{
  int i, num_hosts = 0;

  for (i = 0; i <= db_Host_status_list.last_host_idx && i < list_size; i++)
    {
      if (db_Host_status_list.hostlist[i].status & status)
	{
	  hostlist[num_hosts++] = db_Host_status_list.hostlist[i].hostname;
	}
    }
  hostlist[num_hosts] = NULL;

  return num_hosts;
}

/*
 * DATABASE ACCESS
 */

/*
 * db_login() -
 * return      : Error code.
 * name(in)    : user name
 * password(in): optional password
 *
 */
int
db_login (const char *name, const char *password)
{
  int retval;

  retval = au_login (name, password, false);

  return (retval);
}

#if !defined(WINDOWS)
/*
 * sigfpe_handler() - The function is registered with the system to handle
 *       the SIGFPE signal. It will call the user function if one was set when
 *       the database was started.
 * return : void
 * sig    : signal number.
 */
void
sigfpe_handler (int sig)
{
  void (*prev_sig) (int);
  /* If the user had a SIGFPE handler, call it */
  if ((prev_sigfpe_handler != SIG_IGN) &&
#if defined(SIG_ERR)
      (prev_sigfpe_handler != SIG_ERR) &&
#endif
#if defined(SIG_HOLD)
      (prev_sigfpe_handler != SIG_HOLD) &&
#endif
      (prev_sigfpe_handler != SIG_DFL))
    {
      (*prev_sigfpe_handler) (sig);
    }
  /* If using reliable signals, the previous handler is this routine because it's been reestablished.  In that case,
   * don't change the value of the user's handler. */
  prev_sig = os_set_signal_handler (SIGFPE, sigfpe_handler);
  if (prev_sig != sigfpe_handler)
    {
      prev_sigfpe_handler = prev_sig;
    }
}
#endif /* !WINDOWS */

/*
 * db_clear_host_connected() -
 */
void
db_clear_host_connected (void)
{
  boot_clear_host_connected ();
}

/*
 * db_restart() - This is the primary interface function for opening a
 *    database. The database must have already been created using the
 *    system defined generator tool.
 *
 * return           : error code.
 * program(in)      : the program name from argv[0]
 * print_version(in): flag to enable printing of an initial herald message
 * volume(in)       : the name of the database (server)
 *
 */
int
db_restart (const char *program, int print_version, const char *volume)
{
  int error = NO_ERROR;
  BOOT_CLIENT_CREDENTIAL client_credential;

  if (program == NULL || volume == NULL)
    {
      error = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
    }
  else
    {
      strncpy (db_Program_name, program, PATH_MAX);
      db_Database_name[0] = '\0';

      /* authorization will need to access the database and call some db_ functions so assume connection will be ok
       * until after boot_restart_client returns */
      db_Connect_status = DB_CONNECTION_STATUS_CONNECTED;

      client_credential.client_type = (BOOT_CLIENT_TYPE) db_Client_type;
      client_credential.client_info = NULL;
      client_credential.db_name = (char *) volume;
      client_credential.db_user = NULL;
      client_credential.db_password = NULL;
      client_credential.program_name = (char *) program;
      client_credential.login_name = NULL;
      client_credential.host_name = NULL;
      client_credential.process_id = -1;
      client_credential.preferred_hosts = db_Preferred_hosts;
      client_credential.connect_order = db_Connect_order;

      error = boot_restart_client (&client_credential);
      if (error != NO_ERROR)
	{
	  db_Connect_status = DB_CONNECTION_STATUS_NOT_CONNECTED;
	}
      else
	{
	  db_Connect_status = DB_CONNECTION_STATUS_CONNECTED;
	  strncpy (db_Database_name, volume, DB_MAX_IDENTIFIER_LENGTH);
	  install_static_methods ();
#if !defined(WINDOWS)
#if defined(SA_MODE) && (defined(LINUX) || defined(x86_SOLARIS))
	  if (!jsp_jvm_is_loaded ())
	    {
	      prev_sigfpe_handler = os_set_signal_handler (SIGFPE, sigfpe_handler);
	    }
#else /* SA_MODE && (LINUX||X86_SOLARIS) */
	  prev_sigfpe_handler = os_set_signal_handler (SIGFPE, sigfpe_handler);
#endif /* SA_MODE && (LINUX||X86_SOLARIS) */
#endif /* !WINDOWS */
	}
    }

  return (error);
}

/*
 * db_restart_ex() - extended db_restart()
 *
 * returns : error code.
 *
 *   program(in) : the program name from argv[0]
 *   db_name(in) : the name of the database (server)
 *   db_user(in) : the database user name
 *   db_password(in) : the password
 *   client_type(in) : DB_CLIENT_TYPE_XXX in db.h
 *   preferred_hosts(in) : DO NOT USE IT, set it to NULL and
 *                         use db_set_preferred_hosts instead
 */
int
db_restart_ex (const char *program, const char *db_name, const char *db_user, const char *db_password,
	       const char *preferred_hosts, int client_type)
{
  int retval;

  retval = au_login (db_user, db_password, false);
  if (retval != NO_ERROR)
    {
      return retval;
    }

  db_set_client_type (client_type);
#if !defined(CS_MODE)
  /* if we're in SERVER_MODE, this is the only place where we can initialize the sessions state module */
  switch (client_type)
    {
    case DB_CLIENT_TYPE_ADMIN_CSQL:
    case DB_CLIENT_TYPE_READ_ONLY_CSQL:
    case DB_CLIENT_TYPE_CSQL:
      session_states_init (NULL);
    default:
      break;
    }
#endif
  /* For backward compatibility. Do not use the parameter, preferred_hosts. A caller is supposed to use
   * db_set_preferred_hosts before db_restart_ex is called. */
  if (preferred_hosts != NULL)
    {
      db_set_preferred_hosts (preferred_hosts);
    }

  return db_restart (program, false, db_name);
}

/*
 * db_shutdown() - This closes a database that was previously restarted.
 * return : error code.
 *
 * note: This will ABORT the current transaction.
 */
int
db_shutdown (void)
{
  int error = NO_ERROR;

  error = boot_shutdown_client (true);
  db_Database_name[0] = '\0';
  db_Connect_status = DB_CONNECTION_STATUS_NOT_CONNECTED;
  db_Program_name[0] = '\0';
#if !defined(WINDOWS)
  (void) os_set_signal_handler (SIGFPE, prev_sigfpe_handler);
#endif
  db_Disable_modifications = 0;

  db_free_execution_plan ();

  return (error);
}

int
db_ping_server (int client_val, int *server_val)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
#if defined (CS_MODE)
  error = net_client_ping_server (client_val, server_val, 5000);
#endif /* CS_MODE */
  return error;
}

/*
 * db_disable_modification - Disable database modification operation
 *   return: error code
 *
 * NOTE: This function will change 'db_Disable_modifications'.
 */
int
db_disable_modification (void)
{
  /* CHECK_CONNECT_ERROR (); */
  db_Disable_modifications++;
  return NO_ERROR;
}

/*
 * db_enable_modification - Enable database modification operation
 *   return: error code
 *
 * NOTE: This function will change 'db_Disable_modifications'.
 */
int
db_enable_modification (void)
{
  /* CHECK_CONNECT_ERROR (); */
  db_Disable_modifications--;
  return NO_ERROR;
}

/*
 * db_end_session - end current session
 *   return: error code
 *
 * NOTE: This function ends the session identified by 'db_Session_id'
 */
int
db_end_session (void)
{
  int retval;

  CHECK_CONNECT_ERROR ();

  retval = csession_end_session (db_get_session_id ());

  return (retval);
}

/*
 * db_get_row_count - get affected row count
 *  return: error code
 *  row_count (out) : row count
 */
int
db_get_row_count (int *row_count)
{
  CHECK_CONNECT_ERROR ();
  return csession_get_row_count (row_count);
}

/*
 * db_get_last_insert_id - get the value of the last updated serial
 *  return: error code
 *  value (out) : the value of the last updated serial
 */
int
db_get_last_insert_id (DB_VALUE * value)
{
  CHECK_CONNECT_ERROR ();
  return csession_get_last_insert_id (value, true);
}


/*
 * db_get_variable () - get the value of a session variable
 * return : error code or NO_ERROR
 * name (in)  : name of the variable
 * value (out): value of the variable
 */
int
db_get_variable (DB_VALUE * name, DB_VALUE * value)
{
  CHECK_CONNECT_ERROR ();
  return csession_get_variable (name, value);
}

/*
 *  TRANSACTION MANAGEMENT
 */

/*
 * db_commit_transaction() - Commits the current transaction.
 *    You must call this function if you want changes to be made permanent.
 * return : error code.
 *
 * note : If you call db_shutdown without calling this function,
 *    the transaction will be aborted and the changes lost.
 */
int
db_commit_transaction (void)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  /* CHECK_MODIFICATION_ERROR (); */

  /* API does not support RETAIN LOCK */
  retval = tran_commit (false);

  return (retval);
}

/*
 * db_abort_transaction() - Abort the current transaction.
 *    This will throw away all changes that have been made since the last call
 *    to db_commit_transaction.
 *    Currently this will invoke a garbage collection because its a
 *    convenient place to test this and is probably what we want anyway.
 *
 * return : error code.
 */
int
db_abort_transaction (void)
{
  int error;

  CHECK_CONNECT_ERROR ();
  /* CHECK_MODIFICATION_ERROR (); */

  error = tran_abort ();

  return (error);
}

/*
 * db_commit_is_needed() - This function can be used to test to see if there
 *    are any dirty objects in the workspace that have not been flushed OR
 *    if there are any objects on the server that have been flushed but have
 *    not been committed during the current transaction.  This could be used
 *    to display a warning message or prompt window in interface utilities
 *    that gives the user a last chance to commit a transaction before
 *    exiting the process.
 *
 * return : non-zero if there objects need to be committed
 *
 */
int
db_commit_is_needed (void)
{
  int retval;

  CHECK_CONNECT_FALSE ();

  retval = (tran_has_updated ())? 1 : 0;

  return (retval);
}

int
db_savepoint_transaction_internal (const char *savepoint_name)
{
  int retval;

  retval = tran_savepoint_internal (savepoint_name, USER_SAVEPOINT);

  return (retval);
}

/*
 * db_savepoint_transaction() - see the note below.
 *
 * returns/side-effects: error code.
 * savepoint_name(in)  : Name of the savepoint
 *
 * note: A savepoint is established for the current transaction, so
 *       that future transaction operations can be rolled back to this
 *       established savepoint. This operation is called a partial
 *       abort (rollback). That is, all database actions affected by
 *       the transaction after the savepoint are "undone", and all
 *       effects of the transaction preceding the savepoint remain. The
 *       transaction can then continue executing other database
 *       statements. It is permissible to abort to the same savepoint
 *       repeatedly within the same transaction.
 *       If the same savepoint name is used in multiple savepoint
 *       declarations within the same transaction, then only the latest
 *       savepoint with that name is available for aborts and the
 *       others are forgotten.
 *       There is no limit on the number of savepoints that a
 *       transaction can have.
 */
int
db_savepoint_transaction (const char *savepoint_name)
{
  int retval;

  CHECK_CONNECT_ERROR ();

  retval = db_savepoint_transaction_internal (savepoint_name);

  return (retval);
}

int
db_abort_to_savepoint_internal (const char *savepoint_name)
{
  int error;

  if (savepoint_name == NULL)
    {
      return db_abort_transaction ();
    }

  error = tran_abort_upto_user_savepoint (savepoint_name);

  return (error);
}

/*
 * db_abort_to_savepoint() - All the effects of the current transaction
 *     after the given savepoint are undone and all effects of the transaction
 *     preceding the given savepoint remain. After the partial abort the
 *     transaction can continue its normal execution as if the
 *     statements that were undone were never executed.
 *
 * return            : error code
 * savepoint_name(in): Name of the savepoint or NULL
 *
 * note: If savepoint_name is NULL, the transaction is aborted.
 */
int
db_abort_to_savepoint (const char *savepoint_name)
{
  int error;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  error = db_abort_to_savepoint_internal (savepoint_name);

  return (error);
}

/*
 * db_set_global_transaction_info() - Set the user information related with
 *     the global transaction. The global transaction identified by the
 *     'global_transaction_id' should exist and should be the value returned by
 *     'db_2pc_start_transaction'. You can use this function to set the longer
 *     format of global transaction identifier such as XID of XA interface.
 *
 * return : error code.
 * global_transaction_id(in): global transaction identifier
 * info(in) : pointer to the user information to be set
 * size(in) : size of the user information to be set
 */
int
db_set_global_transaction_info (int global_transaction_id, void *info, int size)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  if (global_transaction_id <= 0 || info == NULL || size <= 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  retval = tran_set_global_tran_info (global_transaction_id, info, size);

  return (retval);
}

/*
 * db_get_global_transaction_info() - Get the user information of the global
 *     transaction identified by the 'global_transaction_id'. You can use this function to
 *     get the longer format of global transaction identifier such as XID of
 *     XA interface. This function is designed to use if you want to get XID
 *     after calling 'db_2pc_prepared_transactions' to support xa_recover()
 * return : error code.
 *
 * global_transaction_id: global transaction identifier
 * buffer(out):
 *        pointer to the buffer into which the user information is stored
 * size(in)   : size of the buffer
 *
 */
int
db_get_global_transaction_info (int global_transaction_id, void *buffer, int size)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  if (global_transaction_id <= 0 || buffer == NULL || size <= 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  retval = tran_get_global_tran_info (global_transaction_id, buffer, size);

  return (retval);
}

/*
 * db_2pc_start_transaction() - Make current transaction as a part of a global
 *      transaction by assigning a global transaction identifier(global_transaction_id). It is
 *      recommended to call this function just after the end of a transaction
 *     (commit or abort) before executing other works. This function is one way
 *     of getting global_transaction_id of the transaction. The other way is to use
 *     'db_2pc_prepare_to_commit_transaction'. The function
 *     'db_2pc_prepare_transaction' should be used if this function is called.
 *
 * return : return global transaction identifier
 *
 */

int
db_2pc_start_transaction (void)
{
  int global_transaction_id;

  CHECK_CONNECT_MINUSONE ();
  CHECK_MODIFICATION_MINUSONE ();

  global_transaction_id = tran_2pc_start ();

  return (global_transaction_id);
}

/*
 * db_2pc_prepare_transaction() - Prepare the current transaction for
 *     commitment in 2PC. The transaction should be made as a part of a global
 *     transaction before by 'db_2pc_start_transaction', a pair one of this
 *     function. The system promises not to unilaterally abort the transaction.
 *     After this function call, the only API functions that should be executed
 *     are 'db_commit_transaction' & 'db_abort_transaction'.
 *
 * return : error code.
 *
 */
int
db_2pc_prepare_transaction (void)
{
  int retval;

  CHECK_CONNECT_MINUSONE ();
  CHECK_MODIFICATION_MINUSONE ();

  retval = tran_2pc_prepare ();

  return (retval);
}

/*
 * db_2pc_prepared_transactions() - For restart recovery of global transactions
 *     , this function returns gtrids of transactions in prepared state, which
 *     was a part of a global transaction. If the return value is less than the
 *     'size', there's no more transactions to recover.
 * return      : the number of ids copied into 'gtrids[]'
 * gtrids(out) : array into which global transaction identifiers are copied
 * size        : size of 'gtrids[]' array
 */
int
db_2pc_prepared_transactions (int gtrids[], int size)
{
  int count;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  if (gtrids == NULL || size <= 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  count = tran_2pc_recovery_prepared (gtrids, size);

  return (count);
}				/* db_2pc_prepared_transactions() */

/*
 * db_2pc_attach_transaction() - Attaches the user to the transaction that was
 *     the local part of the specified global transaction. The current
 *     transaction is aborted before the attachement takes place. The current
 *     transaction must not be in the middle of a 2PC. It is recommended to
 *     attach a client to a 2PC loose end transaction just after the client
 *     restart or after a commit or abort.
 *
 * return   : error code.
 * global_transaction_id(in): Global transaction identifier.
 */
int
db_2pc_attach_transaction (int global_transaction_id)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  retval = tran_2pc_attach_global_tran (global_transaction_id);

  return (retval);
}

/*
 * db_2pc_prepare_to_commit_transaction() - This function prepares the
 *    transaction identified by "global_transaction_id" for commitment. The system promises
 *    not to unilaterally abort the transaction. After this function call, the
 *    only API functions that should be executed are db_commit_transaction
 *    & db_abort_transaction.
 * return   : error code.
 * global_transaction_id(in): Identifier of the global transaction.
 *
 */
int
db_2pc_prepare_to_commit_transaction (int global_transaction_id)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_MODIFICATION_ERROR ();

  retval = tran_2pc_prepare_global_tran (global_transaction_id);

  return (retval);
}

/*
 * db_set_interrupt: Set or clear a database interruption flags.
 * return : void
 * set(in): Set or clear an interruption
 *
 */
void
db_set_interrupt (int set)
{
  CHECK_CONNECT_VOID ();
  locator_set_sig_interrupt (set);
}

/*
 * db_set_suppress_repl_on_transaction : Suppress writing replication logs during
 *                                       setting the flag on the transaction
 *
 * return : void
 * set(in): Set or clear the flag
 *
 */
int
db_set_suppress_repl_on_transaction (int set)
{
  CHECK_CONNECT_ERROR ();
  return log_set_suppress_repl_on_transaction (set);
}

/*
 * db_checkpoint: Set or clear a database interruption flags.
 * return : void
 * set(in): Set or clear an interruption
 *
 */
int
db_checkpoint (void)
{
  CHECK_CONNECT_ERROR ();
  return log_checkpoint ();
}

/*
 * db_set_lock_timeout() - This sets a timeout on the amount of time to spend
 *    waiting to aquire a lock on an object.  Normally the system will wait
 *    forever for a lock to be granted.  If you enable lock timeouts, you must
 *    be prepared to handle lock failure errors at the return of any function
 *    that performs an operation on a DB_OBJECT. A timeout value of zero
 *    indicates that there is no timeout and the system will return immediately
 *    if the lock cannot be granted.  A positive integer indicates the maximum
 *    number of seconds to wait. A value of -1 indicates an infinite timeout
 *    where the system will wait forever to aquire the lock.  Infinite timeout
 *    is the default behavior.
 *
 * return      : the old timeout value.
 * seconds(in) : the new timeout value
 *
 */
int
db_set_lock_timeout (int seconds)
{
  int retval;

  CHECK_CONNECT_MINUSONE ();
  CHECK_MODIFICATION_MINUSONE ();

  if (seconds > 0)
    {
      retval = tran_reset_wait_times (seconds * 1000);
    }
  else
    {
      retval = tran_reset_wait_times (seconds);
    }


  return (retval);
}

/*
 * db_set_isolation() - Set the isolation level for present and future client
 *     transactions to the given isolation level. It is recommended to set the
 *     isolation level at the beginning of the client transaction. If the
 *     isolation level is set in the middle of the client transaction, some
 *     resources/locks acquired by the current transactions may be released at
 *     this point according to the new isolation level. We say that the
 *     transaction will see the given isolation level from this point on.
 *     However, we should not call the transaction as one of that isolation
 *     level.
 *     For example, if a transaction with TRAN_READ_COMMITTED
 *     is change to TRAN_REPEATABLE_READ, we cannot say that the
 *     transaction has run with the last level of isolation...just that a part
 *     of the transaction was run with that.
 *
 * return        : error code.
 * isolation(in) : new Isolation level.
 */
int
db_set_isolation (DB_TRAN_ISOLATION isolation)
{
  int retval;

  CHECK_CONNECT_MINUSONE ();

  retval = tran_reset_isolation (isolation, TM_TRAN_ASYNC_WS ());
  return (retval);
}

/*
 * db_get_tran_settings() - Retrieve transaction settings.
 * return : none
 * lock_wait(out)      : Transaction lock wait assigned to client transaction
 *                       (Set as a side effect)
 * tran_isolation(out) : Transaction isolation assigned to client transactions
 *                       (Set as a side effect)
 */
void
db_get_tran_settings (int *lock_wait, DB_TRAN_ISOLATION * tran_isolation)
{
  bool dummy;
  int lock_timeout_in_msecs = -1;

  CHECK_CONNECT_VOID ();
  /* API does not support ASYNC WORKSPACE */
  tran_get_tran_settings (&lock_timeout_in_msecs, tran_isolation, &dummy /* async_ws */ );
  if (lock_timeout_in_msecs > 0)
    {
      *lock_wait = lock_timeout_in_msecs / 1000;
    }
  else
    {
      *lock_wait = lock_timeout_in_msecs;
    }
}

/*
 * db_synchronize_cache() - Decache any obsolete objects that were accessed by
 *         the current transaction. This can happen when the client transaction
 *         is not running with TRAN_REPEATABLE_READ isolation level.
 *         That is some of the locks for accessed objects were relesed. CUBRID
 *         tries to synchronize the client cache by decaching accessed objects
 *         that have been updated by other transactions. This is done when
 *         objects are fetched, as part of the fetch notification of
 *         inconsistent objects are collected and brought to the client cache.
 *
 * return : nothing
 *
 */
void
db_synchronize_cache (void)
{
  CHECK_CONNECT_VOID ();

  locator_synch_isolation_incons ();
}

/*
 *  AUTHORIZATION
 */

/*
 * db_find_user() - Returns the database object for a named user if that user
 *                  has been defined to the authorization system.
 * return  : user object
 * name(in): user name
 *
 */

DB_OBJECT *
db_find_user (const char *name)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (name);

  retval = au_find_user (name);
  return (retval);
}

/*
 * db_find_user_to_drop() - find the object by name user for dropping.
 *
 * return  : error code
 * name(in): user name
 * user(out): user object
 *
 */
int
db_find_user_to_drop (const char *name, DB_OBJECT ** user)
{
  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (name);

  return au_find_user_to_drop (name, user);
}

/*
 * db_add_user() - This will add a new user to the database.  Only the DBA can
 *       add users. If the user already exists, its object pointer will be
 *       returned and the exists flag will be set to non-zero. The exists
 *       pointer can be NULL if the caller isn't interested in this value.
 *
 * return     : new user object
 * name(in)   : user name
 * exists(out): pointer to flag, set if user already exists
 *
 */
DB_OBJECT *
db_add_user (const char *name, int *exists)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (name);
  CHECK_MODIFICATION_NULL ();

  retval = au_add_user (name, exists);

  return (retval);
}

/*
 * db_drop_user() - This will remove a user from the database.  Only the DBA
 *    can remove user objects.  You should call this rather than db_drop so
 *    that the internal system tables are updated correctly.
 * return  : error code.
 * user(in): user object pointer
 *
 */
int
db_drop_user (DB_OBJECT * user)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (user);
  CHECK_MODIFICATION_ERROR ();

  retval = au_drop_user (user);

  return (retval);
}

/*
 * db_add_member() - Adds a member to a user/group.  Recall that users and
 *    groups are exactly the same object, groups are just a convenient
 *    nameing convention to refer to users with members. The member will
 *    inherit all privilidges granted to the user either directly or
 *    indirectly.
 *
 * return : error code
 * user(in/out)  : user/group to get the new member
 * member(in/out): member user to add
 *
 */
int
db_add_member (DB_OBJECT * user, DB_OBJECT * member)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (user, member);
  CHECK_MODIFICATION_ERROR ();

  retval = au_add_member (user, member);

  return (retval);
}

/*
 * db_drop_member() - removes a member from a user/group.  The removed member
 *    loses all privilidges that were inherted directely or indirectely from
 *    the group.
 *
 * return : error code
 * user(in/out)  : user/group that needs member removed
 * member(in/out): member to remove
 *
 */

int
db_drop_member (DB_OBJECT * user, DB_OBJECT * member)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (user, member);
  CHECK_MODIFICATION_ERROR ();

  retval = au_drop_member (user, member);

  return (retval);
}

/*
 * db_set_password() - This is used to change the password string for a user
 *    object. The current password must be provided and match correctly before
 *    the new one is assigned.
 * return    : Error code
 * user(out) : user object
 * old_passwd(in) : the old password
 * new_passwd(in) : the new password
 *
 */
int
db_set_password (DB_OBJECT * user, const char *old_passwd, const char *new_passwd)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (user);
  CHECK_MODIFICATION_ERROR ();

  /* should check old password ! */
  retval = au_set_password (user, new_passwd);

  return (retval);
}

/*
 * db_set_user_comment() - This is used to set the comment string for a user.
 * return    : Error code
 * user(out) : user object
 * comment(in) : a comment string
 *
 */
int
db_set_user_comment (DB_OBJECT * user, const char *comment)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (user);
  CHECK_MODIFICATION_ERROR ();

  retval = au_set_user_comment (user, comment);

  return (retval);
}

/*
 * db_grant() -This is the basic mechanism for passing permissions to other
 *    users.  The authorization type is one of the numeric values defined
 *    by the DB_AUTH enumeration.  If more than one authorization is to
 *    be granted, the values in DB_AUTH can be combined using the C bitwise
 *    "or" operator |.  Errors are likely if the currently logged in user
 *    was not the owner of the class and was not given the grant_option for
 *    the desired authorization types.
 * return  : error code
 * user(in)         : a user object
 * class(in)        : a class object
 * auth(in)         : an authorization type
 * grant_option(in) : true if the grant option is to be added
 *
 */
int
db_grant (MOP user, MOP class_, AU_TYPE auth, int grant_option)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (user, class_);
  CHECK_MODIFICATION_ERROR ();

  retval = do_check_partitioned_class (class_, CHECK_PARTITION_SUBS, NULL);
  if (!retval)
    {
      retval = au_grant (user, class_, auth, (bool) grant_option);
    }

  return (retval);
}

/*
 * db_revoke() - This is the basic mechanism for revoking previously granted
 *    authorizations.  A prior authorization must have been made.
 * returns  : error code
 * user(in) : a user object
 * class_mop(in): a class object
 * auth(in) : the authorization type(s) to revoke
 *
 */
int
db_revoke (MOP user, MOP class_mop, AU_TYPE auth)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (user, class_mop);
  CHECK_MODIFICATION_ERROR ();

  retval = do_check_partitioned_class (class_mop, CHECK_PARTITION_SUBS, NULL);
  if (!retval)
    {
      retval = au_revoke (user, class_mop, auth);
    }

  return (retval);
}

/*
 * db_check_authorization() - This will check to see if a particular
 *    authorization is available for a class.  An error will be returned
 *    if the authorization was not granted.
 * return  : error status
 * op(in)  : class or instance object
 * auth(in): authorization type
 *
 */
int
db_check_authorization (MOP op, DB_AUTH auth)
{
  SM_CLASS *class_;
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (op);

  /* should try to get a write lock on the class if the authorization type is AU_ALTER or AU_INDEX ? */

  retval = (au_fetch_class (op, &class_, AU_FETCH_READ, auth));

  return (retval);
}

/*
 * db_check_authorization() - same as db_check_authorization but also checks
 *    for the grant option.
 * return  : error status
 * op(in)  : class or instance object
 * auth(in): authorization type
 *
 */
int
db_check_authorization_and_grant_option (MOP op, DB_AUTH auth)
{
  SM_CLASS *class_;
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (op);

  retval = (au_fetch_class (op, &class_, AU_FETCH_READ, (DB_AUTH) (auth | (auth << AU_GRANT_SHIFT))));

  return (retval);
}

/*
 * db_get_owner() - returns the user object that owns the class.
 * return   : owner object
 * class(in): class object
 *
 */
DB_OBJECT *
db_get_owner (DB_OBJECT * class_obj)
{
  DB_OBJECT *retval;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (class_obj);

  retval = au_get_class_owner (class_obj);
  return (retval);
}

/*
 * db_get_user_name() - This returns the name of the user that is currently
 *    logged in. It simply makes a copy of the authorization name buffer.
 *    The returned string must later be freed with db_string_free.
 *
 * return : name of current user
 */
char *
db_get_user_name (void)
{
  const char *name;

  CHECK_CONNECT_NULL ();

  /* Kludge, twiddle the constness of this thing.  It probably doesn't need to be const anyway, its just a copy of the
   * attribute value. */
  name = au_user_name ();

  return ((char *) name);
}

/*
 * db_get_user_and_host_name() - This returns the name of the user that is
 *    currently logged in and the host name. Format for return value is
 *    user_name@host_name
 *
 * return : user and host name
 */
char *
db_get_user_and_host_name (void)
{
  char *user = NULL;
  char *username = NULL;
  char hostname[MAXHOSTNAMELEN];
  int len;

  if (GETHOSTNAME (hostname, MAXHOSTNAMELEN) != 0)
    {
      return NULL;
    }

  username = db_get_user_name ();
  if (!username)
    {
      return NULL;
    }

  len = strlen (hostname) + strlen (username) + 2;
  user = (char *) db_private_alloc (NULL, len);
  if (!user)
    {
      db_string_free (username);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) len);
      return 0;
    }

  strcpy (user, username);
  strcat (user, "@");
  strcat (user, hostname);
  db_string_free (username);

  return user;
}

/*
 * db_get_user() - This returns the user object of the current user. If no user
 *    has been logged in, it returns NULL. No error is set if NULL is returned,
 *    it simply means that there is no active user.
 * return : name of current user
 */
DB_OBJECT *
db_get_user (void)
{
  return Au_user;
}

/*
 * db_print_stats() - Debugging function for printing misc database statistics.
 * return : void.
 *
 */
void
db_print_stats (void)
{
  ws_dump (stdout);
}

/*
 * db_lock_read() - This function attempts to secure a read lock on a
 *    particular object.  If the lock could not be obtained an error is
 *    returned.  This should be used whenever there are a sequence of read
 *    operations to be performed on an object.  This allows the implementor
 *    to check once for object availablity and then assume that subsequent
 *    references will always succeed in locking.
 * return : error code
 * op(in) : object pointer
 */
int
db_lock_read (DB_OBJECT * op)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (op);

  retval = (obj_lock (op, 0));

  return (retval);
}

/*
 * db_lock_write() - This function attempts to secure a write lock on a
 *    partiular object.If the lock could not be obtained, an error is returned.
 *    This should be used whenever a sequence of write or read operations
 *    is to be performed on an object.  This allows the implementor to check
 *    once for object availability and then assume that subsequent references
 *    will always succeed in locking.
 * return : error code
 * op(in) : object pointer
 *
 * note: it is important that if an application knows that update/write
 *    operations will be done to an object that a write lock be obtained BEFORE
 *    obtaining a read lock or performing any operations that obtain read locks
 *    Upgrading a read lock to a write lock incurrs some overhead so it is
 *    beneficial to plan ahead and request the write lock at first.
 */
int
db_lock_write (DB_OBJECT * op)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (op);

  retval = (obj_lock (op, 1));

  return (retval);
}

/*
 * GENERAL INFORMATION FUNCTIONS
 */

/*
 * db_freepgs() - Returns the number of free pages on a particular database
 *    volume.  The semantics of this function are a bit unclear currently
 *    since support for multiple database volumes is still being defined.
 * return     : the number of free pages
 * volume_label(in) : volume label name
 */
int
db_freepgs (const char *volume_label)
{
  int retval;

  CHECK_CONNECT_ZERO ();

  retval = ((int) disk_get_free_numpages (NULL_VOLID));

  return (retval);
}

/*
 * db_totalpgs() - Returns the total number of pages on a volume. The semantics
 *    of this function are unclear as support for multiple database volumes is
 *    still being defined.
 * return     : the number of total pages on a volume
 * volume_label(in) : volume name
 */
int
db_totalpgs (const char *volume_label)
{
  int retval;

  CHECK_CONNECT_ZERO ();

  retval = ((int) disk_get_total_numpages (NULL_VOLID));

  return (retval);
}

/*
 * ERROR INTERFACE
 */

const char *
db_error_string_test (int level)
{
  const char *retval;

  retval = db_error_string (level);
  return (retval);
}

/*
 * db_error_string() - This is used to get a string describing the LAST error
 *    that was detected in the database system.
 *    Whenever a db_ function returns an negative error code or when they
 *    return results such as NULL pointers that indicate error, the error will
 *    have been stored in a global structure that can be examined to find out
 *    more about the error.
 *    The string returned will be overwritten when the next error is detected
 *    so it may be necessary to copy it to a private area if you need to keep
 *    it for some lenght of time.  The level parameter controls the amount of
 *    information to be included in the message, level 1 is for short messages,
 *    level 3 is for longer error messages.  Not all error conditions
 *    (few in fact) have level 3 descriptions, all have level 1 descriptions.
 *    If you ask for level 3 and there is no description present, you will be
 *    returned the description at the next highest level.
 * return    : string containing description of the error
 * level(in) : level of description
 */
const char *
db_error_string (int level)
{
  /* this can be called when the database is not started */
  return er_msg ();
}

int
db_error_code_test (void)
{
  int retval;

  retval = db_error_code ();
  return (retval);
}

/*
 * db_error_code() - This is used to get an integer code identifying the LAST
 *    error that was detected by the database.  See the description under
 *    db_error_string for more information on how error descriptions are
 *    maintained.  Normally, an application would use db_error_string
 *    to display error messages to the user.  It may be useful in some
 *    cases to let an application examine the error code and conditionalize
 *    execution to handle a particular event.  In these cases, this function
 *    can be used to get the error code.
 * return : a error code constant
 */
int
db_error_code (void)
{
  int retval;

  /* can be called when the database is not started */
  retval = ((int) er_errid ());
  return (retval);
}

/*
 * db_error_init() - This is used to initialize the output log file for
 *    database error messages.  It should be used by tools that wish to
 *    redirect error messages from the console.
 *    If this routine is not called, all errors and warnings are printed
 *    to stderr.  This should be called Immediately after a database restart
 *    and must be called again after a shutdown/restart sequence
 *    (i.e. the specified file is closed) when the database is shut down).
 * return : true if successful, false if not successful
 * logfile(in) : pathname for log messages.
 */
int
db_error_init (const char *logfile)
{
  /* can be called when the database is not started */
  er_init (logfile, prm_get_integer_value (PRM_ID_ER_EXIT_ASK));
  return (1);
}

/*
 *
 * db_register_error_loghandler () - This function registers user supplied
 * error log handler function (db_error_log_handler_t type) which is called
 * whenever DBMS error message is to be logged.
 *
 * return : previously registered error log handler function (may be NULL)
 * f (in) : user supplied error log handler function
 *
 */

db_error_log_handler_t
db_register_error_log_handler (db_error_log_handler_t f)
{
  return (db_error_log_handler_t) er_register_log_handler ((er_log_handler_t) f);
}

/*
 *  CLUSTER FETCH FUNCTIONS
 */

/*
 * db_fetch_array() -
 *    This function can be used to lock and fetch a collection of objects in a
 *    single call to the server. This is provided only for performance reasons.
 *    The objarray is a user allocated array of object pointers.  The last
 *    entry in the array must be NULL.
 *    The purpose argument specifies for what type of operation the objects
 *    are needed. Depending upon the operation the system will select the
 *    needed lock.
 *    If quit_on_error is zero, an attempt will be made to fetch all of the
 *    objects in the array.  If one of the objects could not be fetched with
 *    the indicated lock, it will be ignored.  In this case, an error code
 *    will be returned only if there was another system error such as
 *    unilateral abort due to a deadlock detection.
 *    If quit_on_error is non-zero, the operation will stop the first time a
 *    lock cannot be obtained on any object.  The lock error will then be
 *    returned by this function.
 * return : error condition
 * objects(out)     : array of object pointers (NULL terminated)
 * purpose(in)      : fetch purpose
 * quit_on_error(in): non-zero if operation quits at first error
 */

int
db_fetch_array (DB_OBJECT ** objects, DB_FETCH_MODE purpose, int quit_on_error)
{
  int error = NO_ERROR;
  int count = 0;
  MOBJ obj;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (objects);

  if (purpose != DB_FETCH_READ && purpose != DB_FETCH_WRITE)
    {
      purpose = DB_FETCH_READ;
    }

  for (count = 0; objects[count] != NULL; count++)
    {
      ;
    }

  obj = locator_fetch_set (count, objects, purpose, DB_FETCH_READ, quit_on_error);
  if (obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  return (error);
}

/*
 * db_fetch_list() - This is a list interface to db_fetch_array.  It simply
 *    constructs an array of objects pointers given an object list.
 * return      : error code
 * objects(in/out)  : object list
 * purpose(in)      : fetch purpose
 * quit_on_error(in): non-zero if operation quits after first error
 *
 */
int
db_fetch_list (DB_OBJLIST * objects, DB_FETCH_MODE purpose, int quit_on_error)
{
  int error = NO_ERROR;
  DB_OBJECT **object_array;
  DB_OBJLIST *object_list;
  int len, i;

  len = db_list_length ((DB_LIST *) objects);
  if (len)
    {
      object_array = (DB_OBJECT **) malloc (sizeof (DB_OBJECT *) * (len + 1));
      if (object_array != NULL)
	{
	  for (object_list = objects, i = 0; object_list != NULL; object_list = object_list->next, i++)
	    {
	      object_array[i] = object_list->op;
	    }
	  object_array[len] = NULL;
	  error = db_fetch_array (object_array, purpose, quit_on_error);

	  /* make sure we don't leave any GC roots around */
	  for (i = 0; i < len; i++)
	    object_array[i] = NULL;
	  free_and_init (object_array);
	}
    }
  return (error);
}

/*
 * fetch_set_internal() -
 *    This is used to fetch all of the objects contained in a set
 *    in a single call to the server.  It is a convenience function that
 *    behaves similar to db_fetch_array().
 *    If quit_on_error is zero, an attempt will be made to fetch all of the
 *    objects in the array.  If one of the objects could not be fetched with
 *    the indicated lock, it will be ignored.  In this case, an error code will
 *    be returned only if there was another system error such as unilateral
 *    abort due to a deadlock detection.
 *    If quit_on_error is non-zero, the operation will stop the first time a
 *    lock cannot be obtained on any object.  The lock error will then be
 *    returned by this function.
 * return : error code
 * set(in/out) : a set object
 * purpose(in) : fetch purpose
 * quit_on_error(in): non-zero if operation quits after first error
 */
static int
fetch_set_internal (DB_SET * set, DB_FETCH_MODE purpose, int quit_on_error)
{
  int error = NO_ERROR;
  DB_VALUE value;
  MOBJ obj;
  int max, cnt, i;
  DB_OBJECT **mops;
  size_t buf_size;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (set);

  if (purpose != DB_FETCH_READ && purpose != DB_FETCH_WRITE)
    {
      purpose = DB_FETCH_READ;
    }

  max = set_size (set);
  if (max)
    {
      buf_size = (max + 1) * sizeof (DB_OBJECT *);
      mops = (DB_OBJECT **) malloc (buf_size);
      if (mops == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	  return error;
	}
      cnt = 0;

      for (i = 0; i < max && error == NO_ERROR; i++)
	{
	  error = set_get_element (set, i, &value);
	  if (error == NO_ERROR)
	    {
	      if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT && DB_GET_OBJECT (&value) != NULL)
		{
		  mops[cnt] = DB_GET_OBJECT (&value);
		  cnt++;
		}
	      db_value_clear (&value);
	    }
	}
      mops[cnt] = NULL;
      if (error == NO_ERROR && cnt)
	{
	  obj = locator_fetch_set (cnt, mops, purpose, DB_FETCH_READ, quit_on_error);
	  if (obj == NULL)
	    {
	      error = er_errid ();
	    }
	}

      for (i = 0; i < max; i++)
	mops[i] = NULL;

      free_and_init (mops);
    }

  return (error);
}

/*
 * db_fetch_set() - see the function fetch_set_internal()
 * return : error code
 * set(in/out) : a set object
 * purpose(in) : fetch purpose
 * quit_on_error(in): non-zero if operation quits after first error
 */
int
db_fetch_set (DB_SET * set, DB_FETCH_MODE purpose, int quit_on_error)
{
  int retval;

  retval = (fetch_set_internal (set, purpose, quit_on_error));
  return (retval);
}

/*
 * db_fetch_seq() - see the function fetch_set_internal()
 * return : error code
 * seq(in/out) : a seq object
 * purpose(in) : fetch purpose
 * quit_on_error(in): non-zero if operation quits after first error
 */
int
db_fetch_seq (DB_SEQ * seq, DB_FETCH_MODE purpose, int quit_on_error)
{
  int retval;

  retval = (fetch_set_internal ((DB_SET *) seq, purpose, quit_on_error));
  return (retval);
}

/*
 * db_fetch_composition() -
 *    This function locks and fetches a composition hierarchy in a single
 *    call to the server.  It is provided for performance only.
 * return : error code
 * object(in/out)    : object that is the root of a composition hierarchy
 * purpose(in)       : fetch purpose
 * max_level(in)     : maximum composition depth
 * quit_on_error(in) : non-zero if operation quits after first error
 */
int
db_fetch_composition (DB_OBJECT * object, DB_FETCH_MODE purpose, int max_level, int quit_on_error)
{
  int error = NO_ERROR;
  MOBJ obj = NULL;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (object);

  if (purpose != DB_FETCH_READ && purpose != DB_FETCH_WRITE)
    {
      purpose = DB_FETCH_READ;
    }

  object = db_real_instance (object);
  if (object != NULL && !WS_ISVID (object))
    {
      obj = locator_fetch_nested (object, purpose, max_level, quit_on_error);
    }
  else
    {
      /* we don't do proxies, but probably should. This protects locator_fetch from virtual db_objects */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
    }
  if (obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return (error);
}

/*
 *  MISC UTILITIES
 */

/*
 * db_warnspace() - Enables low storage warning messages for a particular
 *    database volume.  If enabled, when the number of free pages on a volume
 *    falls below a certain threshold, warning messages will be sent to the
 *    log or console.
 *    This function currently has no effect.
 * volume_label(in) : volume name
 */
void
db_warnspace (const char *volume_label)
{
  CHECK_CONNECT_VOID ();

#if 0
  if (volume_label == NULL)
    {
      dk_warnspace (0);
    }
  else
    {
      dk_warnspace (fileio_find_volume_id_with_label (volume_label));
    }
#endif
}

/*
 * db_preload_classes() - This can be used to preload the methods for a group
 *    of classes at one time thereby avoiding repeated dynamic links as the
 *    classes are fetched on demand.
 * return    : void
 * names(in) : a "varargs" list of names terminated with NULL
 */
void
db_preload_classes (const char *name1, ...)
{
  va_list variable_argument;
  DB_OBJLIST *classes = NULL;
  const char *name;
  MOP op;

  CHECK_CONNECT_VOID ();

  va_start (variable_argument, name1);

  name = name1;
  while (name != NULL)
    {
      op = sm_find_class (name);
      if (op != NULL)
	{
	  if (ml_add (&classes, op, NULL))
	    {
	      return;		/* memory error, abort */
	    }
	}
      name = va_arg (variable_argument, const char *);
    }
  va_end (variable_argument);

  sm_prelink_methods (classes);
}

/*
 * db_link_static_methods() - This is used to specify the implementation
 *    functions for methods that are statically linked with the application
 *    program and do not need to be dynamically linked.  The method link
 *    strucutre contains a string that must match the implementation name for
 *    a method and a function pointer that points to the actual function to be
 *    used when the method is invoked.  You can call this function any number
 *    of times.  The actual linkage information is kept in a global table and
 *    will persist between restart/shutdown sequences of the database.
 * return : void
 * methods(in) : an array of method link structures
 *
 * note : This is one of the very few db_ functions that can be called before
 *    a database is opened with db_restart.
 *    If an entry for a particular function name was already made, it will
 *    be replaced with the new function pointer.
 */
void
db_link_static_methods (DB_METHOD_LINK * methods)
{
  DB_METHOD_LINK *method_link;

  if (methods != NULL)
    {
      for (method_link = methods; method_link->method != NULL; method_link++)
	sm_add_static_method (method_link->method, method_link->function);
    }
}

/*
 * db_unlink_static_methods() - This can be used to remove static linking
 *    information for methods that were previously defined with
 *    db_link_static_methods.
 *    It not usually necessary to call this function.  The static method
 *    definitions generally persist for the duration of the application
 *    program.
 * return      : void
 * methods(in) : an array of method link structures
 */

void
db_unlink_static_methods (DB_METHOD_LINK * methods)
{
  DB_METHOD_LINK *method_link;

  if (methods != NULL)
    {
      for (method_link = methods; method_link->method != NULL; method_link++)
	sm_delete_static_method (method_link->method);
    }
}

/*
 * db_flush_static_methods() - Removes ALL static link information from the
 *    system.  This could be used prior to database termination to make sure
 *    that all system resources have been cleaned up.
 * return : void
 */
void
db_flush_static_methods (void)
{
  sm_flush_static_methods ();
}

/*
 * db_force_method_reload() - This is strictly a debugging function that can
 *    be used to force the dynamic linker to reload the methods for a class.
 *    This would be done if the methods has already been linked, and a change
 *    was made to the external .o file.
 *    This probably should not be an exported db_ function.
 * return   : void
 * obj(in)  : class or instance
 */
void
db_force_method_reload (MOP obj)
{
  CHECK_CONNECT_VOID ();

  /* handles NULL */
  sm_force_method_link (obj);

}

/*
 * db_string_free() - Used to free strings that have been returned from any of
 *    the db_ functions.
 * return    : void
 * string(in): a string that was allocated within the workspace
 *
 */
void
db_string_free (char *string)
{
  /* don't check connection here, we always allow things to be freed */
  if (string != NULL)
    {
      db_ws_free (string);
    }
}

/*
 * db_objlist_free() - free an object list that was returned by one of the
 *    other db_ functions.
 * returns/side-effects: none
 * list(in): an object list
 */
void
db_objlist_free (DB_OBJLIST * list)
{
  /* don't check connection here, we always allow things to be freed */
  /* the list must have been allocated with the ml_ext functions */
  if (list != NULL)
    {
      ml_ext_free (list);
    }
}

/*
 * db_identifier() - This function returns the permanent object identifier of
 *    the given object.
 * return : Pointer to object identifier
 * obj(in): Object
 */
DB_IDENTIFIER *
db_identifier (DB_OBJECT * obj)
{
  return ws_identifier_with_check (obj, true);
}

/*
 * db_object() - This function returns the object (MOP) associated with given
 *    permanent object identifier.
 * return : Pointer to Object
 * oid(in): Permanent Object Identifier
 */
DB_OBJECT *
db_object (DB_IDENTIFIER * oid)
{
  DB_OBJECT *retval;

  retval = ws_mop (oid, NULL);

  return (retval);
}

/*
 * db_chn() - This function finds the cache coherency number of the given
 *    object.
 *    The cache coherency number of an object changes (increased by one) when
 *    the object is flushed after the object is altered.
 * return : cache coherecy number
 * obj(in): Object
 * purpose(in): fetch purpose
 */
int
db_chn (DB_OBJECT * obj, DB_FETCH_MODE purpose)
{
  int chn;

#if 0				/* currently, unused */
  if (purpose != DB_FETCH_READ && purpose != DB_FETCH_WRITE)
    purpose = DB_FETCH_READ;
#endif

  chn = locator_get_cache_coherency_number (obj);	/* with DB_FETCH_READ */

  return chn;
}

/*
 * db_set_system_parameters () - set new values to system parameters
 *
 * return    : error code
 * data (in) : string with new parameter values defined as:
 *	       "param1=new_val1; param2=new_val2; ..."
 */
int
db_set_system_parameters (const char *data)
{
  int rc;
  int error = NO_ERROR;
  SYSPRM_ASSIGN_VALUE *assignments = NULL;

  /* validate changes */
  rc = sysprm_validate_change_parameters (data, true, &assignments);
  /* If a server parameter is changed, user must belong to DBA group */
  if (rc == PRM_ERR_NOT_FOR_CLIENT && Au_dba_user != NULL && !au_is_dba_group_member (Au_user))
    {
      /* user is not authorized to do the changes */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_DBA_ONLY, 1, "db_set_system_parameters");
      error = ER_AU_DBA_ONLY;
      goto cleanup;
    }
  if (rc == PRM_ERR_NOT_FOR_CLIENT || rc == PRM_ERR_NOT_FOR_CLIENT_NO_AUTH)
    {
      /* set system parameters on server */
      rc = sysprm_change_server_parameters (assignments);
    }
  if (rc == PRM_ERR_NO_ERROR)
    {
      /* values were successfully set on server, set them on client too */
      sysprm_change_parameter_values (assignments, true, true);
    }

  /* convert SYSPRM_ERR to error code */
  error = sysprm_set_error (rc, data);

cleanup:
  /* clean up */
  sysprm_free_assign_values (&assignments);
  return error;
}

/*
 * db_set_system_parameters_for_ha_repl () - set new values for system
 *					     parameters for HA replication
 *
 * return    : error code
 * data (in) : string with new parameter values defined as:
 *	       "param1=new_val1; param2=new_val2; ..."
 */
int
db_set_system_parameters_for_ha_repl (const char *data)
{
  return db_set_system_parameters (data);
}

/*
 * db_reset_system_parameters_from_assignments () - reset system parameter
 *	values from a string containing list of assignments
 *
 * return    : error code
 * data (in) : string with new parameter values defined as:
 *	       "param1=new_val1; param2=new_val2; ..."
 *
 */
int
db_reset_system_parameters_from_assignments (const char *data)
{
  int rc;
  int error = NO_ERROR;
  char buf[LINE_MAX];

  rc = sysprm_make_default_values (data, buf, sizeof (buf));
  if (rc == PRM_ERR_NO_ERROR)
    {
      return db_set_system_parameters (buf);
    }

  error = sysprm_set_error (rc, data);

  return error;
}

/*
 * db_get_system_parameters () - get system parameters values.
 *
 * return	 : error code.
 * data (in/out) : data (in) is a list of system parameters to get with next
 *		   format: "param1; param2; ...".
 *		   data (out) is a list of system parameters and their values
 *		   with the format: "param1=val1;param2=val2..."
 * len (in)	 : maximum allowed size of output.
 */
int
db_get_system_parameters (char *data, int len)
{
  int rc;
  int error = NO_ERROR;
  SYSPRM_ASSIGN_VALUE *prm_values = NULL;

  er_clear ();

  /* parse data and obtain the parameters required to print */
  rc = sysprm_obtain_parameters (data, &prm_values);
  if (rc == PRM_ERR_NOT_FOR_CLIENT)
    {
      /* obtain parameter values from server */
      rc = sysprm_obtain_server_parameters (&prm_values);
    }

  /* convert SYSPRM_ERR to error code */
  error = sysprm_set_error (rc, data);

  if (error == NO_ERROR)
    {
      /* print parameter values in data */
      memset (data, 0, len);
      (void) sysprm_print_assign_values (prm_values, data, len);
    }

  /* clean up */
  sysprm_free_assign_values (&prm_values);
  return error;
}

/*
 * db_get_host_connected() - return the host name connected
 * return : host name or NULL
 */
char *
db_get_host_connected (void)
{
  /* CHECK_CONNECT_NULL (); */

#if defined(CS_MODE)
  return boot_get_host_connected ();
#else
  return (char *) "localhost";
#endif
}

 /* 
  * db_get_ha_server_state() - get the connected server's HA state
  * return : number defined in HA_SERVER_STATE
  * buffer(out) : buffer where the string of the HA state to be stored.
  * maxlen(in)  : the length of 'buffer' argument.
  */
int
db_get_ha_server_state (char *buffer, int maxlen)
{
  int ha_state;

  CHECK_CONNECT_ERROR ();

#if defined(CS_MODE)
  ha_state = boot_get_ha_server_state ();
#else
  ha_state = -1;
#endif
  if (buffer)
    {
      strncpy (buffer, css_ha_server_state_string (ha_state), maxlen);
    }
  return ha_state;
}

void
db_set_server_session_key (const char *key)
{
  boot_set_server_session_key (key);
}

char *
db_get_server_session_key (void)
{
  return boot_get_server_session_key ();
}

/*
 * db_get_session_id () - get current session id
 * return : session id
 */
SESSION_ID
db_get_session_id (void)
{
  return db_Session_id;
}

/*
 * db_set_session_id () - set current session id
 * return : void
 * session_id (in): session id
 */
void
db_set_session_id (const SESSION_ID session_id)
{
  db_Session_id = session_id;
}

/*
 * db_find_or_create_session - check if current session is still active
 *                               if not, create a new session
 * return error code or NO_ERROR
 * db_user(in)  : 
 * program_name(in)  :
 * Note: This function will check if the current session is active and will
 *	 create a new one if needed and save user access status in server
 */
int
db_find_or_create_session (const char *db_user, const char *program_name)
{
  int err = NO_ERROR;
  SESSION_ID sess_id = db_get_session_id ();
  int row_count = DB_ROW_COUNT_NOT_SET;
  char *server_session_key;
  const char *host_name = boot_get_host_name ();

  server_session_key = db_get_server_session_key ();
  /* server_session_key is in/out parameter, it is replaced new key */
  err = csession_find_or_create_session (&sess_id, &row_count, server_session_key, db_user, host_name, program_name);
  if (err != NO_ERROR)
    {
      db_set_session_id (DB_EMPTY_SESSION);
      db_update_row_count_cache (DB_ROW_COUNT_NOT_SET);
      return err;
    }

  db_set_session_id (sess_id);
  db_update_row_count_cache (row_count);

  return NO_ERROR;
}

/*
 * db_get_row_count_cache () - get the cached value for row count
 * return : row count
 */
int
db_get_row_count_cache (void)
{
  return db_Row_count;
}

/*
* db_update_row_count_cache () - update the cached value of row count
* return : void
*/
void
db_update_row_count_cache (const int row_count)
{
  db_Row_count = row_count;
}
