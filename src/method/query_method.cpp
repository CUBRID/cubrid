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

#include "query_method.hpp"

#include <unordered_map>
#include <deque>
#include <vector>

#include "authenticate.h"	/* AU_ENABLE, AU_DISABLE */
#include "dbi.h"		/* db_enable_modification(), db_disable_modification() */
#include "dbtype.h"
#include "jsp_cl.h"		/* jsp_call_from_server() */
#include "method_def.hpp"	/* method_sig_list, method_sig_node */
#include "object_accessor.h"	/* obj_ */
#include "object_primitive.h"	/* pr_is_set_type() */
#include "set_object.h"		/* set_convert_oids_to_objects() */
#include "virtual_object.h"	/* vid_oid_to_object() */

#include "transaction_cl.h"

#include "network.h"
#include "network_interface_cl.h"
#include "mem_block.hpp"	/* cubmem::extensible_block */
#include "packer.hpp"		/* packing_packer */

#include "method_callback.hpp"
#include "method_query_handler.hpp"

// *INDENT-OFF*
/* For builtin C Method */
static std::unordered_map <UINT64, std::vector<DB_VALUE>> runtime_args;
/* For Java SP Method */
// *INDENT-ON*

/* FIXME: duplicated function implementation; The following three functions are ported from the client cursor (cursor.c) */
static bool method_has_set_vobjs (DB_SET *set);
static int method_fixup_set_vobjs (DB_VALUE *value_p);
static int method_fixup_vobjs (DB_VALUE *value_p);

#if defined (CS_MODE)
static int method_prepare_arguments (packing_unpacker &unpacker);
static int method_invoke_builtin (packing_unpacker &unpacker);
#endif

void method_reset ()
{
  cubmethod::get_callback_handler()->free_query_handle_all (false);
}

#if defined (CS_MODE)
/*
 * method_error () - Send error code to the server
 *   return:
 *   rc(in)     : enquiry return code
 *   host(in)   : host name
 *   server_name(in)    : server name
 *   error_id (in)    : error code
 */
int
method_error (unsigned int rc, int error_id)
{
  int error = NO_ERROR;
  tran_begin_libcas_function ();
  error = cubmethod::method_send_data_to_server (METHOD_ERROR, error_id);
  tran_begin_libcas_function();
  return error;
}

/*
 * method_dispatch () - Dispatch method protocol from the server
 *   return:
 *   rc(in)     : enquiry return code
 *   host(in)   : host name
 *   server_name(in)    : server name
 *   methoddata (in)    : data buffer
 *   methoddata_size (in) : data buffer size
 */
int
method_dispatch (unsigned int rc, char *methoddata, int methoddata_size)
{
  int error = NO_ERROR;

  packing_unpacker unpacker (methoddata, (size_t) methoddata_size);

  int method_dispatch_code;
  unpacker.unpack_int (method_dispatch_code);

  tran_begin_libcas_function ();
  int depth = tran_get_libcas_depth ();
  error = cubmethod::set_connection_info (depth - 1, rc);
  if (error == NO_ERROR)
    {
      int save_auth = 0;
      switch (method_dispatch_code)
	{
	case METHOD_REQUEST_ARG_PREPARE:
	  error = method_prepare_arguments (unpacker);
	  break;
	case METHOD_REQUEST_INVOKE:
	  AU_SAVE_AND_ENABLE (save_auth);
	  error = method_invoke_builtin (unpacker);
	  AU_RESTORE (save_auth);
	  break;
	case METHOD_REQUEST_CALLBACK:
	  AU_SAVE_AND_ENABLE (save_auth);
	  error = cubmethod::get_callback_handler()->callback_dispatch (unpacker);
	  AU_RESTORE (save_auth);
	  break;
	case METHOD_REQUEST_END:
	  cubmethod::get_callback_handler()->free_query_handle_all (false);
	  break;
	default:
	  assert (false); // the other callbacks are disabled now
	  return ER_FAILED;
	  break;
	}
    }

  tran_end_libcas_function ();
  return error;
}

/*
 * method_invoke_builtin () - Invoke C Method with runtime arguments
 *   return:
 *   unpacker (in)     : unpacker
 *   conn_info (in)   : enquiry return code, host name, server name
 */
static int
method_invoke_builtin (packing_unpacker &unpacker)
{
  int error = NO_ERROR;
  UINT64 id;
  unpacker.unpack_bigint (id);

  METHOD_SIG sig;
  sig.unpack (unpacker);

  DB_VALUE result;
  db_make_null (&result);

  // reset previous arguments
  auto search = runtime_args.find (id);
  if (search != runtime_args.end())
    {
      std::vector<DB_VALUE> &args = search->second;
      error = method_invoke (result, args, &sig);
      if (error == NO_ERROR)
	{
	  /* send a result value to server */
	  error = cubmethod::method_send_data_to_server (METHOD_SUCCESS, result);
	}
      db_value_clear (&result);
    }
  else
    {
      error = ER_GENERIC_ERROR;
    }

  sig.freemem ();
  return error;
}

/*
 * method_prepare_arguments () - Stores at DB_VALUE arguments (runtime_args) for C Method
 *   return:
 *   unpacker (in)     : unpacker
 *   conn_info (in)   : enquiry return code, host name, server name
 */
static int
method_prepare_arguments (packing_unpacker &unpacker)
{
  UINT64 id;
  unpacker.unpack_bigint (id);

  int arg_count;
  unpacker.unpack_int (arg_count);

  // reset previous arguments
  auto search = runtime_args.find (id);
  if (search != runtime_args.end())
    {
      std::vector<DB_VALUE> &prev_args = search->second;
      int prev_args_count = prev_args.size();
      for (int i = 0; i < prev_args_count; i++)
	{
	  db_value_clear (&prev_args[i]);
	}
      runtime_args.erase (search);
    }

  // new arguments
  std::vector<DB_VALUE> arguments;
  arguments.resize (arg_count);
  for (int i = 0; i < arg_count; i++)
    {
      unpacker.unpack_db_value (arguments[i]);
      method_fixup_vobjs (&arguments[i]);
    }
  runtime_args.insert ({id, arguments});
  return NO_ERROR;
}
#endif

/*
 * method_invoke () -
 *   return: int
 *   result (out)     :
 *   args (in)   : objects & arguments DB_VALUEs
 *   meth_sig_p (in) : Method signatures
 */
// *INDENT-OFF*
int
method_invoke (DB_VALUE & result, std::vector <DB_VALUE> &args, method_sig_node * meth_sig_p)
// *INDENT-ON*
{
  int error = NO_ERROR;
  int turn_on_auth = 1;

  assert (meth_sig_p != NULL);
  assert (meth_sig_p->method_type == METHOD_TYPE_CLASS_METHOD || meth_sig_p->method_type == METHOD_TYPE_INSTANCE_METHOD);

  /* The first position # is for the object ID */
  int num_args = meth_sig_p->num_method_args + 1;

  // *INDENT-OFF*
  std::vector <DB_VALUE *> arg_val_p (num_args + 1, NULL); /* + 1 for C method */
  // *INDENT-ON*
  for (int i = 0; i < num_args; ++i)
    {
      int pos = meth_sig_p->method_arg_pos[i];
      arg_val_p[i] = &args[pos];
    }

  db_make_null (&result);
  if (meth_sig_p->method_type == METHOD_TYPE_INSTANCE_METHOD || meth_sig_p->method_type == METHOD_TYPE_CLASS_METHOD)
    {
      /* Don't call the method if the object is NULL or it has been deleted.  A method call on a NULL object is
       * NULL. */
      if (!DB_IS_NULL (arg_val_p[0]))
	{
	  error = db_is_any_class (db_get_object (arg_val_p[0]));
	  if (error == 0)
	    {
	      error = db_is_instance (db_get_object (arg_val_p[0]));
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
	  error = obj_send_array (db_get_object (arg_val_p[0]), meth_sig_p->method_name, &result, &arg_val_p[1]);
	  db_enable_modification ();
	  AU_DISABLE (turn_on_auth);
	}
    }
  else
    {
      /* java stored procedure is not handled here anymore */
      assert (false);
      error = ER_GENERIC_ERROR;
    }

  /* error handling */
  if (error != NO_ERROR)
    {
      pr_clear_value (&result);
      error = ER_FAILED;
    }
  else if (DB_VALUE_TYPE (&result) == DB_TYPE_ERROR)
    {
      if (er_errid () == NO_ERROR)	/* caller has not set an error */
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 1);
	}
      pr_clear_value (&result);
      error = ER_GENERIC_ERROR;
    }

  return error;
}

/*
 * method_has_set_vobjs () -
 *   return: nonzero iff set has some vobjs, zero otherwise
 *   set (in): set/sequence db_value
 */
static bool
method_has_set_vobjs (DB_SET *set)
{
  int i, size;
  DB_VALUE element;

  size = db_set_size (set);

  for (i = 0; i < size; i++)
    {
      if (db_set_get (set, i, &element) != NO_ERROR)
	{
	  return false;
	}

      if (DB_VALUE_TYPE (&element) == DB_TYPE_VOBJ)
	{
	  pr_clear_value (&element);
	  return true;
	}

      pr_clear_value (&element);
    }

  return false;
}

/*
 * method_fixup_set_vobjs() - if val is a set/seq of vobjs then
 * 			    turn it into a set/seq of vmops
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *   value_p (in/out): a db_value
 */
static int
method_fixup_set_vobjs (DB_VALUE *value_p)
{
  DB_TYPE type;
  int rc, i, size;
  DB_VALUE element;
  DB_SET *set, *new_set;

  type = DB_VALUE_TYPE (value_p);
  if (!pr_is_set_type (type))
    {
      return ER_FAILED;
    }

  set = db_get_set (value_p);
  size = db_set_size (set);

  if (method_has_set_vobjs (set) == false)
    {
      return set_convert_oids_to_objects (set);
    }

  switch (type)
    {
    case DB_TYPE_SET:
      new_set = db_set_create_basic (NULL, NULL);
      break;
    case DB_TYPE_MULTISET:
      new_set = db_set_create_multi (NULL, NULL);
      break;
    case DB_TYPE_SEQUENCE:
      new_set = db_seq_create (NULL, NULL, size);
      break;
    default:
      return ER_FAILED;
    }

  /* fixup element vobjs into vmops and add them to new */
  for (i = 0; i < size; i++)
    {
      if (db_set_get (set, i, &element) != NO_ERROR)
	{
	  db_set_free (new_set);
	  return ER_FAILED;
	}

      if (method_fixup_vobjs (&element) != NO_ERROR)
	{
	  db_set_free (new_set);
	  return ER_FAILED;
	}

      if (type == DB_TYPE_SEQUENCE)
	{
	  rc = db_seq_put (new_set, i, &element);
	}
      else
	{
	  rc = db_set_add (new_set, &element);
	}

      if (rc != NO_ERROR)
	{
	  db_set_free (new_set);
	  return ER_FAILED;
	}
    }

  pr_clear_value (value_p);

  switch (type)
    {
    case DB_TYPE_SET:
      db_make_set (value_p, new_set);
      break;
    case DB_TYPE_MULTISET:
      db_make_multiset (value_p, new_set);
      break;
    case DB_TYPE_SEQUENCE:
      db_make_sequence (value_p, new_set);
      break;
    default:
      db_set_free (new_set);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * method_fixup_vobjs () -
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *   value_p (in/out): a db_value
 * Note: if value_p is an OID then turn it into an OBJECT type value
 *       if value_p is a VOBJ then turn it into a vmop
 *       if value_p is a set/seq then do same fixups on its elements
 */
static int
method_fixup_vobjs (DB_VALUE *value_p)
{
  DB_OBJECT *obj;
  int rc;

  switch (DB_VALUE_DOMAIN_TYPE (value_p))
    {
    case DB_TYPE_OID:
      rc = vid_oid_to_object (value_p, &obj);
      db_make_object (value_p, obj);
      break;

    case DB_TYPE_VOBJ:
      if (DB_IS_NULL (value_p))
	{
	  pr_clear_value (value_p);
	  db_value_domain_init (value_p, DB_TYPE_OBJECT, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	  rc = NO_ERROR;
	}
      else
	{
	  rc = vid_vobj_to_object (value_p, &obj);
	  pr_clear_value (value_p);
	  db_make_object (value_p, obj);
	}
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      // fixup any set/seq of vobjs into a set/seq of vmops
      rc = method_fixup_set_vobjs (value_p);
      value_p->need_clear = true;
      break;

    default:
      rc = NO_ERROR;
      break;
    }

  return rc;
}
