/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * method_scan.c - Routines to implement scanning an array of values
 *                 received by the comm interface
 */

#ident "$Id$"

#include "config.h"

#include <string.h>

#include "xasl_support.h"
#include "network.h"
#include "network_interface_sr.h"
#ifndef	SERVER_MODE
#include "object_accessor.h"
#endif
#include "authenticate.h"
#include "jsp_sr.h"
#include "scan_manager.h"
#include "method_scan.h"
#include "xserver_interface.h"

/* this must be the last header file included!!! */
#include "dbval.h"
#include "db.h"

#define ENTER_SERVER_IN_METHOD_CALL(save_heap_id_) \
  do { \
    db_on_server = 1; \
    private_heap_id = save_heap_id_; \
  } while (0)

#define EXIT_SERVER_IN_METHOD_CALL(save_heap_id_) \
  do { \
     save_heap_id_ = private_heap_id; \
     private_heap_id = 0; \
     db_on_server = 0; \
  } while (0)

static int method_open_value_array_scan (METHOD_SCAN_BUFFER * scan_buf);
static int method_close_value_array_scan (METHOD_SCAN_BUFFER * scan_buf);
static SCAN_CODE method_scan_next_value_array (METHOD_SCAN_BUFFER * scan_buf,
					       VAL_LIST * val_list);
static int method_invoke (THREAD_ENTRY * thread_p,
			  METHOD_SCAN_BUFFER * scan_buf);
static SCAN_CODE method_receive_results (THREAD_ENTRY * thread_p,
					 METHOD_SCAN_BUFFER * scan_buf);

#ifndef SERVER_MODE
static void method_clear_scan_buffer (METHOD_SCAN_BUFFER * scan_buf);
#else
static VACOMM_BUFFER *method_initialize_vacomm_buffer (void);
static void method_free_vacomm_buffer (VACOMM_BUFFER * vacomm_buffer);
static SCAN_CODE method_receive_value (THREAD_ENTRY * thread_p,
				       DB_VALUE * dbval,
				       VACOMM_BUFFER * vacomm_buffer);
#endif /* SERVER_MODE */

/*
 * xs_open_va_scan () -
 *   return: int
 *   scan_buf(in)       : Value array buffer
 *   scanbuf_type(in)   : Method signature list
 */
static int
method_open_value_array_scan (METHOD_SCAN_BUFFER * scan_buffer_p)
{
  int num_methods;

  num_methods = scan_buffer_p->s.method_ctl.method_sig_list->no_methods;
  if (num_methods <= 0)
    {
      num_methods = MAX_XS_SCANBUF_DBVALS;	/* for safe-guard */
    }

  scan_buffer_p->dbval_list =
    (QPROC_DB_VALUE_LIST) malloc (sizeof (scan_buffer_p->dbval_list[0]) *
				  num_methods);

  if (scan_buffer_p->dbval_list == NULL)
    {
      return ER_FAILED;
    }

#ifdef SERVER_MODE
  scan_buffer_p->vacomm_buffer = method_initialize_vacomm_buffer ();
  if (scan_buffer_p->vacomm_buffer == NULL)
    {
      return ER_FAILED;
    }
#endif /* SERVER_MODE */

  return NO_ERROR;
}

/*
 * method_close_value_array_scan () -
 *   return: int
 *   scan_buf(in)       : Value array buffer
 */
static int
method_close_value_array_scan (METHOD_SCAN_BUFFER * scan_buffer_p)
{
#ifdef SERVER_MODE
  method_free_vacomm_buffer (scan_buffer_p->vacomm_buffer);
  scan_buffer_p->vacomm_buffer = NULL;
#endif /* SERVER_MODE */

  free_and_init (scan_buffer_p->dbval_list);
  return NO_ERROR;
}

/*
 * method_scan_next_value_array () -
 *   return: int
 *   scan_buf(in)       : Value array buffer
 *   val_list(in)       :
 */
static SCAN_CODE
method_scan_next_value_array (METHOD_SCAN_BUFFER * scan_buffer_p,
			      VAL_LIST * value_list_p)
{
  SCAN_CODE scan_result = S_SUCCESS;
  QPROC_DB_VALUE_LIST dbval_list;
  int n;

  dbval_list = scan_buffer_p->dbval_list;
  for (n = 0; n < value_list_p->val_cnt; n++)
    {
      dbval_list->next = dbval_list + 1;
      dbval_list++;
    }

  scan_buffer_p->dbval_list[value_list_p->val_cnt - 1].next = NULL;
  value_list_p->valp = scan_buffer_p->dbval_list;

  return scan_result;
}

/*
 * method_open_scan () -
 *   return: int
 *   scan_buf(in)       : Value array buffer
 *   list_id(in)        :
 *   method_sig_list(in): Method signature list
 */
int
method_open_scan (THREAD_ENTRY * thread_p, METHOD_SCAN_BUFFER * scan_buffer_p,
		  QFILE_LIST_ID * list_id_p,
		  METHOD_SIG_LIST * method_sig_list_p)
{
  int error;
  METHOD_INFO *method_ctl_p;

  method_ctl_p = &scan_buffer_p->s.method_ctl;

  method_ctl_p->list_id = list_id_p;
  method_ctl_p->method_sig_list = method_sig_list_p;

  error = method_open_value_array_scan (scan_buffer_p);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = method_invoke (thread_p, scan_buffer_p);
  if (error != NO_ERROR)
    {
      (void) method_close_value_array_scan (scan_buffer_p);
      return error;
    }

  return NO_ERROR;
}

/*
 * method_close_scan () -
 *   return: int
 *   scan_buf(in)       : Value array buffer
 */
int
method_close_scan (THREAD_ENTRY * thread_p,
		   METHOD_SCAN_BUFFER * scan_buffer_p)
{
#ifdef SERVER_MODE
  VACOMM_BUFFER *vacomm_buffer_p;

  /*
     If the method scan is being closed before the client is done,
     the status could be zero (1st buffer not received) or
     METHOD_SUCCESS (last buffer not received).
   */

  vacomm_buffer_p = scan_buffer_p->vacomm_buffer;

  if ((vacomm_buffer_p)
      && ((vacomm_buffer_p->status == 0)
	  || (vacomm_buffer_p->status == METHOD_SUCCESS)))
    {
      vacomm_buffer_p->action = VACOMM_BUFFER_ABORT;

      do
	{
	  (void) method_receive_results (thread_p, scan_buffer_p);
	}
      while (vacomm_buffer_p->status == METHOD_SUCCESS);
    }
#else /* SERVER_MODE */
  method_clear_scan_buffer (scan_buffer_p);
#endif /* SERVER_MODE */

  return method_close_value_array_scan (scan_buffer_p);
}

/*
 * method_scan_next () -
 *   return: int
 *   scan_buf(in)       : Value array buffer
 *   val_list(in)       :
 */
SCAN_CODE
method_scan_next (THREAD_ENTRY * thread_p, METHOD_SCAN_BUFFER * scan_buffer_p,
		  VAL_LIST * value_list_p)
{
  SCAN_CODE scan_result;

  scan_result = method_receive_results (thread_p, scan_buffer_p);

  if (scan_result == S_SUCCESS)
    {
      value_list_p->val_cnt =
	scan_buffer_p->s.method_ctl.method_sig_list->no_methods;
      scan_result =
	method_scan_next_value_array (scan_buffer_p, value_list_p);
    }

  return scan_result;
}

#ifdef SERVER_MODE
static int
method_invoke_from_server (THREAD_ENTRY * thread_p,
			   METHOD_SCAN_BUFFER * scan_buffer_p)
{
  METHOD_INFO *method_ctl_p;

  method_ctl_p = &scan_buffer_p->s.method_ctl;
  return xs_send_method_call_info_to_client (thread_p, method_ctl_p->list_id,
					     method_ctl_p->method_sig_list);
}
#else
static int
method_invoke_from_stand_alone (METHOD_SCAN_BUFFER * scan_buffer_p)
{
  int meth_no;
  int val_cnt;
  int i;
  METHOD_INFO *method_ctl_p;
  METHOD_SIG_LIST *method_sig_list;
  METHOD_SIG *meth_sig;

  method_ctl_p = &scan_buffer_p->s.method_ctl;
  method_sig_list = method_ctl_p->method_sig_list;
  scan_buffer_p->crs_id.buffer = NULL;

  val_cnt = 0;
  meth_sig = method_sig_list->method_sig;

  for (meth_no = 0; meth_no < method_sig_list->no_methods; meth_no++)
    {
      val_cnt += meth_sig->no_method_args + 1;
      meth_sig = meth_sig->next;
    }

  if (method_ctl_p->list_id->type_list.type_cnt > val_cnt)
    {
      val_cnt = method_ctl_p->list_id->type_list.type_cnt;
    }

  scan_buffer_p->val_cnt = val_cnt;
  scan_buffer_p->vallist = (DB_VALUE *) malloc (sizeof (DB_VALUE) * val_cnt);

  if ((val_cnt > 0) && (scan_buffer_p->vallist == NULL))
    {
      return ER_FAILED;
    }
  /*
   * Make sure that these containers get initialized with meaningful
   * bits.  It's possible to wind up in method_clear_scan_buffer() without ever
   * having actually received any method results, and if that happens
   * we'll be trying to db_value_clear() junk unless we initialize things
   * here.
   */
  for (i = 0; i < val_cnt; i++)
    {
      DB_MAKE_NULL (&scan_buffer_p->vallist[i]);
    }

  scan_buffer_p->valptrs =
    (DB_VALUE **) malloc (sizeof (DB_VALUE *) * (val_cnt + 1));

  if (scan_buffer_p->valptrs == NULL)
    {
      method_clear_scan_buffer (scan_buffer_p);
      return ER_FAILED;
    }

  scan_buffer_p->oid_cols =
    (int *) malloc (sizeof (int) * method_sig_list->no_methods);

  if (scan_buffer_p->oid_cols == NULL)
    {
      method_clear_scan_buffer (scan_buffer_p);
      return ER_FAILED;
    }

  meth_sig = method_sig_list->method_sig;
  for (meth_no = 0; meth_no < method_sig_list->no_methods; meth_no++)
    {
      scan_buffer_p->oid_cols[meth_no] = meth_sig->method_arg_pos[0];
      meth_sig = meth_sig->next;
    }

  if (!cursor_open (&scan_buffer_p->crs_id, method_ctl_p->list_id,
		    false, false))
    {
      method_clear_scan_buffer (scan_buffer_p);
      return ER_FAILED;
    }

  /* tfile_vfid pointer as query id for method scan */
  scan_buffer_p->crs_id.query_id = (int) method_ctl_p->list_id->tfile_vfid;

  cursor_set_oid_columns (&scan_buffer_p->crs_id,
			  scan_buffer_p->oid_cols,
			  method_sig_list->no_methods);

  return NO_ERROR;
}
#endif

/*
 * method_invoke () -
 *   return: int
 *   scan_buf(in)       : Value array buffer
 */
static int
method_invoke (THREAD_ENTRY * thread_p, METHOD_SCAN_BUFFER * scan_buffer_p)
{
#if defined(SERVER_MODE)
  return method_invoke_from_server (thread_p, scan_buffer_p);
#else /* SERVER_MODE */
  return method_invoke_from_stand_alone (scan_buffer_p);
#endif /* SERVER_MODE */
}

#ifdef SERVER_MODE
static SCAN_CODE
method_receive_results_for_server (THREAD_ENTRY * thread_p,
				   METHOD_SCAN_BUFFER * scan_buffer_p)
{
  QPROC_DB_VALUE_LIST dbval_list;
  int meth_no, i;
  METHOD_SIG *meth_sig_p;
  SCAN_CODE result;
  DB_VALUE *dbval_p;
  METHOD_SIG_LIST *method_sig_list_p;

  method_sig_list_p = scan_buffer_p->s.method_ctl.method_sig_list;
  meth_sig_p = method_sig_list_p->method_sig;
  dbval_list = scan_buffer_p->dbval_list;

  for (meth_no = 0; meth_no < method_sig_list_p->no_methods; ++meth_no)
    {
      dbval_p = (DB_VALUE *) malloc (sizeof (DB_VALUE));
      dbval_list->val = dbval_p;

      if (dbval_p == NULL)
	{
	  result = S_ERROR;
	}
      else
	{
	  DB_MAKE_NULL (dbval_p);
	  result = method_receive_value (thread_p, dbval_p,
					 scan_buffer_p->vacomm_buffer);
	}

      if (result != S_SUCCESS)
	{
	  for (i = 0; i <= meth_no; i++)
	    {
	      if (scan_buffer_p->dbval_list[i].val)
		{
		  db_value_clear (scan_buffer_p->dbval_list[i].val);
		  free_and_init (scan_buffer_p->dbval_list[i].val);
		}
	    }

	  if (result == S_ERROR)
	    {
	      scan_buffer_p->vacomm_buffer->status = METHOD_ERROR;
	    }

	  return result;
	}

      dbval_list++;
      meth_sig_p = meth_sig_p->next;
    }

  return S_SUCCESS;
}

#else

static SCAN_CODE
method_receive_results_for_stand_alone (METHOD_SCAN_BUFFER * scan_buffer_p)
{
  QPROC_DB_VALUE_LIST dbval_list;
  int meth_no;
  METHOD_SIG *meth_sig;
  int crs_result;
  int no_args;
  int pos;
  int arg;
  int turn_on_auth = 1;
  DB_VALUE val;
  int error;
  unsigned int save_heap_id;
  METHOD_SIG_LIST *method_sig_list;

  method_sig_list = scan_buffer_p->s.method_ctl.method_sig_list;

  EXIT_SERVER_IN_METHOD_CALL (save_heap_id);

  crs_result = cursor_next_tuple (&scan_buffer_p->crs_id);
  if (crs_result == DB_CURSOR_SUCCESS)
    {
      DB_VALUE *ptr;
      int i;

      /* Since we may be calling this in a loop, we need to clear out the
       * vallist to avoid leaking old pointers.
       */
      for (i = 0, ptr = scan_buffer_p->vallist;
	   ptr && i < scan_buffer_p->val_cnt; i++, ptr++)
	{
	  pr_clear_value (ptr);
	}

      if (cursor_get_tuple_value_list (&scan_buffer_p->crs_id,
				       scan_buffer_p->s.method_ctl.list_id->
				       type_list.type_cnt,
				       scan_buffer_p->vallist) != NO_ERROR)
	{
	  method_clear_scan_buffer (scan_buffer_p);
	  ENTER_SERVER_IN_METHOD_CALL (save_heap_id);
	  return S_ERROR;
	}

      meth_sig = method_sig_list->method_sig;
      dbval_list = scan_buffer_p->dbval_list;

      for (meth_no = 0; meth_no < method_sig_list->no_methods; meth_no++)
	{
	  /* The first position # is for the object ID */
	  no_args = meth_sig->no_method_args + 1;
	  for (arg = 0; arg < no_args; ++arg)
	    {
	      pos = meth_sig->method_arg_pos[arg];
	      scan_buffer_p->valptrs[arg] = &scan_buffer_p->vallist[pos];
	    }

	  scan_buffer_p->valptrs[no_args] = NULL;
	  DB_MAKE_NULL (&val);

	  if (meth_sig->class_name != NULL)
	    {
	      /* Don't call the method if the object is NULL or it has been
	       * deleted.  A method call on a NULL object is NULL.
	       */
	      if ((DB_IS_NULL (scan_buffer_p->valptrs[0])) ||
		  !(db_is_any_class
		    (DB_GET_OBJECT (scan_buffer_p->valptrs[0]))
		    ||
		    db_is_instance (DB_GET_OBJECT
				    (scan_buffer_p->valptrs[0]))))
		{
		  error = NO_ERROR;
		}
	      else
		{
		  /* methods must run with authorization turned on and database
		   * modifications turned off.
		   */
		  turn_on_auth = 0;
		  AU_ENABLE (turn_on_auth);
		  db_Disable_modifications++;
		  error =
		    obj_send_array (DB_GET_OBJECT (scan_buffer_p->valptrs[0]),
				    meth_sig->method_name, &val,
				    &scan_buffer_p->valptrs[1]);
		  db_Disable_modifications--;
		  AU_DISABLE (turn_on_auth);
		}
	    }
	  else
	    {
	      /* java stored procedure call */
	      turn_on_auth = 0;
	      AU_ENABLE (turn_on_auth);
	      db_Disable_modifications++;
	      error = jsp_call_from_server (&val, scan_buffer_p->valptrs,
					    meth_sig->method_name,
					    meth_sig->no_method_args);
	      db_Disable_modifications--;
	      AU_DISABLE (turn_on_auth);
	    }

	  ENTER_SERVER_IN_METHOD_CALL (save_heap_id);

	  if (error != NO_ERROR)
	    {
	      method_clear_scan_buffer (scan_buffer_p);
	      return S_ERROR;
	    }

	  if (DB_VALUE_TYPE (&val) == DB_TYPE_ERROR)
	    {
	      if (er_errid () == NO_ERROR)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
			  1);
		}
	      method_clear_scan_buffer (scan_buffer_p);

	      return S_ERROR;
	    }

	  dbval_list->val = regu_dbval_db_alloc ();

	  if (dbval_list->val == NULL)
	    {
	      pr_clear_value (&val);
	      return S_ERROR;
	    }

	  /* Don't forget to translate any OBJECTS to OIDs. */
	  if (DB_VALUE_DOMAIN_TYPE (&val) == DB_TYPE_OBJECT)
	    {
	      DB_MAKE_OID (dbval_list->val, ws_oid (DB_GET_OBJECT (&val)));
	    }
	  else if (db_value_clone (&val, dbval_list->val) != NO_ERROR)
	    {
	      pr_clear_value (&val);
	      return S_ERROR;
	    }
	  pr_clear_value (&val);

	  dbval_list++;
	  meth_sig = meth_sig->next;
	}

      return S_SUCCESS;
    }
  else if (crs_result == DB_CURSOR_END)
    {
      ENTER_SERVER_IN_METHOD_CALL (save_heap_id);
      method_clear_scan_buffer (scan_buffer_p);
      return S_END;
    }
  else
    {
      ENTER_SERVER_IN_METHOD_CALL (save_heap_id);
      method_clear_scan_buffer (scan_buffer_p);
      return crs_result;
    }
}
#endif

/*
 * method_receive_results () -
 *   return: int
 *   scan_buf(in)       : Value array buffer
 */
static SCAN_CODE
method_receive_results (THREAD_ENTRY * thread_p,
			METHOD_SCAN_BUFFER * scan_buffer_p)
{
#if defined(SERVER_MODE)
  return method_receive_results_for_server (thread_p, scan_buffer_p);
#else /* SERVER_MODE */
  return method_receive_results_for_stand_alone (scan_buffer_p);
#endif /* SERVER_MODE */
}

#if !defined(SERVER_MODE)
/*
 * method_clear_scan_buffer () -
 *   return:
 *   scan_buffer_p(in)       : Value array buffer
 */
static void
method_clear_scan_buffer (METHOD_SCAN_BUFFER * scan_buffer_p)
{
  int i;
  DB_VALUE *ptr;

  if (scan_buffer_p->crs_id.buffer)
    {
      cursor_close (&scan_buffer_p->crs_id);
      scan_buffer_p->crs_id.buffer = NULL;
    }

  ptr = scan_buffer_p->vallist;
  for (i = 0; ptr && i < scan_buffer_p->val_cnt; i++)
    {
      db_value_clear (ptr);
      ptr++;
    }

  free_and_init (scan_buffer_p->valptrs);
  free_and_init (scan_buffer_p->vallist);
  free_and_init (scan_buffer_p->oid_cols);

  return;
}
#else /* !SERVER_MODE */
/*
 * method_free_vacomm_buffer () - Frees the comm buffer
 *   return:
 *   vacomm_buffer(in)  : Transmission buffer
 */
static void
method_free_vacomm_buffer (VACOMM_BUFFER * vacomm_buffer_p)
{
  if (vacomm_buffer_p)
    {
      free_and_init (vacomm_buffer_p->area);
      free_and_init (vacomm_buffer_p);
    }
}

/*
 * method_initialize_vacomm_buffer () - Initializes the method comm buffer
 *   return:
 */
static VACOMM_BUFFER *
method_initialize_vacomm_buffer (void)
{
  VACOMM_BUFFER *vacomm_buffer;

  vacomm_buffer = (VACOMM_BUFFER *) malloc (sizeof (VACOMM_BUFFER));
  if (vacomm_buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (VACOMM_BUFFER));
      return NULL;
    }

  vacomm_buffer->length = 0;
  vacomm_buffer->status = 0;
  vacomm_buffer->action = VACOMM_BUFFER_SEND;
  vacomm_buffer->error = 0;
  vacomm_buffer->no_vals = 0;
  vacomm_buffer->area = NULL;
  vacomm_buffer->buffer = NULL;
  vacomm_buffer->cur_pos = 0;
  vacomm_buffer->size = 0;

  return vacomm_buffer;
}

/*
 * method_receive_value () -
 *   return: int (number of values received)
 *   dbval(in)  : value
 *   vacomm_buffer(in)  : Transmission buffer
 *
 * Note: If there is a db_value in the transmission buffer,
 * unpack it & return 1.  Otherwise, if EOF is indicated return 0,
 * else receive the buffer from the client and then unpack
 * the value and return 1.  If an error is indicated, return -1
 * because the error has been set by the comm interface.
 */
static SCAN_CODE
method_receive_value (THREAD_ENTRY * thread_p, DB_VALUE * dbval_p,
		      VACOMM_BUFFER * vacomm_buffer_p)
{
  int error;
  char *p;

  if (vacomm_buffer_p->cur_pos == 0)
    {
      if (vacomm_buffer_p->status == METHOD_EOF)
	{
	  return S_END;
	}

      vacomm_buffer_p->status = METHOD_ERROR;

      error = xs_receive_data_from_client (thread_p, &vacomm_buffer_p->area,
					   &vacomm_buffer_p->size);
      if (error == NO_ERROR)
	{
	  vacomm_buffer_p->buffer =
	    vacomm_buffer_p->area + VACOMM_BUFFER_HEADER_SIZE;
	  p =
	    or_unpack_int (vacomm_buffer_p->area +
			   VACOMM_BUFFER_HEADER_LENGTH_OFFSET,
			   &vacomm_buffer_p->length);
	  p =
	    or_unpack_int (vacomm_buffer_p->area +
			   VACOMM_BUFFER_HEADER_STATUS_OFFSET,
			   &vacomm_buffer_p->status);

	  if (vacomm_buffer_p->status == METHOD_ERROR)
	    {
	      p = or_unpack_int (vacomm_buffer_p->area +
				 VACOMM_BUFFER_HEADER_ERROR_OFFSET,
				 &vacomm_buffer_p->error);
	    }
	  else
	    {
	      p = or_unpack_int (vacomm_buffer_p->area +
				 VACOMM_BUFFER_HEADER_NO_VALS_OFFSET,
				 &vacomm_buffer_p->no_vals);
	    }

	  if (vacomm_buffer_p->status == METHOD_SUCCESS)
	    {
	      error =
		xs_send_action_to_client (thread_p,
					  (VACOMM_BUFFER_CLIENT_ACTION)
					  vacomm_buffer_p->action);
	      if (error != NO_ERROR)
		{
		  return S_ERROR;
		}
	    }
	}
      else
	{
	  xs_send_action_to_client (thread_p, (VACOMM_BUFFER_CLIENT_ACTION)
				    vacomm_buffer_p->action);
	  return S_ERROR;
	}
    }

  if (vacomm_buffer_p->status == METHOD_ERROR)
    {
      return S_ERROR;
    }

  if (vacomm_buffer_p->no_vals > 0)
    {
      p = or_unpack_db_value (vacomm_buffer_p->buffer +
			      vacomm_buffer_p->cur_pos, dbval_p);
      vacomm_buffer_p->cur_pos = p - vacomm_buffer_p->buffer;
      vacomm_buffer_p->no_vals--;
    }
  else
    {
      return S_END;
    }

  if (vacomm_buffer_p->no_vals == 0)
    {
      vacomm_buffer_p->cur_pos = 0;
    }

  return S_SUCCESS;
}
#endif /* !SERVER_MODE */
