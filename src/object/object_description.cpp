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
 * object_description.cpp
 */

#include "object_description.hpp"

#include "authenticate.h"
#include "class_object.h"
#include "db_value_printer.hpp"
#include "dbi.h"
#include "locator_cl.h"
#include "mem_block.hpp"
#include "object_print_util.hpp"
#include "schema_manager.h"
#include "string_buffer.hpp"
#include "transaction_cl.h"
#include "work_space.h"
#include "dbtype.h"

object_description::object_description ()
  : classname (0)
  , oid (0)
  , attributes (0)
  , shared (0)
{
}

int object_description::init (struct db_object *op)
{
  if (op == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  int error;
  SM_CLASS *class_;
  SM_ATTRIBUTE *attribute_p;
  char *obj;
  int i, count;
  char **strs;
  int pin;
  size_t buf_size;
  DB_VALUE value;

  int is_class = locator_is_class (op, DB_FETCH_READ);
  if (is_class)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  error = au_fetch_instance (op, &obj, AU_FETCH_READ, TM_TRAN_READ_FETCH_VERSION (), AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  pin = ws_pin (op, 1);
  error = au_fetch_class (ws_class_mop (op), &class_, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }
  this->classname = object_print::copy_string ((char *) sm_ch_name ((MOBJ) class_));

  string_buffer sb;
  db_value_printer printer (sb);

  db_make_object (&value, op);
  printer.describe_data (&value);
  db_value_clear (&value);
  db_make_null (&value);

  this->oid = sb.release_ptr ();

  if (class_->ordered_attributes != NULL)
    {
      count = class_->att_count + class_->shared_count + 1;
      buf_size = sizeof (char *) * count;

      strs = (char **) malloc (buf_size);
      if (strs == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      i = 0;
      for (attribute_p = class_->ordered_attributes; attribute_p != NULL;
	   attribute_p = attribute_p->order_link)
	{
	  sb.clear ();// We're starting a new line here, so we don't want to append to the old buffer
	  sb ("%20s = ", attribute_p->header.name);
	  if (attribute_p->header.name_space == ID_SHARED_ATTRIBUTE)
	    {
	      printer.describe_value (&attribute_p->default_value.value);
	    }
	  else
	    {
	      db_get (op, attribute_p->header.name, &value);
	      printer.describe_value (&value);
	    }
	  strs[i] = sb.release_ptr ();
	  i++;
	}

      strs[i] = NULL;
      attributes = strs;//ToDo: refactor this->attributes as std::vector<char*>
    }

  /* will we ever want to separate these lists ? */
  (void) ws_pin (op, pin);

  return NO_ERROR;
}

object_description::~object_description ()
{
  free (classname);
  free (oid);
  object_print::free_strarray (attributes);
  object_print::free_strarray (shared);
}
