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
 * scanner_support.c - scanner support functions
 */

#ident "$Id$"

#include "config.h"

#include <math.h>

#include "porting.h"
#include "parser_message.h"

#define JP_MAXNAME          256

#include "parser.h"
#include "chartype.h"
#include "language_support.h"
#include "intl_support.h"
#include "csql_grammar_scan.h"
#include "memory_alloc.h"
#include "misc_string.h"

#define IS_WHITE_CHAR(c) \
                    ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\0')
#define IS_WHITE_SPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')

#define IS_HINT_ON_TABLE(h) \
  		((h) & (PT_HINT_INDEX_SS | PT_HINT_INDEX_LS))

static char *pt_trim_as_identifier (char *name);

//#define PRINT_HIT_HINT_INFO   /* When declared, matching hint information is output to the screen. */
#if defined(PRINT_HIT_HINT_INFO)
static void debug_hit_hint_print (PT_HINT * hint_table);
#define PRINT_HIT_HINT(...)  printf(__VA_ARGS__)
#else
#define  debug_hit_hint_print(hint)
#define PRINT_HIT_HINT(...)
#endif

#define HINT_LEAD_CHAR_SIZE (129)
static u_char hint_table_lead_offset[HINT_LEAD_CHAR_SIZE] = { 0, };

int parser_input_host_index = 0;
int parser_statement_OK = 0;
PARSER_CONTEXT *this_parser;
int parser_output_host_index = 0;
extern "C"
{
  extern int yycolumn;
  extern int csql_yyget_lineno ();
}
extern int yycolumn_end;

/*
 * pt_makename () -
 *   return:
 *   name(in):
 */
char *
pt_makename (const char *name)
{
  return pt_append_string (this_parser, NULL, name);
}

/*
 * pt_trim_as_identifier () - trim double quotes,
 *            square brackets, or backtick symbol
 *   return:
 *   name(in):
 */
static char *
pt_trim_as_identifier (char *name)
{
  char *tmp_name;
  size_t len;

  len = strlen (name);
  if (len >= 2
      && ((name[0] == '[' && name[len - 1] == ']') || (name[0] == '`' && name[len - 1] == '`')
	  || (name[0] == '"' && name[len - 1] == '"')))
    {
      tmp_name = pt_makename (name);
      tmp_name[len - 1] = '\0';
      tmp_name += 1;

      return tmp_name;
    }
  else
    {
      return name;
    }
}

/*
 * pt_makename_trim_as_identifier () - trim double quotes,
 *            square brackets, or backtick symbol
 *   return:
 *   name(in):
 */
static char *
pt_makename_trim_as_identifier (char *name)
{
  char *tmp_name, *pnew, chBk;
  size_t len;

  len = strlen (name);
  if (len >= 2
      && ((name[0] == '[' && name[len - 1] == ']') || (name[0] == '`' && name[len - 1] == '`')
	  || (name[0] == '"' && name[len - 1] == '"')))
    {
      chBk = name[len - 1];
      name[len - 1] = '\0';
      pnew = pt_makename (name + 1);
      name[len - 1] = chBk;

      return pnew;
    }
  else
    {
      return pt_makename (name);
    }
}

/*
 * pt_parser_line_col () - set line and column of node allocated to
 *                         current parse position
 *   return:
 *   node(in): a PT_NODE ptr
 */
void
pt_parser_line_col (PT_NODE * node)
{
  if (node == NULL)
    return;

  node->line_number = csql_yyget_lineno ();
  node->column_number = yycolumn;
}

/*
 * pt_nextchar () - called by the scanner
 *   return: c the next character from input. -1   at end of file
 */
int
pt_nextchar (void)
{
  int c = (this_parser->next_char) (this_parser);
  return c;
}

static int
hint_token_cmp (const void *a, const void *b)
{
  return strcmp (((const PT_HINT *) a)->tokens, ((const PT_HINT *) b)->tokens);
}

/*
 * pt_initialize_hint () - initialize hint string
 *   parser(in): parser context
 *   hint_table(in): hint table to initialize
 */
void
pt_initialize_hint (PARSER_CONTEXT * parser, PT_HINT hint_table[])
{
  static int was_initialized = 0;

  if (was_initialized)
    {
      return;
    }

  int i;

  memset (hint_table_lead_offset, 0x00, sizeof (hint_table_lead_offset));
  for (i = 0; hint_table[i].tokens; i++)
    {
#ifndef NDEBUG
      char *p;
      for (p = (char *) hint_table[i].tokens; *p; p++)
	{
	  assert (toupper (*p) == *p);
	}
#endif
      hint_table[i].is_hit = false;
      hint_table[i].length = (int) strlen (hint_table[i].tokens);
      hint_table_lead_offset[(unsigned char) (hint_table[i].tokens[0])]++;
    }

  // ordering by asc 
  qsort (hint_table, i, sizeof (hint_table[0]), &hint_token_cmp);

  // Cumulative Distribution Counting
  int sum = 0;
  int tCnt = hint_table_lead_offset[0];
  for (i = 0; i < HINT_LEAD_CHAR_SIZE; i++)
    {
      tCnt = hint_table_lead_offset[i];
      hint_table_lead_offset[i] = sum;
      sum += tCnt;
    }

  // Copy for lower character
  for (i = 'A'; i <= 'Z'; i++)
    {
      hint_table_lead_offset[i + 32 /*('a'-'A') */ ] = hint_table_lead_offset[i];
    }

  was_initialized = 1;
}

/*
 * pt_cleanup_hint () - cleanup hint arg_list
 *   parser(in): parser context
 *   hint_table(in): hint table to cleanup
 */
void
pt_cleanup_hint (PARSER_CONTEXT * parser, PT_HINT hint_table[])
{
  int i;
  for (i = 0; hint_table[i].tokens; i++)
    {
      if (hint_table[i].arg_list != NULL)
	{
	  parser_free_node (parser, hint_table[i].arg_list);
	  hint_table[i].arg_list = NULL;
	}
    }
}

/*
 * pt_get_hint () - get hint value from hint table
 *   text(in): hint text
 *   hint_table(in): hint info structure list
 *   node(in): node which will take hint context
 */
void
pt_get_hint (const char *text, PT_HINT hint_table[], PT_NODE * node)
{
  int i;

  PRINT_HIT_HINT ("(HINT HIT) ");

  /* read hint info */
  for (i = 0; hint_table[i].tokens; i++)
    {
      if (hint_table[i].is_hit)
	{
	  debug_hit_hint_print (hint_table + i);

	  switch (hint_table[i].hint)
	    {
	    case PT_HINT_NONE:
	      break;
	    case PT_HINT_ORDERED:	/* force join left-to-right */

/* TEMPORARY COMMENTED CODE: DO NOT REMOVE ME !!! */
#if 0
	      node->info.query.q.select.ordered = hint_table[i].arg_list;
	      hint_table[i].arg_list = NULL;
#endif /* 0 */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint = (PT_HINT_ENUM) (node->info.delete_.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint = (PT_HINT_ENUM) (node->info.update.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_NO_INDEX_SS:	/* disable index skip scan */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_INDEX_SS:
	      if (node->node_type == PT_SELECT)
		{
		  if (hint_table[i].arg_list != NULL && PT_IS_NULL_NODE (hint_table[i].arg_list))
		    {
		      /* For INDEX_SS(), just ignore index skip scan hint */
		      node->info.query.q.select.hint =
			(PT_HINT_ENUM) (node->info.query.q.select.hint & ~PT_HINT_INDEX_SS);
		      node->info.query.q.select.index_ss = NULL;
		    }
		  else
		    {
		      node->info.query.q.select.hint =
			(PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		      node->info.query.q.select.index_ss = hint_table[i].arg_list;
		      hint_table[i].arg_list = NULL;
		    }
		}
	      break;
#if 0
	    case PT_HINT_Y:	/* not used */
	      break;
#endif /* 0 */
	    case PT_HINT_USE_NL:	/* force nl-join */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		  node->info.query.q.select.use_nl = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint = (PT_HINT_ENUM) (node->info.delete_.hint | hint_table[i].hint);
		  node->info.delete_.use_nl_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint = (PT_HINT_ENUM) (node->info.update.hint | hint_table[i].hint);
		  node->info.update.use_nl_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_USE_IDX:	/* force idx-join */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		  node->info.query.q.select.use_idx = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint = (PT_HINT_ENUM) (node->info.delete_.hint | hint_table[i].hint);
		  node->info.delete_.use_idx_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint = (PT_HINT_ENUM) (node->info.update.hint | hint_table[i].hint);
		  node->info.update.use_idx_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_USE_MERGE:	/* force m-join */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		  node->info.query.q.select.use_merge = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint = (PT_HINT_ENUM) (node->info.delete_.hint | hint_table[i].hint);
		  node->info.delete_.use_merge_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint = (PT_HINT_ENUM) (node->info.update.hint | hint_table[i].hint);
		  node->info.update.use_merge_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
#if 0
	    case PT_HINT_USE_HASH:	/* not used */
	      break;
#endif /* 0 */
	    case PT_HINT_RECOMPILE:	/* recompile */
	      node->flag.recompile = 1;
	      break;
	    case PT_HINT_LK_TIMEOUT:	/* lock timeout */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		  node->info.query.q.select.waitsecs_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint = (PT_HINT_ENUM) (node->info.update.hint | hint_table[i].hint);
		  node->info.update.waitsecs_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint = (PT_HINT_ENUM) (node->info.delete_.hint | hint_table[i].hint);
		  node->info.delete_.waitsecs_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_INSERT)
		{
		  node->info.insert.hint = (PT_HINT_ENUM) (node->info.insert.hint | hint_table[i].hint);
		  node->info.insert.waitsecs_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_MERGE)
		{
		  node->info.merge.hint = (PT_HINT_ENUM) (node->info.merge.hint | hint_table[i].hint);
		  node->info.merge.waitsecs_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_NO_LOGGING:	/* no logging */
	      if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint = (PT_HINT_ENUM) (node->info.update.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint = (PT_HINT_ENUM) (node->info.delete_.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_INSERT)
		{
		  node->info.insert.hint = (PT_HINT_ENUM) (node->info.insert.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_MERGE)
		{
		  node->info.merge.hint = (PT_HINT_ENUM) (node->info.merge.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_QUERY_CACHE:	/* query_cache */
	      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
		{
		  node->info.query.hint = (PT_HINT_ENUM) (node->info.query.hint | hint_table[i].hint);
		  node->info.query.qcache_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		  if (node->info.query.qcache_hint)
		    {
		      if (atoi (node->info.query.qcache_hint->info.name.original))
			node->info.query.flag.do_cache = 1;
		      else
			node->info.query.flag.do_not_cache = 1;
		    }
		  else
		    {
		      node->info.query.flag.do_cache = 1;
		    }
		}
	      break;
#if 0
	    case PT_HINT_QUERY_NO_CACHE:
	      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
		{
		  node->info.query.hint = (PT_HINT_ENUM) (node->info.query.hint | hint_table[i].hint);
		  node->info.query.qcache_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		  /* force not use the query cache */
		  node->info.query.flag.reexecute = 1;
		  node->info.query.flag.do_cache = 0;
		  node->info.query.flag.do_not_cache = 1;
		}
	      break;
#endif
	    case PT_HINT_REEXECUTE:	/* reexecute */
	      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
		{
		  node->info.query.hint = (PT_HINT_ENUM) (node->info.query.hint | hint_table[i].hint);
		  node->info.query.flag.reexecute = 1;
		}
	      break;
	    case PT_HINT_JDBC_CACHE:	/* jdbc cache */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		  node->info.query.q.select.jdbc_life_time = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_USE_IDX_DESC:	/* descending index scan */
	    case PT_HINT_NO_COVERING_IDX:	/* do not use covering index scan */
	    case PT_HINT_NO_IDX_DESC:	/* do not use descending index scan */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint = (PT_HINT_ENUM) (node->info.delete_.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint = (PT_HINT_ENUM) (node->info.update.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_INSERT_MODE:
	      if (node->node_type == PT_INSERT)
		{
		  node->info.insert.hint = (PT_HINT_ENUM) (node->info.insert.hint | hint_table[i].hint);
		  node->info.insert.insert_mode = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_NO_MULTI_RANGE_OPT:
	    case PT_HINT_NO_SORT_LIMIT:
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint = (PT_HINT_ENUM) (node->info.delete_.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint = (PT_HINT_ENUM) (node->info.update.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_USE_UPDATE_IDX:
	      if (node->node_type == PT_MERGE)
		{
		  node->info.merge.hint = (PT_HINT_ENUM) (node->info.merge.hint | hint_table[i].hint);
		  node->info.merge.update.index_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_USE_INSERT_IDX:
	      if (node->node_type == PT_MERGE)
		{
		  node->info.merge.hint = (PT_HINT_ENUM) (node->info.merge.hint | hint_table[i].hint);
		  node->info.merge.insert.index_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_NO_HASH_AGGREGATE:
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_NO_HASH_LIST_SCAN:
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_NO_PUSH_PRED:
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_SKIP_UPDATE_NULL:
	      if (node->node_type == PT_ALTER)
		{
		  node->info.alter.hint = (PT_HINT_ENUM) (node->info.alter.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_NO_INDEX_LS:	/* disable loose index scan */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_INDEX_LS:	/* enable loose index scan */
	      if (node->node_type == PT_SELECT)
		{
		  if (hint_table[i].arg_list != NULL && PT_IS_NULL_NODE (hint_table[i].arg_list))
		    {
		      /* For INDEX_LS(), just ignore loose index scan hint */
		      node->info.query.q.select.hint =
			(PT_HINT_ENUM) (node->info.query.q.select.hint & ~PT_HINT_INDEX_LS);
		      node->info.query.q.select.index_ls = NULL;
		    }
		  else
		    {
		      node->info.query.q.select.hint =
			(PT_HINT_ENUM) (node->info.query.q.select.hint | PT_HINT_INDEX_LS);
		      node->info.query.q.select.index_ls = hint_table[i].arg_list;
		      hint_table[i].arg_list = NULL;
		    }
		}
	      break;
	    case PT_HINT_SELECT_RECORD_INFO:
	    case PT_HINT_SELECT_PAGE_INFO:
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_SELECT_KEY_INFO:
	    case PT_HINT_SELECT_BTREE_NODE_INFO:
	      if (node->node_type == PT_SELECT)
		{
		  /* SELECT_KEY_INFO hint can work if it has one and only one index name as argument. Same for
		   * SELECT_BTREE_NODE_INFO. Ignore hint if this condition is not met. */
		  if (hint_table[i].arg_list == NULL || hint_table[i].arg_list->next != NULL)
		    {
		      break;
		    }
		  node->info.query.q.select.hint = (PT_HINT_ENUM) (node->info.query.q.select.hint | hint_table[i].hint);
		  node->info.query.q.select.using_index = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_USE_SBR:	/* statement-based replication */
	      if (node->node_type == PT_INSERT)
		{
		  node->info.insert.hint = (PT_HINT_ENUM) (node->info.insert.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint = (PT_HINT_ENUM) (node->info.delete_.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint = (PT_HINT_ENUM) (node->info.update.hint | hint_table[i].hint);
		}
	      break;
	    case PT_HINT_NO_SUPPLEMENTAL_LOG:	/* statement-based replication */
	      if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint = (PT_HINT_ENUM) (node->info.delete_.hint | hint_table[i].hint);
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint = (PT_HINT_ENUM) (node->info.update.hint | hint_table[i].hint);
		}
	      break;
	    default:
	      break;
	    }

	  if (hint_table[i].arg_list)
	    {
	      parser_free_tree (this_parser, hint_table[i].arg_list);
	      hint_table[i].arg_list = NULL;
	    }
	}
    }				/* for (i = ... ) */

}


//=============================================================================
static void
get_hint_args_func (unsigned char *arg_start, unsigned char *arg_end, unsigned char *arg_dot, PT_HINT * hint_table)
{
  unsigned char ch_backup = '\0';
  unsigned char *temp;
  PT_NODE *arg;

  /* trim space around found spec name */
  while ((arg_start < arg_end) && IS_WHITE_SPACE (*arg_start))
    {
      arg_start++;
    }

  while ((arg_end > arg_start) && IS_WHITE_SPACE (arg_end[-1]))
    {
      arg_end--;
    }

  if (arg_start == arg_end)
    {
      return;
    }

  arg = parser_new_node (this_parser, PT_NAME);
  if (!arg)
    {
      return;
    }

  if (!arg_dot || IS_HINT_ON_TABLE (hint_table->hint))
    {
      ch_backup = *arg_end;	// backup
      *arg_end = '\0';
      arg->info.name.original = pt_makename_trim_as_identifier ((char *) arg_start);
      *arg_end = ch_backup;	// restore     
    }
  else
    {
      *arg_dot = '\0';

      /* trim space around found spec name */
      temp = arg_dot - 1;
      while ((temp > arg_start) && IS_WHITE_SPACE (temp[0]))
	{
	  temp--;
	}
      ch_backup = temp[1];	// backup
      temp[1] = '\0';
      arg->info.name.resolved = pt_makename_trim_as_identifier ((char *) arg_start);
      temp[1] = ch_backup;

      *arg_dot++ = '.';		// restore

      /* trim space around found spec name */
      while ((arg_dot < arg_end) && IS_WHITE_SPACE (*arg_dot))
	{
	  arg_dot++;
	}
      ch_backup = *arg_end;	// backup
      *arg_end = '\0';
      arg->info.name.original = pt_makename_trim_as_identifier ((char *) arg_dot);
      *arg_end = ch_backup;	// restore
    }

  arg->info.name.meta_class = PT_HINT_NAME;
  hint_table->arg_list = parser_append_node (arg, hint_table->arg_list);
}

static inline int
hint_case_cmp (const char *input_string, const char *hint_token, int length, int &matched_idx)
{
  int cmp;

  // hint_token is always uppercase.  
  while (matched_idx < length)
    {
      if (input_string[matched_idx] == '\0')
	{
	  return -1;
	}

      cmp = toupper (input_string[matched_idx]) - hint_token[matched_idx];
      if (cmp != 0)
	{
	  return cmp;
	}

      matched_idx++;
    }

  return 0;
}

static int
find_hint_token (PT_HINT hint_table[], unsigned char *string)
{
  int i, len, matched_idx;
  char cBk;
  int cmp;

  if (!string || string[0] == '\0')
    {
      return -1;
    }

  matched_idx = 0;
  for (i = hint_table_lead_offset[*string]; hint_table[i].tokens; i++)
    {
      if (matched_idx > 0)
	{
	  if (memcmp (hint_table[i - 1].tokens, hint_table[i].tokens, matched_idx) != 0)
	    {
	      matched_idx = 0;
	    }
	}

      len = hint_table[i].length;
      cmp = hint_case_cmp ((char *) string, hint_table[i].tokens, len, matched_idx);
      if (cmp == 0)
	{
	  if (string[len] == '\0' || IS_WHITE_SPACE (string[len]) || string[len] == '(')
	    {
	      return i;
	    }
	}
      else if (cmp < 0)
	{
	  break;
	}
    }

  return -1;
}


static unsigned char *
read_hint_args (unsigned char *instr, PT_HINT hint_table[], int hint_idx, PT_HINT_ENUM * result_hint)
{
  unsigned char *sp, delimeter;
  unsigned char *in = instr;
  unsigned char *dot_ptr = 0x00;

#if 0				//
  while (IS_WHITE_SPACE (*in))
    {
      in++;
    }
#endif

  if (*in != '(')
    {
      /* found specified hint */
      hint_table[hint_idx].is_hit = true;
      *result_hint |= hint_table[hint_idx].hint;
      return instr;
    }

  /*
   * If hint_table[hint_idx].arg_list is reentrant from an existing state,
   *  which one will be adopted, the old one or the new one?
   */

  // As in the existing method, if only the first matched information is judged as valid, the second is ignored.
  bool is_illegal_expression = false;
  bool is_first_hit = false;

  if (hint_table[hint_idx].is_hit == false)
    {
      is_first_hit = true;
    }

  in++;
  while (IS_WHITE_SPACE (*in))
    {
      in++;
    }

  if (*in == ')')
    {
      if (is_first_hit)
	{
	  if (IS_HINT_ON_TABLE (hint_table[hint_idx].hint))
	    {
	      /*
	       * INDEX_SS() or INDEX_LS() means do not apply
	       * hint, use special node to mark this.
	       */
	      PT_NODE *arg = parser_new_node (this_parser, PT_VALUE);
	      if (arg)
		{
		  arg->type_enum = PT_TYPE_NULL;
		}
	      hint_table[hint_idx].arg_list = arg;
	      hint_table[hint_idx].is_hit = true;
	      *result_hint |= hint_table[hint_idx].hint;
	    }
	}

      return in + 1;
    }

  sp = in;
  while (*in)
    {
      if (*in == '"' || *in == '[' || *in == '`')
	{
	  delimeter = (*in == '[') ? ']' : *in;
	  in++;
	  while (*in)
	    {
	      if (*in == '\\' && *(in + 1) == delimeter)
		{
		  in++;
		}
	      else if (*in == delimeter)
		{
		  in++;
		  break;
		}
	      in++;
	    }
	}

      // IS_WHITE_SPACE (*in)
      if (*in == '(')
	{
	  /* illegal hint expression */
	  is_illegal_expression = true;
	}
      else if (*in == '.')
	{
	  if (!dot_ptr)
	    {
	      dot_ptr = in;	// set the position of the first dot
	    }
	}
      else if (*in == ',')
	{
	  if (is_first_hit && !is_illegal_expression)
	    {
	      get_hint_args_func (sp, in, dot_ptr, &(hint_table[hint_idx]));
	    }
	  dot_ptr = 0x00;
	  sp = in + 1;
	}
      else if (*in == ')')
	{			// OK
	  if (is_first_hit && !is_illegal_expression)
	    {
	      get_hint_args_func (sp, in, dot_ptr, &(hint_table[hint_idx]));
	      hint_table[hint_idx].is_hit = true;
	      *result_hint |= hint_table[hint_idx].hint;
	    }

	  return in + 1;
	}

      in++;
    }

  /* illegal hint expression */
  if (is_first_hit)
    {
      parser_free_node (this_parser, hint_table[hint_idx].arg_list);
      hint_table[hint_idx].arg_list = NULL;

#if 1
      // This code has been inserted to handle the same as the existing code.
      // It responds to the following types of errors:  INDEX_SS( (idx)
      if (IS_HINT_ON_TABLE (hint_table[hint_idx].hint))
	{
	  /*
	   * INDEX_SS() or INDEX_LS() means do not apply
	   * hint, use special node to mark this.
	   */
	  PT_NODE *arg = parser_new_node (this_parser, PT_VALUE);
	  if (arg)
	    {
	      arg->type_enum = PT_TYPE_NULL;
	    }
	  hint_table[hint_idx].arg_list = arg;
	  hint_table[hint_idx].is_hit = true;
	  *result_hint |= hint_table[hint_idx].hint;
	}
#endif
    }

  return in;
}

/*
 * pt_check_hint () - search hint comment from hint table
 *   text(in): hint comment
 *   hint_table(in): hint info structure list
 *   result_hint(in): found result
 *   prev_is_white_char(in): -
 */
void
pt_check_hint (const char *text, PT_HINT hint_table[], PT_HINT_ENUM * result_hint)
{
  unsigned char *hint_p;
  int hint_idx;
  bool start_flag = true;
  unsigned char *h_str = (unsigned char *) text + 1;	// skip '+'

  PRINT_HIT_HINT ("\n(HINT STRING) %s\n", text);

  // reset hit info.
  for (int i = 0; hint_table[i].tokens; i++)
    {
      hint_table[i].is_hit = false;
    }

  *result_hint = PT_HINT_NONE;
  while (IS_WHITE_SPACE (*h_str))
    {
      h_str++;
    }

  while (*h_str)
    {
      if (*h_str >= 0x80)
	{
	  do
	    {
	      h_str++;
	    }
	  while (*h_str >= 0x80);
	  start_flag = false;
	  continue;
	}
      else if (IS_WHITE_SPACE (*h_str))
	{
	  start_flag = true;
	  h_str++;
	}
      else if (start_flag == false)
	{
	  h_str++;
	}
      else if (hint_table_lead_offset[h_str[0]] >= hint_table_lead_offset[h_str[0] + 1])
	{
	  start_flag = false;
	  h_str++;
	}
      else
	{
	  hint_idx = find_hint_token (hint_table, h_str);
	  if (hint_idx == -1)
	    {
	      start_flag = false;
	      h_str++;
	    }
	  else
	    {
	      h_str += hint_table[hint_idx].length;
	      h_str = read_hint_args (h_str, hint_table, hint_idx, result_hint);
	    }
	}
    }
}

#if defined(PRINT_HIT_HINT_INFO)
static void
debug_hit_hint_print (PT_HINT * hint_table)
{
  PT_NODE *px;

  if (hint_table->arg_list == NULL)
    {
      fprintf (stdout, " %s ", hint_table->tokens);
      return;
    }

  fprintf (stdout, " %s(", hint_table->tokens);

  px = hint_table->arg_list;
  do
    {
      if (px->node_type == PT_NAME)
	{
	  if (px->info.name.resolved)
	    {
	      fprintf (stdout, "[%s].[%s]", px->info.name.resolved, px->info.name.original);
	    }
	  else
	    {
	      fprintf (stdout, "[%s]", px->info.name.original);
	    }
	}
      else if (px->node_type == PT_VALUE)
	{
	  assert (px->type_enum == PT_TYPE_NULL);
	  fprintf (stdout, "%s", "***");
	}
      else
	{
	  assert (0);
	}

      px = px->next;
      if (px)
	{
	  fprintf (stdout, ", ");
	}

    }
  while (px);

  fprintf (stdout, ")");
}
#endif
