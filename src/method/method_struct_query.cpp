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

#include "method_struct_query.hpp"

#include "dbtype.h"
#include "language_support.h" /* lang_charset(); */
#include "method_struct_invoke.hpp"
#include "method_struct_value.hpp"
#include "method_query_util.hpp"
#if defined (CS_MODE)
#include "method_schema_info.hpp"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
  column_info::column_info ()
  {
    // default constructor
    db_type = DB_TYPE_NULL;
    set_type = DB_TYPE_NULL;

    scale = 0;
    prec = 0;
    charset = lang_charset();
    is_non_null = 0;

    /* col_name, attr_name, class_name, default_value leave as empty */
    // FIXME: to remove warning
    col_name.clear ();
    attr_name.clear ();
    class_name.clear ();
    default_value_string.clear();

    auto_increment = 0;
    unique_key = 0;
    primary_key = 0;
    reverse_index = 0;
    reverse_unique = 0;
    foreign_key = 0;
    shared = 0;
  }

  column_info::column_info (int db_type, int set_type, short scale, int prec, char charset,
			    std::string col_name)
  {
    this->db_type = db_type;
    this->set_type = set_type;

    this->scale = scale;
    this->prec = prec;
    this->charset = charset;

    this->col_name.assign (col_name);
    str_trim (this->col_name);

    attr_name.clear ();
    class_name.clear ();
    default_value_string.clear();

    auto_increment = 0;
    unique_key = 0;
    primary_key = 0;
    reverse_index = 0;
    reverse_unique = 0;
    foreign_key = 0;
    shared = 0;
  }

  column_info::column_info (int db_type, int set_type, short scale, int prec, char charset,
			    std::string col_name, std::string default_value, char auto_increment,
			    char unique_key, char primary_key, char reverse_index, char reverse_unique,
			    char foreign_key, char shared, std::string attr_name, std::string class_name,
			    char is_non_null)
  {
    this->db_type = db_type;
    this->set_type = set_type;

    this->scale = scale;
    this->prec = prec;
    this->charset = charset;

    this->col_name.assign (col_name);
    str_trim (this->col_name);

    this->attr_name.assign (attr_name);
    this->class_name.assign (class_name);
    this->default_value_string.assign (default_value);

    if (is_non_null >= 1)
      {
	this->is_non_null = 1;
      }
    else if (is_non_null < 0)
      {
	this->is_non_null = 0;
      }

    this->auto_increment = auto_increment;
    this->unique_key = unique_key;
    this->primary_key = primary_key;
    this->reverse_index = reverse_index;
    this->reverse_unique = reverse_unique;
    this->foreign_key = foreign_key;
    this->shared = shared;
  }

  void
  column_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (db_type);
    serializator.pack_int (set_type);

    serializator.pack_int (charset);
    serializator.pack_short (scale);
    serializator.pack_int (prec);
    serializator.pack_string (col_name);
    serializator.pack_string (attr_name);
    serializator.pack_string (class_name);
    serializator.pack_string (default_value_string);

    serializator.pack_int (is_non_null);
    serializator.pack_int (auto_increment);
    serializator.pack_int (unique_key);
    serializator.pack_int (primary_key);
    serializator.pack_int (reverse_index);
    serializator.pack_int (reverse_unique);
    serializator.pack_int (foreign_key);
    serializator.pack_int (shared);
  }

  void
  column_info::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (db_type);
    deserializator.unpack_int (set_type);

    int cs, p, inn;
    deserializator.unpack_int (cs);
    deserializator.unpack_short (scale);
    deserializator.unpack_int (p);

    deserializator.unpack_string (col_name);
    deserializator.unpack_string (attr_name);
    deserializator.unpack_string (class_name);
    deserializator.unpack_string (default_value_string);

    deserializator.unpack_int (inn);

    charset = (char) cs;
    prec = (char) p;
    is_non_null = (char) inn;

    int a, u, pk, ri, ru, f, s;
    deserializator.unpack_int (a);
    deserializator.unpack_int (u);
    deserializator.unpack_int (pk);
    deserializator.unpack_int (ri);
    deserializator.unpack_int (ru);
    deserializator.unpack_int (f);
    deserializator.unpack_int (s);

    auto_increment = (char) a;
    unique_key = (char) u;
    primary_key = (char) pk;
    reverse_index = (char) ri;
    reverse_unique = (char) ru;
    foreign_key = (char) f;
    shared = (char) s;
  }

  size_t
  column_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // db_type
    size += serializator.get_packed_int_size (size); // set_type

    size += serializator.get_packed_int_size (size); // charset
    size += serializator.get_packed_short_size (size); // scale
    size += serializator.get_packed_int_size (size); // prec

    size += serializator.get_packed_string_size (col_name, size); // col_name
    size += serializator.get_packed_string_size (attr_name, size); // attr_name
    size += serializator.get_packed_string_size (class_name, size); // class_name
    size += serializator.get_packed_string_size (default_value_string, size); // default_value_string

    size += serializator.get_packed_int_size (size); // is_non_null
    size += serializator.get_packed_int_size (size); // auto_increment
    size += serializator.get_packed_int_size (size); // unique_key
    size += serializator.get_packed_int_size (size); // primary_key
    size += serializator.get_packed_int_size (size); // reverse_index
    size += serializator.get_packed_int_size (size); // reverse_unique
    size += serializator.get_packed_int_size (size); // foreign_key
    size += serializator.get_packed_int_size (size); // shared
    return size;
  }

  void
  column_info::dump ()
  {
    fprintf (stdout, "class_name: %s\n", class_name.c_str());
    fprintf (stdout, "attr_name: %s\n", attr_name.c_str());
    fprintf (stdout, "col_name: %s\n", col_name.c_str());
    fprintf (stdout, "default_value_string: %s\n", default_value_string.c_str());

    fprintf (stdout, "scale: %d\n", scale);
    fprintf (stdout, "prec: %d\n", prec);

    fprintf (stdout, "charset: %d\n", charset);
    fprintf (stdout, "is_non_null: %d\n", is_non_null);


    fprintf (stdout, "auto_increment: %d\n", auto_increment);
    fprintf (stdout, "unique_key: %d\n", unique_key);
    fprintf (stdout, "primary_key: %d\n", primary_key);
    fprintf (stdout, "reverse_index: %d\n", reverse_index);
    fprintf (stdout, "reverse_unique: %d\n", reverse_unique);
    fprintf (stdout, "foreign_key: %d\n", foreign_key);
    fprintf (stdout, "shared: %d\n", shared);
  }

  prepare_info::prepare_info ()
    : handle_id (-1),
      stmt_type (-1),
      num_markers (0)
  {
    //
  }

  void
  prepare_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (handle_id);
    serializator.pack_int (stmt_type);
    serializator.pack_int (num_markers);
    serializator.pack_int (column_infos.size());
    for (int i = 0; i < (int) column_infos.size(); i++)
      {
	column_infos[i].pack (serializator);
      }
  }

  void
  prepare_info::unpack (cubpacking::unpacker &deserializator)
  {
    int num_column_info;
    deserializator.unpack_int (handle_id);
    deserializator.unpack_int (stmt_type);
    deserializator.unpack_int (num_markers);
    deserializator.unpack_int (num_column_info);

    if (num_column_info > 0)
      {
	column_infos.resize (num_column_info);
	for (int i = 0; i < (int) num_column_info; i++)
	  {
	    column_infos[i].unpack (deserializator);
	  }
      }
  }

  size_t
  prepare_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // handle_id
    size += serializator.get_packed_int_size (size); // stmt_type
    size += serializator.get_packed_int_size (size); // num_markers
    size += serializator.get_packed_int_size (size); // num_columns

    if (column_infos.size() > 0)
      {
	for (int i = 0; i < (int) column_infos.size(); i++)
	  {
	    size += column_infos[i].get_packed_size (serializator, size);
	  }
      }
    return size;
  }

  void
  prepare_info::dump ()
  {
    fprintf (stdout, "handler_id: %d\n", handle_id);
    fprintf (stdout, "stmt_type: %d\n", stmt_type);
    fprintf (stdout, "num_markers: %d\n", num_markers);
    fprintf (stdout, "column_infos.size(): %d\n", (int) column_infos.size());
    fprintf (stdout, "==============================\n");
    for (int i = 0; i < (int) column_infos.size(); i++)
      {
	fprintf (stdout, "=> column_infos[%d]:\n", i);
	column_infos[i].dump();
      }
    fprintf (stdout, "==============================\n");
  }

  prepare_call_info::prepare_call_info ()
  {
    num_args = 0;
    is_first_out = false;
  }

  prepare_call_info::~prepare_call_info ()
  {
    clear ();
  }

  int
  prepare_call_info::set_is_first_out (std::string &sql_stmt)
  {
    if (!sql_stmt.empty() && sql_stmt[0] == '?')
      {
	is_first_out = true;

	std::size_t found = sql_stmt.find ('=');
	/* '=' is not found */
	if (found == std::string::npos)
	  {
	    return ER_FAILED;
	  }

	sql_stmt = sql_stmt.substr (found + 1);
      }

    return NO_ERROR;
  }

  void
  prepare_call_info::clear ()
  {
    int i = 0;
    if (is_first_out)
      {
	db_value_clear (&dbval_args[0]);
	i++;
      }

    while (i < (int) dbval_args.size())
      {
	db_make_null (&dbval_args[i]);
	i++;
      }
  }

  int
  prepare_call_info::set_prepare_call_info (int num_args)
  {
    this->num_args = num_args;
    if (num_args > 0)
      {
	param_modes.resize (num_args);
	for (int i = 0; i < (int) param_modes.size(); i++)
	  {
	    param_modes[i] = 0;
	  }

	dbval_args.resize (num_args + 1);
	for (int i = 0; i < (int) dbval_args.size(); i++)
	  {
	    db_make_null (&dbval_args[i]);
	  }
      }

    return NO_ERROR;
  }

  void
  prepare_call_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (num_args);
    serializator.pack_bool (is_first_out);

    dbvalue_java sp_val;
    for (int i = 0; i < (int) dbval_args.size(); i++)
      {
	sp_val.value = (DB_VALUE *) &dbval_args[i];
	sp_val.pack (serializator);
      }

    for (int i = 0; i < (int) param_modes.size(); i++)
      {
	serializator.pack_int (param_modes[i]);
      }
  }

  void
  prepare_call_info::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (num_args);
    deserializator.unpack_bool (is_first_out);

    dbval_args.resize (is_first_out ? num_args + 1 : num_args);
    param_modes.resize (num_args);

    dbvalue_java sp_val;
    for (int i = 0; i < (int) dbval_args.size (); i++)
      {
	sp_val.value = &dbval_args[i];
	sp_val.unpack (deserializator);
      }

    for (int i = 0; i < (int) param_modes.size (); i++)
      {
	deserializator.unpack_int (param_modes[i]);
      }
  }

  size_t
  prepare_call_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // num_args
    size += serializator.get_packed_bool_size (size); // is_first_out

    dbvalue_java sp_val;
    for (int i = 0; i < (int) dbval_args.size(); i++)
      {
	sp_val.value = (DB_VALUE *) &dbval_args[i];
	size += sp_val.get_packed_size (serializator, size); // dbval_args[i]
      }

    for (int i = 0; i < (int) param_modes.size(); i++)
      {
	size += serializator.get_packed_int_size (size); // param_modes[i]
      }

    return size;
  }

  query_result_info::query_result_info ()
  {
    //
    query_id = NULL_QUERY_ID;
    stmt_type = -1;
    tuple_count = 0;
    ins_oid.pageid = 0;
    ins_oid.slotid = 0;
    ins_oid.volid = 0;
    include_oid = false;
  }

  void
  query_result_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (stmt_type);
    serializator.pack_int (tuple_count);
    serializator.pack_oid (ins_oid);
    serializator.pack_bool (include_oid);
    serializator.pack_bigint (query_id);
  }

  void
  query_result_info::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (stmt_type);
    deserializator.unpack_int (tuple_count);
    deserializator.unpack_oid (ins_oid);
    deserializator.unpack_bool (include_oid);

    uint64_t qid;
    deserializator.unpack_bigint (qid);
    query_id = (QUERY_ID) qid;
  }

  size_t
  query_result_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // stmt_type
    size += serializator.get_packed_int_size (size); // tuple_count
    size += serializator.get_packed_oid_size (size); // ins_oid
    size += serializator.get_packed_bool_size (size); // include_oid
    size += serializator.get_packed_bigint_size (size); // query_id
    return size;
  }

  void
  query_result_info::dump ()
  {
    fprintf (stdout, "stmt_type: %d\n", stmt_type);
    fprintf (stdout, "tuple_count: %d\n", tuple_count);
    fprintf (stdout, "ins_oid (%d, %d, %d)\n", ins_oid.pageid, ins_oid.slotid, ins_oid.volid);
    fprintf (stdout, "include_oid: %d\n", include_oid);
    fprintf (stdout, "query_id: %lld\n", query_id);
  }

  void
  execute_request::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (handler_id);
    serializator.pack_int (execute_flag);
    serializator.pack_int (max_field);
    serializator.pack_int (is_forward_only);
    serializator.pack_int (has_parameter);

    if (has_parameter > 0)
      {
	serializator.pack_int (param_values.size());

	dbvalue_java sp_val;
	for (int i = 0; i < (int) param_values.size(); i++)
	  {
	    if (has_parameter == 2)
	      {
		sp_val.value = (DB_VALUE *) &param_values[i];
		sp_val.pack (serializator);
	      }
	    else if (has_parameter == 1)
	      {
		serializator.pack_db_value (param_values[i]);
	      }
	    serializator.pack_int (param_modes[i]);
	  }
      }
  }

  execute_request::~execute_request()
  {
    clear ();
  }

  void
  execute_request::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (handler_id);
    deserializator.unpack_int (execute_flag);
    deserializator.unpack_int (max_field);
    deserializator.unpack_int (is_forward_only);
    deserializator.unpack_int (has_parameter);

    int parameter_cnt;
    if (has_parameter > 0)
      {
	deserializator.unpack_int (parameter_cnt);

	param_values.resize (parameter_cnt);
	param_modes.resize (parameter_cnt);

	dbvalue_java sp_val;
	for (int i = 0; i < parameter_cnt; i++)
	  {
	    if (has_parameter == 2)
	      {
		sp_val.value = &param_values[i];
		sp_val.unpack (deserializator);
	      }
	    else if (has_parameter == 1)
	      {
		deserializator.unpack_db_value (param_values[i]);
	      }
	    deserializator.unpack_int (param_modes[i]);
	  }
      }
  }

  size_t
  execute_request::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // handler_id
    size += serializator.get_packed_int_size (size); // execute_flag
    size += serializator.get_packed_int_size (size); // max_field
    size += serializator.get_packed_int_size (size); // is_forward_only
    size += serializator.get_packed_int_size (size); // has_parameter

    if (has_parameter > 0)
      {
	size += serializator.get_packed_int_size (size); // param_values.size()
	dbvalue_java sp_val;

	for (int i = 0; i < (int) param_values.size(); i++)
	  {
	    if (has_parameter == 2)
	      {
		sp_val.value = (DB_VALUE *) &param_values[i];
		size += sp_val.get_packed_size (serializator, size);
	      }
	    else if (has_parameter == 1)
	      {
		size += serializator.get_packed_db_value_size (param_values[i], size);
	      }
	    size += serializator.get_packed_int_size (size);
	  }
      }

    return size;
  }

  void
  execute_request::clear ()
  {
    for (int i = 0; i < (int) param_values.size(); i++)
      {
	db_value_clear (&param_values[i]);
      }
  }

  execute_info::execute_info ()
  {
    num_affected = 0;
    stmt_type = CUBRID_STMT_NONE;
    num_markers = 0;
    call_info = nullptr;
  }

  execute_info::~execute_info ()
  {
    call_info = nullptr;
  }

  void
  execute_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (num_affected);
    qresult_info.pack (serializator);

    serializator.pack_int (column_infos.size());
    if (column_infos.size() > 0)
      {
	serializator.pack_int (stmt_type);
	serializator.pack_int (num_markers);
	for (int i = 0; i < (int) column_infos.size(); i++)
	  {
	    column_infos[i].pack (serializator);
	  }
      }

    if (call_info != nullptr)
      {
	serializator.pack_bool (true);
	call_info->pack (serializator);
      }
    else
      {
	serializator.pack_bool (false);
      }
  }

  void
  execute_info::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (num_affected);
    qresult_info.unpack (deserializator);

    int num_column_info;
    deserializator.unpack_int (num_column_info);
    if (num_column_info > 0)
      {
	deserializator.unpack_int (stmt_type);
	deserializator.unpack_int (num_markers);

	column_infos.resize (num_column_info);
	for (int i = 0; i < (int) num_column_info; i++)
	  {
	    column_infos[i].unpack (deserializator);
	  }
      }

    bool has_call_info = false;
    deserializator.unpack_bool (has_call_info);
    if (has_call_info)
      {
	call_info = new prepare_call_info (); // need to be freed;
      }
  }

  size_t
  execute_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // num_affected

    size += qresult_info.get_packed_size (serializator, size);

    size += serializator.get_packed_int_size (size); // num_columns
    if (column_infos.size() > 0)
      {
	size += serializator.get_packed_int_size (size); // stmt_type
	size += serializator.get_packed_int_size (size); // num_markers
	for (int i = 0; i < (int) column_infos.size(); i++)
	  {
	    size += column_infos[i].get_packed_size (serializator, size);
	  }
      }

    size += serializator.get_packed_bool_size (size); // has_prepare_info
    if (call_info != nullptr)
      {
	size += call_info->get_packed_size (serializator, size);
      }

    return size;
  }

  void
  execute_info::dump ()
  {
    fprintf (stdout, "handle_id: %d\n", handle_id);
    fprintf (stdout, "num_affected: %d\n", num_affected);

    fprintf (stdout, "==============================\n");
    fprintf (stdout, "=> qresult_info:\n");
    qresult_info.dump();
    fprintf (stdout, "==============================\n");

    fprintf (stdout, "column_infos.size(): %d\n", (int) column_infos.size());
    if (column_infos.size() > 0)
      {
	fprintf (stdout, "stmt_type: %d\n", stmt_type);
	fprintf (stdout, "num_markers: %d\n", num_markers);
	fprintf (stdout, "==============================\n");
	for (int i = 0; i < (int) column_infos.size(); i++)
	  {
	    fprintf (stdout, "=> column_infos[%d]:\n", i);
	    column_infos[i].dump();
	  }
	fprintf (stdout, "==============================\n");
      }
  }

  result_tuple_info::result_tuple_info ()
  {
    index = 0;
  }

  result_tuple_info::result_tuple_info (int idx, std::vector<DB_VALUE> &attr)
  {
    index = idx;
    attributes.resize (attr.size());
    for (int i = 0; i < (int) attributes.size(); i++)
      {
	attributes[i] = attr[i];
      }
    oid = OID_INITIALIZER;
  }

  result_tuple_info::result_tuple_info (int idx, std::vector<DB_VALUE> &attr, OID &oid_val)
    : result_tuple_info (idx, attr)
  {
    COPY_OID (&oid, &oid_val);
  }

  result_tuple_info::result_tuple_info (result_tuple_info &&other)
  {
    index = other.index;
    attributes = std::move (other.attributes);

    other.index = -1;
    other.attributes.clear ();
  }

  result_tuple_info::~result_tuple_info ()
  {
    for (int i = 0; i < (int) attributes.size(); i++)
      {
	db_value_clear (&attributes[i]);
      }
  }

  void
  result_tuple_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (index);
    serializator.pack_int (attributes.size());

    dbvalue_java sp_val;

    for (const DB_VALUE &attr : attributes)
      {
	sp_val.value = (DB_VALUE *) &attr;
	sp_val.pack (serializator);
      }

    serializator.pack_oid (oid);
  }

  void
  result_tuple_info::unpack (cubpacking::unpacker &deserializator)
  {
    int num_attributes;
    deserializator.unpack_int (index);
    deserializator.unpack_int (num_attributes);

    if (num_attributes > 0)
      {
	dbvalue_java sp_val;

	attributes.resize (num_attributes);
	for (int i = 0; i < num_attributes; i++)
	  {
	    sp_val.value = &attributes[i];
	    sp_val.unpack (deserializator);
	    attributes[i] = *sp_val.value;
	  }
      }

    deserializator.unpack_oid (oid);
  }

  size_t
  result_tuple_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // index
    size += serializator.get_packed_int_size (size); // num attributes

    dbvalue_java sp_val;
    for (const DB_VALUE &attr : attributes)
      {
	sp_val.value = (DB_VALUE *) &attr;
	size += sp_val.get_packed_size (serializator, size);
      }

    size += serializator.get_packed_oid_size (size); // oid
    return size;
  }

  void
  fetch_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (tuples.size ());
    for (int i = 0; i < (int) tuples.size(); i++)
      {
	tuples[i].pack (serializator);
      }
  }

  void
  fetch_info::unpack (cubpacking::unpacker &deserializator)
  {
    int num_fetched;
    deserializator.unpack_int (num_fetched);

    if (num_fetched > 0)
      {
	tuples.resize (num_fetched);
	for (int i = 0; i < num_fetched; i++)
	  {
	    tuples[i].unpack (deserializator);
	  }
      }
  }

  size_t
  fetch_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // num_fetched == tuples.size();
    for (int i = 0; i < (int) tuples.size(); i++)
      {
	size += tuples[i].get_packed_size (serializator, size);
      }
    return size;
  }

  void
  make_outresult_info::pack (cubpacking::packer &serializator) const
  {
    qresult_info.pack (serializator);

    serializator.pack_int (column_infos.size());
    if (column_infos.size() > 0)
      {
	for (int i = 0; i < (int) column_infos.size(); i++)
	  {
	    column_infos[i].pack (serializator);
	  }
      }
  }

  void
  make_outresult_info::unpack (cubpacking::unpacker &deserializator)
  {
    qresult_info.unpack (deserializator);

    int num_column_info;
    deserializator.unpack_int (num_column_info);
    if (num_column_info > 0)
      {
	column_infos.resize (num_column_info);
	for (int i = 0; i < (int) num_column_info; i++)
	  {
	    column_infos[i].unpack (deserializator);
	  }
      }
  }

  size_t
  make_outresult_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = qresult_info.get_packed_size (serializator, start_offset);

    size += serializator.get_packed_int_size (size); // num_columns
    if (column_infos.size() > 0)
      {
	for (int i = 0; i < (int) column_infos.size(); i++)
	  {
	    size += column_infos[i].get_packed_size (serializator, size);
	  }
      }

    return size;
  }

  void
  get_generated_keys_info::pack (cubpacking::packer &serializator) const
  {
    qresult_info.pack (serializator);

    serializator.pack_int (column_infos.size());
    if (column_infos.size() > 0)
      {
	for (int i = 0; i < (int) column_infos.size(); i++)
	  {
	    column_infos[i].pack (serializator);
	  }
      }

    generated_keys.pack (serializator);
  }

  void
  get_generated_keys_info::unpack (cubpacking::unpacker &deserializator)
  {
    qresult_info.unpack (deserializator);

    int num_column_info;
    deserializator.unpack_int (num_column_info);
    if (num_column_info > 0)
      {
	column_infos.resize (num_column_info);
	for (int i = 0; i < (int) num_column_info; i++)
	  {
	    column_infos[i].unpack (deserializator);
	  }
      }

    generated_keys.unpack (deserializator);
  }

  size_t
  get_generated_keys_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = qresult_info.get_packed_size (serializator, start_offset);

    size += serializator.get_packed_int_size (size); // num_columns
    if (column_infos.size() > 0)
      {
	for (int i = 0; i < (int) column_infos.size(); i++)
	  {
	    size += column_infos[i].get_packed_size (serializator, size);
	  }
      }

    size += generated_keys.get_packed_size (serializator, size);
    return size;
  }
}
