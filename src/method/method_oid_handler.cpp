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

#include "method_oid_handler.hpp"

#include <cstring>
#include <string>

#include "dbi.h"
#include "dbtype.h"
#include "method_struct_oid_info.hpp"
#include "method_query_util.hpp"
#include "object_primitive.h"
#include "oid.h"

namespace cubmethod
{
  oid_handler::~oid_handler ()
  {
    //
  }

//////////////////////////////////////////////////////////////////////////
// OID
//////////////////////////////////////////////////////////////////////////

  int
  oid_handler::check_object (DB_OBJECT *obj)
  {
    int error = NO_ERROR;

    if (obj == NULL)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_OBJECT, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    er_clear ();
    error = db_is_instance (obj);
    if (error < 0)
      {
	m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }
    else if (error > 0)
      {
	return 0;
      }

    error = db_error_code ();
    if (error < 0)
      {
	m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	return ER_FAILED;
      }

    m_error_ctx.set_error (METHOD_CALLBACK_ER_OBJECT, NULL, __FILE__, __LINE__);
    return ER_FAILED;
  }

  oid_get_info
  oid_handler::oid_get (OID &oid, std::vector<std::string> &attr_names)
  {
    int error = NO_ERROR;
    oid_get_info info;

    DB_OBJECT *obj = db_object (&oid);
    if (obj == NULL)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_OBJECT, NULL, __FILE__, __LINE__);
	return info;
      }

    error = check_object (obj);
    if (error < 0)
      {
	return info;
      }

    // set attribute names
    if (attr_names.empty ())
      {
	/* get attributes name */
	DB_ATTRIBUTE *attributes = db_get_attributes (obj);
	for (DB_ATTRIBUTE *att = attributes; att; att = db_attribute_next (att))
	  {
	    const char *name = db_attribute_name (att);
	    attr_names.emplace_back (name);
	  }
      }

    // set attribute column info
    for (std::string &attr_name : attr_names)
      {
	DB_ATTRIBUTE *attr = NULL;
	char *p = (char *) std::strrchr (attr_name.c_str (), '.');
	if (p == NULL)
	  {
	    attr = db_get_attribute (obj, attr_name.c_str ());
	    if (attr == NULL)
	      {
		m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
		return info;
	      }
	  }
	else
	  {
	    DB_VALUE path_val;
	    *p = '\0';
	    error = db_get (obj, attr_name.c_str (), &path_val);
	    if (error < 0 || db_value_is_null (&path_val) == true)
	      {
		attr = NULL;
	      }
	    else
	      {
		DB_OBJECT *path_obj = db_get_object (&path_val);
		attr = db_get_attribute (path_obj, p + 1);
		if (attr == NULL)
		  {
		    m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
		    return info;
		  }
	      }
	    *p = '.';
	    db_value_clear (&path_val);
	  }

	int db_type = DB_TYPE_NULL, set_type = DB_TYPE_NULL, precision = 0;
	short scale = 0;
	char charset = 0;
	if (attr != NULL)
	  {
	    DB_DOMAIN *domain = db_attribute_domain (attr);
	    db_type = TP_DOMAIN_TYPE (domain);
	    if (TP_IS_SET_TYPE (db_type))
	      {
		set_type = get_set_domain (domain, precision, scale, charset);
	      }
	    else
	      {
		set_type = DB_TYPE_NULL;
		precision = db_domain_precision (domain);
		scale = (short) db_domain_scale (domain);
		charset = db_domain_codeset (domain);
	      }
	  }

	info.column_infos.emplace_back (db_type, set_type, scale, precision, charset, attr_name);
      }

    // set class name
    const char *cname = db_get_class_name (obj);
    if (cname != NULL)
      {
	info.class_name.assign (cname);
      }

    // set attr names
    info.attr_names = std::move (attr_names);

    // set OID data
    DB_VALUE val;
    for (std::string &attr_name : info.attr_names)
      {
	if (db_get (obj, attr_name.c_str (), &val) < 0)
	  {
	    db_make_null (&val);
	  }
	info.db_values.push_back (val);
      }

    return info;
  }

  int
  oid_handler::oid_put (OID &oid, std::vector<std::string> &attr_names, std::vector<DB_VALUE> &attr_values)
  {
    int error = NO_ERROR;

    DB_OBJECT *obj = db_object (&oid);
    if (obj == NULL)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_OBJECT, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    error = check_object (obj);
    if (error < 0)
      {
	return ER_FAILED;
      }

    DB_OTMPL *otmpl = dbt_edit_object (obj);
    if (otmpl == NULL)
      {
	m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	return ER_FAILED;
      }

    assert (attr_names.size () == attr_values.size ());

    DB_VALUE *attr_val = NULL;
    int i = 0;

    for (int i = 0; i < (int) attr_names.size (); i++)
      {
	const char *attr_name = attr_names[i].c_str ();
	char attr_type = get_attr_type (obj, attr_name);
	attr_val = &attr_values[i];

	error = dbt_put (otmpl, attr_name, attr_val);
	if (error < 0)
	  {
	    m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	    db_value_clear (attr_val);
	    dbt_abort_object (otmpl);
	    return error;
	  }

	db_value_clear (attr_val);
      }

    obj = dbt_finish_object (otmpl);
    if (obj == NULL)
      {
	m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	return ER_FAILED;
      }

    return NO_ERROR;
  }

  int
  oid_handler::oid_cmd (OID &oid, int cmd, std::string &res)
  {
    int error = NO_ERROR;

    DB_OBJECT *obj = db_object (&oid);
    if (obj == NULL)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_OBJECT, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    if (cmd != OID_IS_INSTANCE)
      {
	error = check_object (obj);
	if (error < 0)
	  {
	    return ER_FAILED;
	  }
      }

    if (cmd == OID_DROP)
      {
	error = db_drop (obj);
      }
    else if (cmd == OID_IS_INSTANCE)
      {
	if (obj == NULL)
	  {
	    error = 0;
	  }
	else
	  {
	    er_clear ();
	    if (db_is_instance (obj) > 0)
	      {
		error = 1;
	      }
	    else
	      {
		error = db_error_code ();
		if (error == ER_HEAP_UNKNOWN_OBJECT)
		  {
		    error = 0;
		  }
	      }
	  }
      }
    else if (cmd == OID_LOCK_READ)
      {
	if (obj == NULL)
	  {
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_OBJECT, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
	error = db_lock_read (obj);
      }
    else if (cmd == OID_LOCK_WRITE)
      {
	if (obj == NULL)
	  {
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_OBJECT, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
	error = db_lock_write (obj);
      }
    else if (cmd == OID_CLASS_NAME)
      {
	if (obj == NULL)
	  {
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_OBJECT, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
	char *class_name = (char *) db_get_class_name (obj);
	if (class_name == NULL)
	  {
	    error = db_error_code ();
	    class_name = (char *) "";
	  }
	else
	  {
	    error = NO_ERROR;
	  }
	res.assign (class_name);
      }
    else
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    return error;
  }

//////////////////////////////////////////////////////////////////////////
// Collection
//////////////////////////////////////////////////////////////////////////

  int
  oid_handler::collection_cmd (OID &oid, int cmd, int seq_index, std::string &attr_name, DB_VALUE &elem_value)
  {
    int error = NO_ERROR;

    DB_OBJECT *obj = db_object (&oid);
    if (obj == NULL)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_OBJECT, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    error = check_object (obj);
    if (error < 0)
      {
	return ER_FAILED;
      }

    error = NO_ERROR;

    const char *name = attr_name.c_str ();
    DB_ATTRIBUTE *attr = db_get_attribute (obj, name);
    if (attr == NULL)
      {
	m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	return ER_FAILED;
      }

    DB_DOMAIN *domain = db_attribute_domain (attr);
    DB_TYPE type = TP_DOMAIN_TYPE (domain);
    if (type != DB_TYPE_SET && type != DB_TYPE_MULTISET && type != DB_TYPE_SEQUENCE)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_NOT_COLLECTION, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    int dummy1;
    short dummy2;
    char dummy3;
    DB_TYPE elem_type = (DB_TYPE) get_set_domain (domain, dummy1, dummy2, dummy3);
    DB_DOMAIN *elem_domain = db_domain_set (domain);
    if (elem_type <= 0)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_COLLECTION_DOMAIN, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    DB_VALUE val;
    error = db_get (obj, name, &val);
    if (error < 0)
      {
	m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    DB_COLLECTION *collection = NULL;
    if (db_value_type (&val) != DB_TYPE_NULL)
      {
	collection = db_get_collection (&val);
      }

    error = NO_ERROR; // reset error

    switch (cmd)
      {
      case COL_GET:
	/* TODO: not implemented at Server-Side JDBC */
	m_error_ctx.set_error (METHOD_CALLBACK_ER_NOT_IMPLEMENTED, NULL, __FILE__, __LINE__);
	error = ER_FAILED;
	break;
      case COL_SIZE:
	error = col_size (collection); // error is col_size
	break;
      case COL_SET_DROP:
	error = col_set_drop (collection, &elem_value);
	break;
      case COL_SET_ADD:
	error = col_set_add (collection, &elem_value);
	break;
      case COL_SEQ_DROP:
	error = col_seq_drop (collection, seq_index);
	break;
      case COL_SEQ_INSERT:
	error = col_seq_insert (collection, seq_index, &elem_value);
	break;
      case COL_SEQ_PUT:
	error = col_seq_put (collection, seq_index, &elem_value);
	break;
      default:
	assert (false); // invalid command
	error = ER_FAILED;
	break;
      }

    /* db_put */
    switch (cmd)
      {
      case COL_SET_DROP:
      case COL_SET_ADD:
      case COL_SEQ_DROP:
      case COL_SEQ_INSERT:
      case COL_SEQ_PUT:
	if (error >= 0)
	  {
	    db_put (obj, name, &val);
	  }
	break;
      default:
	/* do nothing */
	break;
      }

    db_col_free (collection);
    db_value_clear (&val);

    return error;
  }


  int
  oid_handler::col_set_drop (DB_COLLECTION *col, DB_VALUE *ele_val)
  {
    if (col != NULL)
      {
	int error = db_set_drop (col, ele_val);
	if (error < 0)
	  {
	    m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
      }
    return NO_ERROR;
  }

  int
  oid_handler::col_set_add (DB_COLLECTION *col, DB_VALUE *ele_val)
  {
    int error = NO_ERROR;

    if (col != NULL)
      {
	error = db_set_add (col, ele_val);
	if (error < 0)
	  {
	    m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
      }

    return NO_ERROR;
  }

  int
  oid_handler::col_seq_drop (DB_COLLECTION *col, int seq_index)
  {
    if (col != NULL)
      {
	int error = db_seq_drop (col, seq_index - 1);
	if (error < 0)
	  {
	    m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
      }
    return NO_ERROR;
  }

  int
  oid_handler::col_seq_insert (DB_COLLECTION *col, int seq_index, DB_VALUE *ele_val)
  {
    int error = NO_ERROR;

    if (col != NULL)
      {
	error = db_seq_insert (col, seq_index - 1, ele_val);
	if (error < 0)
	  {
	    m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
      }

    return NO_ERROR;
  }

  int
  oid_handler::col_seq_put (DB_COLLECTION *col, int seq_index, DB_VALUE *ele_val)
  {
    int error = NO_ERROR;

    if (col != NULL)
      {
	error = db_seq_put (col, seq_index - 1, ele_val);
	if (error < 0)
	  {
	    m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
      }

    return NO_ERROR;
  }

  int
  oid_handler::col_size (DB_COLLECTION *col)
  {
    int col_size;
    if (col == NULL)
      {
	col_size = -1;
      }
    else
      {
	col_size = db_col_size (col);
      }

    return col_size;
  }

//////////////////////////////////////////////////////////////////////////
// MISC
//////////////////////////////////////////////////////////////////////////

  char
  oid_handler::get_attr_type (DB_OBJECT *obj, const char *attr_name)
  {
    DB_ATTRIBUTE *attribute = db_get_attribute (obj, attr_name);
    if (attribute == NULL)
      {
	return DB_TYPE_NULL;
      }

    DB_DOMAIN *attr_domain = db_attribute_domain (attribute);
    if (attr_domain == NULL)
      {
	return DB_TYPE_NULL;
      }

    int db_type = TP_DOMAIN_TYPE (attr_domain);
    if (TP_IS_SET_TYPE (db_type))
      {
	int dummy1;
	short dummy2;
	char dummy3;
	db_type = get_set_domain (attr_domain, dummy1, dummy2, dummy3);
      }
    return db_type;
  }
}
