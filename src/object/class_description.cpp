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
 * class_description.cpp
 */

#include "class_description.hpp"

#include "authenticate.h"
#include "class_object.h"
#include "locator_cl.h"
#include "mem_block.hpp"
#include "message_catalog.h"
#include "msgcat_help.hpp"
#include "object_printer.hpp"
#include "object_print_util.hpp"
#include "parse_tree.h"
#include "parser.h"
#include "schema_manager.h"
#include "string_buffer.hpp"

namespace
{
  void describe_trigger_list (tr_triglist *trig, string_buffer &sb, object_printer &printer, std::vector<char *> &v)
  {
    for (TR_TRIGLIST *t = trig; t != NULL; t = t->next)
      {
	sb.clear ();
	printer.describe_class_trigger (*t->trigger);
	v.push_back (object_print::copy_string (sb.get_buffer ()));
      }
  }

  void init_triggers (const sm_class &cls, struct db_object &dbo, string_buffer &sb, object_printer &printer,
		      std::vector<char *> &triggers)
  {
    SM_ATTRIBUTE *attribute_p;
    TR_SCHEMA_CACHE *cache;
    int i;

    cache = cls.triggers;
    if (cache != NULL && !tr_validate_schema_cache (cache, &dbo))
      {
	for (i = 0; i < cache->array_length; i++)
	  {
	    describe_trigger_list (cache->triggers[i], sb, printer, triggers);
	  }
      }

    for (attribute_p = cls.ordered_attributes; attribute_p != NULL; attribute_p = attribute_p->order_link)
      {
	cache = attribute_p->triggers;
	if (cache != NULL && !tr_validate_schema_cache (cache, &dbo))
	  {
	    for (i = 0; i < cache->array_length; i++)
	      {
		describe_trigger_list (cache->triggers[i], sb, printer, triggers);
	      }
	  }
      }

    for (attribute_p = cls.class_attributes; attribute_p != NULL;
	 attribute_p = (SM_ATTRIBUTE *) attribute_p->header.next)
      {
	cache = attribute_p->triggers;
	if (cache != NULL && !tr_validate_schema_cache (cache, &dbo))
	  {
	    for (i = 0; i < cache->array_length; i++)
	      {
		describe_trigger_list (cache->triggers[i], sb, printer, triggers);
	      }
	  }
      }
  }
} //namespace

//former obj_print_make_class_help()
class_description::class_description ()
  : name (NULL)
  , class_type (NULL)
  , collation (NULL)
  , supers (NULL)
  , subs (NULL)
  , attributes (NULL)
  , class_attributes (NULL)
  , methods (NULL)
  , class_methods (NULL)
  , resolutions (NULL)
  , method_files (NULL)
  , query_spec (NULL)
  , object_id (NULL)
  , triggers ()
  , constraints (NULL)
  , partition ()
  , comment (NULL)
{
}

class_description::~class_description ()
{
  if (name != NULL)
    {
      free (name);
    }
  if (class_type != NULL)
    {
      free (class_type);
    }
  if (object_id != NULL)
    {
      free (object_id);
    }
  if (collation != NULL)
    {
      free (collation);
    }
  object_print::free_strarray (supers);
  object_print::free_strarray (subs);
  object_print::free_strarray (attributes);
  object_print::free_strarray (class_attributes);
  object_print::free_strarray (methods);
  object_print::free_strarray (class_methods);
  object_print::free_strarray (resolutions);
  object_print::free_strarray (method_files);
  object_print::free_strarray (query_spec);
#if 0 //bSolo: temporary until evolve above gcc 4.4.7
  for (auto it: triggers)
    {
      free (it);
    }
#else
  for (auto it=triggers.begin (); it != triggers.end (); ++it)
    {
      free (*it);
    }
#endif
  triggers.clear ();
  object_print::free_strarray (constraints);
#if 0 //bSolo: temporary until evolve above gcc 4.4.7
  for (auto it: partition)
    {
      free (it);
    }
#else
  for (auto it=partition.begin (); it != partition.end (); ++it)
    {
      free (*it);
    }
#endif
  partition.clear ();
  if (comment != NULL)
    {
      free (comment);
    }
}

int class_description::init (const char *name)
{
  db_object *op = sm_find_class (name);
  if (op == NULL)
    {
      int error_code = NO_ERROR;
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  return init (op, CSQL_SCHEMA_COMMAND);
}

int class_description::init (struct db_object *op, type prt_type)
{
  assert (op != NULL);
  string_buffer sb;
  return init (op, prt_type, sb);
}

int class_description::init (struct db_object *op, type prt_type, string_buffer &sb)
{
  assert (op != NULL);

  // cleanup before (re)initialize
  this->~class_description ();

  SM_CLASS *class_;
  SM_ATTRIBUTE *a;
  SM_METHOD *m;
  SM_QUERY_SPEC *p;
  DB_OBJLIST *super, *user;
  int count, i;
  char **strs;
  const char *kludge;
  int is_class = 0;
  SM_CLASS *subclass;
  bool include_inherited;
  bool force_print_att_coll = false;
  bool has_comment = false;
  int max_name_size = SM_MAX_IDENTIFIER_LENGTH + 50;
  size_t buf_size = 0;
  object_printer printer (sb);

  include_inherited = (prt_type == CSQL_SCHEMA_COMMAND);

  is_class = locator_is_class (op, DB_FETCH_READ);
  if (is_class < 0)
    {
      return ER_FAILED;
    }
  if (!is_class || locator_is_root (op))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  int err = au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT);
  if (err != NO_ERROR)
    {
      return err;
    }
  if (class_->comment != NULL && class_->comment[0] != '\0')
    {
      has_comment = true;
      max_name_size = SM_MAX_IDENTIFIER_LENGTH + SM_MAX_CLASS_COMMENT_LENGTH + 50;
    }

  force_print_att_coll = (class_->collation_id != LANG_SYS_COLLATION) ? true : false;
  /* make sure all the information is up to date */
  if (sm_clean_class (op, class_) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (prt_type == CSQL_SCHEMA_COMMAND)
    {
      /*
       * For the case of "print schema",
       * this->name is set to:
       *   exact class name
       *   + COLLATE collation_name if exists;
       *   + COMMENT 'text' if exists;
       *
       * The caller uses this->name to fill in "<Class Name> $name"
       */
      if (class_->collation_id == LANG_SYS_COLLATION)
	{
	  sb.clear ();
	  if (has_comment)
	    {
	      sb ("%-20s ", (char *) sm_ch_name ((MOBJ) class_));
	      printer.describe_comment (class_->comment);
	    }
	  else
	    {
	      sb ("%s", (char *) sm_ch_name ((MOBJ) class_));
	    }
	}
      else
	{
	  if (has_comment)
	    {
	      sb ("%-20s COLLATE %s ", sm_ch_name ((MOBJ) class_), lang_get_collation_name (class_->collation_id));
	      printer.describe_comment (class_->comment);
	    }
	  else
	    {
	      sb ("%-20s COLLATE %s", sm_ch_name ((MOBJ) class_), lang_get_collation_name (class_->collation_id));
	    }
	}
      this->name = object_print::copy_string (sb.get_buffer ());
    }
  else
    {
      /*
       * For the case prt_type == OBJ_PRINT_SHOW_CREATE_TABLE
       * this->name is set to the exact class name
       */
      sb.clear ();
      sb ("[%s]", sm_remove_qualifier_name (sm_ch_name ((MOBJ) class_)));
      this->name = object_print::copy_string (sb.get_buffer ());
    }

  switch (class_->class_type)
    {
    default:
      this->class_type = object_print::copy_string (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP,
			 MSGCAT_HELP_META_CLASS_HEADER));
      break;
    case SM_CLASS_CT:
      this->class_type = object_print::copy_string (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP,
			 MSGCAT_HELP_CLASS_HEADER));
      break;
    case SM_VCLASS_CT:
      this->class_type = object_print::copy_string (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP,
			 MSGCAT_HELP_VCLASS_HEADER));
      break;
    }

  this->collation = object_print::copy_string (lang_get_collation_name (class_->collation_id));
  if (this->collation == NULL)
    {
      return ER_FAILED;
    }

  if (has_comment && prt_type != CSQL_SCHEMA_COMMAND)
    {
      /*
       * For the case except "print schema",
       * comment is copied to this->comment anyway
       */
      this->comment = object_print::copy_string (class_->comment);
      if (this->comment == NULL)
	{
	  return ER_FAILED;
	}
    }

  if (class_->inheritance != NULL)
    {
      count = ws_list_length ((DB_LIST *) class_->inheritance);
      buf_size = sizeof (char *) * (count + 1);

      strs = (char **) malloc (buf_size);
      if (strs == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	  return ER_FAILED;
	}

      i = 0;
      for (super = class_->inheritance; super != NULL; super = super->next)
	{
	  /* kludge for const vs. non-const warnings */
	  kludge = sm_get_ch_name (super->op);
	  if (kludge == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return ER_FAILED;
	    }

	  if (prt_type == CSQL_SCHEMA_COMMAND)
	    {
	      strs[i] = object_print::copy_string ((char *) kludge);
	    }
	  else
	    {
	      /* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
	      sb.clear ();
	      sb ("[%s]", kludge);
	      strs[i] = object_print::copy_string (sb.get_buffer ());
	    }
	  i++;
	}

      strs[i] = 0;
      this->supers = strs;
    }

  if (class_->users != NULL)
    {
      count = ws_list_length ((DB_LIST *) class_->users);
      buf_size = sizeof (char *) * (count + 1);

      strs = (char **) malloc (buf_size);
      if (strs == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	  return ER_FAILED;
	}

      i = 0;
      for (user = class_->users; user != NULL; user = user->next)
	{
	  /* kludge for const vs. non-const warnings */
	  kludge = sm_get_ch_name (user->op);
	  if (kludge == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return ER_FAILED;
	    }

	  if (prt_type == CSQL_SCHEMA_COMMAND)
	    {
	      strs[i] = object_print::copy_string ((char *) kludge);
	    }
	  else
	    {
	      /* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
	      sb.clear ();
	      sb ("[%s]", kludge);
	      strs[i] = object_print::copy_string (sb.get_buffer ());
	    }

	  i++;
	}

      strs[i] = 0;
      this->subs = strs;
    }

  if (class_->attributes != NULL || class_->shared != NULL)
    {
      if (include_inherited)
	{
	  count = class_->att_count + class_->shared_count;
	}
      else
	{
	  count = 0;
	  /* find the number own by itself */
	  for (a = class_->ordered_attributes; a != NULL; a = a->order_link)
	    {
	      if (a->class_mop == op)
		{
		  count++;
		}
	    }
	}

      if (count > 0)
	{
	  buf_size = sizeof (char *) * (count + 1);

	  strs = (char **) malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      return ER_FAILED;
	    }

	  i = 0;
	  for (a = class_->ordered_attributes; a != NULL; a = a->order_link)
	    {
	      if (include_inherited || (!include_inherited && a->class_mop == op))
		{
		  sb.clear ();
		  printer.describe_attribute (*op, *a, (a->class_mop != op), prt_type, force_print_att_coll);
		  if (sb.len () == 0)
		    {
		      return ER_FAILED;
		    }
		  strs[i] = object_print::copy_string (sb.get_buffer ());
		  i++;
		}
	    }

	  strs[i] = 0;
	  this->attributes = strs;
	}
    }

  if (class_->class_attributes != NULL)
    {
      if (include_inherited)
	{
	  count = class_->class_attribute_count;
	}
      else
	{
	  count = 0;
	  /* find the number own by itself */
	  for (a = class_->class_attributes; a != NULL; a = (SM_ATTRIBUTE *) a->header.next)
	    {
	      if (a->class_mop == op)
		{
		  count++;
		}
	    }
	}

      if (count > 0)
	{
	  buf_size = sizeof (char *) * (count + 1);

	  strs = (char **)malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      return ER_FAILED;
	    }

	  i = 0;
	  for (a = class_->class_attributes; a != NULL; a = (SM_ATTRIBUTE *) a->header.next)
	    {
	      if (include_inherited || (!include_inherited && a->class_mop == op))
		{
		  sb.clear ();
		  printer.describe_attribute (*op, *a, (a->class_mop != op), prt_type, force_print_att_coll);
		  if (sb.len () == 0)
		    {
		      return ER_FAILED;
		    }
		  strs[i] = object_print::copy_string (sb.get_buffer ());
		  i++;
		}
	    }

	  strs[i] = 0;
	  this->class_attributes = strs;
	}
    }

  if (class_->methods != NULL)
    {
      if (include_inherited)
	{
	  count = class_->method_count;
	}
      else
	{
	  count = 0;
	  /* find the number own by itself */
	  for (m = class_->methods; m != NULL; m = (SM_METHOD *) m->header.next)
	    {
	      if (m->class_mop == op)
		{
		  count++;
		}
	    }
	}

      if (count > 0)
	{
	  buf_size = sizeof (char *) * (count + 1);

	  strs = (char **) malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      return ER_FAILED;
	    }

	  i = 0;
	  for (m = class_->methods; m != NULL; m = (SM_METHOD *) m->header.next)
	    {
	      if (include_inherited || (!include_inherited && m->class_mop == op))
		{
		  sb.clear ();
		  printer.describe_method (*op, *m, prt_type);
		  strs[i] = object_print::copy_string (sb.get_buffer ());
		  i++;
		}
	    }

	  strs[i] = 0;
	  this->methods = strs;
	}
    }

  if (class_->class_methods != NULL)
    {
      if (include_inherited)
	{
	  count = class_->class_method_count;
	}
      else
	{
	  count = 0;
	  /* find the number own by itself */
	  for (m = class_->class_methods; m != NULL; m = (SM_METHOD *) m->header.next)
	    {
	      if (m->class_mop == op)
		{
		  count++;
		}
	    }
	}

      if (count > 0)
	{
	  buf_size = sizeof (char *) * (count + 1);

	  strs = (char **) malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      return ER_FAILED;
	    }

	  i = 0;
	  for (m = class_->class_methods; m != NULL; m = (SM_METHOD *) m->header.next)
	    {
	      if (include_inherited || (!include_inherited && m->class_mop == op))
		{
		  sb.clear ();
		  printer.describe_method (*op, *m, prt_type);
		  strs[i] = object_print::copy_string (sb.get_buffer ());
		  i++;
		}
	    }

	  strs[i] = 0;
	  this->class_methods = strs;
	}
    }

  if (class_->resolutions != NULL)
    {
      count = ws_list_length ((DB_LIST *) class_->resolutions);
      buf_size = sizeof (char *) * (count + 1);

      strs = (char **) malloc (buf_size);
      if (strs == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	  return ER_FAILED;
	}

      i = 0;
      for (SM_RESOLUTION *r = class_->resolutions; r != NULL; r = r->next)
	{
	  sb.clear ();
	  printer.describe_resolution (*r, prt_type);
	  strs[i] = object_print::copy_string (sb.get_buffer ());
	  i++;
	}

      strs[i] = 0;
      this->resolutions = strs;
    }

  if (class_->method_files != NULL)
    {
      if (include_inherited)
	{
	  count = ws_list_length ((DB_LIST *) class_->method_files);
	}
      else
	{
	  count = 0;

	  /* find the number own by itself */
	  for (SM_METHOD_FILE *f = class_->method_files; f != NULL; f = f->next)
	    {
	      if (f->class_mop == op)
		{
		  count++;
		}
	    }
	}

      if (count > 0)
	{
	  buf_size = sizeof (char *) * (count + 1);

	  strs = (char **) malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      return ER_FAILED;
	    }

	  i = 0;
	  for (SM_METHOD_FILE *f = class_->method_files; f != NULL; f = f->next)
	    {
	      if (include_inherited || (!include_inherited && f->class_mop == op))
		{
		  sb.clear ();
		  printer.describe_method_file (*op, *f);
		  strs[i] = object_print::copy_string (sb.get_buffer ());
		  i++;
		}
	    }

	  strs[i] = 0;
	  this->method_files = strs;
	}
    }

  if (class_->query_spec != NULL)
    {
      count = ws_list_length ((DB_LIST *) class_->query_spec);
      buf_size = sizeof (char *) * (count + 1);

      strs = (char **) malloc (buf_size);
      if (strs == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	  return ER_FAILED;
	}

      i = 0;
      for (p = class_->query_spec; p != NULL; p = p->next)
	{
	  strs[i] = object_print::copy_string ((char *) p->specification);
	  i++;
	}

      strs[i] = 0;
      this->query_spec = strs;
    }

  /* these are a bit more complicated */
  init_triggers (*class_, *op, sb, printer, triggers);

  /*
   * Process multi-column class constraints (Unique and Indexes).
   * Single column constraints (NOT 0) are displayed along with
   * the attributes.
   */
  this->constraints = 0;	/* initialize */
  if (class_->constraints != NULL)
    {
      SM_CLASS_CONSTRAINT *c;

      count = 0;
      for (c = class_->constraints; c; c = c->next)
	{
	  if (SM_IS_CONSTRAINT_INDEX_FAMILY (c->type))
	    {
	      /* Csql schema command will print all constraints, which include the constraints belong to the table
	       * itself and belong to the parent table. But show create table will only print the constraints which
	       * belong to the table itself.
	       */
	      if (include_inherited
		  || (!include_inherited && c->attributes[0] != NULL && c->attributes[0]->class_mop == op))
		{
		  count++;
		}
	    }
	}

      if (count > 0)
	{
	  buf_size = sizeof (char *) * (count + 1);

	  strs = (char **) malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      return ER_FAILED;
	    }

	  i = 0;
	  for (c = class_->constraints; c; c = c->next)
	    {
	      if (SM_IS_CONSTRAINT_INDEX_FAMILY (c->type))
		{
		  if (include_inherited
		      || (!include_inherited && c->attributes[0] != NULL && c->attributes[0]->class_mop == op))
		    {
		      sb.clear ();
		      printer.describe_constraint (*class_, *c, prt_type);
		      strs[i] = object_print::copy_string (sb.get_buffer ());
		      if (strs[i] == NULL)
			{
			  this->constraints = strs;
			  return ER_FAILED;
			}
		      i++;
		    }
		}
	    }

	  strs[i] = 0;
	  this->constraints = strs;
	}
    }

  //partition
  if (class_->partition != NULL && class_->partition->pname == NULL)
    {
      sb.clear ();
      printer.describe_partition_info (*class_->partition);
      partition.push_back (object_print::copy_string (sb.get_buffer ()));

      bool is_print_partition = true;
      count = 0;

      /* Show create table will not print the sub partition for hash partition table. */
      if (prt_type == SHOW_CREATE_TABLE)
	{
	  is_print_partition = (class_->partition->partition_type != PT_PARTITION_HASH);
	}

      if (is_print_partition)
	{
	  for (user = class_->users; user != NULL; user = user->next)
	    {
	      if (au_fetch_class (user->op, &subclass, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	      if (subclass->partition)
		{
		  sb.clear ();
		  printer.describe_partition_parts (*subclass->partition, prt_type);
		  partition.push_back (object_print::copy_string (sb.get_buffer ()));
		}
	    }
	}
    }

  return NO_ERROR;
}
