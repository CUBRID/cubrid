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

#include "method_struct_value.hpp"

#include "dbtype.h"
#include "db_date.h"
#include "numeric_opfunc.h" /* numeric_db_value_print() */
#include "oid.h" /* oid_Null_oid */
#include "set_object.h"
#include "object_representation.h" /* db_string_put_cs_and_collation() */
#include "object_primitive.h"
#include "language_support.h" /* lang_* () */

#include "memory_private_allocator.hpp" /* cubmem::PRIVATE_BLOCK_ALLOCATOR */

#if !defined (SERVER_MODE)
#include "work_space.h" /* WS_OID */
#endif

#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// DB_VALUE
//////////////////////////////////////////////////////////////////////////

  dbvalue_java::dbvalue_java ()
    : value (nullptr)
  {
  }

  void
  dbvalue_java::pack_value_internal (cubpacking::packer &serializator, DB_VALUE &v) const
  {
    int param_type = DB_VALUE_TYPE (&v);
    serializator.pack_int (param_type);

    switch (param_type)
      {
      case DB_TYPE_INTEGER:
	serializator.pack_int (db_get_int (&v));
	break;

      case DB_TYPE_SHORT:
	serializator.pack_short (db_get_short (&v));
	break;

      case DB_TYPE_BIGINT:
	serializator.pack_bigint (db_get_bigint (&v));
	break;

      case DB_TYPE_FLOAT:
      {
	// FIXME: update packer
	// serializator.pack_float (db_get_float (&v));
	float f = db_get_float (&v);
	OR_BUF or_buf;
	serializator.delegate_to_or_buf (OR_FLOAT_SIZE, or_buf);
	OR_PUT_FLOAT (or_buf.ptr, f);
      }
      break;

      case DB_TYPE_DOUBLE:
      {
	// FIXME: update packer
	// serializator.pack_double (db_get_double (&v));
	double d = db_get_double (&v);
	OR_BUF or_buf;
	serializator.delegate_to_or_buf (OR_DOUBLE_SIZE, or_buf);
	OR_PUT_DOUBLE (or_buf.ptr, d);
      }
      break;

      case DB_TYPE_MONETARY:
      {
	DB_MONETARY *money = db_get_monetary (&v);

	OR_BUF or_buf;
	serializator.delegate_to_or_buf (OR_DOUBLE_SIZE, or_buf);
	OR_PUT_DOUBLE (or_buf.ptr, money->amount);

	// FIXME: update packer
	// serializator.pack_double (money->amount);
      }
      break;

      case DB_TYPE_NUMERIC:
      {
	char str_buf[NUMERIC_MAX_STRING_SIZE];
	numeric_db_value_print (&v, str_buf);
	serializator.pack_c_string (str_buf, strlen (str_buf));
      }
      break;

      case DB_TYPE_CHAR:
      case DB_TYPE_NCHAR:
      case DB_TYPE_VARNCHAR:
      case DB_TYPE_STRING:
	// TODO: support unicode decomposed string
      {
	serializator.pack_c_string (db_get_string (&v), db_get_string_size (&v));
      }
      break;

      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
	// NOTE: This type was not implemented at the previous version
	assert (false);
	break;

      case DB_TYPE_DATE:
      {
	int year, month, day;
	db_date_decode (db_get_date (&v), &month, &day, &year);
	serializator.pack_int (year);
	serializator.pack_int (month - 1);
	serializator.pack_int (day);
      }
      break;

      case DB_TYPE_TIME:
      {
	int hour, min, sec;
	db_time_decode (db_get_time (&v), &hour, &min, &sec);
	serializator.pack_int (hour);
	serializator.pack_int (min);
	serializator.pack_int (sec);
      }
      break;

      case DB_TYPE_TIMESTAMP:
      {
	int year, month, day, hour, min, sec;
	DB_TIMESTAMP *timestamp = db_get_timestamp (&v);
	DB_DATE date;
	DB_TIME time;
	(void) db_timestamp_decode_ses (timestamp, &date, &time);
	db_date_decode (&date, &month, &day, &year);
	db_time_decode (&time, &hour, &min, &sec);

	serializator.pack_int (year);
	serializator.pack_int (month - 1);
	serializator.pack_int (day);
	serializator.pack_int (hour);
	serializator.pack_int (min);
	serializator.pack_int (sec);
      }
      break;

      case DB_TYPE_DATETIME:
      {
	int year, month, day, hour, min, sec, msec;
	DB_DATETIME *datetime = db_get_datetime (&v);
	db_datetime_decode (datetime, &month, &day, &year, &hour, &min, &sec, &msec);

	serializator.pack_int (year);
	serializator.pack_int (month - 1);
	serializator.pack_int (day);
	serializator.pack_int (hour);
	serializator.pack_int (min);
	serializator.pack_int (sec);
	serializator.pack_int (msec);
      }
      break;

      case DB_TYPE_SET:
      case DB_TYPE_MULTISET:
      case DB_TYPE_SEQUENCE:
      {
	DB_SET *set = db_get_set (&v);
	int ncol = set_size (set);

	DB_VALUE elem_v;

	serializator.pack_int (ncol);
	for (int i = 0; i < ncol; i++)
	  {
	    if (set_get_element (set, i, &elem_v) != NO_ERROR) /* TODO: set_get_element_nocopy () */
	      {
		assert (false);
		break;
	      }

	    pack_value_internal (serializator, elem_v);
	    db_value_clear (&elem_v);
	  }
      }
      break;

      case DB_TYPE_OID:
      {
	OID *oid = db_get_oid (&v);
	serializator.pack_oid (*oid);
      }
      break;

      case DB_TYPE_OBJECT:
      {
#if !defined (SERVER_MODE)
	OID *oid = (OID *) (&oid_Null_oid);
	MOP mop = db_get_object (&v);
	if (mop != NULL)
	  {
	    oid = WS_OID (mop);
	  }
	serializator.pack_oid (*oid);
#else
	// TODO: Implement a way to pack DB_TYPE_OBJECT value on Server
	assert (false);
#endif
      }
      break;

      case DB_TYPE_RESULTSET:
      {
	DB_RESULTSET rs = db_get_resultset (&v);
	serializator.pack_bigint (rs);
      }
      break;

      case DB_TYPE_NULL:
	break;

      default:
	assert (false);
	break;
      }
  }

  size_t
  dbvalue_java::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    if (value == nullptr)
      {
	return 0;
      }

    return get_packed_value_size_internal (serializator, start_offset, *value);
  }

  void
  dbvalue_java::pack (cubpacking::packer &serializator) const
  {
    if (value == nullptr)
      {
	return;
      }

    pack_value_internal (serializator, *value);
  }

  size_t
  dbvalue_java::get_packed_value_size_internal (cubpacking::packer &serializator, std::size_t start_offset,
      DB_VALUE &v) const
  {
    DB_TYPE type = DB_VALUE_TYPE (&v);

    size_t size = serializator.get_packed_int_size (start_offset); /* type */
    switch (type)
      {
      case DB_TYPE_INTEGER:

	size += serializator.get_packed_int_size (size);
	break;

      case DB_TYPE_SHORT:

	size += serializator.get_packed_short_size (size);
	break;

      case DB_TYPE_BIGINT:

	size += serializator.get_packed_bigint_size (size);
	break;

      case DB_TYPE_FLOAT:

	size += OR_FLOAT_SIZE;

	// FIXME: no alignment ?
	// size += DB_ALIGN (size, OR_FLOAT_SIZE) - size + OR_FLOAT_SIZE;

	// FIXME: update packer
	// size += serializator.get_packed_float_size (size);
	break;

      case DB_TYPE_DOUBLE:
      case DB_TYPE_MONETARY:

	size += OR_DOUBLE_SIZE;

	// FIXME: no alignment
	// size += DB_ALIGN (size, OR_DOUBLE_SIZE) - size + OR_DOUBLE_SIZE;

	// assert (false);
	// FIXME: update packer
	// size += serializator.get_packed_double_size (size);
	break;

      case DB_TYPE_NUMERIC:
      {
	char str_buf[NUMERIC_MAX_STRING_SIZE];
	numeric_db_value_print (value, str_buf);
	size += serializator.get_packed_int_size (size); /* dummy length */
	size += serializator.get_packed_c_string_size (str_buf, strlen (str_buf), size);
      }
      break;

      case DB_TYPE_CHAR:
      case DB_TYPE_NCHAR:
      case DB_TYPE_VARNCHAR:
      case DB_TYPE_STRING:
      {
	size += serializator.get_packed_int_size (size); /* dummy size */
	size += serializator.get_packed_c_string_size (db_get_string (value), db_get_string_size (value), size);
      }
      break;

      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
	// NOTE: This type was not implemented at the previous version
	assert (false);
	break;

      case DB_TYPE_OID:
      {

	size += serializator.get_packed_oid_size (size);
      }
      break;

      case DB_TYPE_OBJECT:
#if !defined (SERVER_MODE)

	size += serializator.get_packed_oid_size (size);
#else
	// TODO: Implement a way to pack DB_TYPE_OBJECT value on Server
	assert (false);
#endif
	break;

      case DB_TYPE_DATE:
      case DB_TYPE_TIME:

	size += serializator.get_packed_int_size (size); /* hour */
	size += serializator.get_packed_int_size (size); /* min */
	size += serializator.get_packed_int_size (size); /* sec */
	break;

      case DB_TYPE_TIMESTAMP:

	size += serializator.get_packed_int_size (size); /* year */
	size += serializator.get_packed_int_size (size); /* month */
	size += serializator.get_packed_int_size (size); /* day */
	size += serializator.get_packed_int_size (size); /* hour */
	size += serializator.get_packed_int_size (size); /* min */
	size += serializator.get_packed_int_size (size); /* sec */
	break;

      case DB_TYPE_DATETIME:

	size += serializator.get_packed_int_size (size); /* year */
	size += serializator.get_packed_int_size (size); /* month */
	size += serializator.get_packed_int_size (size); /* day */
	size += serializator.get_packed_int_size (size); /* hour */
	size += serializator.get_packed_int_size (size); /* min */
	size += serializator.get_packed_int_size (size); /* sec */
	size += serializator.get_packed_int_size (size); /* msec */
	break;

      case DB_TYPE_SET:
      case DB_TYPE_MULTISET:
      case DB_TYPE_SEQUENCE:
      {
	DB_SET *set = db_get_set (&v);
	int ncol = set_size (set);
	DB_VALUE elem_v;


	size += serializator.get_packed_int_size (size); /* ncol */

	for (int i = 0; i < ncol; i++)
	  {
	    if (set_get_element (set, i, &elem_v) != NO_ERROR)
	      {
		assert (false);
		break;
	      }

	    size += get_packed_value_size_internal (serializator, size, elem_v);
	    db_value_clear (&elem_v);
	  }
      }
      break;

      case DB_TYPE_RESULTSET:
	size += serializator.get_packed_bigint_size (size); /* DB_RESULTSET */
	break;

      case DB_TYPE_NULL:
	break;
      default:
	assert (false);
	break;
      }

    return size;
  }

  void
  dbvalue_java::unpack (cubpacking::unpacker &deserializator)
  {
    if (value == nullptr)
      {
	return;
      }

    unpack_value_interanl (deserializator, value);
  }

  void
  dbvalue_java::unpack_value_interanl (cubpacking::unpacker &deserializator, DB_VALUE *v)
  {
    int type;
    deserializator.unpack_int (type);

    switch (type)
      {
      case DB_TYPE_INTEGER:
      {
	int i;
	deserializator.unpack_int (i);
	db_make_int (v, i);
      }
      break;

      case DB_TYPE_BIGINT:
      {
	DB_BIGINT bi;
	deserializator.unpack_bigint (bi);
	db_make_bigint (v, bi);
      }
      break;

      case DB_TYPE_SHORT:
      {
	short s;
	deserializator.unpack_short (s);
	db_make_short (v, s);
      }
      break;

      case DB_TYPE_FLOAT:
      {
	float f;
	// FIXME: update packer
	// deserializator.unpack_float (f);
	OR_BUF or_buf;
	deserializator.delegate_to_or_buf (OR_FLOAT_SIZE, or_buf);
	OR_GET_FLOAT (or_buf.ptr, &f);
	db_make_float (v, f);
      }
      break;

      case DB_TYPE_DOUBLE:
      {
	double d;
	// FIXME: update packer
	// deserializator.unpack_double (d);
	OR_BUF or_buf;
	deserializator.delegate_to_or_buf (OR_DOUBLE_SIZE, or_buf);
	OR_GET_DOUBLE (or_buf.ptr, &d);
	db_make_double (v, d);
	// db_make_double (&v, d);
      }
      break;

      case DB_TYPE_NUMERIC:
      {
	cubmem::extensible_block blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
	deserializator.unpack_string_to_memblock (blk);

	INTL_CODESET codeset;
#if !defined (SERVER_MODE)
	codeset = lang_get_client_charset ();
#else
	codeset = LANG_SYS_CODESET;
#endif
	if (numeric_coerce_string_to_num (blk.get_ptr (), strlen (blk.get_ptr ()), codeset, v) != NO_ERROR)
	  {
	    // TODO: needs error handling?
	    assert (false);
	  }
      }
      break;

      case DB_TYPE_CHAR:
      case DB_TYPE_NCHAR:
      case DB_TYPE_VARNCHAR:
      case DB_TYPE_STRING:
      {
	cubmem::extensible_block blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
	deserializator.unpack_string_to_memblock (blk);

	// TODO: unicode compose hanlding

#if 0
	char *invalid_pos = NULL;
	int len = strlen (blk.get_ptr ());
	if (intl_check_string (blk.get_ptr (), len, &invalid_pos, lang_get_client_charset ()) != INTL_UTF8_VALID)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_CHAR, 1, invalid_pos - blk.get_ptr ());
	    return NULL;
	  }

	int composed_size;
	if (lang_get_client_charset () == INTL_CODESET_UTF8
	    && unicode_string_need_compose (blk.get_ptr (), len, &composed_size, lang_get_generic_unicode_norm ()))
	  {
	    cubmem::extensible_block blk_composed { cubmem::PRIVATE_BLOCK_ALLOCATOR };;
	    bool is_composed = false;

	    blk_composed.extend_to (composed_size + 1);

	    unicode_compose_string (blk.get_ptr (), len, blk_composed.get_ptr (), &composed_size, &is_composed,
				    lang_get_generic_unicode_norm ());
	    blk_composed.get_ptr ()[composed_size] = '\0';

	    assert (composed_size <= len);

	    if (is_composed)
	      {
		db_make_string (&v, blk_composed.get_ptr ());
	      }
	    else
	      {
		db_make_string (&v, blk.get_ptr ());
	      }
	  }
#endif
	db_make_string (v, blk.release_ptr ());

	INTL_CODESET codeset;
	int collation;
#if !defined (SERVER_MODE)
	codeset = lang_get_client_charset ();
	collation = lang_get_client_collation ();
#else
	codeset = LANG_SYS_CODESET;
	collation = LANG_SYS_COLLATION;
#endif
	db_string_put_cs_and_collation (v, codeset, collation);
	v->need_clear = true;
      }
      break;

      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
	// NOTE: This type was not implemented at the previous version
	assert (false);
	break;

      case DB_TYPE_DATE:
      {
	DB_DATE date;
	cubmem::extensible_block blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
	deserializator.unpack_string_to_memblock (blk);
	if (db_string_to_date (blk.get_ptr (), &date) != NO_ERROR)
	  {
	    assert (false);
	    return;
	  }
	else
	  {
	    db_value_put_encoded_date (v, &date);
	  }
      }
      break;

      case DB_TYPE_TIME:
      {
	DB_TIME time;
	cubmem::extensible_block blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
	deserializator.unpack_string_to_memblock (blk);
	if (db_string_to_time (blk.get_ptr (), &time) != NO_ERROR)
	  {
	    assert (false);
	    return;
	  }
	else
	  {
	    db_value_put_encoded_time (v, &time);
	  }
      }
      break;

      case DB_TYPE_TIMESTAMP:
      {
	DB_TIMESTAMP timestamp;
	cubmem::extensible_block blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
	deserializator.unpack_string_to_memblock (blk);
	if (db_string_to_timestamp (blk.get_ptr (), &timestamp) != NO_ERROR)
	  {
	    assert (false);
	    return;
	  }
	else
	  {
	    db_make_timestamp (v, timestamp);
	  }
      }
      break;

      case DB_TYPE_DATETIME:
      {
	DB_DATETIME datetime;
	cubmem::extensible_block blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
	deserializator.unpack_string_to_memblock (blk);
	if (db_string_to_datetime (blk.get_ptr (), &datetime) != NO_ERROR)
	  {
	    assert (false);
	    return;
	  }
	else
	  {
	    db_make_datetime (v, &datetime);
	  }
      }
      break;

      case DB_TYPE_SET:
      case DB_TYPE_MULTISET:
      case DB_TYPE_SEQUENCE:
      {
	int ncol;
	deserializator.unpack_int (ncol);

	DB_SET *set = set_create ((DB_TYPE) type, ncol);
	DB_VALUE elem;
	for (int i = 0; i < ncol; i++)
	  {
	    unpack_value_interanl (deserializator, &elem);
	    if (set_add_element (set, &elem) != NO_ERROR)
	      {
		// FIXME: error handling
		set_free (set);
		assert (false);
		break;
	      }
	    pr_clear_value (&elem);
	  }
	if (type == DB_TYPE_SET)
	  {
	    db_make_set (v, set);
	  }
	else if (type == DB_TYPE_MULTISET)
	  {
	    db_make_multiset (v, set);
	  }
	else if (type == DB_TYPE_SEQUENCE)
	  {
	    db_make_sequence (v, set);
	  }
      }
      break;

      case DB_TYPE_OID:
      {
	OID oid;
	deserializator.unpack_oid (oid);
	db_make_oid (v, &oid);
      }
      break;

      case DB_TYPE_OBJECT:
      {
	OID oid;
	deserializator.unpack_oid (oid);
#if !defined (SERVER_MODE)
	MOP obj = ws_mop (&oid, NULL);
	db_make_object (v, obj);
#else
	// TODO: Implement a way to pack DB_TYPE_OBJECT value on Server?
	db_make_oid (v, &oid);
#endif
      }
      break;

      case DB_TYPE_MONETARY:
      {
	double monetary;

	// FIXME: update packer
	// deserializator.unpack_double (monetary);

	OR_BUF or_buf;
	deserializator.delegate_to_or_buf (OR_DOUBLE_SIZE, or_buf);
	OR_GET_DOUBLE (or_buf.ptr, &monetary);
	if (db_make_monetary (v, DB_CURRENCY_DEFAULT, monetary) != NO_ERROR)
	  {
	    // FIXME: error handling
	    assert (false);
	  }
      }
      break;

      case DB_TYPE_RESULTSET:
      {
	uint64_t val;
	deserializator.unpack_bigint (val);
	db_make_resultset (v, val);
      }
      break;

      case DB_TYPE_NULL:
      default:
	db_make_null (v);
	break;
      }
  }
}
