/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * TODO: rename?? merge ??
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "parser.h"
#include "dbi.h"
#include "execute_statement_11.h"

#if defined(WINDOWS)
#include "ustring.h"
#endif /* WINDOWS */

#define IS_NAME(n)      ((n)->node_type == PT_NAME)
#define IS_STRING(n)    ((n)->node_type == PT_VALUE &&          \
                         ((n)->type_enum == PT_TYPE_VARCHAR  || \
                          (n)->type_enum == PT_TYPE_CHAR     || \
                          (n)->type_enum == PT_TYPE_VARNCHAR || \
                          (n)->type_enum == PT_TYPE_NCHAR))
#define GET_NAME(n)     ((char *) (n)->info.name.original)
#define GET_STRING(n)   ((char *) (n)->info.value.data_value.str->bytes)

/*
 * do_grant() - Grants priviledges
 *   return: Error code if grant fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a grant statement
 */
int
do_grant (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *user, *user_list;
  DB_OBJECT *user_obj, *class_;
  PT_NODE *auth_cmd_list, *auth_list, *auth;
  DB_AUTH db_auth;
  PT_NODE *spec_list, *s_list, *spec;
  PT_NODE *entity_list, *entity;
  int grant_option;

  user_list = statement->info.grant.user_list;
  auth_cmd_list = statement->info.grant.auth_cmd_list;
  spec_list = statement->info.grant.spec_list;

  if (statement->info.grant.grant_option == PT_GRANT_OPTION)
    {
      grant_option = true;
    }
  else
    {
      grant_option = false;
    }

  for (user = user_list; user != NULL; user = user->next)
    {
      user_obj = db_find_user (user->info.name.original);
      if (user_obj == NULL)
	{
	  error = er_errid ();
	  return (error);
	}
      auth_list = auth_cmd_list;
      for (auth = auth_list; auth != NULL; auth = auth->next)
	{
	  db_auth = pt_auth_to_db_auth (auth);
	  s_list = spec_list;
	  for (spec = s_list; spec != NULL; spec = spec->next)
	    {
	      entity_list = spec->info.spec.flat_entity_list;
	      for (entity = entity_list; entity != NULL;
		   entity = entity->next)
		{
		  class_ = db_find_class (entity->info.name.original);
		  if (class_ == NULL)
		    {
		      error = er_errid ();
		      return (error);
		    }
		  error = db_grant (user_obj, class_, db_auth, grant_option);
		  if (error != NO_ERROR)
		    return error;
		}
	    }
	}
    }

  return error;
}

/*
 * do_revoke() - Revokes priviledges
 *   return: Error code if revoke fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a revoke statement
 */
int
do_revoke (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;

  PT_NODE *user, *user_list;
  DB_OBJECT *user_obj, *class_;
  PT_NODE *auth_cmd_list, *auth_list, *auth;
  DB_AUTH db_auth;
  PT_NODE *spec_list, *s_list, *spec;
  PT_NODE *entity_list, *entity;

  user_list = statement->info.revoke.user_list;
  auth_cmd_list = statement->info.revoke.auth_cmd_list;
  spec_list = statement->info.revoke.spec_list;

  for (user = user_list; user != NULL; user = user->next)
    {
      user_obj = db_find_user (user->info.name.original);
      if (user_obj == NULL)
	{
	  error = er_errid ();
	  return (error);
	}
      auth_list = auth_cmd_list;
      for (auth = auth_list; auth != NULL; auth = auth->next)
	{
	  db_auth = pt_auth_to_db_auth (auth);
	  s_list = spec_list;
	  for (spec = s_list; spec != NULL; spec = spec->next)
	    {
	      entity_list = spec->info.spec.flat_entity_list;
	      for (entity = entity_list; entity != NULL;
		   entity = entity->next)
		{
		  class_ = db_find_class (entity->info.name.original);
		  if (class_ == NULL)
		    {
		      error = er_errid ();
		      return (error);
		    }
		  error = db_revoke (user_obj, class_, db_auth);
		  if (error != NO_ERROR)
		    return error;
		}
	    }
	}
    }

  return error;
}

/*
 * do_create_user() - Create a user
 *   return: Error code if creation fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a create user statement
 */
int
do_create_user (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OBJECT *user, *group, *member;
  int exists;
  PT_NODE *node, *node2;
  const char *user_name, *password;
  const char *group_name, *member_name;

  user = NULL;
  node = statement->info.create_user.user_name;
  user_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;

  /* first, check if user_name is in group or member clause */
  for (node = statement->info.create_user.groups;
       node != NULL; node = node->next)
    {
      group_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
      if (strcasecmp (user_name, group_name) == 0)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_AU_MEMBER_CAUSES_CYCLES, 0);
	  return ER_AU_MEMBER_CAUSES_CYCLES;
	}
    }

  for (node = statement->info.create_user.members;
       node != NULL; node = node->next)
    {
      member_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
      if (strcasecmp (user_name, member_name) == 0 ||
	  strcasecmp (member_name, AU_PUBLIC_USER_NAME) == 0)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_AU_MEMBER_CAUSES_CYCLES, 0);
	  return ER_AU_MEMBER_CAUSES_CYCLES;
	}
    }

  /* second, check if group name is in member clause */
  for (node = statement->info.create_user.groups;
       node != NULL; node = node->next)
    {
      group_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
      for (node2 = statement->info.create_user.members;
	   node2 != NULL; node2 = node2->next)
	{
	  member_name = (node2 && IS_NAME (node2)) ? GET_NAME (node2) : NULL;
	  if (strcasecmp (group_name, member_name) == 0)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_AU_MEMBER_CAUSES_CYCLES, 0);
	      return ER_AU_MEMBER_CAUSES_CYCLES;
	    }
	}
    }

  if (!parser || !statement || !user_name)
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
  else
    {
      exists = 0;
      user = db_add_user (user_name, &exists);
      if (user == NULL)
	{
	  error = er_errid ();
	}
      else if (exists)
	{
	  error = ER_AU_USER_EXISTS;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, user_name);
	}
      else
	{
	  node = statement->info.create_user.password;
	  password = (node && IS_STRING (node)) ? GET_STRING (node) : NULL;
	  if (error == NO_ERROR && password)
	    {
	      error = au_set_password (user, password);
	    }

	  node = statement->info.create_user.groups;
	  group_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
	  if (error == NO_ERROR && group_name)
	    do
	      {
		group = db_find_user (group_name);
		if (group == NULL)
		  {
		    error = er_errid ();
		  }
		else
		  {
		    error = db_add_member (group, user);
		  }
		node = node->next;
		group_name = (node
			      && IS_NAME (node)) ? GET_NAME (node) : NULL;
	      }
	    while (error == NO_ERROR && group_name);

	  node = statement->info.create_user.members;
	  member_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
	  if (error == NO_ERROR && member_name)
	    {
	      do
		{
		  member = db_find_user (member_name);
		  if (member == NULL)
		    {
		      error = er_errid ();
		    }
		  else
		    {
		      error = db_add_member (user, member);
		    }
		  node = node->next;
		  member_name = (node
				 && IS_NAME (node)) ? GET_NAME (node) : NULL;
		}
	      while (error == NO_ERROR && member_name);
	    }
	}
      if (error != NO_ERROR)
	{
	  if (user && exists == 0)
	    {
	      er_stack_push ();
	      db_drop_user (user);
	      er_stack_pop ();
	    }
	}
    }
  return error;
}

/*
 * do_drop_user() - Drop the user
 *   return: Error code if dropping fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a drop user statement
 */
int
do_drop_user (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OBJECT *user;
  PT_NODE *node;
  const char *user_name;

  node = statement->info.create_user.user_name;
  user_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
  if (!parser || !statement || !user_name)
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
  else
    {
      user = db_find_user (user_name);
      if (user == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  error = db_drop_user (user);
	}
    }
  return error;
}

/*
 * do_alter_user() - Change the user's password
 *   return: Error code if alter fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of an alter statement
 */
int
do_alter_user (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OBJECT *user;
  PT_NODE *node;
  const char *user_name, *password;

  node = statement->info.alter_user.user_name;
  user_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
  if (!parser || !statement || !user_name)
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
  else
    {
      user = db_find_user (user_name);
      if (user == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  node = statement->info.alter_user.password;
	  password = (node && IS_STRING (node)) ? GET_STRING (node) : NULL;
	  error = au_set_password (user, password);
	}
    }
  return error;
}
