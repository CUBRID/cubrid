/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "object_print_class_description.hpp"
#include "authenticate.h"
#include "class_object.h"
#include "locator_cl.h"
#include "msgcat_help.hpp"
#include "message_catalog.h"
#include "object_print_parser.hpp"
#include "object_print_util.hpp"
#include "parse_tree.h"
#include "parser.h"
#include "schema_manager.h"
#include "string_buffer.hpp"

static PARSER_CONTEXT *parser;

//former obj_print_make_class_help()
object_print::class_description::class_description()
  : name(0)
  , class_type(0)
  , collation(0)
  , supers(0)
  , subs(0)
  , attributes(0)
  , class_attributes(0)
  , methods(0)
  , class_methods(0)
  , resolutions(0)
  , method_files(0)
  , query_spec(0)
  , object_id(0)
  , triggers(0)
  , constraints(0)
  , partition(0)
  , comment(0)
{
}

object_print::class_description::class_description(const char* name)
  : class_description(sm_find_class(name), CSQL_SCHEMA_COMMAND)
{
}

/*
 * obj_print_help_class () - Constructs a class help structure containing textual
 *                 information about the class.
 *   return: class help structure
 *   op(in): class object
 *   prt_type(in): the print type: csql schema or show create table
 */
object_print::class_description::class_description(db_object* op, type prt_type)
  : class_description()
{
        SM_CLASS *class_;
        SM_ATTRIBUTE *a;
        SM_METHOD *m;
        SM_QUERY_SPEC *p;
        DB_OBJLIST *super, *user;
        int count, i, is_cubrid = 0;
        char **strs;
        const char *kludge;
        int is_class = 0;
        SM_CLASS *subclass;
        bool include_inherited;
        bool force_print_att_coll = false;
        bool has_comment = false;
        int max_name_size = SM_MAX_IDENTIFIER_LENGTH + 50;
        size_t buf_size = 0;
        strlist *str_list_head = 0, *current_str = 0, *tmp_str = 0;
        char b[8192] = {0};//bSolo: temp hack
        string_buffer sb(sizeof(b), b);
        object_print_parser obj_print(sb);
      
        if (parser == 0)
          {
            parser = parser_create_parser ();
          }
        if (parser == 0)
          {
            goto error_exit;
          }
      
        include_inherited = (prt_type == CSQL_SCHEMA_COMMAND);
      
        is_class = locator_is_class (op, DB_FETCH_READ);
        if (is_class < 0)
          {
            goto error_exit;
          }
        if (!is_class || locator_is_root (op))
          {
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
            goto error_exit;
          }
      
        else if (au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
          {
            if (class_->comment != 0 && class_->comment[0] != '\0')
              {
                has_comment = true;
                max_name_size = SM_MAX_IDENTIFIER_LENGTH + SM_MAX_CLASS_COMMENT_LENGTH + 50;
              }
      
            force_print_att_coll = (class_->collation_id != LANG_SYS_COLLATION) ? true : false;
            /* make sure all the information is up to date */
            if (sm_clean_class (op, class_) != NO_ERROR)
              {
                goto error_exit;
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
                    sb.clear();
                    if (has_comment)
                      {
                        sb("%-20s ", (char*)sm_ch_name((MOBJ)class_));
                        obj_print.describe_comment(class_->comment);
                      }
                    else
                      {
                        sb("%s", (char*)sm_ch_name((MOBJ)class_));
                      }
                  }
                else
                  {
                    if (has_comment)
                      {
                        sb("%-20s COLLATE %s ", sm_ch_name((MOBJ)class_), lang_get_collation_name(class_->collation_id));
                        obj_print.describe_comment(class_->comment);
                      }
                    else
                      {
                        sb("%-20s COLLATE %s", sm_ch_name((MOBJ)class_), lang_get_collation_name(class_->collation_id));
                      }
                  }
                this->name = copy_string(sb.get_buffer());
              }
            else
              {
                /* 
                 * For the case prt_type == OBJ_PRINT_SHOW_CREATE_TABLE
                 * this->name is set to the exact class name
                 */
                sb.clear();
                sb("[%s]", sm_ch_name ((MOBJ) class_));
                this->name = copy_string(sb.get_buffer());
              }
      
            switch (class_->class_type)
              {
              default:
                this->class_type =
                  copy_string (msgcat_message
                                         (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_META_CLASS_HEADER));
                break;
              case SM_CLASS_CT:
                this->class_type =
                  copy_string (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_HEADER));
                break;
              case SM_VCLASS_CT:
                this->class_type =
                  copy_string (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_VCLASS_HEADER));
                break;
              }
      
            this->collation = copy_string (lang_get_collation_name (class_->collation_id));
            if (this->collation == 0)
              {
                goto error_exit;
              }
      
            if (has_comment && prt_type != CSQL_SCHEMA_COMMAND)
              {
                /* 
                 * For the case except "print schema",
                 * comment is copied to this->comment anyway
                 */
                this->comment = copy_string (class_->comment);
                if (this->comment == 0)
                  {
                    goto error_exit;
                  }
              }
      
            if (class_->inheritance != 0)
              {
                count = ws_list_length ((DB_LIST *) class_->inheritance);
                buf_size = sizeof (char *) * (count + 1);
                strs = (char **) malloc (buf_size);
                if (strs == 0)
                  {
                    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                    goto error_exit;
                  }
                i = 0;
                for (super = class_->inheritance; super != 0; super = super->next)
                  {
                    /* kludge for const vs. non-const warnings */
                    kludge = sm_get_ch_name (super->op);
                    if (kludge == 0)
                      {
                        assert (er_errid () != NO_ERROR);
                        goto error_exit;
                      }
      
                    if (prt_type == CSQL_SCHEMA_COMMAND)
                      {
                        strs[i] = copy_string ((char *) kludge);
                      }
                    else
                      {		/* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
                        sb.clear();
                        sb("[%s]", kludge);
                        strs[i] = copy_string (sb.get_buffer());
                      }
                    i++;
                  }
                strs[i] = 0;
                this->supers = strs;
              }
      
            if (class_->users != 0)
              {
                count = ws_list_length ((DB_LIST *) class_->users);
                buf_size = sizeof (char *) * (count + 1);
                strs = (char **) malloc (buf_size);
                if (strs == 0)
                  {
                    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                    goto error_exit;
                  }
                i = 0;
                for (user = class_->users; user != 0; user = user->next)
                  {
                    /* kludge for const vs. non-const warnings */
                    kludge = sm_get_ch_name (user->op);
                    if (kludge == 0)
                      {
                        assert (er_errid () != NO_ERROR);
                        goto error_exit;
                      }
      
                    if (prt_type == CSQL_SCHEMA_COMMAND)
                      {
                        strs[i] = copy_string ((char *) kludge);
                      }
                    else
                      {		/* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
                        sb.clear();
                        sb("[%s]", kludge);
                        strs[i] = copy_string(sb.get_buffer());
                      }
      
                    i++;
                  }
                strs[i] = 0;
                this->subs = strs;
              }
      
            if (class_->attributes != 0 || class_->shared != 0)
              {
                if (include_inherited)
                  {
                    count = class_->att_count + class_->shared_count;
                  }
                else
                  {
                    count = 0;
                    /* find the number own by itself */
                    for (a = class_->ordered_attributes; a != 0; a = a->order_link)
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
                    if (strs == 0)
                      {
                        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                        goto error_exit;
                      }
      
                    i = 0;
                    for (a = class_->ordered_attributes; a != 0; a = a->order_link)
                      {
                        if (include_inherited || (!include_inherited && a->class_mop == op))
                          {
                            sb.clear();
                            obj_print.describe_attribute(*op, *a, (a->class_mop != op), prt_type, force_print_att_coll);
                            if(sb.len() == 0)
                              {
                                goto error_exit;
                              }
                            strs[i] = copy_string(sb.get_buffer());
                            i++;
                          }
                      }
                    strs[i] = 0;
                    this->attributes = strs;
                  }
              }
      
            if (class_->class_attributes != 0)
              {
                if (include_inherited)
                  {
                    count = class_->class_attribute_count;
                  }
                else
                  {
                    count = 0;
                    /* find the number own by itself */
                    for (a = class_->class_attributes; a != 0; a = (SM_ATTRIBUTE *) a->header.next)
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
                    if (strs == 0)
                      {
                        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                        goto error_exit;
                      }
      
                    i = 0;
                    for (a = class_->class_attributes; a != 0; a = (SM_ATTRIBUTE *) a->header.next)
                      {
                        if (include_inherited || (!include_inherited && a->class_mop == op))
                          {
                            sb.clear();
                            obj_print.describe_attribute(*op, *a, (a->class_mop != op), prt_type, force_print_att_coll);
                            if(sb.len() == 0)
                              {
                                goto error_exit;
                              }
                              strs[i] = copy_string(sb.get_buffer());
                              i++;
                          }
                      }
                    strs[i] = 0;
                    this->class_attributes = strs;
                  }
              }
      
            if (class_->methods != 0)
              {
                if (include_inherited)
                  {
                    count = class_->method_count;
                  }
                else
                  {
                    count = 0;
                    /* find the number own by itself */
                    for (m = class_->methods; m != 0; m = (SM_METHOD *) m->header.next)
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
                    if (strs == 0)
                      {
                        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                        goto error_exit;
                      }
                    i = 0;
                    for (m = class_->methods; m != 0; m = (SM_METHOD *) m->header.next)
                      {
                        if (include_inherited || (!include_inherited && m->class_mop == op))
                          {
                            sb.clear();
                            obj_print.describe_method(*op, *m, prt_type);
                            strs[i] = copy_string(sb.get_buffer());
                            i++;
                          }
                      }
                    strs[i] = 0;
                    this->methods = strs;
                  }
              }
      
            if (class_->class_methods != 0)
              {
                if (include_inherited)
                  {
                    count = class_->class_method_count;
                  }
                else
                  {
                    count = 0;
                    /* find the number own by itself */
                    for (m = class_->class_methods; m != 0; m = (SM_METHOD *) m->header.next)
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
                    if (strs == 0)
                      {
                        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                        goto error_exit;
                      }
                    i = 0;
                    for (m = class_->class_methods; m != 0; m = (SM_METHOD *) m->header.next)
                      {
                        if (include_inherited || (!include_inherited && m->class_mop == op))
                          {
                            sb.clear();
                            obj_print.describe_method(*op, *m, prt_type);
                            strs[i] = copy_string(sb.get_buffer());
                            i++;
                          }
                      }
                    strs[i] = 0;
                    this->class_methods = strs;
                  }
              }
      
            if (class_->resolutions != 0)
              {
                count = ws_list_length ((DB_LIST *) class_->resolutions);
                buf_size = sizeof (char *) * (count + 1);
                strs = (char **) malloc (buf_size);
                if (strs == 0)
                  {
                    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                    goto error_exit;
                  }
                i = 0;
      
                for (SM_RESOLUTION*r = class_->resolutions; r != 0; r = r->next)
                  {
                    sb.clear();
                    obj_print.describe_resolution(*r, prt_type);
                    strs[i] = copy_string(sb.get_buffer());
                    i++;
                  }
                strs[i] = 0;
                this->resolutions = strs;
              }
      
            if (class_->method_files != 0)
              {
                if (include_inherited)
                  {
                    count = ws_list_length ((DB_LIST *) class_->method_files);
                  }
                else
                  {
                    count = 0;
                    /* find the number own by itself */
                    for (SM_METHOD_FILE *f = class_->method_files; f != 0; f = f->next)
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
                    if (strs == 0)
                      {
                        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                        goto error_exit;
                      }
                    i = 0;
                    for (SM_METHOD_FILE *f = class_->method_files; f != 0; f = f->next)
                      {
                        if (include_inherited || (!include_inherited && f->class_mop == op))
                          {
                            sb.clear();
                            obj_print.describe_method_file(*op, *f);
                            strs[i] = copy_string(sb.get_buffer());
                            i++;
                          }
                      }
                    strs[i] = 0;
                    this->method_files = strs;
                  }
              }
      
            if (class_->query_spec != 0)
              {
                count = ws_list_length ((DB_LIST *) class_->query_spec);
                buf_size = sizeof (char *) * (count + 1);
                strs = (char **) malloc (buf_size);
                if (strs == 0)
                  {
                    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                    goto error_exit;
                  }
                i = 0;
                for (p = class_->query_spec; p != 0; p = p->next)
                  {
                    strs[i] = copy_string ((char *) p->specification);
                    i++;
                  }
                strs[i] = 0;
                this->query_spec = strs;
              }
      
            /* these are a bit more complicated */
            this->triggers = (char **) obj_print.describe_class_triggers (*class_, *op);
      
            /* 
             *  Process multi-column class constraints (Unique and Indexes).
             *  Single column constraints (NOT 0) are displayed along with
             *  the attributes.
             */
            this->constraints = 0;	/* initialize */
            if (class_->constraints != 0)
              {
                SM_CLASS_CONSTRAINT *c;
      
                count = 0;
                for (c = class_->constraints; c; c = c->next)
                  {
                    if (SM_IS_CONSTRAINT_INDEX_FAMILY (c->type))
                      {
                        /* Csql schema command will print all constraints, which include the constraints belong to the table
                         * itself and belong to the parent table. But show create table will only print the constraints which 
                         * belong to the table itself. */
                        if (include_inherited
                            || (!include_inherited && c->attributes[0] != 0 && c->attributes[0]->class_mop == op))
                          {
                            count++;
                          }
                      }
                  }
      
                if (count > 0)
                  {
                    buf_size = sizeof (char *) * (count + 1);
                    strs = (char **) malloc (buf_size);
                    if (strs == 0)
                      {
                        er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                        goto error_exit;
                      }
      
                    i = 0;
                    for (c = class_->constraints; c; c = c->next)
                      {
                        if (SM_IS_CONSTRAINT_INDEX_FAMILY (c->type))
                          {
                            if (include_inherited
                                || (!include_inherited && c->attributes[0] != 0 && c->attributes[0]->class_mop == op))
                              {
                                sb.clear();
                                obj_print.describe_constraint(*class_, *c, prt_type);
                                strs[i] = copy_string(sb.get_buffer());
                                if (strs[i] == 0)
                                  {
                                    this->constraints = strs;
                                    goto error_exit;
                                  }
                                i++;
                              }
                          }
                      }
                    strs[i] = 0;
                    this->constraints = strs;
                  }
              }
      
            this->partition = 0;	/* initialize */
            if (class_->partition != 0 && class_->partition->pname == 0)
              {
                bool is_print_partition = true;
      
                count = 0;
      
                /* Show create table will not print the sub partition for hash partition table. */
                if (prt_type == SHOW_CREATE_TABLE)
                  {
                    is_print_partition = (class_->partition->partition_type != PT_PARTITION_HASH);
                  }
      
                if (is_print_partition)
                  {
                    for (user = class_->users; user != 0; user = user->next)
                      {
                        if (au_fetch_class (user->op, &subclass, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
                          {
                            goto error_exit;
                          }
      
                        if (subclass->partition)
                          {
                            sb.clear();
                            obj_print.describe_partition_parts(*subclass->partition, prt_type);
                            PARSER_VARCHAR* descr = pt_append_nulstring(parser, 0, sb.get_buffer());
      
                            /* Temporarily store it into STRLIST, later we will copy it into a fixed length array of which
                             * the size should be determined by the counter of this iteration. */
                            buf_size = sizeof (strlist);
                            tmp_str = (strlist *) malloc (buf_size);
                            if (tmp_str == 0)
                              {
                                er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                                goto error_exit;
                              }
      
                            tmp_str->next = 0;
                            tmp_str->string = (char*)pt_get_varchar_bytes(descr);
      
                            /* Whether it is the first node. */
                            if (str_list_head == 0)
                              {
                                /* Set the head of the list. */
                                str_list_head = tmp_str;
                              }
                            else
                              {
                                /* Link it at the end of the list. */
                                current_str->next = tmp_str;
                              }
      
                            current_str = tmp_str;
      
                            count++;
                          }
      
                      }
                  }
      
                /* Allocate a fixed array to store the strings involving class-partition, sub-partitions and a 0 to
                 * indicate the end position. */
                buf_size = sizeof (char *) * (count + 2);
                strs = (char **) malloc (buf_size);
                if (strs == 0)
                  {
                    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
                    goto error_exit;
                  }
      
                memset (strs, 0, buf_size);
      
                sb.clear();
                obj_print.describe_partition_info(*class_->partition);
                strs[0] = copy_string(sb.get_buffer());
      
                /* Copy all from the list into the array and release the list */
                for (current_str = str_list_head, i = 1; current_str != 0; i++)
                  {
                    strs[i] = copy_string (current_str->string);
      
                    tmp_str = current_str;
                    current_str = current_str->next;
      
                    free_and_init (tmp_str);
                  }
      
                strs[i] = 0;
                this->partition = strs;
              }
      
          }
      
        parser_free_parser (parser);
        parser = 0;		/* Remember, it's a global! */
        return;
      
      error_exit:
      
        for (current_str = str_list_head; current_str != 0;)
          {
            tmp_str = current_str;
            current_str = current_str->next;
            free_and_init (tmp_str);
          }
      
        if (parser)
          {
            parser_free_parser (parser);
            parser = 0;		/* Remember, it's a global! */
          }
}

object_print::class_description::~class_description()
{
        if (name != 0)
        {
          free(name);
        }
        if (class_type != 0)
        {
                free (class_type);
        }
        if (object_id != 0)
        {
                free(object_id);
        }
        if (collation != 0)
        {
                free(collation);
        }
        free_strarray (supers);
        free_strarray (subs);
        free_strarray (attributes);
        free_strarray (class_attributes);
        free_strarray (methods);
        free_strarray (class_methods);
        free_strarray (resolutions);
        free_strarray (method_files);
        free_strarray (query_spec);
        free_strarray (triggers);
        free_strarray (constraints);
        free_strarray (partition);
        if (comment != 0)
        {
                free(comment);
        }
}
