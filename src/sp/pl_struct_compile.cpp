/*
 *
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

#include "pl_struct_compile.hpp"

#include "byte_order.h"
#include "connection_support.hpp"
#include "dbtype.h"		/* db_value_* */

#include "method_struct_value.hpp"
#include "sp_constants.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubpl
{
//////////////////////////////////////////////////////////////////////////
// plcsql_compile_request
//////////////////////////////////////////////////////////////////////////

  plcsql_compile_request::plcsql_compile_request () {};

  void
  plcsql_compile_request::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (type);

    switch (type)
      {
      case PLCSQL_COMPILE_TYPE_SP:
	serializator.pack_all (code, owner, mode);
	break;
      case PLCSQL_COMPILE_TYPE_PKG_SPEC:
	serializator.pack_all (code, body_code, owner, mode);
	break;
      case PLCSQL_COMPILE_TYPE_PKG_BODY:
	serializator.pack_all (body_code, owner, mode);
	break;
      default:
	assert (false);
	break;
      }
  }

  size_t
  plcsql_compile_request::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset);      // type

    switch (type)
      {
      case PLCSQL_COMPILE_TYPE_SP:
	size += serializator.get_all_packed_size_starting_offset (size, code, owner, mode);
	break;
      case PLCSQL_COMPILE_TYPE_PKG_SPEC:
	size += serializator.get_all_packed_size_starting_offset (size, code, body_code, owner, mode);
	break;
      case PLCSQL_COMPILE_TYPE_PKG_BODY:
	size += serializator.get_all_packed_size_starting_offset (size, body_code, owner, mode);
	break;
      default:
	assert (false);
	break;
      }

    return size;
  }

  void
  plcsql_compile_request::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (type);

    switch (type)
      {
      case PLCSQL_COMPILE_TYPE_SP:
	deserializator.unpack_all (code, owner, mode);
	break;
      case PLCSQL_COMPILE_TYPE_PKG_SPEC:
	deserializator.unpack_all (code, body_code, owner, mode);
	break;
      case PLCSQL_COMPILE_TYPE_PKG_BODY:
	deserializator.unpack_all (body_code, owner, mode);
	break;
      default:
	assert (false);
	break;
      }
  }

//////////////////////////////////////////////////////////////////////////
// plcsql_compile_response
//////////////////////////////////////////////////////////////////////////

  plcsql_compile_response::plcsql_compile_response () : err_code (-1) {} ;

  void
  plcsql_compile_response::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (err_code);

    if (err_code < 0)
      {
	serializator.pack_all (err_line, err_column, err_msg);
      }
    else
      {
	assert (false);       // currently, plcsql_compile_repsonse::pack() is called only for an error case
      }
  }

  size_t
  plcsql_compile_response::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // err_code

    if (err_code < 0)
      {
	size += serializator.get_all_packed_size_starting_offset (size, err_line, err_column, err_msg);
      }
    else
      {
	assert (false);       // currently, plcsql_compile_repsonse::pack() is called only for an error case
      }

    return size;
  }

  void
  plcsql_compile_response::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (err_code);
    if (err_code < 0)
      {
	deserializator.unpack_all (err_line, err_column, err_msg);
      }
    else
      {
	deserializator.unpack_int (type);

	switch (type)
	  {
	  case PLCSQL_COMPILE_TYPE_SP:
	  {
	    deserializator.unpack_all (translated_code, class_name, compiled_code, create_stmt, java_signature);

	    int dependencies_size = 0;
	    deserializator.unpack_int (dependencies_size);
	    if (dependencies_size > 0)
	      {
		dependencies.resize (dependencies_size);
		for (int i = 0; i < dependencies_size; i++)
		  {
		    dependencies[i].unpack (deserializator);
		  }
	      }
	  }
	  break;

	  case PLCSQL_COMPILE_TYPE_PKG_SPEC:
	  {
	    deserializator.unpack_all (translated_code, class_name, compiled_code);

	    // dependencies
	    int dependencies_size = 0;
	    deserializator.unpack_int (dependencies_size);
	    if (dependencies_size > 0)
	      {
		dependencies.resize (dependencies_size);
		for (int i = 0; i < dependencies_size; i++)
		  {
		    dependencies[i].unpack (deserializator);
		  }
	      }

	    // sp
	    int sp_size = 0;
	    deserializator.unpack_int (sp_size);
	    if (sp_size > 0)
	      {
		sp.resize (sp_size);
		for (int i = 0; i < sp_size; i++)
		  {
		    sp[i].unpack (deserializator);
		  }
	      }

	    // var
	    int var_size = 0;
	    deserializator.unpack_int (var_size);
	    if (var_size > 0)
	      {
		var.resize (var_size);
		for (int i = 0; i < var_size; i++)
		  {
		    var[i].unpack (deserializator);
		  }
	      }

	    // exception
	    int exception_size = 0;
	    deserializator.unpack_int (exception_size);
	    if (exception_size > 0)
	      {
		exception.resize (exception_size);
		for (int i = 0; i < exception_size; i++)
		  {
		    exception[i].unpack (deserializator);
		  }
	      }

	    // cursor
	    int cursor_size = 0;
	    deserializator.unpack_int (cursor_size);
	    if (cursor_size > 0)
	      {
		cursor.resize (cursor_size);
		for (int i = 0; i < cursor_size; i++)
		  {
		    cursor[i].unpack (deserializator);
		  }
	      }

	    // rec_type
	    int rec_type_size = 0;
	    deserializator.unpack_int (rec_type_size);
	    if (rec_type_size > 0)
	      {
		rec_type.resize (rec_type_size);
		for (int i = 0; i < rec_type_size; i++)
		  {
		    rec_type[i].unpack (deserializator);
		  }
	      }
	  }

	  break;

	  case PLCSQL_COMPILE_TYPE_PKG_BODY:
	    // no values to unpack: error_code = 0 is the only relevant information in this case
	    break;
	  default:
	    assert (false);
	  }
      }
  }

//////////////////////////////////////////////////////////////////////////
// plcsql_dependency
//////////////////////////////////////////////////////////////////////////

  plcsql_dependency::plcsql_dependency () {};

  void
  plcsql_dependency::pack (cubpacking::packer &serializator) const
  {
    assert (false);     // unreachable
  }

  size_t
  plcsql_dependency::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    assert (false);     // unreachable
    return 0;
  }

  void
  plcsql_dependency::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (obj_type);
    deserializator.unpack_string (obj_uniq_name);
  }

//////////////////////////////////////////////////////////////////////////
// sql_semantics
//////////////////////////////////////////////////////////////////////////

  sql_semantics::sql_semantics () {};

  void
  sql_semantics::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (idx);
    serializator.pack_int (sql_type);
    serializator.pack_string (rewritten_query);

    if (sql_type >= 0)
      {
	serializator.pack_int (columns.size());
	for (int i = 0; i < (int) columns.size(); i++)
	  {
	    columns[i].pack (serializator);
	  }

	serializator.pack_int (hvs.size ());
	for (int i = 0; i < (int) hvs.size(); i++)
	  {
	    hvs[i].pack (serializator);
	  }

	serializator.pack_int (into_vars.size ());
	for (int i = 0; i < (int) into_vars.size (); i++)
	  {
	    serializator.pack_string (into_vars[i]);
	  }

	serializator.pack_int (dependencies.size ());
	for (int i = 0; i < (int) dependencies.size(); i++)
	  {
	    dependencies[i].pack (serializator);
	  }
      }
  }

  size_t
  sql_semantics::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // idx
    size += serializator.get_packed_int_size (size); // sql_type
    size += serializator.get_packed_string_size (rewritten_query, size); // rewritten_query

    if (sql_type >= 0)
      {
	size += serializator.get_packed_int_size (size); // num_columns
	if (columns.size() > 0)
	  {
	    for (int i = 0; i < (int) columns.size(); i++)
	      {
		size += columns[i].get_packed_size (serializator, size);
	      }
	  }

	size += serializator.get_packed_int_size (size); // hvs size
	if (hvs.size() > 0) // host variables
	  {
	    for (int i = 0; i < (int) hvs.size(); i++)
	      {
		size += hvs[i].get_packed_size (serializator, size);
	      }
	  }

	size += serializator.get_packed_int_size (size); // into_vars size
	if (into_vars.size() > 0) // into variables
	  {
	    for (int i = 0; i < (int) into_vars.size (); i++)
	      {
		size += serializator.get_packed_string_size (into_vars[i], size);
	      }
	  }

	size += serializator.get_packed_int_size (size); // dependencies size
	if (dependencies.size() > 0) // dependencies
	  {
	    for (int i = 0; i < (int) dependencies.size(); i++)
	      {
		size += dependencies[i].get_packed_size (serializator, size);
	      }
	  }
      }

    return size;
  }

  void
  sql_semantics::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (idx);
    deserializator.unpack_int (sql_type);

    if (sql_type >= 0)
      {
	int column_size = 0;
	deserializator.unpack_int (column_size);

	if (column_size > 0)
	  {
	    columns.resize (column_size);
	    for (int i = 0; i < (int) column_size; i++)
	      {
		columns[i].unpack (deserializator);
	      }
	  }

	int hv_size = 0;
	deserializator.unpack_int (hv_size);

	if (hv_size > 0)
	  {
	    hvs.resize (hv_size);
	    for (int i = 0; i < (int) hv_size; i++)
	      {
		hvs[i].unpack (deserializator);
	      }
	  }

	std::string s;
	int into_vars_size = 0;
	deserializator.unpack_int (into_vars_size);
	for (int i = 0; i < into_vars_size; i++)
	  {
	    deserializator.unpack_string (s);
	    into_vars.push_back (s);
	  }

	int dependencies_size = 0;
	deserializator.unpack_int (dependencies_size);

	if (dependencies_size > 0)
	  {
	    dependencies.resize (dependencies_size);
	    for (int i = 0; i < (int) dependencies_size; i++)
	      {
		dependencies[i].unpack (deserializator);
	      }
	  }
      }
  }

//////////////////////////////////////////////////////////////////////////
// sql_semantics_request
//////////////////////////////////////////////////////////////////////////

  sql_semantics_request::sql_semantics_request ()
  {
    code = METHOD_CALLBACK_GET_SQL_SEMANTICS;
  }

  void
  sql_semantics_request::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (code);
    serializator.pack_int (sqls.size ());
    for (int i = 0; i < (int) sqls.size (); i++)
      {
	serializator.pack_string (sqls[i]);
      }
  }

  size_t
  sql_semantics_request::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // code
    size += serializator.get_packed_int_size (size); // size
    for (int i = 0; i < (int) sqls.size (); i++)
      {
	size += serializator.get_packed_string_size (sqls[i], size);
      }

    return size;
  }

  void
  sql_semantics_request::unpack (cubpacking::unpacker &deserializator)
  {
    code = METHOD_CALLBACK_GET_SQL_SEMANTICS;
    int size;
    deserializator.unpack_int (size);
    if (size != 1)
      {
	// current implementation only asks one by one.
	er_log_debug (ARG_FILE_LINE, "note: size of SQL semantics request %d\n", size);
      }

    std::string s;
    for (int i = 0; i < size; i++)
      {
	deserializator.unpack_string (s);
	sqls.push_back (s);
      }
  }

//////////////////////////////////////////////////////////////////////////
// sql_semantics_response
//////////////////////////////////////////////////////////////////////////

  void
  sql_semantics_response::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (semantics.size ());
    for (int i = 0; i < (int) semantics.size (); i++)
      {
	semantics[i].pack (serializator);
      }
  }

  size_t
  sql_semantics_response::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // sizes
    for (int i = 0; i < (int) semantics.size (); i++)
      {
	size += semantics[i].get_packed_size (serializator, size);
      }

    return size;
  }

  void
  sql_semantics_response::unpack (cubpacking::unpacker &deserializator)
  {
    assert (false);     // unreachable
  }

//////////////////////////////////////////////////////////////////////////
// pl_parameter_info
//////////////////////////////////////////////////////////////////////////

  pl_parameter_info::pl_parameter_info ()
    : mode (0)
    , name ("?")
    , type (DB_TYPE_NULL)
    , precision (0)
    , scale (0)
    , charset (0)
    , has_default (0)
  {
    db_make_null (&value);
  }

  pl_parameter_info::~pl_parameter_info ()
  {
    // value is create by pt_value_to_db ()
    // it doesn't have to call db_value_clear ()
  }

  void
  pl_parameter_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (mode);

    serializator.pack_string (name);

    serializator.pack_int (type);
    serializator.pack_int (precision);
    serializator.pack_int (scale);
    serializator.pack_int (charset);
    serializator.pack_int (has_default);

    if (!DB_IS_NULL (&value))
      {
	cubmethod::dbvalue_java sp_val;
	serializator.pack_int (1);
	sp_val.value = (DB_VALUE *) &value;
	sp_val.pack (serializator);
      }
    else
      {
	serializator.pack_int (0);
      }
  }

  size_t
  pl_parameter_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // mode

    size += serializator.get_packed_string_size (name, size);

    size += serializator.get_packed_int_size (size); // type
    size += serializator.get_packed_int_size (size); // precision
    size += serializator.get_packed_int_size (size); // scale
    size += serializator.get_packed_int_size (size); // charset
    size += serializator.get_packed_int_size (size); // has_default

    size += serializator.get_packed_int_size (size); // value is null
    if (!DB_IS_NULL (&value))
      {
	cubmethod::dbvalue_java sp_val;
	sp_val.value = (DB_VALUE *) &value;
	size += sp_val.get_packed_size (serializator, size);
      }

    return size;
  }

  void
  pl_parameter_info::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (mode);

    deserializator.unpack_string (name);

    deserializator.unpack_int (type);
    deserializator.unpack_int (precision);
    deserializator.unpack_int (scale);
    deserializator.unpack_int (charset);
    deserializator.unpack_int (has_default);

    int value_is_null;
    deserializator.unpack_int (value_is_null);

    if (value_is_null == 1)
      {
	cubmethod::dbvalue_java value_unpacker;
	value_unpacker.value = &value;
	value_unpacker.unpack (deserializator);
      }
    else
      {
	db_make_null (&value);
      }
  }

//////////////////////////////////////////////////////////////////////////
// global_semantics_question
//////////////////////////////////////////////////////////////////////////

#define GLOBAL_SEMANTICS_QUESTION_PACKER_ARGS() \
  type, name

#define GLOBAL_SEMANTICS_REQUEST_PACKER_ARGS() \
  qsqs

  void
  global_semantics_question::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_all (GLOBAL_SEMANTICS_QUESTION_PACKER_ARGS());
  }

  size_t
  global_semantics_question::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, GLOBAL_SEMANTICS_QUESTION_PACKER_ARGS ());
  }

  void
  global_semantics_question::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (GLOBAL_SEMANTICS_QUESTION_PACKER_ARGS ());
  }

//////////////////////////////////////////////////////////////////////////
// global_semantics_request
//////////////////////////////////////////////////////////////////////////

  void
  global_semantics_request::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (code);
    serializator.pack_all (GLOBAL_SEMANTICS_REQUEST_PACKER_ARGS());
  }

  global_semantics_request::global_semantics_request ()
    : code (METHOD_CALLBACK_GET_GLOBAL_SEMANTICS)
  {
    //
  }

  size_t
  global_semantics_request::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // code
    size += serializator.get_all_packed_size_starting_offset (size, GLOBAL_SEMANTICS_REQUEST_PACKER_ARGS ());
    return size;
  }

  void
  global_semantics_request::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (GLOBAL_SEMANTICS_REQUEST_PACKER_ARGS ());
  }

//////////////////////////////////////////////////////////////////////////
// global_semantics_response_common
//////////////////////////////////////////////////////////////////////////

#define GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS() \
  idx, err_id, err_msg

#define GLOBAL_SEMANTICS_RESPONSE_UDPF_PACKER_ARGS() \
  GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS(), ret, args

#define GLOBAL_SEMANTICS_RESPONSE_SERIAL_PACKER_ARGS() \
  GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS()

#define GLOBAL_SEMANTICS_RESPONSE_COLUMN_PACKER_ARGS() \
  GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS(), c_info

  global_semantics_response_common::global_semantics_response_common ()
    : idx (-1)
    , err_id (0)
    , err_msg {}
  {
    //
  }

  void
  global_semantics_response_common::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_all (GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS());
  }

  size_t
  global_semantics_response_common::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS ());
  }

  void
  global_semantics_response_common::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS ());
  }

  global_semantics_response_udpf::global_semantics_response_udpf ()
    : ret ()
    , args {}
  {
    //
  }

//////////////////////////////////////////////////////////////////////////
// global_semantics_response_udpf
//////////////////////////////////////////////////////////////////////////

  void
  global_semantics_response_udpf::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_all (GLOBAL_SEMANTICS_RESPONSE_UDPF_PACKER_ARGS());
  }

  size_t
  global_semantics_response_udpf::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset,
	   GLOBAL_SEMANTICS_RESPONSE_UDPF_PACKER_ARGS ());
  }

  void
  global_semantics_response_udpf::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (GLOBAL_SEMANTICS_RESPONSE_UDPF_PACKER_ARGS ());
  }

//////////////////////////////////////////////////////////////////////////
// global_semantics_responses_serial
//////////////////////////////////////////////////////////////////////////

  void
  global_semantics_response_serial::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_all (GLOBAL_SEMANTICS_RESPONSE_SERIAL_PACKER_ARGS());
  }

  size_t
  global_semantics_response_serial::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, GLOBAL_SEMANTICS_RESPONSE_SERIAL_PACKER_ARGS ());
  }

  void
  global_semantics_response_serial::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (GLOBAL_SEMANTICS_RESPONSE_SERIAL_PACKER_ARGS ());
  }

//////////////////////////////////////////////////////////////////////////
// global_semantics_responses_column
//////////////////////////////////////////////////////////////////////////

  global_semantics_response_column::global_semantics_response_column ()
    : c_info ()
  {
    //
  }

  void
  global_semantics_response_column::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_all (GLOBAL_SEMANTICS_RESPONSE_COLUMN_PACKER_ARGS());
  }

  size_t
  global_semantics_response_column::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, GLOBAL_SEMANTICS_RESPONSE_COLUMN_PACKER_ARGS ());
  }

  void
  global_semantics_response_column::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (GLOBAL_SEMANTICS_RESPONSE_COLUMN_PACKER_ARGS ());
  }

//////////////////////////////////////////////////////////////////////////
// global_semantics_response
//////////////////////////////////////////////////////////////////////////

  void
  global_semantics_response::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_bigint (qs.size ());

    for (const auto &res : qs)
      {
	res->pack (serializator);
      }
  }

  size_t
  global_semantics_response::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_bigint_size (start_offset);	// size

    for (const auto &res : qs)
      {
	size += res->get_packed_size (serializator, size);
      }

    return size;
  }

  void
  global_semantics_response::unpack (cubpacking::unpacker &deserializator)
  {
    assert (false);
  }

//////////////////////////////////////////////////////////////////////////
// package related
//////////////////////////////////////////////////////////////////////////

  pkg_sp_arg::pkg_sp_arg()
  {
  }

  void
  pkg_sp_arg::pack (cubpacking::packer &serializator) const
  {
    assert (false); // unreachable
  }

  size_t
  pkg_sp_arg::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    assert (false); // unreachable
    return 0;
  }

  void
  pkg_sp_arg::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (name, data_type, mode, default_value, comment);
  }

  pkg_sp::pkg_sp()
  {
  }

  void
  pkg_sp::pack (cubpacking::packer &serializator) const
  {
    assert (false); // unreachable
  }

  size_t
  pkg_sp::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    assert (false); // unreachable
    return 0;
  }

  void
  pkg_sp::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (java_signature, name, type, return_type, directive, sql_data_access, comment);

    int args_size = 0;
    deserializator.unpack_int (args_size);
    if (args_size > 0)
      {
	args.resize (args_size);
	for (int i = 0; i < args_size; i++)
	  {
	    args[i].unpack (deserializator);
	  }
      }
  }

  pkg_var::pkg_var()
  {
  }

  void
  pkg_var::pack (cubpacking::packer &serializator) const
  {
    assert (false); // unreachable
  }

  size_t
  pkg_var::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    assert (false); // unreachable
    return 0;
  }

  void
  pkg_var::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (data_type, prec, scale, flags, name, init_value, comment);
  }

  pkg_exception::pkg_exception()
  {
  }

  void
  pkg_exception::pack (cubpacking::packer &serializator) const
  {
    assert (false); // unreachable
  }

  size_t
  pkg_exception::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    assert (false); // unreachable
    return 0;
  }

  void
  pkg_exception::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (name, comment);
  }

  pkg_cursor::pkg_cursor()
  {
  }

  void
  pkg_cursor::pack (cubpacking::packer &serializator) const
  {
    assert (false); // unreachable
  }

  size_t
  pkg_cursor::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    assert (false); // unreachable
    return 0;
  }

  void
  pkg_cursor::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (name, record_type, comment);
    int parameters_size = 0;
    deserializator.unpack_int (parameters_size);
    if (parameters_size > 0)
      {
	std::string s;
	for (int i = 0; i < parameters_size; i++)
	  {
	    deserializator.unpack_string (s);
	    parameters.push_back (s);
	  }
      }
  }

  pkg_rec_type::pkg_rec_type()
  {
  }

  void
  pkg_rec_type::pack (cubpacking::packer &serializator) const
  {
    assert (false); // unreachable
  }

  size_t
  pkg_rec_type::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    assert (false); // unreachable
    return 0;
  }

  void
  pkg_rec_type::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (name, comment);
    int fields_size = 0;
    deserializator.unpack_int (fields_size);
    if (fields_size > 0)
      {
	std::string s;
	for (int i = 0; i < fields_size; i++)
	  {
	    deserializator.unpack_string (s);
	    fields.push_back (s);
	  }
      }
  }

}
