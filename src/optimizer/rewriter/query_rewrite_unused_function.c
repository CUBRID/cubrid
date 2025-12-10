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
 * query_rewrite_backup.c
 */

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * qo_is_partition_attr () -
 *   return:
 *   node(in):
 */
static int
qo_is_partition_attr (PT_NODE * node)
{
  if (node == NULL)
    {
      return 0;
    }

  node = pt_get_end_path_node (node);

  if (node->node_type == PT_NAME && node->info.name.meta_class == PT_NORMAL && node->info.name.spec_id)
    {
      if (node->info.name.partition)
	{
	  return 1;
	}
    }

  return 0;
}

/*
 * qo_convert_path_to_name () -
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   node(in): node to test for path conversion
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
qo_convert_path_to_name (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) arg;
  PT_NODE *name;

  if (node->node_type == PT_DOT_ && (name = node->info.dot.arg2) && name->node_type == PT_NAME
      && name->info.name.spec_id == spec->info.spec.id)
    {
      node->info.dot.arg2 = NULL;
      name->next = node->next;
      node->next = NULL;
      parser_free_tree (parser, node);
      node = name;
      if (spec->info.spec.range_var)
	{
	  name->info.name.resolved = spec->info.spec.range_var->info.name.original;
	}
    }
  return node;
}

/*
 * qo_rewrite_as_join () - Given a statement, a path root, a path spec ptr,
 *			rewrite the statement into a join with the path spec
 *   return:
 *   parser(in):
 *   root(in):
 *   statement(in):
 *   path_spec_ptr(in):
 */
static void
qo_rewrite_as_join (PARSER_CONTEXT * parser, PT_NODE * root, PT_NODE * statement, PT_NODE ** path_spec_ptr)
{
  PT_NODE *path_spec;
  PT_NODE *conjunct;

  path_spec = *path_spec_ptr;

  conjunct = path_spec->info.spec.path_conjuncts;
  path_spec->info.spec.path_conjuncts = NULL;
  *path_spec_ptr = path_spec->next;
  path_spec->next = root->next;
  root->next = path_spec;
  statement->info.query.q.select.where = parser_append_node (conjunct, statement->info.query.q.select.where);
  statement = parser_walk_tree (parser, statement, qo_convert_path_to_name, path_spec, NULL, NULL);
}

/*
 * qo_rewrite_as_derived () - Given a statement, a path root, a path spec ptr,
 *			   rewrite the spec to be a table derived from a join
 *			   of the path_spec table and the root table
 *   return:
 *   parser(in):
 *   root(in):
 *   root_where(in):
 *   statement(in):
 *   path_spec_ptr(in):
 */
static void
qo_rewrite_as_derived (PARSER_CONTEXT * parser, PT_NODE * root, PT_NODE * root_where, PT_NODE * statement,
		       PT_NODE ** path_spec_ptr)
{
  PT_NODE *path_spec;
  PT_NODE *conjunct;
  PT_NODE *new_spec;
  PT_NODE *new_root;
  PT_NODE *query;
  PT_NODE *temp;

  path_spec = *path_spec_ptr;
  new_spec = parser_copy_tree (parser, path_spec);
  if (new_spec == NULL)
    {
      PT_INTERNAL_ERROR (parser, "copy tree");
      return;
    }

  conjunct = new_spec->info.spec.path_conjuncts;
  new_spec->info.spec.path_conjuncts = NULL;

  if (root->info.spec.derived_table)
    {
      /* if the root spec is a derived table query, construct a derived table query for this path spec by building on
       * top of that. This will be the case for outer path expressions 2 or more deep. */
      query = parser_copy_tree (parser, root->info.spec.derived_table);
      if (query == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "copy tree");
	  return;
	}

      new_root = query->info.query.q.select.from;
      parser_free_tree (parser, query->info.query.q.select.list);
    }
  else
    {
      /* if the root spec is a class spec, construct a derived table query for this path spec from scratch. */
      new_root = parser_copy_tree (parser, root);
      query = parser_new_node (parser, PT_SELECT);

      if (query == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return;
	}

      query->info.query.q.select.from = new_root;
      query->info.query.correlation_level = 0;
    }
  new_root = parser_append_node (new_spec, new_root);
  query->info.query.all_distinct = PT_DISTINCT;
  query->info.query.q.select.where = parser_append_node (root_where, query->info.query.q.select.where);
  query->info.query.q.select.where = parser_append_node (conjunct, query->info.query.q.select.where);
  temp = query->info.query.q.select.list = parser_copy_tree_list (parser, path_spec->info.spec.referenced_attrs);
  while (temp)
    {
      /* force all the names to be fully qualified */
      temp->info.name.resolved = new_spec->info.spec.range_var->info.name.original;
      temp = temp->next;
    }
  query->info.query.is_subquery = PT_IS_SUBQUERY;
  mq_regenerate_if_ambiguous (parser, new_spec, query, new_root);
  mq_set_references (parser, query, new_spec);
  mq_set_references (parser, query, new_root);

  /* Here we set up positional correspondance to the derived queries select list, but we must preserve the spec
   * identity of the path_spec, so we copy the original referenced attrs, not the copied/reset list. */
  temp = path_spec->info.spec.as_attr_list = parser_copy_tree_list (parser, path_spec->info.spec.referenced_attrs);
  while (temp)
    {
      temp->info.name.resolved = NULL;
      temp = temp->next;
    }

  parser_free_tree (parser, path_spec->info.spec.entity_name);
  path_spec->info.spec.entity_name = NULL;
  parser_free_tree (parser, path_spec->info.spec.flat_entity_list);
  path_spec->info.spec.flat_entity_list = NULL;

  path_spec->info.spec.derived_table = query;
  path_spec->info.spec.derived_table_type = PT_IS_SUBQUERY;
}
#endif /* ENABLE_UNUSED_FUNCTION */
