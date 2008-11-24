/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * scanner_support.c - scanner support functions
 */

#ident "$Id$"

#include "config.h"

#include <math.h>

#include "parser_message.h"

#define ZZLEXBUFSIZE        17000
#define ZZERRSTD_SUPPLIED   1
#define ZZRDFUNC_SUPPLIED   1
#define ZZRDSTREAM_SUPPLIED 1
#define ZZSYNSUPPLIED       1
#define ZZRESYNCHSUPPLIED   1
#define ZZ_PREFIX 	    gr_
#define ZZ_MAX_SYNTAX_ERRORS 40
#define D_TextSize 	    255
#define JP_MAXNAME          256
#define LL_K 		    2

#include "charbuf.h"
#include "parser.h"
#include "dbdef.h"
#include "chartype.h"
#include "language_support.h"
#include "intl_support.h"
#include "zzpref.h"
#include "antlr.h"
#include "dlgdef.h"
#include "csql_grammar_mode.h"
#include "csql_grammar_scan.h"
#include "memory_alloc.h"
#include "misc_string.h"

#define LABMAX 	   	    3000
#define zzEOF_TOKEN 	    1

#define IS_WHITE_CHAR(c) \
                    ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\0')

#define GET_NEXT_BODY(c) \
  if (lp_look_state == 0)  /* normal input, get next character */\
    {\
      c = (this_parser->next_char)(this_parser);\
    }\
  else   /* return next la char */\
    {\
      c = input_look_ahead[input_look_ahead_ptr++];\
      if(! input_look_ahead[input_look_ahead_ptr] ) \
        {\
          lp_look_state=0;\
        }\
    }\

int input_host_index = 0;
int statement_OK;
PARSER_CONTEXT *this_parser;
int output_host_index = 0;
int lp_look_state = 0;

static unsigned char input_look_ahead[LABMAX];
static int input_look_ahead_ptr = 0;


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
 * zzTRACE () -
 *   return:
 *   rule(in):
 */
long
zzTRACE ()
{
  return 0;
}


/*
 * get_next () -
 *   return:
 *   get_next(in):
 */
static int
get_next ()
{
  int c;

  GET_NEXT_BODY (c);

  return c;
}


/*
 * pt_fix_left_parens () - called by scanner when it sees a '('
 *   return: none
 *
 * Note :
 * change all the '(' into '\01' when the input is '((((select',
 *    that is, this input becomes '\01\01\01\01select', or
 * change all the '(' into '\02' when the input is '((((anything_else',
 *    that is, this input becomes '\02\02\02\02anything_else'
 *
 * use of the characters '\01' & '\02' may not be portable.
 */

void
pt_fix_left_parens (void)
{
  int c, done = 0, found_select = 0, i, j;
  unsigned char *id;

  lp_look_state = j = 0;
  input_look_ahead[j++] = '(';
  zzcharfull = 0;

  /* if the first character following the first '(' is '\n' then
     skip the '\n', otherwise pt_nextchar will increment zzline twice. */
  c = (zzchar == '\n' ? get_next () : zzchar);

  /* scan for a '(((...( SELECT' */
  while (c != EOF && !done)
    {
      switch (c)
	{
	default:
	  if (char_isspace (c))
	    {
	      if (c == '\n')
		input_look_ahead[j++] = c;	/* buffer newlines */
	      c = get_next ();	/* skip white space */
	    }
	  else
	    {
	      input_look_ahead[j++] = c;
	      done = 1;
	    }
	  break;
	case 'S':
	case 's':		/* scan for rest of identifier */
	  id = &input_look_ahead[j];
	  while (c != EOF && char_isalpha (c))
	    {
	      input_look_ahead[j++] = c;
	      c = get_next ();
	    }
	  input_look_ahead[j] = '\0';
	  if (intl_mbs_casecmp ("SELECT", (char *) id) == 0)
	    {
	      /* check next char.
	       * keep out unlimited token.
	       * example: tab_name(SELECT_ATT1, SELECT_ATT2, ...)
	       */
	      if (char_isspace (c) || c == EOF)
		{
		  found_select = 1;
		}
	    }
	  if (c != EOF)
	    {
	      input_look_ahead[j++] = c;
	    }
	  done = 1;
	  break;
	case '(':		/* save (, and keep going */
	  input_look_ahead[j++] = c;
	  c = get_next ();
	  break;
	case '-':		/* skip comments */
	  c = get_next ();
	  if (c != '-')
	    {
	      input_look_ahead[j++] = '-';
	      input_look_ahead[j++] = c;
	      done = 1;
	    }
	  else			/* '--': sql-style comment, skip it */
	    {
	      while (c != EOF)
		{
		  c = get_next ();
		  if (c == '\n')
		    {
		      input_look_ahead[j++] = c;	/* buffer newlines */
		      c = get_next ();
		      break;
		    }
		}
	    }
	  break;
	case '/':		/* skip comments */
	  c = get_next ();
	  if (c != '*')
	    {
	      input_look_ahead[j++] = '/';
	      input_look_ahead[j++] = c;
	      done = 1;
	    }
	  else			/* C-style comment, skip it */
	    {
	      while (c != EOF)
		{
		  c = get_next ();
		  if (c == '\n')
		    {
		      input_look_ahead[j++] = c;	/* buffer newlines */
		    }
		  else if (c == '*')
		    {
		      c = get_next ();
		      if (c == '\n')
			{
			  input_look_ahead[j++] = c;	/* buffer newlines */
			}
		      else if (c == '/')
			{
			  c = get_next ();
			  break;
			}
		    }
		}
	    }
	  break;
	}
    }

  /* go through the buffer and fix the ('s except last char */
  input_look_ahead[j] = 0;
  for (i = 0; i < j - 1; i++)
    {
      if (input_look_ahead[i] == '(')
	{
	  input_look_ahead[i] = (found_select ? '\01' : '\02');
	}
      /* must use two characters that can not occur in any source query */
    }

  if ((c == EOF) && (input_look_ahead[0] == '('))
    {
      /* end with '('. this is error case */
      input_look_ahead[0] = '\02';
    }

  /* set up so get_next will return chars from buffer */
  input_look_ahead_ptr = 0;
  lp_look_state = 1;
}

/*
 * pt_check_hint () -
 *   return:
 *   text(in):
 *   hint_table(in):
 *   result_hint(in):
 *   prev_is_white_char(in):
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
		{		/* no argements */
		  /* found specified hint */
		  hint = hint_table[i].hint;
		}
	      else if (*(hint_p) == '(')
		{		/* need to check for argument */
		  hint_p++;	/* consume '(' */
		  arg_start = hint_p;
		  arg_end = strstr (arg_start, ")");
		  /* check arguments */
		  if (arg_end && ((len = arg_end - arg_start) > 0))
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
				      arg->info.name.original =
					pt_makename (arg_start);
				      arg->info.name.meta_class =
					PT_HINT_NAME;
				      hint_table[i].arg_list =
					parser_append_node (arg,
							    hint_table[i].
							    arg_list);
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
				  arg->info.name.original =
				    pt_makename (arg_start);
				  arg->info.name.meta_class = PT_HINT_NAME;
				  hint_table[i].arg_list =
				    parser_append_node (arg,
							hint_table[i].
							arg_list);
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

/*
 * pt_get_hint () -
 *   return:
 *   text(in):
 *   hint_table(in):
 *   node(in):
 */
void
pt_get_hint (const char *text, PT_HINT hint_table[], PT_NODE * node)
{
  int i;

  /* read hint info */
  for (i = 0; hint_table[i].tokens; i++)
    {
      if (strstr (text, hint_table[i].tokens))
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
	      break;
	    case PT_HINT_USE_IDX:	/* force idx-join */
	      if (node->node_type == PT_SELECT)
		{
		  node->info.query.q.select.hint |= hint_table[i].hint;
		  node->info.query.q.select.use_idx = hint_table[i].arg_list;
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
	    default:
	      break;
	    }
	}
    }				/* for (i = ... ) */
}


/*
 * pt_nextchar () - called by the scanner that Antlr creates.
 *   return: c the next character from input. -1   at end of file
 */
int
pt_nextchar (void)
{
  int c;
  GET_NEXT_BODY (c);

  if (zzbufovfcnt == 1)
    {
      PT_NODE node;		/* temp for line, column number propagation */
      node.line_number = this_parser->line;
      node.column_number = this_parser->column;
      PT_ERRORm (this_parser, &node, MSGCAT_SET_PARSER_SYNTAX,
		 MSGCAT_SYNTAX_TOKEN_TOO_LONG);
    }
  this_parser->column++;
  if (c == '\n')
    {
      this_parser->line = zzline++;
      this_parser->column = 0;
      zzendcol = 0;
    }
  return c;
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

#if defined(LL_K)
  {
    int line_behind = (LL_K - 2 <= 0 ? 0 : LL_K - 2);

    /* zzlineLA & zzcolumnLA are set in zzCONSUME in antlr.h    */
    node->line_number = zzlineLA[line_behind];	/* zzline is a token ahead */
    node->column_number = zzcolumnLA[LL_K - 1];
  }
#else /* LL_K */
  node->line_number = parser->line;	/* these are from scanner */
  node->column_number = parser->column;	/* to track errors        */
#endif /* LL_K */
}


/*
 * zzsyn () - creates a new syntax error message,
 *            appends it to this_parser->error_msgs
 *   return: none
 *   text(in): printname of the offending (error) token
 *   tok(in): integer code of the offending (error) token
 *   egroup(in): user-supplied printname of nonterminal
 *               (the lhs of the production where the error was detected)
 *   eset(in): bit encoded FOLLOW set
 *   etok(in): a one-element FOLLOW set
 */
void
zzsyn (const char *text, long tok, const char *egroup,
       unsigned long *eset, long etok)
{
  PT_NODE *dummy;

  dummy = parser_new_node (this_parser, PT_EXPR);
  if (dummy == NULL)
    return;			/* no space to record messages into */

  statement_OK = 0;

#if defined(LL_K)
  dummy->line_number = zzlineLA[0];
  dummy->column_number = zzcolumnLA[0];
#else /* LL_K */
  dummy->line_number = zzline;
#endif /* LL_K */

  if (pt_is_reserved_word (text))
    {
      PT_ERRORmf2 (this_parser, dummy, MSGCAT_SET_PARSER_SYNTAX,
		   MSGCAT_SYNTAX_KEYWORD_ERROR,
		   (tok == zzEOF_TOKEN ? "EOF" : text),
		   (etok ? zztokens[etok] : zzedecode (eset)));
    }
  else
    {
      if (egroup && strlen (egroup) > 0)
	{
	  PT_ERRORmf3 (this_parser, dummy, MSGCAT_SET_PARSER_SYNTAX,
		       MSGCAT_SYNTAX_ERROR_MSG2,
		       (tok == zzEOF_TOKEN ? "EOF" : text),
		       (etok ? zztokens[etok] : zzedecode (eset)), egroup);
	}
      else
	{
	  PT_ERRORmf2 (this_parser, dummy, MSGCAT_SET_PARSER_SYNTAX,
		       MSGCAT_SYNTAX_ERROR_MSG1,
		       (tok == zzEOF_TOKEN ? "EOF" : text),
		       (etok ? zztokens[etok] : zzedecode (eset)));
	}
    }

  parser_free_tree (this_parser, dummy);
}


/*
 * zzresynch () - resynchronize parser state after a syntax error repair
 *   return: none
 *   wd(in): a bit encoded vector of resynchronization symbols
 *   mask(in): a bit mask
 *
 * Note :
 * consume input tokens until a resynchronization symbol is found.
 * if too many parser errors then skip everything.
 * modifies position of scanner in the input stream.
 */
void
zzresynch (unsigned long *wd, unsigned long mask)
{
  long tok;

  tok = LA (1);
  if (tok == zzEOF_TOKEN)
    return;

  /* We must consume at least one token.
   * Otherwise, error repair can loop forever. */
  do
    {
      zzCONSUME;
    }
  while (!(wd[LA (1)] & mask) && LA (1) != zzEOF_TOKEN);

  if (pt_length_of_list (this_parser->error_msgs) <= ZZ_MAX_SYNTAX_ERRORS)
    return;

  /* too many syntax errors already, so skip to EOF */
  while (LA (1) != zzEOF_TOKEN)
    zzCONSUME;
}


/*
 * zzerrstd () - creates a new lexical error message,
 *               appends it to this_parser->error_msgs
 *   return:
 *   s(in): a brief characterization of the lexical error detected
 */
void
zzerrstd (const char *s)
{
  int length;
  unsigned char j_buffer[JP_MAXNAME + 1];
  PT_NODE *node;
  char *buf = 0, msg[4096];

  node = parser_new_node (this_parser, PT_ZZ_ERROR_MSG);
  if (node == NULL)
    return;			/* no space to record messages into */

  statement_OK = 0;
  node->info.error_msg.statement_number = this_parser->statement_number;

  node->column_number = zzbegcol;
  node->line_number = zzline;

/*
 * If the character at the end of error token (zzlextext[length - 1]) is
 * 8bit-set character, we must read next character as latter byte of
 * double-byte character, and concatenate it to zzlextext.  Because printf()
 * cannot print one-byte 8bit-set character in string(EUC).
 */

  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      length = strlen (zzlextext);
      if (length > 0)
	{
	  if ((zzlextext[length - 1] & 0x80) != 0)
	    {
	      strcpy ((char *) j_buffer, zzlextext);
	      j_buffer[length] = (unsigned char) zzchar;
	      j_buffer[length + 1] = '\0';
	      zzreplstr (j_buffer);
	    }
	}
    }
  sprintf (msg, msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SYNTAX,
				MSGCAT_SYNTAX_LEXICAL_ERROR), zzlextext);

  buf = pt_append_string (this_parser, buf, msg);

  node->info.error_msg.error_message = buf;
  if (!this_parser->error_msgs)
    this_parser->error_msgs = node;
  else
    parser_append_node (node, this_parser->error_msgs);
}

/*
 * zzrdfunc () -
 *   return:
 *   f(in):
 */
void
zzrdfunc (long (*f) ())
{
  zzbegcol = this_parser->column;
  zzendcol = this_parser->column;
  zzline = this_parser->line;
  zzchar = 0;
  zzbufovfcnt = 0;
  zzcharfull = 0;
  zzauto = 0;

  zzstream_in = NULL;
  zzfunc_in = f;
}
