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

  oid_get_info
  oid_handler::oid_get (DB_OBJECT *obj, std::vector<std::string> &attr_names)
  {
    int error = NO_ERROR;
    oid_get_info info;

    // set attribute names
    if (attr_names.empty())
      {
	/* get attributes name */
	DB_ATTRIBUTE *attributes = db_get_attributes (obj);
	for (DB_ATTRIBUTE *att = attributes; att; att = db_attribute_next (att))
	  {
	    const char *name = db_attribute_name (att);
	    attr_names.emplace_back (name);
	  }
      }
    info.attr_names = std::move (attr_names);

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
		// TODO: error handling
		// return db_error_code();
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
		    // TODO: error handling
		    // return db_error_code ();
		  }
	      }
	    *p = '.';
	    db_value_clear (&path_val);
	  }

	int db_type = DB_TYPE_NULL, set_type, precision = 0;
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

	// column_info info (db_type, set_type, scale, prec, charset, attr_name);
	info.column_infos.emplace_back (db_type, set_type, scale, precision, charset, attr_name);
      }

    // set class name
    std::string &class_name = info.class_name;
    const char *cname = db_get_class_name (obj);
    if (cname != NULL)
      {
	class_name.assign (cname);
      }

    // set OID data
    DB_VALUE val;
    for (std::string &attr_name : attr_names)
      {
	if (db_get (obj, attr_name.c_str (), &val) < 0)
	  {
	    db_make_null (&val);
	  }
	info.db_values.push_back (val);
	db_value_clear (&val);
      }

    return info;
  }

  int
  oid_handler::oid_put (DB_OBJECT *obj, std::vector<std::string> &attr_names, std::vector<DB_VALUE> &attr_values)
  {
    int error = NO_ERROR;
    DB_OTMPL *otmpl = dbt_edit_object (obj);
    if (otmpl == NULL)
      {
	return db_error_code ();
	// TODO: error handling
	// err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      }

    assert (attr_names.size() == attr_values.size());

    DB_VALUE *attr_val = NULL;
    int i = 0;
    for (std::string &name : attr_names)
      {
	char attr_type = get_attr_type (obj, name.c_str ());
	attr_val = &attr_values[i];

	error = dbt_put (otmpl, name.c_str (), attr_val);
	if (error < 0)
	  {
	    // TODO: error handling
	    db_value_clear (attr_val);
	    dbt_abort_object (otmpl);
	  }

	db_value_clear (attr_val);
      }

    obj = dbt_finish_object (otmpl);
    if (obj == NULL)
      {
	// TODO: error handling
	error = db_error_code ();
      }

    return error;
  }

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