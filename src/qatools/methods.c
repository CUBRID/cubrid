/* $Revision: 1.4 $ */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <dbi.h>

void
print_dept_info (DB_OBJECT * object, DB_VALUE * return_value)
{
  static char buf[128];
  DB_VALUE dept_no_val, dept_name_val, address_val;
  const char *dept_name, *address;
  int dept_no;

  db_get (object, "dept_no", &dept_no_val);
  db_get (object, "dept_name", &dept_name_val);
  db_get (object, "address", &address_val);

  dept_no = DB_GET_INTEGER (&dept_no_val);
  dept_name = DB_GET_STRING (&dept_name_val);
  address = DB_GET_STRING (&address_val);

  sprintf (buf, "The dept info is: %d, %s, %s", dept_no, dept_name, address);
  DB_MAKE_STRING (return_value, buf);

  db_value_clear (&dept_name_val);
  db_value_clear (&address_val);
  return;
}


void
print_emp_info (DB_OBJECT * object, DB_VALUE * return_value)
{
  static char buf[128];
  DB_VALUE emp_name_val, ssn_val, dept_name_val, mgr_name_val;
  const char *emp_name, *dept_name, *mgr_name;
  int ssn;

  db_get (object, "emp_name", &emp_name_val);
  db_get (object, "ssn", &ssn_val);
  db_get (object, "dept.dept_name", &dept_name_val);
  db_get (object, "mgr.emp_name", &mgr_name_val);

  emp_name = DB_GET_STRING (&emp_name_val);
  ssn = DB_GET_INTEGER (&ssn_val);
  dept_name = DB_GET_STRING (&dept_name_val);
  mgr_name = DB_GET_STRING (&mgr_name_val);

  sprintf (buf,
	   "The dept info is: ssn: %d, name: %s, dept name: %s, mgr name: %s ",
	   ssn, emp_name, dept_name, mgr_name);

  DB_MAKE_STRING (return_value, buf);

  db_value_clear (&emp_name_val);
  db_value_clear (&dept_name_val);
  db_value_clear (&mgr_name_val);
  return;
}


void
print_utilizes_info (DB_OBJECT * object, DB_VALUE * return_value)
{
  static char buf[128];
  DB_VALUE comp_name_val, quantity_val;
  const char *comp_name;
  int quantity;

  db_get (object, "component.name", &comp_name_val);
  db_get (object, "quantity", &quantity_val);

  comp_name = DB_GET_STRING (&comp_name_val);
  quantity = DB_GET_INTEGER (&quantity_val);

  sprintf (buf, "%d nos of component '%s' are used", quantity, comp_name);
  DB_MAKE_STRING (return_value, buf);

  db_value_clear (&comp_name_val);
  return;
}


/* method part_computecost (partname string): monetary

   Algorithm:
   
   p := (SELECT part FROM part WHERE name = partname);

   IF p IS IN basepart
     return_val := p.cost;
   ELSE
     return_val := "A composite part: qty = %d, %s",
                   p.uses.quantity, compute_cost(p.component.name)';

NB: these contortions show that writing methods correctly needs careful thought.
*/

void
part_computecost (DB_OBJECT * object, DB_VALUE * return_value)
{
  static char buf[256], buf_recursive[256];
  DB_VALUE cost_val, component_val, qty_val, part_val;
  DB_OBJECT *base_part, *composite_part, *part, *component, *cls;
  int error;
  int qty;
  const char *unit_cost;

  base_part = db_find_class ("base_part");
  composite_part = db_find_class ("composite_part");

  if (db_is_instance_of (object, base_part))
    {
      error = db_get (object, "cost", &cost_val);
      if (error || DB_IS_NULL (&cost_val))
	{
	  sprintf (buf, "The base part's cost is indeterminate.");
	  DB_MAKE_STRING (return_value, buf);
	}
      else
	{
	  sprintf (buf, "The base part costs US$%f.",
		   DB_GET_MONETARY (&cost_val)->amount);
	  DB_MAKE_STRING (return_value, buf);
	}
    }
  else if (db_is_instance_of (object, composite_part))
    {
      if (!db_get (object, "uses", &component_val)
	  && (component = DB_GET_OBJECT (&component_val))
	  && !db_get (component, "quantity", &qty_val)
	  && !db_get (component, "component", &part_val)
	  && (part = DB_GET_OBJECT (&part_val)))
	{
	  qty = DB_GET_INTEGER (&qty_val);
	  part_computecost (part, &cost_val);
	  unit_cost = DB_GET_STRING (&cost_val);
	  sprintf (buf_recursive, "A composite part: qty=%d, %s", qty,
		   unit_cost);
	  DB_MAKE_STRING (return_value, buf_recursive);
	}
      else
	{
	  sprintf (buf, "The composite part's cost is indeterminate.");
	  DB_MAKE_STRING (return_value, buf);
	}
    }
  else
    {
      if ((cls = db_get_class (object)))
	sprintf (buf, "The object is a %s", db_get_class_name (cls));
      else
	sprintf (buf, "The object is not a part.");
      DB_MAKE_STRING (return_value, buf);
    }
}


void
unsold_stock_discount (DB_OBJECT * object, DB_VALUE * return_value)
{
  DB_VALUE val;
  DB_MONETARY *money;
  double discount;

  db_get (object, "cost", &val);
  if (!(money = DB_GET_MONETARY (&val)))
    discount = 0.10;
  else if (money->amount < 128000)
    discount = 0.20;
  else
    discount = 0.30;

  DB_MAKE_DOUBLE (return_value, discount);
}


void
get_self (MOP invoked_on, DB_VALUE * return_value)
{
  DB_MAKE_OBJECT (return_value, invoked_on);
  return;
}

void
num_in_class (MOP invoked_on, DB_VALUE * return_value)
{
  DB_OBJLIST *list, *tmp;
  int i = 0;

  list = db_get_all_objects (invoked_on);

  for (tmp = list; tmp; tmp = tmp->next)
    i++;

  db_objlist_free (list);

  DB_MAKE_INTEGER (return_value, i);
  return;
}

void
add_int (MOP invoked_on, DB_VALUE * return_value,
	 DB_VALUE * p1, DB_VALUE * p2)
{
  DB_MAKE_INTEGER (return_value, DB_GET_INTEGER (p1) + DB_GET_INTEGER (p2));
  return;
}

void
mul_int (MOP invoked_on, DB_VALUE * return_value,
	 DB_VALUE * p1, DB_VALUE * p2)
{
  DB_MAKE_INTEGER (return_value, DB_GET_INTEGER (p1) * DB_GET_INTEGER (p2));
  return;
}

void
concat_str (MOP invoked_on, DB_VALUE * return_value,
	    DB_VALUE * p1, DB_VALUE * p2)
{
  int len1 = db_get_string_size (p1);
  int len2 = db_get_string_size (p2);
  char *tmpstr;

  DB_MAKE_NULL (return_value);
  if (DB_IS_NULL (p1) && DB_IS_NULL (p2))
    return;

  if (DB_IS_NULL (p1))
    {
      tmpstr = malloc (len2 + 1);
      memcpy (tmpstr, db_get_string (p2), len2);
      memset (tmpstr + len2, '\0', 1);
    }
  else if (DB_IS_NULL (p2))
    {
      tmpstr = malloc (len1 + 1);
      memcpy (tmpstr, db_get_string (p1), len1);
      memset (tmpstr + len1, '\0', 1);
    }
  else
    {
      tmpstr = malloc (len1 + len2 + 1);
      memcpy (tmpstr, db_get_string (p1), len1);
      memcpy (tmpstr + len1, db_get_string (p2), len2);
      memset (tmpstr + len1 + len2, '\0', 1);
    }

  DB_MAKE_STRING (return_value, tmpstr);
  return;
}


void
sub_one (MOP invoked_on, DB_VALUE * return_value, DB_VALUE * p1)
{
  DB_MAKE_INTEGER (return_value, DB_GET_INTEGER (p1) - 1);
  return;
}

void
same_seq (MOP invoked_on, DB_VALUE * return_value, DB_VALUE * p1)
{
  DB_MAKE_SEQUENCE (return_value, DB_GET_SEQUENCE (p1));
  return;
}

void
bad_method (DB_OBJECT * self, DB_VALUE * result)
{
  DB_OBJLIST *list, *tmp;
  MOP cl;
  DB_VALUE passwd;
#ifdef MSW
#define CLOSE_OUTPUT fclose(stdout)
  FILE *stdout = NULL;
  if (!(stdout = fopen ("method.log", "a")))
    return;
#else
#define CLOSE_OUTPUT
#endif
  cl = db_find_class ("db_password");
  if (cl == NULL)
    {
      fprintf (stdout, "%s\n", db_error_string (3));
      CLOSE_OUTPUT;
      return;
    }
  list = db_get_all_objects (cl);
  if (list == NULL)
    {
      db_objlist_free (list);
      fprintf (stdout, "%s\n", db_error_string (3));
      CLOSE_OUTPUT;
      return;
    }
  for (tmp = list; tmp; tmp = tmp->next)
    {
      if (db_get (tmp->op, "password", &passwd) < 0)
	{
	  fprintf (stdout, "%s\n", db_error_string (3));
	  db_objlist_free (list);
	  CLOSE_OUTPUT;
	  return;
	}
      if (DB_VALUE_TYPE (&passwd) == DB_TYPE_STRING)
	fprintf (stdout, "password = %s/n", DB_GET_STRING (&passwd));
      else
	fprintf (stdout, "password is NULL");
    }
  db_objlist_free (list);
  CLOSE_OUTPUT;
}

void
bad_method1 (DB_OBJECT * class, DB_VALUE * result, DB_VALUE * input_arg)
{
  DB_OBJECT *new_obj;
#ifdef MSW
#define CLOSE_OUTPUT fclose(stdout)
  FILE *stdout = NULL;
  if (!(stdout = fopen ("method.log", "a")))
    return;
#else
#define CLOSE_OUTPUT
#endif
  DB_MAKE_NULL (result);
  new_obj = db_create (class);
  if (new_obj == NULL)
    {
      fprintf (stdout, "%s\n", db_error_string (3));
      CLOSE_OUTPUT;
      return;
    }

  if (db_put (new_obj, "xint", input_arg))
    {
      fprintf (stdout, "%s\n", db_error_string (3));
      CLOSE_OUTPUT;
      return;
    }
  DB_MAKE_OBJECT (result, new_obj);
  CLOSE_OUTPUT;
}


/* next two functions are used (only) by mtests/all_method.sql */
void
put_dept_val (MOP invoked_from, DB_VALUE * return_value, DB_VALUE * parm)
{

  static char buf[128];
  int error;

  if ((error = db_put (invoked_from, "dept", parm)))
    {
      sprintf (buf, "Can't put dept");
      DB_MAKE_STRING (return_value, buf);
      return;
    }

  sprintf (buf, "success");
  DB_MAKE_STRING (return_value, buf);
  return;
}

void
do_lock (MOP invoked_from, DB_VALUE * return_value, DB_VALUE * parm)
{

  static char buf[128];
  int error;
  int which_lock;

  which_lock = DB_GET_INTEGER (parm);

  if (which_lock)
    {
      if ((error = db_lock_write (invoked_from)))
	{
	  sprintf (buf, "Can't do lock");
	  DB_MAKE_STRING (return_value, buf);
	  return;
	}
    }
  else
    {
      if ((error = db_lock_read (invoked_from)))
	{
	  sprintf (buf, "Can't do lock");
	  DB_MAKE_STRING (return_value, buf);
	  return;
	}
    }

  sprintf (buf, "success");
  DB_MAKE_STRING (return_value, buf);
  return;
}
