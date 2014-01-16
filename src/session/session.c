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
 * session.c - session state internal API
 */

#ident "$Id$"


#include <assert.h>

#if !defined(WINDOWS)
#include <sys/time.h>
#include <sys/resource.h>
#endif /* !WINDDOWS */
#include "jansson.h"

#include "porting.h"
#include "critical_section.h"
#include "memory_hash.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "db.h"
#include "query_executor.h"
#include "session.h"
#include "environment_variable.h"
#if defined(SERVER_MODE)
#include "connection_sr.h"
#endif
#include "xserver_interface.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define SESSIONS_HASH_SIZE 1000
#define MAX_SESSION_VARIABLES_COUNT 20
#define MAX_PREPARED_STATEMENTS_COUNT 20

typedef struct active_sessions
{
  MHT_TABLE *sessions_table;
  SESSION_ID last_sesson_id;
  int num_holdable_cursors;
} ACTIVE_SESSIONS;

typedef struct session_timeout_info SESSION_TIMEOUT_INFO;
struct session_timeout_info
{
  SESSION_ID *session_ids;
  int count;
  struct timeval *timeout;
};

typedef struct session_variable SESSION_VARIABLE;
struct session_variable
{
  char *name;
  DB_VALUE *value;
  SESSION_VARIABLE *next;
};


typedef struct prepared_statement PREPARED_STATEMENT;
struct prepared_statement
{
  OID user;
  char *name;
  char *alias_print;
  int info_length;
  char *info;
  PREPARED_STATEMENT *next;
};
typedef struct session_query_entry SESSION_QUERY_ENTRY;
struct session_query_entry
{
  QUERY_ID query_id;		/* unique query identifier */
  QFILE_LIST_ID *list_id;	/* result list file identifier */
  QMGR_TEMP_FILE *temp_file;	/* temp files */
  int num_tmp;			/* number of temp files allocated */
  int total_count;		/* total number of file pages allocated
				 * for the entire query */
  QUERY_FLAG query_flag;

  SESSION_QUERY_ENTRY *next;
};

typedef struct session_state
{
  SESSION_ID session_id;
  bool is_trigger_involved;
  bool is_last_insert_id_generated;
  DB_VALUE cur_insert_id;
  DB_VALUE last_insert_id;
  int row_count;
  SESSION_VARIABLE *session_variables;
  PREPARED_STATEMENT *statements;
  SESSION_QUERY_ENTRY *queries;
  struct timeval session_timeout;
  SOCKET related_socket;
  SESSION_PARAM *session_parameters;
  char *trace_stats;
  char *plan_string;
  int trace_format;
} SESSION_STATE;

/* the active sessions storage */
static ACTIVE_SESSIONS sessions = { NULL, 0, 0 };

static unsigned int sessions_hash (const void *key,
				   unsigned int hash_table_size);
static int session_compare (const void *key_left, const void *key_right);

static int session_free_session (const void *key, void *data, void *args);

static int session_print_active_sessions (const void *key, void *data,
					  void *args);
static int session_check_timeout (const void *key, void *data, void *args);

static void session_free_prepared_statement (PREPARED_STATEMENT * stmt_p);

static int session_add_variable (SESSION_STATE * state_p,
				 const DB_VALUE * name, DB_VALUE * value);

static int session_drop_variable (SESSION_STATE * state_p,
				  const DB_VALUE * name);

static void free_session_variable (SESSION_VARIABLE * var);

static void update_session_variable (SESSION_VARIABLE * var,
				     const DB_VALUE * new_value);

static DB_VALUE *db_value_alloc_and_copy (const DB_VALUE * src);

static int session_dump_session (const void *key, void *data, void *args);
static void session_dump_variable (SESSION_VARIABLE * var);
static void session_dump_prepared_statement (PREPARED_STATEMENT * stmt_p);

static SESSION_QUERY_ENTRY *qentry_to_sentry (QMGR_QUERY_ENTRY * qentry_p);
static int session_preserve_temporary_files (THREAD_ENTRY * thread_p,
					     SESSION_QUERY_ENTRY * q_entry);
static void sentry_to_qentry (const SESSION_QUERY_ENTRY * sentry_p,
			      QMGR_QUERY_ENTRY * qentry_p);
static void session_free_sentry_data (THREAD_ENTRY * thread_p,
				      SESSION_QUERY_ENTRY * sentry_p);
static void session_set_conn_entry_data (THREAD_ENTRY * thread_p,
					 SESSION_STATE * session_p);
static SESSION_STATE *session_get_session_state (THREAD_ENTRY * thread_p);

/*
 * session_hash () - hashing function for the session hash
 *   return: int
 *   key(in): Session key
 *   htsize(in): Memory Hash Table Size
 *
 * Note: Generate a hash number for the given key for the given hash table
 *	 size.
 */
static unsigned int
sessions_hash (const void *key, unsigned int hash_table_size)
{
  const unsigned int *session_id = (const unsigned int *) key;

  return ((*session_id) % hash_table_size);
}

/*
 * sessions_compare () - Compare two session keys
 *   return: int (true or false)
 *   key_left  (in) : First session key
 *   key_right (in) : Second session key
 */
static int
sessions_compare (const void *key_left, const void *key_right)
{
  const unsigned int *key1, *key2;

  key1 = (SESSION_ID *) key_left;
  key2 = (SESSION_ID *) key_right;

  return (*key1) == (*key2);
}

/*
 * session_free_prepared_statement () - free memory allocated for a prepared
 *					statement
 * return : void
 * stmt_p (in) : prepared statement object
 */
static void
session_free_prepared_statement (PREPARED_STATEMENT * stmt_p)
{
  if (stmt_p == NULL)
    {
      return;
    }

  er_log_debug (ARG_FILE_LINE, "drop statement %s\n", stmt_p->name);

  if (stmt_p->name != NULL)
    {
      free_and_init (stmt_p->name);
    }
  if (stmt_p->alias_print != NULL)
    {
      free_and_init (stmt_p->alias_print);
    }
  if (stmt_p->info != NULL)
    {
      free_and_init (stmt_p->info);
    }

  free_and_init (stmt_p);
}

/*
 * sessions_is_states_table_initialized () - check to see if session states
 *					     memory area is initialized
 *   return: true if initialized, false otherwise
 *
 * Note: this function should only be called after entering the critical
 * section used by the session state module
 */
bool
sessions_is_states_table_initialized (void)
{
  return (sessions.sessions_table != NULL);
}

/*
 * session_states_init () - Initialize session states area
 *   return: NO_ERROR or error code
 *
 * Note: Creates and initializes a main memory hash table that will be
 * used by session states operations. This routine should only be
 * called once during server boot.
 */
int
session_states_init (THREAD_ENTRY * thread_p)
{
  sessions.last_sesson_id = 0;
  sessions.num_holdable_cursors = 0;

  er_log_debug (ARG_FILE_LINE, "creating session states table\n");

  if (csect_enter (thread_p, CSECT_SESSION_STATE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (sessions_is_states_table_initialized ())
    {
      csect_exit (thread_p, CSECT_SESSION_STATE);
      return NO_ERROR;
    }

  sessions.sessions_table = mht_create ("Sessions_State_Table",
					SESSIONS_HASH_SIZE, sessions_hash,
					sessions_compare);
  if (sessions.sessions_table == NULL)
    {
      csect_exit (thread_p, CSECT_SESSION_STATE);
      return ER_FAILED;
    }

  csect_exit (thread_p, CSECT_SESSION_STATE);

  return NO_ERROR;
}

/*
 * session_states_finalize () - cleanup the session states information
 *   return: NO_ERROR or error code
 *   thread_p (in) : the thread executing this function
 *
 * Note: This function deletes the session states global storage area.
 *	 This function should be called only during server shutdown
 */
void
session_states_finalize (THREAD_ENTRY * thread_p)
{
  const char *env_value;

  env_value = envvar_get ("DUMP_SESSION");
  if (env_value != NULL)
    {
      session_states_dump (thread_p);
    }

  er_log_debug (ARG_FILE_LINE, "deleting session state table\n");

  if (csect_enter (thread_p, CSECT_SESSION_STATE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  if (sessions.sessions_table != NULL)
    {
      (void) mht_map (sessions.sessions_table, session_free_session, NULL);
      mht_destroy (sessions.sessions_table);
      sessions.sessions_table = NULL;
    }

  csect_exit (thread_p, CSECT_SESSION_STATE);
}

/*
 * session_state_create () - Create a sessions state with the specified id
 *   return: NO_ERROR or error code
 *   session_id (in) : the session id
 *
 * Note: This function creates and adds a sessions state object to the
 *       sessions state memory hash. This function should be called when a
 *	 session starts.
 */
int
session_state_create (THREAD_ENTRY * thread_p, SESSION_KEY * key)
{
  static bool overflow = false;
  SESSION_STATE *session_p = NULL;
  SESSION_ID *session_key = NULL;

  session_p = (SESSION_STATE *) malloc (sizeof (SESSION_STATE));
  if (session_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (SESSION_STATE));
      goto error_return;
    }

  session_p->session_id = DB_EMPTY_SESSION;
  DB_MAKE_NULL (&session_p->cur_insert_id);
  DB_MAKE_NULL (&session_p->last_insert_id);
  session_p->is_trigger_involved = false;
  session_p->is_last_insert_id_generated = false;
  session_p->row_count = -1;
  session_p->session_variables = NULL;
  session_p->statements = NULL;
  session_p->queries = NULL;
  session_p->related_socket = INVALID_SOCKET;
  session_p->session_parameters = NULL;
  session_p->trace_stats = NULL;
  session_p->plan_string = NULL;
  session_p->trace_format = QUERY_TRACE_TEXT;

  /* initialize the timeout */
  if (gettimeofday (&(session_p->session_timeout), NULL) != 0)
    {
      goto error_return;
    }

  session_key = (SESSION_ID *) malloc (sizeof (SESSION_ID));
  if (session_key == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (SESSION_ID));
      goto error_return;
    }

  /* add this session_p state to the session_p states table */
  if (csect_enter (thread_p, CSECT_SESSION_STATE, INF_WAIT) != NO_ERROR)
    {
      goto error_return;
    }

  if (sessions.last_sesson_id >= UINT_MAX - 1)
    {
      /* we really should do something else here */
      sessions.last_sesson_id = 0;
      overflow = true;
    }

  if (overflow)
    {
      do
	{
	  sessions.last_sesson_id++;
	  key->id = sessions.last_sesson_id;
	}
      while (mht_get (sessions.sessions_table, &key->id) != NULL);
    }
  else
    {
      sessions.last_sesson_id++;
      key->id = sessions.last_sesson_id;
    }

  session_p->session_id = key->id;
  *session_key = key->id;

  er_log_debug (ARG_FILE_LINE, "adding session with id %u\n", key->id);

  (void) mht_put (sessions.sessions_table, session_key, session_p);

  if (prm_get_bool_value (PRM_ID_ER_LOG_DEBUG) == true)
    {
      er_log_debug (ARG_FILE_LINE, "printing active sessions\n");
      mht_map (sessions.sessions_table, session_print_active_sessions, NULL);
      er_log_debug (ARG_FILE_LINE, "finished printing active sessions\n");
    }
  /* need to do this under the critical section because we cannot rely on the
   * session_p pointer after we left it */
  session_set_conn_entry_data (thread_p, session_p);
  csect_exit (thread_p, CSECT_SESSION_STATE);

  return NO_ERROR;

error_return:
  if (session_p != NULL)
    {
      free_and_init (session_p);
    }

  if (session_key != NULL)
    {
      free_and_init (session_key);
    }

  return ER_FAILED;
}

/*
 * session_free_session () - Free the memory associated with a session state
 *   return  : NO_ERROR or error code
 *   key(in) : the key from the MHT_TABLE for this session
 *   data(in): session state data
 *   args(in): not used
 *
 * Note: This function is used with the MHT_TABLE routines to free an entry in
 * the table
 */
static int
session_free_session (const void *key, void *data, void *args)
{
  SESSION_STATE *session = (SESSION_STATE *) data;
  SESSION_ID *sess_key = (SESSION_ID *) key;
  SESSION_VARIABLE *vcurent = NULL, *vnext = NULL;
  PREPARED_STATEMENT *pcurent = NULL, *pnext = NULL;
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  SESSION_QUERY_ENTRY *qcurent = NULL, *qnext = NULL;
  int cnt = 0;

  if (session == NULL)
    {
      if (sess_key != NULL)
	{
	  free_and_init (sess_key);
	}
      return NO_ERROR;
    }
  er_log_debug (ARG_FILE_LINE, "session_free_session %u\n",
		session->session_id);

  /* free session key */
  if (sess_key != NULL)
    {
      free_and_init (sess_key);
    }

  /* free session variables */
  vcurent = session->session_variables;
  while (vcurent != NULL)
    {
      vnext = vcurent->next;
      free_session_variable (vcurent);
      vcurent = vnext;
    }

  /* free session statements */
  pcurent = session->statements;
  while (pcurent != NULL)
    {
      pnext = pcurent->next;
      session_free_prepared_statement (pcurent);
      pcurent = pnext;
    }
  /* free holdable queries */
  qcurent = session->queries;
  while (qcurent)
    {
      qnext = qcurent->next;
      qcurent->next = NULL;
      session_free_sentry_data (thread_p, qcurent);
      free_and_init (qcurent);
      qcurent = qnext;
      cnt++;
    }

  if (session->session_parameters)
    {
      sysprm_free_session_parameters (&session->session_parameters);
    }

  er_log_debug (ARG_FILE_LINE,
		"session_free_session closed %d queries for %d\n", cnt,
		session->session_id);

  pr_clear_value (&session->cur_insert_id);
  pr_clear_value (&session->last_insert_id);

  if (session->trace_stats != NULL)
    {
      free_and_init (session->trace_stats);
    }

  if (session->plan_string != NULL)
    {
      free_and_init (session->plan_string);
    }

  free_and_init (session);

  return NO_ERROR;
}

/*
 * session_state_destroy () - close a session state
 *   return	    : NO_ERROR or error code
 *   session_id(in) : the identifier for the session
 */
int
session_state_destroy (THREAD_ENTRY * thread_p, const SESSION_KEY * key)
{
  SESSION_STATE *session_p;
  int error = NO_ERROR;

  er_log_debug (ARG_FILE_LINE, "removing session %u", key->id);

  if (csect_enter (thread_p, CSECT_SESSION_STATE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  session_p = (SESSION_STATE *) mht_get (sessions.sessions_table, &key->id);
  if (session_p == NULL || session_p->related_socket != key->fd)
    {
      csect_exit (thread_p, CSECT_SESSION_STATE);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SES_SESSION_EXPIRED, 0);
      return ER_SES_SESSION_EXPIRED;
    }

  error = mht_rem (sessions.sessions_table, &key->id,
		   session_free_session, NULL);

#if defined(SERVER_MODE)
  if (thread_p != NULL && thread_p->conn_entry != NULL
      && thread_p->conn_entry->session_p != NULL)
    {
      thread_p->conn_entry->session_p = NULL;
    }
#endif

  csect_exit (thread_p, CSECT_SESSION_STATE);

  return error;
}

static int
session_print_active_sessions (const void *key, void *data, void *args)
{
  SESSION_STATE *session_p = (SESSION_STATE *) data;

  er_log_debug (ARG_FILE_LINE, "session %u", session_p->session_id);

  return NO_ERROR;
}

/*
 * session_check_session () - check if the session state with id session_id
 *			      exists and update the timeout for it
 *   return	    : NO_ERROR or error code
 *   session_id(in) : the identifier for the session
 */
int
session_check_session (THREAD_ENTRY * thread_p, const SESSION_KEY * key)
{
  SESSION_STATE *session_p = NULL;
  int error = NO_ERROR;

  er_log_debug (ARG_FILE_LINE, "updating timeout for session_id %u\n",
		key->id);

  if (csect_enter (thread_p, CSECT_SESSION_STATE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  session_p = (SESSION_STATE *) mht_get (sessions.sessions_table, &key->id);
  if (session_p == NULL)
    {
      csect_exit (thread_p, CSECT_SESSION_STATE);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SES_SESSION_EXPIRED, 0);
      return ER_SES_SESSION_EXPIRED;
    }

  /* update the timeout */
  if (gettimeofday (&(session_p->session_timeout), NULL) != 0)
    {
      csect_exit (thread_p, CSECT_SESSION_STATE);
      return ER_FAILED;
    }
  /* need to do this under the critical section because we cannot rely on the
   * session_p pointer after we left it */
  session_set_conn_entry_data (thread_p, session_p);
  csect_exit (thread_p, CSECT_SESSION_STATE);
  return error;
}

/*
 * session_set_session_key () -
 *   return          : NO_ERROR or error code
 *   session_key(in) : the identifier for the session
 */
int
session_set_session_key (THREAD_ENTRY * thread_p, const SESSION_KEY * key)
{
  SESSION_STATE *session_p = NULL;
  int error = NO_ERROR;

  er_log_debug (ARG_FILE_LINE, "set related socket for session_id %u\n",
		key->id);

  if (csect_enter (thread_p, CSECT_SESSION_STATE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  session_p = (SESSION_STATE *) mht_get (sessions.sessions_table, &key->id);
  if (session_p == NULL)
    {
      csect_exit (thread_p, CSECT_SESSION_STATE);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SES_SESSION_EXPIRED, 0);
      return ER_SES_SESSION_EXPIRED;
    }

  session_p->related_socket = key->fd;

  csect_exit (thread_p, CSECT_SESSION_STATE);

  return error;
}

/*
 * session_remove_expired_sessions () - remove expired sessions
 *   return      : NO_ERROR or error code
 *   timeout(in) :
 */
int
session_remove_expired_sessions (struct timeval *timeout)
{
  int err = NO_ERROR;
  SESSION_TIMEOUT_INFO timeout_info;

  timeout_info.count = -1;
  timeout_info.session_ids = NULL;
  timeout_info.timeout = timeout;

  if (csect_enter (NULL, CSECT_SESSION_STATE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  err = mht_map (sessions.sessions_table, session_check_timeout,
		 &timeout_info);

  csect_exit (NULL, CSECT_SESSION_STATE);

  if (timeout_info.session_ids != NULL)
    {
      assert (timeout_info.count > 0);
      free_and_init (timeout_info.session_ids);
    }

  return err;
}

/*
 * session_check_timeout  () - verify if a session timeout and remove it if
 *			       the timeout expired
 *   return  : NO_ERROR or error code
 *   key(in) : session id
 *   data(in): session state data
 *   args(in): timeout
 */
static int
session_check_timeout (const void *key, void *data, void *args)
{
  int err = NO_ERROR, i = 0;
  SESSION_STATE *session_p = (SESSION_STATE *) data;
  SESSION_TIMEOUT_INFO *timeout_info = (SESSION_TIMEOUT_INFO *) args;

  if (timeout_info->timeout->tv_sec - session_p->session_timeout.tv_sec
      >= prm_get_integer_value (PRM_ID_SESSION_STATE_TIMEOUT))
    {
#if defined(SERVER_MODE)
      /* first see if we still have an active connection */
      if (timeout_info->count == -1)
	{
	  /* we need to get the active connection list */
	  err =
	    css_get_session_ids_for_active_connections (&timeout_info->
							session_ids,
							&timeout_info->count);
	  if (err != NO_ERROR)
	    {
	      return err;
	    }
	}
      for (i = 0; i < timeout_info->count; i++)
	{
	  if (timeout_info->session_ids[i] == session_p->session_id)
	    {
	      /* also update timeout */
	      if (gettimeofday (&(session_p->session_timeout), NULL) != 0)
		{
		  err = ER_FAILED;
		}
	      return err;
	    }
	}
#endif
      /* remove this session: timeout expired and it doesn't have an active
       * connection. */
      er_log_debug (ARG_FILE_LINE, "timeout expired for session %u\n",
		    session_p->session_id);

      err = mht_rem (sessions.sessions_table, &(session_p->session_id),
		     session_free_session, NULL);
      if (err != NO_ERROR)
	{
	  return err;
	}
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "timeout ok for session %u\n",
		    session_p->session_id);
    }

  return err;
}

/*
 * free_session_variable () - free memory allocated for a session variable
 * return : void
 * var (in) : session variable
 */
static void
free_session_variable (SESSION_VARIABLE * var)
{
  if (var == NULL)
    {
      return;
    }

  if (var->name != NULL)
    {
      free_and_init (var->name);
    }

  if (var->value != NULL)
    {
      if (QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (var->value)))
	{
	  /* free allocated string */
	  free_and_init (var->value->data.ch.medium.buf);
	}
      free_and_init (var->value);
    }

  free_and_init (var);
}

/*
 * session_add_variable () - add a session variable to the list
 * return:   error code
 * state_p (in)	  : session state object
 * name (in)	  : name of the variable
 * value (in)	  : variable value
 */
static int
session_add_variable (SESSION_STATE * state_p, const DB_VALUE * name,
		      DB_VALUE * value)
{
  SESSION_VARIABLE *var = NULL;
  SESSION_VARIABLE *current = NULL;
  DB_VALUE *val = NULL;
  int len = 0, count = 0;
  const char *name_str;

  assert (DB_VALUE_DOMAIN_TYPE (name) == DB_TYPE_CHAR);

  name_str = DB_GET_STRING (name);

  assert (name_str != NULL);

  len = DB_GET_STRING_SIZE (name);
  len = MAX (len, strlen ("collect_exec_stats"));

  if (strncasecmp (name_str, "collect_exec_stats", len) == 0)
    {
      if (DB_GET_INT (value) == 1)
	{
	  xmnt_server_start_stats (NULL, false);
	}
      else if (DB_GET_INT (value) == 0)
	{
	  xmnt_server_stop_stats (NULL);
	}
    }
  else if (strncasecmp (name_str, "trace_plan", 10) == 0)
    {
      if (state_p->plan_string != NULL)
	{
	  free_and_init (state_p->plan_string);
	}

      state_p->plan_string = strdup (DB_PULL_STRING (value));
    }

  current = state_p->session_variables;
  while (current)
    {
      assert (current->name != NULL);

      if (intl_identifier_casecmp (name_str, current->name) == 0)
	{
	  /* if it already exists, just update the value */
	  update_session_variable (current, value);
	  return NO_ERROR;
	}

      current = current->next;
      count++;
    }

  if (count >= MAX_SESSION_VARIABLES_COUNT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SES_TOO_MANY_VARIABLES, 0);
      return ER_FAILED;
    }

  /* create a new session variable and add it to the list */
  var = (SESSION_VARIABLE *) malloc (sizeof (SESSION_VARIABLE));
  if (var == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (SESSION_VARIABLE));
      return ER_FAILED;
    }

  len = DB_GET_STRING_SIZE (name);
  var->name = (char *) malloc (len + 1);
  if (var->name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, len + 1);
      goto error;
    }

  memcpy (var->name, name_str, len);
  var->name[len] = 0;

  var->value = db_value_alloc_and_copy (value);

  /* add new variable to the beginning of the list */
  var->next = state_p->session_variables;
  state_p->session_variables = var;

  return NO_ERROR;

error:
  if (var != NULL)
    {
      if (var->name)
	{
	  free_and_init (var->name);
	}

      pr_clear_value (var->value);
      free_and_init (var);
    }

  return ER_FAILED;
}

/*
 * db_value_alloc_and_copy () - create a DB_VALUE on the heap
 * return   : DB_VALUE or NULL
 * src (in) : value to copy
 */
static DB_VALUE *
db_value_alloc_and_copy (const DB_VALUE * src)
{
  DB_TYPE src_dbtype;
  TP_DOMAIN *domain = NULL;
  DB_VALUE *dest = NULL;
  DB_VALUE conv;
  bool use_conv = false;
  int length = 0, precision = 0, scale = 0;
  char *str = NULL;
  const char *src_str;

  dest = (DB_VALUE *) malloc (sizeof (DB_VALUE));
  if (dest == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (DB_VALUE));
      return NULL;
    }

  src_dbtype = DB_VALUE_DOMAIN_TYPE (src);
  if (DB_IS_NULL (src))
    {
      DB_MAKE_NULL (dest);
      return dest;
    }

  if (TP_IS_NUMERIC_TYPE (src_dbtype))
    {
      pr_clone_value ((DB_VALUE *) src, dest);
      return dest;
    }

  if (!QSTR_IS_ANY_CHAR_OR_BIT (src_dbtype))
    {
      /* attempt to convert to varchar */
      DB_MAKE_NULL (&conv);
      domain = db_type_to_db_domain (DB_TYPE_VARCHAR);
      domain->precision = TP_FLOATING_PRECISION_VALUE;

      if (tp_value_cast (src, &conv, domain, false) != DOMAIN_COMPATIBLE)
	{
	  DB_MAKE_NULL (dest);
	  return dest;
	}

      src_dbtype = DB_TYPE_VARCHAR;
      free_and_init (dest);
      dest = db_value_alloc_and_copy (&conv);
      pr_clear_value (&conv);

      return dest;
    }

  length = DB_GET_STRING_SIZE (src);
  scale = 0;
  str = (char *) malloc (length + 1);
  if (str == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      length + 1);
      return NULL;
    }

  src_str = DB_GET_STRING (src);
  if (src_str != NULL)
    {
      memcpy (str, src_str, length);
    }

  precision = db_value_precision (src);
  db_value_domain_init (dest, src_dbtype, precision, scale);
  dest->need_clear = true;
  switch (src_dbtype)
    {
    case DB_TYPE_CHAR:
      DB_MAKE_CHAR (dest, precision, str, length, DB_GET_STRING_CODESET (src),
		    DB_GET_STRING_COLLATION (src));
      break;
    case DB_TYPE_NCHAR:
      DB_MAKE_NCHAR (dest, precision, str, length,
		     DB_GET_STRING_CODESET (src),
		     DB_GET_STRING_COLLATION (src));
      break;
    case DB_TYPE_VARCHAR:
      DB_MAKE_VARCHAR (dest, precision, str, length,
		       DB_GET_STRING_CODESET (src),
		       DB_GET_STRING_COLLATION (src));
      break;
    case DB_TYPE_VARNCHAR:
      DB_MAKE_VARNCHAR (dest, precision, str, length,
			DB_GET_STRING_CODESET (src),
			DB_GET_STRING_COLLATION (src));
      break;
    case DB_TYPE_BIT:
      DB_MAKE_BIT (dest, precision, str, length);
      break;
    case DB_TYPE_VARBIT:
      DB_MAKE_VARBIT (dest, precision, str, length);
      break;
    default:
      assert (false);
      return NULL;
    }

  return dest;
}

/*
 * update_session_variable () - update the value of a session variable
 * return : void
 * var (in/out)	  : the variable to update
 * new_value (in) : the new value
 */
static void
update_session_variable (SESSION_VARIABLE * var, const DB_VALUE * new_value)
{
  if (var->value != NULL)
    {
      if (QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (var->value)))
	{
	  /* free allocated string */
	  free_and_init (var->value->data.ch.medium.buf);
	}
      free_and_init (var->value);
    }

  var->value = db_value_alloc_and_copy (new_value);
}

/*
 * session_drop_variable () - drop a session variable from the list
 * return:   error code
 * state_p (in)	  : session state object
 * name (in)	  : name of the variable
 */
static int
session_drop_variable (SESSION_STATE * state_p, const DB_VALUE * name)
{
  SESSION_VARIABLE *current = NULL, *prev = NULL;
  DB_VALUE *val = NULL;
  int count = 0;
  const char *name_str;

  if (state_p->session_variables == NULL)
    {
      return NO_ERROR;
    }

  assert (DB_VALUE_DOMAIN_TYPE (name) == DB_TYPE_CHAR);
  name_str = DB_GET_STRING (name);

  assert (name_str != NULL);

  current = state_p->session_variables;
  while (current)
    {
      assert (current->name != NULL);

      if (intl_identifier_casecmp (name_str, current->name) == 0)
	{
	  SESSION_VARIABLE *next = current->next;
	  free_session_variable (current);
	  if (prev == NULL)
	    {
	      state_p->session_variables = next;
	    }
	  else
	    {
	      prev->next = next;
	    }
	  return NO_ERROR;
	}

      prev = current;
      current = current->next;
    }

  return NO_ERROR;
}

/*
 * session_get_session_id  () - get the session id associated with a thread
 *   return  : NO_ERROR or error code
 *   thread_p  (in) : thread for which to get the session id
 *   session_id(out): session_id
 */
int
session_get_session_id (THREAD_ENTRY * thread_p, SESSION_KEY * key)
{
  assert (key != NULL);

#if !defined(SERVER_MODE)
  key->id = thread_get_current_session_id ();
  key->fd = INVALID_SOCKET;

  return NO_ERROR;
#else
  if (thread_p == NULL)
    {
      return ER_FAILED;
    }

  if (thread_p->conn_entry == NULL)
    {
      return ER_FAILED;
    }

  key->id = thread_p->conn_entry->session_id;
  key->fd = thread_p->conn_entry->fd;

  return NO_ERROR;
#endif /*SERVER_MODE */
}

/*
 * session_get_last_insert_id  () - get the value of the last inserted id
 *				    in the session associated with a thread
 *   return  : NO_ERROR or error code
 *   thread_p (in)  : thread that identifies the session
 *   value (out)    : pointer into which to store the last insert id value
 *   update_last_insert_id(in): whether update the last insert id
 */
int
session_get_last_insert_id (THREAD_ENTRY * thread_p, DB_VALUE * value,
			    bool update_last_insert_id)
{
  SESSION_STATE *state_p = NULL;

  assert (value != NULL);
  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }
  if (update_last_insert_id && !DB_IS_NULL (&state_p->cur_insert_id))
    {
      pr_clone_value (&state_p->cur_insert_id, &state_p->last_insert_id);
      pr_clear_value (&state_p->cur_insert_id);
    }
  pr_clone_value (&state_p->last_insert_id, value);

  return NO_ERROR;
}

/*
 * session_set_cur_insert_id  () - set the value of the current inserted id
 *				in the session associated with a thread
 *   return  : NO_ERROR or error code
 *   thread_p (in) : thread that identifies the session
 *   value (in)	   : the value of the last inserted id
 *   force (in)    : update the value unconditionally
 *
 * Note: Even though we allow other data types for serial columns, the session
 * keeps the value of the last insert id as a DB_TYPE_NUMERIC. This function
 * performs a coercion here if needed.
 */
int
session_set_cur_insert_id (THREAD_ENTRY * thread_p, const DB_VALUE * value,
			   bool force)
{
  SESSION_STATE *state_p = NULL;
  bool need_coercion = false;

  if (DB_VALUE_TYPE (value) != DB_TYPE_NUMERIC)
    {
      need_coercion = true;
    }
  else if (DB_VALUE_PRECISION (value) != DB_MAX_NUMERIC_PRECISION ||
	   DB_VALUE_SCALE (value) != 0)
    {
      need_coercion = true;
    }

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  if ((force == false && state_p->is_last_insert_id_generated == true)
      || state_p->is_trigger_involved == true)
    {
      return NO_ERROR;
    }

  if (!DB_IS_NULL (&state_p->cur_insert_id))
    {
      pr_clone_value (&state_p->cur_insert_id, &state_p->last_insert_id);
      pr_clear_value (&state_p->cur_insert_id);
    }

  if (!need_coercion)
    {
      pr_clone_value ((DB_VALUE *) value, &state_p->cur_insert_id);
    }
  else
    {
      TP_DOMAIN *num = tp_domain_resolve_default (DB_TYPE_NUMERIC);
      num->precision = DB_MAX_NUMERIC_PRECISION;
      num->scale = 0;
      if (tp_value_cast (value, &state_p->cur_insert_id, num, false)
	  != DOMAIN_COMPATIBLE)
	{
	  pr_clear_value (&state_p->cur_insert_id);
	  return ER_FAILED;
	}
    }

  state_p->is_last_insert_id_generated = true;

  return NO_ERROR;
}

/*
 * session_reset_cur_insert_id () - reset the current insert_id as NULL
 *                                  when the insert fail.
 *   return  : NO_ERROR or error code
 *   thread_p (in) : thread that identifies the session
 */
int
session_reset_cur_insert_id (THREAD_ENTRY * thread_p)
{
  SESSION_STATE *state_p = NULL;

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  if (state_p->is_last_insert_id_generated == false)
    {
      return NO_ERROR;
    }

  pr_clear_value (&state_p->cur_insert_id);
  state_p->is_last_insert_id_generated = false;

  return NO_ERROR;
}

/*
 * session_begin_insert_values  () - set is_last_insert_id_generated to false
 *                                  in the session associated with a thread
 *   return  : NO_ERROR or error code
 *   thread_p (in) : thread that identifies the session
 */
int
session_begin_insert_values (THREAD_ENTRY * thread_p)
{
  SESSION_STATE *state_p = NULL;

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  state_p->is_last_insert_id_generated = false;

  return NO_ERROR;
}

/*
 * session_set_trigger_state () - set is_trigger_involved
 *
 *   return  : NO_ERROR or error code
 *   thread_p (in) : thread that identifies the session
 *   in_trigger(in):
 */
int
session_set_trigger_state (THREAD_ENTRY * thread_p, bool in_trigger)
{
  SESSION_STATE *state_p = NULL;

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  state_p->is_trigger_involved = in_trigger;

  return NO_ERROR;
}



/*
 * session_get_row_count () - get the affected row count from the session
 *			      associated with a thread
 *   return  : NO_ERROR or error code
 *   thread_p(in)	  : thread that identifies the session
 *   row_count(out)	  : pointer into which to store the count
 *
 * Note: Row count refers to the number of rows affected by the last INSERT,
 * UPDATE or DELETE statement
 */
int
session_get_row_count (THREAD_ENTRY * thread_p, int *row_count)
{
  SESSION_STATE *state_p = NULL;

  assert (row_count != NULL);
  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  *row_count = state_p->row_count;

  return NO_ERROR;
}

/*
 * session_set_row_count () - set the count of affected rows for a session
 *
 *   return  : NO_ERROR or error code
 *   thread_p(in)     : thread that identifies the session
 *   row_count(in)    : row count
 */
int
session_set_row_count (THREAD_ENTRY * thread_p, const int row_count)
{
  SESSION_STATE *state_p = NULL;
  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  er_log_debug (ARG_FILE_LINE,
		"setting row_count for session %u to %d\n",
		state_p->session_id, state_p->row_count);
  state_p->row_count = row_count;

  return NO_ERROR;
}

/*
 * session_get_session_params () - get the list of session parameters stored
 *				   in session_state
 *
 *   return			 : NO_ERROR or error code
 *   thread_p(in)		 : thread that identifies the session
 *   session_parameters_ptr(out) : pointer to session parameter list
 */
int
session_get_session_parameters (THREAD_ENTRY * thread_p,
				SESSION_PARAM ** session_parameters_ptr)
{
  SESSION_STATE *state_p = NULL;

  assert (session_parameters_ptr != NULL);
  if (*session_parameters_ptr)
    {
      free_and_init (*session_parameters_ptr);
    }
  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  *session_parameters_ptr = state_p->session_parameters;

  return NO_ERROR;
}

/*
 * session_set_session_parameters () - set session parameters to session state
 *
 * return		   : error code
 * thread_p (in)	   : worker thread
 * session_parameters (in) : array of session parameters
 */
int
session_set_session_parameters (THREAD_ENTRY * thread_p,
				SESSION_PARAM * session_parameters)
{
  SESSION_STATE *state_p = NULL;

  assert (session_parameters != NULL);
  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  state_p->session_parameters = session_parameters;

  return NO_ERROR;
}

/*
 * session_create_prepared_statement () - create a prepared statement and add
 *					  it to the prepared statements list
 * return : NO_ERROR or error code
 * thread_p (in)	:
 * user (in)		: OID of the user who prepared this statement
 * name (in)		: the name of the statement
 * alias_print(in)	: the printed compiled statement
 * info (in)		: serialized prepared statement info
 * info_len (in)	: serialized buffer length
 *
 * Note: This function assumes that the memory for its arguments was
 * dynamically allocated and does not copy the values received. It's important
 * that the caller never frees this memory. If an error occurs, this function
 * will free the memory allocated for its arguments.
 */
int
session_create_prepared_statement (THREAD_ENTRY * thread_p, OID user,
				   char *name, char *alias_print, char *info,
				   int info_len)
{
  SESSION_STATE *state_p = NULL;
  PREPARED_STATEMENT *stmt_p = NULL;
  int err = NO_ERROR;

  stmt_p = (PREPARED_STATEMENT *) malloc (sizeof (PREPARED_STATEMENT));
  if (stmt_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (PREPARED_STATEMENT));
      err = ER_FAILED;
      goto error;
    }

  COPY_OID (&(stmt_p->user), &user);
  stmt_p->name = name;
  stmt_p->alias_print = alias_print;
  stmt_p->info_length = info_len;
  stmt_p->info = info;
  stmt_p->next = NULL;

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      err = ER_FAILED;
      goto error;
    }

  er_log_debug (ARG_FILE_LINE, "create statement %s(%d)\n", name,
		state_p->session_id);

  if (state_p->statements == NULL)
    {
      state_p->statements = stmt_p;
    }
  else
    {
      /* find and remove prepared statements with the same name */
      int cnt = 0;
      PREPARED_STATEMENT *current = NULL, *prev = NULL;

      current = state_p->statements;
      while (current != NULL)
	{
	  if (intl_identifier_casecmp (current->name, name) == 0)
	    {
	      /* we need to remove it */
	      if (prev == NULL)
		{
		  state_p->statements = current->next;
		}
	      else
		{
		  prev->next = current->next;
		}
	      current->next = NULL;
	      session_free_prepared_statement (current);
	      break;
	    }
	  cnt++;
	  prev = current;
	  current = current->next;
	}

      if (cnt >= MAX_PREPARED_STATEMENTS_COUNT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_SES_TOO_MANY_STATEMENTS, 0);
	  err = ER_FAILED;
	  goto error;
	}
      else if (state_p->statements == NULL)
	{
	  /* add the new statement at the beginning of the list */
	  state_p->statements = stmt_p;
	}
      else
	{
	  stmt_p->next = state_p->statements;
	  state_p->statements = stmt_p;
	}
    }

  er_log_debug (ARG_FILE_LINE, "success %s(%d)\n", name, state_p->session_id);

  return NO_ERROR;

error:
  if (stmt_p != NULL)
    {
      free_and_init (stmt_p);
    }

  return err;
}

/*
 * session_get_prepared_statement () - get available information about a
 *				       prepared statement
 * return:   NO_ERROR or error code
 * thread_p (in)	:
 * name (in)		: the name of the prepared statement
 * info (out)		: serialized prepared statement information
 * info_len (out)	: serialized buffer length
 * xasl_id (out)	: XASL ID for this statement
 *
 * Note: This function allocates memory for query, columns and parameters
 * using db_private_alloc. This memory must be freed by the caller by using
 * db_private_free.
 */
int
session_get_prepared_statement (THREAD_ENTRY * thread_p, const char *name,
				char **info, int *info_len, XASL_ID * xasl_id)
{
  SESSION_STATE *state_p = NULL;
  PREPARED_STATEMENT *stmt_p = NULL;
  int err = NO_ERROR;
  XASL_CACHE_ENTRY *entry = NULL;
  OID user;
  const char *alias_print;
  char *data = NULL;

  assert (xasl_id != NULL);
  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }
  for (stmt_p = state_p->statements; stmt_p != NULL; stmt_p = stmt_p->next)
    {
      if (intl_identifier_casecmp (stmt_p->name, name) == 0)
	{
	  break;
	}
    }

  if (stmt_p == NULL)
    {
      /* prepared statement not found */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_PREPARED_NAME_NOT_FOUND,
	      1, name);
      return ER_FAILED;
    }

  /* alias_print */
  alias_print = stmt_p->alias_print;
  *info_len = stmt_p->info_length;

  if (stmt_p->info_length == 0)
    {
      *info = NULL;
    }
  else
    {
      data = (char *) malloc (stmt_p->info_length);
      if (data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, stmt_p->info_length);
	  return ER_FAILED;
	}
      memcpy (data, stmt_p->info, stmt_p->info_length);
      *info = data;
    }

  /* copy the user identifier, we will need it below */
  COPY_OID (&user, &stmt_p->user);

  /* since the xasl id is not session specific, we can fetch it outside of
     the session critical section */
  if (alias_print == NULL)
    {
      /* if we don't have an alias print, we do not search for the XASL id */
      XASL_ID_SET_NULL (xasl_id);
      er_log_debug (ARG_FILE_LINE, "found null xasl_id for %s(%d)\n", name,
		    state_p->session_id);
      return NO_ERROR;
    }

  entry = qexec_lookup_xasl_cache_ent (thread_p, alias_print, &user);
  if (entry == NULL)
    {
      XASL_ID_SET_NULL (xasl_id);
      er_log_debug (ARG_FILE_LINE, "found null xasl_id for %s(%d)\n", name,
		    state_p->session_id);
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "found xasl_id for %s(%d)\n", name,
		    state_p->session_id);
      XASL_ID_COPY (xasl_id, &entry->xasl_id);
    }

  return NO_ERROR;
}

/*
 * session_delete_prepared_statement () - delete a prepared statement
 * return : error code or NO_ERROR
 * thread_p (in)  :
 * name (in)	  : name of the prepared statement
 */
int
session_delete_prepared_statement (THREAD_ENTRY * thread_p, const char *name)
{
  SESSION_STATE *state_p = NULL;
  PREPARED_STATEMENT *stmt_p = NULL, *prev = NULL;
  int err = NO_ERROR;
  bool found = false;

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  er_log_debug (ARG_FILE_LINE, "dropping %s from session_id %d\n", name,
		state_p->session_id);

  for (stmt_p = state_p->statements, prev = NULL; stmt_p != NULL;
       prev = stmt_p, stmt_p = stmt_p->next)
    {
      if (intl_identifier_casecmp (stmt_p->name, name) == 0)
	{
	  if (prev == NULL)
	    {
	      state_p->statements = stmt_p->next;
	    }
	  else
	    {
	      prev->next = stmt_p->next;
	    }
	  stmt_p->next = NULL;
	  session_free_prepared_statement (stmt_p);
	  found = true;
	  break;
	}
    }

  if (!found)
    {
      /* prepared statement not found */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_PREPARED_NAME_NOT_FOUND,
	      1, name);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * login_user () - login user
 * return	  : error code
 * thread_p	  : worker thread
 * username(in)	  : name of the user
 */
int
login_user (THREAD_ENTRY * thread_p, const char *username)
{
  int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  LOG_TDES *tdes = NULL;

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      strncpy (tdes->client.db_user, username, DB_MAX_USER_LENGTH);
    }

  return NO_ERROR;
}

/*
 * session_set_session_variables () - set session variables
 * return : error code
 * thread_p (in) : worker thread
 * values (in)	 : array of variables to set
 * count (in)	 : number of elements in array
 */
int
session_set_session_variables (THREAD_ENTRY * thread_p, DB_VALUE * values,
			       const int count)
{
  SESSION_STATE *state_p = NULL;
  int i = 0;

  assert (count % 2 == 0);
  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  for (i = 0; i < count; i += 2)
    {
      if (session_add_variable (state_p, &values[i], &values[i + 1])
	  != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * session_define_variable () - define a session variable
 * return : int
 * thread_p (in) : worker thread
 * name (in)	 : name of the variable
 * value (in)	 : variable value
 */
int
session_define_variable (THREAD_ENTRY * thread_p, DB_VALUE * name,
			 DB_VALUE * value, DB_VALUE * result)
{
  SESSION_STATE *state_p = NULL;
  int err = NO_ERROR;

  assert (DB_VALUE_DOMAIN_TYPE (name) == DB_TYPE_CHAR);
  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  err = session_add_variable (state_p, name, value);
  if (err == NO_ERROR)
    {
      pr_clone_value (value, result);
    }
  else
    {
      DB_MAKE_NULL (result);
    }

  return err;
}

/*
 * session_get_variable () - get the value of a variable
 * return : error code
 * thread_p (in)  : worker thread
 * name (in)	  : name of the variable
 * result (out)	  : variable value
 */
int
session_get_variable (THREAD_ENTRY * thread_p, const DB_VALUE * name,
		      DB_VALUE * result)
{
  SESSION_STATE *state_p = NULL;
  const char *name_str;
  SESSION_VARIABLE *var;

  assert (DB_VALUE_DOMAIN_TYPE (name) == DB_TYPE_CHAR);

  name_str = DB_GET_STRING (name);
  assert (name_str != NULL);
  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  var = state_p->session_variables;
  while (var != NULL)
    {
      assert (var->name != NULL);

      if (intl_identifier_casecmp (var->name, name_str) == 0)
	{
	  pr_clone_value (var->value, result);
	  break;
	}
      var = var->next;
    }

  if (var == NULL)
    {
      /* we didn't find it, set error and exit */
      char *var_name = NULL;
      int name_len = strlen (name_str);

      var_name = (char *) malloc (name_len + 1);
      if (var_name != NULL)
	{
	  memcpy (var_name, name_str, name_len);
	  var_name[name_len] = 0;
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SES_VARIABLE_NOT_FOUND, 1,
	      var_name);

      if (var_name != NULL)
	{
	  free_and_init (var_name);
	}

      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * session_get_session_variable_no_copy () - get the value of a session
 *					     variable
 * return : int
 * thread_p (in) : worker thread
 * name (in)	 : name of the variable
 * value (in/out): variable value
 * Note: This function gets a reference to a session variable from the session
 * state object. Because it gets the actual pointer, it is not thread safe
 * and it should only be called in the stand alone mode
 */
int
session_get_variable_no_copy (THREAD_ENTRY * thread_p, const DB_VALUE * name,
			      DB_VALUE ** result)
{
  SESSION_KEY key;
  SESSION_STATE *state_p = NULL;
  int name_len;
  const char *name_str;
  SESSION_VARIABLE *var;

#if defined (SERVER_MODE)
  /* do not call this function in a multi-threaded context */
  assert (false);
  return ER_FAILED;
#endif

  assert (name != NULL);
  assert (DB_VALUE_DOMAIN_TYPE (name) == DB_TYPE_CHAR);
  assert (result != NULL);

  name_str = DB_GET_STRING (name);
  name_len = (name_str != NULL) ? strlen (name_str) : 0;

  if (session_get_session_id (thread_p, &key) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (!sessions_is_states_table_initialized ())
    {
      return ER_FAILED;
    }

  state_p = mht_get (sessions.sessions_table, &key.id);
  if (state_p == NULL || state_p->related_socket != key.fd)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SES_SESSION_EXPIRED, 0);
      return ER_FAILED;
    }

  var = state_p->session_variables;
  while (var != NULL)
    {
      assert (var->name != NULL);

      if (name_len != strlen (var->name))
	{
	  var = var->next;
	  continue;
	}
      if (strncasecmp (var->name, name_str, name_len) == 0)
	{
	  *result = var->value;
	  break;
	}
      var = var->next;
    }

  if (var == NULL)
    {
      /* we didn't find it, set error and exit */
      char *var_name = NULL;

      var_name = (char *) malloc (name_len + 1);
      if (var_name != NULL)
	{
	  memcpy (var_name, name_str, name_len);
	  var_name[name_len] = 0;
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SES_VARIABLE_NOT_FOUND, 1,
	      var_name);

      if (var_name != NULL)
	{
	  free_and_init (var_name);
	}

      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * session_drop_session_variables () - drop session variables
 * return : error code
 * thread_p (in) : worker thread
 * values (in)	 : array of variables to drop
 * count (in)	 : number of elements in array
 */
int
session_drop_session_variables (THREAD_ENTRY * thread_p, DB_VALUE * values,
				const int count)
{
  SESSION_STATE *state_p = NULL;
  int i = 0;

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  for (i = 0; i < count; i++)
    {
      if (session_drop_variable (state_p, &values[i]) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * session_get_exec_stats_and_clear () - get execution statistics
 * return : error code
 * thread_p (in)  : worker thread
 * name (in)      : name of the stats
 * result (out)   : stats value
 */
int
session_get_exec_stats_and_clear (THREAD_ENTRY * thread_p,
				  const DB_VALUE * name, DB_VALUE * result)
{
  const char *name_str;
  UINT64 stat_val;

  assert (DB_VALUE_DOMAIN_TYPE (name) == DB_TYPE_CHAR);

  name_str = DB_GET_STRING (name);

  stat_val = mnt_x_get_stats_and_clear (thread_p, name_str);
  DB_MAKE_BIGINT (result, stat_val);

  return NO_ERROR;
}

/*
 * session_states_dump () - dump the session states information
 *   return: void
 *   thread_p (in) : the thread executing this function
 */
void
session_states_dump (THREAD_ENTRY * thread_p)
{
  if (csect_enter (thread_p, CSECT_SESSION_STATE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  if (sessions.sessions_table != NULL)
    {
      fprintf (stdout, "\nSESSION COUNT = %d\n",
	       mht_count (sessions.sessions_table));
      (void) mht_map (sessions.sessions_table, session_dump_session, NULL);
      fflush (stdout);
    }

  csect_exit (thread_p, CSECT_SESSION_STATE);
}

/*
 * session_dump_session () - dump a session state
 *   return  : NO_ERROR
 *   key(in) : the key from the MHT_TABLE for this session
 *   data(in): session state data
 *   args(in): not used
 */
static int
session_dump_session (const void *key, void *data, void *args)
{
  SESSION_STATE *session = (SESSION_STATE *) data;
  SESSION_ID *sess_key = (SESSION_ID *) key;
  SESSION_VARIABLE *vcurent, *vnext;
  PREPARED_STATEMENT *pcurent, *pnext;
  DB_VALUE v;

  fprintf (stdout, "SESSION ID = %d\n", session->session_id);

  db_value_coerce (&session->last_insert_id, &v,
		   db_type_to_db_domain (DB_TYPE_VARCHAR));
  fprintf (stdout, "\tLAST_INSERT_ID = %s\n", DB_PULL_STRING (&v));
  db_value_clear (&v);

  fprintf (stdout, "\tROW_COUNT = %d\n", session->row_count);

  fprintf (stdout, "\tSESSION VARIABLES\n");
  vcurent = session->session_variables;
  while (vcurent != NULL)
    {
      vnext = vcurent->next;
      session_dump_variable (vcurent);
      vcurent = vnext;
    }

  fprintf (stdout, "\tPREPRARE STATEMENTS\n");
  pcurent = session->statements;
  while (pcurent != NULL)
    {
      pnext = pcurent->next;
      session_dump_prepared_statement (pcurent);
      pcurent = pnext;
    }

  fprintf (stdout, "\n");
  return NO_ERROR;
}

/*
 * session_dump_variable () - dump a session variable
 * return : void
 * var (in) : session variable
 */
static void
session_dump_variable (SESSION_VARIABLE * var)
{
  DB_VALUE v;

  if (var == NULL)
    {
      return;
    }

  if (var->name != NULL)
    {
      fprintf (stdout, "\t\t%s = ", var->name);
    }

  if (var->value != NULL)
    {
      db_value_coerce (var->value, &v,
		       db_type_to_db_domain (DB_TYPE_VARCHAR));
      fprintf (stdout, "%s\n", DB_PULL_STRING (&v));
      db_value_clear (&v);
    }
}

/*
 * session_dump_prepared_statement () - dump a prepared statement
 * return : void
 * stmt_p (in) : prepared statement object
 */
static void
session_dump_prepared_statement (PREPARED_STATEMENT * stmt_p)
{
  if (stmt_p == NULL)
    {
      return;
    }

  if (stmt_p->name != NULL)
    {
      fprintf (stdout, "\t\t%s = ", stmt_p->name);
    }

  if (stmt_p->alias_print != NULL)
    {
      fprintf (stdout, "%s\n", stmt_p->alias_print);
    }
}

/*
 * qentry_to_sentry () - create a session query entry from a query manager
 *			 entry
 * return : session query entry or NULL
 * qentry_p (in) : query manager query entry
 */
static SESSION_QUERY_ENTRY *
qentry_to_sentry (QMGR_QUERY_ENTRY * qentry_p)
{
  SESSION_QUERY_ENTRY *sqentry_p = NULL;
  assert (qentry_p != NULL);
  sqentry_p = (SESSION_QUERY_ENTRY *) malloc (sizeof (SESSION_QUERY_ENTRY));
  if (sqentry_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (SESSION_QUERY_ENTRY));
      return NULL;
    }
  sqentry_p->query_id = qentry_p->query_id;
  sqentry_p->list_id = qentry_p->list_id;
  sqentry_p->temp_file = qentry_p->temp_vfid;

  qentry_p->list_id = NULL;
  qentry_p->temp_vfid = NULL;

  sqentry_p->num_tmp = qentry_p->num_tmp;
  sqentry_p->total_count = qentry_p->total_count;
  sqentry_p->query_flag = qentry_p->query_flag;

  sqentry_p->next = NULL;

  return sqentry_p;
}

/*
 * session_preserve_temporary_files () - remove list files used by qentry_p
 *					 from the file manager so that it
 *					 doesn't delete them at transaction
 *					 end
 * return : error code or NO_ERROR
 * thread_p (in) :
 * qentry_p (in) :  query entry
 */
static int
session_preserve_temporary_files (THREAD_ENTRY * thread_p,
				  SESSION_QUERY_ENTRY * qentry_p)
{
  VFID *vfids = NULL;
  int count = 0;
  int i = 0;
  QMGR_TEMP_FILE *tfile_vfid_p = NULL, *temp = NULL;

  if (qentry_p == NULL)
    {
      assert (false);
      return NO_ERROR;
    }
  if (qentry_p->list_id == NULL)
    {
      return NO_ERROR;
    }
  if (qentry_p->list_id->page_cnt == 0)
    {
      /* make sure temp_file is not cyclic */
      if (qentry_p->temp_file)
	{
	  qentry_p->temp_file->prev->next = NULL;
	}
      return NO_ERROR;
    }
  if (qentry_p->temp_file)
    {
      tfile_vfid_p = qentry_p->temp_file;
      tfile_vfid_p->prev->next = NULL;
      while (tfile_vfid_p)
	{
	  file_preserve_temporary (thread_p, &tfile_vfid_p->temp_vfid);
	  temp = tfile_vfid_p;
	  tfile_vfid_p = tfile_vfid_p->next;
	}
    }
  return NO_ERROR;
}

/*
 * sentry_to_qentry () - create a query manager entry from a session query
 *			 entry
 * return : void
 * sentry_p (in)     : session query entry
 * qentry_p (in/out) : query manager query entry
 */
static void
sentry_to_qentry (const SESSION_QUERY_ENTRY * sentry_p,
		  QMGR_QUERY_ENTRY * qentry_p)
{
  qentry_p->query_id = sentry_p->query_id;
  qentry_p->list_id = sentry_p->list_id;
  qentry_p->temp_vfid = sentry_p->temp_file;

  qentry_p->list_ent = NULL;
  qentry_p->num_tmp = sentry_p->num_tmp;
  qentry_p->total_count = sentry_p->total_count;
  qentry_p->query_mode = QUERY_COMPLETED;
  qentry_p->query_flag = sentry_p->query_flag;
  qentry_p->save_vpid.pageid = NULL_PAGEID;
  qentry_p->save_vpid.volid = NULL_VOLID;
  XASL_ID_SET_NULL (&qentry_p->xasl_id);
  qentry_p->xasl_ent = NULL;
  qentry_p->er_msg = NULL;
  qentry_p->is_holdable = true;
}

/*
 * session_store_query_entry_info () - create a query entry
 * return : void
 * thread_p (in) :
 * qentry_p (in) : query entry
 */
void
session_store_query_entry_info (THREAD_ENTRY * thread_p,
				QMGR_QUERY_ENTRY * qentry_p)
{
  SESSION_STATE *state_p = NULL;
  SESSION_QUERY_ENTRY *sqentry_p = NULL, *current = NULL;

  assert (qentry_p != NULL);

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return;
    }

  /* iterate over queries so we don't add the same query twice */
  current = state_p->queries;
  while (current != NULL)
    {
      if (current->query_id == qentry_p->query_id)
	{
	  /* we don't need to add it again, just set list_id to null
	     so that the query manager does not drop it */
	  qentry_p->list_id = NULL;
	  qentry_p->temp_vfid = NULL;
	  return;
	}
      current = current->next;
    }

  /* We didn't find it. Create an entry and add it to the list */
  sqentry_p = qentry_to_sentry (qentry_p);
  if (sqentry_p == NULL)
    {
      return;
    }
  session_preserve_temporary_files (thread_p, sqentry_p);

  if (state_p->queries == NULL)
    {
      state_p->queries = sqentry_p;
    }
  else
    {
      sqentry_p->next = state_p->queries;
      state_p->queries = sqentry_p;
    }

  mnt_qm_holdable_cursor (thread_p, ++sessions.num_holdable_cursors);
}

/*
 * session_free_sentry_data () - close list files associated with a query
 *				 entry
 * return : void
 * thread_p (in) :
 * sentry_p (in) :
 */
static void
session_free_sentry_data (THREAD_ENTRY * thread_p,
			  SESSION_QUERY_ENTRY * sentry_p)
{
  if (sentry_p == NULL)
    {
      return;
    }

  if (sentry_p->list_id != NULL)
    {
      qfile_close_list (thread_p, sentry_p->list_id);
      qfile_free_list_id (sentry_p->list_id);
    }

  if (sentry_p->temp_file != NULL)
    {
      qmgr_free_temp_file_list (thread_p, sentry_p->temp_file,
				sentry_p->query_id, false);
    }

  mnt_qm_holdable_cursor (thread_p, --sessions.num_holdable_cursors);
}

/*
 * session_load_query_entry_info () - search for a query entry
 * return : error code or NO_ERROR
 * thread_p (in) :
 * qentry_p (in/out) : query entry
 */
int
session_load_query_entry_info (THREAD_ENTRY * thread_p,
			       QMGR_QUERY_ENTRY * qentry_p)
{
  SESSION_STATE *state_p = NULL;
  SESSION_QUERY_ENTRY *sentry_p = NULL;

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  sentry_p = state_p->queries;
  while (sentry_p != NULL)
    {
      if (sentry_p->query_id == qentry_p->query_id)
	{
	  sentry_to_qentry (sentry_p, qentry_p);
	  return NO_ERROR;
	}
      sentry_p = sentry_p->next;
    }
  return ER_FAILED;
}

/*
 * session_remove_query_entry_info () - remove a query entry from the holdable
 *					queries list
 * return : error code or NO_ERROR
 * thread_p (in) : active thread
 * query_id (in) : query id
 */
int
session_remove_query_entry_info (THREAD_ENTRY * thread_p,
				 const QUERY_ID query_id)
{
  SESSION_STATE *state_p = NULL;
  SESSION_QUERY_ENTRY *sentry_p = NULL, *prev = NULL;
  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  sentry_p = state_p->queries;
  while (sentry_p != NULL)
    {
      if (sentry_p->query_id == query_id)
	{
	  /* remove sentry_p from the queries list */
	  if (prev == NULL)
	    {
	      state_p->queries = sentry_p->next;
	    }
	  else
	    {
	      prev->next = sentry_p->next;
	    }
	  session_free_sentry_data (thread_p, sentry_p);

	  free_and_init (sentry_p);
	  break;
	}
      prev = sentry_p;
      sentry_p = sentry_p->next;
    }

  return NO_ERROR;
}

/*
 * session_remove_query_entry_info () - remove a query entry from the holdable
 *					queries list but do not close the
 *					associated list files
 * return : error code or NO_ERROR
 * thread_p (in) : active thread
 * query_id (in) : query id
 */
int
session_clear_query_entry_info (THREAD_ENTRY * thread_p,
				const QUERY_ID query_id)
{
  SESSION_STATE *state_p = NULL;
  SESSION_QUERY_ENTRY *sentry_p = NULL, *prev = NULL;

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  sentry_p = state_p->queries;
  while (sentry_p != NULL)
    {
      if (sentry_p->query_id == query_id)
	{
	  /* remove sentry_p from the queries list */
	  if (prev == NULL)
	    {
	      state_p->queries = sentry_p->next;
	    }
	  else
	    {
	      prev->next = sentry_p->next;
	    }

	  free_and_init (sentry_p);
	  mnt_qm_holdable_cursor (thread_p, --sessions.num_holdable_cursors);

	  break;
	}
      prev = sentry_p;
      sentry_p = sentry_p->next;
    }

  return NO_ERROR;
}

/*
 * session_set_conn_entry_data () - set references to session state objects
 *				    into the connection entry associated
 *				    with this thread
 * return : void
 * thread_p (in) : current thread
 * session_p (in) : session state object
 */
static void
session_set_conn_entry_data (THREAD_ENTRY * thread_p,
			     SESSION_STATE * session_p)
{
#if defined(SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
      if (thread_p == NULL)
	{
	  return;
	}
    }

  if (thread_p->conn_entry != NULL)
    {
      /* If we have a connection entry associated with this thread, setup
       * session data for this conn_entry
       */
      thread_p->conn_entry->session_p = session_p;
    }
#endif
}

/*
 * session_get_session_parameter () - get a reference to a system parameter
 * return : session parameter object
 * thread_p (in) : thread entry
 * id (in) : parameter id
 */
SESSION_PARAM *
session_get_session_parameter (THREAD_ENTRY * thread_p, PARAM_ID id)
{
  int i, count;
  SESSION_STATE *session_p = NULL;
  session_p = session_get_session_state (thread_p);
  if (session_p == NULL)
    {
      return NULL;
    }
  assert (id <= PRM_LAST_ID);
  count = sysprm_get_session_parameters_count ();
  for (i = 0; i < count; i++)
    {
      if (session_p->session_parameters[i].prm_id == id)
	{
	  return &session_p->session_parameters[i];
	}
    }
  return NULL;
}

/*
 * session_get_session_state () - get the session state object
 * return : session state object or NULL in case of error
 * thread_p (in) : thread for which to get the session
 */
static SESSION_STATE *
session_get_session_state (THREAD_ENTRY * thread_p)
{
#if defined(SERVER_MODE)
  /* The session state object cached in the conn_entry object associated with
   * every server request. Instead of accessing the session states hash
   * through a critical section, we can just return the hashed value. */
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  if (thread_p != NULL && thread_p->conn_entry != NULL
      && thread_p->conn_entry->session_p != NULL)
    {
      return thread_p->conn_entry->session_p;
    }
  else
    {
      /* any request for this object should find it cached in the connection
       * entry */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SES_SESSION_EXPIRED, 0);
      return NULL;
    }
#else
  SESSION_KEY key;
  int error = NO_ERROR;
  SESSION_STATE *state_p = NULL;
  error = session_get_session_id (thread_p, &key);
  if (error != NO_ERROR)
    {
      return NULL;
    }

  error = csect_enter_as_reader (thread_p, CSECT_SESSION_STATE, INF_WAIT);
  if (error != NO_ERROR)
    {
      return NULL;
    }

  if (!sessions_is_states_table_initialized ())
    {
      csect_exit (thread_p, CSECT_SESSION_STATE);
      return NULL;
    }

  state_p = mht_get (sessions.sessions_table, &key.id);
  csect_exit (thread_p, CSECT_SESSION_STATE);

  if (state_p == NULL || state_p->related_socket != key.fd)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SES_SESSION_EXPIRED, 0);
      return NULL;
    }

  return state_p;
#endif
}

/*
 * session_get_trace_stats () - return query trace result
 *   return  : query trace
 *   result(out) :
 */
int
session_get_trace_stats (THREAD_ENTRY * thread_p, DB_VALUE * result)
{
  SESSION_STATE *state_p = NULL;
  char *trace_str = NULL;
  size_t sizeloc;
  FILE *fp;
  json_t *plan, *xasl, *stats;
  DB_VALUE temp_result;

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  if (state_p->plan_string == NULL && state_p->trace_stats == NULL)
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  if (state_p->trace_format == QUERY_TRACE_TEXT)
    {
      fp = port_open_memstream (&trace_str, &sizeloc);
      if (fp)
	{
	  if (state_p->plan_string != NULL)
	    {
	      fprintf (fp, "\nQuery Plan:\n%s", state_p->plan_string);
	    }

	  if (state_p->trace_stats != NULL)
	    {
	      fprintf (fp, "\nTrace Statistics:\n%s", state_p->trace_stats);
	    }

	  port_close_memstream (fp, &trace_str, &sizeloc);
	}
    }
  else if (state_p->trace_format == QUERY_TRACE_JSON)
    {
      stats = json_object ();

      if (state_p->plan_string != NULL)
	{
	  plan = json_loads (state_p->plan_string, 0, NULL);
	  if (plan != NULL)
	    {
	      json_object_set_new (stats, "Query Plan", plan);
	    }
	}

      if (state_p->trace_stats != NULL)
	{
	  xasl = json_loads (state_p->trace_stats, 0, NULL);
	  if (xasl != NULL)
	    {
	      json_object_set_new (stats, "Trace Statistics", xasl);
	    }
	}

      trace_str = json_dumps (stats, JSON_INDENT (2) | JSON_PRESERVE_ORDER);

      json_object_clear (stats);
      json_decref (stats);
    }

  if (trace_str != NULL)
    {
      DB_MAKE_STRING (&temp_result, trace_str);
      pr_clone_value (&temp_result, result);
      free_and_init (trace_str);
    }
  else
    {
      DB_MAKE_NULL (result);
    }

  thread_set_clear_trace (thread_p, true);

  return NO_ERROR;
}

/*
 * session_set_trace_stats () - save query trace result to session
 *   return  :
 *   stats(in) :
 *   format(in) :
 */
int
session_set_trace_stats (THREAD_ENTRY * thread_p, char *stats, int format)
{
  SESSION_STATE *state_p = NULL;

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  if (state_p->trace_stats != NULL)
    {
      free_and_init (state_p->trace_stats);
    }

  state_p->trace_stats = stats;
  state_p->trace_format = format;

  return NO_ERROR;
}

/*
 * session_clear_trace_stats () - clear query trace result from session
 *   return  :
 *   stats(in) :
 *   format(in) :
 */
int
session_clear_trace_stats (THREAD_ENTRY * thread_p)
{
  SESSION_STATE *state_p = NULL;

  assert (thread_need_clear_trace (thread_p) == true);

  state_p = session_get_session_state (thread_p);
  if (state_p == NULL)
    {
      return ER_FAILED;
    }

  if (state_p->plan_string != NULL)
    {
      free_and_init (state_p->plan_string);
    }

  if (state_p->trace_stats != NULL)
    {
      free_and_init (state_p->trace_stats);
    }

  thread_set_clear_trace (thread_p, false);

  return NO_ERROR;
}
