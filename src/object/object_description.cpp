#include "object_description.hpp"
#include "authenticate.h"
#include "class_object.h"
#include "db_value_printer.hpp"
#include "dbi.h"
#include "locator_cl.h"
#include "object_print_util.hpp"
#include "schema_manager.h"
#include "string_buffer.hpp"
#include "transaction_cl.h"
#include "work_space.h"

object_description::object_description (struct db_object *op)
  : classname (0)
  , oid (0)
  , attributes (0)
  , shared (0)
{
  if (op == 0)
    {
      return;
    }
  int error;
  SM_CLASS *class_;
  SM_ATTRIBUTE *attribute_p;
  char *obj;
  int i, count, is_class = 0;
  object_description *info = NULL;
  char **strs;
  int pin;
  size_t buf_size;
  DB_VALUE value;
  char b[8192] = {0};
  string_buffer sb (sizeof (b), b);
  db_value_printer printer (sb);

  if (op != NULL)
    {
      is_class = locator_is_class (op, DB_FETCH_READ);
      if (is_class < 0)
	{
	  return;
	}
    }
  if (op == NULL || is_class)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return;
    }
  else
    {
      error = au_fetch_instance (op, &obj, AU_FETCH_READ, TM_TRAN_READ_FETCH_VERSION (), AU_SELECT);
      if (error == NO_ERROR)
	{
	  pin = ws_pin (op, 1);
	  error = au_fetch_class (ws_class_mop (op), &class_, AU_FETCH_READ, AU_SELECT);
	  if (error == NO_ERROR)
	    {

	      this->classname = object_print::copy_string ((char *) sm_ch_name ((MOBJ) class_));

	      DB_MAKE_OBJECT (&value, op);
	      obj_print.describe_data (&value);
	      db_value_clear (&value);
	      DB_MAKE_NULL (&value);

	      this->oid = object_print::copy_string (sb.get_buffer());

	      if (class_->ordered_attributes != NULL)
		{
		  count = class_->att_count + class_->shared_count + 1;
		  buf_size = sizeof (char *) * count;
		  strs = (char **) malloc (buf_size);
		  if (strs == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
		      return;
		    }
		  i = 0;
		  for (attribute_p = class_->ordered_attributes; attribute_p != NULL;
		       attribute_p = attribute_p->order_link)
		    {
		      /*
		       * We're starting a new line here, so we don't
		       * want to append to the old buffer; pass NULL
		       * to pt_append_nulstring so that we start a new
		       * string.
		       */
		      sb.clear();
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
		      strs[i] = object_print::copy_string (sb.get_buffer());
		      i++;
		    }
		  strs[i] = NULL;
		  info->attributes = strs;
		}

	      /* will we ever want to separate these lists ? */
	    }
	  (void) ws_pin (op, pin);
	}
    }
}

object_description::~object_description()
{
  free (classname);
  free (oid);
  object_print::free_strarray (attributes);
  object_print::free_strarray (shared);
}
