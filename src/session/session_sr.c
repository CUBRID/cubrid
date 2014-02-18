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
 * session_sr.c - Session management on the server
 */

#include "xserver_interface.h"
#include "session.h"

/*
 *  xsession_create_new () - create a new session
 *  return		: error code
 *  session_id (in/out) : session id
 * Note: this function allocates a new session id and creates a session for
 * it
 */
int
xsession_create_new (THREAD_ENTRY * thread_p, SESSION_KEY * key)
{
  int status = NO_ERROR;

  assert (key != NULL);

  return session_state_create (thread_p, key);
}

/*
 *  xsession_check_session  () - validates the session with session_id
 *  return	    : error code
 *  session_id (in) : session id
 * Note: this function checks if the session with session_id is still active
 * and updates the last access timeout for it
 */
int
xsession_check_session (THREAD_ENTRY * thread_p, const SESSION_KEY * key)
{
  return session_check_session (thread_p, key);
}

/*
 *  xsession_set_session_key () -
 *  return          : error code
 *  session_key (in) : session identifier
 */
int
xsession_set_session_key (THREAD_ENTRY * thread_p, const SESSION_KEY * key)
{
  return session_set_session_key (thread_p, key);
}

/*
 *  xsession_end_session () - end the session with session_id
 *  return	    : error code
 *  session_id (in) : session id
 *  thread_p (in)
 */
int
xsession_end_session (THREAD_ENTRY * thread_p, const SESSION_KEY * key)
{
  return session_state_destroy (thread_p, key);
}

/*
 *  xsession_set_row_count () - set the count of affected rows for the
 *				    session associated to thread_p
 *  return	  : error code
 *  thread_p (in) : worker thread
 *  row_count(in) : affected rows count
 */
int
xsession_set_row_count (THREAD_ENTRY * thread_p, int row_count)
{
  return session_set_row_count (thread_p, row_count);
}

/*
 *  xsession_get_row_count () - get the count of affected rows for the
 *				    session associated to thread_p
 *  return	  : error code
 *  thread_p (in) : worker thread
 *  row_count(out): affected rows count
 */
int
xsession_get_row_count (THREAD_ENTRY * thread_p, int *row_count)
{
  return session_get_row_count (thread_p, row_count);
}

/*
 *  xsession_set_cur_insert_id () - set the value of current insert id
 *
 *  return	  : error code
 *  thread_p (in) : worker thread
 *  value (in)	  : the value of last insert id
 *  force (in)    : update the value unconditionally
 *
 */
int
xsession_set_cur_insert_id (THREAD_ENTRY * thread_p, const DB_VALUE * value,
			    bool force)
{
  int err = NO_ERROR;

  assert (value != NULL);

  err = session_set_cur_insert_id (thread_p, value, force);

  return err;
}

/*
 *  xsession_get_last_insert_id () - retrieve the value of the last insert id
 *
 *  return	  : error code
 *  thread_p (in) : worker thread
 *  value (out)	  : the value of last insert id
 *  update_last_insert_id(in): whether update the last insert id
 */
int
xsession_get_last_insert_id (THREAD_ENTRY * thread_p, DB_VALUE * value,
			     bool update_last_insert_id)
{
  int err = NO_ERROR;

  assert (value != NULL);

  err = session_get_last_insert_id (thread_p, value, update_last_insert_id);
  if (err != NO_ERROR)
    {
      DB_MAKE_NULL (value);
    }
  return err;
}

/*
 *  xsession_reset_cur_insert_id () - reset current insert id as NULL
 *
 *  return	  : error code
 *  thread_p (in) : worker thread
 */
int
xsession_reset_cur_insert_id (THREAD_ENTRY * thread_p)
{
  int err = NO_ERROR;

  err = session_reset_cur_insert_id (thread_p);

  return err;
}

/*
 * xsession_create_prepared_statement () - create a prepared statement and add
 *					   it to the prepared statements list
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
 * will free the memory allocated for its arguments
 */
int
xsession_create_prepared_statement (THREAD_ENTRY * thread_p, OID user,
				    char *name, char *alias_print, char *info,
				    int info_len)
{
  return session_create_prepared_statement (thread_p, user, name, alias_print,
					    info, info_len);
}

/*
 * xsession_get_prepared_statement () - get the information about a prepared
 *					statement
 * return : NO_ERROR or error code
 * thread_p (in)	:
 * name (in)		: the name of the statement
 * name (in)		: the name of the prepared statement
 * info (out)		: serialized prepared statement information
 * info_len (out)	: serialized buffer length
 * xasl_id (out)	: XASL ID for this statement
 * xasl_header_p (out)	: XASL node header for this statement.
 */
int
xsession_get_prepared_statement (THREAD_ENTRY * thread_p, const char *name,
				 char **info, int *info_len,
				 XASL_ID * xasl_id,
				 XASL_NODE_HEADER * xasl_header_p)
{
  XASL_CACHE_ENTRY *xasl_entry = NULL;

  int error = session_get_prepared_statement (thread_p, name, info, info_len,
					      &xasl_entry);

  assert (xasl_id != NULL);
  XASL_ID_SET_NULL (xasl_id);

  if (error == NO_ERROR && xasl_entry != NULL)
    {
      XASL_ID_COPY (xasl_id, &xasl_entry->xasl_id);

      if (xasl_header_p != NULL)
	{
	  /* get XASL node header from XASL stream */
	  qfile_load_xasl_node_header (thread_p, xasl_id, xasl_header_p);
	}

      (void) qexec_remove_my_tran_id_in_xasl_entry (thread_p, xasl_entry,
						    true);
    }

  return error;
}

/*
 * xsession_delete_prepared_statement () - delete a prepared statement
 * return : error code or NO_ERROR
 * thread_p (in)  :
 * name (in)	  : name of the prepared statement
 */
int
xsession_delete_prepared_statement (THREAD_ENTRY * thread_p, const char *name)
{
  return session_delete_prepared_statement (thread_p, name);
}

/*
 * xlogin_user () - login user
 * return : error code or NO_ERROR
 * thread_p (in)  :
 * username (in)  : name of the prepared statement
 */
int
xlogin_user (THREAD_ENTRY * thread_p, const char *username)
{
  return login_user (thread_p, username);
}

/*
 * xsession_set_session_variables () - set session variables
 * return : error code
 * thread_p (in) : worker thread
 * values (in)	 : array of variables to set
 * count (in)	 : number of elements in array
 */
int
xsession_set_session_variables (THREAD_ENTRY * thread_p, DB_VALUE * values,
				const int count)
{
  return session_set_session_variables (thread_p, values, count);
}

/*
 * xsession_get_session_variable () - get the value of a session variable
 * return : int
 * thread_p (in) : worker thread
 * name (in)	 : name of the variable
 * value (out)	 : variable value
 */
int
xsession_get_session_variable (THREAD_ENTRY * thread_p, const DB_VALUE * name,
			       DB_VALUE * value)
{
  return session_get_variable (thread_p, name, value);
}

/*
 * xsession_get_session_variable_no_copy () - get the value of a session
 *					      variable
 * return : int
 * thread_p (in) : worker thread
 * name (in)	 : name of the variable
 * value (in/out): variable value
 * Note: This function gets a reference to a session variable from the session
 * state object. Because it gets the actual pointer, it is not thread safe
 * and it should only be called in the stand alone mode
 */
int
xsession_get_session_variable_no_copy (THREAD_ENTRY * thread_p,
				       const DB_VALUE * name,
				       DB_VALUE ** value)
{
#if defined (SERVER_MODE)
  /* do not call this function in a multi-threaded context */
  assert (false);
  return ER_FAILED;
#endif
  return session_get_variable_no_copy (thread_p, name, value);
}

/*
 * xsession_drop_session_variables () - drop session variables
 * return : error code or NO_ERROR
 * thread_p (in) : worker thread
 * values (in)   : names of the variables to drop
 * count (in)	 : number of elements in the values array
 */
int
xsession_drop_session_variables (THREAD_ENTRY * thread_p, DB_VALUE * values,
				 const int count)
{
  return session_drop_session_variables (thread_p, values, count);
}

/*
 * xsession_store_query_entry_info () - create a query entry
 * return : void
 * thread_p (in) :
 * qentry_p (in) : query entry
 */
void
xsession_store_query_entry_info (THREAD_ENTRY * thread_p,
				 QMGR_QUERY_ENTRY * qentry_p)
{
  session_store_query_entry_info (thread_p, qentry_p);
}

/*
 * xsession_load_query_entry_info () - search for a query entry
 * return : error code or NO_ERROR
 * thread_p (in) :
 * qentry_p (in/out) : query entry
 */
int
xsession_load_query_entry_info (THREAD_ENTRY * thread_p,
				QMGR_QUERY_ENTRY * qentry_p)
{
  return session_load_query_entry_info (thread_p, qentry_p);
}

/*
 * xsession_remove_query_entry_info () - remove a query entry from the
 *					 holdable queries list
 * return : error code or NO_ERROR
 * thread_p (in) : active thread
 * query_id (in) : query id
 */
int
xsession_remove_query_entry_info (THREAD_ENTRY * thread_p,
				  const QUERY_ID query_id)
{
  return session_remove_query_entry_info (thread_p, query_id);
}

/*
 * xsession_remove_query_entry_info () - remove a query entry from the
 *					 holdable queries list but do not
 *					 close the associated list files
 * return : error code or NO_ERROR
 * thread_p (in) : active thread
 * query_id (in) : query id
 */
int
xsession_clear_query_entry_info (THREAD_ENTRY * thread_p,
				 const QUERY_ID query_id)
{
  return session_clear_query_entry_info (thread_p, query_id);
}
