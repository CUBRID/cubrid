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
 * query_method.c - Method calls in queries
 */

#ident "$Id$"

#include "query_method.h"

#include "authenticate.h"
#include "config.h"
#include "db.h"
#include "dbtype.h"
#include "jsp_cl.h"		/* jsp_call_from_server */
#include "method_def.hpp"
#include "network.h"
#include "network_interface_cl.h"
#include "object_accessor.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_list.h"
#include "regu_var.hpp"

#include "packer.hpp"		/* packing_packer */
#include "mem_block.hpp"	/* cubmem::extensible_block */

static void methid_sig_freemem (method_sig_node * meth_sig);

/*
 * method_send_error_to_server () - Send an error indication to the server
 *   return:
 *   rc(in)     : enquiry return code
 *   host(in)   : host name
 *   server_name(in)    : server name
 */
int
method_send_error_to_server (unsigned int rc, char *host_p, char *server_name_p)
{
  packing_packer packer;
  cubmem::extensible_block ext_blk;
  packer.set_buffer_and_pack_all (ext_blk, (int) METHOD_ERROR, (int) er_errid ());

  int error = net_client_send_data (host_p, rc, (char *) packer.get_buffer_start (), packer.get_current_size ());
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
method_invoke_for_server (unsigned int rc, char *host_p, char *server_name_p, std::vector < DB_VALUE > &args,
			  method_sig_list * method_sig_list_p)
{
  DB_VALUE *val_list_p = NULL;
  DB_VALUE **arg_values_p;
  int *oid_cols;
  int turn_on_auth = 1;
  int num_method;
  int num_args;
  int pos;
  int arg;
  int value_count;
  DB_VALUE value;
  METHOD_SIG *meth_sig_p;
  int error = NO_ERROR;
  int count;
  DB_VALUE *value_p;

  {
    meth_sig_p = method_sig_list_p->method_sig;
    value_count = 0;

    for (num_method = 0; num_method < method_sig_list_p->num_methods; num_method++)
      {
	value_count += meth_sig_p->num_method_args + 1;
	meth_sig_p = meth_sig_p->next;
      }

    val_list_p = (DB_VALUE *) malloc (sizeof (DB_VALUE) * value_count);
    if (val_list_p == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE) * value_count);
	return ER_FAILED;
      }

    for (count = 0, value_p = val_list_p; count < value_count; count++, value_p++)
      {
	db_make_null (value_p);
      }

    arg_values_p = (DB_VALUE **) malloc (sizeof (DB_VALUE *) * (value_count + 1));
    if (arg_values_p == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *) * (value_count + 1));
	free_and_init (val_list_p);
	return ER_FAILED;
      }

    oid_cols = (int *) malloc (sizeof (int) * method_sig_list_p->num_methods);
    if (oid_cols == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		sizeof (int) * method_sig_list_p->num_methods);
	free_and_init (val_list_p);
	free_and_init (arg_values_p);
	return ER_FAILED;
      }

    meth_sig_p = method_sig_list_p->method_sig;
    for (num_method = 0; num_method < method_sig_list_p->num_methods; num_method++)
      {
	oid_cols[num_method] = meth_sig_p->method_arg_pos[0];
	meth_sig_p = meth_sig_p->next;
      }

    for (num_method = 0, meth_sig_p = method_sig_list_p->method_sig; num_method < method_sig_list_p->num_methods;
	 ++num_method, meth_sig_p = meth_sig_p->next)
      {
	/* The first position # is for the object ID */
	num_args = meth_sig_p->num_method_args + 1;
	for (arg = 0; arg < num_args; ++arg)
	  {
	    pos = meth_sig_p->method_arg_pos[arg];
	    arg_values_p[arg] = &args[pos];
	  }

	arg_values_p[num_args] = (DB_VALUE *) 0;
	db_make_null (&value);

	if (meth_sig_p->method_type != METHOD_IS_JAVA_SP)
	  {
	    /* Don't call the method if the object is NULL or it has been deleted.  A method call on a NULL object is
	     * NULL. */
	    if (!DB_IS_NULL (arg_values_p[0]))
	      {
		error = db_is_any_class (db_get_object (arg_values_p[0]));
		if (error == 0)
		  {
		    error = db_is_instance (db_get_object (arg_values_p[0]));
		  }
	      }
	    if (error == ER_HEAP_UNKNOWN_OBJECT)
	      {
		error = NO_ERROR;
	      }
	    else if (error > 0)
	      {
		/* methods must run with authorization turned on and database modifications turned off. */
		turn_on_auth = 0;
		AU_ENABLE (turn_on_auth);
		db_disable_modification ();
		error =
		  obj_send_array (db_get_object (arg_values_p[0]), meth_sig_p->method_name, &value, &arg_values_p[1]);
		db_enable_modification ();
		AU_DISABLE (turn_on_auth);
	      }
	  }
	else
	  {
	    /* java stored procedure call */
	    turn_on_auth = 0;
	    AU_ENABLE (turn_on_auth);
	    db_disable_modification ();
	    error = jsp_call_from_server (&value, arg_values_p, meth_sig_p->method_name, meth_sig_p->num_method_args);
	    db_enable_modification ();
	    AU_DISABLE (turn_on_auth);
	  }

	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	if (DB_VALUE_TYPE (&value) == DB_TYPE_ERROR)
	  {
	    if (er_errid () == NO_ERROR)	/* caller has not set an error */
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 1);
	      }
	    goto end;
	  }

	/* send a result value to server */
	packing_packer packer;
	cubmem::extensible_block ext_blk;
	packer.set_buffer_and_pack_all (ext_blk, (int) METHOD_SUCCESS, value);
	error = net_client_send_data (host_p, rc, (char *) packer.get_buffer_start (), packer.get_current_size ());

	pr_clear_value (&value);
      }

    for (count = 0, value_p = val_list_p; count < value_count; count++, value_p++)
      {
	pr_clear_value (value_p);
      }
  }

end:
  free_and_init (values_p);

  pr_clear_value (&value);
  for (count = 0, value_p = val_list_p; count < value_count; count++, value_p++)
    {
      pr_clear_value (value_p);
    }

  free_and_init (val_list_p);
  free_and_init (oid_cols);

  return error;
}

/*
 * methid_sig_freemem () -
 *   return:
 *   method_sig(in)     : pointer to a method_sig
 *
 * Note: Free function for METHOD_SIG using free_and_init.
 */
static void
methid_sig_freemem (method_sig_node * method_sig)
{
  if (method_sig != NULL)
    {
      methid_sig_freemem (method_sig->next);
      db_private_free_and_init (NULL, method_sig->method_name);
      db_private_free_and_init (NULL, method_sig->class_name);
      db_private_free_and_init (NULL, method_sig->method_arg_pos);
      db_private_free_and_init (NULL, method_sig);
    }
}

/*
 * method_sig_list_freemem () -
 *   return:
 *   meth_sig_list(in)        : pointer to a meth_sig_list
 *
 * Note: Free function for METHOD_SIG_LIST using free_and_init.
 */
void
method_sig_list_freemem (method_sig_list * meth_sig_list)
{
  if (meth_sig_list != NULL)
    {
      methid_sig_freemem (meth_sig_list->method_sig);
      db_private_free_and_init (NULL, meth_sig_list);
    }
}
