/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_drop.c : Code for dropping a Classes by Parse Tree descriptions.
 * TODO: rename??? merge ???
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "parser.h"
#include "dbi.h"
#include "execute_schema_8.h"
#include "execute_statement_11.h"
#include "system_parameter.h"

static int drop_class_name (const char *name);

/*
 * drop_class_name() - This static routine drops a class by name.
 *   return: Error code
 *   name(in): Class name to drop
 */
static int
drop_class_name (const char *name)
{
  DB_OBJECT *class_;

  class_ = db_find_class (name);

  if (class_)
    {
      return db_drop_class (class_);
    }
  else
    {
      /* if class is null, return the global error. */
      return er_errid ();
    }
}

/*
 * do_drop() - Drops a vclass, class, or ldbvclass
 *   return: Error code if a vclass is not deleted.
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a drop statement
 */
int
do_drop (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *entity_spec_list, *entity_spec;
  PT_NODE *entity;
  PT_NODE *entity_list;

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  /* partitioned sub-class check */
  entity_spec_list = statement->info.drop.spec_list;
  for (entity_spec = entity_spec_list; entity_spec != NULL;
       entity_spec = entity_spec->next)
    {
      entity_list = entity_spec->info.spec.flat_entity_list;
      for (entity = entity_list; entity != NULL; entity = entity->next)
	{
	  if (do_is_partitioned_subclass
	      (NULL, entity->info.name.original, NULL))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_INVALID_PARTITION_REQUEST, 0);
	      return er_errid ();
	    }
	}
    }
  entity_spec_list = statement->info.drop.spec_list;
  for (entity_spec = entity_spec_list; entity_spec != NULL;
       entity_spec = entity_spec->next)
    {
      entity_list = entity_spec->info.spec.flat_entity_list;
      for (entity = entity_list; entity != NULL; entity = entity->next)
	{
	  error = drop_class_name (entity->info.name.original);
	  if (error != NO_ERROR)
	    return error;
	}
    }
  return error;
}

/*  i think that do_rename is a bogus function which won't work
    -- must fix later. */
/*
 * do_rename() - Drops a vclass, class, or ldbvclass
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a rename statement
 */
int
do_rename (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  const char *new_name, *old_name;
  DB_OBJECT *old_class;

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  old_name = statement->info.rename.old_name->info.name.original;
  new_name = statement->info.rename.new_name->info.name.original;
  if (do_is_partitioned_subclass (NULL, old_name, NULL))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_PARTITION_REQUEST,
	      0);
      return er_errid ();
    }

  old_class = db_find_class (old_name);
  if (old_class == NULL)
    {
      return er_errid ();
    }
  else
    error = db_rename_class (old_class, new_name);

  return error;
}
