/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_index.c - Parse tree to index commands translation.
 */

#ident "$Id$"

#include "config.h"

#include "db.h"
#include "error_manager.h"
#include "parser.h"
#include "schema_manager_3.h"
#include "semantic_check.h"
#include "system_parameter.h"
#include "execute_statement_11.h"

typedef enum
{
  CREATE = 0, DROP
} DO_INDEX;

static int create_or_drop_index_helper (const PARSER_CONTEXT * parser,
					const PT_NODE * statement,
					DB_OBJECT * obj,
					const DO_INDEX do_index);
/* 
 * create_or_drop_index_helper()
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): A create or drop index statement
 *   obj(in): Class object
 *   do_index(in) : Flag to indicate CREATE or DROP 
 *
 * Note: If you feel the need
 */
static int
create_or_drop_index_helper (const PARSER_CONTEXT * parser,
			     const PT_NODE * statement,
			     DB_OBJECT * obj, const DO_INDEX do_index)
{
  int error = NO_ERROR;

  int i, nnames;
  DB_CONSTRAINT_TYPE ctype;
  PT_NODE *c, *n;
  const char **attnames;
  int *asc_desc;
  char *cname;

  nnames = pt_length_of_list (statement->info.index.column_names);
  attnames = (const char **) malloc ((nnames + 1) * sizeof (char *));
  asc_desc = (int *) malloc ((nnames) * sizeof (int));
  if (attnames == NULL || asc_desc == NULL)
    {
      return er_errid ();
    }

  for (c = statement->info.index.column_names, i = 0; c != NULL;
       c = c->next, i++)
    {
      asc_desc[i] = c->info.sort_spec.asc_or_desc == PT_ASC ? 0 : 1;
      n = c->info.sort_spec.expr;	/* column name node */
      attnames[i] = n->info.name.original;
    }
  attnames[i] = NULL;

  c = statement->info.index.index_name;
  if (statement->info.index.unique)
    {
      ctype = (statement->info.index.reverse == false) ?
	DB_CONSTRAINT_UNIQUE : DB_CONSTRAINT_REVERSE_UNIQUE;
    }
  else
    {
      ctype = (statement->info.index.reverse == false) ?
	DB_CONSTRAINT_INDEX : DB_CONSTRAINT_REVERSE_INDEX;
    }

  cname = sm_produce_constraint_name (sm_class_name (obj), ctype,
				      attnames, asc_desc,
				      (c ? c->info.name.original : NULL));
  if (cname == NULL)
    {
      error = er_errid ();
    }
  else
    {
      if (do_index == CREATE)
	{
	  error =
	    sm_add_constraint (obj, ctype, cname, attnames, asc_desc, false);
	}
      else			/* do_index == DROP */
	{
	  error = sm_drop_constraint (obj, ctype, cname, attnames, false);
	}
      sm_free_constraint_name (cname);
    }

  free_and_init (attnames);
  free_and_init (asc_desc);

  return error;

}

/* 
 * do_create_index() - Creates an index
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statemnet(in) : Parse tree of a create index statement
 */
int
do_create_index (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  PT_NODE *cls;
  DB_OBJECT *obj;

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  cls = statement->info.index.indexed_class;
  obj = db_find_class (cls->info.name.original);
  if (obj == NULL)
    {
      return er_errid ();
    }

  return create_or_drop_index_helper (parser, statement, obj, CREATE);

}				/* do_create_index */

/* 
 * do_drop_index() - Drops an index on a class.
 *   return: Error code if it fails
 *   parser(in) : Parser context
 *   statement(in): Parse tree of a drop index statement
 */
int
do_drop_index (PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  PT_NODE *cls;
  DB_OBJECT *obj;

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  cls = statement->info.index.indexed_class;
  if (!cls)
    {
      PT_NODE *c;
      DB_CONSTRAINT_TYPE ctype;

      c = statement->info.index.index_name;
      if (!c || !c->info.name.original)
	{
	  return ER_SM_INVALID_DEF_CONSTRAINT_NAME_PARAMS;
	}
      if (statement->info.index.unique)
	{
	  ctype = (statement->info.index.reverse == false) ?
	    DB_CONSTRAINT_UNIQUE : DB_CONSTRAINT_REVERSE_UNIQUE;
	}
      else
	{
	  ctype = (statement->info.index.reverse == false) ?
	    DB_CONSTRAINT_INDEX : DB_CONSTRAINT_REVERSE_INDEX;
	}
      cls = pt_find_class_of_index (parser, c, ctype);
      if (!cls)
	{
	  return er_errid ();
	}
    }
  obj = db_find_class (cls->info.name.original);
  if (obj == NULL)
    {
      return er_errid ();
    }

  cls->info.name.db_object = obj;
  pt_check_user_owns_class (parser, cls);

  return create_or_drop_index_helper (parser, statement, obj, DROP);

}

/* 
 * do_alter_index() - Alters an index on a class.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a alter index statement
 */
int
do_alter_index (PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;

  DB_OBJECT *obj;
  PT_NODE *cls, *c, *n;
  int i, nnames;
  DB_CONSTRAINT_TYPE ctype;
  char **attnames;
  int *asc_desc;
  char *cname;
  const char *idx_name;
  SM_CLASS *smcls;
  SM_CLASS_CONSTRAINT *idx;
  SM_ATTRIBUTE **attp;
  int attnames_allocated = 0;
  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  cls = statement->info.index.indexed_class;
  if (!cls)
    {
      c = statement->info.index.index_name;
      if (!c || !c->info.name.original)
	{
	  return ER_SM_INVALID_DEF_CONSTRAINT_NAME_PARAMS;
	}
      if (statement->info.index.unique)
	{
	  ctype = (statement->info.index.reverse == false) ?
	    DB_CONSTRAINT_UNIQUE : DB_CONSTRAINT_REVERSE_UNIQUE;
	}
      else
	{
	  ctype = (statement->info.index.reverse == false) ?
	    DB_CONSTRAINT_INDEX : DB_CONSTRAINT_REVERSE_INDEX;
	}
      cls = pt_find_class_of_index (parser, c, ctype);
      if (!cls)
	{
	  return er_errid ();
	}
    }
  obj = db_find_class (cls->info.name.original);
  if (obj)
    {
      cls->info.name.db_object = obj;
      pt_check_user_owns_class (parser, cls);
    }
  else
    {
      return er_errid ();
    }

  if (statement->info.index.column_names == NULL)
    {
      /* find the attributes of the index */
      c = statement->info.index.index_name;
      idx_name = c->info.name.original;
      idx = NULL;
      if (au_fetch_class (obj, &smcls, AU_FETCH_READ, AU_SELECT) == NO_ERROR
	  && (idx = classobj_find_class_index (smcls, idx_name)) != NULL)
	{
	  attp = idx->attributes;
	  if (attp == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OBJ_INVALID_ATTRIBUTE, 1, "unknown");
	      return er_errid ();
	    }
	  nnames = 0;
	  while (*attp++)
	    {
	      nnames++;
	    }
	  attnames = (char **) malloc ((nnames + 1) * sizeof (char *));
	  if (attnames == NULL)
	    {
	      return er_errid ();
	    }
	  attnames_allocated = 1;
	  for (i = 0, attp = idx->attributes; *attp; i++, attp++)
	    {
	      attnames[i] = strdup ((*attp)->header.name);
	    }
	  attnames[i] = NULL;
	}
    }
  else
    {
      nnames = pt_length_of_list (statement->info.index.column_names);
      attnames = (char **) malloc ((nnames + 1) * sizeof (char *));
      asc_desc = (int *) malloc ((nnames) * sizeof (int));
      if (attnames == NULL || asc_desc == NULL)
	{
	  return er_errid ();
	}

      for (c = statement->info.index.column_names, i = 0; c != NULL;
	   c = c->next, i++)
	{
	  asc_desc[i] = c->info.sort_spec.asc_or_desc == PT_ASC ? 0 : 1;
	  n = c->info.sort_spec.expr;	/* column name node */
	  attnames[i] = (char *) n->info.name.original;
	}
      attnames[i] = NULL;
    }

  c = statement->info.index.index_name;
  if (statement->info.index.unique)
    {
      ctype = (statement->info.index.reverse == false) ?
	DB_CONSTRAINT_UNIQUE : DB_CONSTRAINT_REVERSE_UNIQUE;
    }
  else
    {
      ctype = (statement->info.index.reverse == false) ?
	DB_CONSTRAINT_INDEX : DB_CONSTRAINT_REVERSE_INDEX;
    }

  cname = sm_produce_constraint_name (sm_class_name (obj), ctype,
				      (const char **) attnames, asc_desc,
				      (c ? c->info.name.original : NULL));
  if (cname == NULL)
    {
      if (error == NO_ERROR && (error = er_errid ()) == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	}
    }
  else
    {
      error = sm_drop_constraint (obj, ctype, cname, (const char **) attnames,
				  false);
      if (error == NO_ERROR)
	{
	  error = sm_add_constraint (obj, ctype, cname,
				     (const char **) attnames, asc_desc,
				     false);
	}
      sm_free_constraint_name (cname);
    }

  if (attnames_allocated)
    {
      for (i = 0; attnames[i]; i++)
	{
	  free_and_init (attnames[i]);
	}
    }

  free_and_init (attnames);
  free_and_init (asc_desc);

  return error;
}
