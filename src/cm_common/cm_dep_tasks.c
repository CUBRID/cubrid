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
 * cm_dep_tasks.c -
 */

#if defined(WINDOWS)
#include <process.h>
#include <io.h>
#else
#include <sys/shm.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "cm_dep.h"
#include "cm_utils.h"
#include "cm_defines.h"
#include "cm_portable.h"
#include "cm_execute_sa.h"
#include "cm_defines.h"
#include "perf_monitor.h"
#include "dbi.h"
#include "utility.h"
#include "environment_variable.h"
#include "cm_stat.h"
#include "intl_support.h"
#include "language_support.h"
#include "system_parameter.h"

extern int set_size (DB_COLLECTION * set);
extern int set_get_element (DB_COLLECTION * set, int index, DB_VALUE * value);

#if defined(WINDOWS)
#define OP_SERVER_SHM_OPEN(SHM_KEY, HANDLE_PTR)            \
        op_server_shm_open(SHM_KEY, HANDLE_PTR)
#define OP_SERVER_SHM_DETACH(PTR, HMAP)		\
        do {                            \
          if (HMAP != NULL) {           \
            UnmapViewOfFile(PTR);       \
            CloseHandle(HMAP);          \
          }                             \
        } while (0)
static void *op_server_shm_open (int shm_key, HANDLE * hOut);
static void shm_key_to_name (int shm_key, char *name_str);
#else /* WINDOWS */
#define OP_SERVER_SHM_OPEN(SHM_KEY, HANDLE_PTR)            \
        op_server_shm_open(SHM_KEY)
#define OP_SERVER_SHM_DETACH(PTR, HMAP)    shmdt((void*)(PTR))
static void *op_server_shm_open (int shm_key);
#endif /* !WINDOWS */


#define ERROR_MSG_SIZE       (PATH_MAX)
#define DB_RESTART_SERVER_NAME   "emgr_server"
#define DBMT_ERR_MSG_SET(ERR_BUF, MSG)	\
	strncpy(ERR_BUF, MSG, ERROR_MSG_SIZE - 1)
#define CUBRID_ERR_MSG_SET(ERR_BUF)	\
	DBMT_ERR_MSG_SET(ERR_BUF, db_error_string(1))

#define QUERY_BUFFER_MAX        4096

#if !defined(WINDOWS)
#define STRING_APPEND(buffer_p, avail_size_holder, ...) \
  do {                                                          \
    if (avail_size_holder > 0) {                                \
      int n = snprintf (buffer_p, avail_size_holder, __VA_ARGS__);      \
      if (n > 0)        {                                       \
        if ((size_t) n < avail_size_holder) {                   \
          buffer_p += n; avail_size_holder -= n;                \
        } else {                                                \
          buffer_p += (avail_size_holder - 1);                  \
          avail_size_holder = 0;                                \
        }                                                       \
      }                                                         \
    }                                                           \
  } while (0)
#else /* !WINDOWS */
#define STRING_APPEND(buffer_p, avail_size_holder, ...) \
  do {                                                          \
    if (avail_size_holder > 0) {                                \
      int n = _snprintf (buffer_p, avail_size_holder, __VA_ARGS__);     \
      if (n < 0 || (size_t) n >= avail_size_holder) {           \
        buffer_p += (avail_size_holder - 1);                    \
        avail_size_holder = 0;                                  \
        *buffer_p = '\0';                                       \
      } else {                                                  \
        buffer_p += n; avail_size_holder -= n;                  \
      }                                                         \
    }                                                           \
  } while (0)
#endif /* !WINDOWS */


#define SH_MODE 0644
#define MAX_SERVER_THREAD_COUNT         500

static int get_dbvoldir (char *vol_dir, size_t vol_dir_size, char *dbname);
static int getservershmid (char *dir, char *dbname);

static int user_login_sa (nvplist * out, char *_dbmt_error, char *dbname, char *dbuser, char *dbpasswd);
static int uStringEqualIgnoreCase (const char *str1, const char *str2);
static int read_file (char *filename, char **outbuf);
static int _op_db_login (nvplist * out, nvplist * in, int ha_mode, char *_dbmt_error);
static int _op_get_system_classes_info (nvplist * out, char *_dbmt_error);
static int _op_get_detailed_class_info (nvplist * out, DB_OBJECT * classobj, char *_dbmt_error);
static char *_op_get_type_name (DB_DOMAIN * domain);
static void _op_get_attribute_info (nvplist * out, DB_ATTRIBUTE * attr, int isclass);
static char *_op_get_value_string (DB_VALUE * value);
static void _op_get_method_info (nvplist * out, DB_METHOD * method, int isclass);
static void _op_get_method_file_info (nvplist * out, DB_METHFILE * mfile);
static void _op_get_query_spec_info (nvplist * out, DB_QUERY_SPEC * spec);
static void _op_get_resolution_info (nvplist * out, DB_RESOLUTION * res, int isclass);
static void _op_get_constraint_info (nvplist * out, DB_CONSTRAINT * con);
static void _op_get_class_info (nvplist * out, DB_OBJECT * classobj);
static int _op_get_user_classes_info (nvplist * out, char *_dbmt_error);
static int _op_is_classattribute (DB_ATTRIBUTE * attr, DB_OBJECT * classobj);
static int class_info_sa (const char *dbname, const char *uid, const char *passwd, char *cli_ver_val, nvplist * out,
			  char *_dbmt_error);
static char *_op_get_set_value (DB_VALUE * val);
static int trigger_info_sa (const char *dbname, const char *uid, const char *password, nvplist * res,
			    char *_dbmt_error);
static void op_get_trigger_information (nvplist * res, DB_OBJECT * p_trigger);
static int revoke_all_from_user (DB_OBJECT * user);
static void _op_get_db_user_name (nvplist * res, DB_OBJECT * user);
static void _op_get_db_user_id (nvplist * res, DB_OBJECT * user);
static void _op_get_db_user_groups (nvplist * res, DB_OBJECT * user);
static void _op_get_db_user_authorization (nvplist * res, DB_OBJECT * user);

static T_EMGR_VERSION get_client_version (char *cli_ver_val);

#if defined(WINDOWS)
static void
shm_key_to_name (int shm_key, char *name_str)
{

  sprintf (name_str, "cubrid_shm_%d", shm_key);

}
#endif
#if defined(WINDOWS)
static void *
op_server_shm_open (int shm_key, HANDLE * hOut)
{
  LPVOID lpvMem = NULL;		/* address of shared memory */
  HANDLE hMapObject = NULL;
  char shm_name[64];

  *hOut = NULL;

  shm_key_to_name (shm_key, shm_name);

  hMapObject = OpenFileMapping (FILE_MAP_WRITE,	/* read/write access */
				FALSE,	/* inherit flag */
				shm_name);	/* name of map object */
  if (hMapObject == NULL)
    {
      return NULL;
    }

  /* Get a pointer to the file-mapped shared memory. */
  lpvMem = MapViewOfFile (hMapObject,	/* object to map view of */
			  FILE_MAP_WRITE,	/* read/write access */
			  0,	/* high offset: map from */
			  0,	/* low offset: beginning */
			  0);	/* default: map entire file */
  if (lpvMem == NULL)
    {
      CloseHandle (hMapObject);
      return NULL;
    }

  *hOut = hMapObject;
  return lpvMem;
}
#else
static void *
op_server_shm_open (int shm_key)
{
  int mid;
  void *p;

  if (shm_key < 0)
    {
      return NULL;
    }
  mid = shmget (shm_key, 0, SH_MODE);

  if (mid == -1)
    {
      return NULL;
    }

  p = shmat (mid, (char *) 0, SHM_RDONLY);
  if (p == (void *) -1)
    {
      return NULL;
    }

  return p;
}
#endif



static int
_op_db_login (nvplist * out, nvplist * in, int ha_mode, char *_dbmt_error)
{
  int errcode;
  char *id, *pwd, *db_name;
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];

  id = nv_get_val (in, "_DBID");
  pwd = nv_get_val (in, "_DBPASSWD");
  db_name = nv_get_val (in, "_DBNAME");

  db_login (id, pwd);

  if (ha_mode != 0)
    {
      snprintf (dbname_at_hostname, sizeof (dbname_at_hostname), "%s@127.0.0.1", db_name);
      errcode = db_restart (DB_RESTART_SERVER_NAME, 0, dbname_at_hostname);
    }
  else
    {
      errcode = db_restart (DB_RESTART_SERVER_NAME, 0, db_name);
    }

  if (errcode < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      return -1;
    }

  db_set_isolation (TRAN_READ_COMMITTED);

  db_set_lock_timeout (1);

  return 0;
}

int
cm_get_diag_data (T_CM_DIAG_MONITOR_DB_VALUE * ret_result, char *db_name, char *mon_db)
{
  int i;
  T_SHM_DIAG_INFO_SERVER *server_shm = NULL;
  T_DIAG_MONITOR_DB_VALUE server_result;

#if defined(WINDOWS)
  HANDLE shm_handle = NULL;
#endif

  if (db_name != NULL)
    {
      int server_shmid;
      char vol_dir[1024];

      if (get_dbvoldir (vol_dir, sizeof (vol_dir), db_name) != -1)
	{
	  server_shmid = getservershmid (vol_dir, db_name);
	  if (server_shmid > 0)
	    {
	      server_shm = (T_SHM_DIAG_INFO_SERVER *) OP_SERVER_SHM_OPEN (server_shmid, &shm_handle);
	    }
	}
    }

  if (server_shm && mon_db != NULL && strcmp (mon_db, "yes") == 0)
    {
      int numthread = server_shm->num_thread;
      memset (&server_result, 0, sizeof (server_result));
      for (i = 0; i < MAX_SERVER_THREAD_COUNT && i < numthread; i++)
	{
	  server_result.query_open_page += server_shm->thread[i].query_open_page;
	  server_result.query_opened_page += server_shm->thread[i].query_opened_page;
	  server_result.query_slow_query += server_shm->thread[i].query_slow_query;
	  server_result.query_full_scan += server_shm->thread[i].query_full_scan;
	  server_result.conn_cli_request += server_shm->thread[i].conn_cli_request;
	  server_result.conn_aborted_clients += server_shm->thread[i].conn_aborted_clients;
	  server_result.conn_conn_req += server_shm->thread[i].conn_conn_req;
	  server_result.conn_conn_reject += server_shm->thread[i].conn_conn_reject;
	  server_result.buffer_page_write += server_shm->thread[i].buffer_page_write;
	  server_result.buffer_page_read += server_shm->thread[i].buffer_page_read;
	  server_result.lock_deadlock += server_shm->thread[i].lock_deadlock;
	  server_result.lock_request += server_shm->thread[i].lock_request;
	}

      OP_SERVER_SHM_DETACH (server_shm, shm_handle);

      ret_result->query_open_page = server_result.query_open_page;
      ret_result->query_opened_page = server_result.query_opened_page;
      ret_result->query_slow_query = server_result.query_slow_query;
      ret_result->query_full_scan = server_result.query_full_scan;
      ret_result->lock_deadlock = server_result.lock_deadlock;
      ret_result->lock_request = server_result.lock_request;
      ret_result->conn_cli_request = server_result.conn_cli_request;
      ret_result->conn_aborted_clients = server_result.conn_aborted_clients;
      ret_result->conn_conn_req = server_result.conn_conn_req;
      ret_result->conn_conn_reject = server_result.conn_conn_reject;
      ret_result->buffer_page_write = server_result.buffer_page_write;
      ret_result->buffer_page_read = server_result.buffer_page_read;

      return 0;
    }

  return -1;

}

int
cm_ts_delete_user (nvplist * req, nvplist * res, char *_dbmt_error)
{
  DB_OBJECT *dbuser;
  char *newdbusername = nv_get_val (req, "username");
  int ha_mode = 0;
  char *dbname;
  T_DB_SERVICE_MODE db_mode;

  dbname = nv_get_val (req, "_DBNAME");
  if (dbname == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (_op_db_login (res, req, ha_mode, _dbmt_error) < 0)
    {
      return ERR_WITH_MSG;
    }
  if (db_find_user_to_drop (newdbusername, &dbuser) != NO_ERROR)
    {
      goto error_return;
    }
  if (db_drop_user (dbuser) < 0)
    {
      goto error_return;
    }
  if (db_commit_transaction () < 0)
    {
      goto error_return;
    }
  db_shutdown ();
  return ERR_NO_ERROR;

error_return:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}


int
cm_tsDBMTUserLogin (nvplist * in, nvplist * out, char *_dbmt_error)
{
  int errcode;
  int ha_mode = 0;
  char *targetid, *dbname, *dbuser, *dbpasswd;
  DB_OBJECT *user, *obj;
  DB_VALUE v;
  DB_COLLECTION *col;
  int i;
  bool isdba = false;
  T_DB_SERVICE_MODE db_mode = DB_SERVICE_MODE_NONE;
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];

  targetid = nv_get_val (in, "targetid");
  dbname = nv_get_val (in, "dbname");
  dbuser = nv_get_val (in, "dbuser");
  dbpasswd = nv_get_val (in, "dbpasswd");

  if (dbname == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  nv_add_nvp (out, "targetid", targetid);
  nv_add_nvp (out, "dbname", dbname);

  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      return (user_login_sa (out, _dbmt_error, dbname, dbuser, dbpasswd));
    }

  db_login (dbuser, dbpasswd);

  if (ha_mode != 0)
    {
      snprintf (dbname_at_hostname, sizeof (dbname_at_hostname), "%s@127.0.0.1", dbname);
      errcode = db_restart (DB_RESTART_SERVER_NAME, 0, dbname_at_hostname);
    }
  else
    {
      errcode = db_restart (DB_RESTART_SERVER_NAME, 0, dbname);
    }

  if (errcode < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      return ERR_WITH_MSG;
    }

  if (uStringEqualIgnoreCase (dbuser, "DBA"))
    {
      nv_add_nvp (out, "authority", "isdba");
      db_shutdown ();
      return ERR_NO_ERROR;
    }

  user = db_find_user (dbuser);

  if (user == NULL)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_get (user, "groups", &v);
  col = db_get_set (&v);
  for (i = 0; i < db_set_size (col); i++)
    {
      db_set_get (col, i, &v);
      obj = db_get_object (&v);
      db_get (obj, "name", &v);
      if (uStringEqualIgnoreCase (db_get_string (&v), "DBA"))
	{
	  isdba = true;
	  break;
	}
    }

  if (isdba)
    {
      nv_add_nvp (out, "authority", "isdba");
    }
  else
    {
      nv_add_nvp (out, "authority", "isnotdba");
    }

  db_shutdown ();
  return ERR_NO_ERROR;
}

int
cm_ts_optimizedb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *classname;
  T_DB_SERVICE_MODE db_mode;
  int ha_mode = 0;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  classname = nv_get_val (req, "classname");

  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      char cmd_name[PATH_MAX];
      const char *argv[6];
      int argc = 0;

      (void) envvar_bindir_file (cmd_name, PATH_MAX, UTIL_CUBRID);
      argv[argc++] = cmd_name;
      argv[argc++] = UTIL_OPTION_OPTIMIZEDB;
      if (classname != NULL)
	{
	  argv[argc++] = "--" OPTIMIZE_CLASS_NAME_L;
	  argv[argc++] = classname;
	}
      argv[argc++] = dbname;
      argv[argc++] = NULL;

      if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
	{			/* optimizedb */
	  strcpy (_dbmt_error, argv[0]);
	  return ERR_SYSTEM_CALL;
	}
    }
  else
    {
      char sql[QUERY_BUFFER_MAX];
      DB_QUERY_RESULT *result;
      DB_QUERY_ERROR query_error;

      if (_op_db_login (res, req, ha_mode, _dbmt_error) < 0)
	{
	  return ERR_WITH_MSG;
	}
      if (classname == NULL)
	{
	  strcpy (sql, "UPDATE STATISTICS ON ALL CLASSES");
	}
      else
	{
	  snprintf (sql, sizeof (sql) - 1, "UPDATE STATISTICS ON \"%s\"", classname);
	}
      if (db_execute (sql, &result, &query_error) < 0)
	{
	  goto error_return;
	}
      db_query_end (result);

      if (db_commit_transaction () < 0)
	{
	  goto error_return;
	}
      db_shutdown ();
    }
  return ERR_NO_ERROR;

error_return:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
cm_ts_class_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *dbstatus, *dbname;
  T_DB_SERVICE_MODE db_mode;
  int ha_mode = 0;

  dbstatus = nv_get_val (in, "dbstatus");
  dbname = nv_get_val (in, "_DBNAME");
  if (dbname == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      char *uid = nv_get_val (in, "_DBID");
      char *passwd = nv_get_val (in, "_DBPASSWD");
      char *cli_ver_val = nv_get_val (in, "_CLIENT_VERSION");
      return (class_info_sa (dbname, uid, passwd, cli_ver_val, out, _dbmt_error));
    }
  else
    {
      if (_op_db_login (out, in, ha_mode, _dbmt_error))
	{
	  return ERR_WITH_MSG;
	}
      nv_add_nvp (out, "dbname", dbname);
      if (_op_get_system_classes_info (out, _dbmt_error) < 0)
	{
	  db_shutdown ();
	  return ERR_WITH_MSG;
	}
      if (_op_get_user_classes_info (out, _dbmt_error) < 0)
	{
	  db_shutdown ();
	  return ERR_WITH_MSG;
	}

      if (db_commit_transaction () < 0)
	{
	  CUBRID_ERR_MSG_SET (_dbmt_error);
	  db_shutdown ();
	  return ERR_WITH_MSG;
	}
      db_shutdown ();
    }

  return ERR_NO_ERROR;
}

int
cm_ts_class (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *classname, *dbname;
  DB_OBJECT *classobj;
  int ha_mode = 0;
  T_DB_SERVICE_MODE db_mode;

  dbname = nv_get_val (in, "_DBNAME");
  if (dbname == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (_op_db_login (out, in, ha_mode, _dbmt_error) < 0)
    {
      return ERR_WITH_MSG;
    }
  classname = nv_get_val (in, "classname");
  classobj = db_find_class (classname);

  if (classobj == NULL)
    {
      goto error_return;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (db_commit_transaction () < 0)
    {
      goto error_return;
    }
  db_shutdown ();
  return ERR_NO_ERROR;

error_return:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
cm_ts_get_triggerinfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  DB_OBJLIST *trigger_list, *temp;
  DB_OBJECT *p_trigger;
  int errcode;
  T_DB_SERVICE_MODE db_mode;
  char *dbname;
  int ha_mode = 0;
  trigger_list = NULL;

  dbname = nv_get_val (req, "_DBNAME");
  if (dbname == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      /* use ts_get_triggerinfo_sa funtion */
      char *uid;
      char *password;
      uid = nv_get_val (req, "_DBID");
      password = nv_get_val (req, "_DBPASSWD");
      return (trigger_info_sa (dbname, uid, password, res, _dbmt_error));
    }

  /* cs mode serviece */
  if (_op_db_login (res, req, ha_mode, _dbmt_error) < 0)
    {
      return ERR_WITH_MSG;
    }

  errcode = db_find_all_triggers (&trigger_list);
  if (errcode)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  nv_add_nvp (res, "dbname", dbname);
  nv_add_nvp (res, "open", "triggerlist");

  temp = trigger_list;
  while (temp)
    {
      p_trigger = temp->op;
      nv_add_nvp (res, "open", "triggerinfo");

      /* set trigger information */
      op_get_trigger_information (res, p_trigger);

      nv_add_nvp (res, "close", "triggerinfo");
      temp = temp->next;
    }

  nv_add_nvp (res, "close", "triggerlist");

  db_objlist_free (trigger_list);
  db_shutdown ();

  return ERR_NO_ERROR;
}

int
cm_ts_update_attribute (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *attr_name, *category, *index, *not_null, *unique, *defaultv, *old_attr_name, *dbname;
  DB_OBJECT *classobj;
  DB_ATTRIBUTE *attrobj;
  int errcode, is_class, flag;
  int ha_mode = 0;
  T_DB_SERVICE_MODE db_mode;

  dbname = nv_get_val (in, "_DBNAME");
  if (dbname == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (_op_db_login (out, in, ha_mode, _dbmt_error) < 0)
    {
      return ERR_WITH_MSG;
    }

  class_name = nv_get_val (in, "classname");
  attr_name = nv_get_val (in, "newattributename");
  index = nv_get_val (in, "index");
  not_null = nv_get_val (in, "notnull");
  unique = nv_get_val (in, "unique");
  defaultv = nv_get_val (in, "default");
  old_attr_name = nv_get_val (in, "oldattributename");
  category = nv_get_val (in, "category");
  classobj = db_find_class (class_name);

  if (classobj == NULL)
    {
      snprintf (_dbmt_error, PATH_MAX - 1, "class [%s] not exists.", class_name);
      return ERR_WITH_MSG;
    }

  if ((category != NULL) && (strcmp (category, "instance") == 0))
    {
      is_class = 0;
    }
  else
    {
      is_class = 1;
    }

  if ((attr_name != NULL) && (old_attr_name != NULL) && (strcmp (attr_name, old_attr_name) != 0))
    {
      errcode = db_rename (classobj, old_attr_name, is_class, attr_name);
      if (errcode < 0)
	{
	  goto error_return;
	}
    }

  if (is_class)
    {
      attrobj = db_get_class_attribute (classobj, attr_name);
    }
  else
    {
      attrobj = db_get_attribute (classobj, attr_name);
    }

  flag = db_attribute_is_indexed (attrobj);
  if (index != NULL)
    {
      if (!flag && !strcmp (index, "y"))
	{
	  if (db_add_index (classobj, attr_name) < 0)
	    {
	      goto error_return;
	    }
	}
      else if (flag && !strcmp (index, "n"))
	{
	  if (db_drop_index (classobj, attr_name) < 0)
	    {
	      goto error_return;
	    }
	}
    }

  /* memory struct may be changed. should get attrobj again. */
  if (is_class)
    {
      attrobj = db_get_class_attribute (classobj, attr_name);
    }
  else
    {
      attrobj = db_get_attribute (classobj, attr_name);
    }

  flag = db_attribute_is_non_null (attrobj);
  if (not_null != NULL)
    {
      if (!flag && !strcmp (not_null, "y"))
	{
	  if (db_constrain_non_null (classobj, attr_name, is_class, 1) < 0)
	    {
	      goto error_return;
	    }
	}
      else if (flag && !strcmp (not_null, "n"))
	{
	  if (db_constrain_non_null (classobj, attr_name, is_class, 0) < 0)
	    {
	      goto error_return;
	    }
	}
    }

  /* memory struct may be changed. should get attrobj again. */
  if (is_class)
    {
      attrobj = db_get_class_attribute (classobj, attr_name);
    }
  else
    {
      attrobj = db_get_attribute (classobj, attr_name);
    }

  flag = db_attribute_is_unique (attrobj);
  if (unique != NULL)
    {
      if (!flag && !strcmp (unique, "y"))
	{
	  int errcode;
	  errcode = db_constrain_unique (classobj, attr_name, 1);
	  if (errcode < 0 && errcode != ER_SM_INDEX_EXISTS)
	    {
	      goto error_return;
	    }
	}
      else if (flag && !strcmp (unique, "n"))
	{
	  if (db_constrain_unique (classobj, attr_name, 0) < 0)
	    {
	      goto error_return;
	    }
	}
    }

  /* default value management */
  if (defaultv != NULL)
    {
      char buf[1024];
      DB_QUERY_ERROR error_stats;
      DB_QUERY_RESULT *result;

      if (is_class)
	{
	  snprintf (buf, sizeof (buf) - 1, "ALTER \"%s\" CHANGE CLASS \"%s\" DEFAULT %s", class_name, attr_name,
		    defaultv);
	}
      else
	{
	  snprintf (buf, sizeof (buf) - 1, "ALTER \"%s\" CHANGE \"%s\" DEFAULT %s", class_name, attr_name, defaultv);
	}
      lang_set_parser_use_client_charset (false);
      if (db_execute (buf, &result, &error_stats) < 0)
	{
	  lang_set_parser_use_client_charset (true);
	  goto error_return;
	}
      lang_set_parser_use_client_charset (true);
      db_query_end (result);
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (db_commit_transaction () < 0)
    {
      goto error_return;
    }
  db_shutdown ();
  return ERR_NO_ERROR;

error_return:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
cm_ts_update_user (nvplist * req, nvplist * res, char *_dbmt_error)
{
  int errcode;
  DB_OBJECT *dbuser;
  DB_OBJECT *obj;
  int aset;
  DB_COLLECTION *gset;
  DB_VALUE val, val2;
  int ha_mode = 0;
  T_DB_SERVICE_MODE db_mode;

  const char *db_passwd;
  const char *db_name;
  const char *new_db_user_name;
  const char *new_db_user_pass;

  int i, sect, sect_len;
  char *tval, *sval;
  int anum;

  db_passwd = nv_get_val (req, "_DBPASSWD");
  db_name = nv_get_val (req, "_DBNAME");
  new_db_user_name = nv_get_val (req, "username");
  new_db_user_pass = nv_get_val (req, "userpass");

  if (db_name == NULL)
    {
      return ERR_WITH_MSG;
    }

  db_mode = uDatabaseMode (db_name, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", db_name);
      return ERR_STANDALONE_MODE;
    }

  if (_op_db_login (res, req, ha_mode, _dbmt_error) < 0)
    {
      return ERR_WITH_MSG;
    }
  if ((dbuser = db_find_user (new_db_user_name)) == NULL)
    {
      goto error_return;
    }
  if (new_db_user_pass != NULL)
    {				/* if password need to be changed ... */
      const char *dbmt_db_id;
      const char *old_db_passwd;

      dbmt_db_id = nv_get_val (req, "_DBID");
      if (dbmt_db_id == NULL)
	dbmt_db_id = "";

      if (intl_identifier_casecmp (new_db_user_name, dbmt_db_id) == 0)
	{
	  old_db_passwd = db_passwd;
	}
      else
	{
	  old_db_passwd = NULL;
	}

      /* set new password */
      if (uStringEqual (new_db_user_pass, "__NULL__"))
	{
	  new_db_user_pass = "";
	}
      errcode = db_set_password (dbuser, old_db_passwd, new_db_user_pass);
      if (errcode < 0)
	{
	  goto error_return;
	}
    }

  /* clear existing group - clear group, direct group */
  if (db_get (dbuser, "groups", &val) < 0)
    {
      goto error_return;
    }
  gset = db_get_set (&val);
  for (i = 0; i < db_set_size (gset); i++)
    {
      db_set_get (gset, i, &val2);
      if (db_drop_member (db_get_object (&val2), dbuser) < 0)
	{
	  db_value_clear (&val2);
	  db_value_clear (&val);
	  goto error_return;
	}
      db_value_clear (&val2);
    }
  db_value_clear (&val);
  if (db_get (dbuser, "direct_groups", &val) < 0)
    {
      goto error_return;
    }
  gset = db_get_set (&val);
  for (i = 0; i < db_set_size (gset); i++)
    {
      db_set_get (gset, i, &val2);
      if (db_drop_member (db_get_object (&val2), dbuser) < 0)
	{
	  db_value_clear (&val2);
	  db_value_clear (&val);
	  goto error_return;
	}
      db_value_clear (&val2);
    }
  db_value_clear (&val);

#if 0
  if (db_add_member (db_find_user ("PUBLIC"), dbuser) < 0)
    {
      goto error_return;
    }
#endif

  /* make itself a member of other groups */
  nv_locate (req, "groups", &sect, &sect_len);
  if (sect >= 0)
    {
      for (i = 0; i < sect_len; ++i)
	{
	  nv_lookup (req, sect + i, NULL, &tval);
	  if ((obj = db_find_user (tval)) == NULL)
	    {
	      continue;
	    }
	  if (db_add_member (obj, dbuser) < 0)
	    {
	      goto error_return;
	    }
	}
    }

  /* remove members to this group -- this section is not used. */
  nv_locate (req, "removemembers", &sect, &sect_len);
  if (sect >= 0)
    {
      for (i = 0; i < sect_len; ++i)
	{
	  nv_lookup (req, sect + i, NULL, &tval);
	  if ((obj = db_find_user (tval)) == NULL)
	    {
	      continue;
	    }
	  if (db_drop_member (dbuser, obj) < 0)
	    {
	      goto error_return;
	    }
	}
    }
  /* add members to this group -- this section is not used. */
  nv_locate (req, "addmembers", &sect, &sect_len);
  if (sect >= 0)
    {
      for (i = 0; i < sect_len; ++i)
	{
	  nv_lookup (req, sect + i, NULL, &tval);
	  if ((obj = db_find_user (tval)) == NULL)
	    {
	      continue;
	    }
	  if (db_add_member (dbuser, obj) < 0)
	    {
	      goto error_return;
	    }
	}
    }

  /* add authorization info */
  nv_locate (req, "authorization", &sect, &sect_len);
  if (sect >= 0)
    {
      revoke_all_from_user (dbuser);

      for (i = 0; i < sect_len; ++i)
	{
	  nv_lookup (req, sect + i, &sval, &tval);

	  if ((obj = db_find_class (sval)) == NULL)
	    {
	      continue;
	    }
	  if (db_is_system_class (obj))
	    {
	      continue;
	    }
	  if (tval == NULL)
	    {
	      continue;
	    }

	  anum = atoi (tval);
	  if (anum == 0)
	    {
	      continue;
	    }
	  aset = anum & DB_AUTH_ALL;
	  if (db_grant (dbuser, obj, (DB_AUTH) aset, 0) < 0)
	    {
	      continue;
	    }

	  aset = (anum >> 8) & (DB_AUTH_ALL);
	  if (db_grant (dbuser, obj, (DB_AUTH) aset, 1) < 0)
	    {
	      continue;
	    }
	}
    }

  if (db_commit_transaction () < 0)
    {
      goto error_return;
    }
  db_shutdown ();

  return ERR_NO_ERROR;

error_return:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
cm_ts_create_user (nvplist * req, nvplist * res, char *_dbmt_error)
{
  DB_OBJECT *dbuser;
  DB_OBJECT *obj;
  int exists, aset;
  char *dbname;
  int ha_mode = 0;
  T_DB_SERVICE_MODE db_mode;

  const char *new_db_user_name;
  const char *new_db_user_pass;

  int i, sect, sect_len;
  char *tval, *sval;
  int anum;

  dbname = nv_get_val (req, "_DBNAME");
  if (dbname == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (_op_db_login (res, req, ha_mode, _dbmt_error) < 0)
    {
      return ERR_WITH_MSG;
    }
  new_db_user_name = nv_get_val (req, "username");
  new_db_user_pass = nv_get_val (req, "userpass");

  dbuser = db_add_user (new_db_user_name, &exists);
  if ((dbuser == NULL) || exists)
    {
      goto error_return;
    }

  if (uStringEqual (new_db_user_pass, "__NULL__"))
    new_db_user_pass = "";
  if (db_set_password (dbuser, NULL, new_db_user_pass) < 0)
    {
      goto error_return;
    }

  /* make itself a member of other groups */
  nv_locate (req, "groups", &sect, &sect_len);
  if (sect >= 0)
    {
      for (i = 0; i < sect_len; ++i)
	{
	  nv_lookup (req, sect + i, NULL, &tval);
	  if ((obj = db_find_user (tval)) == NULL)
	    {
	      continue;
	    }
	  db_add_member (obj, dbuser);
	}
    }

  /* add members to this group */
  nv_locate (req, "addmembers", &sect, &sect_len);
  if (sect >= 0)
    {
      for (i = 0; i < sect_len; ++i)
	{
	  nv_lookup (req, sect + i, NULL, &tval);
	  if ((obj = db_find_user (tval)) == NULL)
	    {
	      continue;
	    }
	  db_add_member (dbuser, obj);
	}
    }

  /* add authorization info */
  nv_locate (req, "authorization", &sect, &sect_len);
  if (sect >= 0)
    {
      for (i = 0; i < sect_len; ++i)
	{
	  nv_lookup (req, sect + i, &sval, &tval);
	  if ((obj = db_find_class (sval)) == NULL)
	    {
	      continue;
	    }
	  if (tval == NULL)
	    {
	      continue;
	    }

	  anum = atoi (tval);
	  if (anum == 0)
	    {
	      continue;
	    }

	  aset = anum & DB_AUTH_ALL;
	  if (db_grant (dbuser, obj, (DB_AUTH) aset, 0) < 0)
	    {
	      continue;
	    }

	  aset = (anum >> 8) & (DB_AUTH_ALL);
	  if (db_grant (dbuser, obj, (DB_AUTH) aset, 1) < 0)
	    {
	      continue;
	    }
	}
    }

  if (db_commit_transaction () < 0)
    {
      goto error_return;
    }
  db_shutdown ();
  return ERR_NO_ERROR;

error_return:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
cm_ts_userinfo (nvplist * in, nvplist * out, char *_dbmt_error)
{
  DB_OBJECT *p_class_db_user, *p_user;
  DB_OBJLIST *user_list, *temp;
  char *db_name;
  int ha_mode = 0;
  T_DB_SERVICE_MODE db_mode;

  db_name = nv_get_val (in, "dbname");
  if (db_name == NULL)
    {
      return ERR_WITH_MSG;
    }
  db_mode = uDatabaseMode (db_name, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", db_name);
      return ERR_STANDALONE_MODE;
    }

  if (_op_db_login (out, in, ha_mode, _dbmt_error) < 0)
    {
      return ERR_WITH_MSG;
    }
  p_class_db_user = db_find_class ("db_user");
  if (p_class_db_user == NULL)
    {
      goto error_return;
    }

  nv_add_nvp (out, "dbname", db_name);

  user_list = db_get_all_objects (p_class_db_user);
  if (user_list == NULL)
    {
      if (db_error_code () < 0)
	{
	  goto error_return;
	}
    }
  temp = user_list;
  while (temp != NULL)
    {
      p_user = temp->op;
      nv_add_nvp (out, "open", "user");
      _op_get_db_user_name (out, p_user);
      _op_get_db_user_id (out, p_user);

      nv_add_nvp (out, "open", "groups");
      _op_get_db_user_groups (out, p_user);
      nv_add_nvp (out, "close", "groups");

      nv_add_nvp (out, "open", "authorization");
      _op_get_db_user_authorization (out, p_user);
      nv_add_nvp (out, "close", "authorization");

      nv_add_nvp (out, "close", "user");
      temp = temp->next;
    }
  db_objlist_free (user_list);
  if (db_commit_transaction () < 0)
    {
      goto error_return;
    }
  db_shutdown ();
  return ERR_NO_ERROR;

error_return:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

static int
user_login_sa (nvplist * out, char *_dbmt_error, char *dbname, char *dbuser, char *dbpasswd)
{
  char opcode[10];
  char outfile[PATH_MAX], errfile[PATH_MAX];
  char tmpfile[100];
  const char *argv[10];
  char cmd_name[PATH_MAX];
  char *outmsg = NULL, *errmsg = NULL;

  snprintf (tmpfile, sizeof (tmpfile) - 1, "%s%d", "DBMT_ems_sa.", getpid ());
  (void) envvar_tmpdir_file (outfile, PATH_MAX, tmpfile);
  snprintf (errfile, PATH_MAX - 1, "%s.err", outfile);
  unlink (outfile);
  unlink (errfile);

  snprintf (opcode, sizeof (opcode) - 1, "%d", EMS_SA_DBMT_USER_LOGIN);

  (void) envvar_bindir_file (cmd_name, PATH_MAX, "cub_jobsa" UTIL_EXE_EXT);

  argv[0] = cmd_name;
  argv[1] = opcode;
  argv[2] = outfile;
  argv[3] = errfile;
  argv[4] = (dbname ? dbname : "");
  argv[5] = (dbuser ? dbuser : "");
  argv[6] = (dbpasswd ? dbpasswd : "");
  argv[7] = NULL;

  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* cub_jobsa */
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  if (read_file (outfile, &outmsg) < 0)
    {
      strcpy (_dbmt_error, "internal error");
      goto login_err;
    }
  if (read_file (errfile, &errmsg) < 0)
    {
      strcpy (_dbmt_error, "internal error");
      goto login_err;
    }

  ut_trim (errmsg);
  ut_trim (outmsg);

  if (errmsg != NULL && strlen (errmsg) > 0)
    {
      strcpy (_dbmt_error, errmsg);
      goto login_err;
    }


  if (uStringEqual (outmsg, "isdba"))
    {
      nv_add_nvp (out, "authority", "isdba");
    }
  else
    {
      nv_add_nvp (out, "authority", "isnotdba");
    }

  unlink (outfile);
  unlink (errfile);
  return ERR_NO_ERROR;

login_err:
  if (errmsg != NULL)
    {
      free (errmsg);
    }
  if (outmsg != NULL)
    {
      free (outmsg);
    }
  unlink (outfile);
  unlink (errfile);
  return ERR_WITH_MSG;
}

static int
uStringEqualIgnoreCase (const char *str1, const char *str2)
{
  if (str1 == NULL || str2 == NULL)
    return 0;
  if (strcasecmp (str1, str2) == 0)
    return 1;
  return 0;
}



static int
read_file (char *filename, char **outbuf)
{
  struct stat statbuf;
  int size;
  char *buf;
  int fd;

  *outbuf = NULL;

  if (stat (filename, &statbuf) != 0)
    {
      return -1;
    }
  size = (int) statbuf.st_size;

  if (size <= 0)
    {
      return 0;
    }

  buf = (char *) malloc (size + 1);
  if (buf == NULL)
    {
      return -1;
    }
  fd = open (filename, O_RDONLY);
  if (fd < 0)
    {
      free (buf);
      return -1;
    }
#if defined(WINDOWS)
  if (setmode (fd, O_BINARY) == -1)
    {
      close (fd);
      free (buf);
      return -1;
    }
#endif

  read (fd, buf, size);
  buf[size] = '\0';
  close (fd);

  *outbuf = buf;
  return 0;
}

static int
class_info_sa (const char *dbname, const char *uid, const char *passwd, char *cli_ver_val, nvplist * out,
	       char *_dbmt_error)
{
  char strbuf[1024];
  char outfile[PATH_MAX], errfile[PATH_MAX];
  FILE *fp;
  int ret_val = ERR_NO_ERROR;
  char cmd_name[PATH_MAX];
  const char *argv[10];
  char cli_ver[10];
  char opcode[10];
  char tmpfile[100];

  snprintf (tmpfile, sizeof (tmpfile) - 1, "%s%d", "DBMT_class_info.", getpid ());
  (void) envvar_tmpdir_file (outfile, PATH_MAX, tmpfile);
  snprintf (errfile, PATH_MAX - 1, "%s.err", outfile);
  unlink (outfile);
  unlink (errfile);


  if (uid == NULL)
    {
      uid = "";
    }
  if (passwd == NULL)
    {
      passwd = "";
    }

  snprintf (cli_ver, sizeof (cli_ver) - 1, "%d", get_client_version (cli_ver_val));
  (void) envvar_bindir_file (cmd_name, PATH_MAX, "cub_jobsa" UTIL_EXE_EXT);
  snprintf (opcode, sizeof (opcode) - 1, "%d", EMS_SA_CLASS_INFO);
  argv[0] = cmd_name;
  argv[1] = opcode;
  argv[2] = dbname;
  argv[3] = uid;
  argv[4] = passwd;
  argv[5] = outfile;
  argv[6] = errfile;
  argv[7] = cli_ver;
  argv[8] = NULL;

  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* cub_jobsa */
      sprintf (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  fp = fopen (errfile, "r");
  if (fp != NULL)
    {
      memset (strbuf, '0', sizeof (strbuf));
      fgets (strbuf, sizeof (strbuf), fp);
      DBMT_ERR_MSG_SET (_dbmt_error, strbuf);
      fclose (fp);
      ret_val = ERR_WITH_MSG;
      goto class_info_sa_finale;
    }

  fp = fopen (outfile, "r");
  if (fp == NULL)
    {
      sprintf (_dbmt_error, "class_info");
      ret_val = ERR_SYSTEM_CALL;
      goto class_info_sa_finale;
    }

  nv_add_nvp (out, "dbname", dbname);
  while (fgets (strbuf, sizeof (strbuf), fp))
    {
      char name[32], value[128];

      if (sscanf (strbuf, "%31s %127s", name, value) < 2)
	{
	  continue;
	}
      nv_add_nvp (out, name, value);
    }
  fclose (fp);

class_info_sa_finale:
  unlink (outfile);
  unlink (errfile);
  return ret_val;
}

static int
_op_get_system_classes_info (nvplist * out, char *_dbmt_error)
{
  DB_OBJLIST *objlist, *temp;
  DB_OBJECT *classobj;
  DB_AUTH auth;

  objlist = db_get_all_classes ();
  if (objlist == NULL)
    {
      if (db_error_code () < 0)
	{
	  CUBRID_ERR_MSG_SET (_dbmt_error);
	  return -1;
	}
    }
  nv_add_nvp (out, "open", "systemclass");
  for (temp = objlist; temp != NULL; temp = temp->next)
    {
      classobj = temp->op;
      if (db_is_system_class (classobj))
	{
	  auth = DB_AUTH_SELECT;
	  if (db_check_authorization (classobj, auth) == 0)
	    {
	      _op_get_class_info (out, classobj);
	    }
	}
    }
  nv_add_nvp (out, "close", "systemclass");
  db_objlist_free (objlist);
  return 0;
}

static int
_op_get_user_classes_info (nvplist * out, char *_dbmt_error)
{
  DB_OBJLIST *objlist, *temp;
  DB_OBJECT *classobj;
  DB_AUTH auth;

  objlist = db_get_all_classes ();
  if (objlist == NULL)
    {
      if (db_error_code () < 0)
	{
	  CUBRID_ERR_MSG_SET (_dbmt_error);
	  return -1;
	}
    }
  nv_add_nvp (out, "open", "userclass");
  for (temp = objlist; temp != NULL; temp = temp->next)
    {
      classobj = temp->op;
      if (!db_is_system_class (classobj))
	{
	  auth = DB_AUTH_SELECT;
	  if (db_check_authorization (classobj, auth) == 0)
	    {
	      _op_get_class_info (out, classobj);
	    }
	}
    }
  nv_add_nvp (out, "close", "userclass");
  db_objlist_free (objlist);
  return 0;
}

/* save to nvplist with some information by class */
static void
_op_get_class_info (nvplist * out, DB_OBJECT * classobj)
{
  DB_OBJLIST *objlist, *temp;
  DB_OBJECT *obj;
  DB_VALUE v;

  nv_add_nvp (out, "open", "class");
  nv_add_nvp (out, "classname", db_get_class_name (classobj));

  obj = db_get_owner (classobj);
  if (db_get (obj, "name", &v) < 0)
    {
      nv_add_nvp (out, "owner", "");
    }
  else
    {
      nv_add_nvp (out, "owner", db_get_string (&v));
      db_value_clear (&v);
    }

  objlist = db_get_superclasses (classobj);
  for (temp = objlist; temp != NULL; temp = temp->next)
    {
      obj = temp->op;
      nv_add_nvp (out, "superclass", db_get_class_name (obj));
    }
  db_objlist_free (objlist);

  if (db_is_vclass (classobj))
    {
      nv_add_nvp (out, "virtual", "view");
    }
  else
    {
      nv_add_nvp (out, "virtual", "normal");
    }

  nv_add_nvp (out, "close", "class");
}

static T_EMGR_VERSION
get_client_version (char *cli_ver_val)
{
  /* set CLIENT_VERSION */
  char *cli_ver;
  int major_ver, minor_ver;

  cli_ver = cli_ver_val;

  if (cli_ver == NULL)
    cli_ver = strdup ("1.0");

  make_version_info (cli_ver, &major_ver, &minor_ver);
  return EMGR_MAKE_VER (major_ver, minor_ver);

}

static int
_op_get_detailed_class_info (nvplist * out, DB_OBJECT * classobj, char *_dbmt_error)
{
  DB_OBJLIST *objlist, *temp;
  DB_OBJECT *obj;
  DB_ATTRIBUTE *attr;
  DB_METHOD *method;
  DB_METHFILE *mfile;
  DB_RESOLUTION *res;
  DB_CONSTRAINT *con;
  DB_QUERY_SPEC *spec;
  DB_VALUE v;
  char *class_name;
  char *dbname = NULL;

  dbname = db_get_database_name ();
  nv_add_nvp (out, "open", "classinfo");
  nv_add_nvp (out, "dbname", dbname);
  class_name = (char *) db_get_class_name (classobj);
  if (class_name == NULL)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_string_free (dbname);
      return -1;
    }
  nv_add_nvp (out, "classname", class_name);
  if (db_is_system_class (classobj))
    {
      nv_add_nvp (out, "type", "system");
    }
  else
    {
      nv_add_nvp (out, "type", "user");
    }
  obj = db_get_owner (classobj);
  if (db_get (obj, "name", &v) < 0)
    {
      nv_add_nvp (out, "owner", "");
    }
  else
    {
      nv_add_nvp (out, "owner", db_get_string (&v));
      db_value_clear (&v);
    }

  objlist = db_get_superclasses (classobj);
  for (temp = objlist; temp != NULL; temp = temp->next)
    {
      obj = temp->op;
      nv_add_nvp (out, "superclass", (char *) db_get_class_name (obj));
    }
  db_objlist_free (objlist);
  objlist = db_get_subclasses (classobj);
  for (temp = objlist; temp != NULL; temp = temp->next)
    {
      obj = temp->op;
      nv_add_nvp (out, "subclass", (char *) db_get_class_name (obj));
    }
  db_objlist_free (objlist);

  if (db_is_vclass (classobj))
    {
      nv_add_nvp (out, "virtual", "view");
    }
  else
    {
      nv_add_nvp (out, "virtual", "normal");
    }

  /* class_ attributes info */
  for (attr = db_get_class_attributes (classobj); attr != NULL; attr = db_attribute_next (attr))
    {
      _op_get_attribute_info (out, attr, 1);
    }

  /* attributes info */
  for (attr = db_get_attributes (classobj); attr != NULL; attr = db_attribute_next (attr))
    {
      _op_get_attribute_info (out, attr, 0);
    }

  /* class_ methods */
  for (method = db_get_class_methods (classobj); method != NULL; method = db_method_next (method))
    {
      _op_get_method_info (out, method, 1);
    }

  /* methods */
  for (method = db_get_methods (classobj); method != NULL; method = db_method_next (method))
    {
      _op_get_method_info (out, method, 0);
    }

  /* method files */
  for (mfile = db_get_method_files (classobj); mfile != NULL; mfile = db_methfile_next (mfile))
    {
      _op_get_method_file_info (out, mfile);
    }

  /* class_ resolutions */
  for (res = db_get_class_resolutions (classobj); res != NULL; res = db_resolution_next (res))
    {
      _op_get_resolution_info (out, res, 1);
    }

  /* resolutions */
  for (res = db_get_resolutions (classobj); res != NULL; res = db_resolution_next (res))
    {
      _op_get_resolution_info (out, res, 0);
    }

  /* constraints */
  for (con = db_get_constraints (classobj); con != NULL; con = db_constraint_next (con))
    {
      _op_get_constraint_info (out, con);
    }

  /* query specs */
  for (spec = db_get_query_specs (classobj); spec != NULL; spec = db_query_spec_next (spec))
    {
      _op_get_query_spec_info (out, spec);
    }

  nv_add_nvp (out, "close", "classinfo");

  if (dbname)
    {
      db_string_free (dbname);
    }
  return 0;
}

/* constraint information retrieve */
static void
_op_get_constraint_info (nvplist * out, DB_CONSTRAINT * con)
{
  char *classname;
  char query[QUERY_BUFFER_MAX], attr_name[128], order[10];
  int i, end, con_type;
  DB_ATTRIBUTE **attr;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;
  DB_OBJECT *classobj;
  DB_VALUE val;

  classname = nv_get_val (out, "classname");
  classobj = db_find_class (classname);
  nv_add_nvp (out, "open", "constraint");
  con_type = db_constraint_type (con);
  switch (con_type)
    {
    case DB_CONSTRAINT_UNIQUE:
      nv_add_nvp (out, "type", "UNIQUE");
      break;
    case DB_CONSTRAINT_NOT_NULL:
      nv_add_nvp (out, "type", "NOT NULL");
      break;
    case DB_CONSTRAINT_INDEX:
      nv_add_nvp (out, "type", "INDEX");
      break;
    case DB_CONSTRAINT_REVERSE_UNIQUE:
      nv_add_nvp (out, "type", "REVERSE UNIQUE");
      break;
    case DB_CONSTRAINT_REVERSE_INDEX:
      nv_add_nvp (out, "type", "REVERSE INDEX");
      break;
    case DB_CONSTRAINT_PRIMARY_KEY:
      nv_add_nvp (out, "type", "PRIMARY KEY");
      break;
    case DB_CONSTRAINT_FOREIGN_KEY:
      nv_add_nvp (out, "type", "FOREIGN KEY");
      break;
    }

  nv_add_nvp (out, "name", db_constraint_name (con));
  attr = db_constraint_attributes (con);
  for (i = 0; attr[i] != NULL; i++)
    {
      if (_op_is_classattribute (attr[i], classobj))
	{
	  nv_add_nvp (out, "classattribute", db_attribute_name (attr[i]));
	}
      else
	{
	  nv_add_nvp (out, "attribute", db_attribute_name (attr[i]));
	}
    }

  if ((con_type == DB_CONSTRAINT_UNIQUE) || (con_type == DB_CONSTRAINT_INDEX))
    {
      char buf[256];

      snprintf (query, sizeof (query) - 1,
		"select [key_attr_name], [asc_desc] from [db_index_key] where [class_name]='%s' and [index_name]='%s' order by [key_order] asc",
		classname, db_constraint_name (con));

      lang_set_parser_use_client_charset (false);
      db_execute (query, &result, &query_error);
      lang_set_parser_use_client_charset (true);

      end = db_query_first_tuple (result);

      while (end == 0)
	{
	  char *db_string_p = NULL;

	  db_query_get_tuple_value (result, 0, &val);
	  db_string_p = db_get_string (&val);
	  if (db_string_p != NULL)
	    {
	      snprintf (attr_name, sizeof (attr_name) - 1, "%s", db_string_p);
	    }
	  db_value_clear (&val);

	  db_query_get_tuple_value (result, 1, &val);
	  db_string_p = db_get_string (&val);
	  if (db_string_p != NULL)
	    {
	      snprintf (order, sizeof (order) - 1, "%s", db_string_p);
	    }
	  db_value_clear (&val);

	  snprintf (buf, sizeof (buf) - 1, "%s %s", attr_name, order);
	  buf[255] = '\0';
	  nv_add_nvp (out, "rule", buf);
	  end = db_query_next_tuple (result);
	}
      db_query_end (result);
    }

  if (con_type == DB_CONSTRAINT_FOREIGN_KEY)
    {
      char buf[256];
      DB_OBJECT *ref_cls;

      ref_cls = db_get_foreign_key_ref_class (con);
      snprintf (buf, sizeof (buf) - 1, "REFERENCES %s", db_get_class_name (ref_cls));
      nv_add_nvp (out, "rule", buf);

      snprintf (buf, sizeof (buf) - 1, "ON DELETE %s", db_get_foreign_key_action (con, DB_FK_DELETE));
      nv_add_nvp (out, "rule", buf);

      snprintf (buf, sizeof (buf) - 1, "ON UPDATE %s", db_get_foreign_key_action (con, DB_FK_UPDATE));
      nv_add_nvp (out, "rule", buf);
    }

  nv_add_nvp (out, "close", "constraint");
}

static void
_op_get_attribute_info (nvplist * out, DB_ATTRIBUTE * attr, int isclass)
{
  char *type_name, *v_str;
  DB_OBJECT *superobj;

  if (isclass)
    {
      nv_add_nvp (out, "open", "classattribute");
    }
  else
    {
      nv_add_nvp (out, "open", "attribute");
    }
  nv_add_nvp (out, "name", db_attribute_name (attr));
  type_name = _op_get_type_name (db_attribute_domain (attr));
  nv_add_nvp (out, "type", type_name);
  free (type_name);

  superobj = db_attribute_class (attr);
  if (superobj != NULL)
    {
      nv_add_nvp (out, "inherit", db_get_class_name (superobj));
    }
  else
    {
      nv_add_nvp (out, "inherit", "");
    }

  if (db_attribute_is_indexed (attr))
    {
      nv_add_nvp (out, "indexed", "y");
    }
  else
    {
      nv_add_nvp (out, "indexed", "n");
    }

  if (db_attribute_is_non_null (attr))
    {
      nv_add_nvp (out, "notnull", "y");
    }
  else
    {
      nv_add_nvp (out, "notnull", "n");
    }

  if (db_attribute_is_shared (attr))
    {
      nv_add_nvp (out, "shared", "y");
    }
  else
    {
      nv_add_nvp (out, "shared", "n");
    }

  if (db_attribute_is_unique (attr))
    {
      nv_add_nvp (out, "unique", "y");
    }
  else
    {
      nv_add_nvp (out, "unique", "n");
    }

  v_str = _op_get_value_string (db_attribute_default (attr));
  nv_add_nvp (out, "default", v_str);
  free (v_str);
  if (isclass)
    {
      nv_add_nvp (out, "close", "classattribute");
    }
  else
    {
      nv_add_nvp (out, "close", "attribute");
    }
}

/* resolution information retrieve */
static void
_op_get_resolution_info (nvplist * out, DB_RESOLUTION * res, int isclass)
{
  if (isclass)
    {
      nv_add_nvp (out, "open", "classresolution");
    }
  else
    {
      nv_add_nvp (out, "open", "resolution");
    }
  nv_add_nvp (out, "name", db_resolution_name (res));
  nv_add_nvp (out, "classname", db_get_class_name (db_resolution_class (res)));
  nv_add_nvp (out, "alias", db_resolution_alias (res));
  if (isclass)
    {
      nv_add_nvp (out, "close", "classresolution");
    }
  else
    {
      nv_add_nvp (out, "close", "resolution");
    }
}

static void
_op_get_method_info (nvplist * out, DB_METHOD * method, int isclass)
{
  int i, cnt;
  char *type_name;
  DB_DOMAIN *d;
  DB_OBJECT *superobj;

  if (isclass)
    {
      nv_add_nvp (out, "open", "classmethod");
    }
  else
    {
      nv_add_nvp (out, "open", "method");
    }
  nv_add_nvp (out, "name", db_method_name (method));
  superobj = db_method_class (method);
  if (superobj != NULL)
    {
      nv_add_nvp (out, "inherit", db_get_class_name (superobj));
    }
  else
    {
      nv_add_nvp (out, "inherit", "");
    }
  cnt = db_method_arg_count (method);
  for (i = 0; i <= cnt; i++)
    {
      d = db_method_arg_domain (method, i);
      type_name = _op_get_type_name (d);
      if (type_name != NULL)
	{
	  nv_add_nvp (out, "argument", type_name);
	  free (type_name);
	}
      else
	nv_add_nvp (out, "argument", "void");
    }
  nv_add_nvp (out, "function", db_method_function (method));
  if (isclass)
    {
      nv_add_nvp (out, "close", "classmethod");
    }
  else
    {
      nv_add_nvp (out, "close", "method");
    }
}

static void
_op_get_method_file_info (nvplist * out, DB_METHFILE * mfile)
{
  nv_add_nvp (out, "methodfile", db_methfile_name (mfile));
}

/* vclass's query spec information retrieve */
static void
_op_get_query_spec_info (nvplist * out, DB_QUERY_SPEC * spec)
{
  nv_add_nvp (out, "queryspec", db_query_spec_string (spec));
}

/* return 1 if argument's attribute is classattribute. or return 0 */
static int
_op_is_classattribute (DB_ATTRIBUTE * attr, DB_OBJECT * classobj)
{
  DB_ATTRIBUTE *temp;
  int id = db_attribute_id (attr);
  for (temp = db_get_class_attributes (classobj); temp != NULL; temp = db_attribute_next (temp))
    {
      if (id == db_attribute_id (temp))
	{
	  return 1;
	}
    }
  return 0;
}

/* function that return domain's type name */
/* it should be free by caller */
static char *
_op_get_type_name (DB_DOMAIN * domain)
{
  DB_OBJECT *class_;
  DB_DOMAIN *set_domain;
  DB_TYPE type_id;
  int p;
  char *result, *temp;
  const size_t result_size = 255;

  result = (char *) malloc (result_size + 1);
  if (result == NULL)
    {
      return NULL;
    }

  type_id = TP_DOMAIN_TYPE (domain);
  if (type_id == 0)
    {
      free (result);
      return NULL;
    }
  else if (type_id == DB_TYPE_OBJECT)
    {
      class_ = db_domain_class (domain);
      if (class_ != NULL)
	{
	  snprintf (result, result_size, "%s", db_get_class_name (class_));
	}
      else
	{
	  *result = '\0';
	}
    }
  else if (type_id == DB_TYPE_NUMERIC)
    {
      int s;
      p = db_domain_precision (domain);
      s = db_domain_scale (domain);
      snprintf (result, result_size, "%s(%d,%d)", db_get_type_name (type_id), p, s);
    }
  else if ((p = db_domain_precision (domain)) != 0)
    {
      snprintf (result, result_size, "%s(%d)", db_get_type_name (type_id), p);
    }
  else if (TP_IS_SET_TYPE (type_id))
    {
      size_t avail_size = result_size;
      char *result_p = result;

      set_domain = db_domain_set (domain);

      STRING_APPEND (result_p, avail_size, "%s", db_get_type_name (type_id));

      if (domain != NULL)
	{
	  STRING_APPEND (result_p, avail_size, "_of(");

	  temp = _op_get_type_name (set_domain);
	  if (temp != NULL)
	    {
	      STRING_APPEND (result_p, avail_size, "%s", temp);
	      free (temp);

	      for (set_domain = db_domain_next (set_domain); set_domain != NULL;
		   set_domain = db_domain_next (set_domain))
		{
		  temp = _op_get_type_name (set_domain);
		  if (temp != NULL)
		    {
		      STRING_APPEND (result_p, avail_size, ",%s", temp);
		      free (temp);
		    }
		}
	    }
	  STRING_APPEND (result_p, avail_size, ")");
	}
    }
  else
    {
      snprintf (result, result_size, "%s", db_get_type_name (type_id));
    }

  return result;
}

static char *
_op_get_value_string (DB_VALUE * value)
{
#if !defined (NUMERIC_MAX_STRING_SIZE)
#define NUMERIC_MAX_STRING_SIZE (80 + 1)
#endif
  char *result, *return_result, *db_string_p;
  DB_TYPE type;
  DB_DATE *date_v;
  DB_TIME *time_v;
  DB_TIMESTAMP *timestamp_v;
  DB_DATETIME *datetime_v;
  DB_MONETARY *money;
  DB_SET *set;
  DB_VALUE val;
  int size, i, idx, result_size = 255;
  double dv;
  float fv;
  int iv;
  DB_BIGINT bigint;
  short sv;
  DB_TIMESTAMPTZ *ts_tz;
  DB_DATETIMETZ *dt_tz;
  DB_TIMETZ *timetz_v;
  char str_buf[NUMERIC_MAX_STRING_SIZE];

  extern char *numeric_db_value_print (DB_VALUE *, char *str_buf);
  extern int db_get_string_length (const DB_VALUE * value);
  extern int db_bit_string (const DB_VALUE * the_db_bit, const char *bit_format, char *string, int max_size);

  result = (char *) malloc (result_size + 1);
  if (result == NULL)
    {
      return NULL;
    }
  memset (result, 0, result_size + 1);
  type = db_value_type (value);

  switch (type)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
      db_string_p = db_get_string (value);
      if (db_string_p != NULL)
	{
	  snprintf (result, result_size, "%s", db_string_p);
	}
      break;
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      db_string_p = db_get_nchar (value, &size);
      if (db_string_p != NULL)
	{
	  snprintf (result, result_size, "N'%s'", db_string_p);
	}
      break;
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      size = ((db_get_string_length (value) + 3) / 4) + 4;
      db_string_p = (char *) malloc (size);
      if (db_string_p == NULL)
	{
	  break;
	}
      if (db_bit_string (value, "%X", db_string_p, size) == 0)
	{
	  snprintf (result, result_size, "X'%s'", db_string_p);
	}
      free (db_string_p);
      break;
    case DB_TYPE_DOUBLE:
      dv = db_get_double (value);
      snprintf (result, result_size, "%f", dv);
      break;
    case DB_TYPE_FLOAT:
      fv = db_get_float (value);
      snprintf (result, result_size, "%f", fv);
      break;
    case DB_TYPE_NUMERIC:
      snprintf (result, result_size, "%s", numeric_db_value_print ((DB_VALUE *) value, str_buf));
      break;
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      set = db_get_set (value);
      if (set != NULL)
	{
	  idx = snprintf (result, result_size, "%s", "{");
	  size = set_size (set);
	  for (i = 0; i < size && idx < result_size; i++)
	    {
	      set_get_element (set, i, &val);
	      return_result = _op_get_set_value (&val);
	      if (return_result == NULL)
		goto exit_on_end;
	      if (i == 0)
		idx += snprintf (result + idx, result_size - idx, "%s", return_result);
	      else
		idx += snprintf (result + idx, result_size - idx, ",%s", return_result);
	      free (return_result);
	      db_value_clear (&val);
	    }
	  idx += snprintf (result + idx, result_size - idx, "%s", "}");
	  if (idx >= result_size)
	    strncpy (result + result_size - 4, "...}", 4);
	  result[result_size] = '\0';
	}
      break;
    case DB_TYPE_MONETARY:
      money = db_get_monetary (value);
      if (money != NULL)
	{
	  snprintf (result, result_size, "%.2f", money->amount);
	}
      break;
    case DB_TYPE_INTEGER:
      iv = db_get_int (value);
      snprintf (result, result_size, "%d", iv);
      break;
    case DB_TYPE_BIGINT:
      bigint = db_get_bigint (value);
      snprintf (result, result_size, "%lld", (long long) bigint);
      break;
    case DB_TYPE_SHORT:
      sv = db_get_short (value);
      snprintf (result, result_size, "%d", sv);
      break;
    case DB_TYPE_DATE:
      date_v = db_get_date (value);
      db_date_to_string (result, 256, date_v);
      break;
    case DB_TYPE_TIME:
      time_v = db_get_time (value);
      db_time_to_string (result, 256, time_v);
      break;
    case DB_TYPE_TIMELTZ:
      time_v = db_get_time (value);
      db_timeltz_to_string (result, 256, time_v);
      break;
    case DB_TYPE_TIMETZ:
      timetz_v = db_get_timetz (value);
      db_timetz_to_string (result, 256, &timetz_v->time, &timetz_v->tz_id);
      break;
    case DB_TYPE_TIMESTAMP:
      timestamp_v = db_get_timestamp (value);
      db_timestamp_to_string (result, 256, timestamp_v);
      break;
    case DB_TYPE_TIMESTAMPTZ:
      ts_tz = db_get_timestamptz (value);
      timestamp_v = &(ts_tz->timestamp);
      db_timestamptz_to_string (result, 256, timestamp_v, &ts_tz->tz_id);
      break;
    case DB_TYPE_TIMESTAMPLTZ:
      timestamp_v = db_get_timestamp (value);
      db_timestampltz_to_string (result, 256, timestamp_v);
      break;
    case DB_TYPE_DATETIME:
      datetime_v = db_get_datetime (value);
      db_datetime_to_string (result, 256, datetime_v);
      break;
    case DB_TYPE_DATETIMETZ:
      dt_tz = db_get_datetimetz (value);
      datetime_v = &(dt_tz->datetime);
      db_datetimetz_to_string (result, 256, datetime_v, &(dt_tz->tz_id));
      break;
    case DB_TYPE_DATETIMELTZ:
      datetime_v = db_get_datetime (value);
      db_datetimeltz_to_string (result, 256, datetime_v);
      break;
    default:
      result[0] = '\0';
    }

exit_on_end:

  return result;

#if 0
exit_on_error:

  if (result)
    {
      free (result);
      result = NULL;		/* set error */
    }

  goto exit_on_end;
#endif

#undef NUMERIC_MAX_STRING_SIZE
}

static char *
_op_get_set_value (DB_VALUE * val)
{
  char *result, *return_result;
  DB_TYPE type;
  int result_size = 255;
  result = (char *) malloc (result_size + 1);
  if (result == NULL)
    {
      return NULL;
    }
  return_result = _op_get_value_string (val);
  if (return_result == NULL)
    {
      free (result);
      return NULL;
    }
  type = db_value_type (val);
  switch (type)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
      snprintf (result, result_size, "%s%s%s", "'", return_result, "'");
      break;

    case DB_TYPE_DATE:
      snprintf (result, result_size, "%s%s%s", "date '", return_result, "'");
      break;

    case DB_TYPE_TIME:
      snprintf (result, result_size, "%s%s%s", "time '", return_result, "'");
      break;

    case DB_TYPE_UTIME:
      snprintf (result, result_size, "%s%s%s", "timestamp '", return_result, "'");
      break;

    case DB_TYPE_TIMESTAMPTZ:
      snprintf (result, result_size, "%s%s%s", "timestamptz '", return_result, "'");
      break;

    case DB_TYPE_TIMESTAMPLTZ:
      snprintf (result, result_size, "%s%s%s", "timestampltz '", return_result, "'");
      break;

    case DB_TYPE_DATETIME:
      snprintf (result, result_size, "%s%s%s", "datetime '", return_result, "'");
      break;

    case DB_TYPE_DATETIMETZ:
      snprintf (result, result_size, "%s%s%s", "datetimetz '", return_result, "'");
      break;

    case DB_TYPE_DATETIMELTZ:
      snprintf (result, result_size, "%s%s%s", "datetimeltz '", return_result, "'");
      break;

    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      snprintf (result, result_size, "%s%s%s", "N'", return_result, "'");
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      snprintf (result, result_size, "%s%s%s", "X'", return_result, "'");
      break;

    case DB_TYPE_BLOB:
      snprintf (result, result_size, "%s%s%s", "BLOB'", return_result, "'");
      break;

    case DB_TYPE_CLOB:
      snprintf (result, result_size, "%s%s%s", "CLOB'", return_result, "'");
      break;

    default:
      snprintf (result, result_size, "%s", return_result);
      break;

    }
  free (return_result);
  result[result_size] = '\0';
  return result;
}

static int
trigger_info_sa (const char *dbname, const char *uid, const char *passwd, nvplist * out, char *_dbmt_error)
{
  char strbuf[1024];
  char outfile[PATH_MAX], errfile[PATH_MAX];
  FILE *fp;
  int ret_val = ERR_NO_ERROR;
  char cmd_name[PATH_MAX];
  const char *argv[10];
  char tmpfile[100];

  snprintf (tmpfile, sizeof (tmpfile) - 1, "%s%d", "DBMT_trigger_info.", getpid ());
  (void) envvar_tmpdir_file (outfile, PATH_MAX, tmpfile);
  snprintf (errfile, PATH_MAX - 1, "%s.err", outfile);
  unlink (outfile);
  unlink (errfile);


  if (uid == NULL)
    {
      uid = "";
    }
  if (passwd == NULL)
    {
      passwd = "";
    }

  (void) envvar_bindir_file (cmd_name, PATH_MAX, "cub_sainfo" UTIL_EXE_EXT);

  argv[0] = cmd_name;
  argv[1] = dbname;
  argv[2] = uid;
  argv[3] = passwd;
  argv[4] = outfile;
  argv[5] = errfile;
  argv[6] = NULL;

  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* cm_sainfo sa mode */
      sprintf (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  fp = fopen (errfile, "r");
  if (fp != NULL)
    {
      memset (strbuf, '0', sizeof (strbuf));
      fgets (strbuf, sizeof (strbuf), fp);
      DBMT_ERR_MSG_SET (_dbmt_error, strbuf);
      fclose (fp);
      ret_val = ERR_WITH_MSG;
      goto trigger_info_sa_finale;
    }

  nv_add_nvp (out, "dbname", dbname);
  ret_val = nv_readfrom (out, outfile);
  if (ret_val < 0)
    {
      goto trigger_info_sa_finale;
    }
trigger_info_sa_finale:
  unlink (outfile);
  unlink (errfile);
  return ret_val;
}

static void
op_get_trigger_information (nvplist * res, DB_OBJECT * p_trigger)
{
  char *trigger_name, *action, *attr, *condition, *comment;
  DB_OBJECT *target_class;
  DB_TRIGGER_EVENT event;
  DB_TRIGGER_TIME eventtime, actiontime;
  DB_TRIGGER_ACTION action_type;
  DB_TRIGGER_STATUS status;
  double priority;
  char pri_string[16];

  trigger_name = action = comment = NULL;

  /* trigger name */
  db_trigger_name (p_trigger, &trigger_name);
  nv_add_nvp (res, "name", trigger_name);
  if (trigger_name)
    {
      db_string_free ((char *) trigger_name);
    }
  /* eventtime */
  db_trigger_condition_time (p_trigger, &eventtime);
  switch (eventtime)
    {
    case TR_TIME_BEFORE:
      nv_add_nvp (res, "conditiontime", "BEFORE");
      break;
    case TR_TIME_AFTER:
      nv_add_nvp (res, "conditiontime", "AFTER");
      break;
    case TR_TIME_DEFERRED:
      nv_add_nvp (res, "conditiontime", "DEFERRED");
      break;
    case TR_TIME_NULL:
      break;
    }

  /* eventtype */
  db_trigger_event (p_trigger, &event);
  switch (event)
    {
    case TR_EVENT_UPDATE:
      nv_add_nvp (res, "eventtype", "UPDATE");
      break;
    case TR_EVENT_STATEMENT_UPDATE:
      nv_add_nvp (res, "eventtype", "STATEMENT UPDATE");
      break;

    case TR_EVENT_DELETE:
      nv_add_nvp (res, "eventtype", "DELETE");
      break;
    case TR_EVENT_STATEMENT_DELETE:
      nv_add_nvp (res, "eventtype", "STATEMENT DELETE");
      break;

    case TR_EVENT_INSERT:
      nv_add_nvp (res, "eventtype", "INSERT");
      break;
    case TR_EVENT_STATEMENT_INSERT:
      nv_add_nvp (res, "eventtype", "STATEMENT INSERT");
      break;

    case TR_EVENT_COMMIT:
      nv_add_nvp (res, "eventtype", "COMMIT");
      break;
    case TR_EVENT_ROLLBACK:
      nv_add_nvp (res, "eventtype", "ROLLBACK");
      break;
    default:
      break;
    }

  /* trigger action */
  db_trigger_action_type (p_trigger, &action_type);
  switch (action_type)
    {
    case TR_ACT_EXPRESSION:	/* act like TR_ACT_PRINT */
    case TR_ACT_PRINT:
      db_trigger_action (p_trigger, &action);
      nv_add_nvp (res, "action", action);
      if (action)
	{
	  db_string_free ((char *) action);
	}
      break;

    case TR_ACT_REJECT:
      nv_add_nvp (res, "action", "REJECT");
      break;
    case TR_ACT_INVALIDATE:
      nv_add_nvp (res, "action", "INVALIDATE TRANSACTION");
      break;
    case TR_ACT_NULL:
      break;
    }

  /* target class_ & att */
  db_trigger_class (p_trigger, &target_class);
  if (target_class != NULL)
    {
      nv_add_nvp (res, "target_class", db_get_class_name (target_class));
    }

  db_trigger_attribute (p_trigger, &attr);
  if (attr != NULL)
    {
      nv_add_nvp (res, "target_att", attr);
      db_string_free ((char *) attr);
    }

  /* condition */
  db_trigger_condition (p_trigger, &condition);
  if (condition != NULL)
    {
      nv_add_nvp (res, "condition", condition);
      db_string_free ((char *) condition);
    }

  /* delayedactiontime */
  db_trigger_action_time (p_trigger, &actiontime);
  switch (actiontime)
    {
    case TR_TIME_BEFORE:
      nv_add_nvp (res, "actiontime", "BEFORE");
      break;
    case TR_TIME_AFTER:
      nv_add_nvp (res, "actiontime", "AFTER");
      break;
    case TR_TIME_DEFERRED:
      nv_add_nvp (res, "actiontime", "DEFERRED");
      break;
    case TR_TIME_NULL:
      break;
    }

  /* status */
  db_trigger_status (p_trigger, &status);
  switch (status)
    {
    case TR_STATUS_ACTIVE:
      nv_add_nvp (res, "status", "ACTIVE");
      break;
    case TR_STATUS_INACTIVE:
      nv_add_nvp (res, "status", "INACTIVE");
      break;
    case TR_STATUS_INVALID:
      nv_add_nvp (res, "status", "INVALID");
    }

  /* priority */
  db_trigger_priority (p_trigger, &priority);
  snprintf (pri_string, sizeof (pri_string) - 1, "%4.5f", priority);
  nv_add_nvp (res, "priority", pri_string);

  /* trigger comment */
  db_trigger_comment (p_trigger, &comment);
  if (comment != NULL)
    {
      nv_add_nvp (res, "comment", comment);
      db_string_free ((char *) comment);
    }
}

static int
revoke_all_from_user (DB_OBJECT * user)
{
  int i;
  DB_VALUE v;
  DB_OBJECT *obj, **class_obj = NULL;
  int num_class = 0;
  DB_COLLECTION *col;

  db_get (user, "authorization.grants", &v);
  col = db_get_collection (&v);
  for (i = 0; i < db_seq_size (col); i += 3)
    {
      db_seq_get (col, i, &v);
      obj = db_get_object (&v);
      if (db_is_system_class (obj))
	{
	  continue;
	}
      class_obj = (DB_OBJECT **) (REALLOC (class_obj, sizeof (DB_OBJECT *) * (num_class + 1)));
      if (class_obj == NULL)
	{
	  return ERR_MEM_ALLOC;
	}
      class_obj[num_class] = obj;
      num_class++;
    }

  for (i = 0; i < num_class; i++)
    {
      db_revoke (user, class_obj[i], DB_AUTH_ALL);
    }
  FREE_MEM (class_obj);
  return ERR_NO_ERROR;
}

static void
_op_get_db_user_name (nvplist * res, DB_OBJECT * user)
{
  DB_VALUE v;

  db_get (user, "name", &v);
  nv_add_nvp (res, "name", db_get_string (&v));
  db_value_clear (&v);
}

static void
_op_get_db_user_authorization (nvplist * res, DB_OBJECT * user)
{
  DB_VALUE v;
  DB_OBJECT *obj;
  DB_COLLECTION *col;
  char buf[20];
  int i;

  db_get (user, "authorization.grants", &v);
  col = db_get_collection (&v);
  for (i = 0; i < db_seq_size (col); i += 3)
    {
      db_seq_get (col, i, &v);
      obj = db_get_object (&v);
      db_seq_get (col, i + 2, &v);
      snprintf (buf, sizeof (buf) - 1, "%d", db_get_int (&v));
      nv_add_nvp (res, (char *) db_get_class_name (obj), buf);
    }
}

static void
_op_get_db_user_id (nvplist * res, DB_OBJECT * user)
{
  DB_VALUE v;
  char buf[20];

  db_get (user, "id", &v);

  if (DB_IS_NULL (&v))
    {
      buf[0] = '\0';
    }
  else
    {
      snprintf (buf, sizeof (buf) - 1, "%d", db_get_int (&v));
    }

  nv_add_nvp (res, "id", buf);
  db_value_clear (&v);
}

static void
_op_get_db_user_groups (nvplist * res, DB_OBJECT * user)
{
  DB_VALUE v;
  DB_OBJECT *obj;
  DB_COLLECTION *col;
  int i;

  db_get (user, "direct_groups", &v);
  col = db_get_set (&v);
  for (i = 0; i < db_set_size (col); i++)
    {
      db_set_get (col, i, &v);
      obj = db_get_object (&v);
      db_get (obj, "name", &v);
      nv_add_nvp (res, "group", db_get_string (&v));
    }
}

static int
get_dbvoldir (char *vol_dir, size_t vol_dir_size, char *dbname)
{
  FILE *databases_txt;
  const char *envpath;
  char db_txt[1024];
  char cbuf[2048];
  char volname[1024];
  char scan_format[128];

  if (vol_dir == NULL || dbname == NULL)
    {
      return -1;
    }
#if !defined (DO_NOT_USE_CUBRIDENV)
  envpath = getenv (CUBRID_DATABASES_ENV);
#else
  envpath = CUBRID_VARDIR;
#endif
  if (envpath == NULL || strlen (envpath) == 0)
    {
      return -1;
    }

  snprintf (db_txt, sizeof (db_txt) - 1, "%s/%s", envpath, CUBRID_DATABASE_TXT);
  databases_txt = fopen (db_txt, "r");
  if (databases_txt == NULL)
    {
      return -1;
    }

  snprintf (scan_format, sizeof (scan_format) - 1, "%%%lus %%%lus %%*s %%*s", (unsigned long) sizeof (volname) - 1,
	    (unsigned long) vol_dir_size - 1);

  while (fgets (cbuf, sizeof (cbuf), databases_txt))
    {
      if (sscanf (cbuf, scan_format, volname, vol_dir) < 2)
	{
	  continue;
	}
      if (strcmp (volname, dbname) == 0)
	{
	  fclose (databases_txt);
	  return 1;
	}
    }

  fclose (databases_txt);
  return -1;
}

static int
getservershmid (char *dir, char *dbname)
{
  int shm_key;
  int result = 0;
  char key_file[PATH_MAX], cbuf[1024];
  FILE *fdkey_file;

  if (dir == NULL || dbname == NULL)
    {
      return -1;
    }

  snprintf (key_file, PATH_MAX - 1, "%s/%s_shm.key", dir, dbname);
  fdkey_file = fopen (key_file, "r");
  if (fdkey_file == NULL)
    {
      return -1;
    }
  if (fgets (cbuf, sizeof (cbuf), fdkey_file) == NULL)
    {
      return -1;
    }

  result = parse_int (&shm_key, cbuf, 16);
  if (result != 0)
    {
      return -1;
    }

  fclose (fdkey_file);
  return shm_key;
}
