/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * qp_meth.c - Method calls in queries
 *
 * Note: if you feel the need
 */

#ident "$Id$"

#include "config.h"

#include "db.h"
#include "qp_mem.h"
#include "query_method.h"
#include "object_accessor.h"
#include "jsp_earth.h"

#include "object_representation.h"
#include "network.h"
#include "network_interface_sky.h"
#include "scan_manager.h"

/* this must be the last header file included!!! */
#include "dbval.h"

static int method_initialize_vacomm_buffer (VACOMM_BUFFER * vacomm_buffer,
					    unsigned int rc,
					    char *host, char *server_name);
static void method_clear_vacomm_buffer (VACOMM_BUFFER * vacomm_buffer);
static int method_send_value_to_server (DB_VALUE * dbval,
					VACOMM_BUFFER * vacomm_buffer);
static int method_send_eof_to_server (VACOMM_BUFFER * vacomm_buffer);

/*
 * method_clear_vacomm_buffer () - Clears the comm buffer
 *   return: 
 *   vacomm_buffer(in)  : Transmission buffer
 */
static void
method_clear_vacomm_buffer (VACOMM_BUFFER * vacomm_buffer_p)
{
  if (vacomm_buffer_p)
    {
      free_and_init (vacomm_buffer_p->area);
      free_and_init (vacomm_buffer_p->host);
      free_and_init (vacomm_buffer_p->server_name);
    }
}

/*
 * method_initialize_vacomm_buffer () - Initializes the comm buffer
 *   return: 
 *   vacomm_buffer(in)  : 
 *   rc(in)     : client transmission request ID
 *   host(in)   :
 *   server_name(in)    :
 */
static int
method_initialize_vacomm_buffer (VACOMM_BUFFER * vacomm_buffer_p,
				 unsigned int rc, char *host_p,
				 char *server_name_p)
{
  vacomm_buffer_p->rc = rc;
  vacomm_buffer_p->server_name = NULL;
  vacomm_buffer_p->area = NULL;

  vacomm_buffer_p->host = strdup (host_p);
  if (vacomm_buffer_p->host == NULL)
    {
      method_clear_vacomm_buffer (vacomm_buffer_p);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, strlen (host_p) + 1);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  vacomm_buffer_p->server_name = strdup (server_name_p);
  if (vacomm_buffer_p->server_name == NULL)
    {
      method_clear_vacomm_buffer (vacomm_buffer_p);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, strlen (server_name_p) + 1);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  vacomm_buffer_p->area = (char *) malloc (VACOMM_BUFFER_SIZE);
  if (vacomm_buffer_p->area == NULL)
    {
      method_clear_vacomm_buffer (vacomm_buffer_p);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, VACOMM_BUFFER_SIZE);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  vacomm_buffer_p->buffer = vacomm_buffer_p->area + VACOMM_BUFFER_HEADER_SIZE;
  vacomm_buffer_p->no_vals = 0;
  vacomm_buffer_p->cur_pos = 0;
  vacomm_buffer_p->size = VACOMM_BUFFER_SIZE - VACOMM_BUFFER_HEADER_SIZE;
  vacomm_buffer_p->action = VACOMM_BUFFER_SEND;

  return NO_ERROR;
}

/*
 * method_send_value_to_server () -
 *   return: 
 *   dbval(in)  : value
 *   vacomm_buffer(in)  : Transmission buffer
 *                                                                             
 * Note: If the db_value will fit into the transmission buffer,         
 * pack it into the buffer.  Otherwise, if the buffer is empty,   
 * expand it, else send the buffer to the server and then pack    
 * the value into the buffer.                                     
 */
static int
method_send_value_to_server (DB_VALUE * dbval_p,
			     VACOMM_BUFFER * vacomm_buffer_p)
{
  int dbval_length;
  char *new_area_p, *p;
  int error = 0;
  int length;
  int action;

  dbval_length = or_db_value_size (dbval_p);
  while ((vacomm_buffer_p->cur_pos + dbval_length) > vacomm_buffer_p->size)
    {
      if (vacomm_buffer_p->cur_pos == 0)
	{
	  new_area_p = (char *) realloc (vacomm_buffer_p->area,
					 dbval_length +
					 VACOMM_BUFFER_HEADER_SIZE);
	  if (new_area_p == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      dbval_length + VACOMM_BUFFER_HEADER_SIZE);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  vacomm_buffer_p->area = new_area_p;
	  vacomm_buffer_p->buffer = (vacomm_buffer_p->area +
				     VACOMM_BUFFER_HEADER_SIZE);
	  vacomm_buffer_p->size = dbval_length;
	}
      else
	{
	  if (vacomm_buffer_p->action == VACOMM_BUFFER_SEND)
	    {
	      length = vacomm_buffer_p->cur_pos + VACOMM_BUFFER_HEADER_SIZE;
	      p = or_pack_int (vacomm_buffer_p->area +
			       VACOMM_BUFFER_HEADER_LENGTH_OFFSET, length);
	      p = or_pack_int (vacomm_buffer_p->area +
			       VACOMM_BUFFER_HEADER_STATUS_OFFSET,
			       (int) METHOD_SUCCESS);
	      p = or_pack_int (vacomm_buffer_p->area +
			       VACOMM_BUFFER_HEADER_NO_VALS_OFFSET,
			       vacomm_buffer_p->no_vals);
	      error = net_client_send_data (vacomm_buffer_p->host,
					    vacomm_buffer_p->rc,
					    vacomm_buffer_p->area,
					    vacomm_buffer_p->cur_pos +
					    VACOMM_BUFFER_HEADER_SIZE);
	      if (error != NO_ERROR)
		{
		  return ER_FAILED;
		}

	      error = net_client_receive_action (vacomm_buffer_p->rc,
						 &action);
	      if (error)
		{
		  return ER_FAILED;
		}

	      vacomm_buffer_p->action = action;
	      if (vacomm_buffer_p->action != VACOMM_BUFFER_SEND)
		{
		  return ER_FAILED;
		}
	    }
	  vacomm_buffer_p->cur_pos = 0;
	  vacomm_buffer_p->no_vals = 0;
	}
    }

  ++vacomm_buffer_p->no_vals;
  p = or_pack_db_value (vacomm_buffer_p->buffer + vacomm_buffer_p->cur_pos,
			dbval_p);
  if (vacomm_buffer_p->buffer + vacomm_buffer_p->cur_pos + dbval_length != p)
    {
      return ER_FAILED;
    }

  vacomm_buffer_p->cur_pos += dbval_length;
  return NO_ERROR;
}

/*
 * method_send_eof_to_server () -
 *   return: 
 *   vacomm_buffer(in)  : Transmission buffer
 *                                                                             
 * Note: Send the transmission buffer to the server and indicate EOF.   
 */
static int
method_send_eof_to_server (VACOMM_BUFFER * vacomm_buffer_p)
{
  int length, error;
  char *p;

  length = vacomm_buffer_p->cur_pos + VACOMM_BUFFER_HEADER_SIZE;
  p = or_pack_int (vacomm_buffer_p->area +
		   VACOMM_BUFFER_HEADER_LENGTH_OFFSET, length);
  p = or_pack_int (vacomm_buffer_p->area + VACOMM_BUFFER_HEADER_STATUS_OFFSET,
		   (int) METHOD_EOF);
  p =
    or_pack_int (vacomm_buffer_p->area + VACOMM_BUFFER_HEADER_NO_VALS_OFFSET,
		 vacomm_buffer_p->no_vals);

  error = net_client_send_data (vacomm_buffer_p->host, vacomm_buffer_p->rc,
				vacomm_buffer_p->area,
				vacomm_buffer_p->cur_pos +
				VACOMM_BUFFER_HEADER_SIZE);

  if (error != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * method_send_error_to_server () - Send an error indication to the server
 *   return: 
 *   rc(in)     : enquiry return code
 *   host(in)   : host name
 *   server_name(in)    : server name
 */
int
method_send_error_to_server (unsigned int rc, char *host_p,
			     char *server_name_p)
{
  char *p;
  char area[VACOMM_BUFFER_HEADER_SIZE];
  int error;

  p = or_pack_int (area + VACOMM_BUFFER_HEADER_LENGTH_OFFSET,
		   VACOMM_BUFFER_HEADER_SIZE);
  p = or_pack_int (area + VACOMM_BUFFER_HEADER_STATUS_OFFSET,
		   (int) METHOD_ERROR);
  p = or_pack_int (area + VACOMM_BUFFER_HEADER_ERROR_OFFSET, er_errid ());

  error = net_client_send_data (host_p, rc, area, VACOMM_BUFFER_HEADER_SIZE);

  if (error != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * method_invoke_for_server () -
 *   return: int
 *   rc(in)     :
 *   host(in)   :
 *   server_name(in)    :
 *   list_id(in)        : List ID for objects & arguments
 *   method_sig_list(in): Method signatures
 */
int
method_invoke_for_server (unsigned int rc,
			  char *host_p,
			  char *server_name_p,
			  QFILE_LIST_ID * list_id_p,
			  METHOD_SIG_LIST * method_sig_list_p)
{
  DB_VALUE *val_list_p = NULL;
  DB_VALUE **values_p;
  int *oid_cols;
  CURSOR_ID cursor_id;
  int turn_on_auth = 1;
  int cursor_result;
  int method_no;
  int no_args;
  int pos;
  int arg;
  int value_count;
  DB_VALUE value;
  METHOD_SIG *meth_sig_p;
  int error;
  VACOMM_BUFFER vacomm_buffer;
  int count;
  DB_VALUE *value_p;

  DB_MAKE_NULL (&value);

  if (method_initialize_vacomm_buffer (&vacomm_buffer, rc, host_p,
				       server_name_p) != NO_ERROR)
    {
      return ER_FAILED;
    }

  meth_sig_p = method_sig_list_p->method_sig;
  value_count = 0;

  for (method_no = 0; method_no < method_sig_list_p->no_methods; method_no++)
    {
      value_count += meth_sig_p->no_method_args + 1;
      meth_sig_p = meth_sig_p->next;
    }

  if (list_id_p->type_list.type_cnt > value_count)
    {
      value_count = list_id_p->type_list.type_cnt;
    }

  val_list_p = (DB_VALUE *) malloc (sizeof (DB_VALUE) * value_count);
  if (val_list_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (DB_VALUE) * value_count);
      method_clear_vacomm_buffer (&vacomm_buffer);
      return ER_FAILED;
    }

  for (count = 0, value_p = val_list_p; count < value_count;
       count++, value_p++)
    {
      DB_MAKE_NULL (value_p);
    }

  values_p = (DB_VALUE **) malloc (sizeof (DB_VALUE *) * (value_count + 1));
  if (values_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (DB_VALUE *) * (value_count + 1));
      method_clear_vacomm_buffer (&vacomm_buffer);
      free_and_init (val_list_p);
      return ER_FAILED;
    }

  oid_cols = (int *) malloc (sizeof (int) * method_sig_list_p->no_methods);
  if (oid_cols == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (int) * method_sig_list_p->no_methods);
      method_clear_vacomm_buffer (&vacomm_buffer);
      free_and_init (val_list_p);
      free_and_init (values_p);
      return ER_FAILED;
    }

  meth_sig_p = method_sig_list_p->method_sig;
  for (method_no = 0; method_no < method_sig_list_p->no_methods; method_no++)
    {
      oid_cols[method_no] = meth_sig_p->method_arg_pos[0];
      meth_sig_p = meth_sig_p->next;
    }

  if (!cursor_open (&cursor_id, list_id_p, false, false))
    {
      method_clear_vacomm_buffer (&vacomm_buffer);
      free_and_init (values_p);
      free_and_init (val_list_p);
      free_and_init (oid_cols);
      return ER_FAILED;
    }

  /* tfile_vfid pointer as query id for method scan */
  cursor_id.query_id = (int) list_id_p->tfile_vfid;

  cursor_set_oid_columns (&cursor_id, oid_cols,
			  method_sig_list_p->no_methods);

  while (true)
    {
      cursor_result = cursor_next_tuple (&cursor_id);
      if (cursor_result != DB_CURSOR_SUCCESS)
	{
	  break;
	}

      if (cursor_get_tuple_value_list (&cursor_id,
				       list_id_p->type_list.type_cnt,
				       val_list_p) != NO_ERROR)
	{
	  cursor_result = -1;
	  goto end;
	}

      for (method_no = 0,
	   meth_sig_p = method_sig_list_p->method_sig;
	   method_no < method_sig_list_p->no_methods;
	   ++method_no, meth_sig_p = meth_sig_p->next)
	{
	  /* The first position # is for the object ID */
	  no_args = meth_sig_p->no_method_args + 1;
	  for (arg = 0; arg < no_args; ++arg)
	    {
	      pos = meth_sig_p->method_arg_pos[arg];
	      values_p[arg] = &val_list_p[pos];
	    }

	  values_p[no_args] = (DB_VALUE *) 0;
	  DB_MAKE_NULL (&value);

	  if (meth_sig_p->class_name != NULL)
	    {
	      /* Don't call the method if the object is NULL or it has been
	       * deleted.  A method call on a NULL object is NULL.
	       */
	      if ((DB_IS_NULL (values_p[0])) ||
		  !(db_is_any_class (DB_GET_OBJECT (values_p[0])) ||
		    db_is_instance (DB_GET_OBJECT (values_p[0]))))
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
		  error = obj_send_array (DB_GET_OBJECT (values_p[0]),
					  meth_sig_p->method_name,
					  &value, &values_p[1]);
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
	      error = jsp_call_from_server (&value, values_p,
					    meth_sig_p->method_name,
					    meth_sig_p->no_method_args);
	      db_Disable_modifications--;
	      AU_DISABLE (turn_on_auth);
	    }

	  if (error != NO_ERROR)
	    {
	      cursor_result = -1;
	      goto end;
	    }

	  if (DB_VALUE_TYPE (&value) == DB_TYPE_ERROR)
	    {
	      if (er_errid () == NO_ERROR)	/* caller has not set an error */
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
			  1);
		}
	      cursor_result = -1;
	      goto end;
	    }

	  error = method_send_value_to_server (&value, &vacomm_buffer);
	  if (error != NO_ERROR)
	    {
	      if (vacomm_buffer.action == VACOMM_BUFFER_ABORT)
		{
		  cursor_result = DB_CURSOR_END;
		}
	      else
		{
		  cursor_result = -1;
		}
	      goto end;
	    }

	  pr_clear_value (&value);
	}

      for (count = 0, value_p = val_list_p; count < value_count;
	   count++, value_p++)
	{
	  pr_clear_value (value_p);
	}
    }

end:
  cursor_close (&cursor_id);
  free_and_init (values_p);

  pr_clear_value (&value);
  for (count = 0, value_p = val_list_p; count < value_count;
       count++, value_p++)
    {
      pr_clear_value (value_p);
    }

  free_and_init (val_list_p);
  free_and_init (oid_cols);

  if (cursor_result == DB_CURSOR_END)
    {
      error = method_send_eof_to_server (&vacomm_buffer);
      method_clear_vacomm_buffer (&vacomm_buffer);
      return error;
    }
  else
    {
      method_clear_vacomm_buffer (&vacomm_buffer);
      return ER_FAILED;
    }
}
