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
#include "dbdef.h"
#include "chartype.h"
#include "language_support.h"
#include "intl_support.h"
#include "csql_grammar_scan.h"
#include "memory_alloc.h"
#include "misc_string.h"

#define IS_WHITE_CHAR(c) \
                    ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\0')


int parser_input_host_index = 0;
int parser_statement_OK = 0;
PARSER_CONTEXT *this_parser;
int parser_output_host_index = 0;

extern int yyline;
extern int yycolumn;
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

  node->line_number = yyline;
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

  /* read hint info */
  for (i = 0; hint_table[i].tokens; i++)
    {
      if (stristr (text, hint_table[i].tokens))
	{

	  switch (hint_table[i].hint)
	    {
	    case PT_HINT_NONE:
	      break;
	    case PT_HINT_ORDERED:	/* force join left-to-right */

/* TEMPORARY COMMENTED CODE: DO NOT REMOVE ME !!! */
#if 0
	      node->info.query.q.select.ordered = hint_table[i].arg_list;
	      hint_table[i].arg_list = NULL;
#endif /* 0                              */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint |= hint_table[i].hint;
		}
	      break;
#if 0
	    case PT_HINT_W:	/* not used */
	      break;
	    case PT_HINT_X:	/* not used */
	      break;
	    case PT_HINT_Y:	/* not used */
	      break;
#endif /* 0 */
	    case PT_HINT_USE_NL:	/* force nl-join */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint |= hint_table[i].hint;
		  node->info.query.q.select.use_nl = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint |= hint_table[i].hint;
		  node->info.delete_.use_nl_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint |= hint_table[i].hint;
		  node->info.update.use_nl_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_USE_IDX:	/* force idx-join */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint |= hint_table[i].hint;
		  node->info.query.q.select.use_idx = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint |= hint_table[i].hint;
		  node->info.delete_.use_idx_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint |= hint_table[i].hint;
		  node->info.update.use_idx_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_USE_MERGE:	/* force m-join */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint |= hint_table[i].hint;
		  node->info.query.q.select.use_merge =
		    hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint |= hint_table[i].hint;
		  node->info.delete_.use_merge_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint |= hint_table[i].hint;
		  node->info.update.use_merge_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
#if 0
	    case PT_HINT_USE_HASH:	/* not used */
	      break;
#endif /* 0 */
	    case PT_HINT_RECOMPILE:	/* recompile */
	      node->recompile = 1;
	      break;
	    case PT_HINT_LK_TIMEOUT:	/* lock timeout */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint |= hint_table[i].hint;
		  node->info.query.q.select.waitsecs_hint =
		    hint_table[i].arg_list;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint |= hint_table[i].hint;
		  node->info.update.waitsecs_hint = hint_table[i].arg_list;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint |= hint_table[i].hint;
		  node->info.delete_.waitsecs_hint = hint_table[i].arg_list;
		}
	      else if (node->node_type == PT_INSERT)
		{
		  node->info.insert.hint |= hint_table[i].hint;
		  node->info.insert.waitsecs_hint = hint_table[i].arg_list;
		}
	      else if (node->node_type == PT_MERGE)
		{
		  node->info.merge.hint |= hint_table[i].hint;
		  node->info.merge.waitsecs_hint = hint_table[i].arg_list;
		}
	      hint_table[i].arg_list = NULL;
	      break;
	    case PT_HINT_NO_LOGGING:	/* no logging */
	      if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_INSERT)
		{
		  node->info.insert.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_MERGE)
		{
		  node->info.merge.hint |= hint_table[i].hint;
		}
	      hint_table[i].arg_list = NULL;
	      break;
	    case PT_HINT_REL_LOCK:	/* release lock */
	      if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_INSERT)
		{
		  node->info.insert.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_MERGE)
		{
		  node->info.merge.hint |= hint_table[i].hint;
		}
	      hint_table[i].arg_list = NULL;
	      break;
	    case PT_HINT_QUERY_CACHE:	/* query_cache */
	      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
		{
		  node->info.query.hint |= hint_table[i].hint;
		  node->info.query.qcache_hint = hint_table[i].arg_list;
		  if (node->info.query.qcache_hint)
		    {
		      if (atoi
			  (node->info.query.qcache_hint->info.name.original))
			node->info.query.do_cache = 1;
		      else
			node->info.query.do_not_cache = 1;
		    }
		  else
		    {
		      node->info.query.do_cache = 1;
		    }
		}
	      hint_table[i].arg_list = NULL;
	      break;
	    case PT_HINT_REEXECUTE:	/* reexecute */
	      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
		{
		  node->info.query.hint |= hint_table[i].hint;
		  node->info.query.reexecute = 1;
		}
	      break;
	    case PT_HINT_JDBC_CACHE:	/* jdbc cache */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint |= hint_table[i].hint;
		  node->info.query.q.select.jdbc_life_time =
		    hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_NO_STATS:	/* no stats */
	      if (node->node_type == PT_CREATE_ENTITY)
		{
		  node->info.create_entity.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_ALTER)
		{
		  node->info.alter.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_CREATE_INDEX
		       || node->node_type == PT_DROP_INDEX)
		{
		  node->info.index.hint |= hint_table[i].hint;
		}
	      break;
	    case PT_HINT_USE_IDX_DESC:	/* descending index scan */
	    case PT_HINT_NO_COVERING_IDX:	/* do not use covering index scan */
	    case PT_HINT_NO_IDX_DESC:	/* do not use descending index scan */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint |= hint_table[i].hint;
		}
	      break;
	    case PT_HINT_INSERT_MODE:
	      if (node->node_type == PT_INSERT)
		{
		  node->info.insert.hint |= hint_table[i].hint;
		  node->info.insert.insert_mode = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_NO_MULTI_RANGE_OPT:
	    case PT_HINT_NO_SORT_LIMIT:
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_DELETE)
		{
		  node->info.delete_.hint |= hint_table[i].hint;
		}
	      else if (node->node_type == PT_UPDATE)
		{
		  node->info.update.hint |= hint_table[i].hint;
		}
	      break;
	    case PT_HINT_USE_UPDATE_IDX:
	      if (node->node_type == PT_MERGE)
		{
		  node->info.merge.hint |= hint_table[i].hint;
		  node->info.merge.update.index_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    case PT_HINT_USE_INSERT_IDX:
	      if (node->node_type == PT_MERGE)
		{
		  node->info.merge.hint |= hint_table[i].hint;
		  node->info.merge.insert.index_hint = hint_table[i].arg_list;
		  hint_table[i].arg_list = NULL;
		}
	      break;
	    default:
	      break;
	    }
	}
    }				/* for (i = ... ) */
}

/*
 * pt_check_hint () - search hint comment from hint table
 *   text(in): hint comment
 *   hint_table(in): hint info structure list
 *   result_hint(in): found result
 *   prev_is_white_char(in): flag indicates prev char
 */
void
pt_check_hint (const char *text, PT_HINT hint_table[],
	       PT_HINT_ENUM * result_hint, bool prev_is_white_char)
{
  int i, j, len, count;
  PT_HINT_ENUM hint;
  char hint_buf[JP_MAXNAME];
  char *hint_p, *arg_start, *arg_end, *temp;
  PT_NODE *arg;

  for (i = 0; hint_table[i].tokens; i++)
    {
      count = 0;		/* init */
      hint = PT_HINT_NONE;	/* init */
      strncpy (hint_buf, text, JP_MAXNAME);
      hint_buf[JP_MAXNAME - 1] = '\0';
      hint_p = ustr_casestr (hint_buf, hint_table[i].tokens);

      while (hint_p)
	{
	  len = strlen (hint_table[i].tokens);
	  /* check token before */
	  if ((count == 0 && (prev_is_white_char ||
			      (hint_p > hint_buf
			       && IS_WHITE_CHAR (*(hint_p - 1)))))
	      || IS_WHITE_CHAR (*(hint_p - 1)))
	    {
	      hint_p += len;	/* consume token */
	      /* check token after */
	      if (IS_WHITE_CHAR (*(hint_p)))
		{		/* no arguments */
		  /* found specified hint */
		  hint = hint_table[i].hint;
		}
	      else if (*(hint_p) == '(')
		{		/* need to check for argument */
		  hint_p++;	/* consume '(' */
		  arg_start = hint_p;
		  arg_end = strstr (arg_start, ")");
		  /* check arguments */
		  if (arg_end
		      && ((len = CAST_STRLEN (arg_end - arg_start)) > 0))
		    {
		      for (j = 0; j < len; j++)
			{
			  if (hint_p[j] == '(')
			    {
			      /* illegal hint expression */
			      break;
			    }
			  if (hint_p[j] == ',')
			    {	/* found delimiter */
			      hint_p[j] = '\0';	/* replace ',' */
			      /* trim space around found spec name */
			      for (; arg_start < &(hint_p[j]); arg_start++)
				{
				  if (!IS_WHITE_CHAR (*arg_start))
				    {
				      break;
				    }
				}
			      for (temp = &(hint_p[j - 1]); temp > arg_start;
				   temp--)
				{
				  if (!IS_WHITE_CHAR (*temp))
				    {
				      break;
				    }
				  *temp = '\0';	/* counsume space */
				}
			      /* add specified spec */
			      if (arg_start < &(hint_p[j]))
				{
				  arg =
				    parser_new_node (this_parser, PT_NAME);
				  if (arg)
				    {
				      temp = strstr (arg_start, ".");
				      if (temp && temp < &(hint_p[j]))
					{
					  *temp = '\0';
					  arg->info.name.resolved =
					    pt_makename (arg_start);
					  *temp++ = '.';
					  arg->info.name.original =
					    pt_makename (temp);
					}
				      else
					{
					  arg->info.name.original =
					    pt_makename (arg_start);
					}
				      arg->info.name.meta_class =
					PT_HINT_NAME;
				      hint_table[i].arg_list =
					parser_append_node (arg,
							    hint_table
							    [i].arg_list);
				    }
				}
			      arg_start = &(hint_p[j + 1]);
			    }
			}	/* for (j = ... ) */

		      if (j < len)
			{
			  /* error occurs. free alloced nodes */
			  if (hint_table[i].arg_list)
			    {
			      parser_free_tree (this_parser,
						hint_table[i].arg_list);
			      hint_table[i].arg_list = NULL;
			    }
			  /* consume illegal hint expression */
			  hint_p += j + 1;
			}
		      else
			{	/* OK */
			  /* check last argument */
			  hint_p[j] = '\0';	/* replace ')' */
			  /* trim space around found spec name */
			  for (; arg_start < &(hint_p[j]); arg_start++)
			    {
			      if (!IS_WHITE_CHAR (*arg_start))
				{
				  break;
				}
			    }
			  for (temp = &(hint_p[j - 1]); temp > arg_start;
			       temp--)
			    {
			      if (!IS_WHITE_CHAR (*temp))
				{
				  break;
				}
			      *temp = '\0';	/* counsume space */
			    }
			  if (arg_start < &(hint_p[j]))
			    {
			      arg = parser_new_node (this_parser, PT_NAME);
			      if (arg)
				{
				  temp = strstr (arg_start, ".");
				  if (temp && temp < &(hint_p[j]))
				    {
				      *temp = '\0';
				      arg->info.name.resolved =
					pt_makename (arg_start);
				      *temp++ = '.';
				      arg->info.name.original =
					pt_makename (temp);
				    }
				  else
				    {
				      arg->info.name.original =
					pt_makename (arg_start);
				    }
				  arg->info.name.meta_class = PT_HINT_NAME;
				  hint_table[i].arg_list =
				    parser_append_node (arg,
							hint_table
							[i].arg_list);
				}
			    }

			  hint_p += len;	/* consume arguments */
			  hint_p++;	/* consume ')' */
			}
		    }

		  /* found specified hint */
		  if (hint_table[i].arg_list)
		    {
		      hint = hint_table[i].hint;
		    }
		}
	    }
	  else
	    {
	      /* not found specified hint */
	      hint_p += len;	/* consume token */
	    }

	  /* check for found specified hint */
	  if (hint & hint_table[i].hint)
	    {
	      /* save hint and immediately stop */
	      *result_hint |= hint;
	      break;
	    }

	  count++;

	  /* step to next */
	  hint_p = ustr_casestr (hint_p, hint_table[i].tokens);

	}			/* while (hint_p) */
    }				/* for (i = ... ) */
}
