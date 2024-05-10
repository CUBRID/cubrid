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
#include <vector>

#include "dbtype.h"

#if !defined (SERVER_MODE)
#include "authenticate.h"	/* AU_ENABLE, AU_DISABLE */
#include "dbi.h"		/* db_enable_modification(), db_disable_modification() */
#include "object_accessor.h"	/* obj_ */
#include "object_primitive.h"	/* pr_is_set_type() */
#include "set_object.h"		/* set_convert_oids_to_objects() */
#include "virtual_object.h"	/* vid_oid_to_object() */
#include "network.h"
#include "network_interface_cl.h"

#include "mem_block.hpp"	/* cubmem::extensible_block */
#include "method_callback.hpp"
#include "method_def.hpp"	/* method_sig_list, method_sig_node */
#include "method_query_handler.hpp"

#include "transaction_cl.h"
#include "packer.hpp"		/* packing_packer */
#endif

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "method_invoke_group.hpp"
#include "method_struct_invoke.hpp"
#include "thread_compat.hpp"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SA_MODE)
int method_Num_method_jsp_calls = 0;

extern unsigned int db_on_server;

#define ENTER_SERVER_IN_METHOD_CALL(save_pri_heap_id_) \
  do { \
    db_on_server = 1; \
    private_heap_id = save_pri_heap_id_; \
  } while (0)

#define EXIT_SERVER_IN_METHOD_CALL(save_pri_heap_id_) \
  do { \
     save_pri_heap_id_ = private_heap_id; \
     private_heap_id = 0; \
     db_on_server = 0; \
  } while (0)
#endif

#if !defined (SERVER_MODE)
/* For builtin C Method */
static std::unordered_map <UINT64, std::vector<DB_VALUE>> runtime_args;

/* data queue */
static std::queue <cubmem::extensible_block> data_queue;

static void method_erase_runtime_arguments (UINT64 id);
static void method_set_runtime_arguments (UINT64 id, std::vector<DB_VALUE> &args);

static int method_prepare_arguments (packing_unpacker &unpacker);
static int method_invoke_builtin (packing_unpacker &unpacker, DB_VALUE &result);
static int method_invoke_builtin_internal (DB_VALUE &result, std::vector<DB_VALUE> &args, method_sig_node *meth_sig_p);

static int method_dispatch_internal (packing_unpacker &unpacker);

/* FIXME: duplicated function implementation; The following three functions are ported from the client cursor (cursor.c) */
static bool method_has_set_vobjs (DB_SET *set);
static int method_fixup_set_vobjs (DB_VALUE *value_p);
static int method_fixup_vobjs (DB_VALUE *value_p);
#endif

#if defined (CS_MODE)
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

  tran_begin_libcas_function ();
  int depth = tran_get_libcas_depth ();
  if (depth > METHOD_MAX_RECURSION_DEPTH)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TOO_MANY_NESTED_CALL, 0);
      error = ER_SP_TOO_MANY_NESTED_CALL;
    }

  if (error == NO_ERROR)
    {
      cubmethod::mcon_set_connection_info (depth - 1, rc);
      error = method_dispatch_internal (unpacker);
    }

  tran_end_libcas_function ();
  return error;
}

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
  tran_begin_libcas_function();
  int depth = tran_get_libcas_depth ();
  cubmethod::mcon_set_connection_info (depth - 1, rc);
  error = cubmethod::mcon_send_data_to_server (METHOD_ERROR, error_id);
  tran_end_libcas_function();
  return error;
}
#elif defined (SA_MODE)
/*
 * method_dispatch () - Dispatch method protocol for SA Mode
 *   return:
 *   unpacker(in)     : unpacker for request
 */
int
method_dispatch (packing_unpacker &unpacker)
{
  int error = NO_ERROR;
  HL_HEAPID save_pri_heap_id;
  EXIT_SERVER_IN_METHOD_CALL (save_pri_heap_id);
  ++method_Num_method_jsp_calls;

  tran_begin_libcas_function ();
  int depth = tran_get_libcas_depth ();
  if (depth > METHOD_MAX_RECURSION_DEPTH)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TOO_MANY_NESTED_CALL, 0);
      error = ER_SP_TOO_MANY_NESTED_CALL;
    }

  if (error == NO_ERROR)
    {
      error = method_dispatch_internal (unpacker);
    }

  tran_end_libcas_function ();

  --method_Num_method_jsp_calls;
  ENTER_SERVER_IN_METHOD_CALL (save_pri_heap_id);
  return error;
}
#endif

#if !defined (SERVER_MODE)
/*
 * method_dispatch_internal () - Dispatch method protocol from the server
 *   return:
 *   rc(in)     : enquiry return code
 *   host(in)   : host name
 *   server_name(in)    : server name
 *   methoddata (in)    : data buffer
 *   methoddata_size (in) : data buffer size
 */
int
method_dispatch_internal (packing_unpacker &unpacker)
{
  int error = NO_ERROR;
  int method_dispatch_code;
  unpacker.unpack_int (method_dispatch_code);

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
	  DB_VALUE value;
	  error = method_invoke_builtin (unpacker, value);
	  AU_RESTORE (save_auth);
	  break;
	case METHOD_REQUEST_CALLBACK:
	  AU_SAVE_AND_ENABLE (save_auth);
	  error = cubmethod::get_callback_handler()->callback_dispatch (unpacker);
	  AU_RESTORE (save_auth);
	  break;
	case METHOD_REQUEST_END:
	{
	  uint64_t id;
	  std::vector <int> handlers;
	  unpacker.unpack_all (id, handlers);
	  for (int i = 0; i < handlers.size (); i++)
	    {
	      cubmethod::get_callback_handler()->free_query_handle (handlers[i], false);
	    }
	}
	break;
	default:
	  assert (false); // the other callbacks are disabled now
	  return ER_FAILED;
	  break;
	}
    }

  return error;
}

/*
 * method_invoke_builtin () - Invoke C Method with runtime arguments
 *   return:
 *   unpacker (in)     : unpacker
 *   result (out)   : result
 */
static int
method_invoke_builtin (packing_unpacker &unpacker, DB_VALUE &result)
{
  int error = NO_ERROR;
  UINT64 id;
  METHOD_SIG sig;

  unpacker.unpack_bigint (id);
  sig.unpack (unpacker);

  auto search = runtime_args.find (id);
  if (search != runtime_args.end())
    {
      std::vector<DB_VALUE> &args = search->second;
      error = method_invoke_builtin_internal (result, args, &sig);
      if (error == NO_ERROR)
	{
	  /* send a result value to server */
	  error = cubmethod::mcon_send_data_to_server (METHOD_SUCCESS, result);
	}
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
  std::vector<DB_VALUE> arguments;

  unpacker.unpack_all (id, arguments);

  method_erase_runtime_arguments (id);
  method_set_runtime_arguments (id, arguments);

  return NO_ERROR;
}

/*
 * method_erase_runtime_arguments () -
 *   return: void
 *   id (in)     : method_invoke_group's id
 */
void
method_erase_runtime_arguments (UINT64 id)
{
  auto search = runtime_args.find (id);
  if (search != runtime_args.end())
    {
      std::vector<DB_VALUE> &prev_args = search->second;
      pr_clear_value_vector (prev_args);
      runtime_args.erase (search);
    }
}

/*
 * method_set_runtime_arguments () -
 *   return: void
 *   id (in)     : method_invoke_group's id
*    args (in)      : DB_VALUE arguments
 */
void
method_set_runtime_arguments (UINT64 id, std::vector<DB_VALUE> &args)
{
  for (DB_VALUE &v : args)
    {
      method_fixup_vobjs (&v);
    }
  runtime_args.insert ({id, args});
}

/*
 * method_invoke_builtin_internal () -
 *   return: int
 *   result (out)     :
 *   args (in)   : objects & arguments DB_VALUEs
 *   meth_sig_p (in) : Method signatures
 */
// *INDENT-OFF*
int
method_invoke_builtin_internal (DB_VALUE & result, std::vector<DB_VALUE> &args, method_sig_node * meth_sig_p)
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
#endif

#if defined (SERVER_MODE) || defined (SA_MODE)
/*
 *  xmethod_invoke_fold_constants () - perform constant folding for method
 *  return	  : error code
 *  thread_p (in) : worker thread
 *  sig_list(in) : method signature
 *  args(in) : method argument
 */
int xmethod_invoke_fold_constants (THREAD_ENTRY *thread_p, const method_sig_list &sig_list,
				   std::vector<std::reference_wrapper<DB_VALUE>> &args,
				   DB_VALUE &result)
{
  int error_code = NO_ERROR;
  cubmethod::method_invoke_group *method_group = cubmethod::get_rctx (thread_p)->create_invoke_group (thread_p, sig_list,
      false);
  method_group->begin ();

  std::vector<bool> dummy_use_vec (args.size(), true);
  error_code = method_group->prepare (args, dummy_use_vec);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = method_group->execute (args);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  DB_VALUE &res = method_group->get_return_value (0);
  db_value_clone (&res, &result);
  return error_code;
}
#endif
