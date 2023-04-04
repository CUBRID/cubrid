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

#include "method_compile_def.hpp"

#include "byte_order.h"
#include "connection_support.h"
#include "dbtype.h"		/* db_value_* */
#include "method_def.hpp"
#include "method_struct_value.hpp"

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// compile info
//////////////////////////////////////////////////////////////////////////
  compile_info::compile_info ()
    : err_code (-1)
    , err_line (0)
  {
    //
  }

  void
  compile_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (err_code);
    if (err_code < 0)
      {
	serializator.pack_int (err_line);
	serializator.pack_string (err_msg);
      }
    else
      {
	serializator.pack_string (translated_code);
	serializator.pack_string (register_stmt);
	serializator.pack_string (java_class_name);
      }
  }

  size_t
  compile_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // err_code

    if (err_code < 0)
      {
	size += serializator.get_packed_int_size (size); // err_line
	size += serializator.get_packed_string_size (err_msg, size); // err_msg
      }
    else
      {
	size += serializator.get_packed_string_size (translated_code, size); // translated_code
	size += serializator.get_packed_string_size (register_stmt, size); // register_stmt
	size += serializator.get_packed_string_size (java_class_name, size); // java_class_name
      }

    return size;
  }

  void
  compile_info::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (err_code);
    if (err_code < 0)
      {
	deserializator.unpack_int (err_line);
	deserializator.unpack_string (err_msg);
      }
    else
      {
	deserializator.unpack_string (translated_code);
	deserializator.unpack_string (register_stmt);
	deserializator.unpack_string (java_class_name);
      }
  }

//////////////////////////////////////////////////////////////////////////
// sql semantics
//////////////////////////////////////////////////////////////////////////
  sql_semantics::sql_semantics ()
  {
    //
  }

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

	if (hvs.size() > 0) // host variables
	  {
	    for (int i = 0; i < (int) hvs.size(); i++)
	      {
		size += hvs[i].get_packed_size (serializator, size);
	      }
	  }

	size += serializator.get_packed_int_size (size); // into_vars size
	for (int i = 0; i < (int) into_vars.size (); i++)
	  {
	    size += serializator.get_packed_string_size (into_vars[i], size);
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
      }
  }

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

    std::string s;
    for (int i = 0; i < size; i++)
      {
	deserializator.unpack_string (s);
	sqls.push_back (s);
      }
  }

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
    //
  }

  pl_parameter_info::pl_parameter_info ()
    : mode (0)
    , name ("?")
    , type (DB_TYPE_NULL)
    , precision (0)
    , scale (0)
    , charset (0)
  {
    db_make_null (&value);
  }

  pl_parameter_info::~pl_parameter_info ()
  {
    // db_value_clear (&value);
    // db_make_null (&value);
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

    if (value.domain.general_info.is_null == 0)
      {
	dbvalue_java sp_val;
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

    size += serializator.get_packed_int_size (size); // value is null
    if (value.domain.general_info.is_null == 0)
      {
	dbvalue_java sp_val;
	sp_val.value = (DB_VALUE *) &value;
	size += sp_val.get_packed_size (serializator, size);
      }

    return size;
  }

  void
  pl_parameter_info::unpack (cubpacking::unpacker &deserializator)
  {
    //
  }

//////////////////////////////////////////////////////////////////////////
// global semantics
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

#define GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS() \
  idx, err_id, err_msg

#define GLOBAL_SEMANTICS_RESPONSE_UDPF_PACKER_ARGS() \
  GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS(), ret, args

#define GLOBAL_SEMANTICS_RESPONSE_SERIAL_PACKER_ARGS() \
  GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS()

#define GLOBAL_SEMANTICS_RESPONSE_COLUMN_PACKER_ARGS() \
  GLOBAL_SEMANTICS_RESPONSE_COMMON_PACKER_ARGS(), c_info

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

  void
  global_semantics_response::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_bigint (qs.size ());

    for (const auto &res : qs)
      {
	(*res).pack (serializator);
      }
  }

  size_t
  global_semantics_response::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_bigint_size (start_offset);	// size

    for (const auto &res : qs)
      {
	size += (*res).get_packed_size (serializator, size);
      }

    return size;
  }

  void
  global_semantics_response::unpack (cubpacking::unpacker &deserializator)
  {
    //
    assert (false);
  }
}