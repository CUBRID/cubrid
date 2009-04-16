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
 * fserver_task.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>		/* umask()         */
#include <sys/stat.h>		/* umask(), stat() */
#include <time.h>
#include <ctype.h>		/* isalpha()       */
#include <fcntl.h>
#include <errno.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <io.h>
#else
#include <libgen.h>		/* strfind() */
#include <sys/shm.h>
#include <unistd.h>
#include <sys/wait.h>		/* wait()          */
#include <dirent.h>		/* opendir() ...   */
#include <pwd.h>		/* getpwuid_r() */
#include <netdb.h>
#if !defined(HPUX) && !defined(AIX)
#include <sys/procfs.h>
#endif
#endif

#include "dbi.h"
#include "cm_porting.h"
#include "cm_server_util.h"
#include "cm_job_task.h"
#include "cm_nameval.h"
#include "cm_dstring.h"
#include "cm_config.h"
#include "cm_command_execute.h"
#include "cm_user.h"
#include "cm_text_encryption.h"
#include "cm_broker_admin.h"
#include "cm_version.h"
#include "cm_execute_sa.h"
#include "utility.h"

#ifdef DIAG_DEVEL
#include "perf_monitor.h"
#include "cm_diag_client_request.h"
#include "cm_diag_util.h"
#endif

#include<assert.h>

extern int set_size (DB_COLLECTION * set);
extern int set_get_element (DB_COLLECTION * set, int index, DB_VALUE * value);


#ifdef _DEBUG_
#include "deb.h"
#define MALLOC(p) debug_malloc(p)
#else
#define MALLOC(p) malloc(p)
#endif

#define DB_RESTART_SERVER_NAME	"emgr_server"

#define DBMT_ERR_MSG_SET(ERR_BUF, MSG)	\
	strncpy(ERR_BUF, MSG, DBMT_ERROR_MSG_SIZE - 1)
#define CUBRID_ERR_MSG_SET(ERR_BUF)	\
	DBMT_ERR_MSG_SET(ERR_BUF, db_error_string(1))

#ifdef DIAG_DEVEL
#define MAX_BROKER_NAMELENGTH 128
#define MAX_AS_COUNT          200
#ifdef WIN32
#define SET_LONGLONG_STR(STR, LL_VALUE) sprintf(STR, "%I64d", LL_VALUE);
#else
#define SET_LONGLONG_STR(STR, LL_VALUE) sprintf(STR, "%lld", LL_VALUE);
#endif
#endif

#if 0				/* ACTIVITY PROFILE */
#define INIT_SERVER_ACT_DATA(SERVER_ACT_DATA) \
    do { \
        SERVER_ACT_DATA.query_fullscan = NULL; \
        SERVER_ACT_DATA.lock_deadlock = NULL; \
        SERVER_ACT_DATA.buffer_page_read = NULL; \
        SERVER_ACT_DATA.buffer_page_write = NULL; \
    } while (0)

#define INIT_CAS_ACT_DATA(CAS_ACT_DATA) \
    do { \
        CAS_ACT_DATA.request = NULL; \
        CAS_ACT_DATA.transaction = NULL; \
    } while(0)
#endif


extern T_EMGR_VERSION CLIENT_VERSION;

#if 0				/* ACTIVITY PROFILE */
static char *eventclass_type_str[] = {
  "CAS Request",
  "CAS Transaction",
  "SVR Fullscan",
  "SVR Deadlock",
  "SVR I/O read",
  "SVR I/O write"
};				/* fserver_slave */
#endif

static int op_db_user_pass_check (char *dbname, char *dbuser, char *dbpass);
static int _op_db_login (nvplist * out, nvplist * in, char *_dbmt_error);
static char *_op_get_type_name (DB_DOMAIN * domain);
static void _op_get_attribute_info (nvplist * out, DB_ATTRIBUTE * attr,
				    int isclass);
static char *_op_get_value_string (DB_VALUE * value);
static void _op_get_method_info (nvplist * out, DB_METHOD * method,
				 int isclass);
static void _op_get_method_file_info (nvplist * out, DB_METHFILE * mfile);
static void _op_get_resolution_info (nvplist * out, DB_RESOLUTION * res,
				     int isclass);
static void _op_get_constraint_info (nvplist * out, DB_CONSTRAINT * con);
static void _op_get_query_spec_info (nvplist * out, DB_QUERY_SPEC * spec);
static void _op_get_class_info (nvplist * out, DB_OBJECT * classobj);
static int _op_get_detailed_class_info (nvplist * out, DB_OBJECT * classobj,
					char *_dbmt_error);
static int _op_get_system_classes_info (nvplist * out, char *_dbmt_error);
static int _op_get_user_classes_info (nvplist * out, char *_dbmt_error);
static void _op_get_db_user_name (nvplist * res, DB_OBJECT * user);
static void _op_get_db_user_id (nvplist * res, DB_OBJECT * user);
static void _op_get_db_user_password (nvplist * res, DB_OBJECT * user);
static void _op_get_db_user_groups (nvplist * res, DB_OBJECT * user);
static void _op_get_db_user_authorization (nvplist * res, DB_OBJECT * user);
static void _op_get_super_info (nvplist * out, DB_OBJECT * classobj);
static int _op_is_classattribute (DB_ATTRIBUTE * attr, DB_OBJECT * classobj);
static void _tsAppendDBMTUserList (nvplist * res, T_DBMT_USER * dbmt_user,
				   char *_dbmt_error);
static int _tsAppendDBList (nvplist * res, char dbdir_flag);
static int _tsParseSpacedb (nvplist * req, nvplist * res, char *dbname,
			    char *_dbmt_error, T_SPACEDB_RESULT * cmd_res);
static void _ts_gen_spaceinfo (nvplist * res, char *filename,
			       char *dbinstalldir, char *type, int pagesize);
static char *_ts_get_error_log_param (char *dbname);
static int _ts_lockdb_parse_us (nvplist * res, FILE * infile);
static int _ts_lockdb_parse_kr (nvplist * res, FILE * infile);
static char *get_user_name (int uid, char *name_buf);
static int class_info_sa (char *dbname, char *uid, char *passwd,
			  nvplist * out, char *_dbmt_error);
static int trigger_info_sa (char *dbname, char *uid, char *password,
			    nvplist * res, char *_dbmt_error);
#ifdef WIN32
static void replace_colon (char *path);
#endif
static int revoke_all_from_user (DB_OBJECT * user);

static int op_get_objectid_attlist (DB_NAMELIST ** att_list,
				    char *att_comma_list);
static int op_make_ldbinput_file_register (nvplist * req,
					   char *input_filename);
static int op_make_ldbinput_file_remove (nvplist * req, char *input_filename);
static int op_make_triggerinput_file_add (nvplist * req,
					  char *input_filename);
static int op_make_triggerinput_file_drop (nvplist * req,
					   char *input_filename);
static int op_make_triggerinput_file_alter (nvplist * req,
					    char *input_filename);
static void op_get_trigger_information (nvplist * res, DB_OBJECT * p_trigger);
static int op_make_password_check_file (char *input_file);
static void op_auto_exec_query_get_newplan_id (char *id_buf, char *filename);
static char *op_get_cubrid_ver (char *buffer);

static int get_filename_from_path (char *path, char *filename);
#ifdef DIAG_DEVEL
static int get_dbvoldir (char *vol_dir, char *dbname, char *err_buf);
static int getservershmid (char *dir, char *dbname, char *err_buf);
#if 0				/* ACTIVITY PROFILE */
static int read_activity_data_from_file (void *result_data, char *filename,
					 struct timeval start_time,
					 T_DIAG_SERVER_TYPE type);
static int parse_and_make_client_activity_data (void *result_data, char *buf,
						int size,
						struct timeval start_time,
						T_DIAG_SERVER_TYPE type);
static char *get_activity_event_type_str (T_DIAG_ACTIVITY_EVENTCLASS_TYPE
					  type);
static int add_activity_list (T_ACTIVITY_DATA ** header,
			      T_ACTIVITY_DATA * list);
#endif
static int get_client_monitoring_config (nvplist * cli_request,
					 T_CLIENT_MONITOR_CONFIG * c_config);
#ifdef WIN32
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
#else
#define OP_SERVER_SHM_OPEN(SHM_KEY, HANDLE_PTR)            \
        op_server_shm_open(SHM_KEY)
#define OP_SERVER_SHM_DETACH(PTR, HMAP)    shmdt((void*)(PTR))
static void *op_server_shm_open (int shm_key);
#endif
#endif
#ifdef _DEBUG_
void *debug_malloc (size_t size);
#endif
static int read_file (char *filename, char **outbuf);
static int user_login_sa (nvplist * out, char *_dbmt_error, char *dbname,
			  char *dbuser, char *dbpasswd);

static int get_dbvoldir (char *vol_dir, char *dbname, char *err_buf);
static int getservershmid (char *dir, char *dbname, char *err_buf);

int
_tsReadUserCapability (nvplist * ud, char *id, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  char strbuf[1024];
  char err_flag = 1;
  int i, j;
  int retval;

  if ((retval = dbmt_user_read (&dbmt_user, _dbmt_error)) != ERR_NO_ERROR)
    {
      return retval;
    }
  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      if (strcmp (id, dbmt_user.user_info[i].user_name) == 0)
	{
	  for (j = 0; j < dbmt_user.user_info[i].num_dbinfo; j++)
	    {
	      dbmt_user_db_auth_str (&(dbmt_user.user_info[i].dbinfo[j]),
				     strbuf);
	      nv_add_nvp (ud, dbmt_user.user_info[i].dbinfo[j].dbname,
			  strbuf);
	    }
	  err_flag = 0;
	  break;
	}
    }
  dbmt_user_free (&dbmt_user);

  if (err_flag)
    {
      if (_dbmt_error)
	sprintf (_dbmt_error, "%s", id);
      return ERR_USER_CAPABILITY;
    }

  return ERR_NO_ERROR;
}

/******************************************************************************
	FSERVER TASK INTERFACE
******************************************************************************/

/******************************************************************************
	TASK CUBRID
******************************************************************************/

int
ts_create_class (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *user_name, *db_name;
  char sql[1024];
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  user_name = nv_get_val (in, "username");
  db_name = nv_get_val (in, "_DBNAME");

  if (user_name != NULL)
    {
      if (db_login (user_name, NULL) < 0)
	{
	  CUBRID_ERR_MSG_SET (_dbmt_error);
	  return ERR_WITH_MSG;
	}
    }

  sprintf (sql, "CREATE CLASS \"%s\"", class_name);
  if (db_execute (sql, &result, &query_error) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }
  db_query_end (result);

  /* re-login original user */
  if (user_name != NULL)
    {
      if (db_login (nv_get_val (in, "_DBID"), nv_get_val (in, "_DBPASSWD")) <
	  0)
	{
	  CUBRID_ERR_MSG_SET (_dbmt_error);
	  return ERR_WITH_MSG;
	}
    }

  nv_add_nvp (out, "dbname", nv_get_val (in, "dbname"));
  nv_add_nvp (out, "classname", class_name);

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

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_create_vclass (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *user_name, *db_name;
  DB_OBJECT *class_;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  user_name = nv_get_val (in, "username");
  db_name = nv_get_val (in, "_DBNAME");

  if (user_name != NULL)
    {
      if (db_login (user_name, NULL) < 0)
	{
	  CUBRID_ERR_MSG_SET (_dbmt_error);
	  return ERR_WITH_MSG;
	}
    }

  class_ = db_create_vclass (class_name);
  if (class_ == NULL)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  /* re-login original user */
  if (user_name != NULL)
    {
      if (db_login (nv_get_val (in, "_DBID"), nv_get_val (in, "_DBPASSWD")) <
	  0)
	{
	  CUBRID_ERR_MSG_SET (_dbmt_error);
	  return ERR_WITH_MSG;
	}
    }

  nv_add_nvp (out, "dbname", nv_get_val (in, "dbname"));
  nv_add_nvp (out, "classname", class_name);

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

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_userinfo (nvplist * in, nvplist * out, char *_dbmt_error)
{
  DB_OBJECT *p_class_db_user, *p_user;
  DB_OBJLIST *user_list, *temp;
  char *db_name;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  db_name = nv_get_val (in, "dbname");
  p_class_db_user = db_find_class ("db_user");
  if (p_class_db_user == NULL)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  nv_add_nvp (out, "dbname", db_name);

  user_list = db_get_all_objects (p_class_db_user);
  if (user_list == NULL)
    {
      if (db_error_code () < 0)
	{
	  CUBRID_ERR_MSG_SET (_dbmt_error);
	  db_shutdown ();
	  return ERR_WITH_MSG;
	}
    }
  temp = user_list;
  while (temp != NULL)
    {
      p_user = temp->op;
      nv_add_nvp (out, "open", "user");
      _op_get_db_user_name (out, p_user);
      _op_get_db_user_id (out, p_user);
      _op_get_db_user_password (out, p_user);

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
  db_commit_transaction ();
  db_shutdown ();

  return ERR_NO_ERROR;
}

int
ts_create_user (nvplist * req, nvplist * res, char *_dbmt_error)
{
  DB_OBJECT *dbuser;
  DB_OBJECT *obj;
  int exists, aset;

  char *newdbusername;
  char *newdbuserpass;

  int i, sect, sect_len;
  char *tval, *sval;
  int anum;

  if (_op_db_login (res, req, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  newdbusername = nv_get_val (req, "username");
  newdbuserpass = nv_get_val (req, "userpass");

  dbuser = db_add_user (newdbusername, &exists);
  if ((dbuser == NULL) || exists)
    {
      goto create_user_error;
    }

  if (uStringEqual (newdbuserpass, "__NULL__"))
    newdbuserpass = "";
  if (db_set_password (dbuser, NULL, newdbuserpass) < 0)
    {
      goto create_user_error;
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
	  anum = atoi (tval);
	  if (anum == 0)
	    continue;
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

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;

create_user_error:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
ts_delete_user (nvplist * req, nvplist * res, char *_dbmt_error)
{
  DB_OBJECT *dbuser;
  char *newdbusername = nv_get_val (req, "username");

  if (_op_db_login (res, req, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  if ((dbuser = db_find_user (newdbusername)) == NULL)
    goto delete_user_error;

  if (db_drop_user (dbuser) < 0)
    goto delete_user_error;

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;

delete_user_error:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
ts_update_user (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  int errcode;
  DB_OBJECT *dbuser;
  DB_OBJECT *obj;
  int aset;
  DB_COLLECTION *gset;
  DB_VALUE val;

  char *dbpasswd;
  char *dbname;
  char *newdbusername;
  char *newdbuserpass;

  int i, sect, sect_len;
  char *tval, *sval;
  int anum;

  dbpasswd = nv_get_val (req, "_DBPASSWD");
  dbname = nv_get_val (req, "_DBNAME");
  newdbusername = nv_get_val (req, "username");
  newdbuserpass = nv_get_val (req, "userpass");

  if (_op_db_login (res, req, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  if ((dbuser = db_find_user (newdbusername)) == NULL)
    goto update_user_error;

  if (newdbuserpass)
    {				/* if password need to be changed ... */
      char *dbmt_db_id;
      char *old_db_passwd;

      dbmt_db_id = nv_get_val (req, "_DBID");
      if (dbmt_db_id == NULL)
	dbmt_db_id = "";

      if (strcasecmp (newdbusername, dbmt_db_id) == 0)
	old_db_passwd = dbpasswd;
      else
	old_db_passwd = NULL;

      /* set new password */
      if (uStringEqual (newdbuserpass, "__NULL__"))
	newdbuserpass = "";
      errcode = db_set_password (dbuser, old_db_passwd, newdbuserpass);
      if (errcode < 0)
	{
	  goto update_user_error;
	}
    }

  /* clear existing group - clear group, direct group */
  gset = db_col_create (DB_TYPE_SET, 0, NULL);
  DB_MAKE_SET (&val, gset);
  if (db_put (dbuser, "groups", &val) < 0)
    goto update_user_error;
  if (db_put (dbuser, "direct_groups", &val) < 0)
    goto update_user_error;
  db_col_free (gset);

#if 0
  if (db_add_member (db_find_user ("PUBLIC"), dbuser) < 0)
    goto update_user_error;
#endif

  /* make itself a member of other groups */
  nv_locate (req, "groups", &sect, &sect_len);
  if (sect >= 0)
    {
      for (i = 0; i < sect_len; ++i)
	{
	  nv_lookup (req, sect + i, NULL, &tval);
	  if ((obj = db_find_user (tval)) == NULL)
	    continue;
	  if (db_add_member (obj, dbuser) < 0)
	    goto update_user_error;
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
	    continue;
	  if (db_drop_member (dbuser, obj) < 0)
	    goto update_user_error;
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
	    continue;
	  if (db_add_member (dbuser, obj) < 0)
	    goto update_user_error;
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
	    continue;
	  if (db_is_system_class (obj))
	    continue;

	  anum = atoi (tval);
	  if (anum == 0)
	    continue;
	  aset = anum & DB_AUTH_ALL;
	  if (db_grant (dbuser, obj, (DB_AUTH) aset, 0) < 0)
	    continue;

	  aset = (anum >> 8) & (DB_AUTH_ALL);
	  if (db_grant (dbuser, obj, (DB_AUTH) aset, 1) < 0)
	    continue;
	}
    }

  db_commit_transaction ();
  db_shutdown ();

#ifndef	PK_AUTHENTICAITON
  if (newdbuserpass)
    {
      if (dbmt_user_read (&dbmt_user, _dbmt_error) == ERR_NO_ERROR)
	{
	  char hexacoded[PASSWD_ENC_LENGTH];
	  int src_dbinfo;

	  uEncrypt (PASSWD_LENGTH, newdbuserpass, hexacoded);
	  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
	    {
	      src_dbinfo =
		dbmt_user_search (&(dbmt_user.user_info[i]), dbname);
	      if (src_dbinfo < 0)
		continue;
	      if (strcmp
		  (dbmt_user.user_info[i].dbinfo[src_dbinfo].uid,
		   newdbusername) != 0)
		{
		  continue;
		}
	      strcpy (dbmt_user.user_info[i].dbinfo[src_dbinfo].passwd,
		      hexacoded);
	    }
	  dbmt_user_write_cubrid_pass (&dbmt_user, _dbmt_error);
	  dbmt_user_free (&dbmt_user);
	}
    }
#endif

  return ERR_NO_ERROR;

update_user_error:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
ts_check_authority (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *id, *pwd, *dbname;
  int _ret;

  T_DB_SERVICE_MODE db_mode;

  if ((dbname = nv_get_val (in, "_DBNAME")) == NULL)
    return ERR_PARAM_MISSING;

  id = nv_get_val (in, "_DBID");
  pwd = nv_get_val (in, "_DBPASSWD");

  if (!_isRegisteredDB (dbname))
    return ERR_DB_NONEXISTANT;

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  _ret = op_db_user_pass_check (dbname, id, pwd);

  nv_add_nvp (out, "dbname", dbname);

  if (_ret == 0)
    nv_add_nvp (out, "authority", "none");
  else if (_ret == 1)
    {
      if (uStringEqual (id, "dba"))
	nv_add_nvp (out, "authority", "dba");
      else
	nv_add_nvp (out, "authority", "general");
    }

  return ERR_NO_ERROR;
}

int
ts_class_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *dbstatus, *dbname;
  T_DB_SERVICE_MODE db_mode;

  dbstatus = nv_get_val (in, "dbstatus");
  dbname = nv_get_val (in, "_DBNAME");

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      char *uid = nv_get_val (in, "_DBID");
      char *passwd = nv_get_val (in, "_DBPASSWD");
      return (class_info_sa (dbname, uid, passwd, out, _dbmt_error));
    }
  else
    {
      if (_op_db_login (out, in, _dbmt_error))
	return ERR_WITH_MSG;

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

      db_commit_transaction ();
      db_shutdown ();
    }

  return ERR_NO_ERROR;
}

int
ts_class (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *classname;
  DB_OBJECT *classobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  classname = nv_get_val (in, "classname");
  classobj = db_find_class (classname);
  if (classobj == NULL)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_rename_class (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *old_class_name, *new_class_name;
  DB_OBJECT *class_;
  int errcode;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  old_class_name = nv_get_val (in, "oldclassname");
  new_class_name = nv_get_val (in, "newclassname");
  class_ = db_find_class (old_class_name);
  if ((errcode = db_rename_class (class_, new_class_name)) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  nv_add_nvp (out, "dbname", nv_get_val (in, "dbname"));
  nv_add_nvp (out, "classname", new_class_name);

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
  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_drop_class (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name;
  DB_OBJECT *class_;
  int errcode;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  class_ = db_find_class (class_name);
  if ((errcode = db_drop_class (class_)) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  nv_add_nvp (out, "dbname", nv_get_val (in, "dbname"));
  nv_add_nvp (out, "classname", class_name);

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

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_add_attribute (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *attr_name, *type, *category, *defaultv,
    *unique, *notnull;
  char buf[1024];
  DB_OBJECT *classobj;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  attr_name = nv_get_val (in, "attributename");
  type = nv_get_val (in, "type");
  category = nv_get_val (in, "category");
  defaultv = nv_get_val (in, "default");
  unique = nv_get_val (in, "unique");
  notnull = nv_get_val (in, "notnull");
  classobj = db_find_class (class_name);

  sprintf (buf, "ALTER \"%s\" ADD %s ATTRIBUTE \"%s\" %s %s %s %s %s",
	   class_name,
	   uStringEqual (category, "class") ? "CLASS" : "",
	   attr_name,
	   type,
	   uStringEqual (category, "shared") ? "SHARED" :
	   (defaultv != NULL) ? "DEFAULT" : "",
	   (defaultv != NULL) ? defaultv : "",
	   uStringEqual (unique, "yes") ? "UNIQUE" : "",
	   uStringEqual (notnull, "yes") ? "NOT NULL" : "");

  if (db_execute (buf, &result, &query_error) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }
  db_query_end (result);

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();

  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_drop_attribute (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *attr_name, *category;
  DB_OBJECT *classobj;
  int errcode;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  attr_name = nv_get_val (in, "attributename");
  category = nv_get_val (in, "category");
  classobj = db_find_class (class_name);

  if (!strcmp (category, "class"))
    errcode = db_drop_class_attribute (classobj, attr_name);
  else
    errcode = db_drop_attribute (classobj, attr_name);
  if (errcode < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_update_attribute (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *attr_name, *category, *index, *not_null, *unique,
    *defaultv, *old_attr_name;
  DB_OBJECT *classobj;
  DB_ATTRIBUTE *attrobj;
  int errcode, is_class, flag;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  attr_name = nv_get_val (in, "newattributename");
  index = nv_get_val (in, "index");
  not_null = nv_get_val (in, "notnull");
  unique = nv_get_val (in, "unique");
  defaultv = nv_get_val (in, "default");
  old_attr_name = nv_get_val (in, "oldattributename");
  category = nv_get_val (in, "category");
  classobj = db_find_class (class_name);

  if (strcmp (category, "instance") == 0)
    is_class = 0;
  else
    is_class = 1;

  if (strcmp (attr_name, old_attr_name) != 0)
    {
      errcode = db_rename (classobj, old_attr_name, is_class, attr_name);
      if (errcode < 0)
	{
	  goto update_attr_error;
	}
    }

  if (is_class)
    attrobj = db_get_class_attribute (classobj, attr_name);
  else
    attrobj = db_get_attribute (classobj, attr_name);

  flag = db_attribute_is_indexed (attrobj);
  if (!flag && !strcmp (index, "y"))
    {
      if (db_add_index (classobj, attr_name) < 0)
	goto update_attr_error;
    }
  else if (flag && !strcmp (index, "n"))
    {
      if (db_drop_index (classobj, attr_name) < 0)
	goto update_attr_error;
    }

  flag = db_attribute_is_non_null (attrobj);
  if (!flag && !strcmp (not_null, "y"))
    {
      if (db_constrain_non_null (classobj, attr_name, is_class, 1) < 0)
	goto update_attr_error;
    }
  else if (flag && !strcmp (not_null, "n"))
    {
      if (db_constrain_non_null (classobj, attr_name, is_class, 0) < 0)
	goto update_attr_error;
    }

  flag = db_attribute_is_unique (attrobj);
  if (!flag && !strcmp (unique, "y"))
    {
      int errcode;
      errcode = db_constrain_unique (classobj, attr_name, 1);
      if (errcode < 0 && errcode != ER_SM_INDEX_EXISTS)
	goto update_attr_error;
    }
  else if (flag && !strcmp (unique, "n"))
    {
      if (db_constrain_unique (classobj, attr_name, 0) < 0)
	goto update_attr_error;
    }

  /* default value management */
  if (defaultv != NULL)
    {
      char buf[1024];
      DB_QUERY_ERROR error_stats;
      DB_QUERY_RESULT *result;

      if (is_class)
	sprintf (buf, "ALTER \"%s\" CHANGE CLASS \"%s\" DEFAULT %s",
		 class_name, attr_name, defaultv);
      else
	sprintf (buf, "ALTER \"%s\" CHANGE \"%s\" DEFAULT %s",
		 class_name, attr_name, defaultv);
      if (db_execute (buf, &result, &error_stats) < 0)
	goto update_attr_error;
      db_query_end (result);
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;

update_attr_error:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
ts_add_constraint (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *type, *name, *category, *attr_count_string, **attr_names;
  char **attr_orders, *reference_class_name, **foreign_key_names;
  char **reference_key_names;
  char *n, *v;
  char query[512];
  DB_OBJECT *classobj;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;
  int errcode, attr_count, i, j, k, l, m, is_class;

  errcode = j = k = l = m = 0;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  type = nv_get_val (in, "type");
  name = nv_get_val (in, "name");
  category = nv_get_val (in, "category");
  if (!strcmp (category, "instance"))
    {
      is_class = 0;
    }
  else
    {
      is_class = 1;
    }

  reference_class_name = nv_get_val (in, "refclsname");
  attr_count_string = nv_get_val (in, "attributecount");
  attr_count = atoi (attr_count_string);
  attr_names = (char **) malloc (sizeof (char *) * (attr_count + 1));
  attr_orders = (char **) malloc (sizeof (char *) * (attr_count + 1));
  foreign_key_names = (char **) malloc (sizeof (char *) * (attr_count + 1));
  reference_key_names = (char **) malloc (sizeof (char *) * (attr_count + 1));

  for (i = 0; i < in->nvplist_leng; i++)
    {
      nv_lookup (in, i, &n, &v);
      if (!strcmp (n, "attribute"))
	{
	  attr_names[j++] = v;
	}
      if (!strcmp (n, "attribute_order"))
	{
	  attr_orders[k++] = v;
	}
      if (!strcmp (n, "forikey"))
	{
	  foreign_key_names[l++] = v;
	}
      if (!strcmp (n, "refkey"))
	{
	  reference_key_names[m++] = v;
	}
    }

  attr_names[j++] = NULL;
  attr_orders[k++] = NULL;
  foreign_key_names[l++] = NULL;
  reference_key_names[m++] = NULL;

  if (!strcmp (type, "UNIQUE") || !strcmp (type, "INDEX"))
    {
      if (class_name != NULL)
	{
	  memset (query, 0, sizeof (char) * 512);
	  sprintf (query, "CREATE %s INDEX \"%s\" on \"%s\" ( ",
		   (!strcmp (type, "UNIQUE") ? type : " "), name, class_name);
	  for (i = 0; i < attr_count; i++)
	    {
	      if (i == (attr_count - 1))
		{
		  sprintf (query + strlen (query), "\"%s\" %s );",
			   attr_names[i], attr_orders[i]);
		}
	      else
		{
		  sprintf (query + strlen (query), "\"%s\" %s ,",
			   attr_names[i], attr_orders[i]);
		}
	    }

	  if (db_execute (query, &result, &query_error) < 0)
	    {
	      CUBRID_ERR_MSG_SET (_dbmt_error);
	      db_shutdown ();
	      errcode = ERR_WITH_MSG;
	      goto add_con_final;
	    }
	  db_query_end (result);
	}
    }
  else if (!strcmp (type, "REVERSE UNIQUE"))
    {
      errcode =
	db_add_constraint (classobj, DB_CONSTRAINT_REVERSE_UNIQUE, name,
			   (const char **) attr_names, is_class);
    }
  else if (!strcmp (type, "REVERSE INDEX"))
    {
      errcode =
	db_add_constraint (classobj, DB_CONSTRAINT_REVERSE_INDEX, name,
			   (const char **) attr_names, is_class);
    }
  else if (!strcmp (type, "PRIMARY KEY"))
    {
      errcode = db_add_constraint (classobj, DB_CONSTRAINT_PRIMARY_KEY, name,
				   (const char **) attr_names, is_class);
    }
  else if (!strcmp (type, "FOREIGN KEY"))
    {
      if (class_name != NULL)
	{
	  memset (query, 0, sizeof (char) * 512);
	  sprintf (query,
		   "ALTER TABLE \"%s\" ADD CONSTRAINT \"%s\" FOREIGN KEY ( ",
		   class_name, name);
	  for (i = 0; i < attr_count; i++)
	    {
	      if (i == (attr_count - 1))
		{
		  sprintf (query + strlen (query),
			   "\"%s\" ) references \"%s\" ( ",
			   foreign_key_names[i], reference_class_name);
		}
	      else
		{
		  sprintf (query + strlen (query), "\"%s\" ,",
			   foreign_key_names[i]);
		}
	    }

	  for (i = 0; i < attr_count; i++)
	    {
	      if (i == (attr_count - 1))
		{
		  sprintf (query + strlen (query), "\"%s\" );",
			   reference_key_names[i]);
		}
	      else
		{
		  sprintf (query + strlen (query), "\"%s\" ,",
			   reference_key_names[i]);
		}
	    }

	  if (db_execute (query, &result, &query_error) < 0)
	    {
	      CUBRID_ERR_MSG_SET (_dbmt_error);
	      db_shutdown ();
	      errcode = ERR_WITH_MSG;
	      goto add_con_final;
	    }
	  db_query_end (result);
	}
    }
  else
    {
      errcode = db_add_constraint (classobj, DB_CONSTRAINT_NOT_NULL, name,
				   (const char **) attr_names, is_class);
    }

  if (errcode < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      errcode = ERR_WITH_MSG;
      goto add_con_final;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      errcode = ERR_WITH_MSG;
    }

add_con_final:
  if (attr_names)
    {
      free (attr_names);
    }

  if (attr_orders)
    {
      free (attr_orders);
    }

  if (foreign_key_names)
    {
      free (foreign_key_names);
    }

  if (reference_key_names)
    {
      free (reference_key_names);
    }

  if (errcode == ERR_WITH_MSG)
    {
      return errcode;
    }

  db_commit_transaction ();
  db_shutdown ();

  return ERR_NO_ERROR;
}

int
ts_drop_constraint (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *type, *name, *category, *attr_count_string, **attr_names;
  char *n, *v;
  DB_OBJECT *classobj;
  int errcode, attr_count, i, j, is_class;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;

  errcode = j = 0;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  type = nv_get_val (in, "type");
  name = nv_get_val (in, "name");
  category = nv_get_val (in, "category");

  if (!strcmp (category, "instance"))
    {
      is_class = 0;
    }
  else
    {
      is_class = 1;
    }

  attr_count_string = nv_get_val (in, "attributecount");
  attr_count = atoi (attr_count_string);
  attr_names = (char **) malloc (sizeof (char *) * (attr_count + 1));

  for (i = 0; i < in->nvplist_leng; i++)
    {
      nv_lookup (in, i, &n, &v);
      if (!strcmp (n, "attribute"))
	attr_names[j++] = v;
    }
  attr_names[j++] = NULL;

  if (!strcmp (type, "UNIQUE"))
    {
      errcode = db_drop_constraint (classobj, DB_CONSTRAINT_UNIQUE, name,
				    (const char **) attr_names, is_class);
    }
  else if (!strcmp (type, "INDEX"))
    {
      errcode = db_drop_constraint (classobj, DB_CONSTRAINT_INDEX, name,
				    (const char **) attr_names, is_class);
    }
  else if (!strcmp (type, "REVERSE INDEX"))
    {
      errcode =
	db_drop_constraint (classobj, DB_CONSTRAINT_REVERSE_INDEX, name,
			    (const char **) attr_names, is_class);
    }
  else if (!strcmp (type, "REVERSE UNIQUE"))
    {
      errcode =
	db_drop_constraint (classobj, DB_CONSTRAINT_REVERSE_UNIQUE, name,
			    (const char **) attr_names, is_class);
    }
  else if (!strcmp (type, "PRIMARY KEY"))
    {
      errcode = db_drop_constraint (classobj, DB_CONSTRAINT_PRIMARY_KEY, name,
				    (const char **) attr_names, is_class);
    }
  else if (!strcmp (type, "FOREIGN KEY"))
    {
      if (class_name != NULL)
	{
	  char query[512];
	  memset (query, 0, sizeof (char) * 512);
	  sprintf (query, "ALTER TABLE \"%s\" DROP CONSTRAINT \"%s\" ",
		   class_name, name);
	  if (db_execute (query, &result, &query_error) < 0)
	    {
	      CUBRID_ERR_MSG_SET (_dbmt_error);
	      db_shutdown ();
	      errcode = ERR_WITH_MSG;
	      goto drop_con_final;
	    }
	  db_query_end (result);
	}
    }
  else
    {
      errcode = db_drop_constraint (classobj, DB_CONSTRAINT_NOT_NULL, name,
				    (const char **) attr_names, is_class);
    }

  if (errcode < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      errcode = ERR_WITH_MSG;
      goto drop_con_final;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      errcode = ERR_WITH_MSG;
    }

drop_con_final:
  if (attr_names)
    {
      free (attr_names);
    }

  if (errcode == ERR_WITH_MSG)
    {
      return errcode;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_add_super (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *super;
  DB_OBJECT *classobj, *superobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  super = nv_get_val (in, "super");
  superobj = db_find_class (super);

  if (db_add_super (classobj, superobj) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_drop_super (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *super;
  DB_OBJECT *classobj, *superobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  super = nv_get_val (in, "super");
  superobj = db_find_class (super);

  if (db_drop_super (classobj, superobj) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_get_superclasses_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name;
  DB_OBJECT *classobj, *obj;
  DB_OBJLIST *objlist, *temp;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);

  nv_add_nvp (out, "open", "superclassesinfo");

  objlist = db_get_superclasses (classobj);
  for (temp = objlist; temp != NULL; temp = temp->next)
    {
      obj = temp->op;
      _op_get_super_info (out, obj);
    }
  db_objlist_free (objlist);

  nv_add_nvp (out, "close", "superclassesinfo");

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_add_resolution (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *super, *name, *alias, *category;
  DB_OBJECT *classobj, *superobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  super = nv_get_val (in, "super");
  superobj = db_find_class (super);
  name = nv_get_val (in, "name");
  alias = nv_get_val (in, "alias");
  category = nv_get_val (in, "category");
  if (!strcmp (category, "instance"))
    {
      if (db_add_resolution (classobj, superobj, name, alias) < 0)
	goto add_resolution_error;
    }
  else
    {
      if (db_add_class_resolution (classobj, superobj, name, alias) < 0)
	goto add_resolution_error;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;

add_resolution_error:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
ts_drop_resolution (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *super, *name, *category;
  DB_OBJECT *classobj, *superobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  super = nv_get_val (in, "super");
  superobj = db_find_class (super);
  name = nv_get_val (in, "name");
  category = nv_get_val (in, "category");

  if (!strcmp (category, "instance"))
    {
      if (db_drop_resolution (classobj, superobj, name) < 0)
	goto drop_resolution_error;
    }
  else
    {
      if (db_drop_class_resolution (classobj, superobj, name) < 0)
	goto drop_resolution_error;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;

drop_resolution_error:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
ts_add_method (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *method_name, *implementation, *category;
  DB_OBJECT *classobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  method_name = nv_get_val (in, "methodname");
  implementation = nv_get_val (in, "implementation");
  category = nv_get_val (in, "category");

  if (!strcmp (category, "instance"))
    {
      if (db_add_method (classobj, method_name, implementation) < 0)
	goto add_method_error;
    }
  else
    {
      if (db_add_class_method (classobj, method_name, implementation) < 0)
	goto add_method_error;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;

add_method_error:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
ts_drop_method (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *method_name, *category;
  DB_OBJECT *classobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  method_name = nv_get_val (in, "methodname");
  category = nv_get_val (in, "category");

  if (!strcmp (category, "instance"))
    {
      if (db_drop_method (classobj, method_name) < 0)
	goto drop_method_error;
    }
  else
    {
      if (db_drop_class_method (classobj, method_name) < 0)
	goto drop_method_error;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;

drop_method_error:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
ts_update_method (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *method_name, *implementation, *old_method_name,
    *category;
  char *n, *v;
  DB_OBJECT *classobj;
  DB_METHOD *methodobj;
  int errcode, is_class, i, j = 0;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  method_name = nv_get_val (in, "newmethodname");
  implementation = nv_get_val (in, "implementation");
  old_method_name = nv_get_val (in, "oldmethodname");
  category = nv_get_val (in, "category");

  if (!strcmp (category, "instance"))
    is_class = 0;
  else
    is_class = 1;

  if (strcmp (method_name, old_method_name))
    {
      if (db_rename (classobj, old_method_name, is_class, method_name) < 0)
	goto update_method_error;
    }

  for (i = 0; i < in->nvplist_leng; i++)
    {
      nv_lookup (in, i, &n, &v);
      if (!strcmp (n, "argument"))
	{
	  if (v == NULL)
	    errcode =
	      db_add_argument (classobj, method_name, is_class, j++, NULL);
	  else
	    errcode =
	      db_add_argument (classobj, method_name, is_class, j++, v);
	  if (errcode < 0)
	    goto update_method_error;
	}
    }

  if (is_class)
    methodobj = db_get_class_method (classobj, method_name);
  else
    methodobj = db_get_method (classobj, method_name);

  if (strcmp (implementation, db_method_function (methodobj)))
    {
      if (db_change_method_implementation
	  (classobj, method_name, is_class, implementation) < 0)
	{
	  goto update_method_error;
	}
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;

update_method_error:
  CUBRID_ERR_MSG_SET (_dbmt_error);
  db_shutdown ();
  return ERR_WITH_MSG;
}

int
ts_add_method_file (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *method_file;
  DB_OBJECT *classobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  method_file = nv_get_val (in, "methodfile");

  if (db_add_method_file (classobj, method_file) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_drop_method_file (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *method_file;
  DB_OBJECT *classobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  classobj = db_find_class (class_name);
  method_file = nv_get_val (in, "methodfile");

  if (db_drop_method_file (classobj, method_file) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_add_query_spec (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *vclass_name, *query_spec;
  DB_OBJECT *vclassobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  vclass_name = nv_get_val (in, "vclassname");
  vclassobj = db_find_class (vclass_name);
  query_spec = nv_get_val (in, "queryspec");

  if (db_add_query_spec (vclassobj, query_spec) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, vclassobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_drop_query_spec (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *vclass_name, *query_number;
  DB_OBJECT *vclassobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  vclass_name = nv_get_val (in, "vclassname");
  vclassobj = db_find_class (vclass_name);
  query_number = nv_get_val (in, "querynumber");

  if (db_drop_query_spec (vclassobj, atoi (query_number)) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, vclassobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_change_query_spec (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *vclass_name, *query_number, *query_spec;
  DB_OBJECT *vclassobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  vclass_name = nv_get_val (in, "vclassname");
  vclassobj = db_find_class (vclass_name);
  query_number = nv_get_val (in, "querynumber");
  query_spec = nv_get_val (in, "queryspec");

  if (db_change_query_spec (vclassobj, query_spec, atoi (query_number)) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, vclassobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_validate_query_spec (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *vclass_name, *query_spec;
  DB_OBJECT *vclassobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  vclass_name = nv_get_val (in, "vclassname");
  vclassobj = db_find_class (vclass_name);
  query_spec = nv_get_val (in, "queryspec");

  if (db_validate_query_spec (vclassobj, query_spec) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, vclassobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_validate_vclass (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *vclass_name;
  DB_OBJECT *vclassobj;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  vclass_name = nv_get_val (in, "vclassname");
  vclassobj = db_find_class (vclass_name);

  if (db_validate (vclassobj) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  if (_op_get_detailed_class_info (out, vclassobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }

  db_commit_transaction ();
  db_shutdown ();
  return ERR_NO_ERROR;
}

/******************************************************************************
	TASK UNICAS
******************************************************************************/

int
ts2_get_unicas_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  T_DM_UC_INFO uc_info;
  int i;
  T_DM_UC_CONF uc_conf;

  if (uca_unicas_conf (&uc_conf, NULL, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  if (uca_br_info (&uc_info, _dbmt_error) < 0)
    {
      char *p;
      int tmp_val;

      nv_add_nvp (out, "open", "brokersinfo");
      for (i = 0; i < uc_conf.num_broker; i++)
	{
	  nv_add_nvp (out, "open", "broker");
	  nv_add_nvp (out, "name",
		      uca_conf_find (&(uc_conf.br_conf[i]), "%"));
	  nv_add_nvp (out, "port",
		      uca_conf_find (&(uc_conf.br_conf[i]), "BROKER_PORT"));
	  nv_add_nvp (out, "appl_server_shm_id",
		      uca_conf_find (&(uc_conf.br_conf[i]),
				     "APPL_SERVER_SHM_ID"));
	  p = uca_conf_find (&(uc_conf.br_conf[i]), "SOURCE_ENV");
	  tmp_val = 1;
	  if (p == NULL || *p == '\0')
	    tmp_val = 0;
	  nv_add_nvp_int (out, "source_env", tmp_val);
	  p = uca_conf_find (&(uc_conf.br_conf[i]), "ACCESS_LIST");
	  tmp_val = 1;
	  if (p == NULL || *p == '\0')
	    tmp_val = 0;
	  nv_add_nvp_int (out, "access_list", tmp_val);
	  nv_add_nvp (out, "close", "broker");
	}
      nv_add_nvp (out, "close", "brokersinfo");
      nv_add_nvp (out, "brokerstatus", "OFF");
    }
  else
    {
      char *shmid;
      nv_add_nvp (out, "open", "brokersinfo");
      for (i = 0; i < uc_info.num_info; i++)
	{
	  nv_add_nvp (out, "open", "broker");
	  nv_add_nvp (out, "name", uc_info.info.br_info[i].name);
	  nv_add_nvp (out, "type", uc_info.info.br_info[i].as_type);
	  if (strcmp (uc_info.info.br_info[i].status, "OFF") != 0)
	    {
	      nv_add_nvp_int (out, "pid", uc_info.info.br_info[i].pid);
	      nv_add_nvp_int (out, "port", uc_info.info.br_info[i].port);
	      nv_add_nvp_int (out, "as", uc_info.info.br_info[i].num_as);
	      nv_add_nvp_int (out, "jq", uc_info.info.br_info[i].num_job_q);
	      nv_add_nvp_int (out, "thr", uc_info.info.br_info[i].num_thr);
	      nv_add_nvp_float (out, "cpu", uc_info.info.br_info[i].pcpu,
				"%.2f");
	      nv_add_nvp_int (out, "time", uc_info.info.br_info[i].cpu_time);
	      nv_add_nvp_int (out, "req", uc_info.info.br_info[i].num_req);
	      nv_add_nvp (out, "auto", uc_info.info.br_info[i].auto_add);
	      nv_add_nvp (out, "ses",
			  uc_info.info.br_info[i].session_timeout);
	      if (uc_info.info.br_info[i].sql_log_on_off)
		{
		  if (uc_info.info.br_info[i].sql_log_time < 1000000)
		    nv_add_nvp_int (out, "sqll",
				    uc_info.info.br_info[i].sql_log_time);
		  else
		    nv_add_nvp (out, "sqll", "ON");
		}
	      else
		{
		  nv_add_nvp (out, "sqll", "OFF");
		}
	      nv_add_nvp (out, "log", uc_info.info.br_info[i].log_dir);
	    }
	  nv_add_nvp (out, "state", uc_info.info.br_info[i].status);
	  nv_add_nvp_int (out, "source_env",
			  uc_info.info.br_info[i].source_env_flag);
	  nv_add_nvp_int (out, "access_list",
			  uc_info.info.br_info[i].access_list_flag);
	  shmid =
	    uca_conf_find (uca_conf_find_broker
			   (&uc_conf, uc_info.info.br_info[i].name),
			   "APPL_SERVER_SHM_ID");
	  nv_add_nvp (out, "appl_server_shm_id", shmid);
	  nv_add_nvp (out, "close", "broker");
	}
      nv_add_nvp (out, "close", "brokersinfo");
      nv_add_nvp (out, "brokerstatus", "ON");
      uca_br_info_free (&uc_info);
    }

  uca_unicas_conf_free (&uc_conf);
  return ERR_NO_ERROR;
}

int
ts2_get_broker_on_conf (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;
  int i;
  T_DM_UC_INFO uc_info;
  T_DM_UC_BR_INFO *br_info;

  bname = nv_get_val (in, "bname");
  if (bname == NULL)
    {
      sprintf (_dbmt_error, "broker name");
      return ERR_PARAM_MISSING;
    }

  if (uca_br_info (&uc_info, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  br_info = NULL;
  for (i = 0; i < uc_info.num_info; i++)
    {
      if (strcmp (bname, uc_info.info.br_info[i].name) == 0)
	{
	  br_info = &(uc_info.info.br_info[i]);
	  break;
	}
    }
  if (br_info != NULL)
    {
      if (br_info->sql_log_on_off)
	nv_add_nvp (out, "sql_log", "ON");
      else
	nv_add_nvp (out, "sql_log", "OFF");
      nv_add_nvp_int (out, "sql_log_time", br_info->sql_log_time);
      nv_add_nvp_int (out, "appl_server_max_size", br_info->as_max_size);
      if (br_info->log_backup)
	nv_add_nvp (out, "log_backup", "ON");
      else
	nv_add_nvp (out, "log_backup", "OFF");
      nv_add_nvp_int (out, "time_to_kill", br_info->time_to_kill);
    }
  uca_br_info_free (&uc_info);

  return ERR_NO_ERROR;
}

int
ts2_start_unicas (nvplist * in, nvplist * out, char *_dbmt_error)
{
  if (uca_start (_dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_stop_unicas (nvplist * in, nvplist * out, char *_dbmt_error)
{
  if (uca_stop (_dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_get_admin_log_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char buf[512];
  struct stat statbuf;

  uca_get_file (UC_FID_ADMIN_LOG, buf);

  if (stat (buf, &statbuf) != 0)
    {
      return ERR_STAT;
    }
  nv_add_nvp (out, "open", "adminloginfo");
  nv_add_nvp (out, "path", buf);
  nv_add_nvp (out, "owner", get_user_name (statbuf.st_uid, buf));
  nv_add_nvp_int (out, "size", statbuf.st_size);
  nv_add_nvp_time (out, "lastupdate", statbuf.st_mtime, "%04d.%02d.%02d",
		   NV_ADD_DATE);
  nv_add_nvp (out, "close", "adminloginfo");

  return ERR_NO_ERROR;
}

int
ts2_get_logfile_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
#ifdef WIN32
  HANDLE handle;
  WIN32_FIND_DATA data;
  char find_file[512];
  int found;
#else
  DIR *dp;
  struct dirent *dirp;
#endif
  struct stat statbuf;
  T_DM_UC_CONF uc_conf;
  char logdir[512], err_logdir[512], access_logdir[512], *v;
  char *bname, *from, buf[1024], scriptdir[512];
  char *cur_file;

  bname = nv_get_val (in, "broker");
  from = nv_get_val (in, "from");
  nv_add_nvp (out, "broker", bname);
  nv_add_nvp (out, "from", from);
  nv_add_nvp (out, "open", "logfileinfo");

  if (uca_unicas_conf (&uc_conf, NULL, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  v = uca_conf_find (uca_conf_find_broker (&uc_conf, bname), "ERROR_LOG_DIR");
  if (v == NULL)
    v = BROKER_LOG_DIR "/error_log";
  uca_get_conf_path (v, err_logdir);

  v = uca_conf_find (uca_conf_find_broker (&uc_conf, bname), "LOG_DIR");
  if (v == NULL)
    v = BROKER_LOG_DIR "/sql_log";
  uca_get_conf_path (v, logdir);

  uca_get_conf_path (BROKER_LOG_DIR, access_logdir);

  uca_unicas_conf_free (&uc_conf);


#ifdef WIN32
  sprintf (find_file, "%s/*", access_logdir);
  handle = FindFirstFile (find_file, &data);
  if (handle == INVALID_HANDLE_VALUE)
    {
      nv_add_nvp (out, "open", "logfileinfo");
      nv_add_nvp (out, "close", "logfileinfo");
      return ERR_NO_ERROR;
    }
#else
  dp = opendir (access_logdir);
  if (dp == NULL)
    {
      nv_add_nvp (out, "open", "logfileinfo");
      nv_add_nvp (out, "close", "logfileinfo");
      return ERR_NO_ERROR;
    }
#endif

#ifdef WIN32
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dirp = readdir (dp)) != NULL)
#endif
    {
#ifdef WIN32
      cur_file = data.cFileName;
#else
      cur_file = dirp->d_name;
#endif
      if (strstr (cur_file, bname) != NULL)
	{
	  nv_add_nvp (out, "open", "logfile");
	  if (strstr (cur_file, "access") != NULL)
	    nv_add_nvp (out, "type", "access");
	  sprintf (buf, "%s/%s", access_logdir, cur_file);
	  nv_add_nvp (out, "path", buf);
	  stat (buf, &statbuf);
	  nv_add_nvp (out, "owner", get_user_name (statbuf.st_uid, buf));
	  nv_add_nvp_int (out, "size", statbuf.st_size);
	  nv_add_nvp_time (out, "lastupdate", statbuf.st_mtime,
			   "%04d.%02d.%02d", NV_ADD_DATE);
	  nv_add_nvp (out, "close", "logfile");
	}
    }
#ifdef WIN32
  FindClose (handle);
#else
  closedir (dp);
#endif

#ifdef WIN32
  sprintf (find_file, "%s/*", err_logdir);
  handle = FindFirstFile (find_file, &data);
  if (handle == INVALID_HANDLE_VALUE)
    {
      nv_add_nvp (out, "open", "logfileinfo");
      nv_add_nvp (out, "close", "logfileinfo");
      return ERR_NO_ERROR;
    }
#else
  dp = opendir (err_logdir);
  if (dp == NULL)
    {
      nv_add_nvp (out, "open", "logfileinfo");
      nv_add_nvp (out, "close", "logfileinfo");
      return ERR_NO_ERROR;
    }
#endif

#ifdef WIN32
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dirp = readdir (dp)) != NULL)
#endif
    {
#ifdef WIN32
      cur_file = data.cFileName;
#else
      cur_file = dirp->d_name;
#endif
      if (strstr (cur_file, bname) != NULL)
	{
	  nv_add_nvp (out, "open", "logfile");
	  if (strstr (cur_file, "access") != NULL)
	    nv_add_nvp (out, "type", "access");
	  else if (strstr (cur_file, "err") != NULL)
	    nv_add_nvp (out, "type", "error");
	  sprintf (buf, "%s/%s", err_logdir, cur_file);
	  nv_add_nvp (out, "path", buf);
	  stat (buf, &statbuf);
	  nv_add_nvp (out, "owner", get_user_name (statbuf.st_uid, buf));
	  nv_add_nvp_int (out, "size", statbuf.st_size);
	  nv_add_nvp_time (out, "lastupdate", statbuf.st_mtime,
			   "%04d.%02d.%02d", NV_ADD_DATE);
	  nv_add_nvp (out, "close", "logfile");
	}
    }
#ifdef WIN32
  FindClose (handle);
#else
  closedir (dp);
#endif

#ifdef WIN32
  sprintf (find_file, "%s/*", logdir);
  handle = FindFirstFile (find_file, &data);
  if (handle == INVALID_HANDLE_VALUE)
    {
      return ERR_OPENDIR;
    }
#else
  dp = opendir (logdir);
  if (dp == NULL)
    {
      return ERR_OPENDIR;
    }
#endif

#ifdef WIN32
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dirp = readdir (dp)) != NULL)
#endif
    {
#ifdef WIN32
      cur_file = data.cFileName;
#else
      cur_file = dirp->d_name;
#endif
      if (strstr (cur_file, bname) != NULL)
	{
	  nv_add_nvp (out, "open", "logfile");
	  if (strstr (cur_file, "access") != NULL)
	    nv_add_nvp (out, "type", "access");
	  sprintf (buf, "%s/%s", logdir, cur_file);
	  nv_add_nvp (out, "path", buf);
	  stat (buf, &statbuf);
	  nv_add_nvp (out, "owner", get_user_name (statbuf.st_uid, buf));
	  nv_add_nvp_int (out, "size", statbuf.st_size);
	  nv_add_nvp_time (out, "lastupdate", statbuf.st_mtime,
			   "%04d.%02d.%02d", NV_ADD_DATE);
	  nv_add_nvp (out, "close", "logfile");
	}
    }
#ifdef WIN32
  FindClose (handle);
#else
  closedir (dp);
#endif

  sprintf (scriptdir, "%s", logdir);
#ifdef WIN32
  sprintf (find_file, "%s/*", scriptdir);
  handle = FindFirstFile (find_file, &data);
  if (handle == INVALID_HANDLE_VALUE)
    return ERR_OPENDIR;
#else
  dp = opendir (scriptdir);
  if (dp == NULL)
    return ERR_OPENDIR;
#endif

  sprintf (bname, "%s_", bname);
#ifdef WIN32
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dirp = readdir (dp)) != NULL)
#endif
    {
#ifdef WIN32
      cur_file = data.cFileName;
#else
      cur_file = dirp->d_name;
#endif

      if (strstr (cur_file, bname) != NULL)
	{
	  nv_add_nvp (out, "open", "logfile");
	  nv_add_nvp (out, "type", "script");
	  sprintf (buf, "%s/%s", scriptdir, cur_file);
	  nv_add_nvp (out, "path", buf);
	  stat (buf, &statbuf);
	  nv_add_nvp (out, "owner", get_user_name (statbuf.st_uid, buf));
	  nv_add_nvp_int (out, "size", statbuf.st_size);
	  nv_add_nvp_time (out, "lastupdate", statbuf.st_mtime,
			   "%04d.%02d.%02d", NV_ADD_DATE);
	  nv_add_nvp (out, "close", "logfile");
	}
    }
#ifdef WIN32
  FindClose (handle);
#else
  closedir (dp);
#endif
  nv_add_nvp (out, "close", "logfileinfo");

  return ERR_NO_ERROR;
}

int
ts2_add_broker (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;
  T_DM_UC_CONF uc_conf;
  int retval;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_unicas_conf (&uc_conf, NULL, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  if (uca_conf_find_broker (&uc_conf, bname) != NULL)
    {
      strcpy (_dbmt_error, "The Broker name already exists!");
      uca_unicas_conf_free (&uc_conf);
      return ERR_WITH_MSG;
    }

  if (uca_conf_broker_add (&uc_conf, bname, _dbmt_error) < 0)
    {
      uca_unicas_conf_free (&uc_conf);
      return ERR_WITH_MSG;
    }

  retval = uca_conf_write (&uc_conf, NULL, _dbmt_error);

  uca_unicas_conf_free (&uc_conf);
  return retval;
}

int
ts2_get_add_broker_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  FILE *infile;
  char broker_conf_path[512], strbuf[1024];

  uca_get_file (UC_FID_CUBRID_BROKER_CONF, broker_conf_path);

  if (access (broker_conf_path, F_OK) < 0)
    {
      return ERR_FILE_OPEN_FAIL;
    }

  infile = fopen (broker_conf_path, "r");
  if (infile == NULL)
    {
      strcpy (_dbmt_error, broker_conf_path);
      return ERR_FILE_OPEN_FAIL;
    }

  nv_add_nvp (out, "confname", "broker");
  nv_add_nvp (out, "open", "conflist");

  while (fgets (strbuf, sizeof (strbuf), infile) != NULL)
    {
      nv_add_nvp (out, "confdata", strbuf);
    }
  nv_add_nvp (out, "close", "conflist");
  fclose (infile);

  return ERR_NO_ERROR;
}

int
ts2_delete_broker (nvplist * in, nvplist * out, char *_dbmt_error)
{
  FILE *infile, *outfile;
  char *bname;
  char buf[1024], tmpfile[256];
  char fpath[512];
  int retval;
  T_DM_UC_CONF uc_conf;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_unicas_conf (&uc_conf, NULL, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  retval = uca_conf_write (&uc_conf, bname, _dbmt_error);
  uca_unicas_conf_free (&uc_conf);

  if (retval != ERR_NO_ERROR)
    return retval;

  /* autounicasm.conf */
  conf_get_dbmt_file (FID_AUTO_UNICASM_CONF, fpath);
  infile = fopen (fpath, "r");
  sprintf (tmpfile, "%s/DBMT_casop_%d.%d", sco.dbmt_tmp_dir, TS2_DELETEBROKER,
	   (int) getpid ());
  outfile = fopen (tmpfile, "w");
  if (infile == NULL || outfile == NULL)
    {
      if (infile)
	fclose (infile);
      if (outfile)
	fclose (outfile);
    }
  else
    {
      char conf_bname[128];
      while (fgets (buf, sizeof (buf), infile))
	{
	  if (sscanf (buf, "%s", conf_bname) < 1)
	    continue;
	  if (strcmp (conf_bname, bname) != 0)
	    fputs (buf, outfile);
	}
      fclose (infile);
      fclose (outfile);

      move_file (tmpfile, fpath);
    }

  return ERR_NO_ERROR;
}

int
ts2_rename_broker (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname, *newbname, buf[1024], ufile[256], tmpfile[256];
  FILE *infile, *outfile;
  int retval;
  T_DM_UC_CONF uc_conf;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;
  if ((newbname = nv_get_val (in, "newbname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_unicas_conf (&uc_conf, NULL, _dbmt_error) < 0)
    return ERR_WITH_MSG;
  uca_change_config (&uc_conf, bname, "%", newbname);
  retval = uca_conf_write (&uc_conf, NULL, _dbmt_error);
  uca_unicas_conf_free (&uc_conf);
  if (retval != ERR_NO_ERROR)
    return retval;

  /* autounicasm.conf */
  conf_get_dbmt_file (FID_AUTO_UNICASM_CONF, ufile);
  sprintf (tmpfile, "%s/DBMT_casop_%d.%d", sco.dbmt_tmp_dir, TS2_RENAMEBROKER,
	   (int) getpid ());
  infile = fopen (ufile, "r");
  outfile = fopen (tmpfile, "w");
  if (infile == NULL || outfile == NULL)
    {
      if (infile)
	fclose (infile);
      if (outfile)
	fclose (outfile);
    }
  else
    {
      char conf_bname[128];
      while (fgets (buf, sizeof (buf), infile))
	{
	  if (sscanf (buf, "%s", conf_bname) < 1)
	    continue;
	  if (strcmp (conf_bname, bname) == 0)
	    fprintf (outfile, "%s %s", newbname,
		     buf + strlen (conf_bname) + 1);
	  else
	    fputs (buf, outfile);
	}
      fclose (infile);
      fclose (outfile);
      move_file (tmpfile, ufile);
    }

  return ERR_NO_ERROR;
}

int
ts2_get_broker_status (nvplist * in, nvplist * out, char *_dbmt_error)
{
  T_DM_UC_INFO br_info, job_info;
  char *bname, buf[1024];
  int i;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  nv_add_nvp (out, "bname", bname);

  if (uca_as_info (bname, &br_info, &job_info, _dbmt_error) < 0)
    return ERR_NO_ERROR;

  for (i = 0; i < br_info.num_info; i++)
    {
      nv_add_nvp (out, "open", "asinfo");
      nv_add_nvp_int (out, "as_id", br_info.info.as_info[i].id);
      nv_add_nvp_int (out, "as_pid", br_info.info.as_info[i].pid);
      nv_add_nvp_int (out, "as_c", br_info.info.as_info[i].num_request);
      nv_add_nvp_int (out, "as_psize", br_info.info.as_info[i].psize);
      nv_add_nvp (out, "as_status", br_info.info.as_info[i].status);
      nv_add_nvp_float (out, "as_cpu", br_info.info.as_info[i].pcpu, "%.2f");
      uca_cpu_time_str (br_info.info.as_info[i].cpu_time, buf);
      nv_add_nvp (out, "as_ctime", buf);
      nv_add_nvp_time (out, "as_lat",
		       br_info.info.as_info[i].last_access_time,
		       "%02d/%02d/%02d %02d:%02d:%02d", NV_ADD_DATE_TIME);
      nv_add_nvp (out, "as_cur", br_info.info.as_info[i].log_msg);
      nv_add_nvp (out, "close", "asinfo");
    }
  for (i = 0; i < job_info.num_info; i++)
    {
      nv_add_nvp (out, "open", "jobinfo");
      nv_add_nvp_int (out, "job_id", job_info.info.job_info[i].id);
      nv_add_nvp_int (out, "job_priority",
		      job_info.info.job_info[i].priority);
      nv_add_nvp (out, "job_ip", ip2str (job_info.info.job_info[i].ip, buf));
      nv_add_nvp_time (out, "job_time", job_info.info.job_info[i].recv_time,
		       "%02d:%02d:%02d", NV_ADD_TIME);
      sprintf (buf, "%s:%s", job_info.info.job_info[i].script,
	       job_info.info.job_info[i].prgname);
      nv_add_nvp (out, "job_request", buf);
      nv_add_nvp (out, "close", "jobinfo");
    }

  uca_as_info_free (&br_info, &job_info);

  return ERR_NO_ERROR;
}

int
ts2_get_broker_conf (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;
  int i;
  T_DM_UC_CONF uc_conf;
  T_DM_UC_BR_CONF *br_conf;
  int master_shm_id;
  char shm_id_str[32];

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_unicas_conf (&uc_conf, &master_shm_id, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  sprintf (shm_id_str, "%X", master_shm_id);
  nv_add_nvp (out, "MASTER_SHM_ID", shm_id_str);

  br_conf = uca_conf_find_broker (&uc_conf, bname);
  if (br_conf != NULL)
    {
      nv_add_nvp (out, "bname", bname);
      for (i = 0; i < br_conf->num; i++)
	nv_add_nvp (out, br_conf->item[i].name, br_conf->item[i].value);
    }
  uca_unicas_conf_free (&uc_conf);

  return ERR_NO_ERROR;
}

static int
compare_br_conf (T_DM_UC_BR_CONF * br_conf, char *name, char *value)
{
  int i;

  if (br_conf == NULL)
    return 0;
  for (i = 0; i < br_conf->num; i++)
    {
      if (strcasecmp (br_conf->item[i].name, name) == 0)
	return (strcasecmp (br_conf->item[i].value, value));
    }
  return 0;
}

int
ts2_set_broker_conf (nvplist * in, nvplist * out, char *_dbmt_error)
{
  FILE *outfile;
  char broker_conf_path[512];
  char *conf, *confdata;
  int nv_len, i;

  uca_get_file (UC_FID_CUBRID_BROKER_CONF, broker_conf_path);

  if ((outfile = fopen (broker_conf_path, "w")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  nv_len = in->nvplist_leng;
  for (i = 1; i < nv_len; i++)
    {
      nv_lookup (in, i, &conf, &confdata);
      if (strcmp (conf, "confdata") == 0)
	{
	  if (confdata == NULL)
	    fprintf (outfile, "\n");
	  else
	    fprintf (outfile, "%s\n", confdata);
	}
    }

  fclose (outfile);

  return ERR_NO_ERROR;
}

int
ts2_set_broker_on_conf (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname, *n, *v;

  bname = nv_get_val (in, "bname");
  n = nv_get_val (in, "conf_name");
  v = nv_get_val (in, "conf_value");
  if (bname == NULL || n == NULL || v == NULL)
    return ERR_PARAM_MISSING;

  if (uca_changer (bname, n, v, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_start_broker (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    {
      strcpy (_dbmt_error, "broker name");
      return ERR_PARAM_MISSING;
    }

  if (uca_on (bname, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_stop_broker (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    {
      strcpy (_dbmt_error, "broker name");
      return ERR_PARAM_MISSING;
    }

  if (uca_off (bname, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_suspend_broker (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    {
      strcpy (_dbmt_error, "broker name");
      return ERR_PARAM_MISSING;
    }

  if (uca_suspend (bname, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_resume_broker (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    {
      strcpy (_dbmt_error, "broker name");
      return ERR_PARAM_MISSING;
    }

  if (uca_resume (bname, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_broker_job_first (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname, *jobnum;

  bname = nv_get_val (in, "bname");
  jobnum = nv_get_val (in, "jobnum");
  if (bname == NULL || jobnum == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  if (uca_job_first (bname, atoi (jobnum), _dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_broker_job_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  T_DM_UC_INFO br_info, job_info;
  char *bname, buf[1024];
  int i;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_as_info (bname, &br_info, &job_info, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  nv_add_nvp (out, "bname", bname);
  for (i = 0; i < job_info.num_info; i++)
    {
      nv_add_nvp (out, "open", "jobinfo");
      nv_add_nvp_int (out, "job_id", job_info.info.job_info[i].id);
      nv_add_nvp_int (out, "job_priority",
		      job_info.info.job_info[i].priority);
      nv_add_nvp (out, "job_ip", ip2str (job_info.info.job_info[i].ip, buf));
      nv_add_nvp_time (out, "job_time", job_info.info.job_info[i].recv_time,
		       "%02d:%02d:%02d", NV_ADD_TIME);
      sprintf (buf, "%s:%s", job_info.info.job_info[i].script,
	       job_info.info.job_info[i].prgname);
      nv_add_nvp (out, "job_request", buf);
      nv_add_nvp (out, "close", "jobinfo");
    }
  uca_as_info_free (&br_info, &job_info);

  return ERR_NO_ERROR;
}

int
ts2_add_broker_as (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_add (bname, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_drop_broker_as (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_drop (bname, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_restart_broker_as (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname, *asnum;

  bname = nv_get_val (in, "bname");
  asnum = nv_get_val (in, "asnum");
  if (bname == NULL || asnum == NULL)
    return ERR_PARAM_MISSING;

  if (uca_restart (bname, atoi (asnum), _dbmt_error) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts2_get_broker_status_log (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;
  char buf[1024];
  FILE *infile;
  char *val[7];

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  infile = fopen (conf_get_dbmt_file (FID_UC_AUTO_RESTART_LOG, buf), "rt");
  if (infile == NULL)
    {
      return ERR_NO_ERROR;
    }
  nv_add_nvp (out, "bname", bname);
  while (fgets (buf, sizeof (buf), infile))
    {
      ut_trim (buf);
      if (string_tokenize (buf, val, 7) < 0)
	continue;
      if (strcmp (val[0], bname) == 0)
	{
	  nv_add_nvp (out, "open", "statuslog");
	  nv_add_nvp (out, "time", val[1]);
	  nv_add_nvp (out, "asnum", val[2]);
	  nv_add_nvp (out, "psize", val[3]);
	  nv_add_nvp (out, "cpu_usage", val[4]);
	  nv_add_nvp (out, "busytime", val[5]);
	  nv_add_nvp (out, "restarted", val[6]);
	  nv_add_nvp (out, "close", "statuslog");
	}
    }

  return ERR_NO_ERROR;
}

int
ts2_get_broker_m_conf (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char buf[1024], *bname;
  char fpath[512];
  FILE *infile;
  char *conf_item[AUTOUNICAS_CONF_ENTRY_NUM];

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  conf_get_dbmt_file (FID_AUTO_UNICASM_CONF, fpath);
  infile = fopen (fpath, "r");
  if (infile == NULL)
    {
      return ERR_NO_ERROR;
    }
  while (fgets (buf, sizeof (buf), infile))
    {
      ut_trim (buf);
      if (buf[0] == '#')
	continue;
      if (string_tokenize (buf, conf_item, AUTOUNICAS_CONF_ENTRY_NUM) < 0)
	continue;
      if (strcmp (conf_item[0], bname) == 0)
	{
	  int i;
	  for (i = 0; i < AUTOUNICAS_CONF_ENTRY_NUM; i++)
	    nv_add_nvp (out, autounicas_conf_entry[i], conf_item[i]);
	}
    }

  return ERR_NO_ERROR;
}

int
ts2_set_broker_m_conf (nvplist * in, nvplist * out, char *_dbmt_error)
{
  FILE *infile, *outfile;
  char buf[1024], tmpfile[256];
  char fpath[512];
  char *conf_item[AUTOUNICAS_CONF_ENTRY_NUM];
  int i;
  char conf_bname[128];

  for (i = 0; i < AUTOUNICAS_CONF_ENTRY_NUM; i++)
    {
      conf_item[i] = nv_get_val (in, autounicas_conf_entry[i]);
      if (conf_item[i] == NULL)
	{
	  strcpy (_dbmt_error, autounicas_conf_entry[i]);
	  return ERR_PARAM_MISSING;
	}
    }

  conf_get_dbmt_file (FID_AUTO_UNICASM_CONF, fpath);
  if (access (fpath, F_OK) < 0)
    {
      outfile = fopen (fpath, "w");
      if (outfile == NULL)
	{
	  strcpy (_dbmt_error, fpath);
	  return ERR_FILE_OPEN_FAIL;
	}
      for (i = 0; i < AUTOUNICAS_CONF_ENTRY_NUM; i++)
	fprintf (outfile, "%s ", conf_item[i]);
      fprintf (outfile, "\n");
      fclose (outfile);
      return ERR_NO_ERROR;
    }

  if ((infile = fopen (fpath, "r")) == NULL)
    {
      strcpy (_dbmt_error, fpath);
      return ERR_FILE_OPEN_FAIL;
    }
  sprintf (tmpfile, "%s/DBMT_casop_%d.%d", sco.dbmt_tmp_dir,
	   TS2_SETBROKERMCONF, (int) getpid ());
  outfile = fopen (tmpfile, "w");
  if (outfile == NULL)
    {
      fclose (infile);
      return ERR_TMPFILE_OPEN_FAIL;
    }
  while (fgets (buf, sizeof (buf), infile))
    {
      if (sscanf (buf, "%s", conf_bname) < 1)
	continue;
      if (strcmp (conf_bname, conf_item[0]) != 0)
	fputs (buf, outfile);
    }
  for (i = 0; i < AUTOUNICAS_CONF_ENTRY_NUM; i++)
    fprintf (outfile, "%s ", conf_item[i]);
  fprintf (outfile, "\n");

  fclose (infile);
  fclose (outfile);
  move_file (tmpfile, fpath);

  return ERR_NO_ERROR;
}

int
ts2_get_broker_as_limit (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;
  int i;
  T_DM_UC_INFO uc_info;
  int num_as, max_as, min_as;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  num_as = min_as = max_as = 0;

  if (uca_br_info (&uc_info, _dbmt_error) >= 0)
    {
      for (i = 0; i < uc_info.num_info; i++)
	{
	  if (strcasecmp (uc_info.info.br_info[i].name, bname) == 0)
	    {
	      min_as = uc_info.info.br_info[i].min_as;
	      max_as = uc_info.info.br_info[i].max_as;
	      num_as = uc_info.info.br_info[i].num_as;
	      break;
	    }
	}
      uca_br_info_free (&uc_info);
    }

  nv_add_nvp_int (out, "minas", min_as);
  nv_add_nvp_int (out, "maxas", max_as);
  nv_add_nvp_int (out, "asnum", num_as);

  return ERR_NO_ERROR;
}

int
ts2_get_broker_env_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;
  char *env_file;
  T_DM_UC_CONF uc_conf;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  if (uca_unicas_conf (&uc_conf, NULL, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  env_file =
    uca_conf_find (uca_conf_find_broker (&uc_conf, bname), "SOURCE_ENV");
  if (env_file != NULL)
    {
      char buf[1024], n[128], v[128];
      FILE *infile;

      uca_get_conf_path (env_file, buf);
      if ((infile = fopen (buf, "rt")) != NULL)
	{
	  while (fgets (buf, sizeof (buf), infile))
	    {
	      if (sscanf (buf, "%s %s", n, v) != 2)
		continue;
	      nv_add_nvp (out, n, v);
	    }
	  fclose (infile);
	}
    }
  uca_unicas_conf_free (&uc_conf);

  return ERR_NO_ERROR;
}

int
ts2_set_broker_env_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;
  char *env_file;
  T_DM_UC_CONF uc_conf;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_unicas_conf (&uc_conf, NULL, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  env_file =
    uca_conf_find (uca_conf_find_broker (&uc_conf, bname), "SOURCE_ENV");
  if (env_file != NULL)
    {
      char buf[1024];
      FILE *outfile;
      int i, flag = 0;
      char *n, *v;

      uca_get_conf_path (env_file, buf);
      outfile = fopen (buf, "w");
      if (outfile == NULL)
	{
	  uca_unicas_conf_free (&uc_conf);
	  strcpy (_dbmt_error, buf);
	  return ERR_FILE_OPEN_FAIL;
	}
      else
	{
	  for (i = 0; i < in->nvplist_leng; i++)
	    {
	      nv_lookup (in, i, &n, &v);
	      if ((strcmp (n, "open") == 0) && (strcmp (v, "param") == 0))
		flag = 1;
	      else if ((strcmp (n, "close") == 0)
		       && (strcmp (v, "param") == 0))
		flag = 0;
	      else if (flag == 1 && n != NULL && v != NULL)
		{
		  fprintf (outfile, "%s	%s\n", n, v);
		}
	    }
	  fclose (outfile);
	}
    }
  uca_unicas_conf_free (&uc_conf);

  return ERR_NO_ERROR;
}

int
ts2_access_list_add_ip (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname, *ip;
  char *file_name;
  FILE *outfile;
  int start, length, i;
  char fpath[512];
  T_DM_UC_CONF uc_conf;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_unicas_conf (&uc_conf, NULL, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  file_name =
    uca_conf_find (uca_conf_find_broker (&uc_conf, bname), "ACCESS_LIST");
  if (file_name == NULL || file_name[0] == '\0')
    {
      uca_unicas_conf_free (&uc_conf);
      return ERR_UNICASCONF_PARAM_MISSING;
    }
  uca_get_conf_path (file_name, fpath);
  uca_unicas_conf_free (&uc_conf);

  outfile = fopen (fpath, "a");
  if (outfile == NULL)
    {
      sprintf (_dbmt_error, "%s", fpath);
      return ERR_FILE_OPEN_FAIL;
    }
  nv_locate (in, "ip", &start, &length);
  if (start >= 0)
    {
      for (i = 0; i < length; i++)
	{
	  nv_lookup (in, start + i, NULL, &ip);
	  fprintf (outfile, "%s\n", ip);
	}
    }
  fclose (outfile);

  return ERR_NO_ERROR;
}

int
ts2_access_list_delete_ip (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname, *ip;
  char buf[1024], tmpfile[256];
  char *file_name;
  FILE *infile, *outfile;
  char fpath[512];
  T_DM_UC_CONF uc_conf;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;
  if ((ip = nv_get_val (in, "ip")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_unicas_conf (&uc_conf, NULL, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  file_name =
    uca_conf_find (uca_conf_find_broker (&uc_conf, bname), "ACCESS_LIST");
  if (file_name == NULL || file_name[0] == '\0')
    {
      uca_unicas_conf_free (&uc_conf);
      return ERR_UNICASCONF_PARAM_MISSING;
    }
  uca_get_conf_path (file_name, fpath);
  uca_unicas_conf_free (&uc_conf);

  sprintf (tmpfile, "%s/DBMT_casop_%d.%d", sco.dbmt_tmp_dir,
	   TS2_ACCESSLISTDELETEIP, (int) getpid ());
  if ((infile = fopen (fpath, "rt")) == NULL)
    {
      strcpy (_dbmt_error, file_name);
      return ERR_FILE_OPEN_FAIL;
    }
  outfile = fopen (tmpfile, "w");
  if (outfile == NULL)
    {
      fclose (infile);
      return ERR_TMPFILE_OPEN_FAIL;
    }
  while (fgets (buf, sizeof (buf), infile))
    {
      ut_trim (buf);
      if (strcmp (buf, ip) != 0)
	fprintf (outfile, "%s\n", buf);
    }
  fclose (infile);
  fclose (outfile);
  move_file (tmpfile, fpath);
  return ERR_NO_ERROR;
}

int
ts2_access_list_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;
  char buf[1024];
  char fpath[512], *file_name;
  FILE *infile;
  T_DM_UC_CONF uc_conf;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uca_unicas_conf (&uc_conf, NULL, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  file_name =
    uca_conf_find (uca_conf_find_broker (&uc_conf, bname), "ACCESS_LIST");
  if (file_name == NULL || file_name[0] == '\0')
    {
      uca_unicas_conf_free (&uc_conf);
      return ERR_UNICASCONF_PARAM_MISSING;
    }
  uca_get_conf_path (file_name, fpath);
  uca_unicas_conf_free (&uc_conf);

  infile = fopen (fpath, "rt");
  if (infile == NULL)
    {
      return ERR_NO_ERROR;
    }
  nv_add_nvp (out, "open", "accesslistinfo");
  nv_add_nvp (out, "path", fpath);
  while (fgets (buf, sizeof (buf), infile))
    {
      ut_trim (buf);
      nv_add_nvp (out, "ip", buf);
    }
  fclose (infile);
  nv_add_nvp (out, "close", "accesslistinfo");

  return ERR_NO_ERROR;
}

/******************************************************************************
	TASK CUBRID_CONF
******************************************************************************/

int
ts_set_sysparam (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *outfile;
  char cubridconf_path[512];
  char *conf, *confdata;
  int nv_len, i;

  sprintf (cubridconf_path, "%s/conf/%s", sco.szCubrid, CUBRID_CUBRID_CONF);

  if ((outfile = fopen (cubridconf_path, "w")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  nv_len = req->nvplist_leng;
  for (i = 1; i < nv_len; i++)
    {
      nv_lookup (req, i, &conf, &confdata);
      if (strcmp (conf, "confdata") == 0)
	{
	  if (confdata == NULL)
	    fprintf (outfile, "\n");
	  else
	    fprintf (outfile, "%s\n", confdata);
	}
    }

  fclose (outfile);

  return ERR_NO_ERROR;
}

int
ts_get_all_sysparam (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *infile;
  char cubridconf_path[512], strbuf[1024];

  sprintf (cubridconf_path, "%s/conf/%s", sco.szCubrid, CUBRID_CUBRID_CONF);

  if (access (cubridconf_path, F_OK) < 0)
    {
      return ERR_FILE_OPEN_FAIL;
    }

  infile = fopen (cubridconf_path, "r");
  if (infile == NULL)
    {
      strcpy (_dbmt_error, cubridconf_path);
      return ERR_FILE_OPEN_FAIL;
    }

  nv_add_nvp (res, "confname", "cubrid");
  nv_add_nvp (res, "open", "conflist");

  while (fgets (strbuf, sizeof (strbuf), infile) != NULL)
    {
      nv_add_nvp (res, "confdata", strbuf);
    }
  nv_add_nvp (res, "close", "conflist");
  fclose (infile);

  return ERR_NO_ERROR;
}

/******************************************************************************
	TASK DBMT USER
******************************************************************************/

int
tsCreateDBMTUser (nvplist * req, nvplist * res, char *_dbmt_error)
{
  int num_dbinfo = 0, num_dbmt_user;
  char *dbmt_id, *passwd_p;
  int i, retval;
  char *z_name, *z_value;
  char *dbname, *dbid;
  char dbpasswd[PASSWD_ENC_LENGTH], dbmt_passwd[PASSWD_ENC_LENGTH];
  char *casauth, *dbcreate;
  T_DBMT_USER dbmt_user;
  T_DBMT_USER_DBINFO *dbinfo = NULL;

  memset (&dbmt_user, 0, sizeof (T_DBMT_USER));

  if ((dbmt_id = nv_get_val (req, "targetid")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "target id");
      return ERR_PARAM_MISSING;
    }
  if ((passwd_p = nv_get_val (req, "password")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "password");
      return ERR_PARAM_MISSING;
    }
  uEncrypt (PASSWD_LENGTH, passwd_p, dbmt_passwd);

  if ((retval = dbmt_user_read (&dbmt_user, _dbmt_error)) != ERR_NO_ERROR)
    return retval;

  num_dbmt_user = dbmt_user.num_dbmt_user;
  for (i = 0; i < num_dbmt_user; i++)
    {
      if (strcmp (dbmt_user.user_info[i].user_name, dbmt_id) == 0)
	{
	  dbmt_user_free (&dbmt_user);
	  sprintf (_dbmt_error, "%s", dbmt_id);
	  return ERR_DBMTUSER_EXIST;
	}
    }

  for (i = 0; i < req->nvplist_leng; ++i)
    {
      dbname = dbid = passwd_p = NULL;

      nv_lookup (req, i, &z_name, &z_value);
      if (uStringEqual (z_name, "open"))
	{
	  nv_lookup (req, ++i, &z_name, &z_value);
	  while (!uStringEqual (z_name, "close"))
	    {
	      if (uStringEqual (z_name, "dbname"))
		dbname = z_value;
	      else if (uStringEqual (z_name, "dbid"))
		dbid = z_value;
	      else if (uStringEqual (z_name, "dbpassword"))
		passwd_p = z_value;
	      else
		{
		  dbmt_user_free (&dbmt_user);
		  if (dbinfo)
		    free (dbinfo);
		  return ERR_REQUEST_FORMAT;
		}
	      nv_lookup (req, ++i, &z_name, &z_value);
	      if (i >= req->nvplist_leng)
		break;
	    }
	}
      if (dbname == NULL || dbid == NULL)
	continue;
      uEncrypt (PASSWD_LENGTH, passwd_p, dbpasswd);
      num_dbinfo++;
      MALLOC_USER_DBINFO (dbinfo, num_dbinfo);
      if (dbinfo == NULL)
	{
	  dbmt_user_free (&dbmt_user);
	  return ERR_MEM_ALLOC;
	}
      dbmt_user_set_dbinfo (&(dbinfo[num_dbinfo - 1]), dbname, "admin", dbid,
			    dbpasswd);
    }

  if ((casauth = nv_get_val (req, "casauth")) == NULL)
    casauth = "";
  num_dbinfo++;
  MALLOC_USER_DBINFO (dbinfo, num_dbinfo);
  if (dbinfo == NULL)
    {
      dbmt_user_free (&dbmt_user);
      return ERR_MEM_ALLOC;
    }
  dbmt_user_set_dbinfo (&(dbinfo[num_dbinfo - 1]), "unicas", casauth, "", "");

  if ((dbcreate = nv_get_val (req, "dbcreate")) == NULL)
    dbcreate = "";
  num_dbinfo++;
  MALLOC_USER_DBINFO (dbinfo, num_dbinfo);
  if (dbinfo == NULL)
    {
      dbmt_user_free (&dbmt_user);
      return ERR_MEM_ALLOC;
    }
  dbmt_user_set_dbinfo (&(dbinfo[num_dbinfo - 1]), "dbcreate", dbcreate, "",
			"");

  num_dbmt_user++;
  MALLOC_USER_INFO (dbmt_user.user_info, num_dbmt_user);
  if (dbmt_user.user_info == NULL)
    {
      dbmt_user_free (&dbmt_user);
      if (dbinfo)
	free (dbinfo);
      return ERR_MEM_ALLOC;
    }
  dbmt_user_set_userinfo (&(dbmt_user.user_info[num_dbmt_user - 1]), dbmt_id,
			  dbmt_passwd, num_dbinfo, dbinfo);
  dbmt_user.num_dbmt_user = num_dbmt_user;

  retval = dbmt_user_write_cubrid_pass (&dbmt_user, _dbmt_error);
  if (retval != ERR_NO_ERROR)
    {
      dbmt_user_free (&dbmt_user);
      return retval;
    }
  dbmt_user_write_pass (&dbmt_user, _dbmt_error);

  /* add dblist */
  retval = _tsAppendDBList (res, 0);
  if (retval != ERR_NO_ERROR)
    {
      ut_error_log (req, "error while adding database lists to response");
      return retval;
    }

  _tsAppendDBMTUserList (res, &dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);

  return ERR_NO_ERROR;
}

int
tsDeleteDBMTUser (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  char *dbmt_id;
  int i, retval, usr_index;
  char strbuf[1024];

  if ((dbmt_id = nv_get_val (req, "targetid")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "target id");
      return ERR_PARAM_MISSING;
    }

  if ((retval = dbmt_user_read (&dbmt_user, _dbmt_error)) != ERR_NO_ERROR)
    return retval;

  usr_index = -1;
  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      if (strcmp (dbmt_user.user_info[i].user_name, dbmt_id) == 0)
	{
	  dbmt_user.user_info[i].user_name[0] = '\0';
	  usr_index = i;
	  break;
	}
    }
  if (usr_index < 0)
    {
      strcpy (_dbmt_error,
	      conf_get_dbmt_file2 (FID_DBMT_CUBRID_PASS, strbuf));
      dbmt_user_free (&dbmt_user);
      return ERR_FILE_INTEGRITY;
    }

  retval = dbmt_user_write_cubrid_pass (&dbmt_user, _dbmt_error);
  if (retval != ERR_NO_ERROR)
    {
      dbmt_user_free (&dbmt_user);
      return retval;
    }
  dbmt_user_write_pass (&dbmt_user, _dbmt_error);

  /* add dblist */
  retval = _tsAppendDBList (res, 0);
  if (retval != ERR_NO_ERROR)
    {
      return retval;
    }

  _tsAppendDBMTUserList (res, &dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);

  return ERR_NO_ERROR;
}

int
tsUpdateDBMTUser (nvplist * req, nvplist * res, char *_dbmt_error)
{
  int i, j, usr_index, retval;
  int cas_idx, dbcreate_idx;
  char *dbmt_id;
  char strbuf[1024];
  T_DBMT_USER dbmt_user;
  T_DBMT_USER_DBINFO *usr_dbinfo = NULL;
  int num_dbinfo = 0;
  char *z_name, *z_value;
  char *dbname, *dbid, *dbpassword, *casauth, *dbcreate;
  char passwd_hexa[PASSWD_ENC_LENGTH];

  memset (&dbmt_user, 0, sizeof (T_DBMT_USER));

  if ((dbmt_id = nv_get_val (req, "targetid")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "target id");
      return ERR_PARAM_MISSING;
    }

  for (i = 0; i < req->nvplist_leng; ++i)
    {
      dbname = dbid = dbpassword = NULL;

      nv_lookup (req, i, &z_name, &z_value);
      if (uStringEqual (z_name, "open"))
	{
	  nv_lookup (req, ++i, &z_name, &z_value);
	  while (!uStringEqual (z_name, "close"))
	    {
	      if (uStringEqual (z_name, "dbname"))
		dbname = z_value;
	      else if (uStringEqual (z_name, "dbid"))
		dbid = z_value;
	      else if (uStringEqual (z_name, "dbpassword"))
		dbpassword = z_value;
	      else
		return ERR_REQUEST_FORMAT;
	      nv_lookup (req, ++i, &z_name, &z_value);
	      if (i >= req->nvplist_leng)
		break;
	    }
	}
      if (dbname == NULL || dbid == NULL)
	continue;
      uEncrypt (PASSWD_LENGTH, dbpassword, passwd_hexa);
      num_dbinfo++;
      MALLOC_USER_DBINFO (usr_dbinfo, num_dbinfo);
      if (usr_dbinfo == NULL)
	return ERR_MEM_ALLOC;
      dbmt_user_set_dbinfo (&(usr_dbinfo[num_dbinfo - 1]), dbname, "admin",
			    dbid, passwd_hexa);
    }

  if ((casauth = nv_get_val (req, "casauth")) != NULL)
    {
      cas_idx = num_dbinfo;
      num_dbinfo++;
    }

  if ((dbcreate = nv_get_val (req, "dbcreate")) != NULL)
    {
      dbcreate_idx = num_dbinfo;
      num_dbinfo++;
    }

  MALLOC_USER_DBINFO (usr_dbinfo, num_dbinfo);

  if (usr_dbinfo == NULL)
    return ERR_MEM_ALLOC;

  if (casauth != NULL)
    dbmt_user_set_dbinfo (&(usr_dbinfo[cas_idx]), "unicas", casauth, "", "");
  if (dbcreate != NULL)
    dbmt_user_set_dbinfo (&(usr_dbinfo[dbcreate_idx]),
			  "dbcreate", dbcreate, "", "");

  if ((retval = dbmt_user_read (&dbmt_user, _dbmt_error)) != ERR_NO_ERROR)
    {
      return retval;
    }

  usr_index = -1;
  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      if (strcmp (dbmt_user.user_info[i].user_name, dbmt_id) == 0)
	{
	  usr_index = i;
	  break;
	}
    }
  if (usr_index < 0)
    {
      strcpy (_dbmt_error,
	      conf_get_dbmt_file2 (FID_DBMT_CUBRID_PASS, strbuf));
      dbmt_user_free (&dbmt_user);
      return ERR_FILE_INTEGRITY;
    }

  if (dbmt_user.user_info[usr_index].dbinfo == NULL)
    {
      dbmt_user.user_info[usr_index].num_dbinfo = num_dbinfo;
      dbmt_user.user_info[usr_index].dbinfo = usr_dbinfo;
      usr_dbinfo = NULL;
    }
  else
    {
      T_DBMT_USER_INFO *current_user_info =
	(T_DBMT_USER_INFO *) & (dbmt_user.user_info[usr_index]);

      for (j = 0; j < num_dbinfo; j++)
	{
	  int find_idx = -1;
	  for (i = 0; i < current_user_info->num_dbinfo; i++)
	    {
	      if (strcmp (current_user_info->dbinfo[i].dbname,
			  usr_dbinfo[j].dbname) == 0)
		{
		  find_idx = i;
		  break;
		}
	    }
	  if (find_idx == -1)
	    {
	      current_user_info->num_dbinfo++;
	      MALLOC_USER_DBINFO (current_user_info->dbinfo,
				  current_user_info->num_dbinfo);
	      find_idx = current_user_info->num_dbinfo - 1;
	    }
	  dbmt_user_set_dbinfo (&(current_user_info->dbinfo[find_idx]),
				usr_dbinfo[j].dbname, usr_dbinfo[j].auth,
				usr_dbinfo[j].uid, usr_dbinfo[j].passwd);
	}
    }

  retval = dbmt_user_write_cubrid_pass (&dbmt_user, _dbmt_error);
  if (retval != ERR_NO_ERROR)
    {
      dbmt_user_free (&dbmt_user);
      if (usr_dbinfo)
	free (usr_dbinfo);
      return retval;
    }

  /* add dblist */
  retval = _tsAppendDBList (res, 0);
  if (retval != ERR_NO_ERROR)
    {
      return retval;
    }

  _tsAppendDBMTUserList (res, &dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);
  if (usr_dbinfo)
    free (usr_dbinfo);

  return ERR_NO_ERROR;
}

int
tsChangeDBMTUserPasswd (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  int i, retval, usr_index;
  char *dbmt_id, *new_passwd;
  char strbuf[1024];

  if ((dbmt_id = nv_get_val (req, "targetid")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "target id");
      return ERR_PARAM_MISSING;
    }
  if ((new_passwd = nv_get_val (req, "newpassword")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "new password");
      return ERR_PARAM_MISSING;
    }

  if ((retval = dbmt_user_read (&dbmt_user, _dbmt_error)) != ERR_NO_ERROR)
    return retval;

  usr_index = -1;
  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      if (strcmp (dbmt_user.user_info[i].user_name, dbmt_id) == 0)
	{
	  if (new_passwd == NULL)
	    {
	      dbmt_user.user_info[i].user_passwd[0] = '\0';
	    }
	  else
	    {
	      char hexacoded[PASSWD_ENC_LENGTH];

	      uEncrypt (PASSWD_LENGTH, new_passwd, hexacoded);
	      strcpy (dbmt_user.user_info[i].user_passwd, hexacoded);
	    }
	  usr_index = i;
	  break;
	}
    }
  if (usr_index < 0)
    {
      strcpy (_dbmt_error,
	      conf_get_dbmt_file2 (FID_DBMT_CUBRID_PASS, strbuf));
      dbmt_user_free (&dbmt_user);
      return ERR_FILE_INTEGRITY;
    }

  retval = dbmt_user_write_pass (&dbmt_user, _dbmt_error);
  if (retval != ERR_NO_ERROR)
    {
      dbmt_user_free (&dbmt_user);
      return retval;
    }

  /* add dblist */
  retval = _tsAppendDBList (res, 0);
  if (retval != ERR_NO_ERROR)
    {
      return retval;
    }

  _tsAppendDBMTUserList (res, &dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);

  return ERR_NO_ERROR;
}

int
tsGetDBMTUserInfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  int retval;

  retval = _tsAppendDBList (res, 0);
  if (retval != ERR_NO_ERROR)
    {
      return retval;
    }

  if ((retval = dbmt_user_read (&dbmt_user, _dbmt_error)) != ERR_NO_ERROR)
    return retval;

  _tsAppendDBMTUserList (res, &dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);

  return ERR_NO_ERROR;
}

/******************************************************************************
	TASK DB UTILS
******************************************************************************/

int
tsCreateDB (nvplist * req, nvplist * res, char *_dbmt_error)
{
  int retval = ERR_NO_ERROR;
  char *dbname = NULL;
  char *numpage = NULL;
  char *pagesize = NULL;
  char *genvolpath = NULL;
  char *logsize = NULL;
  char *logvolpath = NULL, logvolpath_buf[1024];
  char *overwrite_config_file = NULL;
  char targetdir[1024];
  char extvolfile[512] = "";
  char *cubrid_err_file;
  char outofspace_val[512];
  T_DBMT_USER dbmt_user;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[20];
  int argc = 0;

  int gen_dir_created, log_dir_created, ext_dir_created;

  gen_dir_created = log_dir_created = ext_dir_created = 0;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  numpage = nv_get_val (req, "numpage");
  pagesize = nv_get_val (req, "pagesize");
  logsize = nv_get_val (req, "logsize");
  genvolpath = nv_get_val (req, "genvolpath");
  logvolpath = nv_get_val (req, "logvolpath");
  overwrite_config_file = nv_get_val (req, "overwrite_config_file");

  if (genvolpath == NULL)
    {
      strcpy (_dbmt_error, "volumn path");
      return ERR_PARAM_MISSING;
    }
  if (logvolpath != NULL && logvolpath[0] == '\0')
    logvolpath = NULL;

  /* create directory */
  strcpy (targetdir, genvolpath);
  if (access (genvolpath, F_OK) < 0)
    {
      retval = uCreateDir (genvolpath);
      if (retval != ERR_NO_ERROR)
	return retval;
      else
	gen_dir_created = 1;
    }
  if (logvolpath != NULL && access (logvolpath, F_OK) < 0)
    {
      retval = uCreateDir (logvolpath);
      if (retval != ERR_NO_ERROR)
	return retval;
      else
	log_dir_created = 1;
    }

  if (access (genvolpath, W_OK) < 0)
    {
      sprintf (_dbmt_error, "%s: %s\n", genvolpath, strerror (errno));
      return ERR_WITH_MSG;
    }
  if (logvolpath != NULL && access (logvolpath, W_OK) < 0)
    {
      sprintf (_dbmt_error, "%s: %s\n", genvolpath, strerror (errno));
      return ERR_WITH_MSG;
    }

  /* copy config file to the directory and update config file */
  if ((overwrite_config_file == NULL)	/* for backword compatibility */
      || (strcasecmp (overwrite_config_file, "NO") != 0))
    {
      char strbuf[1024];
      FILE *infile = NULL;
      FILE *outfile = NULL;
      char dstrbuf[512];

      sprintf (dstrbuf, "%s/conf/%s", sco.szCubrid, CUBRID_CUBRID_CONF);
      infile = fopen (dstrbuf, "r");
      if (infile == NULL)
	{
	  strcpy (_dbmt_error, dstrbuf);
	  return ERR_FILE_OPEN_FAIL;
	}

      sprintf (dstrbuf, "%s/%s", targetdir, CUBRID_CUBRID_CONF);
      outfile = fopen (dstrbuf, "w");
      if (outfile == NULL)
	{
	  fclose (infile);
	  strcpy (_dbmt_error, dstrbuf);
	  return ERR_FILE_OPEN_FAIL;
	}

      while (fgets (strbuf, sizeof (strbuf), infile))
	{
	  char *p;
	  p = "data_buffer_pages";
	  if (!strncmp (strbuf, p, strlen (p)) && nv_get_val (req, p))
	    {
	      fprintf (outfile, "%s=%s\n", p, nv_get_val (req, p));
	      continue;
	    }
	  p = "media_failure_support";
	  if (!strncmp (strbuf, p, strlen (p)) && nv_get_val (req, p))
	    {
	      fprintf (outfile, "%s=%s\n", p, nv_get_val (req, p));
	      continue;
	    }
	  p = "max_clients";
	  if (!strncmp (strbuf, p, strlen (p)) && nv_get_val (req, p))
	    {
	      fprintf (outfile, "%s=%s\n", p, nv_get_val (req, p));
	      continue;
	    }

	  fputs (strbuf, outfile);
	}
      fclose (infile);
      fclose (outfile);
    }

  /* remove warning out of space message.
   * judge creation failed if created page size is smaller then
   * 343 page, write message with volumn expend to error file.
   */
  if (0)
    {
      char strbuf[1024];
      FILE *infile = NULL;
      FILE *outfile = NULL;
      char oldfilename[512];
      char newfilename[512];
      memset (oldfilename, '\0', sizeof (oldfilename));
      memset (newfilename, '\0', sizeof (newfilename));

      sprintf (oldfilename, "%s/%s", targetdir, CUBRID_CUBRID_CONF);
      infile = fopen (oldfilename, "r");
      if (infile == NULL)
	{
	  strcpy (_dbmt_error, oldfilename);
	  return ERR_FILE_OPEN_FAIL;
	}

      sprintf (newfilename, "%s/tempcubrid.conf", targetdir);
      outfile = fopen (newfilename, "w");
      if (outfile == NULL)
	{
	  fclose (infile);
	  strcpy (_dbmt_error, newfilename);
	  return ERR_FILE_OPEN_FAIL;
	}

      while (fgets (strbuf, sizeof (strbuf), infile))
	{
	  char *p;
	  p = "warn_outofspace_factor";
	  if (!strncmp (strbuf, p, strlen (p)))
	    {
	      memset (outofspace_val, '\0', sizeof (outofspace_val));
	      strncpy (outofspace_val, strbuf, strlen (strbuf));

	      fprintf (outfile, "warn_outofspace_factor=0.0");
	      continue;
	    }

	  fputs (strbuf, outfile);
	}
      fclose (infile);
      fclose (outfile);

      unlink (oldfilename);
      rename (newfilename, oldfilename);
    }

  /* construct spec file */
  if (1)
    {
      int pos, len, i;
      char *tn, *tv;
      FILE *outfile;
      char buf[1024], *val[3];
      char val2_buf[1024];

      sprintf (extvolfile, "%s/extvol.spec", targetdir);
      outfile = fopen (extvolfile, "w");

      nv_locate (req, "exvol", &pos, &len);
      for (i = pos; i < len + pos; ++i)
	{
	  nv_lookup (req, i, &tn, &tv);
	  if (tv == NULL)
	    continue;
	  strcpy (buf, tv);
	  if (string_tokenize2 (buf, val, 3, ';') < 0)
	    {
	      continue;
	    }

#ifdef WIN32
	  val[2] = nt_style_path (val[2], val2_buf);
#endif
	  fprintf (outfile, "NAME %s PATH %s PURPOSE %s NPAGES %s\n\n",
		   tn, val[2], val[0], val[1]);
	  /* create directory, if needed */
	  if (access (val[2], F_OK) < 0)
	    {
	      retval = uCreateDir (val[2]);
	      if (retval != ERR_NO_ERROR)
		return retval;
	      else
		ext_dir_created = 1;
	    }
	}
      fclose (outfile);
    }

  /* construct command */
  cubrid_cmd_name (cmd_name);
#ifdef WIN32
  nt_style_path (targetdir, targetdir);
#endif
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_CREATEDB;
  argv[argc++] = "--" CREATE_FILE_PATH_L;
  argv[argc++] = targetdir;

  if (numpage)
    {
      argv[argc++] = "--" CREATE_PAGES_L;
      argv[argc++] = numpage;
    }
  if (pagesize)
    {
      argv[argc++] = "--" CREATE_PAGE_SIZE_L;
      argv[argc++] = pagesize;
    }
  if (logsize)
    {
      argv[argc++] = "--" CREATE_LOG_PAGE_COUNT_L;
      argv[argc++] = logsize;
    }
  if (logvolpath)
    {
#ifdef WIN32
      logvolpath = nt_style_path (logvolpath, logvolpath_buf);
      /*
         remove_end_of_dir_ch(logvolpath);
       */
#endif
      argv[argc++] = "--" CREATE_LOG_PATH_L;
      argv[argc++] = logvolpath;
    }
  if (extvolfile[0] != '\0')
    {
      argv[argc++] = "--" CREATE_MORE_VOLUME_FILE_L;
      argv[argc++] = extvolfile;
    }

  argv[argc++] = dbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* createdb */

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    {
      int pos, len, i;
      char *tn, *tv;
      char buf[1024], *val[3];

      if ((access (genvolpath, F_OK) == 0) && (gen_dir_created))
	uRemoveDir (genvolpath, REMOVE_DIR_FORCED);
      if ((logvolpath != NULL) && (access (logvolpath, F_OK) == 0)
	  && (log_dir_created))
	uRemoveDir (logvolpath, REMOVE_DIR_FORCED);

      nv_locate (req, "exvol", &pos, &len);
      for (i = pos; i < len + pos; ++i)
	{
	  nv_lookup (req, i, &tn, &tv);
	  if (tv == NULL)
	    continue;
	  strcpy (buf, tv);
	  if (string_tokenize2 (buf, val, 3, ';') < 0)
	    {
	      continue;
	    }
	  if ((access (val[2], F_OK) == 0) && (ext_dir_created))
	    uRemoveDir (val[2], REMOVE_DIR_FORCED);	/* ext vol path */
	}
      return ERR_WITH_MSG;
    }

  if (retval < 0)
    {
      int pos, len, i;
      char *tn, *tv;
      char buf[1024], *val[3];

      if (access (genvolpath, F_OK) == 0)
	uRemoveDir (genvolpath, REMOVE_DIR_FORCED);
      if (logvolpath != NULL && access (logvolpath, F_OK) == 0)
	uRemoveDir (logvolpath, REMOVE_DIR_FORCED);

      nv_locate (req, "exvol", &pos, &len);
      for (i = pos; i < len + pos; ++i)
	{
	  nv_lookup (req, i, &tn, &tv);
	  if (tv == NULL)
	    continue;
	  strcpy (buf, tv);
	  if (string_tokenize2 (buf, val, 3, ';') < 0)
	    {
	      continue;
	    }
	  if (access (val[2], F_OK) == 0)
	    uRemoveDir (val[2], REMOVE_DIR_FORCED);	/* ext vol path */
	}

      sprintf (_dbmt_error, "%s", argv[0]);
      return ERR_SYSTEM_CALL;
    }

  if (dbmt_user_read (&dbmt_user, _dbmt_error) == ERR_NO_ERROR)
    {
      int i;
      T_DBMT_USER_DBINFO tmp_dbinfo;

      memset (&tmp_dbinfo, 0, sizeof (tmp_dbinfo));
      dbmt_user_set_dbinfo (&tmp_dbinfo, dbname, "admin", "dba", "");

      dbmt_user_db_delete (&dbmt_user, dbname);
      for (i = 0; i < dbmt_user.num_dbmt_user; i++)
	{
	  if (strcmp (dbmt_user.user_info[i].user_name, "admin") == 0)
	    {
	      if (dbmt_user_add_dbinfo
		  (&(dbmt_user.user_info[i]), &tmp_dbinfo) == ERR_NO_ERROR)
		{
		  dbmt_user_write_cubrid_pass (&dbmt_user, _dbmt_error);
		}
	      break;
	    }
	}
      dbmt_user_free (&dbmt_user);
    }

  /* restore warn out of space value */
  if (0)
    {
      char strbuf[1024];
      FILE *infile = NULL;
      FILE *outfile = NULL;
      char oldfilename[512];
      char newfilename[512];
      memset (oldfilename, '\0', sizeof (oldfilename));
      memset (newfilename, '\0', sizeof (newfilename));

      sprintf (oldfilename, "%s/%s", targetdir, CUBRID_CUBRID_CONF);
      infile = fopen (oldfilename, "r");
      if (infile == NULL)
	{
	  strcpy (_dbmt_error, oldfilename);
	  return ERR_FILE_OPEN_FAIL;
	}

      sprintf (newfilename, "%s/tempcubrid.conf", targetdir);
      outfile = fopen (newfilename, "w");
      if (outfile == NULL)
	{
	  fclose (infile);
	  strcpy (_dbmt_error, newfilename);
	  return ERR_FILE_OPEN_FAIL;
	}

      while (fgets (strbuf, sizeof (strbuf), infile))
	{
	  char *p;
	  p = "warn_outofspace_factor";
	  if (!strncmp (strbuf, p, strlen (p)))
	    {
	      fprintf (outfile, "%s", outofspace_val);
	      continue;
	    }

	  fputs (strbuf, outfile);
	}
      fclose (infile);
      fclose (outfile);

      unlink (oldfilename);
      rename (newfilename, oldfilename);
    }

  if ((overwrite_config_file == NULL)	/* for backword compatibility */
      || (strcasecmp (overwrite_config_file, "NO") != 0))
    {
      char strbuf[1024];

      sprintf (strbuf, "%s/%s", targetdir, CUBRID_CUBRID_CONF);
      unlink (strbuf);
    }

  if (extvolfile[0] != '\0')
    unlink (extvolfile);

  return ERR_NO_ERROR;
}

int
tsDeleteDB (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  int retval = ERR_NO_ERROR;
  char *dbname = NULL, *delbackup;
  char *cubrid_err_file;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[6];
  int argc = 0;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  delbackup = nv_get_val (req, "delbackup");

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_DELETEDB;
  if (uStringEqual (delbackup, "y"))
    argv[argc++] = "--" DELETE_DELETE_BACKUP_L;
  argv[argc++] = dbname;
  argv[argc++] = NULL;
  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* deletedb */

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;
  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  auto_conf_addvol_delete (FID_AUTO_ADDVOLDB_CONF, dbname);
  auto_conf_backup_delete (FID_AUTO_BACKUPDB_CONF, dbname);
  auto_conf_history_delete (FID_AUTO_HISTORY_CONF, dbname);
  auto_conf_execquery_delete (FID_AUTO_EXECQUERY_CONF, dbname);

  if (dbmt_user_read (&dbmt_user, _dbmt_error) == ERR_NO_ERROR)
    {
      dbmt_user_db_delete (&dbmt_user, dbname);
      dbmt_user_write_cubrid_pass (&dbmt_user, _dbmt_error);
      dbmt_user_free (&dbmt_user);
    }
  return ERR_NO_ERROR;
}

int
tsRenameDB (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  int retval;
  char *dbname = NULL;
  char *newdbname = NULL;
  char *exvolpath, *advanced, *forcedel;
  char tmpfile[256];
  char *cubrid_err_file;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[10];
  int argc = 0;
  T_DB_SERVICE_MODE db_mode;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  else if (db_mode == DB_SERVICE_MODE_CS)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_DB_ACTIVE;
    }

  if ((newdbname = nv_get_val (req, "rename")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "new database name");
      return ERR_PARAM_MISSING;
    }

  exvolpath = nv_get_val (req, "exvolpath");
  advanced = nv_get_val (req, "advanced");
  forcedel = nv_get_val (req, "forcedel");

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_RENAMEDB;

  if (uStringEqual (advanced, "on"))
    {
      FILE *outfile;
      int i, flag = 0, line = 0;
      char *n, *v, n_buf[1024], v_buf[1024];
      char *p;

      sprintf (tmpfile, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir, TS_RENAMEDB,
	       (int) getpid ());
      if ((outfile = fopen (tmpfile, "w")) == NULL)
	return ERR_TMPFILE_OPEN_FAIL;
      for (i = 0; i < req->nvplist_leng; i++)
	{
	  nv_lookup (req, i, &n, &v);
	  if (!strcmp (n, "open") && !strcmp (v, "volume"))
	    flag = 1;
	  else if (!strcmp (n, "close") && !strcmp (v, "volume"))
	    flag = 0;
	  else if (flag == 1)
	    {
#ifdef WIN32
	      replace_colon (n);
	      replace_colon (v);
#endif
	      p = strrchr (v, '/');
	      if (p)
		*p = '\0';
	      if (uCreateDir (v) != ERR_NO_ERROR)
		{
		  fclose (outfile);
		  strcpy (_dbmt_error, v);
		  return ERR_DIR_CREATE_FAIL;
		}
	      if (p)
		*p = '/';
#ifdef WIN32
	      n = nt_style_path (n, n_buf);
	      v = nt_style_path (v, v_buf);
#endif
	      fprintf (outfile, "%d %s %s\n", line++, n, v);

	    }			/* close "else if (flag == 1)" */
	}			/* close "for" loop */
      fclose (outfile);
      argv[argc++] = "--" RENAME_CONTROL_FILE_L;
      argv[argc++] = tmpfile;
    }				/* close "if (adv_flag != NULL)" */
  else if (exvolpath != NULL && !uStringEqual (exvolpath, "none"))
    {
      if (uCreateDir (exvolpath) != ERR_NO_ERROR)
	{
	  strcpy (_dbmt_error, exvolpath);
	  return ERR_DIR_CREATE_FAIL;
	}
      argv[argc++] = "--" RENAME_EXTENTED_VOLUME_PATH_L;
      argv[argc++] = exvolpath;
    }

  if (uStringEqual (forcedel, "y"))
    argv[argc++] = "--" RENAME_DELETE_BACKUP_L;

  argv[argc++] = dbname;
  argv[argc++] = newdbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* renamedb */

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;
  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  auto_conf_addvol_rename (FID_AUTO_ADDVOLDB_CONF, dbname, newdbname);
  auto_conf_backup_rename (FID_AUTO_BACKUPDB_CONF, dbname, newdbname);
  auto_conf_history_rename (FID_AUTO_HISTORY_CONF, dbname, newdbname);
  auto_conf_execquery_rename (FID_AUTO_EXECQUERY_CONF, dbname, newdbname);

  if (dbmt_user_read (&dbmt_user, _dbmt_error) == ERR_NO_ERROR)
    {
      int i, j;
      for (i = 0; i < dbmt_user.num_dbmt_user; i++)
	{
	  for (j = 0; j < dbmt_user.user_info[i].num_dbinfo; j++)
	    {
	      if (strcmp (dbmt_user.user_info[i].dbinfo[j].dbname, dbname) ==
		  0)
		{
		  strcpy (dbmt_user.user_info[i].dbinfo[j].dbname, newdbname);
		}
	    }
	}
      dbmt_user_write_cubrid_pass (&dbmt_user, _dbmt_error);
      dbmt_user_free (&dbmt_user);
    }

  return ERR_NO_ERROR;
}

int
tsStartDB (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  char err_buf[ERR_MSG_SIZE];
  T_DB_SERVICE_MODE db_mode;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  if (db_mode == DB_SERVICE_MODE_CS)
    {
      return ERR_NO_ERROR;
    }

  if (cmd_start_server (dbname, err_buf, sizeof (err_buf)) < 0)
    {
      DBMT_ERR_MSG_SET (_dbmt_error, err_buf);
      return ERR_WITH_MSG;
    }

  /* recount active db num and write to file */
  uWriteDBnfo ();

  return ERR_NO_ERROR;
}

int
tsStopDB (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  if (cmd_stop_server (dbname, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    {
      return ERR_WITH_MSG;
    }

  /* recount active db num and write to file */
  uWriteDBnfo ();

  return ERR_NO_ERROR;
}

int
tsDbspaceInfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname = NULL;
  int retval = ERR_NO_ERROR;
  T_CUBRID_MODE cubrid_mode;
  T_SPACEDB_RESULT *cmd_res;
  T_DB_SERVICE_MODE db_mode;

  /* get dbname */
  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  nv_add_nvp (res, "dbname", dbname);
  nv_add_nvp (res, "pagesize", "-1");

  cubrid_mode =
    (db_mode == DB_SERVICE_MODE_NONE) ? CUBRID_MODE_SA : CUBRID_MODE_CS;
  cmd_res = cmd_spacedb (dbname, cubrid_mode);

  if (cmd_res == NULL)
    {
      sprintf (_dbmt_error, "spacedb %s", dbname);
      retval = ERR_SYSTEM_CALL;
    }
  else if (cmd_res->err_msg[0])
    {
      strcpy (_dbmt_error, cmd_res->err_msg);
      retval = ERR_WITH_MSG;
    }
  else
    {
      retval = _tsParseSpacedb (req, res, dbname, _dbmt_error, cmd_res);
    }
  cmd_spacedb_result_free (cmd_res);

  return retval;
}

int
tsRunAddvoldb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  char *numpage;
  char *volpu;
  char *volpath, volpath_buf[1024];
  char *volname;
  char *size_need_mb;
  char db_dir[512];
  T_DB_SERVICE_MODE db_mode;
  char *err_file;
  int ret;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[15];
  int argc = 0;
  int free_space_mb;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if ((numpage = nv_get_val (req, "numberofpages")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "number of pages");
      return ERR_PARAM_MISSING;
    }
  if ((volpu = nv_get_val (req, "purpose")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "purpose");
      return ERR_PARAM_MISSING;
    }

  volpath = nv_get_val (req, "path");
  volname = nv_get_val (req, "volname");
  size_need_mb = nv_get_val (req, "size_need_mb");

  if (uRetrieveDBDirectory (dbname, db_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  /* check permission of the directory */
  if (access (db_dir, W_OK | X_OK | R_OK) < 0)
    {
      sprintf (_dbmt_error, "%s", db_dir);
      return ERR_PERMISSION;
    }

  if (volpath == NULL)
    volpath = db_dir;

  if (access (volpath, F_OK) < 0)
    {
      if (uCreateDir (volpath) != ERR_NO_ERROR)
	{
	  sprintf (_dbmt_error, "%s", volpath);
	  return ERR_DIR_CREATE_FAIL;
	}
    }

  free_space_mb = ut_disk_free_space (volpath);
  if (size_need_mb && (free_space_mb < atoi (size_need_mb)))
    {
      sprintf (_dbmt_error, "Not enough dist free space");
      return ERR_WITH_MSG;
    }

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_ADDVOLDB;

  if (db_mode == DB_SERVICE_MODE_NONE)
    argv[argc++] = "--" ADDVOL_SA_MODE_L;

#ifdef WIN32
  volpath = nt_style_path (volpath, volpath_buf);
#endif
  argv[argc++] = "--" ADDVOL_FILE_PATH_L;
  argv[argc++] = volpath;

  if (volname)
    {
      argv[argc++] = "--" ADDVOL_VOLUME_NAME_L;
      argv[argc++] = volname;
    }

  argv[argc++] = "--" ADDVOL_PURPOSE_L;
  argv[argc++] = volpu;
  argv[argc++] = dbname;
  argv[argc++] = numpage;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (err_file);

  ret = run_child (argv, 1, NULL, NULL, err_file, NULL);	/* addvoldb */
  if (read_error_file (err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;
  if (ret < 0)
    {
      sprintf (_dbmt_error, "%s", argv[0]);
      return ERR_SYSTEM_CALL;
    }

  nv_add_nvp (res, "dbname", dbname);
  nv_add_nvp (res, "purpose", volpu);

  return ERR_NO_ERROR;
}

int
ts_copydb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *srcdbname, *destdbname, *logpath, *destdbpath, *exvolpath;
  char move_flag, overwrite_flag, adv_flag;
  char tmpfile[256];
  char src_conf_file[256], dest_conf_file[256];
  int i, retval;
  char *cubrid_err_file;
  T_DBMT_USER dbmt_user;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  T_DB_SERVICE_MODE db_mode;
  char *argv[15];
  int argc = 0;


  if ((srcdbname = nv_get_val (req, "srcdbname")) == NULL)
    {
      strcpy (_dbmt_error, "source database name");
      return ERR_PARAM_MISSING;
    }
  if ((destdbname = nv_get_val (req, "destdbname")) == NULL)
    {
      strcpy (_dbmt_error, "destination database name");
      return ERR_PARAM_MISSING;
    }


  adv_flag = uStringEqual (nv_get_val (req, "advanced"), "on") ? 1 : 0;
  overwrite_flag = uStringEqual (nv_get_val (req, "overwrite"), "y") ? 1 : 0;
  move_flag = uStringEqual (nv_get_val (req, "move"), "y") ? 1 : 0;
  if ((logpath = nv_get_val (req, "logpath")) == NULL)
    {
      strcpy (_dbmt_error, "log path");
      return ERR_PARAM_MISSING;
    }
  if ((destdbpath = nv_get_val (req, "destdbpath")) == NULL && adv_flag == 0)
    {
      strcpy (_dbmt_error, "database directory path");
      return ERR_PARAM_MISSING;
    }
  if ((exvolpath = nv_get_val (req, "exvolpath")) == NULL && adv_flag == 0)
    {
      strcpy (_dbmt_error, "extended volume path");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (srcdbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", srcdbname);
      return ERR_STANDALONE_MODE;
    }
  else if (db_mode == DB_SERVICE_MODE_CS)
    {
      sprintf (_dbmt_error, "%s", srcdbname);
      return ERR_DB_ACTIVE;
    }


  /* create command */
  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_COPYDB;
  argv[argc++] = "--" COPY_LOG_PATH_L;
  argv[argc++] = logpath;

  if (adv_flag)
    {
      FILE *outfile;
      int flag = 0, line = 0;
      char *n, *v, n_buf[1024], v_buf[1024];
      char *p;

      sprintf (tmpfile, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir, TS_COPYDB,
	       (int) getpid ());
      if ((outfile = fopen (tmpfile, "w")) == NULL)
	return ERR_TMPFILE_OPEN_FAIL;
      for (i = 0; i < req->nvplist_leng; i++)
	{
	  nv_lookup (req, i, &n, &v);
	  if (!strcmp (n, "open") && !strcmp (v, "volume"))
	    flag = 1;
	  else if (!strcmp (n, "close") && !strcmp (v, "volume"))
	    flag = 0;
	  else if (flag == 1)
	    {
#ifdef WIN32
	      replace_colon (n);
	      replace_colon (v);
#endif
	      p = strrchr (v, '/');
	      if (p)
		*p = '\0';
	      if (uCreateDir (v) != ERR_NO_ERROR)
		{
		  fclose (outfile);
		  strcpy (_dbmt_error, v);
		  return ERR_DIR_CREATE_FAIL;
		}
	      if (p)
		*p = '/';
#ifdef WIN32
	      n = nt_style_path (n, n_buf);
	      v = nt_style_path (v, v_buf);
#endif
	      fprintf (outfile, "%d %s %s\n", line++, n, v);
	    }
	}
      fclose (outfile);
      argv[argc++] = "--" COPY_CONTROL_FILE_L;
      argv[argc++] = tmpfile;
    }
  else
    {				/* adv_flag == 0 */
      if (uCreateDir (destdbpath) != ERR_NO_ERROR)
	{
	  strcpy (_dbmt_error, destdbpath);
	  return ERR_DIR_CREATE_FAIL;
	}
      if (uCreateDir (exvolpath) != ERR_NO_ERROR)
	{
	  strcpy (_dbmt_error, exvolpath);
	  return ERR_DIR_CREATE_FAIL;
	}
      argv[argc++] = "--" COPY_FILE_PATH_L;
      argv[argc++] = destdbpath;
      argv[argc++] = "--" COPY_EXTENTED_VOLUME_PATH_L;
      argv[argc++] = exvolpath;
    }
  if (overwrite_flag)
    argv[argc++] = "--" COPY_REPLACE_L;
  argv[argc++] = srcdbname;
  argv[argc++] = destdbname;
  argv[argc++] = NULL;

  if (uCreateDir (logpath) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, logpath);
      return ERR_DIR_CREATE_FAIL;
    }

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* copydb */
  if (adv_flag)
    unlink (tmpfile);
  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;
  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  /* copy config file */
  if (uRetrieveDBDirectory (srcdbname, src_conf_file) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, srcdbname);
      return ERR_DBDIRNAME_NULL;
    }
  strcat (src_conf_file, "/");
  strcat (src_conf_file, CUBRID_CUBRID_CONF);
  if (uRetrieveDBDirectory (destdbname, dest_conf_file) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, destdbname);
      return ERR_DBDIRNAME_NULL;
    }
  strcat (dest_conf_file, "/");
  strcat (dest_conf_file, CUBRID_CUBRID_CONF);
  /* Doesn't copy if src and desc is same */
  if (strcmp (src_conf_file, dest_conf_file) != 0)
    file_copy (src_conf_file, dest_conf_file);

  /* if move, delete exist database */
  if (move_flag)
    {
      char cmd_name[CUBRID_CMD_NAME_LEN];
      char *argv[5];

      cubrid_cmd_name (cmd_name);
      argv[0] = cmd_name;
      argv[1] = UTIL_OPTION_DELETEDB;
      argv[2] = srcdbname;
      argv[3] = NULL;
      retval = run_child (argv, 1, NULL, NULL, NULL, NULL);	/* deletedb */
      if (retval < 0)
	{
	  strcpy (_dbmt_error, argv[0]);
	  return ERR_SYSTEM_CALL;
	}
    }

  /* cmdb.pass update after delete */
  if (dbmt_user_read (&dbmt_user, _dbmt_error) != ERR_NO_ERROR)
    {
      goto copydb_finale;
    }

  dbmt_user_db_delete (&dbmt_user, destdbname);
  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      int dbinfo_idx;
      T_DBMT_USER_DBINFO tmp_info;

      dbinfo_idx = dbmt_user_search (&(dbmt_user.user_info[i]), srcdbname);
      if (dbinfo_idx < 0)
	continue;
      tmp_info = dbmt_user.user_info[i].dbinfo[dbinfo_idx];
      strcpy (tmp_info.dbname, destdbname);
      if (dbmt_user_add_dbinfo (&(dbmt_user.user_info[i]), &tmp_info) !=
	  ERR_NO_ERROR)
	{
	  dbmt_user_free (&dbmt_user);
	  goto copydb_finale;
	}

    }
  if (move_flag)
    {
      dbmt_user_db_delete (&dbmt_user, srcdbname);
    }
  dbmt_user_write_cubrid_pass (&dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);

copydb_finale:
  return ERR_NO_ERROR;
}

int
ts_optimizedb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *classname;
  T_DB_SERVICE_MODE db_mode;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  classname = nv_get_val (req, "classname");

  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      char cmd_name[CUBRID_CMD_NAME_LEN];
      char *argv[6];
      int argc = 0;

      cubrid_cmd_name (cmd_name);
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
      char sql[256];
      DB_QUERY_RESULT *result;
      DB_QUERY_ERROR query_error;

      if (_op_db_login (res, req, _dbmt_error) < 0)
	return ERR_WITH_MSG;

      if (classname == NULL)
	strcpy (sql, "UPDATE STATISTICS ON ALL CLASSES");
      else
	sprintf (sql, "UPDATE STATISTICS ON \"%s\"", classname);

      if (db_execute (sql, &result, &query_error) < 0)
	{
	  CUBRID_ERR_MSG_SET (_dbmt_error);
	  db_shutdown ();
	  return ERR_WITH_MSG;
	}
      db_query_end (result);

      db_commit_transaction ();
      db_shutdown ();
    }
  return ERR_NO_ERROR;
}

int
ts_checkdb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  T_DB_SERVICE_MODE db_mode;
  char *argv[6];
  int argc = 0;
  char *cubrid_err_file;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_CHECKDB;
  if (db_mode == DB_SERVICE_MODE_NONE)
    argv[argc++] = "--" CHECK_SA_MODE_L;
  else
    argv[argc++] = "--" CHECK_CS_MODE_L;
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  if (run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL) < 0)
    {				/* checkdb */
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts_compactdb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[5];
  T_DB_SERVICE_MODE db_mode;
  char *cubrid_err_file;

  dbname = nv_get_val (req, "dbname");
  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  else if (db_mode == DB_SERVICE_MODE_CS)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_DB_ACTIVE;
    }

  cubrid_cmd_name (cmd_name);
  argv[0] = cmd_name;
  argv[1] = UTIL_OPTION_COMPACTDB;
  argv[2] = dbname;
  argv[3] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  if (run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL) < 0)
    {				/* compactdb */
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts_backupdb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *level, *removelog, *volname, *backupdir, *check;
  char *mt, *zip;
  char backupfilepath[256];
  char inputfilepath[256];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[15];
  int argc = 0;
  FILE *inputfile;
  T_DB_SERVICE_MODE db_mode;
  char *cubrid_err_file;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  level = nv_get_val (req, "level");
  volname = nv_get_val (req, "volname");
  backupdir = nv_get_val (req, "backupdir");
  removelog = nv_get_val (req, "removelog");
  check = nv_get_val (req, "check");
  mt = nv_get_val (req, "mt");
  zip = nv_get_val (req, "zip");

  /* create directory */
  if (access (backupdir, F_OK) < 0)
    {
      if (uCreateDir (backupdir) != ERR_NO_ERROR)
	{
	  strcpy (_dbmt_error, backupdir);
	  return ERR_DIR_CREATE_FAIL;
	}
    }

  sprintf (backupfilepath, "%s/%s", backupdir, volname);

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_BACKUPDB;
  if (db_mode == DB_SERVICE_MODE_NONE)
    argv[argc++] = "--" BACKUP_SA_MODE_L;
  else
    argv[argc++] = "--" BACKUP_CS_MODE_L;
  argv[argc++] = "--" BACKUP_LEVEL_L;
  argv[argc++] = level;
  argv[argc++] = "--" BACKUP_DESTINATION_PATH_L;
  argv[argc++] = backupfilepath;
  if (uStringEqual (removelog, "y"))
    argv[argc++] = "--" BACKUP_REMOVE_ARCHIVE_L;
  if (uStringEqual (check, "n"))
    argv[argc++] = "--" BACKUP_NO_CHECK_L;
  if (mt != NULL)
    {
      argv[argc++] = "--" BACKUP_THREAD_COUNT_L;
      argv[argc++] = mt;
    }
  if (zip != NULL && uStringEqual (zip, "y"))
    argv[argc++] = "--" BACKUP_COMPRESS_L;
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  sprintf (inputfilepath, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir, TS_BACKUPDB,
	   (int) getpid ());
  inputfile = fopen (inputfilepath, "w");
  if (inputfile)
    {
      fprintf (inputfile, "y");
      fclose (inputfile);
    }
  else
    {
      return ERR_FILE_OPEN_FAIL;
    }

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  if (run_child (argv, 1, inputfilepath, NULL, cubrid_err_file, NULL) < 0)
    {				/* backupdb */
      strcpy (_dbmt_error, argv[0]);
      unlink (inputfilepath);
      return ERR_SYSTEM_CALL;
    }

  unlink (inputfilepath);

  if (read_csql_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE)
      < 0)
    {
      return ERR_WITH_MSG;
    }

  return ERR_NO_ERROR;
}

int
ts_unloaddb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *targetdir, *usehash, *hashdir, *target, *s1, *s2,
    *ref, *classonly, *delimit, *estimate, *prefix, *cach, *lofile,
    buf[1024], infofile[256], tmpfile[128], temp[256], n[256], v[256],
    cname[256], p1[64], p2[8], p3[8];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *cubrid_err_file;
  FILE *infile, *outfile;
  int i, flag = 0, no_class = 0, index_exist = 0, trigger_exist = 0;
  struct stat statbuf;
  char *argv[30];
  int argc = 0;
  T_DB_SERVICE_MODE db_mode;

  dbname = nv_get_val (req, "dbname");
  targetdir = nv_get_val (req, "targetdir");
  usehash = nv_get_val (req, "usehash");
  hashdir = nv_get_val (req, "hashdir");
  target = nv_get_val (req, "target");
  ref = nv_get_val (req, "ref");
  classonly = nv_get_val (req, "classonly");
  delimit = nv_get_val (req, "delimit");
  estimate = nv_get_val (req, "estimate");
  prefix = nv_get_val (req, "prefix");
  cach = nv_get_val (req, "cach");
  lofile = nv_get_val (req, "lofile");

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (access (targetdir, F_OK) < 0)
    {
      if (uCreateDir (targetdir) != ERR_NO_ERROR)
	{
	  strcpy (_dbmt_error, targetdir);
	  return ERR_DIR_CREATE_FAIL;
	}
    }

  /* makeup upload class list file */
  sprintf (tmpfile, "%s/DBMT_task_101.%d", sco.dbmt_tmp_dir, (int) getpid ());
  if ((outfile = fopen (tmpfile, "w")) == NULL)
    return ERR_TMPFILE_OPEN_FAIL;
  for (i = 0; i < req->nvplist_leng; i++)
    {
      nv_lookup (req, i, &s1, &s2);
      if (!strcmp (s1, "open"))
	flag = 1;
      else if (!strcmp (s1, "close"))
	flag = 0;
      else if (flag == 1 && !strcmp (s1, "classname"))
	{
	  sprintf (buf, "%s\n", s2);
	  fputs (buf, outfile);
	  no_class++;
	}
    }
  fclose (outfile);

  /* makeup command and execute */
  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_UNLOADDB;
  if (db_mode == DB_SERVICE_MODE_NONE)
    argv[argc++] = "--" UNLOAD_SA_MODE_L;
  else
    argv[argc++] = "--" UNLOAD_CS_MODE_L;
  if (no_class > 0)
    {
      argv[argc++] = "--" UNLOAD_INPUT_CLASS_FILE_L;
      argv[argc++] = tmpfile;
    }
  argv[argc++] = "--" UNLOAD_OUTPUT_PATH_L;
  argv[argc++] = targetdir;
  if (!strcmp (usehash, "yes"))
    {
      argv[argc++] = "--" UNLOAD_HASH_FILE_L;
      argv[argc++] = hashdir;
    }
  if (!strcmp (target, "both"))
    {
      argv[argc++] = "--" UNLOAD_SCHEMA_ONLY_L;
      argv[argc++] = "--" UNLOAD_DATA_ONLY_L;
    }
  else if (!strcmp (target, "schema"))
    argv[argc++] = "--" UNLOAD_SCHEMA_ONLY_L;
  else if (!strcmp (target, "object"))
    argv[argc++] = "--" UNLOAD_DATA_ONLY_L;
  if (uStringEqual (ref, "yes"))
    argv[argc++] = "--" UNLOAD_INCLUDE_REFERENCE_L;
  if (uStringEqual (classonly, "yes"))
    argv[argc++] = "--" UNLOAD_INPUT_CLASS_ONLY_L;
  if (uStringEqual (delimit, "yes"))
    argv[argc++] = "--" UNLOAD_USE_DELIMITER_L;
  if (estimate != NULL && !uStringEqual (estimate, "none"))
    {
      argv[argc++] = "--" UNLOAD_ESTIMATED_SIZE_L;
      argv[argc++] = estimate;
    }
  if (prefix != NULL && !uStringEqual (prefix, "none"))
    {
      argv[argc++] = "--" UNLOAD_OUTPUT_PREFIX_L;
      argv[argc++] = prefix;
    }
  if (cach != NULL && !uStringEqual (cach, "none"))
    {
      argv[argc++] = "--" UNLOAD_CACHED_PAGES_L;
      argv[argc++] = cach;
    }
  if (lofile != NULL && !uStringEqual (lofile, "none"))
    {
      argv[argc++] = "--" UNLOAD_LO_COUNT_L;
      argv[argc++] = lofile;
    }
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  if (run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL) < 0)
    {				/* unloaddb */
      strcpy (_dbmt_error, argv[0]);
      unlink (tmpfile);
      return ERR_SYSTEM_CALL;
    }

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    {
      unlink (tmpfile);
      return ERR_WITH_MSG;
    }

  unlink (tmpfile);

  /* makeup upload result information in unload.log file */
  sprintf (buf, "%s_unloaddb.log", dbname);
  nv_add_nvp (res, "open", "result");
  if ((infile = fopen (buf, "rt")) != NULL)
    {
      flag = 0;
      while (fgets (buf, sizeof (buf), infile))
	{
	  if (buf[0] == '-')
	    flag++;
	  else if (flag == 2 &&
		   sscanf (buf, "%s %*s %s %s %*s %s", cname, p1, p2,
			   p3) == 4)
	    {
	      sprintf (buf, "%s %s/%s", p1, p2, p3);
	      nv_add_nvp (res, cname, buf);
	    }
	}
      fclose (infile);
    }
  nv_add_nvp (res, "close", "result");
  unlink ("unload.log");

  /* save uploaded result file to 'unloaddb.info' file */
  flag = 0;
  sprintf (infofile, "%s/unloaddb.info", sco.szCubrid_databases);
  if ((infile = fopen (infofile, "rt")) == NULL)
    {
      outfile = fopen (infofile, "w");
      sprintf (buf, "%% %s\n", dbname);
      fputs (buf, outfile);
      if (!strcmp (target, "both"))
	{
	  fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_SCHEMA);
	  fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_OBJ);
	}
      else if (!strcmp (target, "schema"))
	{
	  fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_SCHEMA);
	}
      else if (!strcmp (target, "object"))
	{
	  fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_OBJ);
	}
      /* check index file and append if exist */
      sprintf (buf, "%s/%s%s", targetdir, dbname, CUBRID_UNLOAD_EXT_INDEX);
      if (stat (buf, &statbuf) == 0)
	{
	  fprintf (outfile, "index %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_INDEX);
	}
      /* check trigger file and append if exist */
      sprintf (buf, "%s/%s%s", targetdir, dbname, CUBRID_UNLOAD_EXT_TRIGGER);
      if (stat (buf, &statbuf) == 0)
	{
	  fprintf (outfile, "trigger %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_TRIGGER);
	}
      fclose (outfile);
    }
  else
    {
      sprintf (tmpfile, "%s/DBMT_task_102.%d", sco.dbmt_tmp_dir,
	       (int) getpid ());
      outfile = fopen (tmpfile, "w");
      while (fgets (buf, sizeof (buf), infile))
	{
	  if (sscanf (buf, "%s %s", n, v) != 2)
	    {
	      fputs (buf, outfile);
	      continue;
	    }
	  if (!strcmp (n, "%") && !strcmp (v, dbname))
	    {
	      fputs (buf, outfile);
	      if (!strcmp (target, "both"))
		{
		  fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_SCHEMA);
		  fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_OBJ);
		}
	      else if (!strcmp (target, "schema"))
		{
		  fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_SCHEMA);
		}
	      else if (!strcmp (target, "object"))
		{
		  fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_OBJ);
		}
	      /* check index file and append if exist */
	      sprintf (temp, "%s/%s%s", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_INDEX);
	      if (stat (temp, &statbuf) == 0)
		{
		  fprintf (outfile, "index %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_INDEX);
		  index_exist = 1;
		}
	      /* check trigger file and append if exist */
	      sprintf (temp, "%s/%s%s", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_TRIGGER);
	      if (stat (temp, &statbuf) == 0)
		{
		  fprintf (outfile, "trigger %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_TRIGGER);
		  trigger_exist = 1;
		}
	      flag = 1;
	      continue;
	    }
	  if (!strcmp (target, "both") || !strcmp (target, "schema"))
	    {
	      sprintf (temp, "%s/%s%s", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_SCHEMA);
	      if (!strcmp (n, "schema") && !strcmp (v, temp))
		continue;
	    }
	  if (!strcmp (target, "both") || !strcmp (target, "object"))
	    {
	      sprintf (temp, "%s/%s%s", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_OBJ);
	      if (!strcmp (n, "object") && !strcmp (v, temp))
		continue;
	    }
	  if (index_exist)
	    {
	      sprintf (temp, "%s/%s%s", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_INDEX);
	      if (!strcmp (n, "index") && !strcmp (v, temp))
		continue;
	    }
	  if (trigger_exist)
	    {
	      sprintf (temp, "%s/%s%s", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_TRIGGER);
	      if (!strcmp (n, "trigger") && !strcmp (v, temp))
		continue;
	    }
	  fputs (buf, outfile);
	}			/* end of while(fgets()) */
      if (flag == 0)
	{
	  fprintf (outfile, "%% %s\n", dbname);
	  if (!strcmp (target, "both"))
	    {
	      fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_SCHEMA);
	      fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_OBJ);
	    }
	  else if (!strcmp (target, "schema"))
	    {
	      fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_SCHEMA);
	    }
	  else if (!strcmp (target, "object"))
	    {
	      fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_OBJ);
	    }
	  /* check index file and append if exist */
	  sprintf (temp, "%s/%s%s", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_INDEX);
	  if (stat (temp, &statbuf) == 0)
	    {
	      fprintf (outfile, "index %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_INDEX);
	      index_exist = 1;
	    }
	  /* check trigger file and append if exist */
	  sprintf (temp, "%s/%s%s", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_TRIGGER);
	  if (stat (temp, &statbuf) == 0)
	    {
	      fprintf (outfile, "trigger %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_TRIGGER);
	      trigger_exist = 1;
	    }
	}
      fclose (infile);
      fclose (outfile);

      /* copyback */
      infile = fopen (tmpfile, "rt");
      outfile = fopen (infofile, "w");
      while (fgets (buf, sizeof (buf), infile))
	{
	  fputs (buf, outfile);
	}
      fclose (infile);
      fclose (outfile);
      unlink (tmpfile);
    }				/* end of if */

  return ERR_NO_ERROR;
}

int
ts_loaddb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *checkoption, *period, *user, *schema, *object,
    *index, *estimated, *oiduse, *nolog, buf[1024], tmpfile[256];
  FILE *infile;
  T_DB_SERVICE_MODE db_mode;
  char *dbuser, *dbpasswd;
  char *cubrid_err_file;
  int retval;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[25];
  int argc = 0;
  int status;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  dbuser = nv_get_val (req, "_DBID");
  dbpasswd = nv_get_val (req, "_DBPASSWD");

  checkoption = nv_get_val (req, "checkoption");
  period = nv_get_val (req, "period");
  user = nv_get_val (req, "user");
  schema = nv_get_val (req, "schema");
  object = nv_get_val (req, "object");
  index = nv_get_val (req, "index");
#if 0				/* will be added */
  trigger = nv_get_val (req, "trigger");
#endif
  estimated = nv_get_val (req, "estimated");
  oiduse = nv_get_val (req, "oiduse");
  nolog = nv_get_val (req, "nolog");

  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  else if (db_mode == DB_SERVICE_MODE_CS)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_DB_ACTIVE;
    }

  sprintf (tmpfile, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir, TS_LOADDB,
	   (int) getpid ());
  cubrid_cmd_name (cmd_name);
  argc = 0;
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_LOADDB;
  if (!strcmp (checkoption, "syntax"))
    argv[argc++] = "--" LOAD_CHECK_ONLY_L;
  else if (!strcmp (checkoption, "load"))
    argv[argc++] = "--" LOAD_LOAD_ONLY_L;

  if (dbuser)
    {
      argv[argc++] = "--" LOAD_USER_L;
      argv[argc++] = dbuser;
      if (dbpasswd)
	{
	  argv[argc++] = "--" LOAD_PASSWORD_L;
	  argv[argc++] = dbpasswd;
	}
    }

/*    argv[argc++] = "-v";*/
  if (period != NULL && !uStringEqual (period, "none"))
    {
      argv[argc++] = "--" LOAD_PERIODIC_COMMIT_L;
      argv[argc++] = period;
    }

  if (strcmp (schema, "none"))
    {
      argv[argc++] = "--" LOAD_SCHEMA_FILE_L;
      argv[argc++] = schema;
    }

  if (strcmp (object, "none"))
    {
      argv[argc++] = "--" LOAD_DATA_FILE_L;
      argv[argc++] = object;
    }

  if (strcmp (index, "none"))
    {
      argv[argc++] = "--" LOAD_INDEX_FILE_L;
      argv[argc++] = index;
    }

#if 0				/* will be added */
  if (trigger != NULL && !uStringEqual (trigger, "none"))
    {
      argv[argc++] = "-tf";
      argv[argc++] = trigger;
    }
#endif

  if (estimated != NULL && !uStringEqual (estimated, "none"))
    {
      argv[argc++] = "--" LOAD_ESTIMATED_SIZE_L;
      argv[argc++] = estimated;
    }

  if (uStringEqual (oiduse, "no"))
    argv[argc++] = "--" LOAD_NO_OID_L;

  if (uStringEqual (nolog, "yes"))
    argv[argc++] = "--" LOAD_IGNORE_LOGGING_L;

  argv[argc++] = dbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  retval = run_child (argv, 1, NULL, tmpfile, cubrid_err_file, &status);	/* loaddb */
  if (status != 0
      && read_error_file (cubrid_err_file, _dbmt_error,
			  DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;

  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  infile = fopen (tmpfile, "r");

  while (fgets (buf, sizeof (buf), infile))
    {
      uRemoveCRLF (buf);
      nv_add_nvp (res, "line", buf);
    }

  fclose (infile);
  unlink (tmpfile);

  return ERR_NO_ERROR;
}

int
ts_restoredb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *date, *lv, *pathname, *partial;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[15];
  int argc = 0;
  T_DB_SERVICE_MODE db_mode;
  char *cubrid_err_file;
  int status;

  dbname = nv_get_val (req, "dbname");
  db_mode = uDatabaseMode (dbname);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  else if (db_mode == DB_SERVICE_MODE_CS)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_DB_ACTIVE;
    }

  date = nv_get_val (req, "date");
  lv = nv_get_val (req, "level");
  pathname = nv_get_val (req, "pathname");
  partial = nv_get_val (req, "partial");

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_RESTOREDB;
  if (strcmp (date, "none"))
    {
      argv[argc++] = "--" RESTORE_UP_TO_DATE_L;
      argv[argc++] = date;
    }
  argv[argc++] = "--" RESTORE_LEVEL_L;
  argv[argc++] = lv;
  if (pathname != NULL && !uStringEqual (pathname, "none"))
    {
      argv[argc++] = "--" RESTORE_BACKUP_FILE_PATH_L;
      argv[argc++] = pathname;
    }
  if (uStringEqual (partial, "y"))
    argv[argc++] = "--" RESTORE_PARTIAL_RECOVERY_L;
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

#ifdef WIN32
  if (run_child (argv, 1, NULL, NULL, cubrid_err_file, &status) < 0)
#else
  if (run_child (argv, 1, "/dev/null", NULL, cubrid_err_file, &status) < 0)
#endif
    {				/* restoredb */
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  if (status != 0
      && read_error_file (cubrid_err_file, _dbmt_error,
			  DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts_backup_vol_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *lv, *pathname, buf[1024], tmpfile[256];
  int ret;
  FILE *infile;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[10];
  int argc = 0;

  dbname = nv_get_val (req, "dbname");
  sprintf (tmpfile, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir, TS_BACKUPVOLINFO,
	   (int) getpid ());

  if (uIsDatabaseActive (dbname))
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_DB_ACTIVE;
    }

  if (uDatabaseMode (dbname) == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  lv = nv_get_val (req, "level");
  pathname = nv_get_val (req, "pathname");

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_RESTOREDB;
  argv[argc++] = "--" RESTORE_LIST_L;
  if (lv != NULL &&
      (uStringEqual (lv, "0") || uStringEqual (lv, "1")
       || uStringEqual (lv, "2")))
    {
      argv[argc++] = "--" RESTORE_LEVEL_L;
      argv[argc++] = lv;
    }
  if (pathname != NULL && !uStringEqual (pathname, "none"))
    {
      argv[argc++] = "--" RESTORE_BACKUP_FILE_PATH_L;
      argv[argc++] = pathname;
    }
  argv[argc++] = dbname;
  argv[argc++] = NULL;

#ifdef WIN32
  ret = run_child (argv, 1, NULL, tmpfile, NULL, NULL);	/* restoredb -t */
#else
  ret = run_child (argv, 1, "/dev/null", tmpfile, NULL, NULL);	/* restoredb -t */
#endif
  if (ret < 0)
    {
      sprintf (_dbmt_error, "%s", argv[0]);
      return ERR_SYSTEM_CALL;
    }

  infile = fopen (tmpfile, "r");
  while (fgets (buf, sizeof (buf), infile))
    {
      uRemoveCRLF (buf);
      nv_add_nvp (res, "line", buf);
    }
  fclose (infile);
  unlink (tmpfile);

  return ERR_NO_ERROR;
}

int
ts_get_dbsize (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  char strbuf[1024], dbdir[512];
  int pagesize, no_tpage = 0, log_size = 0, baselen;
  struct stat statbuf;
  T_SPACEDB_RESULT *cmd_res;
  T_CUBRID_MODE cubrid_mode;
  int i;
#ifdef WIN32
  char find_file[512];
  WIN32_FIND_DATA data;
  HANDLE handle;
  int found;
#else
  DIR *dirp;
  struct dirent *dp;
#endif
  char *cur_file;

  /* get dbname */
  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  if (uRetrieveDBDirectory (dbname, dbdir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  cubrid_mode =
    (uDatabaseMode (dbname) ==
     DB_SERVICE_MODE_NONE) ? CUBRID_MODE_SA : CUBRID_MODE_CS;
  cmd_res = cmd_spacedb (dbname, cubrid_mode);

  if (cmd_res == NULL || cmd_res->err_msg[0])
    {
      sprintf (_dbmt_error, "spacedb %s", dbname);
      cmd_spacedb_result_free (cmd_res);
      return ERR_SYSTEM_CALL;
    }

  for (i = 0; i < cmd_res->num_vol; i++)
    {
      no_tpage += cmd_res->vol_info[i].total_page;
    }
  for (i = 0; i < cmd_res->num_tmp_vol; i++)
    {
      no_tpage += cmd_res->tmp_vol_info[i].total_page;
    }
  pagesize = cmd_res->page_size;
  cmd_spacedb_result_free (cmd_res);

  /* get log volume info */
#ifdef WIN32
  sprintf (find_file, "%s/*", dbdir);
  if ((handle = FindFirstFile (find_file, &data)) == INVALID_HANDLE_VALUE)
#else
  if ((dirp = opendir (dbdir)) == NULL)
#endif
    {
      sprintf (_dbmt_error, "%s", dbdir);
      return ERR_DIROPENFAIL;
    }

  baselen = strlen (dbname);
#ifdef WIN32
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dp = readdir (dirp)) != NULL)
#endif
    {
#ifdef WIN32
      cur_file = data.cFileName;
#else
      cur_file = dp->d_name;
#endif
      if (!strncmp (cur_file + baselen, "_lginf", 6)
	  || !strcmp (cur_file + baselen, CUBRID_ACT_LOG_EXT)
	  || !strncmp (cur_file + baselen, CUBRID_ARC_LOG_EXT,
		       CUBRID_ARC_LOG_EXT_LEN))
	{
	  sprintf (strbuf, "%s/%s", dbdir, cur_file);
	  stat (strbuf, &statbuf);
	  log_size += statbuf.st_size;
	}
    }

  sprintf (strbuf, "%d", no_tpage * pagesize + log_size);
  nv_add_nvp (res, "dbsize", strbuf);

  return ERR_NO_ERROR;
}

/******************************************************************************
	TASK MISC
******************************************************************************/
int
ts_get_access_right (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *d_id;
  nvplist *ud = NULL;
  int n;
  int retval;
  T_COMMDB_RESULT *cmd_res = NULL;

  ud = nv_create (6, NULL, "\n", ":", "\n");
  d_id = nv_get_val (req, "_ID");

  if ((retval =
       _tsReadUserCapability (ud, d_id, _dbmt_error)) != ERR_NO_ERROR)
    {
      nv_destroy (ud);
      return retval;
    }

  nv_add_nvp (res, "open", "userauth");
  nv_add_nvp (res, "id", d_id);

  for (n = 0; n < ud->nvplist_leng; ++n)
    {
      char *z_name, *z_value;
      char buf[1024], *tok[3];

      nv_lookup (ud, n, &z_name, &z_value);

      if (uStringEqual (z_name, "unicas"))
	continue;
      if (uStringEqual (z_name, "dbcreate"))
	continue;

      if (z_value == NULL)
	continue;

      nv_add_nvp (res, "open", "dbauth");
      strcpy (buf, z_value);
      string_tokenize2 (buf, tok, 3, ';');
      nv_add_nvp (res, "dbname", z_name);

      if (!_isRegisteredDB (z_name))
	{
	  nv_add_nvp (res, "authority", "none");
	}
      else
	{
	  if (cmd_res == NULL)
	    cmd_res = cmd_commdb ();

	  retval = op_db_user_pass_check (z_name, tok[1], tok[2]);
	  if (retval)
	    {
	      if (retval == 2)
		{
		  /* database is running stand alone mode */
		  nv_add_nvp (res, "authority", "unknown");
		}
	      else if (uStringEqual (tok[1], "dba"))
		nv_add_nvp (res, "authority", "dba");
	      else
		nv_add_nvp (res, "authority", "general");
	    }
	  else
	    nv_add_nvp (res, "authority", "none");
	}
      nv_add_nvp (res, "close", "dbauth");
    }
  cmd_commdb_result_free (cmd_res);

  if (nv_get_val (ud, "unicas"))
    nv_add_nvp (res, "casauth", nv_get_val (ud, "unicas"));
  else
    nv_add_nvp (res, "casauth", "none");
#if 0
  if (nv_get_val (ud, "dbcreate"))
    nv_add_nvp (res, "dbcreate", nv_get_val (ud, "dbcreate"));
  else
    nv_add_nvp (res, "dbcreate", "none");
#endif

  nv_add_nvp (res, "close", "userauth");
  nv_destroy (ud);

  return ERR_NO_ERROR;
}


int
tsGetHistory (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char strbuf[1024];
  FILE *infile;
  char err_flag = 0;
  int i;
  char *conf_item[AUTOHISTORY_CONF_ENTRY_NUM];

  infile = fopen (conf_get_dbmt_file (FID_AUTO_HISTORY_CONF, strbuf), "r");
  if (infile == NULL)
    {
      err_flag = 1;
    }
  else
    {
      memset (strbuf, 0, sizeof (strbuf));
      while (fgets (strbuf, sizeof (strbuf), infile))
	{
	  ut_trim (strbuf);
	  if (strbuf[0] == '#' || strbuf[0] == '\0')
	    {
	      memset (strbuf, 0, sizeof (strbuf));
	      continue;
	    }
	  break;
	}
      if (string_tokenize (strbuf, conf_item, AUTOHISTORY_CONF_ENTRY_NUM) < 0)
	err_flag = 1;

      if (!err_flag)
	{
	  for (i = 0; i < AUTOHISTORY_CONF_ENTRY_NUM; i++)
	    {
	      nv_add_nvp (res, autohistory_conf_entry[i], conf_item[i]);
	    }

	  /* read dblist */
	  nv_add_nvp (res, "open", "dblist");
	  while (fgets (strbuf, sizeof (strbuf), infile))
	    {
	      ut_trim (strbuf);
	      if (strbuf[0] == '\0' || strbuf[0] == '#')
		continue;
	      nv_add_nvp (res, "dbname", strbuf);
	    }
	  nv_add_nvp (res, "close", "dblist");
	}
      fclose (infile);
    }

  if (err_flag)
    {
      nv_add_nvp (res, autohistory_conf_entry[0], "OFF");
      nv_add_nvp (res, autohistory_conf_entry[1], "0");
      nv_add_nvp (res, autohistory_conf_entry[2], "0");
      nv_add_nvp (res, autohistory_conf_entry[3], "0");
      nv_add_nvp (res, autohistory_conf_entry[4], "0");
      nv_add_nvp (res, autohistory_conf_entry[5], "0");
      nv_add_nvp (res, autohistory_conf_entry[6], "0");
      nv_add_nvp (res, autohistory_conf_entry[7], "0");
      nv_add_nvp (res, autohistory_conf_entry[8], "0");
      nv_add_nvp (res, autohistory_conf_entry[9], "0");
      nv_add_nvp (res, autohistory_conf_entry[10], "0");
      nv_add_nvp (res, autohistory_conf_entry[11], "0");
      nv_add_nvp (res, autohistory_conf_entry[12], "0");
      nv_add_nvp (res, autohistory_conf_entry[13], "0");
      nv_add_nvp (res, autohistory_conf_entry[14], "0");
      nv_add_nvp (res, "open", "dblist");
      nv_add_nvp (res, "close", "dblist");
    }
  return ERR_NO_ERROR;
}

int
tsSetHistory (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *outfile;
  int ptr, ptrlen;
  char *name, *value;
  int i;
  char conf_file[512];
  char *conf_item[AUTOHISTORY_CONF_ENTRY_NUM];

  for (i = 0; i < AUTOHISTORY_CONF_ENTRY_NUM; i++)
    {
      conf_item[i] = nv_get_val (req, autohistory_conf_entry[i]);
      if (conf_item[i] == NULL)
	{
	  strcpy (_dbmt_error, autohistory_conf_entry[i]);
	  return ERR_PARAM_MISSING;
	}
    }

  conf_get_dbmt_file (FID_AUTO_HISTORY_CONF, conf_file);
  if ((outfile = fopen (conf_file, "w")) == NULL)
    {
      sprintf (_dbmt_error, "%s", conf_file);
      return ERR_FILE_OPEN_FAIL;
    }

  for (i = 0; i < AUTOHISTORY_CONF_ENTRY_NUM; i++)
    fprintf (outfile, "%s ", conf_item[i]);
  fprintf (outfile, "\n");

  nv_locate (req, "dblist", &ptr, &ptrlen);
  for (i = 0; i < ptrlen; ++i)
    {
      nv_lookup (req, i + ptr, &name, &value);
      fprintf (outfile, "%s\n", value);
    }
  fclose (outfile);

  return ERR_NO_ERROR;
}

int
tsGetHistoryFileList (nvplist * req, nvplist * res, char *_dbmt_error)
{
#ifdef WIN32
  WIN32_FIND_DATA data;
  char find_file[512];
  HANDLE handle;
  int found;
#else
  DIR *dirp;
  struct dirent *dp;
#endif
  struct stat statbuf;
  char dirbuf[1024], fpathbuf[1024];
  char *cur_file;

  sprintf (dirbuf, "%s/logs", sco.szCubrid);

#ifdef WIN32
  sprintf (find_file, "%s/*", dirbuf);
  if ((handle = FindFirstFile (find_file, &data)) == INVALID_HANDLE_VALUE)
    return ERR_OPENDIR;
#else
  dirp = opendir (dirbuf);
  if (dirp == NULL)
    return ERR_OPENDIR;
#endif

  nv_add_nvp (res, "open", "filelist");
#ifdef WIN32
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dp = readdir (dirp)) != NULL)
#endif
    {
#ifdef WIN32
      cur_file = data.cFileName;
#else
      cur_file = dp->d_name;
#endif
      if (!strncmp ("_dbmt_history.", cur_file, strlen ("_dbmt_history.")))
	{
	  nv_add_nvp (res, "name", cur_file);
	  sprintf (fpathbuf, "%s/logs/%s", sco.szCubrid, cur_file);
	  stat (fpathbuf, &statbuf);
	  nv_add_nvp_int (res, "size", statbuf.st_size);
	  nv_add_nvp_time (res, "update", statbuf.st_mtime,
			   "%04d/%02d/%02d-%02d:%02d:%02d", NV_ADD_DATE_TIME);
	  nv_add_nvp (res, "path", dirbuf);
	}
    }
#ifdef WIN32
  FindClose (handle);
#else
  closedir (dirp);
#endif
  nv_add_nvp (res, "close", "filelist");
  return ERR_NO_ERROR;
}

int
tsReadHistoryFile (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *fname;
  char fnamebuf[1024], strbuf[1024];
  FILE *infile;

  if ((fname = nv_get_val (req, "name")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "file name");
      return ERR_PARAM_MISSING;
    }
  sprintf (fnamebuf, "%s/logs/%s", sco.szCubrid, fname);

  nv_add_nvp (res, "open", "view");
  if ((infile = fopen (fnamebuf, "r")) != NULL)
    {
      while (fgets (strbuf, 1024, infile))
	{
	  uRemoveCRLF (strbuf);
	  nv_add_nvp (res, "msg", strbuf);
	}
      fclose (infile);
    }
  else
    {
      sprintf (_dbmt_error, "%s", fname);
      return ERR_FILE_OPEN_FAIL;
    }
  nv_add_nvp (res, "close", "view");

  return ERR_NO_ERROR;
}

int
tsGetEnvironment (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char tmpfile[512];
  char strbuf[1024];
  FILE *infile;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[5];

  nv_add_nvp (res, "CUBRID", sco.szCubrid);
  nv_add_nvp (res, "CUBRID_DATABASES", sco.szCubrid_databases);
  nv_add_nvp (res, "CUBRID_DBMT", sco.szCubrid);
  sprintf (tmpfile, "%s/DBMT_task_015.%d", sco.dbmt_tmp_dir, (int) getpid ());

  cmd_name[0] = '\0';
  sprintf (cmd_name, "%s/%s%s", sco.szCubrid,
	   CUBRID_DIR_BIN, UTIL_CUBRID_REL_NAME);

  argv[0] = cmd_name;
  argv[1] = NULL;

  run_child (argv, 1, NULL, tmpfile, NULL, NULL);	/* cubrid_rel */

  if ((infile = fopen (tmpfile, "r")) != NULL)
    {
      fgets (strbuf, 1024, infile);
      fgets (strbuf, 1024, infile);
      uRemoveCRLF (strbuf);
      fclose (infile);
      unlink (tmpfile);
      nv_add_nvp (res, "CUBRIDVER", strbuf);
    }
  else
    nv_add_nvp (res, "CUBRIDVER", "version information not available");

  sprintf (tmpfile, "%s/DBMT_task_015.%d", sco.dbmt_tmp_dir, (int) getpid ());
  sprintf (cmd_name, "%s/bin/cubrid_broker%s", sco.szCubrid, DBMT_EXE_EXT);

  argv[0] = cmd_name;
  argv[1] = "--version";
  argv[2] = NULL;

  run_child (argv, 1, NULL, tmpfile, NULL, NULL);	/* cubrid_broker --version */

  if ((infile = fopen (tmpfile, "r")) != NULL)
    {
      fgets (strbuf, 1024, infile);
      fclose (infile);
      uRemoveCRLF (strbuf);
      unlink (tmpfile);
      nv_add_nvp (res, "BROKERVER", strbuf);
    }
  else
    nv_add_nvp (res, "BROKERVER", "version information not available");

  if (sco.hmtab1 == 1)
    nv_add_nvp (res, "HOSTMONTAB0", "ON");
  else
    nv_add_nvp (res, "HOSTMONTAB0", "OFF");
  if (sco.hmtab2 == 1)
    nv_add_nvp (res, "HOSTMONTAB1", "ON");
  else
    nv_add_nvp (res, "HOSTMONTAB1", "OFF");
  if (sco.hmtab3 == 1)
    nv_add_nvp (res, "HOSTMONTAB2", "ON");
  else
    nv_add_nvp (res, "HOSTMONTAB2", "OFF");
  if (sco.hmtab4 == 1)
    nv_add_nvp (res, "HOSTMONTAB3", "ON");
  else
    nv_add_nvp (res, "HOSTMONTAB3", "OFF");

#if defined(WIN32)
  nv_add_nvp (res, "osinfo", "NT");
#else
  nv_add_nvp (res, "osinfo", "unknown");
#endif


  return ERR_NO_ERROR;
}

int
ts_startinfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_COMMDB_RESULT *cmd_res;
  int retval;

  /* add dblist */
  retval = _tsAppendDBList (res, 1);
  if (retval != ERR_NO_ERROR)
    return retval;

  nv_add_nvp (res, "open", "activelist");
  cmd_res = cmd_commdb ();
  if (cmd_res != NULL)
    {
      T_COMMDB_INFO *info = (T_COMMDB_INFO *) cmd_res->result;
      int i;
      for (i = 0; i < cmd_res->num_result; i++)
	{
	  nv_add_nvp (res, "dbname", info[i].db_name);
	}
    }
  nv_add_nvp (res, "close", "activelist");

  uWriteDBnfo2 (cmd_res);
  cmd_commdb_result_free (cmd_res);

  return ERR_NO_ERROR;
}

int
ts_kill_process (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *pid_str;
  int pid;
  char *tgt_name;

  if ((pid_str = nv_get_val (req, "pid")) == NULL)
    {
      strcpy (_dbmt_error, "pid");
      return ERR_PARAM_MISSING;
    }
  tgt_name = nv_get_val (req, "name");

  pid = atoi (pid_str);
  if (pid > 0)
    {
      if (kill (pid, SIGTERM) < 0)
	{
	  DBMT_ERR_MSG_SET (_dbmt_error, strerror (errno));
	  return ERR_WITH_MSG;
	}
    }

  nv_add_nvp (res, "name", tgt_name);
  uWriteDBnfo ();
  return ERR_NO_ERROR;
}

int
ts_backupdb_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char db_dir[512], log_dir[512];
  char *dbname, vinf[256], buf[1024];
  FILE *infile;
  struct stat statbuf;
  char *tok[3];

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uDatabaseMode (dbname) == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (uRetrieveDBDirectory (dbname, db_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }
  sprintf (buf, "%s/backup", db_dir);
  nv_add_nvp (res, "dbdir", buf);

  if (uRetrieveDBLogDirectory (dbname, log_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  sprintf (vinf, "%s/%s%s", log_dir, dbname, CUBRID_BACKUP_INFO_EXT);
  if ((infile = fopen (vinf, "rt")) != NULL)
    {
      while (fgets (buf, 1024, infile))
	{
	  ut_trim (buf);
	  if (string_tokenize (buf, tok, 3) < 0)
	    continue;
	  if (stat (tok[2], &statbuf) == 0)
	    {
	      sprintf (vinf, "level%s", tok[0]);
	      nv_add_nvp (res, "open", vinf);
	      nv_add_nvp (res, "path", tok[2]);
	      nv_add_nvp_int (res, "size", statbuf.st_size);
	      nv_add_nvp_time (res, "data", statbuf.st_mtime,
			       "%04d.%02d.%02d.%02d.%02d", NV_ADD_DATE_TIME);
	      nv_add_nvp (res, "close", vinf);
	    }
	}
      fclose (infile);
    }

  nv_add_nvp_int (res, "freespace", ut_disk_free_space (db_dir));

  return ERR_NO_ERROR;
}

int
ts_unloaddb_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char n[256], v[256], buf[1024];
  FILE *infile;
  int flag = 0;
  struct stat statbuf;

  sprintf (buf, "%s/unloaddb.info", sco.szCubrid_databases);
  if ((infile = fopen (buf, "rt")) == NULL)
    {
      return ERR_NO_ERROR;
    }

  while (fgets (buf, 1024, infile))
    {
      if (sscanf (buf, "%s %s", n, v) != 2)
	continue;
      if (!strcmp (n, "%"))
	{
	  if (flag == 1)
	    nv_add_nvp (res, "close", "database");
	  else
	    flag = 1;
	  nv_add_nvp (res, "open", "database");
	  nv_add_nvp (res, "dbname", v);
	}
      else
	{
	  if (stat (v, &statbuf) == 0)
	    {
	      char timestr[64];
	      time_to_str (statbuf.st_mtime, "%04d.%02d.%02d %02d:%02d",
			   timestr, TIME_STR_FMT_DATE_TIME);
	      sprintf (buf, "%s;%s", v, timestr);
	      nv_add_nvp (res, n, buf);
	    }
	}
    }
  if (flag == 1)
    nv_add_nvp (res, "close", "database");
  fclose (infile);

  return ERR_NO_ERROR;
}

/* backup automation */

int
ts_get_backup_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  FILE *infile;
  char strbuf[1024];
  char *conf_item[AUTOBACKUP_CONF_ENTRY_NUM];
  int i;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  nv_add_nvp (res, "dbname", dbname);
  infile = fopen (conf_get_dbmt_file (FID_AUTO_BACKUPDB_CONF, strbuf), "r");
  if (infile == NULL)
    {
      return ERR_NO_ERROR;
    }

  while (fgets (strbuf, 1024, infile))
    {
      ut_trim (strbuf);
      if (strbuf[0] == '#')
	continue;
      if (string_tokenize (strbuf, conf_item, AUTOBACKUP_CONF_ENTRY_NUM) < 0)
	continue;

      if (strcmp (conf_item[0], dbname) == 0)
	{
	  for (i = 0; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
	    {
	      nv_add_nvp (res, autobackup_conf_entry[i], conf_item[i]);
	    }
	}
    }
  fclose (infile);

  return ERR_NO_ERROR;
}

int
ts_set_backup_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *infile, *outfile;
  char line[1024], tmpfile[512];
  char autofilepath[512];
  char *conf_item[AUTOBACKUP_CONF_ENTRY_NUM];
  int i;

  if ((conf_item[0] = nv_get_val (req, "_DBNAME")) == NULL)
    return ERR_PARAM_MISSING;
  for (i = 1; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
    {
      conf_item[i] = nv_get_val (req, autobackup_conf_entry[i]);
      if (conf_item[i] == NULL)
	return ERR_PARAM_MISSING;
    }

  conf_get_dbmt_file (FID_AUTO_BACKUPDB_CONF, autofilepath);
  if (access (autofilepath, F_OK) < 0)
    {
      outfile = fopen (autofilepath, "w");
      if (outfile == NULL)
	{
	  strcpy (_dbmt_error, autofilepath);
	  return ERR_FILE_OPEN_FAIL;
	}
      for (i = 0; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
	fprintf (outfile, "%s ", conf_item[i]);
      fprintf (outfile, "\n");
      fclose (outfile);
      return ERR_NO_ERROR;
    }

  if ((infile = fopen (autofilepath, "r")) == NULL)
    {
      strcpy (_dbmt_error, autofilepath);
      return ERR_FILE_OPEN_FAIL;
    }
  sprintf (tmpfile, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir, TS_SETBACKUPINFO,
	   (int) getpid ());
  if ((outfile = fopen (tmpfile, "w")) == NULL)
    {
      fclose (infile);
      return ERR_TMPFILE_OPEN_FAIL;
    }
  while (fgets (line, 1024, infile))
    {
      char conf_dbname[128], conf_backupid[128];

      if (sscanf (line, "%s %s", conf_dbname, conf_backupid) < 2)
	continue;
      if ((strcmp (conf_dbname, conf_item[0]) == 0) &&
	  (strcmp (conf_backupid, conf_item[1]) == 0))
	{
	  for (i = 0; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
	    fprintf (outfile, "%s ", conf_item[i]);
	  fprintf (outfile, "\n");
	}
      else
	{
	  fputs (line, outfile);
	}
    }
  fclose (infile);
  fclose (outfile);
  move_file (tmpfile, autofilepath);

  return ERR_NO_ERROR;
}

int
ts_add_backup_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *outfile;
  char autofilepath[512];
  char *conf_item[AUTOBACKUP_CONF_ENTRY_NUM];
  int i;

  if ((conf_item[0] = nv_get_val (req, "_DBNAME")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }
  for (i = 1; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
    {
      conf_item[i] = nv_get_val (req, autobackup_conf_entry[i]);
      if (conf_item[i] == NULL)
	return ERR_PARAM_MISSING;
    }

  conf_get_dbmt_file (FID_AUTO_BACKUPDB_CONF, autofilepath);
  if ((outfile = fopen (autofilepath, "a")) == NULL)
    {
      strcpy (_dbmt_error, autofilepath);
      return ERR_FILE_OPEN_FAIL;
    }
  for (i = 0; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
    fprintf (outfile, "%s ", conf_item[i]);
  fprintf (outfile, "\n");

  fclose (outfile);

  return ERR_NO_ERROR;
}

int
ts_delete_backup_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *backupid;
  FILE *infile, *outfile;
  char line[1024], tmpfile[256];
  char autofilepath[512];

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }
  backupid = nv_get_val (req, "backupid");

  conf_get_dbmt_file (FID_AUTO_BACKUPDB_CONF, autofilepath);
  if ((infile = fopen (autofilepath, "r")) == NULL)
    {
      strcpy (_dbmt_error, autofilepath);
      return ERR_FILE_OPEN_FAIL;
    }
  sprintf (tmpfile, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir,
	   TS_DELETEBACKUPINFO, (int) getpid ());
  if ((outfile = fopen (tmpfile, "w")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  while (fgets (line, sizeof (line), infile))
    {
      char conf_dbname[128], conf_backupid[128];

      if (sscanf (line, "%s %s", conf_dbname, conf_backupid) != 2)
	continue;
      if ((strcmp (conf_dbname, dbname) != 0) ||
	  (strcmp (conf_backupid, backupid) != 0))
	{
	  fputs (line, outfile);
	}
    }
  fclose (infile);
  fclose (outfile);
  move_file (tmpfile, autofilepath);
  return ERR_NO_ERROR;
}

int
ts_get_log_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, log_dir[512], buf[1024];
  char *error_log_param;
  struct stat statbuf;
#ifdef WIN32
  WIN32_FIND_DATA data;
  HANDLE handle;
  int found;
#else
  DIR *dirp = NULL;
  struct dirent *dp = NULL;
#endif
  char find_file[512];
  char *fname;

  dbname = nv_get_val (req, "_DBNAME");

  if (uRetrieveDBDirectory (dbname, log_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  nv_add_nvp (res, "dbname", dbname);
  nv_add_nvp (res, "open", "loginfo");

  if ((error_log_param = _ts_get_error_log_param (dbname)) == NULL)
    sprintf (buf, "%s/%s.err", log_dir, dbname);
  else if (error_log_param[0] == '/')
    sprintf (buf, "%s", error_log_param);
#ifdef WIN32
  else if (error_log_param[2] == '/')
    sprintf (buf, "%s", error_log_param);
#endif
  else
    sprintf (buf, "%s/%s", log_dir, error_log_param);
  if (stat (buf, &statbuf) == 0)
    {
      nv_add_nvp (res, "open", "log");
      nv_add_nvp (res, "path", buf);
      nv_add_nvp (res, "owner", get_user_name (statbuf.st_uid, buf));
      nv_add_nvp_int (res, "size", statbuf.st_size);
      nv_add_nvp_time (res, "lastupdate", statbuf.st_mtime, "%04d.%02d.%02d",
		       NV_ADD_DATE);
      nv_add_nvp (res, "close", "log");
    }
  FREE_MEM (error_log_param);

  sprintf (buf, "%s/cub_server.err", log_dir);
  if (stat (buf, &statbuf) == 0)
    {
      nv_add_nvp (res, "open", "log");
      nv_add_nvp (res, "path", buf);
      nv_add_nvp (res, "owner", get_user_name (statbuf.st_uid, buf));
      nv_add_nvp_int (res, "size", statbuf.st_size);
      nv_add_nvp_time (res, "lastupdate", statbuf.st_mtime, "%04d.%02d.%02d",
		       NV_ADD_DATE);
      nv_add_nvp (res, "close", "log");
    }

  sprintf (find_file, "%s/%s", sco.szCubrid, CUBRID_ERROR_LOG_DIR);
#ifdef WIN32
  strcat (find_file, "/*");
  if ((handle = FindFirstFile (find_file, &data)) == INVALID_HANDLE_VALUE)
#else
  if ((dirp = opendir (find_file)) == NULL)
#endif
    {
      nv_add_nvp (res, "close", "loginfo");
      return ERR_NO_ERROR;
    }
#ifdef WIN32
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dp = readdir (dirp)) != NULL)
#endif
    {
#ifdef WIN32
      fname = data.cFileName;
#else
      fname = dp->d_name;
#endif
      if (strstr (fname, ".err") == NULL)
	continue;
      if (memcmp (fname, dbname, strlen (dbname)))
	continue;
      if (isalnum (fname[strlen (dbname)]))
	continue;
      sprintf (buf, "%s/%s/%s", sco.szCubrid, CUBRID_ERROR_LOG_DIR, fname);
      if (stat (buf, &statbuf) == 0)
	{
	  nv_add_nvp (res, "open", "log");
	  nv_add_nvp (res, "path", buf);
	  nv_add_nvp (res, "owner", get_user_name (statbuf.st_uid, buf));
	  nv_add_nvp_int (res, "size", statbuf.st_size);
	  nv_add_nvp_time (res, "lastupdate", statbuf.st_mtime,
			   "%04d.%02d.%02d", NV_ADD_DATE);
	  nv_add_nvp (res, "close", "log");
	}
    }
#ifdef WIN32
  FindClose (handle);
#else
  closedir (dirp);
#endif

  nv_add_nvp (res, "close", "loginfo");
  return ERR_NO_ERROR;
}

int
ts_view_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *filepath, *startline, *endline, buf[1024];
  FILE *infile;
  int no_line = 0, start, end;

  dbname = nv_get_val (req, "_DBNAME");
  filepath = nv_get_val (req, "path");
  startline = nv_get_val (req, "start");
  endline = nv_get_val (req, "end");
  if (startline != NULL)
    start = atoi (startline);
  else
    start = -1;
  if (endline != NULL)
    end = atoi (endline);
  else
    end = -1;

  if ((infile = fopen (filepath, "rt")) == NULL)
    {
      sprintf (_dbmt_error, "%s", filepath);
      return ERR_FILE_OPEN_FAIL;
    }
  nv_add_nvp (res, "path", filepath);
  nv_add_nvp (res, "open", "log");
  while (fgets (buf, 1024, infile))
    {
      no_line++;
      if (start != -1 && end != -1)
	{
	  if (start > no_line || end < no_line)
	    continue;
	}
      buf[strlen (buf) - 1] = '\0';
      nv_add_nvp (res, "line", buf);
    }
  fclose (infile);
  nv_add_nvp (res, "close", "log");

  if (start != -1)
    {
      if (start > no_line)
	sprintf (buf, "%d", no_line);
      else
	sprintf (buf, "%d", start);
      nv_add_nvp (res, "start", buf);
    }
  if (end != -1)
    {
      if (end > no_line)
	sprintf (buf, "%d", no_line);
      else
	sprintf (buf, "%d", end);
      nv_add_nvp (res, "end", buf);
    }
  sprintf (buf, "%d", no_line);
  nv_add_nvp (res, "total", buf);

  return ERR_NO_ERROR;
}

int
ts_reset_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *path;
  FILE *outfile;

  path = nv_get_val (req, "path");
  outfile = fopen (path, "w");
  if (outfile == NULL)
    {
      strcpy (_dbmt_error, path);
      return ERR_FILE_OPEN_FAIL;
    }
  fclose (outfile);

  return ERR_NO_ERROR;
}

int
ts_get_db_error (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char dbname[256], logfilepath[256], buf[1024];
  char wday[16], mon[16], day[16], time[16], tz[16], year[16], error_code[16];
  char dbfilepath[512];
  FILE *infile, *logfile;

  sprintf (dbfilepath, "%s/%s", sco.szCubrid_databases, CUBRID_DATABASE_TXT);
  if ((infile = fopen (dbfilepath, "rt")) == NULL)
    {
      strcpy (_dbmt_error, dbfilepath);
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, 1024, infile))
    {
      sscanf (buf, "%s", dbname);
      sprintf (logfilepath, "%s/%s.err", sco.szCubrid_databases, dbname);
      if ((logfile = fopen (logfilepath, "rt")) != NULL)
	{
	  /* retrieve proper log file's information */
	  nv_add_nvp (res, "dbname", dbname);
	  while (fgets (buf, 1024, logfile))
	    {
	      if (!strncmp (buf, "--->>>", 6))
		{
		  sscanf (buf, "%*s %s %s %s %s %s %s", wday, mon, day, time,
			  tz, year);
		  sprintf (buf, "%s %s %s %s %s %s", wday, mon, day, time, tz,
			   year);
		  nv_add_nvp (res, "time", buf);
		  fgets (buf, 1024, logfile);
		  sscanf (buf, "%*s %*s %*s %*s %*s %*s %s", error_code);
		  error_code[strlen (error_code) - 1] = '\0';
		  nv_add_nvp (res, "error_code", error_code);
		  fgets (buf, 1024, logfile);
		  uRemoveCRLF (buf);
		  nv_add_nvp (res, "desc", buf);
		  fgets (buf, 1024, logfile);
		}
	      else if (!strncmp (buf, "Time:", 5))
		{
#ifdef	WIN32
		  int imon, iday, iyear;
		  sscanf (buf, "%*s %d/%d/%d %s %*s %*s %*s %*s %*s %*s %s",
			  &imon, &iday, &iyear, time, error_code);
		  switch (imon)
		    {
		    case 1:
		      strcpy (mon, "Jan");
		      break;
		    case 2:
		      strcpy (mon, "Feb");
		      break;
		    case 3:
		      strcpy (mon, "Mar");
		      break;
		    case 4:
		      strcpy (mon, "Apr");
		      break;
		    case 5:
		      strcpy (mon, "May");
		      break;
		    case 6:
		      strcpy (mon, "Jun");
		      break;
		    case 7:
		      strcpy (mon, "Jul");
		      break;
		    case 8:
		      strcpy (mon, "Aug");
		      break;
		    case 9:
		      strcpy (mon, "Sep");
		      break;
		    case 10:
		      strcpy (mon, "Oct");
		      break;
		    case 11:
		      strcpy (mon, "Nov");
		      break;
		    case 12:
		      strcpy (mon, "Dec");
		      break;
		    }

		  iyear += 2000;
		  sprintf (day, "%d", iday);
		  sprintf (year, "%d", iyear);

		  if (error_code[strlen (error_code) - 1] == ',')
		    error_code[strlen (error_code) - 1] = '\0';

		  sprintf (buf, "%s %s %s %s %s", "---", mon, day, time,
			   year);
#else
		  sscanf (buf,
			  "%*s %s %s %s %s %s %*s %*s %*s %*s %*s %*s %s",
			  wday, mon, day, time, year, error_code);
		  if (error_code[strlen (error_code) - 1] == ',')
		    error_code[strlen (error_code) - 1] = '\0';

		  sprintf (buf, "%s %s %s %s %s", wday, mon, day, time, year);
#endif
		  nv_add_nvp (res, "time", buf);
		  nv_add_nvp (res, "error_code", error_code);
		  fgets (buf, 1024, logfile);
		  uRemoveCRLF (buf);
		  nv_add_nvp (res, "desc", buf);
		}
	    }			/* end of while(fgets( */
	  fclose (logfile);
	}			/* end of if (logfile = fopen( */
    }				/* end of while(fgets( */
  fclose (infile);

  return ERR_NO_ERROR;
}

int
ts_general_db_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  int ipagesize = 0, itotalpage = 0, ifreepage = 0;
  char *dbname;
  T_SPACEDB_RESULT *cmd_res;
  T_SPACEDB_INFO *info;
  T_CUBRID_MODE cubrid_mode;
  T_DB_SERVICE_MODE mode;
  char strbuf[1024];
  int i;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  mode = uDatabaseMode (dbname);

  if (mode == DB_SERVICE_MODE_SA)
    return ERR_STANDALONE_MODE;

  cubrid_mode =
    (mode == DB_SERVICE_MODE_NONE) ? CUBRID_MODE_SA : CUBRID_MODE_CS;
  cmd_res = cmd_spacedb (dbname, cubrid_mode);

  if (cmd_res == NULL)
    {
      sprintf (_dbmt_error, "spacedb %s", dbname);
      return ERR_SYSTEM_CALL;
    }
  else if (cmd_res->err_msg[0])
    {
      strcpy (_dbmt_error, cmd_res->err_msg);
      cmd_spacedb_result_free (cmd_res);
      return ERR_WITH_MSG;
    }

  nv_add_nvp (res, "dbname", dbname);

  ipagesize = cmd_res->page_size;
  info = cmd_res->vol_info;
  for (i = 0; i < cmd_res->num_vol; i++)
    {
      nv_add_nvp (res, "open", "spaceinfo");
      sprintf (strbuf, "%d", info[i].volid);
      nv_add_nvp (res, "volid", strbuf);
      nv_add_nvp (res, "purpose", info[i].purpose);
      sprintf (strbuf, "%d", info[i].total_page);
      nv_add_nvp (res, "total_pages", strbuf);
      sprintf (strbuf, "%d", info[i].free_page);
      nv_add_nvp (res, "free_pages", strbuf);
      sprintf (strbuf, "%s/%s", info[i].location, info[i].vol_name);
      nv_add_nvp (res, "volname", strbuf);
      nv_add_nvp (res, "close", "spaceinfo");
      itotalpage += info[i].total_page;
      ifreepage += info[i].free_page;
    }
  cmd_spacedb_result_free (cmd_res);

  sprintf (strbuf, "%d", ipagesize);
  nv_add_nvp (res, "page_size", strbuf);
  sprintf (strbuf, "%d", itotalpage);
  nv_add_nvp (res, "total_page", strbuf);
  sprintf (strbuf, "%d", ifreepage);
  nv_add_nvp (res, "free_page", strbuf);

  return ERR_NO_ERROR;
}

int
ts_get_auto_add_vol (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *infile = NULL;
  char *dbname;
  char strbuf[1024];
  char *conf_item[AUTOADDVOL_CONF_ENTRY_NUM];
  int i;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  nv_add_nvp (res, autoaddvol_conf_entry[1], "OFF");
  nv_add_nvp (res, autoaddvol_conf_entry[2], "0.0");
  nv_add_nvp (res, autoaddvol_conf_entry[3], "0");
  nv_add_nvp (res, autoaddvol_conf_entry[4], "OFF");
  nv_add_nvp (res, autoaddvol_conf_entry[5], "0.0");
  nv_add_nvp (res, autoaddvol_conf_entry[6], "0");

  infile = fopen (conf_get_dbmt_file (FID_AUTO_ADDVOLDB_CONF, strbuf), "r");
  if (infile == NULL)
    return ERR_NO_ERROR;

  while (fgets (strbuf, sizeof (strbuf), infile))
    {
      ut_trim (strbuf);
      if (strbuf[0] == '#')
	continue;
      if (string_tokenize (strbuf, conf_item, AUTOADDVOL_CONF_ENTRY_NUM) < 0)
	continue;
      if (strcmp (conf_item[0], dbname) == 0)
	{
	  for (i = 1; i < AUTOADDVOL_CONF_ENTRY_NUM; i++)
	    nv_update_val (res, autoaddvol_conf_entry[i], conf_item[i]);
	  break;
	}
    }
  fclose (infile);

  return ERR_NO_ERROR;
}

int
ts_set_auto_add_vol (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *infile, *outfile;
  char line[1024], tmpfile[512];
  char auto_addvol_conf_file[512];
  char *conf_item[AUTOADDVOL_CONF_ENTRY_NUM];
  int i;

  if ((conf_item[0] = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  for (i = 1; i < AUTOADDVOL_CONF_ENTRY_NUM; i++)
    {
      conf_item[i] = nv_get_val (req, autoaddvol_conf_entry[i]);
      if (conf_item[i] == NULL)
	{
	  strcpy (_dbmt_error, autoaddvol_conf_entry[i]);
	  return ERR_PARAM_MISSING;
	}
    }

  conf_get_dbmt_file (FID_AUTO_ADDVOLDB_CONF, auto_addvol_conf_file);
  if (access (auto_addvol_conf_file, F_OK) < 0)
    {
      outfile = fopen (auto_addvol_conf_file, "w");
      if (outfile == NULL)
	{
	  strcpy (_dbmt_error, auto_addvol_conf_file);
	  return ERR_FILE_OPEN_FAIL;
	}
      for (i = 0; i < AUTOADDVOL_CONF_ENTRY_NUM; i++)
	fprintf (outfile, "%s ", conf_item[i]);
      fprintf (outfile, "\n");
      fclose (outfile);
      return ERR_NO_ERROR;
    }

  infile = fopen (auto_addvol_conf_file, "r");
  if (infile == NULL)
    {
      strcpy (_dbmt_error, auto_addvol_conf_file);
      return ERR_FILE_OPEN_FAIL;
    }
  sprintf (tmpfile, "%s/DBMT_task_045.%d", sco.dbmt_tmp_dir, (int) getpid ());
  outfile = fopen (tmpfile, "w");
  if (outfile == NULL)
    {
      fclose (infile);
      return ERR_TMPFILE_OPEN_FAIL;
    }

  while (fgets (line, sizeof (line), infile))
    {
      char conf_dbname[128];
      if (sscanf (line, "%s", conf_dbname) < 1)
	continue;

      if (strcmp (conf_dbname, conf_item[0]) != 0)
	{
	  fputs (line, outfile);
	}
    }
  for (i = 0; i < AUTOADDVOL_CONF_ENTRY_NUM; i++)
    fprintf (outfile, "%s ", conf_item[i]);
  fprintf (outfile, "\n");
  fclose (infile);
  fclose (outfile);

  move_file (tmpfile, auto_addvol_conf_file);

  return ERR_NO_ERROR;
}

int
ts_get_addvol_status (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname = NULL;
  char dbdir[512];

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  if (uRetrieveDBDirectory (dbname, dbdir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  nv_add_nvp_int (res, "freespace", ut_disk_free_space (dbdir));
  nv_add_nvp (res, "volpath", dbdir);
  return ERR_NO_ERROR;
}

int
ts_get_tran_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *dbpasswd;
  char buf[1024], tmpfile[256];
  FILE *infile;
  char *tok[5];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[10];
  int argc = 0;
  int retval;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }
  dbpasswd = nv_get_val (req, "_DBPASSWD");

  sprintf (tmpfile, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir, TS_GETTRANINFO,
	   (int) getpid ());
  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_KILLTRAN;
  if (dbpasswd != NULL)
    {
      argv[argc++] = "--" KILLTRAN_DBA_PASSWORD_L;
      argv[argc++] = dbpasswd;
    }
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  retval = run_child (argv, 1, NULL, tmpfile, NULL, NULL);	/* killtran */
  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }
  if ((infile = fopen (tmpfile, "rt")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }
  nv_add_nvp (res, "dbname", dbname);
  nv_add_nvp (res, "open", "transactioninfo");
  while (fgets (buf, sizeof (buf), infile))
    {
      if (buf[0] == '-')
	break;
    }
  while (fgets (buf, 1024, infile))
    {
      ut_trim (buf);
      if (buf[0] == '-')
	break;
      if (string_tokenize (buf, tok, 5) < 0)
	continue;
      nv_add_nvp (res, "open", "transaction");
      nv_add_nvp (res, "tranindex", tok[0]);
      nv_add_nvp (res, "user", tok[1]);
      nv_add_nvp (res, "host", tok[2]);
      nv_add_nvp (res, "pid", tok[3]);
      nv_add_nvp (res, "program", tok[4]);
      nv_add_nvp (res, "close", "transaction");
    }
  nv_add_nvp (res, "close", "transactioninfo");
  fclose (infile);
  unlink (tmpfile);

  return ERR_NO_ERROR;
}

int
ts_killtran (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *dbpasswd, *type, *val;
  char param[256];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[10];
  int argc = 0;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    return ERR_PARAM_MISSING;
  dbpasswd = nv_get_val (req, "_DBPASSWD");
  if ((type = nv_get_val (req, "type")) == NULL)
    return ERR_PARAM_MISSING;
  if ((val = nv_get_val (req, "parameter")) == NULL)
    return ERR_PARAM_MISSING;

  strncpy (param, val, sizeof (param) - 1);
  param[sizeof (param) - 1] = '\0';

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_KILLTRAN;
  if (dbpasswd != NULL)
    {
      argv[argc++] = "--" KILLTRAN_DBA_PASSWORD_L;
      argv[argc++] = dbpasswd;
    }
  if (strcmp (type, "t") == 0)
    {
      /* remove (+) from formated string such as "1(+) | 1(-)" */
      char *p = strstr (param, "(");
      if (p != NULL)
	*p = '\0';

      argv[argc++] = "--" KILLTRAN_KILL_TRANSACTION_INDEX_L;
    }
  else if (strcmp (type, "u") == 0)
    {
      argv[argc++] = "--" KILLTRAN_KILL_USER_NAME_L;
    }
  else if (strcmp (type, "h") == 0)
    {
      argv[argc++] = "--" KILLTRAN_KILL_HOST_NAME_L;
    }
  else if (strcmp (type, "pg") == 0)
    {
      argv[argc++] = "--" KILLTRAN_KILL_PROGRAM_NAME_L;
    }

  argv[argc++] = param;
  argv[argc++] = "--" KILLTRAN_FORCE_L;
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* killtran */
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  ts_get_tran_info (req, res, _dbmt_error);
  return ERR_NO_ERROR;
}

int
ts_lockdb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char buf[1024], tmpfile[256], tmpfile2[256], s[32];
  char *dbname;
  FILE *infile, *outfile;
  int kr = 0;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[6];

  dbname = nv_get_val (req, "dbname");
  sprintf (tmpfile, "%s/DBMT_task_%d_1.%d", sco.dbmt_tmp_dir, TS_LOCKDB,
	   (int) getpid ());

  cubrid_cmd_name (cmd_name);
  argv[0] = cmd_name;
  argv[1] = UTIL_OPTION_LOCKDB;
  argv[2] = "--" LOCK_OUTPUT_FILE_L;
  argv[3] = tmpfile;
  argv[4] = dbname;
  argv[5] = NULL;

  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* lockdb */
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  /* create file that remove line feed at existed outputfile */
  sprintf (tmpfile2, "%s/DBMT_task_%d_2.%d", sco.dbmt_tmp_dir, TS_LOCKDB,
	   (int) getpid ());
  infile = fopen (tmpfile, "rt");
  outfile = fopen (tmpfile2, "w");
  while (fgets (buf, 1024, infile))
    {
      if (sscanf (buf, "%s", s) == 1)
	fputs (buf, outfile);
      if (kr == 0 && !strcmp (s, "¶ô"))
	kr = 1;
    }
  fclose (infile);
  fclose (outfile);
  unlink (tmpfile);
  if ((infile = fopen (tmpfile2, "rt")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  /* Make version information */
  if (kr)
    {
      if (_ts_lockdb_parse_kr (res, infile) < 0)
	{
	  /* parshing error */
	  strcpy (_dbmt_error,
		  "Lockdb operation has been failed(Unexpected state).");
	  fclose (infile);
	  unlink (tmpfile2);
	  return ERR_WITH_MSG;
	}
    }
  else
    {
      if (_ts_lockdb_parse_us (res, infile) < 0)
	{
	  /* parshing error */
	  strcpy (_dbmt_error,
		  "Lockdb operation has been failed(Unexpected state).");
	  fclose (infile);
	  unlink (tmpfile2);
	  return ERR_WITH_MSG;
	}
    }

  fclose (infile);
  unlink (tmpfile2);

  return ERR_NO_ERROR;
}

int
ts_get_backup_list (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char buf[1024], f[256], s1[256], s2[256], *dbname, log_dir[512];
  FILE *infile;
  int lv = -1;

  dbname = nv_get_val (req, "dbname");

  if (uRetrieveDBLogDirectory (dbname, log_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  sprintf (f, "%s/%s%s", log_dir, dbname, CUBRID_BACKUP_INFO_EXT);
  if ((infile = fopen (f, "rt")) != NULL)
    {
      while (fgets (buf, 1024, infile))
	{
	  sscanf (buf, "%s %*s %s", s1, s2);
	  lv = atoi (s1);
	  sprintf (buf, "level%d", lv);
	  nv_add_nvp (res, buf, s2);
	}
      fclose (infile);
    }
  for (lv++; lv <= 2; lv++)
    {
      sprintf (buf, "level%d", lv);
      nv_add_nvp (res, buf, "none");
    }

  return ERR_NO_ERROR;
}

int
ts_load_access_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char buf[1024], time[256];
  FILE *infile;
  char *tok[5];

  conf_get_dbmt_file (FID_FSERVER_ACCESS_LOG, buf);
  nv_add_nvp (res, "open", "accesslog");
  if ((infile = fopen (buf, "rt")) != NULL)
    {
      while (fgets (buf, sizeof (buf), infile) != NULL)
	{
	  ut_trim (buf);
	  if (string_tokenize (buf, tok, 5) < 0)
	    continue;
	  nv_add_nvp (res, "user", tok[2]);
	  nv_add_nvp (res, "taskname", tok[4]);
	  sprintf (time, "%s %s", tok[0], tok[1]);
	  nv_add_nvp (res, "time", time);
	}
      fclose (infile);
    }

  nv_add_nvp (res, "close", "accesslog");
  conf_get_dbmt_file (FID_FSERVER_ERROR_LOG, buf);
  nv_add_nvp (res, "open", "errorlog");
  if ((infile = fopen (buf, "rt")) != NULL)
    {
      while (fgets (buf, sizeof (buf), infile) != NULL)
	{
	  ut_trim (buf);
	  if (string_tokenize (buf, tok, 5) < 0)
	    continue;
	  nv_add_nvp (res, "user", tok[2]);
	  nv_add_nvp (res, "taskname", tok[4]);
	  sprintf (time, "%s %s", tok[0], tok[1]);
	  nv_add_nvp (res, "time", time);
	  nv_add_nvp (res, "errornote", tok[4] + strlen (tok[4]) + 1);
	}
      fclose (infile);
    }
  nv_add_nvp (res, "close", "errorlog");
  return ERR_NO_ERROR;
}

int
ts_delete_access_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char buf[1024];

  conf_get_dbmt_file (FID_FSERVER_ACCESS_LOG, buf);
  unlink (buf);

  return ERR_NO_ERROR;
}

int
tsGetAutoaddvolLog (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *infile;
  char strbuf[1024];
  char dbname[512];
  char volname[512];
  char purpose[512];
  char page[512];
  char time[512];
  char outcome[512];

  infile = fopen (conf_get_dbmt_file (FID_AUTO_ADDVOLDB_LOG, strbuf), "r");
  if (infile != NULL)
    {
      while (fgets (strbuf, 1024, infile))
	{
	  uRemoveCRLF (strbuf);
	  sscanf (strbuf, "%s %s %s %s %s %s", dbname, volname, purpose, page,
		  time, outcome);
	  nv_add_nvp (res, "open", "log");
	  nv_add_nvp (res, "dbname", dbname);
	  nv_add_nvp (res, "volname", volname);
	  nv_add_nvp (res, "purpose", purpose);
	  nv_add_nvp (res, "page", page);
	  nv_add_nvp (res, "time", time);
	  nv_add_nvp (res, "outcome", outcome);
	  nv_add_nvp (res, "close", "log");
	}
      fclose (infile);
    }
  return ERR_NO_ERROR;
}

int
ts_delete_error_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char buf[1024];

  conf_get_dbmt_file (FID_FSERVER_ERROR_LOG, buf);
  unlink (buf);

  return ERR_NO_ERROR;
}

int
ts_check_dir (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *n, *v;
  int i;

  for (i = 0; i < req->nvplist_leng; i++)
    {
      nv_lookup (req, i, &n, &v);
      if (!strcmp (n, "dir"))
	{
	  if (access (v, F_OK) < 0)
	    nv_add_nvp (res, "noexist", v);
	}
    }

  return ERR_NO_ERROR;
}

int
ts_check_file (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *n, *v;
  int i;

  for (i = 0; i < req->nvplist_leng; i++)
    {
      nv_lookup (req, i, &n, &v);
      if (strcmp (n, "file") == 0)
	{
	  if (access (v, F_OK) == 0)
	    nv_add_nvp (res, "existfile", v);
	}
    }

  return ERR_NO_ERROR;
}

int
ts_get_autobackupdb_error_log (nvplist * req, nvplist * res,
			       char *_dbmt_error)
{
  char buf[1024], logfile[256], s1[256], s2[256], time[32], dbname[256],
    backupid[256];
  FILE *infile;

  sprintf (logfile, "%s/log/manager/auto_backupdb.log", sco.szCubrid);
  if ((infile = fopen (logfile, "r")) == NULL)
    {
      return ERR_NO_ERROR;
    }

  while (fgets (buf, 1024, infile))
    {
      if (sscanf (buf, "%s %s", s1, s2) != 2)
	continue;
      if (!strncmp (s1, "DATE:", 5))
	{
	  sprintf (time, "%s %s", s1 + 5, s2 + 5);
	  if (fgets (buf, 1024, infile) == NULL)
	    break;
	  sscanf (buf, "%s %s", s1, s2);
	  sprintf (dbname, "%s", s1 + 7);
	  sprintf (backupid, "%s", s2 + 9);
	  if (fgets (buf, 1024, infile) == NULL)
	    break;
	  uRemoveCRLF (buf);
	  nv_add_nvp (res, "open", "error");
	  nv_add_nvp (res, "dbname", dbname);
	  nv_add_nvp (res, "backupid", backupid);
	  nv_add_nvp (res, "error_time", time);
	  nv_add_nvp (res, "error_desc", buf + 3);
	  nv_add_nvp (res, "close", "error");
	}
    }
  fclose (infile);

  return ERR_NO_ERROR;
}

int
ts_get_autoexecquery_error_log (nvplist * req, nvplist * res,
				char *_dbmt_error)
{
  char buf[1024], logfile[256], s1[256], s2[256], s3[256], s4[256], time[32],
    dbname[256], username[256], query_id[64], error_code[64];
  FILE *infile;

  sprintf (logfile, "%s/log/manager/auto_execquery.log", sco.szCubrid);
  if ((infile = fopen (logfile, "r")) == NULL)
    return ERR_NO_ERROR;

  while (fgets (buf, sizeof (buf), infile))
    {
      if (sscanf (buf, "%s %s", s1, s2) != 2)
	continue;
      if (!strncmp (s1, "DATE:", 5))
	{
	  sprintf (time, "%s %s", s1 + 5, s2 + 5);	/* 5 = strlen("DATE:"); 5 = strlen("TIME:"); */
	  if (fgets (buf, sizeof (buf), infile) == NULL)
	    break;

	  s3[0] = 0;
	  sscanf (buf, "%s %s %s %s", s1, s2, s3, s4);
	  sprintf (dbname, "%s", s1 + 7);	/* 7 = strlen("DBNAME:") */
	  sprintf (username, "%s", s2 + 14);	/* 14 = strlen("EMGR-USERNAME:") */
	  sprintf (query_id, "%s", s3 + 9);	/* 9 = strlen("QUERY-ID:") */
	  sprintf (error_code, "%s", s4 + 11);	/* 11 = strlen("ERROR-CODE:") */
	  if (fgets (buf, sizeof (buf), infile) == NULL)
	    break;

	  uRemoveCRLF (buf);
	  nv_add_nvp (res, "open", "error");
	  nv_add_nvp (res, "dbname", dbname);
	  nv_add_nvp (res, "username", username);
	  nv_add_nvp (res, "query_id", query_id);
	  nv_add_nvp (res, "error_time", time);
	  nv_add_nvp (res, "error_code", error_code);
	  nv_add_nvp (res, "error_desc", buf + 3);
	  nv_add_nvp (res, "close", "error");
	}
    }

  return ERR_NO_ERROR;
}

/***************************************************************************
	 FSERVER TASK PRIVATE
  *************************************************************************/
static int
op_db_user_pass_check (char *dbname, char *dbuser, char *dbpass)
{
  char decrypted[PASSWD_LENGTH + 1];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[11];
  char input_file[200];
  char *cubrid_err_file;
  int retval, argc;

  T_DB_SERVICE_MODE db_mode;

  if (dbuser == NULL)
    return 0;


  sprintf (input_file, "%s/DBMT_task_%d_%d", sco.dbmt_tmp_dir,
	   TS_GETACCESSRIGHT, (int) getpid ());

  cmd_name[0] = '\0';
  sprintf (cmd_name, "%s/%s%s", sco.szCubrid, CUBRID_DIR_BIN, UTIL_CSQL_NAME);
  argc = 0;
  argv[argc++] = cmd_name;
  argv[argc++] = dbname;
  argv[argc++] = NULL;		/* for "-cs" or "-sa" argument */
  argv[argc++] = "--" CSQL_INPUT_FILE_L;
  argv[argc++] = input_file;
  argv[argc++] = "--" CSQL_USER_L;
  argv[argc++] = dbuser;

  uDecrypt (PASSWD_LENGTH, dbpass, decrypted);
  if (*decrypted != '\0')
    {
      argv[argc++] = "--" CSQL_PASSWORD_L;
      argv[argc++] = decrypted;
    }

  for (; argc < 11; argc++)
    argv[argc] = NULL;

  db_mode = uDatabaseMode (dbname);

  if (db_mode == DB_SERVICE_MODE_CS)
    {
      argv[2] = "--" CSQL_CS_MODE_L;
    }
  else if (db_mode == DB_SERVICE_MODE_NONE)
    {
      argv[2] = "--" CSQL_SA_MODE_L;
    }
  else if (db_mode == DB_SERVICE_MODE_SA)
    {
      return 2;
      /* data base is running in stand alone mode */
    }

  op_make_password_check_file (input_file);

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);
  SET_TRANSACTION_NO_WAIT_MODE_ENV ();

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* csql */

  unlink (input_file);
  if (read_csql_error_file (cubrid_err_file, NULL, DBMT_ERROR_MSG_SIZE) < 0)
    {
      return 0;
    }

  if (retval < 0)
    {
      return 0;
    }

  return 1;
}

static int
_op_db_login (nvplist * out, nvplist * in, char *_dbmt_error)
{
  int errcode;
  char *id, *pwd, *db_name;

  id = nv_get_val (in, "_DBID");
  pwd = nv_get_val (in, "_DBPASSWD");
  db_name = nv_get_val (in, "_DBNAME");

  db_login (id, pwd);
  errcode = db_restart (DB_RESTART_SERVER_NAME, 0, db_name);
  if (errcode < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      return -1;
    }

  db_set_isolation (TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE);
  db_set_lock_timeout (1);

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
  char *result = (char *) malloc (256), *temp;

  type_id = db_domain_type (domain);
  if (type_id == 0)
    return NULL;
  else if (type_id == DB_TYPE_OBJECT)
    {
      class_ = db_domain_class (domain);
      if (class_ != NULL)
	strcpy (result, db_get_class_name (class_));
      else
	strcpy (result, "");
    }
  else if (type_id == DB_TYPE_NUMERIC)
    {
      int s;
      p = db_domain_precision (domain);
      s = db_domain_scale (domain);
      sprintf (result, "%s(%d,%d)", db_get_type_name (type_id), p, s);
    }
  else if ((p = db_domain_precision (domain)) != 0)
    {
      sprintf (result, "%s(%d)", db_get_type_name (type_id), p);
    }
  else if (type_id == DB_TYPE_SET || type_id == DB_TYPE_MULTISET ||
	   type_id == DB_TYPE_LIST || type_id == DB_TYPE_SEQUENCE)
    {
      set_domain = db_domain_set (domain);
      strcpy (result, db_get_type_name (type_id));
      if (domain != NULL)
	{
	  strcat (result, "_of(");
	  temp = _op_get_type_name (set_domain);
	  if (temp != NULL)
	    {
	      strcat (result, temp);
	      free (temp);
	      for (set_domain = db_domain_next (set_domain);
		   set_domain != NULL;
		   set_domain = db_domain_next (set_domain))
		{
		  strcat (result, ",");
		  temp = _op_get_type_name (set_domain);
		  strcat (result, temp);
		  free (temp);
		}
	    }
	  strcat (result, ")");
	}
    }
  else
    {
      strcpy (result, db_get_type_name (type_id));
    }

  return result;
}

static void
_op_get_attribute_info (nvplist * out, DB_ATTRIBUTE * attr, int isclass)
{
  char *type_name, *v_str;
  DB_OBJECT *superobj;

  if (isclass)
    nv_add_nvp (out, "open", "classattribute");
  else
    nv_add_nvp (out, "open", "attribute");
  nv_add_nvp (out, "name", db_attribute_name (attr));
  type_name = _op_get_type_name (db_attribute_domain (attr));
  nv_add_nvp (out, "type", type_name);
  free (type_name);

  superobj = db_attribute_class (attr);
  if (superobj != NULL)
    nv_add_nvp (out, "inherit", db_get_class_name (superobj));
  else
    nv_add_nvp (out, "inherit", "");

  if (db_attribute_is_indexed (attr))
    nv_add_nvp (out, "indexed", "y");
  else
    nv_add_nvp (out, "indexed", "n");
  if (db_attribute_is_non_null (attr))
    nv_add_nvp (out, "notnull", "y");
  else
    nv_add_nvp (out, "notnull", "n");
  if (db_attribute_is_shared (attr))
    nv_add_nvp (out, "shared", "y");
  else
    nv_add_nvp (out, "shared", "n");
  if (db_attribute_is_unique (attr))
    nv_add_nvp (out, "unique", "y");
  else
    nv_add_nvp (out, "unique", "n");

  v_str = _op_get_value_string (db_attribute_default (attr));
  nv_add_nvp (out, "default", v_str);
  free (v_str);
  if (isclass)
    nv_add_nvp (out, "close", "classattribute");
  else
    nv_add_nvp (out, "close", "attribute");
}

static char *
_op_get_set_value (DB_VALUE * val)
{
  char *result, *return_result;
  DB_TYPE type;
  int result_size = 255;
  result = (char *) malloc (result_size + 1);
  if (result == NULL)
    return NULL;

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
      snprintf (result, result_size, "%s%s%s", "timestamp '", return_result,
		"'");
      break;

    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      snprintf (result, result_size, "%s%s%s", "N'", return_result, "'");
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      snprintf (result, result_size, "%s%s%s", "X'", return_result, "'");
      break;

    case DB_TYPE_ELO:
      snprintf (result, result_size, "%s%s%s", "ELO'", return_result, "'");
      break;

    default:
      snprintf (result, result_size, "%s", return_result);
      break;

    }
  free (return_result);
  result[result_size] = '\0';
  return result;
}

static char *
_op_get_value_string (DB_VALUE * value)
{
  char *result, *return_result;
  DB_TYPE type;
  DB_DATE *date_v;
  DB_TIME *time_v;
  DB_TIMESTAMP *timestamp_v;
  DB_MONETARY *money;
  DB_SET *set;
  DB_VALUE val;
  int size, i, idx, result_size = 255;
  double dv;
  float fv;
  int iv;
  short sv;
  extern char *numeric_db_value_print (DB_VALUE *);

  result = (char *) malloc (result_size + 1);
  if (result == NULL)
    return NULL;
  memset (result, 0, result_size + 1);
  type = db_value_type (value);

  switch (type)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
      strncpy (result, db_get_string (value), result_size);
      break;
    case DB_TYPE_DOUBLE:
      dv = db_get_double (value);
      sprintf (result, "%f", dv);
      break;
    case DB_TYPE_FLOAT:
      fv = db_get_float (value);
      sprintf (result, "%f", fv);
      break;
    case DB_TYPE_NUMERIC:
      sprintf (result, "%s", numeric_db_value_print ((DB_VALUE *) value));
      break;
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      set = db_get_set (value);
      if (set != NULL)
	{
	  idx = sprintf (result, "%s", "{");
	  size = set_size (set);
	  for (i = 0; i < size && idx < result_size; i++)
	    {
	      set_get_element (set, i, &val);
	      return_result = _op_get_set_value (&val);
	      if (return_result == NULL)
		goto exit_on_end;
	      if (i == 0)
		idx +=
		  snprintf (result + idx, result_size - idx, "%s",
			    return_result);
	      else
		idx +=
		  snprintf (result + idx, result_size - idx, ",%s",
			    return_result);
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
	sprintf (result, "%.2f", money->amount);
      break;
    case DB_TYPE_INTEGER:
      iv = db_get_int (value);
      sprintf (result, "%d", iv);
      break;
    case DB_TYPE_SHORT:
      sv = db_get_short (value);
      sprintf (result, "%d", sv);
      break;
    case DB_TYPE_DATE:
      date_v = db_get_date (value);
      db_date_to_string (result, 256, date_v);
      break;
    case DB_TYPE_TIME:
      time_v = db_get_time (value);
      db_time_to_string (result, 256, time_v);
      break;
    case DB_TYPE_TIMESTAMP:
      timestamp_v = db_get_timestamp (value);
      db_timestamp_to_string (result, 256, timestamp_v);
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
}

static void
_op_get_method_info (nvplist * out, DB_METHOD * method, int isclass)
{
  int i, cnt;
  char *type_name;
  DB_DOMAIN *d;
  DB_OBJECT *superobj;

  if (isclass)
    nv_add_nvp (out, "open", "classmethod");
  else
    nv_add_nvp (out, "open", "method");
  nv_add_nvp (out, "name", db_method_name (method));
  superobj = db_method_class (method);
  if (superobj != NULL)
    nv_add_nvp (out, "inherit", db_get_class_name (superobj));
  else
    nv_add_nvp (out, "inherit", "");
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
    nv_add_nvp (out, "close", "classmethod");
  else
    nv_add_nvp (out, "close", "method");
}

static void
_op_get_method_file_info (nvplist * out, DB_METHFILE * mfile)
{
  nv_add_nvp (out, "methodfile", db_methfile_name (mfile));
}

/* resolution information retrieve */
static void
_op_get_resolution_info (nvplist * out, DB_RESOLUTION * res, int isclass)
{
  if (isclass)
    nv_add_nvp (out, "open", "classresolution");
  else
    nv_add_nvp (out, "open", "resolution");
  nv_add_nvp (out, "name", db_resolution_name (res));
  nv_add_nvp (out, "classname",
	      db_get_class_name (db_resolution_class (res)));
  nv_add_nvp (out, "alias", db_resolution_alias (res));
  if (isclass)
    nv_add_nvp (out, "close", "classresolution");
  else
    nv_add_nvp (out, "close", "resolution");
}

/* constraint information retrieve */
static void
_op_get_constraint_info (nvplist * out, DB_CONSTRAINT * con)
{
  char *classname;
  char query[1024], attr_name[128], order[10];
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
	nv_add_nvp (out, "classattribute", db_attribute_name (attr[i]));
      else
	nv_add_nvp (out, "attribute", db_attribute_name (attr[i]));
    }

  if ((con_type == DB_CONSTRAINT_UNIQUE) || (con_type == DB_CONSTRAINT_INDEX))
    {
      char buf[256];

      sprintf (query,
	       "select key_attr_name, asc_desc from db_index_key where class_name='%s' and index_name='%s' order by key_order asc",
	       classname, db_constraint_name (con));

      db_execute (query, &result, &query_error);
      end = db_query_first_tuple (result);

      while (end == 0)
	{
	  db_query_get_tuple_value (result, 0, &val);
	  snprintf (attr_name, 127, "%s", db_get_string (&val));
	  attr_name[127] = '\0';
	  db_value_clear (&val);

	  db_query_get_tuple_value (result, 1, &val);
	  strcpy (order, db_get_string (&val));
	  db_value_clear (&val);

	  snprintf (buf, 255, "%s %s", attr_name, order);
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
      const char *cache_attr;

      ref_cls = db_get_foreign_key_ref_class (con);
      snprintf (buf, 255, "REFERENCES %s", db_get_class_name (ref_cls));
      buf[255] = '\0';
      nv_add_nvp (out, "rule", buf);

      snprintf (buf, 255, "ON DELETE %s",
		db_get_foreign_key_action (con, DB_FK_DELETE));
      buf[255] = '\0';
      nv_add_nvp (out, "rule", buf);

      snprintf (buf, 255, "ON UPDATE %s",
		db_get_foreign_key_action (con, DB_FK_UPDATE));
      buf[255] = '\0';
      nv_add_nvp (out, "rule", buf);

      cache_attr = db_get_foreign_key_cache_object_attr (con);

      if (cache_attr)
	{
	  snprintf (buf, 255, "ON CACHE OBJECT %s", cache_attr);
	  buf[255] = '\0';
	  nv_add_nvp (out, "rule", buf);
	}
    }

  nv_add_nvp (out, "close", "constraint");
}

/* vclass's query spec information retrieve */
static void
_op_get_query_spec_info (nvplist * out, DB_QUERY_SPEC * spec)
{
  nv_add_nvp (out, "queryspec", db_query_spec_string (spec));
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
    nv_add_nvp (out, "owner", "");
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

static int
_op_get_detailed_class_info (nvplist * out, DB_OBJECT * classobj,
			     char *_dbmt_error)
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

  nv_add_nvp (out, "open", "classinfo");
  nv_add_nvp (out, "dbname", db_get_database_name ());
  class_name = (char *) db_get_class_name (classobj);
  if (class_name == NULL)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      return -1;
    }
  nv_add_nvp (out, "classname", class_name);
  if (db_is_system_class (classobj))
    nv_add_nvp (out, "type", "system");
  else
    nv_add_nvp (out, "type", "user");
  obj = db_get_owner (classobj);
  if (db_get (obj, "name", &v) < 0)
    nv_add_nvp (out, "owner", "");
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
    nv_add_nvp (out, "virtual", "view");
  else
    nv_add_nvp (out, "virtual", "normal");

  /* class_ attributes info */
  for (attr = db_get_class_attributes (classobj); attr != NULL;
       attr = db_attribute_next (attr))
    {
      _op_get_attribute_info (out, attr, 1);
    }

  /* attributes info */
  for (attr = db_get_attributes (classobj); attr != NULL;
       attr = db_attribute_next (attr))
    {
      _op_get_attribute_info (out, attr, 0);
    }

  /* class_ methods */
  for (method = db_get_class_methods (classobj); method != NULL;
       method = db_method_next (method))
    {
      _op_get_method_info (out, method, 1);
    }

  /* methods */
  for (method = db_get_methods (classobj); method != NULL;
       method = db_method_next (method))
    {
      _op_get_method_info (out, method, 0);
    }

  /* method files */
  for (mfile = db_get_method_files (classobj); mfile != NULL;
       mfile = db_methfile_next (mfile))
    {
      _op_get_method_file_info (out, mfile);
    }

  /* class_ resolutions */
  for (res = db_get_class_resolutions (classobj); res != NULL;
       res = db_resolution_next (res))
    {
      _op_get_resolution_info (out, res, 1);
    }

  /* resolutions */
  for (res = db_get_resolutions (classobj); res != NULL;
       res = db_resolution_next (res))
    {
      _op_get_resolution_info (out, res, 0);
    }

  /* constraints */
  for (con = db_get_constraints (classobj); con != NULL;
       con = db_constraint_next (con))
    {
      _op_get_constraint_info (out, con);
    }

  /* query specs */
  for (spec = db_get_query_specs (classobj); spec != NULL;
       spec = db_query_spec_next (spec))
    {
      _op_get_query_spec_info (out, spec);
    }

  nv_add_nvp (out, "close", "classinfo");
  return 0;
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

static void
_op_get_db_user_name (nvplist * res, DB_OBJECT * user)
{
  DB_VALUE v;

  db_get (user, "name", &v);
  nv_add_nvp (res, "name", db_get_string (&v));
  db_value_clear (&v);
}

static void
_op_get_db_user_id (nvplist * res, DB_OBJECT * user)
{
  DB_VALUE v;
  char buf[20];

  db_get (user, "password", &v);
  sprintf (buf, "%d", db_get_int (&v));
  nv_add_nvp (res, "id", buf);
  db_value_clear (&v);
}

static void
_op_get_db_user_password (nvplist * res, DB_OBJECT * user)
{
  DB_VALUE v;
  DB_OBJECT *obj;

  db_get (user, "password", &v);
  if (!db_value_is_null (&v))
    {
      obj = db_get_object (&v);
      db_value_clear (&v);
      db_get (obj, "password", &v);
      if (!db_value_is_null (&v))
	{
	  nv_add_nvp (res, "password", db_get_string (&v));
	  db_value_clear (&v);
	}
      else
	{
	  nv_add_nvp (res, "password", "");
	}
    }
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
      sprintf (buf, "%d", db_get_int (&v));
      nv_add_nvp (res, (char *) db_get_class_name (obj), buf);
    }
}

static void
_op_get_super_info (nvplist * out, DB_OBJECT * classobj)
{
  DB_OBJLIST *objlist, *temp;
  DB_OBJECT *obj;
  DB_ATTRIBUTE *attr;
  DB_METHOD *method;

  nv_add_nvp (out, "open", "class");
  nv_add_nvp (out, "name", (char *) db_get_class_name (classobj));
  /* class_ attributes info */
  for (attr = db_get_class_attributes (classobj); attr != NULL;
       attr = db_attribute_next (attr))
    {
      nv_add_nvp (out, "classattribute", db_attribute_name (attr));
    }
  /* attributes info */
  for (attr = db_get_attributes (classobj); attr != NULL;
       attr = db_attribute_next (attr))
    {
      nv_add_nvp (out, "attribute", db_attribute_name (attr));
    }
  /* class_ methods */
  for (method = db_get_class_methods (classobj); method != NULL;
       method = db_method_next (method))
    {
      nv_add_nvp (out, "classmethod", db_method_name (method));
    }
  /* methods */
  for (method = db_get_methods (classobj); method != NULL;
       method = db_method_next (method))
    {
      nv_add_nvp (out, "method", db_method_name (method));
    }
  nv_add_nvp (out, "close", "class");

  objlist = db_get_superclasses (classobj);
  for (temp = objlist; temp != NULL; temp = temp->next)
    {
      obj = temp->op;
      _op_get_super_info (out, obj);
    }
  db_objlist_free (objlist);
}

/* return 1 if argument's attribute is classattribute. or return 0 */
static int
_op_is_classattribute (DB_ATTRIBUTE * attr, DB_OBJECT * classobj)
{
  DB_ATTRIBUTE *temp;
  int id = db_attribute_id (attr);
  for (temp = db_get_class_attributes (classobj); temp != NULL;
       temp = db_attribute_next (temp))
    {
      if (id == db_attribute_id (temp))
	return 1;
    }
  return 0;
}

/* Read dbmt password file and append user information to nvplist */
static void
_tsAppendDBMTUserList (nvplist * res, T_DBMT_USER * dbmt_user,
		       char *_dbmt_error)
{
  char decrypted[PASSWD_LENGTH + 1];
  char *unicas_auth, *dbcreate = NULL;
  int i, j;

  nv_add_nvp (res, "open", "userlist");
  for (i = 0; i < dbmt_user->num_dbmt_user; i++)
    {
      if (dbmt_user->user_info[i].user_name[0] == '\0')
	continue;
      nv_add_nvp (res, "open", "user");
      nv_add_nvp (res, "id", dbmt_user->user_info[i].user_name);
      uDecrypt (PASSWD_LENGTH, dbmt_user->user_info[i].user_passwd,
		decrypted);
      nv_add_nvp (res, "passwd", decrypted);
      nv_add_nvp (res, "open", "dbauth");
      unicas_auth = NULL;
      for (j = 0; j < dbmt_user->user_info[i].num_dbinfo; j++)
	{
	  if (dbmt_user->user_info[i].dbinfo[j].dbname[0] == '\0')
	    {
	      continue;
	    }
	  if (strcmp (dbmt_user->user_info[i].dbinfo[j].dbname, "unicas") ==
	      0)
	    {
	      unicas_auth = dbmt_user->user_info[i].dbinfo[j].auth;
	      continue;
	    }
	  else
	    if (strcmp (dbmt_user->user_info[i].dbinfo[j].dbname, "dbcreate")
		== 0)
	    {
	      dbcreate = dbmt_user->user_info[i].dbinfo[j].auth;
	      continue;
	    }
	  nv_add_nvp (res, "dbname",
		      dbmt_user->user_info[i].dbinfo[j].dbname);
	  nv_add_nvp (res, "dbid", dbmt_user->user_info[i].dbinfo[j].uid);
#if 0
	  uDecrypt (PASSWD_LENGTH, dbmt_user->user_info[i].dbinfo[j].passwd,
		    decrypted);
	  nv_add_nvp (res, "dbpasswd", decrypted);
#endif
	  nv_add_nvp (res, "dbpasswd", "");
	}
      nv_add_nvp (res, "close", "dbauth");
      if (unicas_auth == NULL)
	unicas_auth = "none";
      nv_add_nvp (res, "casauth", unicas_auth);
      if (CLIENT_VERSION >= EMGR_MAKE_VER (7, 0))
	{
	  if (dbcreate == NULL)
	    dbcreate = "none";
	  nv_add_nvp (res, "dbcreate", dbcreate);
	}
      nv_add_nvp (res, "close", "user");
    }
  nv_add_nvp (res, "close", "userlist");
}

static int
_tsAppendDBList (nvplist * res, char dbdir_flag)
{
  FILE *infile;
  char *dbinfo[4];
  char strbuf[1024];
  char hname[128];
  struct hostent *hp;
  unsigned char ip_addr[4];

  sprintf (strbuf, "%s/%s", sco.szCubrid_databases, CUBRID_DATABASE_TXT);
  if ((infile = fopen (strbuf, "rt")) == NULL)
    {
      return ERR_DATABASETXT_OPEN;
    }

  memset (hname, 0, sizeof (hname));
  gethostname (hname, sizeof (hname));
  if ((hp = gethostbyname (hname)) == NULL)
    return ERR_NO_ERROR;
  memcpy (ip_addr, hp->h_addr_list[0], 4);

  nv_add_nvp (res, "open", "dblist");
  while (fgets (strbuf, sizeof (strbuf), infile))
    {
      if (string_tokenize (strbuf, dbinfo, 4) < 0)
	continue;
      if ((hp = gethostbyname (dbinfo[2])) == NULL)
	continue;
      /*if the ip equals 127.0.0.1 */
      if (*(int *) (hp->h_addr_list[0]) == htonl (0x7f000001) ||
	  memcmp (ip_addr, hp->h_addr_list[0], 4) == 0)
	{
	  nv_add_nvp (res, "dbname", dbinfo[0]);

	  if (dbdir_flag)
	    nv_add_nvp (res, "dbdir", dbinfo[1]);
	}
    }
  nv_add_nvp (res, "close", "dblist");
  fclose (infile);
  return ERR_NO_ERROR;
}

static int
_tsParseSpacedb (nvplist * req, nvplist * res, char *dbname,
		 char *_dbmt_error, T_SPACEDB_RESULT * cmd_res)
{
  int pagesize, i;
  T_SPACEDB_INFO *vol_info;
  char dbdir[512];
#ifdef WIN32
  WIN32_FIND_DATA data;
  char find_file[512];
  HANDLE handle;
  int found;
#else
  DIR *dirp = NULL;
  struct dirent *dp = NULL;
#endif

  pagesize = cmd_res->page_size;
  nv_update_val_int (res, "pagesize", pagesize);

  vol_info = cmd_res->vol_info;
  for (i = 0; i < cmd_res->num_vol; i++)
    {
      nv_add_nvp (res, "open", "spaceinfo");
      nv_add_nvp (res, "spacename", vol_info[i].vol_name);
      nv_add_nvp (res, "type", vol_info[i].purpose);
      nv_add_nvp (res, "location", vol_info[i].location);
      nv_add_nvp_int (res, "totalpage", vol_info[i].total_page);
      nv_add_nvp_int (res, "freepage", vol_info[i].free_page);
      nv_add_nvp_time (res, "date", vol_info[i].date, "%04d%02d%02d",
		       NV_ADD_DATE);
      nv_add_nvp (res, "close", "spaceinfo");
    }

  vol_info = cmd_res->tmp_vol_info;
  for (i = 0; i < cmd_res->num_tmp_vol; i++)
    {
      nv_add_nvp (res, "open", "spaceinfo");
      nv_add_nvp (res, "spacename", vol_info[i].vol_name);
      nv_add_nvp (res, "type", vol_info[i].purpose);
      nv_add_nvp (res, "location", vol_info[i].location);
      nv_add_nvp_int (res, "totalpage", vol_info[i].total_page);
      nv_add_nvp_int (res, "freepage", vol_info[i].free_page);
      nv_add_nvp_time (res, "date", vol_info[i].date, "%04d%02d%02d",
		       NV_ADD_DATE);
      nv_add_nvp (res, "close", "spaceinfo");
    }

  if (uRetrieveDBLogDirectory (dbname, dbdir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  /* read entries in the directory and generate result */
#ifdef WIN32
  sprintf (find_file, "%s/*", dbdir);
  if ((handle = FindFirstFile (find_file, &data)) == INVALID_HANDLE_VALUE)
#else
  if ((dirp = opendir (dbdir)) == NULL)
#endif
    {
      sprintf (_dbmt_error, "%s", dbdir);
      return ERR_DIROPENFAIL;
    }

#ifdef WIN32
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dp = readdir (dirp)) != NULL)
#endif
    {
      int baselen;
      char *fname;

#ifdef WIN32
      fname = data.cFileName;
#else
      fname = dp->d_name;
#endif
      baselen = strlen (dbname);

      if (strncmp (fname, dbname, baselen) == 0)
	{
	  if (!strcmp (fname + baselen, CUBRID_ACT_LOG_EXT))
	    _ts_gen_spaceinfo (res, fname, dbdir, "Active_log", pagesize);
	  else
	    if (!strncmp
		(fname + baselen, CUBRID_ARC_LOG_EXT, CUBRID_ARC_LOG_EXT_LEN))
	    _ts_gen_spaceinfo (res, fname, dbdir, "Archive_log", pagesize);

#if 0
	  else if (strncmp (fname + baselen, "_lginf", 6) == 0)
	    _ts_gen_spaceinfo (res, fname, dbdir, "Generic_log", pagesize);
#endif
	}
    }
#ifdef WIN32
  FindClose (handle);
#else
  closedir (dirp);
#endif

  /* add last line */
  nv_add_nvp (res, "open", "spaceinfo");
  nv_add_nvp (res, "spacename", "Total");
  nv_add_nvp (res, "type", "");
  nv_add_nvp (res, "location", "");
  nv_add_nvp (res, "totlapage", "0");
  nv_add_nvp (res, "freepage", "0");
  nv_add_nvp (res, "date", "");
  nv_add_nvp (res, "close", "spaceinfo");

  if (uRetrieveDBDirectory (dbname, dbdir) == ERR_NO_ERROR)
    nv_add_nvp_int (res, "freespace", ut_disk_free_space (dbdir));
  else
    nv_add_nvp_int (res, "freespace", -1);

  return ERR_NO_ERROR;
}

static void
_ts_gen_spaceinfo (nvplist * res, char *filename, char *dbinstalldir,
		   char *type, int pagesize)
{
  char volfile[512];
  struct stat statbuf;

  nv_add_nvp (res, "open", "spaceinfo");
  nv_add_nvp (res, "spacename", filename);
  nv_add_nvp (res, "type", type);
  nv_add_nvp (res, "location", dbinstalldir);

  sprintf (volfile, "%s/%s", dbinstalldir, filename);
  stat (volfile, &statbuf);

  nv_add_nvp_int (res, "totalpage", statbuf.st_size / pagesize);
  nv_add_nvp (res, "freepage", " ");

  nv_add_nvp_time (res, "date", statbuf.st_mtime, "%04d%02d%02d",
		   NV_ADD_DATE);

  nv_add_nvp (res, "close", "spaceinfo");
  return;
}

/* if cubrid.conf's error_log is null, construct it by default value if existing */
static char *
_ts_get_error_log_param (char *dbname)
{
  char *tok[2];
  FILE *infile;
  char buf[1024], dbdir[512];

  if ((uRetrieveDBDirectory (dbname, dbdir)) != ERR_NO_ERROR)
    return NULL;
  sprintf (buf, "%s/%s", dbdir, CUBRID_CUBRID_CONF);
  if ((infile = fopen (buf, "r")) == NULL)
    return NULL;

  while (fgets (buf, 1024, infile))
    {
      ut_trim (buf);
      if (isalpha ((int) buf[0]))
	{
	  if (string_tokenize2 (buf, tok, 2, '=') < 0)
	    continue;
	  if (uStringEqual (tok[0], "error_log"))
	    {
	      fclose (infile);
	      if (tok[1][0] == '\0')
		return NULL;
#ifdef WIN32
	      unix_style_path (tok[1]);
#endif
	      return (strdup (tok[1]));
	    }
	}
    }
  return NULL;
}

int
ts_localdb_operation (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *task, *dbname, *dbuser, *dbpasswd;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[11];
  char input_file[200];
  char *cubrid_err_file;
  int retval, argc;
  T_DB_SERVICE_MODE db_mode;

  task = nv_get_val (req, "task");

  if (strcmp (task, "registerlocaldb") == 0)
    {
      sprintf (input_file, "%s/dbmt_task_%d_%d", sco.dbmt_tmp_dir,
	       TS_REGISTERLOCALDB, (int) getpid ());
    }
  else if (strcmp (task, "removelocaldb") == 0)
    {
      sprintf (input_file, "%s/dbmt_task_%d_%d", sco.dbmt_tmp_dir,
	       TS_REMOVELOCALDB, (int) getpid ());
    }

  dbname = nv_get_val (req, "_DBNAME");
  dbuser = nv_get_val (req, "_DBID");
  dbpasswd = nv_get_val (req, "_DBPASSWD");

  cmd_name[0] = '\0';
  sprintf (cmd_name, "%s/%s%s", sco.szCubrid, CUBRID_DIR_BIN, UTIL_CSQL_NAME);

  argc = 0;
  argv[argc++] = cmd_name;
  argv[argc++] = dbname;
  argv[argc++] = NULL;
  argv[argc++] = "--" CSQL_INPUT_FILE_L;
  argv[argc++] = input_file;
  if (dbuser)
    {
      argv[argc++] = "--" CSQL_USER_L;
      argv[argc++] = dbuser;
      if (dbpasswd)
	{
	  argv[argc++] = "--" CSQL_PASSWORD_L;
	  argv[argc++] = dbpasswd;
	}
    }
  argv[argc++] = "--" CSQL_NO_AUTO_COMMIT_L;

  for (; argc < 11; argc++)
    argv[argc] = NULL;

  if (!_isRegisteredDB (dbname))
    return ERR_DB_NONEXISTANT;

  db_mode = uDatabaseMode (dbname);

  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (db_mode == DB_SERVICE_MODE_CS)
    {
      /* run csql command with -cs option */
      argv[2] = "--" CSQL_CS_MODE_L;
    }

  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      /* run csql command with -sa option */
      argv[2] = "--" CSQL_SA_MODE_L;
    }

  /* register ldb with csql command */
  /* csql -sa -i input_file dbname  */

  if (strcmp (task, "registerlocaldb") == 0)
    {
      if (op_make_ldbinput_file_register (req, input_file) == 0)
	{
	  strcpy (_dbmt_error, argv[0]);
	  return ERR_TMPFILE_OPEN_FAIL;
	}
    }
  else if (strcmp (task, "removelocaldb") == 0)
    {
      if (op_make_ldbinput_file_remove (req, input_file) == 0)
	{
	  strcpy (_dbmt_error, argv[0]);
	  return ERR_TMPFILE_OPEN_FAIL;
	}
    }

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);
  SET_TRANSACTION_NO_WAIT_MODE_ENV ();

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* csql - register new ldb */
  unlink (input_file);

  if (read_csql_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE)
      < 0)
    {
      return ERR_WITH_MSG;
    }

  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  nv_add_nvp (res, "dbname", dbname);

  return ERR_NO_ERROR;
}

int
ts_trigger_operation (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *task, *dbname, *dbuser, *dbpasswd;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[11];
  char input_file[200];
  char *cubrid_err_file;
  int retval, argc;
  T_DB_SERVICE_MODE db_mode;

  task = nv_get_val (req, "task");

  if (strcmp (task, "addtrigger") == 0)
    sprintf (input_file, "%s/dbmt_task_%d_%d", sco.dbmt_tmp_dir,
	     TS_ADDNEWTRIGGER, (int) getpid ());
  else if (strcmp (task, "droptrigger") == 0)
    sprintf (input_file, "%s/dbmt_task_%d_%d", sco.dbmt_tmp_dir,
	     TS_DROPTRIGGER, (int) getpid ());
  else if (strcmp (task, "altertrigger") == 0)
    sprintf (input_file, "%s/dbmt_task_%d_%d", sco.dbmt_tmp_dir,
	     TS_ALTERTRIGGER, (int) getpid ());

  dbname = nv_get_val (req, "_DBNAME");
  dbuser = nv_get_val (req, "_DBID");
  dbpasswd = nv_get_val (req, "_DBPASSWD");

  cmd_name[0] = '\0';
  sprintf (cmd_name, "%s/%s%s", sco.szCubrid, CUBRID_DIR_BIN, UTIL_CSQL_NAME);
  argc = 0;
  argv[argc++] = cmd_name;
  argv[argc++] = dbname;
  argv[argc++] = NULL;
  argv[argc++] = "--" CSQL_INPUT_FILE_L;
  argv[argc++] = input_file;
  if (dbuser)
    {
      argv[argc++] = "--" CSQL_USER_L;
      argv[argc++] = dbuser;
      if (dbpasswd)
	{
	  argv[argc++] = "--" CSQL_PASSWORD_L;
	  argv[argc++] = dbpasswd;
	}
    }

  argv[argc++] = "--" CSQL_NO_AUTO_COMMIT_L;

  for (; argc < 11; argc++)
    argv[argc] = NULL;

  if (!_isRegisteredDB (dbname))
    return ERR_DB_NONEXISTANT;

  db_mode = uDatabaseMode (dbname);

  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (db_mode == DB_SERVICE_MODE_CS)
    {
      /* run csql command with -cs option */
      argv[2] = "--" CSQL_CS_MODE_L;
    }

  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      /* run csql command with -sa option */
      argv[2] = "--" CSQL_SA_MODE_L;
    }

  /* csql -sa -i input_file dbname  */
  if (strcmp (task, "addtrigger") == 0)
    {
      if (op_make_triggerinput_file_add (req, input_file) == 0)
	{
	  strcpy (_dbmt_error, argv[0]);
	  return ERR_TMPFILE_OPEN_FAIL;
	}
    }
  else if (strcmp (task, "droptrigger") == 0)
    {
      if (op_make_triggerinput_file_drop (req, input_file) == 0)
	{
	  strcpy (_dbmt_error, argv[0]);
	  return ERR_TMPFILE_OPEN_FAIL;
	}
    }
  else if (strcmp (task, "altertrigger") == 0)
    {
      if (op_make_triggerinput_file_alter (req, input_file) == 0)
	{
	  strcpy (_dbmt_error, argv[0]);
	  return ERR_TMPFILE_OPEN_FAIL;
	}
    }


  INIT_CUBRID_ERROR_FILE (cubrid_err_file);
  SET_TRANSACTION_NO_WAIT_MODE_ENV ();

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* csql - trigger */
  unlink (input_file);

  if (read_csql_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE)
      < 0)
    {
      return ERR_WITH_MSG;
    }

  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  nv_add_nvp (res, "dbname", dbname);

  return ERR_NO_ERROR;
}

int
ts_get_triggerinfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  DB_OBJLIST *trigger_list, *temp;
  DB_OBJECT *p_trigger;
  int errcode;
  T_DB_SERVICE_MODE db_mode;
  char *dbname;

  trigger_list = NULL;

  dbname = nv_get_val (req, "_DBNAME");

  db_mode = uDatabaseMode (dbname);
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
  if (_op_db_login (res, req, _dbmt_error) < 0)
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


static char *
get_user_name (int uid, char *name_buf)
{
#ifdef WIN32
  strcpy (name_buf, "");
#else
  struct passwd *pwd;

  pwd = getpwuid (uid);
  if (pwd->pw_name)
    strcpy (name_buf, pwd->pw_name);
  else
    name_buf[0] = '\0';
#endif

  return name_buf;
}

static int
class_info_sa (char *dbname, char *uid, char *passwd, nvplist * out,
	       char *_dbmt_error)
{
  char strbuf[1024];
  char outfile[512], errfile[512];
  FILE *fp;
  int ret_val = ERR_NO_ERROR;
  char cmd_name[128];
  char *argv[10];
  char cli_ver[10];
  char opcode[10];

  sprintf (outfile, "%s/tmp/DBMT_class_info.%d", sco.szCubrid,
	   (int) getpid ());
  sprintf (errfile, "%s.err", outfile);

  if (uid == NULL)
    uid = "";
  if (passwd == NULL)
    passwd = "";

  sprintf (cli_ver, "%d", CLIENT_VERSION);

  sprintf (cmd_name, "%s/bin/cub_jobsa%s", sco.szCubrid, DBMT_EXE_EXT);
  sprintf (opcode, "%d", EMS_SA_CLASS_INFO);
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

      if (sscanf (strbuf, "%s %s", name, value) < 2)
	continue;
      nv_add_nvp (out, name, value);
    }
  fclose (fp);

class_info_sa_finale:
  unlink (outfile);
  unlink (errfile);
  return ret_val;
}

#ifdef WIN32
static void
replace_colon (char *path)
{
  char *p;
  for (p = path; *p; p++)
    {
      if (*p == '|')
	*p = ':';
    }
}
#endif

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
	continue;
      class_obj =
	(DB_OBJECT
	 **) (REALLOC (class_obj, sizeof (DB_OBJECT *) * (num_class + 1)));
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


static int
op_get_objectid_attlist (DB_NAMELIST ** att_list, char *att_comma_list)
{
  int i, j, string_length;
  char att_name[100];

  j = 0;
  string_length = strlen (att_comma_list);
  memset (att_name, '\0', sizeof (att_name));

  for (i = 0; i < string_length; i++)
    {
      if (*(att_comma_list + i) == ',')
	{
	  db_namelist_add (att_list, att_name);
	  memset (att_name, '\0', sizeof (att_name));
	  j = 0;
	}
      else
	{
	  att_name[j] = *(att_comma_list + i);
	  j++;
	}

      if ((i == string_length - 1) && att_name)
	{
	  if (db_namelist_add (att_list, att_name))
	    {
	      memset (att_name, '\0', sizeof (att_name));
	      j = 0;
	    }
	  /*  else {the name is already on the list;} */
	}
    }

  return 1;
}

#ifdef	_DEBUG_
void *
debug_malloc (size_t size)
{
  assert (size > 0);
  return malloc (size);
}
#endif

static int
op_make_ldbinput_file_register (nvplist * req, char *input_filename)
{
  char *name, *host_name, *name_in_host, *database_type, *user_name;
  char *password, *directory;
  char *max_active, *min_active, *decay_constant, *oid_string;

  FILE *input_file;

  if (input_filename)
    input_file = fopen (input_filename, "w+");
  else
    return 0;

  if (input_file)
    {
      name = nv_get_val (req, "ldbname");
      host_name = nv_get_val (req, "hostname");
      name_in_host = nv_get_val (req, "nameinhost");
      database_type = nv_get_val (req, "type");
      user_name = nv_get_val (req, "username");
      password = nv_get_val (req, "password");
      directory = nv_get_val (req, "directory");
      max_active = nv_get_val (req, "maxactive");
      min_active = nv_get_val (req, "minactive");
      decay_constant = nv_get_val (req, "decayvalue");
      oid_string = nv_get_val (req, "objectid");

/*	  fprintf(input_file, ";autocommit off\n");*/

      fprintf (input_file, "REGISTER LDB %s\n", name);
      fprintf (input_file, "NAME\t'%s'\n", name_in_host);
      fprintf (input_file, "TYPE\t'%s'\n", database_type);
      fprintf (input_file, "HOST\t'%s'\n", host_name);

      if (user_name)
	{
	  fprintf (input_file, "USER\t'%s'\n", user_name);
	  if (password)
	    {
	      fprintf (input_file, "PASSWORD\t'%s'\n", password);
	    }
	  else
	    fprintf (input_file, "PASSWORD\t''\n");
	}
      if (max_active)
	fprintf (input_file, "MAX_ACTIVE\t%s\n", max_active);
      if (min_active)
	fprintf (input_file, "MIN_ACTIVE\t%s\n", min_active);
      if (decay_constant)
	fprintf (input_file, "DECAY_CONSTANT\t%s\n", decay_constant);
      if (directory)
	fprintf (input_file, "DIRECTORY\t'%s'\n", directory);
      if (oid_string)
	fprintf (input_file, "OBJECT_ID\t%s\n", oid_string);

      fprintf (input_file, "\n\ncommit\n");
      fclose (input_file);
    }
  else
    {
      return 0;
    }
  return 1;
}


static int
op_make_ldbinput_file_remove (nvplist * req, char *input_filename)
{
  char *name;

  FILE *input_file;
  input_file = fopen (input_filename, "w+");

  if (input_file)
    {
      name = nv_get_val (req, "ldbname");

/*	  fprintf(input_file,";autocommit off\n");*/
      fprintf (input_file, "DROP LDB %s\n", name);
      fprintf (input_file, "\n\ncommit\n");

      fclose (input_file);
    }
  else
    {
      /* can't open input file */
      return 0;
    }

  return 1;
}

static int
op_make_triggerinput_file_add (nvplist * req, char *input_filename)
{
  char *name, *status, *cond_source, *priority;
  char *event_target, *event_type, *event_time, *actiontime, *action;
  FILE *input_file;

  if (input_filename)
    {
      input_file = fopen (input_filename, "w+");

      if (input_file)
	{
	  name = nv_get_val (req, "triggername");
	  status = nv_get_val (req, "status");
	  event_type = nv_get_val (req, "eventtype");
	  event_target = nv_get_val (req, "eventtarget");
	  event_time = nv_get_val (req, "conditiontime");
	  cond_source = nv_get_val (req, "condition");
	  actiontime = nv_get_val (req, "actiontime");
	  action = nv_get_val (req, "action");
	  priority = nv_get_val (req, "priority");

/*		  fprintf(input_file, ";autocommit off\n");*/
	  fprintf (input_file, "create trigger\t%s\n", name);

	  if (status)
	    fprintf (input_file, "status\t%s\n", status);

	  if (priority)
	    fprintf (input_file, "priority\t%s\n", priority);

	  fprintf (input_file, "%s\t%s\t", event_time, event_type);

	  if (event_target)
	    fprintf (input_file, "ON\t%s\n", event_target);

	  if (cond_source)
	    fprintf (input_file, "if\t%s\n", cond_source);

	  fprintf (input_file, "execute\t");

	  if (actiontime)
	    fprintf (input_file, "%s\t", actiontime);

	  fprintf (input_file, "%s\n", action);
	  fprintf (input_file, "\n\ncommit\n\n");

	  fclose (input_file);
	}
      else
	{
	  /* can't open file */
	  return 0;
	}
    }
  else
    {
      /* input_filename is empty string */
      return 0;
    }

  return 1;

}


static int
op_make_triggerinput_file_drop (nvplist * req, char *input_filename)
{
  char *trigger_name;
  FILE *input_file;

  if (input_filename)
    {
      input_file = fopen (input_filename, "w+");

      if (input_file)
	{
	  trigger_name = nv_get_val (req, "triggername");

/*		  fprintf(input_file, ";autocommit off\n");*/
	  fprintf (input_file, "drop trigger\t%s\n", trigger_name);
	  fprintf (input_file, "\n\n\ncommit\n\n");
	  fclose (input_file);
	}
      else
	{
	  /* can't open input file */
	  return 0;
	}
    }
  else
    {
      /* input_filename is empty string */
      return 0;
    }

  return 1;
}

static int
op_make_password_check_file (char *input_filename)
{
  FILE *input_file;

  if (input_filename)
    {
      input_file = fopen (input_filename, "w+");

      if (input_file)
	{
	  fprintf (input_file, ";commit");
	  fclose (input_file);
	}
      else
	{
	  /* can't open file */
	  return 0;
	}
    }
  else
    {
      /* input_filename is empty string */
      return 0;
    }

  return 1;
}

static int
op_make_triggerinput_file_alter (nvplist * req, char *input_filename)
{
  char *trigger_name, *status, *priority;
  FILE *input_file;

  if (input_filename)
    {
      input_file = fopen (input_filename, "w+");

      if (input_file)
	{
	  trigger_name = nv_get_val (req, "triggername");
	  status = nv_get_val (req, "status");
	  priority = nv_get_val (req, "priority");

/*		  fprintf(input_file, ";autocommit off\n");*/

	  if (status)
	    {
	      fprintf (input_file, "alter trigger\t%s\t", trigger_name);
	      fprintf (input_file, "status %s\t", status);
	    }

	  if (priority)
	    {
	      fprintf (input_file, "alter trigger\t%s\t", trigger_name);
	      fprintf (input_file, "priority %s\t", priority);
	    }

	  fprintf (input_file, "\n\n\ncommit\n\n");
	  fclose (input_file);
	}
      else
	{
	  /* can't open file */
	  return 0;
	}
    }
  else
    {
      /* input_filename is empty string */
      return 0;
    }
  return 1;
}

static int
_ts_lockdb_parse_us (nvplist * res, FILE * infile)
{
  char buf[1024], s[256], s1[256], s2[256], s3[256], s4[256];
  char *temp, *temp2;
  int scan_matched;
  int flag = 0;

  nv_add_nvp (res, "open", "lockinfo");
  while (fgets (buf, 1024, infile))
    {
      sscanf (buf, "%s", s);

      if (flag == 0 && !strcmp (s, "***"))
	{
	  fgets (buf, 1024, infile);
	  scan_matched =
	    sscanf (buf, "%*s %*s %*s %*s %s %*s %*s %*s %*s %s", s1, s2);

	  if (scan_matched != 2)
	    return -1;

	  s1[strlen (s1) - 1] = '\0';
	  nv_add_nvp (res, "esc", s1);
	  nv_add_nvp (res, "dinterval", s2);
	  flag = 1;
	}
      else if (flag == 1)
	{
	  if (strcmp (s, "Transaction") == 0)
	    {
	      scan_matched = sscanf (buf, "%*s %*s %s %s %s", s1, s2, s3);

	      if (scan_matched != 3)
		return -1;
	      s1[strlen (s1) - 1] = '\0';
	      s2[strlen (s2) - 1] = '\0';
	      s3[strlen (s3) - 1] = '\0';

	      nv_add_nvp (res, "open", "transaction");
	      nv_add_nvp (res, "index", s1);
	      nv_add_nvp (res, "pname", s2);

	      temp = strchr (s3, '@');
	      if (temp != NULL)
		{
		  strncpy (buf, s3, (int) (temp - s3));
		  buf[(int) (temp - s3)] = '\0';
		  nv_add_nvp (res, "uid", buf);
		}

	      temp2 = strrchr (s3, '|');
	      if (temp2 != NULL)
		{
		  strncpy (buf, temp + 1, (int) (temp2 - temp - 1));
		  buf[(int) (temp2 - temp) - 1] = '\0';
		  nv_add_nvp (res, "host", buf);
		}
	      nv_add_nvp (res, "pid", temp2 + 1);

	      fgets (buf, 1024, infile);
	      buf[strlen (buf) - 1] = '\0';
	      nv_add_nvp (res, "isolevel", buf + strlen ("Isolation "));

	      fgets (buf, 1024, infile);
	      if (strncmp (buf, "State", strlen ("State")) == 0)
		{
		  fgets (buf, 1024, infile);
		}

	      scan_matched = sscanf (buf, "%*s %s", s1);

	      if (scan_matched != 1)
		return -1;

	      nv_add_nvp (res, "timeout", s1);
	      nv_add_nvp (res, "close", "transaction");
	    }
	  else if (strcmp (s, "Object") == 0)
	    {
	      fgets (buf, 1024, infile);
	      scan_matched =
		sscanf (buf, "%*s %*s %*s %*s %*s %*s %*s %*s %s", s1);
	      if (scan_matched != 1)
		return -1;

	      nv_add_nvp (res, "open", "lot");
	      nv_add_nvp (res, "numlocked", s1);

	      fgets (buf, 1024, infile);
	      scan_matched =
		sscanf (buf, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %s", s2);
	      if (scan_matched != 1)
		return -1;
	      nv_add_nvp (res, "maxnumlock", s2);
	      flag = 2;
	    }
	}			/* end of if (flag == 1) */
      else if (flag == 2)
	{
	  while (!strcmp (s, "OID"))
	    {
	      int num_holders, num_b_holders, num_waiters, scan_matched;

	      scan_matched = sscanf (buf, "%*s %*s %s %s %s", s1, s2, s3);
	      if (scan_matched != 3)
		return -1;

	      strcat (s1, s2);
	      strcat (s1, s3);
	      nv_add_nvp (res, "open", "entry");
	      nv_add_nvp (res, "oid", s1);

	      fgets (buf, 1024, infile);
	      sscanf (buf, "%*s %*s %s", s);

	      s1[0] = s2[0] = s3[0] = '\0';
	      scan_matched = 0;
	      if ((strcmp (s, "Class") == 0) || (strcmp (s, "Instance") == 0))
		{
		  char *p;
		  p = strchr (buf, ':');
		  buf[strlen (buf) - 1] = '\0';
		  if (buf[strlen (buf) - 1] == '.')
		    {
		      buf[strlen (buf) - 1] = '\0';
		    }
		  nv_add_nvp (res, "ob_type", p + 2);
		  fgets (buf, 1024, infile);
		}
	      else if (strcmp (s, "Root") == 0)
		{
		  nv_add_nvp (res, "ob_type", "Root class");
		  fgets (buf, 1024, infile);
		}
	      else
		{
		  /* Current test is not 'OID = ...' and if 'Total mode of holders ...' then */
		  scan_matched =
		    sscanf (buf, "%*s %*s %s %*s %*s %s %*s %*s %s", s1, s2,
			    s3);
		  if ((strncmp (s1, "of", 2) == 0)
		      && (strncmp (s3, "of", 2) == 0))
		    {
		      nv_add_nvp (res, "ob_type", "None");
		    }
		  else
		    return -1;
		}

	      /* already get  scan_matched value, don't sscanf */
	      if (scan_matched == 0)
		scan_matched =
		  sscanf (buf, "%*s %*s %s %*s %*s %s %*s %*s %s", s1, s2,
			  s3);

	      if ((strncmp (s1, "of", 2) == 0)
		  && (strncmp (s3, "of", 2) == 0))
		{
		  /* ignore UnixWare's 'Total mode of ...' text */
		  fgets (buf, 1024, infile);
		  scan_matched =
		    sscanf (buf, "%*s %*s %s %*s %*s %s %*s %*s %s", s1, s2,
			    s3);
		}

	      if (scan_matched != 3)
		return -1;

	      s1[strlen (s1) - 1] = '\0';
	      s2[strlen (s2) - 1] = '\0';

	      num_holders = atoi (s1);
	      num_b_holders = atoi (s2);
	      num_waiters = atoi (s3);

	      nv_add_nvp (res, "num_holders", s1);
	      nv_add_nvp (res, "num_b_holders", s2);
	      nv_add_nvp (res, "num_waiters", s3);

	      while (fgets (buf, 1024, infile))
		{
		  sscanf (buf, "%s", s);
		  if (strcmp (s, "NON_2PL_RELEASED:") == 0)
		    {
		      fgets (buf, 1024, infile);
		      while (sscanf (buf, "%s", s))
			{
			  if (strcmp (s, "Tran_index") != 0)
			    {
			      break;
			    }
			  else
			    {
			      if (fgets (buf, 1024, infile) == NULL)
				break;
			    }
			}
		      break;
		    }		/* ignore NON_2PL_RELEASED information */

		  sscanf (buf, "%*s %s", s);
		  if (strcmp (s, "HOLDERS:") == 0)
		    {
		      int index;

		      for (index = 0; index < num_holders; index++)
			{
			  /* make lock holders information */

			  fgets (buf, 1024, infile);
			  scan_matched =
			    sscanf (buf,
				    "%*s %*s %s %*s %*s %s %*s %*s %s %*s %*s %s",
				    s1, s2, s3, s4);

			  /* parshing error */
			  if (scan_matched < 3)
			    return -1;

			  if (scan_matched == 4)
			    {
			      /* nsubgranules is existed */
			      s3[strlen (s3) - 1] = '\0';
			    }

			  s1[strlen (s1) - 1] = '\0';
			  s2[strlen (s2) - 1] = '\0';

			  nv_add_nvp (res, "open", "lock_holders");
			  nv_add_nvp (res, "tran_index", s1);
			  nv_add_nvp (res, "granted_mode", s2);
			  nv_add_nvp (res, "count", s3);
			  if (scan_matched == 4)
			    nv_add_nvp (res, "nsubgranules", s4);
			  nv_add_nvp (res, "close", "lock_holders");
			}

		      if ((num_b_holders == 0) && (num_waiters == 0))
			break;
		    }
		  else if (strcmp (s, "LOCK") == 0)
		    {
		      int index;
		      char *p;

		      for (index = 0; index < num_b_holders; index++)
			{
			  /* make blocked lock holders */
			  int scan_matched;
			  fgets (buf, 1024, infile);
			  scan_matched =
			    sscanf (buf,
				    "%*s %*s %s %*s %*s %s %*s %*s %s %*s %*s %s",
				    s1, s2, s3, s4);

			  /* parshing error */
			  if (scan_matched < 3)
			    return -1;

			  if (scan_matched == 4)
			    {
			      /* nsubgranules is existed */
			      s3[strlen (s3) - 1] = '\0';
			    }

			  s1[strlen (s1) - 1] = '\0';
			  s2[strlen (s2) - 1] = '\0';

			  nv_add_nvp (res, "open", "b_holders");
			  nv_add_nvp (res, "tran_index", s1);
			  nv_add_nvp (res, "granted_mode", s2);
			  nv_add_nvp (res, "count", s3);

			  if (scan_matched == 4)
			    nv_add_nvp (res, "nsubgranules", s4);

			  fgets (buf, 1024, infile);
			  sscanf (buf, "%*s %*s %s", s1);
			  nv_add_nvp (res, "b_mode", s1);

			  fgets (buf, 1024, infile);
			  p = strchr (buf, '=');
			  buf[strlen (buf) - 1] = '\0';
			  nv_add_nvp (res, "start_at", p + 2);

			  fgets (buf, 1024, infile);
			  sscanf (buf, "%*s %*s %s", s1);
			  nv_add_nvp (res, "waitfornsec", s1);

			  nv_add_nvp (res, "close", "b_holders");
			}

		      if (num_waiters == 0)
			break;
		    }
		  else if (strcmp (s, "WAITERS:") == 0)
		    {
		      int index;

		      for (index = 0; index < num_waiters; index++)
			{
			  /* make lock waiters */
			  char *p;

			  fgets (buf, 1024, infile);
			  sscanf (buf, "%*s %*s %s %*s %*s %s", s1, s2);
			  s1[strlen (s1) - 1] = '\0';

			  nv_add_nvp (res, "open", "waiters");
			  nv_add_nvp (res, "tran_index", s1);
			  nv_add_nvp (res, "b_mode", s2);

			  fgets (buf, 1024, infile);
			  p = strchr (buf, '=');
			  buf[strlen (buf) - 1] = '\0';
			  nv_add_nvp (res, "start_at", p + 2);

			  fgets (buf, 1024, infile);
			  sscanf (buf, "%*s %*s %s", s1);
			  nv_add_nvp (res, "waitfornsec", s1);

			  nv_add_nvp (res, "close", "waiters");
			}
		      break;
		    }
		}		/* end of while - for just one object */
	      nv_add_nvp (res, "close", "entry");
	    }			/* end of while(OID) */
	}
    }
  nv_add_nvp (res, "close", "lot");
  nv_add_nvp (res, "close", "lockinfo");
  return 0;
}

static int
_ts_lockdb_parse_kr (nvplist * res, FILE * infile)
{
  char buf[1024], s[256], s1[256], s2[256], s3[256], s4[256];
  char *temp, *temp2;
  int scan_matched;
  int flag = 0;

  nv_add_nvp (res, "open", "lockinfo");
  while (fgets (buf, 1024, infile))
    {
      sscanf (buf, "%s", s);

      if (flag == 0 && !strcmp (s, "***"))
	{
	  fgets (buf, 1024, infile);
	  scan_matched =
	    sscanf (buf, "%*s %*s %*s %s %*s %*s %*s %*s %s", s1, s2);

	  if (scan_matched != 2)
	    return -1;

	  s1[strlen (s1) - 1] = '\0';
	  nv_add_nvp (res, "esc", s1);
	  nv_add_nvp (res, "dinterval", s2);
	  flag = 1;
	}
      else if (flag == 1)
	{
	  if (strcmp (s, "Æ®·£Àè¼Ç") == 0)
	    {
	      scan_matched = sscanf (buf, "%*s %*s %s %s %s", s1, s2, s3);

	      if (scan_matched != 3)
		return -1;
	      s1[strlen (s1) - 1] = '\0';
	      s2[strlen (s2) - 1] = '\0';
	      s3[strlen (s3) - 1] = '\0';

	      nv_add_nvp (res, "open", "transaction");
	      nv_add_nvp (res, "index", s1);
	      nv_add_nvp (res, "pname", s2);

	      temp = strchr (s3, '@');
	      if (temp != NULL)
		{
		  strncpy (buf, s3, (int) (temp - s3));
		  buf[(int) (temp - s3)] = '\0';
		  nv_add_nvp (res, "uid", buf);
		}

	      temp2 = strrchr (s3, '|');
	      if (temp2 != NULL)
		{
		  strncpy (buf, temp + 1, (int) (temp2 - temp - 1));
		  buf[(int) (temp2 - temp) - 1] = '\0';
		  nv_add_nvp (res, "host", buf);
		}
	      nv_add_nvp (res, "pid", temp2 + 1);

	      fgets (buf, 1024, infile);
	      buf[strlen (buf) - 1] = '\0';
	      nv_add_nvp (res, "isolevel", buf + strlen ("°Ý¸®µµ "));

	      fgets (buf, 1024, infile);
	      if (strncmp
		  (buf, "¼öÇà»óÅÂ", strlen ("¼öÇà»óÅÂ")) == 0)
		{
		  fgets (buf, 1024, infile);
		}

	      scan_matched = sscanf (buf, "%*s %*s %s", s1);

	      if (scan_matched != 1)
		return -1;

	      nv_add_nvp (res, "timeout", s1);
	      nv_add_nvp (res, "close", "transaction");
	    }
	  else if (strcmp (s, "Object") == 0)
	    {
	      fgets (buf, 1024, infile);
	      scan_matched =
		sscanf (buf, "%*s %*s %*s %*s %*s %*s %*s %*s %s", s1);
	      if (scan_matched != 1)
		return -1;

	      nv_add_nvp (res, "open", "lot");
	      nv_add_nvp (res, "numlocked", s1);

	      fgets (buf, 1024, infile);
	      scan_matched =
		sscanf (buf, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %s", s2);
	      if (scan_matched != 1)
		return -1;
	      nv_add_nvp (res, "maxnumlock", s2);
	      flag = 2;
	    }
	}			/* end of if (flag == 1) */
      else if (flag == 2)
	{
	  while (!strcmp (s, "OID"))
	    {
	      int num_holders, num_b_holders, num_waiters;

	      scan_matched = sscanf (buf, "%*s %*s %s %s %s", s1, s2, s3);
	      if (scan_matched != 3)
		return -1;

	      strcat (s1, s2);
	      strcat (s1, s3);
	      nv_add_nvp (res, "open", "entry");
	      nv_add_nvp (res, "oid", s1);

	      fgets (buf, 1024, infile);
	      sscanf (buf, "%*s %*s %s", s);

	      s1[0] = s2[0] = s3[0] = '\0';
	      scan_matched = 0;
	      if ((strcmp (s, "Class") == 0) || (strcmp (s, "Instance") == 0))
		{
		  char *p;
		  p = strchr (buf, ':');
		  buf[strlen (buf) - 1] = '\0';
		  if (buf[strlen (buf) - 1] == '.')
		    {
		      buf[strlen (buf) - 1] = '\0';
		    }
		  nv_add_nvp (res, "ob_type", p + 2);
		  fgets (buf, 1024, infile);
		}
	      else if (strcmp (s, "Root") == 0)
		{
		  nv_add_nvp (res, "ob_type", "Root class");
		  fgets (buf, 1024, infile);
		}
	      else
		{
		  /* current text is not 'OID = ...' and if 'Total mode of holders ...' then */
		  scan_matched =
		    sscanf (buf, "%*s %*s %s %*s %*s %s %*s %*s %s", s1, s2,
			    s3);
		  if ((strncmp (s1, "of", 2) == 0)
		      && (strncmp (s3, "of", 2) == 0))
		    {
		      nv_add_nvp (res, "ob_type", "None");
		    }
		  else
		    return -1;
		}
	      /* Already get scan_matched value, dont't sscanf */
	      if (scan_matched == 0)
		scan_matched =
		  sscanf (buf, "%*s %*s %s %*s %*s %s %*s %*s %s", s1, s2,
			  s3);
	      if ((strncmp (s1, "of", 2) == 0)
		  && (strncmp (s3, "of", 2) == 0))
		{
		  /* ignore UnixWare's 'Total mode of ...' text */
		  fgets (buf, 1024, infile);
		  scan_matched =
		    sscanf (buf, "%*s %*s %s %*s %*s %s %*s %*s %s", s1, s2,
			    s3);
		}

	      if (scan_matched != 3)
		return -1;

	      s1[strlen (s1) - 1] = '\0';
	      s2[strlen (s2) - 1] = '\0';

	      num_holders = atoi (s1);
	      num_b_holders = atoi (s2);
	      num_waiters = atoi (s3);

	      nv_add_nvp (res, "num_holders", s1);
	      nv_add_nvp (res, "num_b_holders", s2);
	      nv_add_nvp (res, "num_waiters", s3);

	      while (fgets (buf, 1024, infile))
		{
		  sscanf (buf, "%s", s);
		  if (strcmp (s, "NON_2PL_RELEASED:") == 0)
		    {
		      fgets (buf, 1024, infile);
		      while (sscanf (buf, "%s", s))
			{
			  if (strcmp (s, "Tran_index") != 0)
			    {
			      break;
			    }
			  else
			    {
			      if (fgets (buf, 1024, infile) == NULL)
				break;
			    }
			}
		      break;
		    }		/* ignore NON_2PL_RELEASED information */

		  sscanf (buf, "%*s %s", s);
		  if (strcmp (s, "HOLDERS:") == 0)
		    {
		      int index;

		      for (index = 0; index < num_holders; index++)
			{
			  /* make lock holders information */

			  fgets (buf, 1024, infile);
			  scan_matched =
			    sscanf (buf,
				    "%*s %*s %s %*s %*s %s %*s %*s %s %*s %*s %s",
				    s1, s2, s3, s4);

			  /* parshing error */
			  if (scan_matched < 3)
			    return -1;

			  if (scan_matched == 4)
			    {
			      /* nsubgranules is existed */
			      s3[strlen (s3) - 1] = '\0';
			    }

			  s1[strlen (s1) - 1] = '\0';
			  s2[strlen (s2) - 1] = '\0';

			  nv_add_nvp (res, "open", "lock_holders");
			  nv_add_nvp (res, "tran_index", s1);
			  nv_add_nvp (res, "granted_mode", s2);
			  nv_add_nvp (res, "count", s3);
			  if (scan_matched == 4)
			    nv_add_nvp (res, "nsubgranules", s4);
			  nv_add_nvp (res, "close", "lock_holders");
			}

		      if ((num_b_holders == 0) && (num_waiters == 0))
			break;
		    }
		  else if (strcmp (s, "LOCK") == 0)
		    {
		      int index;
		      char *p;

		      for (index = 0; index < num_b_holders; index++)
			{
			  /* make blocked lock holders */
			  int scan_matched;
			  fgets (buf, 1024, infile);
			  scan_matched =
			    sscanf (buf,
				    "%*s %*s %s %*s %*s %s %*s %*s %s %*s %*s %s",
				    s1, s2, s3, s4);

			  /* parshing error */
			  if (scan_matched < 3)
			    return -1;

			  if (scan_matched == 4)
			    {
			      /* nsubgranules is existed */
			      s3[strlen (s3) - 1] = '\0';
			    }

			  s1[strlen (s1) - 1] = '\0';
			  s2[strlen (s2) - 1] = '\0';

			  nv_add_nvp (res, "open", "b_holders");
			  nv_add_nvp (res, "tran_index", s1);
			  nv_add_nvp (res, "granted_mode", s2);
			  nv_add_nvp (res, "count", s3);

			  if (scan_matched == 4)
			    nv_add_nvp (res, "nsubgranules", s4);

			  fgets (buf, 1024, infile);
			  sscanf (buf, "%*s %*s %s", s1);
			  nv_add_nvp (res, "b_mode", s1);

			  fgets (buf, 1024, infile);
			  p = strchr (buf, '=');
			  buf[strlen (buf) - 1] = '\0';
			  nv_add_nvp (res, "start_at", p + 2);

			  fgets (buf, 1024, infile);
			  sscanf (buf, "%*s %*s %s", s1);
			  nv_add_nvp (res, "waitfornsec", s1);

			  nv_add_nvp (res, "close", "b_holders");
			}

		      if (num_waiters == 0)
			break;
		    }
		  else if (strcmp (s, "WAITERS:") == 0)
		    {
		      int index;

		      for (index = 0; index < num_waiters; index++)
			{
			  /* make lock waiters */
			  char *p;

			  fgets (buf, 1024, infile);
			  sscanf (buf, "%*s %*s %s %*s %*s %s", s1, s2);
			  s1[strlen (s1) - 1] = '\0';

			  nv_add_nvp (res, "open", "waiters");
			  nv_add_nvp (res, "tran_index", s1);
			  nv_add_nvp (res, "b_mode", s2);

			  fgets (buf, 1024, infile);
			  p = strchr (buf, '=');
			  buf[strlen (buf) - 1] = '\0';
			  nv_add_nvp (res, "start_at", p + 2);

			  fgets (buf, 1024, infile);
			  sscanf (buf, "%*s %*s %s", s1);
			  nv_add_nvp (res, "waitfornsec", s1);
			  nv_add_nvp (res, "close", "waiters");
			}
		      break;
		    }
		}		/* end of while - for just one object */
	      nv_add_nvp (res, "close", "entry");
	    }			/* end of while(OID) */
	}
    }
  nv_add_nvp (res, "close", "lot");
  nv_add_nvp (res, "close", "lockinfo");
  return 0;
}

static void
op_get_trigger_information (nvplist * res, DB_OBJECT * p_trigger)
{
  char *trigger_name, *action, *attr, *condition;
  DB_OBJECT *target_class;
  DB_TRIGGER_EVENT event;
  DB_TRIGGER_TIME eventtime, actiontime;
  DB_TRIGGER_ACTION action_type;
  DB_TRIGGER_STATUS status;
  double priority;
  char pri_string[10];

  trigger_name = action = NULL;

  /* trigger name */
  db_trigger_name (p_trigger, &trigger_name);
  nv_add_nvp (res, "name", trigger_name);
  if (trigger_name)
    db_string_free ((char *) trigger_name);

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
  if (target_class)
    {
      nv_add_nvp (res, "target_class", db_get_class_name (target_class));
    }

  db_trigger_attribute (p_trigger, &attr);
  if (attr)
    {
      nv_add_nvp (res, "target_att", attr);
      db_string_free ((char *) attr);
    }

  /* condition */
  db_trigger_condition (p_trigger, &condition);
  if (condition)
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
  sprintf (pri_string, "%4.5f", priority);
  nv_add_nvp (res, "priority", pri_string);
}

static int
trigger_info_sa (char *dbname, char *uid, char *passwd, nvplist * out,
		 char *_dbmt_error)
{
  char strbuf[1024];
  char outfile[512], errfile[512];
  FILE *fp;
  int ret_val = ERR_NO_ERROR;
  char cmd_name[128];
  char *argv[10];

  sprintf (outfile, "%s/tmp/DBMT_trigger_info.%d", sco.szCubrid,
	   (int) getpid ());
  sprintf (errfile, "%s.err", outfile);

  if (uid == NULL)
    uid = "";
  if (passwd == NULL)
    passwd = "";

  sprintf (cmd_name, "%s/bin/cub_sainfo%s", sco.szCubrid, DBMT_EXE_EXT);
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
    goto trigger_info_sa_finale;

trigger_info_sa_finale:
  unlink (outfile);
  unlink (errfile);
  return ret_val;
}


int
ts_get_file (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *filename, *compress, *file_num;
  int b_compress, index, b_compress_failed;
  char *argv[7];
  char cmd_string[512];
  char com_filename[512];
  struct stat statbuf;
  int status_error;

  status_error = 1;
  memset (com_filename, '\0', sizeof (com_filename));
  memset (cmd_string, '\0', sizeof (cmd_string));
  b_compress_failed = 0;

  file_num = nv_get_val (req, "file_num");
#ifdef	_DEBUG_
  assert (file_num);
#endif

  nv_add_nvp (res, "file_num", file_num);
  for (index = 4; index < atoi (file_num) * 2 + 3; index += 2)
    {
      char *_filename, *_compress;

      nv_add_nvp (res, "open", "file");

      nv_lookup (req, index, &_filename, &filename);
      nv_lookup (req, index + 1, &_compress, &compress);

      if (compress && strcasecmp (compress, "y") == 0)
	b_compress = 1;
      else
	b_compress = 0;

      /* check read permission */
      if (filename)
	{
	  if (access (filename, R_OK) != 0)
	    {
	      nv_add_nvp (res, "filename", filename);
	      nv_add_nvp (res, "filestatus",
			  "File not exist or Read permission error");
	      nv_add_nvp (res, "filesize", "0");
	      nv_add_nvp (res, "close", "file");
	      continue;
	    }
	}

      /* make compress file */
      if (b_compress)
	{
	  char file[64];
	  /* execute gzip */
	  if (get_filename_from_path (filename, file) > 0)
	    {
	      sprintf (com_filename, "%s/tmp/%s_%d.gz", sco.szCubrid,
		       file, (int) getpid ());
	      argv[0] = "gzip";
	      argv[1] = "-c";
	      argv[2] = filename;
	      argv[3] = ">";
	      argv[4] = com_filename;
	      argv[5] = NULL;

	      sprintf (cmd_string, "%s %s %s %s %s", argv[0], argv[1],
		       argv[2], argv[3], argv[4]);

	      if (system (cmd_string) == -1)
		b_compress_failed = 1;
	      else
		b_compress_failed = 0;

	      /* archive file create check */
	      if (access (argv[4], R_OK) == 0)
		{
		  /* if existed, write archive file */
		  filename = com_filename;
		}
	      else
		b_compress_failed = 1;
	    }
	  else
	    b_compress_failed = 1;
	}

      nv_add_nvp (res, "filename", filename);

      /* file status */
      if ((b_compress == 1) && (b_compress_failed == 1))
	{
	  nv_add_nvp (res, "filestatus", "File compress failed");
	}
      else
	{
	  nv_add_nvp (res, "filestatus", "none");
	  status_error = 0;
	}

      /* file size */
      if (stat (filename, &statbuf) == 0)
	{
	  nv_add_nvp_int (res, "filesize", statbuf.st_size);
	}
      else
	{
	  nv_add_nvp (res, "filesize", "0");
	}

      /* delete flag */
      if ((b_compress == 1) && (b_compress_failed == 0))
	nv_add_nvp (res, "delflag", "y");
      else
	nv_add_nvp (res, "delflag", "n");

      nv_add_nvp (res, "close", "file");
    }
/*
	if (status_error == 1)
		return ERR_GET_FILE;
*/
  return ERR_NO_ERROR;
}

static char *
op_get_cubrid_ver (char *buffer)
{
  char tmpfile[512];
  char strbuf[1024];
  FILE *infile;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[5];

  if (!buffer)
    return NULL;

  sprintf (tmpfile, "%s/DBMT_temp.%d", sco.dbmt_tmp_dir, (int) getpid ());

  cmd_name[0] = '\0';
  sprintf (cmd_name, "%s/%s%s", sco.szCubrid,
	   CUBRID_DIR_BIN, UTIL_CUBRID_REL_NAME);

  argv[0] = cmd_name;
  argv[1] = NULL;

  run_child (argv, 1, NULL, tmpfile, NULL, NULL);	/* cubrid_rel */

  if ((infile = fopen (tmpfile, "r")) != NULL)
    {
      fgets (strbuf, 1024, infile);
      fgets (strbuf, 1024, infile);

      sscanf (strbuf, "%*s %*s %*s %s", buffer);
      fclose (infile);
      unlink (tmpfile);
    }

  return buffer;
}

int
ts_set_autoexec_query (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *conf_file, *temp_file;
  char line[MAX_JOB_CONFIG_FILE_LINE_LENGTH], tmpfile[512];
  char auto_exec_query_conf_file[512];
  char *conf_item[AUTOEXECQUERY_CONF_ENTRY_NUM];
  int i, index, length;
  char *name, *value;

  if ((conf_item[0] = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  if ((conf_item[2] = nv_get_val (req, "_ID")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database user");
      return ERR_PARAM_MISSING;
    }

  conf_get_dbmt_file (FID_AUTO_EXECQUERY_CONF, auto_exec_query_conf_file);
  nv_locate (req, "planlist", &index, &length);

  /* check query string length */
  for (i = index + 1; i < index + length; i += 6)
    {
      /* get query string */
      nv_lookup (req, i + 3, &name, &value);
      if (strcmp (name, "query_string") != 0)
	{
	  sprintf (_dbmt_error, "%s",
		   "nv order error. [i+3] must is [query_string]");
	  return ERR_WITH_MSG;
	}

      if (strlen (value) > MAX_AUTOQUERY_SCRIPT_SIZE)
	{
	  /* error handle */
	  sprintf (_dbmt_error,
		   "query script too long. do not exceed MAX_AUTOQUERY_SCRIPT_SIZE(%d) bytes.",
		   MAX_AUTOQUERY_SCRIPT_SIZE);
	  return ERR_WITH_MSG;
	}
    }

  conf_file = temp_file = 0;
  name = value = NULL;

  /* open conf file */
  if (access (auto_exec_query_conf_file, F_OK) == 0)
    {
      /* file is existed */
      conf_file = fopen (auto_exec_query_conf_file, "r");
      if (!conf_file)
	{
	  return ERR_FILE_OPEN_FAIL;
	}
    }
  else
    {
      conf_file = fopen (auto_exec_query_conf_file, "w+");
    }

  /* temp file open */
  sprintf (tmpfile, "%s/DBMT_task_045.%d", sco.dbmt_tmp_dir, (int) getpid ());
  temp_file = fopen (tmpfile, "w");

  if (temp_file == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  if (conf_file)
    {
      while (fgets (line, sizeof (line), conf_file))
	{
	  char username[DBMT_USER_NAME_LEN];
	  char db_name[64];
	  if (sscanf (line, "%s %*s %s", db_name, username) < 1)
	    continue;
	  if ((strcmp (username, conf_item[2]) != 0) ||
	      (strcmp (db_name, conf_item[0]) != 0))
	    {
	      /* write temp file if username or dbname is different */
	      fputs (line, temp_file);
	    }
	}
      fclose (conf_file);
    }

  fclose (temp_file);
  move_file (tmpfile, auto_exec_query_conf_file);

  if (length == 0)
    {
      /* open:planlist        *
       * close:planlist       */
      return ERR_NO_ERROR;
    }

  for (i = index + 1; i < index + length; i += 6)
    {
      /* open:queryplan */
      int mem_alloc = 0;

      if (value)
	if (strcasecmp (value, "planlist") == 0)
	  {
	    return ERR_NO_ERROR;
	  }

      temp_file = fopen (tmpfile, "w");
      if (temp_file == NULL)
	{
	  return ERR_TMPFILE_OPEN_FAIL;
	}

      nv_lookup (req, i, &name, &value);
      conf_item[1] = value;
      nv_lookup (req, i + 1, &name, &value);
      conf_item[3] = value;
      nv_lookup (req, i + 2, &name, &value);
      conf_item[4] = value;
      nv_lookup (req, i + 3, &name, &value);
      conf_item[5] = value;

      if (strcasecmp (conf_item[1], "NEW_PLAN") == 0)
	{
	  /* new query plan */
	  /* allocate newer number and input */
	  conf_item[1] = (char *) MALLOC (sizeof (char) * 5);
	  op_auto_exec_query_get_newplan_id (conf_item[1],
					     auto_exec_query_conf_file);
	  mem_alloc = 1;
	}

      if (access (auto_exec_query_conf_file, F_OK) == 0)
	{
	  /* file is existed */
	  conf_file = fopen (auto_exec_query_conf_file, "r");
	  if (!conf_file)
	    {
	      return ERR_FILE_OPEN_FAIL;
	    }
	}

      if (conf_file)
	{
	  while (fgets (line, sizeof (line), conf_file))
	    {
	      char query_id[64];
	      if (sscanf (line, "%*s %s ", query_id) < 1)
		continue;
	      if (strcmp (query_id, conf_item[1]) != 0)
		{
		  /* write temp file if query_id is different */
		  fputs (line, temp_file);
		}
	    }
	  fclose (conf_file);
	}

      fprintf (temp_file, "%s %s %s %s %s %s\n", conf_item[0], conf_item[1],
	       conf_item[2], conf_item[3], conf_item[4], conf_item[5]);

      if (mem_alloc)
	{
	  FREE_MEM (conf_item[1]);
	}

      fclose (temp_file);
      move_file (tmpfile, auto_exec_query_conf_file);
      /* close:queryplan */
    }

  return ERR_NO_ERROR;
}

int
ts_get_autoexec_query (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *conf_file;
  char buf[MAX_JOB_CONFIG_FILE_LINE_LENGTH];
  char auto_exec_query_conf_file[512];
  char id_num[64], db_name[64], user[64], period[8];
  char detail1[32], detail2[8], query_string[MAX_AUTOQUERY_SCRIPT_SIZE + 1],
    detail[64];
  char *dbname, *dbmt_username;

  conf_get_dbmt_file (FID_AUTO_EXECQUERY_CONF, auto_exec_query_conf_file);
  dbname = nv_get_val (req, "_DBNAME");
  dbmt_username = nv_get_val (req, "_ID");

  nv_add_nvp (res, "dbname", dbname);
  if (access (auto_exec_query_conf_file, F_OK) == 0)
    {
      conf_file = fopen (auto_exec_query_conf_file, "r");
      if (!conf_file)
	{
	  return ERR_FILE_OPEN_FAIL;
	}
    }
  else
    {
      nv_add_nvp (res, "open", "planlist");
      nv_add_nvp (res, "close", "planlist");
      return ERR_NO_ERROR;
    }

  nv_add_nvp (res, "open", "planlist");
  while (fgets (buf, sizeof (buf), conf_file))
    {
      int scan_matched;
      char *p;

      scan_matched =
	sscanf (buf, "%s %s %s %s %s %s", db_name, id_num, user, period,
		detail1, detail2);

      if (scan_matched != 6)
	continue;

      if (strcmp (dbname, db_name) != 0)
	continue;

      if (strcmp (dbmt_username, user) != 0)
	continue;

      nv_add_nvp (res, "open", "queryplan");
      nv_add_nvp (res, "query_id", id_num);
      nv_add_nvp (res, "period", period);

      sprintf (detail, "%s %s", detail1, detail2);
      nv_add_nvp (res, "detail", detail);

      p = strstr (buf, detail2);
      p = p + strlen (detail2) + 1;
      strcpy (query_string, p);
      query_string[strlen (query_string) - 1] = '\0';
      nv_add_nvp (res, "query_string", query_string);
      nv_add_nvp (res, "close", "queryplan");
    }

  nv_add_nvp (res, "close", "planlist");

  fclose (conf_file);
  return ERR_NO_ERROR;
}

static void
op_auto_exec_query_get_newplan_id (char *id_buf, char *conf_filename)
{
  FILE *conf_file;
  char buf[MAX_JOB_CONFIG_FILE_LINE_LENGTH], id_num[5];
  int index;

  for (index = 1; index < 10000; index++)
    {
      int used = 0;
      conf_file = fopen (conf_filename, "r");
      if (conf_file)
	{
	  while (fgets (buf, sizeof (buf), conf_file))
	    {
	      sscanf (buf, "%*s %s", id_num);
	      if (atoi (id_num) == index)
		{
		  used = 1;
		  break;
		}
	    }

	  if (used == 0)
	    {
	      sprintf (id_buf, "%04d", index);
	      fclose (conf_file);
	      break;
	    }
	  fclose (conf_file);
	}
      else
	{
	  strcpy (id_buf, "0001");
	  return;
	}
    }
}


#ifdef DIAG_DEVEL
int
ts_get_diaginfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  int ret_val;

  ret_val = uReadDiagSystemConfig (&(sco.diag_config), _dbmt_error);

  if (ret_val == -1)
    {
      /* error */
      if (_dbmt_error)
	return ERR_WITH_MSG;
      else
	return ERR_GENERAL_ERROR;
    }

  nv_add_nvp (res, "executediag",
	      ((sco.diag_config.Executediag == 1) ? "ON" : "OFF"));
  return ERR_NO_ERROR;
}

int
ts_get_diagdata (nvplist * cli_request, nvplist * cli_response,
		 char *diag_error)
{
  int retval, i;
  T_SHM_DIAG_INFO_SERVER *server_shm = NULL;
  void *br_shm, *as_shm;
  T_CLIENT_MONITOR_CONFIG c_config;
  T_DIAG_MONITOR_DB_VALUE server_result;
  T_DIAG_MONITOR_CAS_VALUE cas_result;
  char *start_time_sec, *start_time_usec, *dbname;
#if 0				/* ACTIVITY PROFILE */
  T_DIAG_ACTIVITY_DB_SERVER server_act_data;
  T_DIAG_ACTIVITY_CAS cas_act_data;
#endif
#ifdef WIN32
  HANDLE shm_handle = NULL;
#endif

  server_shm = (T_SHM_DIAG_INFO_SERVER *) (br_shm = as_shm = NULL);

#if 0				/* ACTIVITY PROFILE */
  INIT_SERVER_ACT_DATA (server_act_data);
  INIT_CAS_ACT_DATA (cas_act_data);
#endif

  dbname = nv_get_val (cli_request, "db_name");
  init_monitor_config (&c_config);
  retval = get_client_monitoring_config (cli_request, &c_config);
  start_time_sec = nv_get_val (cli_request, "start_time_sec");
  start_time_usec = nv_get_val (cli_request, "start_time_usec");

  if (dbname != NULL)
    {
      int server_shmid;
      char vol_dir[1024];

      if (get_dbvoldir (vol_dir, dbname, diag_error) != -1)
	{
	  server_shmid = getservershmid (vol_dir, dbname, diag_error);
	  if (server_shmid > 0)
	    server_shm = (T_SHM_DIAG_INFO_SERVER *)
	      OP_SERVER_SHM_OPEN (server_shmid, &shm_handle);
	}
    }

  if (server_shm && NEED_MON_CUB (c_config))
    {
      char mon_val[32];
      int numthread = server_shm->num_thread;
      init_diag_server_value (&server_result);
      for (i = 0; i < numthread; i++)
	{
	  server_result.query_open_page +=
	    server_shm->thread[i].query_open_page;
	  server_result.query_opened_page +=
	    server_shm->thread[i].query_opened_page;
	  server_result.query_slow_query +=
	    server_shm->thread[i].query_slow_query;
	  server_result.query_full_scan +=
	    server_shm->thread[i].query_full_scan;
	  server_result.conn_cli_request +=
	    server_shm->thread[i].conn_cli_request;
	  server_result.conn_aborted_clients +=
	    server_shm->thread[i].conn_aborted_clients;
	  server_result.conn_conn_req += server_shm->thread[i].conn_conn_req;
	  server_result.conn_conn_reject +=
	    server_shm->thread[i].conn_conn_reject;
	  server_result.buffer_page_write +=
	    server_shm->thread[i].buffer_page_write;
	  server_result.buffer_page_read +=
	    server_shm->thread[i].buffer_page_read;
	  server_result.lock_deadlock += server_shm->thread[i].lock_deadlock;
	  server_result.lock_request += server_shm->thread[i].lock_request;
	}

      nv_add_nvp (cli_response, "db_mon", "start");
      if (NEED_MON_CUB_QUERY (c_config))
	{
	  if (NEED_MON_CUB_QUERY_OPEN_PAGE (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.query_open_page);
	      nv_add_nvp (cli_response, "mon_cub_query_open_page", mon_val);
	    }
	  if (NEED_MON_CUB_QUERY_OPENED_PAGE (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.query_opened_page);
	      nv_add_nvp (cli_response, "mon_cub_query_opened_page", mon_val);
	    }
	  if (NEED_MON_CUB_QUERY_SLOW_QUERY (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.query_slow_query);
	      nv_add_nvp (cli_response, "mon_cub_query_slow_query", mon_val);
	    }
	  if (NEED_MON_CUB_QUERY_FULL_SCAN (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.query_full_scan);
	      nv_add_nvp (cli_response, "mon_cub_query_full_scan", mon_val);
	    }
	}

      if (NEED_MON_CUB_LOCK (c_config))
	{
	  if (NEED_MON_CUB_LOCK_DEADLOCK (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.lock_deadlock);
	      nv_add_nvp (cli_response, "mon_cub_lock_deadlock", mon_val);
	    }
	  if (NEED_MON_CUB_LOCK_REQUEST (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.lock_request);
	      nv_add_nvp (cli_response, "mon_cub_lock_request", mon_val);
	    }
	}

      if (NEED_MON_CUB_CONNECTION (c_config))
	{
	  if (NEED_MON_CUB_CONN_CLI_REQUEST (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.conn_cli_request);
	      nv_add_nvp (cli_response, "mon_cub_conn_cli_request", mon_val);
	    }
	  if (NEED_MON_CUB_CONN_ABORTED_CLIENTS (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.conn_aborted_clients);
	      nv_add_nvp (cli_response, "mon_cub_conn_aborted_clients",
			  mon_val);
	    }
	  if (NEED_MON_CUB_CONN_CONN_REQ (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.conn_conn_req);
	      nv_add_nvp (cli_response, "mon_cub_conn_conn_req", mon_val);
	    }
	  if (NEED_MON_CUB_CONN_CONN_REJECT (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.conn_conn_reject);
	      nv_add_nvp (cli_response, "mon_cub_conn_conn_reject", mon_val);
	    }
	}

      if (NEED_MON_CUB_BUFFER (c_config))
	{
	  if (NEED_MON_CUB_BUFFER_PAGE_WRITE (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.buffer_page_write);
	      nv_add_nvp (cli_response, "mon_cub_buffer_page_write", mon_val);
	    }
	  if (NEED_MON_CUB_BUFFER_PAGE_READ (c_config))
	    {
	      SET_LONGLONG_STR (mon_val, server_result.buffer_page_read);
	      nv_add_nvp (cli_response, "mon_cub_buffer_page_read", mon_val);
	    }
	}
      nv_add_nvp (cli_response, "db_mon", "end");
      OP_SERVER_SHM_DETACH (server_shm, shm_handle);
    }

  if (NEED_MON_CAS (c_config))
    {
      char mon_val[32];
      int num_broker, num_as, as_index;
      long long req[MAX_AS_COUNT], tran[MAX_AS_COUNT];

      init_diag_cas_value (&cas_result);

      br_shm = uca_broker_shm_open (diag_error);
      if (br_shm)
	{
	  num_broker = uca_get_br_num_with_opened_shm (br_shm, diag_error);
	  for (i = 0; i < num_broker; i++)
	    {
	      as_shm = uca_as_shm_open (br_shm, i, diag_error);
	      if (!as_shm)
		break;
	      num_as = uca_get_as_num_with_opened_shm (br_shm, i, diag_error);
	      if (uca_get_as_reqs_received_with_opened_shm
		  (as_shm, req, num_as, diag_error) == 1)
		{
		  if (uca_get_as_tran_processed_with_opened_shm
		      (as_shm, tran, num_as, diag_error) == 1)
		    {
		      for (as_index = 0; as_index < num_as; as_index++)
			{
			  cas_result.reqs_in_interval += req[as_index];
			  cas_result.transactions_in_interval +=
			    tran[as_index];
			}
		    }
		}
	      uca_shm_detach (as_shm);
	    }
	  cas_result.active_sessions =
	    uca_get_active_session_with_opened_shm (br_shm, diag_error);
	  uca_shm_detach (br_shm);
	}

      nv_add_nvp (cli_response, "cas_mon", "start");

      if (NEED_MON_CAS_REQ (c_config))
	{
	  SET_LONGLONG_STR (mon_val, cas_result.reqs_in_interval);
	  nv_add_nvp (cli_response, "cas_mon_req", mon_val);
	}
      if (NEED_MON_CAS_ACT_SESSION (c_config))
	{
	  sprintf (mon_val, "%d", cas_result.active_sessions);
	  nv_add_nvp (cli_response, "cas_mon_act_session", mon_val);
	}
      if (NEED_MON_CAS_TRAN (c_config))
	{
	  SET_LONGLONG_STR (mon_val, cas_result.transactions_in_interval);
	  nv_add_nvp (cli_response, "cas_mon_tran", mon_val);
	}

      nv_add_nvp (cli_response, "cas_mon", "end");
    }

#if 0				/* ACTIVITY_PROFILE */
  if (dbname && NEED_ACT_CUB (c_config))
    {
      if ((!start_time_sec) || (!start_time_usec))
	{
	  gettimeofday (&start_time, NULL);
	}
      else
	{
	  start_time.tv_sec = atol (start_time_sec);
	  start_time.tv_usec = atol (start_time_usec);
	}

      sprintf (filename, "%s/diag_log/%s_diag.log", sco.szCubrid, dbname);
      if (access (filename, F_OK) == 0)
	{
	  read_activity_data_from_file (&server_act_data, filename,
					start_time, DIAG_SERVER_DB);
	}
    }
  if (NEED_ACT_CAS (c_config))
    {
      int num_broker, num_as, as_index;
      char broker_name[MAX_BROKER_NAMELENGTH];

      if ((!start_time_sec) || (!start_time_usec))
	{
	  gettimeofday (&start_time, NULL);
	}
      else
	{
	  start_time.tv_sec = atol (start_time_sec);
	  start_time.tv_usec = atol (start_time_usec);
	}

      br_shm = uca_broker_shm_open (diag_error);
      if (br_shm)
	{
	  num_broker = uca_get_br_num_with_opened_shm (br_shm, diag_error);
	  for (i = 0; i < num_broker; i++)
	    {
	      memset (broker_name, '\0', sizeof (broker_name));
	      as_shm = uca_as_shm_open (br_shm, i, diag_error);
	      if (!as_shm)
		continue;
	      uca_get_br_name_with_opened_shm (br_shm, i, broker_name,
					       MAX_BROKER_NAMELENGTH,
					       diag_error);
	      num_as = uca_get_as_num_with_opened_shm (br_shm, i, diag_error);
	      uca_shm_detach (as_shm);
	      for (j = 0; j < num_as; j++)
		{
		  sprintf (filename, "%s/log/diag/diag_%s_%d.log",
			   sco.szCubrid, broker_name, j);
		  if (access (filename, F_OK) == 0)
		    {
		      read_activity_data_from_file (&cas_act_data, filename,
						    start_time,
						    DIAG_SERVER_CAS);
		    }
		}

	    }
	  uca_shm_detach (br_shm);
	}
    }

  gettimeofday (&start_time, NULL);
  sprintf (time_buf, "%ld", start_time.tv_sec);
  nv_add_nvp (cli_response, "start_time_sec", time_buf);
  sprintf (time_buf, "%ld", start_time.tv_usec);
  nv_add_nvp (cli_response, "start_time_usec", time_buf);

  if (dbname && NEED_ACT_CUB (c_config))
    {
      T_ACTIVITY_DATA *current_data;
      char integer_string[16], current_time[28];

      nv_add_nvp (cli_response, "cub_act", "start");
      if (NEED_ACT_CUB_QUERY_FULLSCAN (c_config))
	{
	  current_data = server_act_data.query_fullscan;
	  while (current_data)
	    {
	      nv_add_nvp (cli_response, "EventClass",
			  get_activity_event_type_str (current_data->
						       EventClass));
	      nv_add_nvp (cli_response, "TextData", current_data->TextData);
	      nv_add_nvp (cli_response, "BinaryData",
			  current_data->BinaryData);
	      sprintf (integer_string, "%d", current_data->IntegerData);
	      nv_add_nvp (cli_response, "IntegerData", integer_string);
	      sprintf (current_time, "%s",
		       ctime (&(current_data->Time.tv_sec)));
	      current_time[strlen (current_time) - 1] = '\0';
	      nv_add_nvp (cli_response, "Time", current_time);

	      current_data = current_data->next;
	    }
	}
      if (NEED_ACT_CUB_LOCK_DEADLOCK (c_config))
	{
	  current_data = server_act_data.lock_deadlock;
	  while (current_data)
	    {
	      nv_add_nvp (cli_response, "EventClass",
			  get_activity_event_type_str (current_data->
						       EventClass));
	      nv_add_nvp (cli_response, "TextData", current_data->TextData);
	      nv_add_nvp (cli_response, "BinaryData",
			  current_data->BinaryData);
	      sprintf (integer_string, "%d", current_data->IntegerData);
	      nv_add_nvp (cli_response, "IntegerData", integer_string);
	      sprintf (current_time, "%s",
		       ctime (&(current_data->Time.tv_sec)));
	      current_time[strlen (current_time) - 1] = '\0';
	      nv_add_nvp (cli_response, "Time", current_time);

	      current_data = current_data->next;
	    }
	}
      if (NEED_ACT_CUB_BUFFER_PAGE_READ (c_config))
	{
	  current_data = server_act_data.buffer_page_read;
	  while (current_data)
	    {
	      nv_add_nvp (cli_response, "EventClass",
			  get_activity_event_type_str (current_data->
						       EventClass));
	      nv_add_nvp (cli_response, "TextData", current_data->TextData);
	      nv_add_nvp (cli_response, "BinaryData",
			  current_data->BinaryData);
	      sprintf (integer_string, "%d", current_data->IntegerData);
	      nv_add_nvp (cli_response, "IntegerData", integer_string);
	      sprintf (current_time, "%s",
		       ctime (&(current_data->Time.tv_sec)));
	      current_time[strlen (current_time) - 1] = '\0';
	      nv_add_nvp (cli_response, "Time", current_time);

	      current_data = current_data->next;
	    }
	}
      if (NEED_ACT_CUB_BUFFER_PAGE_WRITE (c_config))
	{
	  current_data = server_act_data.buffer_page_write;
	  while (current_data)
	    {
	      nv_add_nvp (cli_response, "EventClass",
			  get_activity_event_type_str (current_data->
						       EventClass));
	      nv_add_nvp (cli_response, "TextData", current_data->TextData);
	      nv_add_nvp (cli_response, "BinaryData",
			  current_data->BinaryData);
	      sprintf (integer_string, "%d", current_data->IntegerData);
	      nv_add_nvp (cli_response, "IntegerData", integer_string);
	      sprintf (current_time, "%s",
		       ctime (&(current_data->Time.tv_sec)));
	      current_time[strlen (current_time) - 1] = '\0';
	      nv_add_nvp (cli_response, "Time", current_time);

	      current_data = current_data->next;
	    }
	}
      nv_add_nvp (cli_response, "cub_act", "end");
    }

  if (NEED_ACT_CAS (c_config))
    {
      T_ACTIVITY_DATA *current_data;
      char integer_string[16], current_time[28];

      nv_add_nvp (cli_response, "cas_act", "start");
      if (NEED_ACT_CAS_REQ (c_config))
	{
	  current_data = cas_act_data.request;
	  while (current_data)
	    {
	      nv_add_nvp (cli_response, "EventClass",
			  get_activity_event_type_str (current_data->
						       EventClass));
	      nv_add_nvp (cli_response, "TextData", current_data->TextData);
	      nv_add_nvp (cli_response, "BinaryData",
			  current_data->BinaryData);

	      sprintf (integer_string, "%d", current_data->IntegerData);
	      nv_add_nvp (cli_response, "IntegerData", integer_string);
	      sprintf (current_time, "%s",
		       ctime (&(current_data->Time.tv_sec)));
	      current_time[strlen (current_time) - 1] = '\0';
	      nv_add_nvp (cli_response, "Time", current_time);

	      current_data = current_data->next;
	    }
	}

      if (NEED_ACT_CAS_TRAN (c_config))
	{
	  current_data = cas_act_data.transaction;
	  while (current_data)
	    {
	      nv_add_nvp (cli_response, "EventClass",
			  get_activity_event_type_str (current_data->
						       EventClass));
	      nv_add_nvp (cli_response, "TextData", current_data->TextData);
	      nv_add_nvp (cli_response, "BinaryData",
			  current_data->BinaryData);

	      sprintf (integer_string, "%d", current_data->IntegerData);
	      nv_add_nvp (cli_response, "IntegerData", integer_string);

	      sprintf (current_time, "%s",
		       ctime (&(current_data->Time.tv_sec)));
	      current_time[strlen (current_time) - 1] = '\0';
	      nv_add_nvp (cli_response, "Time", current_time);

	      current_data = current_data->next;
	    }
	}

      nv_add_nvp (cli_response, "cas_act", "end");
    }
#endif

  return ERR_NO_ERROR;
}

#if 0				/* ACTIVITY_PROFILE */
int
ts_addactivitylog (nvplist * cli_request, nvplist * cli_response,
		   char *diag_error)
{
  FILE *outfile;
  char filepath[512];
  int retval, i;
  char *logname, *desc, *templatename, *startdate, *starttime, *enddate,
    *endtime;

  logname = nv_get_val (cli_request, "logname");
  desc = nv_get_val (cli_request, "desc");
  templatename = nv_get_val (cli_request, "templatename");
  startdate = nv_get_val (cli_request, "startdate");
  starttime = nv_get_val (cli_request, "starttime");
  enddate = nv_get_val (cli_request, "enddate");
  endtime = nv_get_val (cli_request, "endtime");

  if (!logname || !templatename || !startdate || !starttime || !enddate
      || !endtime)
    return ERR_PARAM_MISSING;

  /* write information with log configration file */
  conf_get_dbmt_file (FID_DIAG_ACTIVITY_LOG, filepath);
  if ((outfile = fopen (filepath, "a+")) == NULL)
    {
      strcpy (diag_error, filepath);
      return ERR_FILE_OPEN_FAIL;
    }

  fprintf (outfile, "%s ", logname);
  fprintf (outfile, "%s ", templatename);
  fprintf (outfile, "%s ", startdate);
  fprintf (outfile, "%s ", starttime);
  fprintf (outfile, "%s ", enddate);
  fprintf (outfile, "%s ", endtime);
  if (desc)
    fprintf (outfile, "%s\n", desc);

  fclose (outfile);

  return ERR_NO_ERROR;
}

int
ts_updateactivitylog (nvplist * cli_request, nvplist * cli_response,
		      char *diag_error)
{
  FILE *conffile, *tmpfile;
  char filepath[512], tmpfilepath[512], buf[1024];
  int retval, i;
  char *logname, *desc, *templatename, *startdate, *starttime, *enddate,
    *endtime;

  logname = nv_get_val (cli_request, "logname");
  desc = nv_get_val (cli_request, "desc");
  templatename = nv_get_val (cli_request, "templatename");
  startdate = nv_get_val (cli_request, "startdate");
  starttime = nv_get_val (cli_request, "starttime");
  enddate = nv_get_val (cli_request, "enddate");
  endtime = nv_get_val (cli_request, "endtime");

  if (!logname || !templatename || !startdate || !starttime || !enddate
      || !endtime)
    return ERR_PARAM_MISSING;

  conf_get_dbmt_file (FID_DIAG_ACTIVITY_LOG, filepath);
  if ((conffile = fopen (filepath, "a+")) == NULL)
    {
      strcpy (diag_error, filepath);
      return ERR_FILE_OPEN_FAIL;
    }

  sprintf (tmpfilepath, "%s/activitylog_update_%d.tmp", sco.dbmt_tmp_dir,
	   getpid ());
  if ((tmpfile = fopen (tmpfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tmpfilepath);
	}
      fclose (conffile);
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), conffile))
    {
      char *p;
      p = strchr (buf, ' ');
      if (p && strncmp (logname, buf, p - buf) == 0)
	{
	  fprintf (tmpfile, "%s ", logname);
	  fprintf (tmpfile, "%s ", templatename);
	  fprintf (tmpfile, "%s ", startdate);
	  fprintf (tmpfile, "%s ", starttime);
	  fprintf (tmpfile, "%s ", enddate);
	  fprintf (tmpfile, "%s ", endtime);
	  if (desc)
	    fprintf (tmpfile, "%s\n", desc);
	  continue;
	}

      fprintf (tmpfile, "%s", buf);
    }

  fclose (tmpfile);
  fclose (conffile);

  unlink (filepath);
  rename (tmpfilepath, filepath);

  return ERR_NO_ERROR;
}

int
ts_removeactivitylog (nvplist * cli_request, nvplist * cli_response,
		      char *diag_error)
{
  FILE *conffile, *tmpfile;
  char filepath[512], tmpfilepath[512], buf[1024];
  int retval, i;
  char *logname;

  logname = nv_get_val (cli_request, "logname");

  if (!logname)
    return ERR_PARAM_MISSING;

  conf_get_dbmt_file (FID_DIAG_ACTIVITY_LOG, filepath);
  if ((conffile = fopen (filepath, "a+")) == NULL)
    {
      strcpy (diag_error, filepath);
      return ERR_FILE_OPEN_FAIL;
    }

  sprintf (tmpfilepath, "%s/activitylog_remove_%d.tmp", sco.dbmt_tmp_dir,
	   getpid ());
  if ((tmpfile = fopen (tmpfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tmpfilepath);
	}
      fclose (conffile);
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), conffile))
    {
      char *p;
      p = strchr (buf, ' ');
      if (p && strncmp (logname, buf, p - buf) == 0)
	continue;

      fprintf (tmpfile, "%s", buf);
    }

  fclose (tmpfile);
  fclose (conffile);

  unlink (filepath);
  rename (tmpfilepath, filepath);

  return ERR_NO_ERROR;
}

int
ts_getactivitylog (nvplist * cli_request, nvplist * cli_response,
		   char *diag_error)
{
  FILE *conffile, *tmpfile;
  char filepath[512], tmpfilepath[512], buf[1024];
  int retval, i;

  nv_add_nvp (cli_response, "loglist", "start");

  conf_get_dbmt_file (FID_DIAG_ACTIVITY_LOG, filepath);
  if ((conffile = fopen (filepath, "r")) == NULL)
    {
      nv_add_nvp (cli_response, "loglist", "end");
      return ERR_NO_ERROR;
    }

  while (fgets (buf, sizeof (buf), conffile))
    {
      char *p, *q;
      char tmp_buf[512];

      nv_add_nvp (cli_response, "log", "start");
      /* logname */
      p = strchr (buf, ' ');
      if (p)
	{
	  strncpy (tmp_buf, q, p - q);
	  buf[p - q] = '\0';
	  nv_add_nvp (cli_response, "logname", tmp_buf);
	  q = p + 1;
	}

      /* template name */
      p = strchr (q, ' ');
      if (p)
	{
	  strncpy (tmp_buf, q, p - q);
	  buf[p - q] = '\0';
	  nv_add_nvp (cli_response, "templatename", tmp_buf);
	  q = p + 1;
	}

      /* startdate */
      p = strchr (q, ' ');
      if (p)
	{
	  strncpy (tmp_buf, q, p - q);
	  buf[p - q] = '\0';
	  nv_add_nvp (cli_response, "startdate", tmp_buf);
	  q = p + 1;
	}

      /* starttime */
      p = strchr (q, ' ');
      if (p)
	{
	  strncpy (tmp_buf, q, p - q);
	  buf[p - q] = '\0';
	  nv_add_nvp (cli_response, "starttime", tmp_buf);
	  p = q + 1;
	}

      /* enddate */
      p = strchr (q, ' ');
      if (p)
	{
	  strncpy (tmp_buf, q, p - q);
	  buf[p - q] = '\0';
	  nv_add_nvp (cli_response, "enddate", tmp_buf);
	  q = p + 1;
	}

      /* endtime */
      p = strchr (q, ' ');
      if (p)
	{
	  strncpy (tmp_buf, q, p - q);
	  buf[p - q] = '\0';
	  nv_add_nvp (cli_response, "endtime", tmp_buf);
	  nv_add_nvp (cli_response, "desc", p + 1);
	}

      nv_add_nvp (cli_response, "log", "end");
    }
  nv_add_nvp (cli_response, "loglist", "end");

  fclose (tmpfile);
  fclose (conffile);

  unlink (filepath);
  rename (tmpfilepath, filepath);

  return ERR_NO_ERROR;
}
#endif

int
ts_addstatustemplate (nvplist * cli_request, nvplist * cli_response,
		      char *diag_error)
{
  FILE *templatefile, *tempfile;
  char templatefilepath[512], tempfilepath[512];
  char buf[1024];
  char *templatename, *desc, *sampling_term, *dbname;
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");
  desc = nv_get_val (cli_request, "desc");
  sampling_term = nv_get_val (cli_request, "sampling_term");
  dbname = nv_get_val (cli_request, "db_name");

  if (templatename == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_STATUS_TEMPLATE, templatefilepath);
  if (access (templatefilepath, F_OK) < 0)
    {
      templatefile = fopen (templatefilepath, "w");
      if (templatefile == NULL)
	{
	  if (diag_error)
	    strcpy (diag_error, templatefilepath);
	  return ERR_FILE_OPEN_FAIL;
	}
      fclose (templatefile);
    }

  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      if (diag_error)
	strcpy (diag_error, templatefilepath);
      return ERR_FILE_OPEN_FAIL;
    }

  sprintf (tempfilepath, "%s/statustemplate_add.tmp", sco.dbmt_tmp_dir);
  if ((tempfile = fopen (tempfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tempfilepath);
	  fclose (templatefile);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  fprintf (tempfile, "%s", buf);

	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    break;

	  buf[strlen (buf) - 1] = '\0';
	  if (strcmp (buf, templatename) == 0)
	    {
	      strcpy (diag_error, templatename);
	      ret_val = ERR_TEMPLATE_ALREADY_EXIST;
	      break;
	    }

	  fprintf (tempfile, "%s\n", buf);

	  /* copy others */
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      fprintf (tempfile, "%s", buf);
	      if (strncmp (buf, ">>>", 3) == 0)
		break;
	    }
	}
    }

  if (ret_val == ERR_NO_ERROR)
    {
      int i, config_index, config_length;
      char *target_name, *target_value;

      /* add new template config */
      fprintf (tempfile, "<<<\n");
      fprintf (tempfile, "%s\n", templatename);
      if (desc)
	fprintf (tempfile, "%s\n", desc);
      else
	fprintf (tempfile, " \n");

      if (dbname)
	fprintf (tempfile, "%s\n", dbname);
      else
	fprintf (tempfile, " \n");

      fprintf (tempfile, "%s\n", sampling_term);

      if (nv_locate
	  (cli_request, "target_config", &config_index, &config_length) != 1)
	ret_val = ERR_REQUEST_FORMAT;
      else
	{
	  for (i = config_index; i < config_index + config_length; i++)
	    {
	      if (nv_lookup (cli_request, i, &target_name, &target_value) ==
		  1)
		{
		  fprintf (tempfile, "%s %s\n", target_name, target_value);
		}
	      else
		{
		  ret_val = ERR_REQUEST_FORMAT;
		  break;
		}

	    }

	  fprintf (tempfile, ">>>\n");
	}
    }

  fclose (tempfile);
  fclose (templatefile);

  if (ret_val == ERR_NO_ERROR)
    {
      unlink (templatefilepath);
      rename (tempfilepath, templatefilepath);
    }
  else
    {
      unlink (tempfilepath);
    }

  return ret_val;
}

int
ts_removestatustemplate (nvplist * cli_request, nvplist * cli_response,
			 char *diag_error)
{
  FILE *templatefile, *tempfile;
  char templatefilepath[512], tempfilepath[512];
  char buf[1024];
  char *templatename;
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");

  if (templatename == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_STATUS_TEMPLATE, templatefilepath);
  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, templatefilepath);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  sprintf (tempfilepath, "%s/statustemplate_remove.tmp", sco.dbmt_tmp_dir);
  if ((tempfile = fopen (tempfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tempfilepath);
	  fclose (templatefile);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    break;
	  buf[strlen (buf) - 1] = '\0';
	  if (strcmp (buf, templatename) == 0)
	    {
	      continue;
	    }

	  fprintf (tempfile, "<<<\n");
	  fprintf (tempfile, "%s\n", buf);

	  /* copy others */
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      fprintf (tempfile, "%s", buf);
	      if (strncmp (buf, ">>>", 3) == 0)
		break;
	    }
	}
    }

  fclose (tempfile);
  fclose (templatefile);

  if (ret_val == ERR_NO_ERROR)
    {
      unlink (templatefilepath);
      rename (tempfilepath, templatefilepath);
    }
  else
    {
      unlink (tempfilepath);
    }

  return ret_val;
}

#if 0				/* ACTIVITY_PROFILE */
int
ts_removeactivitytemplate (nvplist * cli_request, nvplist * cli_response,
			   char *diag_error)
{
  FILE *templatefile, *tempfile;
  char templatefilepath[512], tempfilepath[512];
  char buf[1024];
  char *templatename;
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");

  if (templatename == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_ACTIVITY_TEMPLATE, templatefilepath);
  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, templatefilepath);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  sprintf (tempfilepath, "%s/activitytemplate_remove_%d.tmp",
	   sco.dbmt_tmp_dir, getpid ());
  if ((tempfile = fopen (tempfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tempfilepath);
	  fclose (templatefile);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    break;
	  buf[strlen (buf) - 1] = '\0';
	  if (strcmp (buf, templatename) == 0)
	    {
	      continue;
	    }

	  fprintf (tempfile, "<<<\n");
	  fprintf (tempfile, "%s\n", buf);

	  /* copy others */
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      fprintf (tempfile, "%s", buf);
	      if (strncmp (buf, ">>>", 3) == 0)
		break;
	    }
	}
    }

  fclose (tempfile);
  fclose (templatefile);

  if (ret_val == ERR_NO_ERROR)
    {
      unlink (templatefilepath);
      rename (tempfilepath, templatefilepath);
    }
  else
    {
      unlink (tempfilepath);
    }

  return ret_val;
}
#endif

int
ts_updatestatustemplate (nvplist * cli_request, nvplist * cli_response,
			 char *diag_error)
{
  FILE *templatefile, *tempfile;
  char templatefilepath[512], tempfilepath[512];
  char buf[1024];
  char *templatename, *desc, *sampling_term, *dbname;
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");
  desc = nv_get_val (cli_request, "desc");
  sampling_term = nv_get_val (cli_request, "sampling_term");
  dbname = nv_get_val (cli_request, "db_name");

  if (templatename == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_STATUS_TEMPLATE, templatefilepath);
  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, templatefilepath);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  sprintf (tempfilepath, "%s/statustemplate_update_%d.tmp", sco.dbmt_tmp_dir,
	   getpid ());
  if ((tempfile = fopen (tempfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tempfilepath);
	  fclose (templatefile);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  fprintf (tempfile, "%s", buf);

	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    break;

	  buf[strlen (buf) - 1] = '\0';
	  if (strcmp (buf, templatename) == 0)
	    {
	      int i, config_index, config_length;
	      char *target_name, *target_value;

	      /* add new configuration */
	      fprintf (tempfile, "%s\n", templatename);
	      if (desc)
		fprintf (tempfile, "%s\n", desc);
	      else
		fprintf (tempfile, " \n");

	      if (dbname)
		fprintf (tempfile, "%s\n", dbname);
	      else
		fprintf (tempfile, " \n");

	      fprintf (tempfile, "%s\n", sampling_term);

	      if (nv_locate
		  (cli_request, "target_config", &config_index,
		   &config_length) != 1)
		{
		  ret_val = ERR_REQUEST_FORMAT;
		  break;
		}
	      else
		{
		  for (i = config_index; i < config_index + config_length;
		       i++)
		    {
		      if (nv_lookup
			  (cli_request, i, &target_name, &target_value) == 1)
			{
			  fprintf (tempfile, "%s %s\n", target_name,
				   target_value);
			}
		      else
			{
			  ret_val = ERR_REQUEST_FORMAT;
			  break;
			}
		    }
		  if (ret_val != ERR_NO_ERROR)
		    break;

		  fprintf (tempfile, ">>>\n");
		}

	      continue;
	    }

	  fprintf (tempfile, "%s\n", buf);

	  /* copy others */
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      fprintf (tempfile, "%s", buf);
	      if (strncmp (buf, ">>>", 3) == 0)
		break;
	    }
	}
    }

  fclose (tempfile);
  fclose (templatefile);

  if (ret_val == ERR_NO_ERROR)
    {
      unlink (templatefilepath);
      rename (tempfilepath, templatefilepath);
    }
  else
    {
      unlink (tempfilepath);
    }

  return ret_val;
}

#if 0				/* ACTIVITY_PROFILE */
int
ts_updateactivitytemplate (nvplist * cli_request, nvplist * cli_response,
			   char *diag_error)
{
  FILE *templatefile, *tempfile;
  char templatefilepath[512], tempfilepath[512];
  char buf[1024];
  char *templatename, *desc, *sampling_term, *dbname;
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");
  desc = nv_get_val (cli_request, "desc");
  dbname = nv_get_val (cli_request, "db_name");
  sampling_term = nv_get_val (cli_request, "sampling_term");

  if (templatename == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_ACTIVITY_TEMPLATE, templatefilepath);
  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, templatefilepath);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  sprintf (tempfilepath, "%s/activitytemplate_add.tmp", sco.dbmt_tmp_dir);
  if ((tempfile = fopen (tempfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tempfilepath);
	  fclose (templatefile);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  fprintf (tempfile, "%s", buf);

	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    break;

	  buf[strlen (buf) - 1] = '\0';
	  if (strcmp (buf, templatename) == 0)
	    {
	      int i, config_index, config_length;
	      char *target_name, *target_value;

	      /* add new configuration */
	      fprintf (tempfile, "%s\n", templatename);
	      if (desc)
		fprintf (tempfile, "%s\n", desc);
	      else
		fprintf (tempfile, " \n");

	      if (dbname)
		fprintf (tempfile, "%s\n", dbname);
	      else
		fprintf (tempfile, " \n");

	      if (nv_locate
		  (cli_request, "target_config", &config_index,
		   &config_length) != 1)
		ret_val = ERR_REQUEST_FORMAT;
	      else
		{
		  for (i = config_index; i < config_index + config_length;
		       i++)
		    {
		      if (nv_lookup
			  (cli_request, i, &target_name, &target_value) == 1)
			{
			  if (strcasecmp (target_value, "yes") == 0)
			    fprintf (tempfile, "%s\n", target_name);
			}
		      else
			{
			  ret_val = ERR_REQUEST_FORMAT;
			  break;
			}
		    }
		  fprintf (tempfile, ">>>\n");
		}
	      continue;
	    }

	  fprintf (tempfile, "%s\n", buf);

	  /* copy others */
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      fprintf (tempfile, "%s", buf);
	      if (strncmp (buf, ">>>", 3) == 0)
		break;
	    }
	}
    }

  fclose (tempfile);
  fclose (templatefile);

  if (ret_val == ERR_NO_ERROR)
    {
      unlink (templatefilepath);
      rename (tempfilepath, templatefilepath);
    }
  else
    {
      unlink (tempfilepath);
    }

  return ret_val;
}

int
ts_addactivitytemplate (nvplist * cli_request, nvplist * cli_response,
			char *diag_error)
{
  FILE *templatefile, *tempfile;
  char templatefilepath[512], tempfilepath[512];
  char buf[1024];
  char *templatename, *desc, *dbname;
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");
  desc = nv_get_val (cli_request, "desc");
  dbname = nv_get_val (cli_request, "db_name");

  if (templatename == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_ACTIVITY_TEMPLATE, templatefilepath);
  if (access (templatefilepath, F_OK) < 0)
    {
      templatefile = fopen (templatefilepath, "w");
      if (templatefile == NULL)
	{
	  if (diag_error)
	    strcpy (diag_error, templatefilepath);
	  return ERR_FILE_OPEN_FAIL;
	}
      fclose (templatefile);
    }

  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, templatefilepath);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  sprintf (tempfilepath, "%s/activitytemplate_add.tmp", sco.dbmt_tmp_dir);
  if ((tempfile = fopen (tempfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tempfilepath);
	  fclose (templatefile);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  fprintf (tempfile, "%s", buf);

	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    break;

	  buf[strlen (buf) - 1] = '\0';
	  if (strcmp (buf, templatename) == 0)
	    {
	      strcpy (diag_error, templatename);
	      ret_val = ERR_TEMPLATE_ALREADY_EXIST;
	      break;
	    }

	  fprintf (tempfile, "%s\n", buf);

	  /* copy others */
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      fprintf (tempfile, "%s", buf);
	      if (strncmp (buf, ">>>", 3) == 0)
		break;
	    }
	}
    }

  if (ret_val == ERR_NO_ERROR)
    {
      int i, config_index, config_length;
      char *target_name, *target_value;

      /* add new template configuration */
      fprintf (tempfile, "<<<\n");
      fprintf (tempfile, "%s\n", templatename);
      if (desc)
	fprintf (tempfile, "%s\n", desc);
      else
	fprintf (tempfile, " \n");

      if (dbname)
	fprintf (tempfile, "%s\n", dbname);
      else
	fprintf (tempfile, " \n");

      if (nv_locate
	  (cli_request, "target_config", &config_index, &config_length) != 1)
	ret_val = ERR_REQUEST_FORMAT;
      else
	{
	  for (i = config_index; i < config_index + config_length; i++)
	    {
	      if (nv_lookup (cli_request, i, &target_name, &target_value) ==
		  1)
		{
		  if (strcasecmp (target_value, "yes") == 0)
		    fprintf (tempfile, "%s\n", target_name);
		}
	      else
		{
		  ret_val = ERR_REQUEST_FORMAT;
		  break;
		}

	    }

	  fprintf (tempfile, ">>>\n");
	}
    }

  fclose (tempfile);
  fclose (templatefile);

  if (ret_val == ERR_NO_ERROR)
    {
      unlink (templatefilepath);
      rename (tempfilepath, templatefilepath);
    }
  else
    {
      unlink (tempfilepath);
    }

  return ret_val;
}
#endif /* ACTIVITY PROFILE */

int
ts_getstatustemplate (nvplist * cli_request, nvplist * cli_response,
		      char *diag_error)
{
  FILE *templatefile;
  char templatefilepath[512];
  char buf[1024];
  char *templatename;
  char targetname[100], targetcolor[8], targetmag[32];
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_STATUS_TEMPLATE, templatefilepath);
  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      return ERR_NO_ERROR;
    }

  nv_add_nvp (cli_response, "start", "templatelist");
  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    break;
	  buf[strlen (buf) - 1] = '\0';
	  if (templatename)
	    {
	      if (strcmp (buf, templatename) != 0)
		{
		  continue;
		}
	    }

	  nv_add_nvp (cli_response, "start", "template");
	  nv_add_nvp (cli_response, "name", buf);
	  if (!fgets (buf, sizeof (buf), templatefile))
	    {
	      ret_val = ERR_WITH_MSG;
	      if (diag_error)
		{
		  strcpy (diag_error, "Invalid file format\n");
		  strcat (diag_error, templatefilepath);
		}

	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  nv_add_nvp (cli_response, "desc", buf);

	  if (!fgets (buf, sizeof (buf), templatefile))
	    {
	      ret_val = ERR_WITH_MSG;
	      if (diag_error)
		{
		  strcpy (diag_error, "Invalid file format\n");
		  strcat (diag_error, templatefilepath);
		}

	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  nv_add_nvp (cli_response, "db_name", buf);

	  if (!fgets (buf, sizeof (buf), templatefile))
	    {
	      ret_val = ERR_WITH_MSG;
	      if (diag_error)
		{
		  strcpy (diag_error, "Invalid file format\n");
		  strcat (diag_error, templatefilepath);
		}

	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  nv_add_nvp (cli_response, "sampling_term", buf);

	  nv_add_nvp (cli_response, "start", "target_config");
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      int matched;
	      if (strncmp (buf, ">>>", 3) == 0)
		break;

	      matched =
		sscanf (buf, "%s %s %s", targetname, targetcolor, targetmag);
	      if (matched != 3)
		continue;	/* error file format */
	      nv_add_nvp (cli_response, targetname, targetcolor);
	      nv_add_nvp (cli_response, targetname, targetmag);
	    }
	  nv_add_nvp (cli_response, "end", "target_config");

	  nv_add_nvp (cli_response, "end", "template");
	}
    }
  nv_add_nvp (cli_response, "end", "templatelist");

  fclose (templatefile);

  return ret_val;
}

#if 0				/* ACTIVITY_PROFILE */
int
ts_getactivitytemplate (nvplist * cli_request, nvplist * cli_response,
			char *diag_error)
{
  FILE *templatefile;
  char templatefilepath[512], tempfilepath[512];
  char buf[1024];
  char *templatename;
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_ACTIVITY_TEMPLATE, templatefilepath);
  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      return ERR_NO_ERROR;
    }

  nv_add_nvp (cli_response, "start", "templatelist");
  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    break;
	  buf[strlen (buf) - 1] = '\0';
	  if (templatename)
	    {
	      if (strcmp (buf, templatename) != 0)
		{
		  continue;
		}
	    }

	  nv_add_nvp (cli_response, "start", "template");
	  nv_add_nvp (cli_response, "name", buf);

	  if (!fgets (buf, sizeof (buf), templatefile))
	    {
	      ret_val = ERR_WITH_MSG;
	      if (diag_error)
		{
		  strcpy (diag_error, "Invalid file format\n");
		  strcat (diag_error, templatefilepath);
		}

	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  nv_add_nvp (cli_response, "desc", buf);

	  if (!fgets (buf, sizeof (buf), templatefile))
	    {
	      ret_val = ERR_WITH_MSG;
	      if (diag_error)
		{
		  strcpy (diag_error, "Invalid file format\n");
		  strcat (diag_error, templatefilepath);
		}

	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  nv_add_nvp (cli_response, "db_name", buf);

	  nv_add_nvp (cli_response, "start", "target_config");
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      if (strncmp (buf, ">>>", 3) == 0)
		break;
	      buf[strlen (buf) - 1] = '\0';
	      nv_add_nvp (cli_response, buf, "yes");
	    }
	  nv_add_nvp (cli_response, "end", "target_config");

	  nv_add_nvp (cli_response, "end", "template");
	}
    }
  nv_add_nvp (cli_response, "end", "templatelist");

  fclose (templatefile);

  return ret_val;

}
#endif /* ACTIVITY PROFILE */

int
ts_getcaslogfilelist (nvplist * cli_request, nvplist * cli_response,
		      char *diag_error)
{
  int i;
#ifdef WIN32
  HANDLE handle;
  WIN32_FIND_DATA data;
  char find_file[512];
  int found;
#else
  DIR *dp;
  struct dirent *dirp;
#endif
  T_DM_UC_CONF uc_conf;
  char logdir[512], *v;
  char *bname, buf[1024];
  char *cur_file;

  /* get cas info num */
  if (uca_unicas_conf (&uc_conf, NULL, diag_error) < 0)
    return ERR_WITH_MSG;

  nv_add_nvp (cli_response, "logfilelist", "start");
  for (i = 0; i < uc_conf.num_broker; i++)
    {
      bname = uca_conf_find (&(uc_conf.br_conf[i]), "%");
      if (!bname)
	continue;

      v = uca_conf_find (uca_conf_find_broker (&uc_conf, bname), "LOG_DIR");
      if (v == NULL)
	v = "log";
      uca_get_conf_path (v, logdir);

      nv_add_nvp (cli_response, "brokername", bname);
#ifdef WIN32
      sprintf (find_file, "%s/%s/%s*.log", bname, UNICAS_SQL_LOG_DIR, logdir);
      handle = FindFirstFile (find_file, &data);
      if (handle == INVALID_HANDLE_VALUE)
	continue;
#else
      sprintf (logdir, "%s/%s", logdir, UNICAS_SQL_LOG_DIR);
      dp = opendir (logdir);
      if (dp == NULL)
	continue;
#endif

#ifdef WIN32
      for (found = 1; found; found = FindNextFile (handle, &data))
#else
      while ((dirp = readdir (dp)) != NULL)
#endif
	{
#ifdef WIN32
	  cur_file = data.cFileName;
#else
	  cur_file = dirp->d_name;
#endif
	  if (strstr (cur_file, bname) != NULL)
	    {
	      sprintf (buf, "%s/%s", logdir, cur_file);
	      nv_add_nvp (cli_response, "logfile", buf);
	    }
	}
    }
  nv_add_nvp (cli_response, "logfilelist", "end");
  uca_unicas_conf_free (&uc_conf);
#ifdef WIN32
  FindClose (handle);
#else
  closedir (dp);
#endif

#ifdef DIAG_DEBUG
  nv_writeto (cli_response,
	      "/home2/lsj1888/CUBRID/CUBRID_MANAGER/bin/res_file/ts_getcaslogfilelist.res");
#endif

  return ERR_NO_ERROR;
}

int
ts_analyzecaslog (nvplist * cli_request, nvplist * cli_response,
		  char *diag_error)
{
  int retval, i, arg_index;
  int matched, sect, sect_len;
  char tmpfileQ[512], tmpfileRes[512], tmpfileT[512],
    tmpfileanalyzeresult[512];
  char *logfile, *option_t;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *diag_err_file;
  char *argv[256];
  char buf[1024], logbuf[2048];
  char qnum[16], max[32], min[32], avg[32], cnt[16], err[16];
  FILE *fdRes, *fdQ, *fdT, *fdAnalyzeResult;
#ifdef WIN32
  DWORD th_id;
#else
  T_THREAD th_id;
#endif

  logfile = nv_get_val (cli_request, "logfile");
  option_t = nv_get_val (cli_request, "option_t");

  /* set prarameter with logfile and execute broker_log_top */
  /* execute at current directory and copy result to $CUBRID/tmp directory */
  sprintf (cmd_name, "%s/bin/broker_log_top%s", sco.szCubrid, DBMT_EXE_EXT);
  arg_index = 0;
  argv[arg_index++] = cmd_name;
  if (option_t && !strcmp (option_t, "yes"))
    {
      argv[arg_index++] = "-t";
    }
  nv_locate (cli_request, "logfilelist", &sect, &sect_len);
  if (sect == -1)
    {
      return ERR_PARAM_MISSING;
    }
  for (i = 0; i < sect_len; i++)
    {
      nv_lookup (cli_request, sect + i, NULL, &logfile);
      if (logfile)
	argv[arg_index++] = logfile;
    }
  argv[arg_index++] = NULL;
  INIT_CUBRID_ERROR_FILE (diag_err_file);

  retval = run_child (argv, 1, NULL, NULL, diag_err_file, NULL);	/* broker_log_top */
  if (read_error_file (diag_err_file, diag_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;

  if (retval < 0)
    {
      if (diag_error)
	strcpy (diag_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  sprintf (tmpfileanalyzeresult, "%s/analyzelog_%d.res", sco.dbmt_tmp_dir,
	   (int) getpid ());
  fdAnalyzeResult = fopen (tmpfileanalyzeresult, "w+");
  if (fdAnalyzeResult == NULL)
    {
      if (diag_error)
	strcpy (diag_error, "Tmpfile");
      return ERR_FILE_OPEN_FAIL;
    }

  if (!strcmp (option_t, "yes"))
    {
      int log_init_flag, log_index;

      sprintf (tmpfileT, "%s/log_top_%d.t", sco.dbmt_tmp_dir,
	       (int) getpid ());
      rename ("./log_top.t", tmpfileT);

      fdT = fopen (tmpfileT, "r");
      if (fdT == NULL)
	{
	  if (diag_error)
	    strcpy (diag_error, "log_top.t");
	  return ERR_FILE_OPEN_FAIL;
	}

      log_index = 1;
      log_init_flag = 1;

      nv_add_nvp (cli_response, "resultlist", "start");
      while (fgets (buf, 1024, fdT))
	{
	  if (strlen (buf) == 1)
	    continue;

	  if (log_init_flag == 1)
	    {
	      nv_add_nvp (cli_response, "result", "start");
	      sprintf (qnum, "[Q%d]", log_index);
	      fprintf (fdAnalyzeResult, "%s\n", qnum);
	      nv_add_nvp (cli_response, "qindex", qnum);
	      log_index++;
	      log_init_flag = 0;
	    }

	  if (!strncmp (buf, "***", 3))
	    {
	      buf[strlen (buf) - 1] = '\0';
	      nv_add_nvp (cli_response, "exec_time", buf + 4);
	      nv_add_nvp (cli_response, "result", "end");
	      log_init_flag = 1;
	    }
	  else
	    {
	      fprintf (fdAnalyzeResult, "%s", buf);
	    }
	}

      nv_add_nvp (cli_response, "resultlist", "end");

      fclose (fdT);
      unlink (tmpfileT);
    }
  else
    {
#ifdef WIN32
      th_id = GetCurrentThreadId ();
#else
      th_id = getpid ();
#endif
      sprintf (tmpfileQ, "%s/log_top_%u.q", sco.dbmt_tmp_dir, th_id);
      sprintf (tmpfileRes, "%s/log_top_%u.res", sco.dbmt_tmp_dir, th_id);

      rename ("./log_top.q", tmpfileQ);
      rename ("./log_top.res", tmpfileRes);

      fdQ = fopen (tmpfileQ, "r");
      if (fdQ == NULL)
	{
	  if (diag_error)
	    strcpy (diag_error, "log_top.q");
	  return ERR_FILE_OPEN_FAIL;
	}

      fdRes = fopen (tmpfileRes, "r");
      if (fdRes == NULL)
	{
	  if (diag_error)
	    strcpy (diag_error, "log_top.res");
	  return ERR_FILE_OPEN_FAIL;
	}

      memset (buf, '\0', 1024);
      memset (logbuf, '\0', 2048);

      nv_add_nvp (cli_response, "resultlist", "start");
      /* read result, log file and create msg with them */
      while (fgets (buf, 1024, fdRes))
	{
	  if (strlen (buf) == 1)
	    continue;

	  if (!strncmp (buf, "[Q", 2))
	    {
	      nv_add_nvp (cli_response, "result", "start");

	      matched =
		sscanf (buf, "%s %s %s %s %s %s", qnum, max, min, avg, cnt,
			err);
	      if (matched != 6)
		continue;
	      nv_add_nvp (cli_response, "qindex", qnum);
	      nv_add_nvp (cli_response, "max", max);
	      nv_add_nvp (cli_response, "min", min);
	      nv_add_nvp (cli_response, "avg", avg);
	      nv_add_nvp (cli_response, "cnt", cnt);
	      err[strlen (err) - 1] = '\0';
	      nv_add_nvp (cli_response, "err", err + 1);

	      fprintf (fdAnalyzeResult, "%s\n", qnum);

	      while (strncmp (logbuf, qnum, 4) != 0)
		{
		  if (fgets (logbuf, 2048, fdQ) == NULL)
		    {
		      if (diag_error)
			strcpy (diag_error,
				"log_top.q file format is not valid");
		      return ERR_WITH_MSG;
		    }
		}

	      while (fgets (logbuf, 2048, fdQ))
		{
		  if (!strncmp (logbuf, "[Q", 2))
		    break;
		  fprintf (fdAnalyzeResult, "%s", logbuf);
		}
	      nv_add_nvp (cli_response, "result", "end");
	    }
	}

      nv_add_nvp (cli_response, "resultlist", "end");

      fclose (fdRes);
      fclose (fdQ);

      unlink (tmpfileQ);
      unlink (tmpfileRes);
    }

  fclose (fdAnalyzeResult);
  nv_add_nvp (cli_response, "resultfile", tmpfileanalyzeresult);

  return ERR_NO_ERROR;
}

int
ts_executecasrunner (nvplist * cli_request, nvplist * cli_response,
		     char *diag_error)
{
  int i, sect, sect_len;
  char *log_string, *brokername, *username, *passwd;
  char *num_thread, *repeat_count, *show_queryresult;
  char *dbname, *casrunnerwithFile, *logfilename;
  char *show_queryplan;
  char bport[6], buf[1024];
  FILE *flogfile, *fresfile2;
  char tmplogfilename[512], resfile[512], resfile2[512];
  char log_converter_res[512];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *argv[25];
  T_DM_UC_CONF uc_conf;
  char out_msg_file_env[1024];
#ifdef WIN32
  DWORD th_id;
#else
  T_THREAD th_id;
#endif

  brokername = nv_get_val (cli_request, "brokername");
  dbname = nv_get_val (cli_request, "dbname");
  username = nv_get_val (cli_request, "username");
  passwd = nv_get_val (cli_request, "passwd");
  num_thread = nv_get_val (cli_request, "num_thread");
  repeat_count = nv_get_val (cli_request, "repeat_count");
  show_queryresult = nv_get_val (cli_request, "show_queryresult");
  show_queryplan = nv_get_val (cli_request, "show_queryplan");
  casrunnerwithFile = nv_get_val (cli_request, "executelogfile");
  logfilename = nv_get_val (cli_request, "logfile");

  if ((!brokername) || (!username) || (!dbname))
    {
      return ERR_PARAM_MISSING;
    }

#ifdef WIN32
  th_id = GetCurrentThreadId ();
#else
  th_id = getpid ();
#endif

  sprintf (resfile, "%s/log_run_%u.res", sco.dbmt_tmp_dir, th_id);
  sprintf (resfile2, "%s/log_run_%u.res2", sco.dbmt_tmp_dir, th_id);

  /* get right port number with broker name */
  if (uca_unicas_conf (&uc_conf, NULL, diag_error) < 0)
    {
      return ERR_WITH_MSG;
    }

  for (i = 0; i < uc_conf.num_broker; i++)
    {
      if (!strcmp (brokername, uca_conf_find (&(uc_conf.br_conf[i]), "%")))
	{
	  strcpy (bport,
		  uca_conf_find (&(uc_conf.br_conf[i]), "BROKER_PORT"));
	  break;
	}
    }

  uca_unicas_conf_free (&uc_conf);

  if (casrunnerwithFile && !strcmp (casrunnerwithFile, "yes"))
    {
      strcpy (tmplogfilename, logfilename);
    }
  else
    {
      /* create logfile */
      sprintf (tmplogfilename, "%s/cas_log_tmp_%u.q", sco.dbmt_tmp_dir,
	       th_id);

      flogfile = fopen (tmplogfilename, "w+");
      if (!flogfile)
	{
	  return ERR_FILE_OPEN_FAIL;
	}

      nv_locate (cli_request, "logstring", &sect, &sect_len);
      if (sect >= 0)
	{
	  for (i = 0; i < sect_len; ++i)
	    {
	      nv_lookup (cli_request, sect + i, NULL, &log_string);
	      fprintf (flogfile, "%s\n",
		       (log_string == NULL) ? " " : log_string);
	    }
	}
      fclose (flogfile);
    }

  /* execute broker_log_converter why logfile is created */
  sprintf (log_converter_res, "%s/log_converted_%u.q_res", sco.dbmt_tmp_dir,
	   th_id);
  sprintf (cmd_name, "%s/bin/broker_log_converter%s", sco.szCubrid,
	   DBMT_EXE_EXT);

  i = 0;
  argv[i] = cmd_name;
  argv[++i] = tmplogfilename;
  argv[++i] = log_converter_res;
  argv[++i] = NULL;

  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* broker_log_converter */
      strcpy (diag_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  /* execute broker_log_runner through logfile that converted */
  sprintf (cmd_name, "%s/bin/broker_log_runner%s", sco.szCubrid,
	   DBMT_EXE_EXT);
  i = 0;
  argv[i] = cmd_name;
  argv[++i] = "-I";
  argv[++i] = "localhost";
  argv[++i] = "-P";
  argv[++i] = bport;
  argv[++i] = "-d";
  argv[++i] = dbname;
  argv[++i] = "-u";
  argv[++i] = username;
  if (passwd)
    {
      argv[++i] = "-p";
      argv[++i] = passwd;
    }
  argv[++i] = "-t";
  argv[++i] = num_thread;
  argv[++i] = "-r";
  argv[++i] = repeat_count;
  if (show_queryplan && !strcmp (show_queryplan, "yes"))
    {
      argv[++i] = "-Q";
    }
  argv[++i] = "-o";
  argv[++i] = resfile;
  argv[++i] = log_converter_res;
  argv[++i] = NULL;

  sprintf (out_msg_file_env, "CUBRID_MANAGER_OUT_MSG_FILE=%s", resfile2);
  putenv (out_msg_file_env);

  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* broker_log_runner */
      return ERR_SYSTEM_CALL;
    }

  /* create message with read file's content */
  nv_add_nvp (cli_response, "result_list", "start");
  fresfile2 = fopen (resfile2, "r");
  if (fresfile2)
    {
      while (fgets (buf, 1024, fresfile2))
	{
	  if (!strncmp (buf, "cas_ip", 6))
	    continue;
	  if (!strncmp (buf, "cas_port", 8))
	    continue;
	  if (!strncmp (buf, "num_thread", 10))
	    continue;
	  if (!strncmp (buf, "repeat", 6))
	    continue;
	  if (!strncmp (buf, "dbname", 6))
	    continue;
	  if (!strncmp (buf, "dbuser", 6))
	    continue;
	  if (!strncmp (buf, "dbpasswd", 6))
	    continue;
	  if (!strncmp (buf, "result_file", 11))
	    continue;

	  buf[strlen (buf) - 1] = '\0';	/* remove new line ch */
	  nv_add_nvp (cli_response, "result", buf);
	}
      fclose (fresfile2);
    }
  nv_add_nvp (cli_response, "result_list", "end");
  nv_add_nvp (cli_response, "query_result_file", resfile);
  nv_add_nvp (cli_response, "query_result_file_num", num_thread);

  if (!strcmp (show_queryresult, "no"))
    {
      /* remove query result file - resfile */
      int i;
      char filename[2048];
      for (i = 0; i < atoi (num_thread); i++)
	{
	  sprintf (filename, "%s.%d", resfile, i);
	  unlink (filename);
	}
    }

  unlink (log_converter_res);	/* broker_log_converter execute result */
  unlink (resfile2);		/* cas log execute result */
  if (!casrunnerwithFile || strcmp (casrunnerwithFile, "yes") != 0)
    {
      unlink (tmplogfilename);	/* temp logfile */
    }
  return ERR_NO_ERROR;
}

int
ts_removecasrunnertmpfile (nvplist * cli_request, nvplist * cli_response,
			   char *diag_error)
{
  char *filename;
  char command[2048];

  filename = nv_get_val (cli_request, "filename");
  if (filename)
    {
      sprintf (command, "%s %s %s*", DEL_DIR, DEL_DIR_OPT, filename);
      if (system (command) == -1)
	{
#ifdef WIN32
	  sprintf (command, "%s %s %s*", DEL_FILE, DEL_FILE_OPT, filename);
	  if (system (command) == -1)
#endif
	    return ERR_DIR_REMOVE_FAIL;
	}
    }
  return ERR_NO_ERROR;
}

int
ts_getcaslogtopresult (nvplist * cli_request, nvplist * cli_response,
		       char *diag_error)
{
  char *filename, *qindex;
  FILE *fd;
  char buf[1024];
  int find_flag;

  filename = nv_get_val (cli_request, "filename");
  qindex = nv_get_val (cli_request, "qindex");
  if (!filename || !qindex)
    {
      return ERR_PARAM_MISSING;
    }

  fd = fopen (filename, "r");
  if (!fd)
    {
      return ERR_FILE_OPEN_FAIL;
    }

  find_flag = 0;
  nv_add_nvp (cli_response, "logstringlist", "start");
  while (fgets (buf, 1024, fd))
    {
      if (!strncmp (buf, "[Q", 2))
	{
	  if (find_flag == 1)
	    break;
	  if (!strncmp (buf, qindex, strlen (qindex)))
	    {
	      find_flag = 1;
	      continue;
	    }
	}

      if (find_flag == 1)
	{
	  buf[strlen (buf) - 1] = '\0';
	  nv_add_nvp (cli_response, "logstring", buf);
	}
    }

  nv_add_nvp (cli_response, "logstringlist", "end");

  fclose (fd);
  return ERR_NO_ERROR;
}

static int
get_dbvoldir (char *vol_dir, char *dbname, char *err_buf)
{
  FILE *databases_txt;
  char *envpath;
  char db_txt[1024];
  char cbuf[2048];
  char volname[1024];

  if (!vol_dir || !dbname)
    return -1;

  envpath = getenv ("CUBRID_DATABASES");
  if (envpath == NULL || strlen (envpath) == 0)
    {
      return -1;
    }

  sprintf (db_txt, "%s/%s", envpath, CUBRID_DATABASE_TXT);
  databases_txt = fopen (db_txt, "r");
  if (databases_txt == NULL)
    {
      return -1;
    }

  while (fgets (cbuf, 1024, databases_txt))
    {
      if (sscanf (cbuf, "%s %s %*s %*s", volname, vol_dir) < 2)
	continue;

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
getservershmid (char *dir, char *dbname, char *err_buf)
{
  int shm_key;
  char key_file[1024], cbuf[1024];
  FILE *fdkey_file;

  if (!dir || !dbname)
    return -1;

  sprintf (key_file, "%s/%s_shm.key", dir, dbname);
  fdkey_file = fopen (key_file, "r");

  if (fdkey_file)
    {
      if (fgets (cbuf, 1024, fdkey_file) == NULL)
	return -1;

      shm_key = strtol (cbuf, NULL, 16);
      fclose (fdkey_file);
      return shm_key;
    }

  return -1;
}

#if 0				/* ACTIVITY_PROFILE */
static int
read_activity_data_from_file (void *result_data, char *filename,
			      struct timeval start_time,
			      T_DIAG_SERVER_TYPE type)
{
  int fd_activity_file;
  ssize_t current_read;
  int total_read_byte;
  char buf[2048], parsing_target_buf[4096];

  if ((result_data == NULL) || (filename == NULL))
    {
      return -1;
    }

  memset (parsing_target_buf, '\0', 4096);

  fd_activity_file = open (filename, O_RDONLY);
  if (fd_activity_file != -1)
    {
      int not_parsed_byte = 0;
      current_read = total_read_byte = 0;

      do
	{
	  memset (buf, '\0', 2048);
	  current_read = read (fd_activity_file, (void *) buf, (size_t) 2048);

	  if (current_read > 0)
	    {
	      strcat (parsing_target_buf, buf);
	      not_parsed_byte =
		parse_and_make_client_activity_data (result_data,
						     parsing_target_buf,
						     current_read +
						     not_parsed_byte,
						     start_time, type);

	      total_read_byte += current_read;

	      if (not_parsed_byte == -1)
		break;
	    }
	}
      while (current_read > 0);

      close (fd_activity_file);
    }
  else
    {
      return -1;
    }
}

static int
parse_and_make_client_activity_data (void *result_data, char *buf, int size,
				     struct timeval start_time,
				     T_DIAG_SERVER_TYPE type)
{
  /* parsing buf and copy that to result_data in order. and copy cannot parsing
   * data to buf and return doesn't parsing(remain thing) byte count.
   * if raise error then -1 return                                              */
  char *p_act_ev_class, *p_act_Text, *p_act_Bin, *p_act_Int, *p_act_Time;
  char *p_current, *p_start;
  int parsed_size, total_parsed_size, ret_val;

  parsed_size = total_parsed_size = 0;

  p_start = buf;
  do
    {
      T_ACTIVITY_DATA activity_data;

      memset (activity_data.TextData, '\0', 1024);
      memset (activity_data.BinaryData, '\0', 1024);

      p_act_ev_class = strstr (p_start, "ev_class:");
      if (p_act_ev_class)
	{
	  p_current = p_act_ev_class + strlen ("ev_class: ");
	}
      else
	break;

      p_act_Text = strstr (p_start, "act_Text:");
      if (p_act_Text)
	{
	  char EventClassString[8];

	  /* copy 'p_current ~ p_act_Text' data to activity_data.EventClass */
	  strncpy (EventClassString, p_current, p_act_Text - p_current);
	  activity_data.EventClass = atoi (EventClassString);
	  p_current = p_act_Text + strlen ("act_Text: ");
	}
      else
	break;

      p_act_Bin = strstr (p_start, "act_Bin: ");
      if (p_act_Bin)
	{
	  /* copy 'p_current ~ p_act_Bin' to activity_data.TextData */
	  strncpy (activity_data.TextData, p_current,
		   p_act_Bin - p_current - 1);
	  p_current = p_act_Bin + strlen ("act_Bin: ");
	}
      else
	break;

      p_act_Int = strstr (p_start, "act_Int: ");
      if (p_act_Int)
	{
	  /* copy 'p_current ~ p_act_Int' to activity_data.BinaryData */
	  memcpy (activity_data.BinaryData, p_current,
		  p_act_Int - p_current - 1);
	  p_current = p_act_Int + strlen ("act_Int: ");
	}
      else
	break;

      p_act_Time = strstr (p_start, "act_Time: ");
      if (p_act_Time)
	{
	  char IntegerString[16];
	  char TimeString[22];	/* sec length : 10 + ',' + microsec length 10 */
	  /* copy 'p_current ~ p_act_Time' to activity_data.IntegerData */
	  strncpy (IntegerString, p_current, p_act_Time - p_current - 1);
	  activity_data.IntegerData = atoi (IntegerString);
	  p_current = p_act_Time + strlen ("act_Time: ");

	  /* copy 10 char from p_current to activity_data.Time */
	  if ((p_current + 21) > (buf + size))
	    {
	      break;
	    }
	  else
	    {
	      strncpy (TimeString, p_current, 10);	/* sec length */
	      activity_data.Time.tv_sec = atol (TimeString);
	      strncpy (TimeString, p_current + 11, 10);	/* micro sec length */
	      activity_data.Time.tv_usec = atol (TimeString);
	      p_current += 21;	/* time value string length */
	    }
	}
      else
	break;

      parsed_size = p_current - p_start;
      total_parsed_size += parsed_size;
      p_start = p_current;

      if ((activity_data.Time.tv_sec > start_time.tv_sec)
	  || ((activity_data.Time.tv_sec == start_time.tv_sec)
	      && (activity_data.Time.tv_usec > start_time.tv_usec)))
	{
	  switch (type)
	    {
	    case DIAG_SERVER_CAS:
	      if (activity_data.EventClass ==
		  DIAG_EVENTCLASS_TYPE_CAS_REQUEST)
		add_activity_list (&
				   (((T_DIAG_ACTIVITY_CAS *) result_data)->
				    request), &activity_data);
	      else if (activity_data.EventClass ==
		       DIAG_EVENTCLASS_TYPE_CAS_TRANSACTION)
		add_activity_list (&
				   (((T_DIAG_ACTIVITY_CAS *) result_data)->
				    transaction), &activity_data);
	      break;
	    case DIAG_SERVER_DB:
	      if (activity_data.EventClass
		  == DIAG_EVENTCLASS_TYPE_SERVER_QUERY_FULL_SCAN)
		add_activity_list (&
				   (((T_DIAG_ACTIVITY_DB_SERVER *)
				     result_data)->query_fullscan),
				   &activity_data);
	      else if (activity_data.EventClass ==
		       DIAG_EVENTCLASS_TYPE_SERVER_LOCK_DEADLOCK)
		add_activity_list (&
				   (((T_DIAG_ACTIVITY_DB_SERVER *)
				     result_data)->lock_deadlock),
				   &activity_data);
	      else if (activity_data.EventClass ==
		       DIAG_EVENTCLASS_TYPE_SERVER_BUFFER_PAGE_READ)
		add_activity_list (&
				   (((T_DIAG_ACTIVITY_DB_SERVER *)
				     result_data)->buffer_page_read),
				   &activity_data);
	      else if (activity_data.EventClass ==
		       DIAG_EVENTCLASS_TYPE_SERVER_BUFFER_PAGE_WRITE)
		add_activity_list (&
				   (((T_DIAG_ACTIVITY_DB_SERVER *)
				     result_data)->buffer_page_write),
				   &activity_data);

	      break;
	    }
	}

    }
  while (p_act_ev_class);

  /* copy didn't parsed data to buf and return remained data's count */
  if (total_parsed_size > 0)
    {
      int not_parsed_size = size - total_parsed_size;

      strncpy (buf, p_start, not_parsed_size);
      memset (buf + not_parsed_size, '\0', size - not_parsed_size);
      return not_parsed_size;
    }
  else
    {
      return size;
    }
}

static char *
get_activity_event_type_str (T_DIAG_ACTIVITY_EVENTCLASS_TYPE type)
{
  return eventclass_type_str[(int) type];
}

static int
add_activity_list (T_ACTIVITY_DATA ** header, T_ACTIVITY_DATA * list)
{
  T_ACTIVITY_DATA *temp_node;
  T_ACTIVITY_DATA *new_node;

  if ((header == NULL) || (list == NULL))
    {
      return -1;
    }

  /* memory alloc (new_node) and copy data */
  new_node = (T_ACTIVITY_DATA *) malloc (sizeof (T_ACTIVITY_DATA));
  if (new_node == NULL)
    {
      return -1;
    }

  new_node->EventClass = list->EventClass;
  strcpy (new_node->TextData, list->TextData);
  memcpy (new_node->BinaryData, list->BinaryData, sizeof (list->BinaryData));
  new_node->IntegerData = list->IntegerData;
  new_node->Time = list->Time;
  new_node->next = NULL;

  /* make link */
  if (*header == NULL)
    {
      *header = new_node;
    }
  else
    {
      temp_node = *header;

      while (temp_node->next != NULL)
	{
	  temp_node = temp_node->next;
	}

      temp_node->next = new_node;
    }

  return 0;
}
#endif /* ACTIVITY PROFILE */

static int
get_client_monitoring_config (nvplist * cli_request,
			      T_CLIENT_MONITOR_CONFIG * c_config)
{
  char *monitor_db, *monitor_cas, *monitor_resource;
  char *cas_mon_req, *cas_mon_tran, *cas_mon_active_session;

  monitor_db = nv_get_val (cli_request, "mon_db");
  monitor_cas = nv_get_val (cli_request, "mon_cas");
  monitor_resource = nv_get_val (cli_request, "mon_resource");

#if 0				/* ACTIVITY_PROFILE */
  activity_db = nv_get_val (cli_request, "act_db");
  activity_cas = nv_get_val (cli_request, "act_cas");
#endif /* ACTIVITY PROFILE */

  if (monitor_db && !strcasecmp (monitor_db, "yes"))
    {
      int header_flag;
      char *monitor_target;

      /* 1. parse "cubrid_query" monitor list */
      header_flag = 0;
      monitor_target = nv_get_val (cli_request, "mon_cub_query_open_page");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_QUERY_OPEN_PAGE (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_query_opened_page");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_QUERY_OPENED_PAGE (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_query_slow_query");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_QUERY_SLOW_QUERY (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_query_full_scan");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_QUERY_FULL_SCAN (*c_config);
	  header_flag = 1;
	}
      if (header_flag == 1)
	SET_CLIENT_MONITOR_INFO_CUB_QUERY (*c_config);

      /* 2. parse "cubrid_conn" monitor list */
      header_flag = 0;
      monitor_target = nv_get_val (cli_request, "mon_cub_conn_cli_request");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_CONN_CLI_REQUEST (*c_config);
	  header_flag = 1;
	}
      monitor_target =
	nv_get_val (cli_request, "mon_cub_conn_aborted_clients");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_CONN_ABORTED_CLIENTS (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_conn_conn_req");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_CONN_CONN_REQ (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_conn_conn_reject");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_CONN_CONN_REJECT (*c_config);
	  header_flag = 1;
	}
      if (header_flag == 1)
	SET_CLIENT_MONITOR_INFO_CUB_CONNECTION (*c_config);

      /* 3. parse "cubrid_buffer" monitor list */
      header_flag = 0;
      monitor_target = nv_get_val (cli_request, "mon_cub_buffer_page_write");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_BUFFER_PAGE_WRITE (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_buffer_page_read");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_BUFFER_PAGE_READ (*c_config);
	  header_flag = 1;
	}
      if (header_flag == 1)
	SET_CLIENT_MONITOR_INFO_CUB_BUFFER (*c_config);

      /* 4 parse "cubrid_lock" monitor list */
      header_flag = 0;
      monitor_target = nv_get_val (cli_request, "mon_cub_lock_deadlock");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_LOCK_DEADLOCK (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_lock_request");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_LOCK_REQUEST (*c_config);
	  header_flag = 1;
	}
      if (header_flag == 1)
	SET_CLIENT_MONITOR_INFO_CUB_LOCK (*c_config);
    }

#if 0				/* ACTIVITY_PROFILE */
  if (activity_db && !strcasecmp (activity_db, "yes"))
    {
      char *act_target;
      int header_flag;

      header_flag = 0;
      act_target = nv_get_val (cli_request, "act_cub_query_fullscan");
      if (act_target && !strcasecmp (act_target, "yes"))
	{
	  SET_CLIENT_ACTINFO_CUB_QUERY_FULLSCAN (*c_config);
	  header_flag = 1;
	}
      act_target = nv_get_val (cli_request, "act_cub_lock_deadlock");
      if (act_target && !strcasecmp (act_target, "yes"))
	{
	  SET_CLIENT_ACTINFO_CUB_LOCK_DEADLOCK (*c_config);
	  header_flag = 1;
	}
      act_target = nv_get_val (cli_request, "act_cub_buffer_page_read");
      if (act_target && !strcasecmp (act_target, "yes"))
	{
	  SET_CLIENT_ACTINFO_CUB_BUFFER_PAGE_READ (*c_config);
	  header_flag = 1;
	}
      act_target = nv_get_val (cli_request, "act_cub_buffer_page_write");
      if (act_target && !strcasecmp (act_target, "yes"))
	{
	  SET_CLIENT_ACTINFO_CUB_BUFFER_PAGE_WRITE (*c_config);
	  header_flag = 1;
	}
      if (header_flag == 1)
	SET_CLIENT_ACTINFO_CUB (*c_config);
    }
#endif /* ACTIVITY PROFILE */

  if (monitor_cas && !strcasecmp (monitor_cas, "yes"))
    {
      cas_mon_req = nv_get_val (cli_request, "cas_mon_req");
      cas_mon_tran = nv_get_val (cli_request, "cas_mon_tran");
      cas_mon_active_session =
	nv_get_val (cli_request, "cas_mon_act_session");

      SET_CLIENT_MONITOR_INFO_CAS (*c_config);
      if (cas_mon_req && !strcasecmp (cas_mon_req, "yes"))
	SET_CLIENT_MONITOR_INFO_CAS_REQ (*c_config);

      if (cas_mon_tran && !strcasecmp (cas_mon_tran, "yes"))
	SET_CLIENT_MONITOR_INFO_CAS_TRAN (*c_config);

      if (cas_mon_active_session
	  && !strcasecmp (cas_mon_active_session, "yes"))
	SET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION (*c_config);
    }

#if 0				/* ACTIVITY_PROFILE */
  if (activity_cas && !strcasecmp (activity_cas, "yes"))
    {
      cas_act_req = nv_get_val (cli_request, "cas_act_req");
      cas_act_tran = nv_get_val (cli_request, "cas_act_tran");

      SET_CLIENT_ACTINFO_CAS (*c_config);
      if (cas_act_req && !strcasecmp (cas_act_req, "yes"))
	SET_CLIENT_ACTINFO_CAS_REQ (*c_config);

      if (cas_act_tran && !strcasecmp (cas_act_tran, "yes"))
	SET_CLIENT_ACTINFO_CAS_TRAN (*c_config);
    }
#endif /* ACTIVITY PROFILE */

  return ERR_NO_ERROR;
}

#ifdef WIN32
static void
shm_key_to_name (int shm_key, char *name_str)
{
  sprintf (name_str, "cubrid_shm_%d", shm_key);
}
#endif

#ifdef WIN32
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
  lpvMem = MapViewOfFile (hMapObject,	/* object to map view of    */
			  FILE_MAP_WRITE,	/* read/write access        */
			  0,	/* high offset:   map from  */
			  0,	/* low offset:    beginning */
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

#endif

static int
get_filename_from_path (char *path, char *filename)
{
  unsigned int i, filename_index;

  if (!path || !filename)
    {
      return 0;
    }

  filename_index = -1;
  for (i = 0; i < strlen (path); i++)
    {
      if (path[i] == '/')
	filename_index = i;
    }

  if (filename_index == -1)
    return -1;
  else if (filename_index == strlen (path))
    return -1;
  else
    {
      strcpy (filename, (path + filename_index + 1));
    }

  return 1;
}

int
tsDBMTUserLogin (nvplist * in, nvplist * out, char *_dbmt_error)
{
  int errcode;
  char *targetid, *dbname, *dbuser, *dbpasswd;
  DB_OBJECT *user, *obj;
  DB_VALUE v;
  DB_COLLECTION *col;
  int i;
  bool isdba = false;
  T_DB_SERVICE_MODE db_mode;

  targetid = nv_get_val (in, "targetid");
  dbname = nv_get_val (in, "dbname");
  dbuser = nv_get_val (in, "dbuser");
  dbpasswd = nv_get_val (in, "dbpasswd");

  db_mode = uDatabaseMode (dbname);
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
  errcode = db_restart (DB_RESTART_SERVER_NAME, 0, dbname);
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
    nv_add_nvp (out, "authority", "isdba");
  else
    nv_add_nvp (out, "authority", "isnotdba");

  db_shutdown ();
  return ERR_NO_ERROR;
}

int
ts_change_owner (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *class_name, *owner_name;
  char buf[1024];
  DB_OBJECT *classobj;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;

  if (_op_db_login (out, in, _dbmt_error) < 0)
    return ERR_WITH_MSG;

  class_name = nv_get_val (in, "classname");
  owner_name = nv_get_val (in, "ownername");
  classobj = db_find_class (class_name);

  if (class_name != NULL && owner_name != NULL)
    {
      sprintf (buf,
	       "CALL change_owner('%s', '%s') ON CLASS db_authorizations",
	       class_name, owner_name);
    }

  if (db_execute (buf, &result, &query_error) < 0)
    {
      CUBRID_ERR_MSG_SET (_dbmt_error);
      db_shutdown ();
      return ERR_WITH_MSG;
    }
  db_query_end (result);

  if (_op_get_detailed_class_info (out, classobj, _dbmt_error) < 0)
    {
      db_shutdown ();
      return ERR_WITH_MSG;
    }
  db_commit_transaction ();

  db_shutdown ();
  return ERR_NO_ERROR;
}

static int
user_login_sa (nvplist * out, char *_dbmt_error, char *dbname, char *dbuser,
	       char *dbpasswd)
{
  char opcode[10];
  char outfile[512], errfile[512];
  char *argv[10];
  char cmd_name[512];
  char *outmsg = NULL, *errmsg = NULL;

  sprintf (outfile, "%s/tmp/DBMT_ems_sa.%d", sco.szCubrid, (int) getpid ());
  sprintf (errfile, "%s.err", outfile);
  unlink (outfile);
  unlink (errfile);

  sprintf (opcode, "%d", EMS_SA_DBMT_USER_LOGIN);

  sprintf (cmd_name, "%s/bin/cub_jobsa%s", sco.szCubrid, DBMT_EXE_EXT);

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
    nv_add_nvp (out, "authority", "isdba");
  else
    nv_add_nvp (out, "authority", "isnotdba");

  unlink (outfile);
  unlink (errfile);
  return ERR_NO_ERROR;

login_err:
  if (errmsg)
    free (errmsg);
  if (outmsg)
    free (outmsg);
  unlink (outfile);
  unlink (errfile);
  return ERR_WITH_MSG;
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
  size = statbuf.st_size;

  if (size <= 0)
    {
      return 0;
    }

  buf = (char *) malloc (size + 1);
  if (buf == NULL)
    return -1;

  fd = open (filename, O_RDONLY);
  if (fd < 0)
    return -1;
#ifdef WIN32
  if (setmode (fd, O_BINARY) == -1)
    {
      close (fd);
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
get_broker_info_from_filename (char *path, char *br_name, int *as_id)
{
#ifdef WIN32
  const char *sql_log_ext = ".sql.log";
  int sql_log_ext_len = strlen (sql_log_ext);
  int path_len;
  char *p;

  if (path == NULL)
    return -1;

  path_len = strlen (path);
  if (strncmp (path, sco.szCubrid, strlen (sco.szCubrid)) != 0 ||
      path_len <= sql_log_ext_len ||
      strcmp (path + (path_len - sql_log_ext_len), sql_log_ext) != 0)
    {
      return -1;
    }

  for (p = path + path_len - 1; p >= path; p--)
    {
      if (*p == '/' || *p == '\\')
	{
	  break;
	}
    }
  path = p + 1;
  path_len = strlen (path);

  *(path + path_len - sql_log_ext_len) = '\0';
  p = strrchr (path, '_');

  *as_id = atoi (p + 1);
  if (*as_id <= 0)
    return -1;

  strncpy (br_name, path, p - path);
  *(br_name + (p - path)) = '\0';

  return 0;
#else
  return -1;
#endif
}

int
ts_remove_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *path;
  int i;
  int sect, sect_len;
  char broker_name[1024];
  int as_id;

  nv_locate (req, "files", &sect, &sect_len);
  if (sect >= 0)
    {
      for (i = 0; i < sect_len; ++i)
	{
	  nv_lookup (req, sect + i, NULL, &path);
	  if (unlink (path) != 0 && errno != ENOENT)
	    {
	      sprintf (_dbmt_error, "Cannot remove file '%s' (%s)", path,
		       strerror (errno));

	      if (get_broker_info_from_filename (path, broker_name, &as_id) <
		  0 || uca_del_cas_log (broker_name, as_id, _dbmt_error) < 0)
		{
		  return ERR_WITH_MSG;
		}
	      _dbmt_error[0] = '\0';
	    }
	}			/* end of for */
    }
  return ERR_NO_ERROR;
}
