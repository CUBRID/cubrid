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
 * broker_log_converter.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if !defined(WINDOWS)
#include <unistd.h>
#endif

#include "cubrid_getopt.h"
#include "cas_common.h"
#include "cas_cci.h"
#include "broker_log_util.h"

static int get_args (int argc, char *argv[]);
static int open_file (char *infilename, char *outfilename, FILE ** infp, FILE ** outfp);
static int log_converter (FILE * infp, FILE * outfp);
static void close_file (FILE * infp, FILE * outfp);
static int log_bind_value (char *str, int bind_len, int lineno, FILE * outfp);

static char add_query_info = 0;
static char add_query_id = 0;
static char *infilename = NULL;

int
main (int argc, char *argv[])
{
  int start_arg;
  char *outfilename = NULL;
  FILE *outfp, *infp;
  int res;

  if ((start_arg = get_args (argc, argv)) < 0)
    return -1;

  infilename = argv[start_arg];
  if (start_arg + 1 <= argc)
    outfilename = argv[start_arg + 1];

  if (open_file (infilename, outfilename, &infp, &outfp) < 0)
    return -1;

  res = log_converter (infp, outfp);

  close_file (infp, outfp);
  return res;
}

static int
log_converter (FILE * infp, FILE * outfp)
{
  char *linebuf;
  char query_flag = 0;
  int lineno = 0;
  char *msg_p;
  T_STRING *linebuf_tstr = NULL;
  int execute_flag = 0;
  int prepare_flag = 0;
  char in_execute = 0;
  int exec_h_id = 0;
  int bind_len = 0;
  int query_id = 0;

  linebuf_tstr = t_string_make (1000);
  if (linebuf_tstr == NULL)
    {
      fprintf (stderr, "malloc error\n");
      goto error;
    }

  while (1)
    {
      if (ut_get_line (infp, linebuf_tstr, NULL, NULL) < 0)
	{
	  fprintf (stderr, "malloc error\n");
	  goto error;
	}
      if (t_string_len (linebuf_tstr) <= 0)
	break;
      linebuf = t_string_str (linebuf_tstr);
      lineno++;

      if (linebuf[strlen (linebuf) - 1] == '\n')
	linebuf[strlen (linebuf) - 1] = '\0';

      if (is_cas_log (linebuf))
	{
	  if (query_flag)
	    {
	      fprintf (outfp, "\n");
	      fprintf (outfp, "P %d %d\n", exec_h_id, prepare_flag);
	    }
	  query_flag = 0;

	  msg_p = get_msg_start_ptr (linebuf);
	  if (strncmp (msg_p, "execute", 7) == 0)
	    {
	      msg_p = ut_get_execute_type (msg_p, &prepare_flag, &execute_flag);
	      if (msg_p == NULL)
		{
		  in_execute = 0;
		  continue;
		}
	      if (strncmp (msg_p, "srv_h_id ", 9) == 0)
		{
		  char *endp;
		  int result = 0;

		  in_execute = 1;
		  msg_p += 9;

		  result = str_to_int32 (&exec_h_id, &endp, msg_p, 10);
		  if (result != 0)
		    {
		      in_execute = 0;
		      continue;
		    }
		  msg_p = endp + 1;

		  fprintf (outfp, "Q ");
		  if (add_query_info == 1 && prepare_flag != 0x40)
		    {
		      fprintf (outfp, "/* %s */ ", infilename);
		    }
		  if (add_query_id == 1)
		    {
		      fprintf (outfp, "/* QUERY_ID %d */ ", query_id++);
		    }

		  fprintf (outfp, "%s%c", msg_p, CAS_RUN_NEW_LINE_CHAR);
		  query_flag = 1;
		}
	      else
		{
		  if (in_execute == 1)
		    {
		      fprintf (outfp, "E %d %d\n", exec_h_id, execute_flag);
		      fprintf (outfp, "C %d\n", exec_h_id);
		      fprintf (outfp, "T\n");
		    }
		  in_execute = 0;
		}
	    }
	  else if (strncmp (msg_p, "bind ", 5) == 0)
	    {
	      bind_len = t_string_bind_len (linebuf_tstr);
	      if (log_bind_value (msg_p, bind_len, lineno, outfp) < 0)
		goto error;
	    }
	}
      else if (query_flag)
	{
	  fprintf (outfp, "%s%c ", linebuf, CAS_RUN_NEW_LINE_CHAR);
	}
    }

  FREE_MEM (linebuf_tstr);
  return 0;

error:
  FREE_MEM (linebuf_tstr);
  return -1;
}

static int
open_file (char *infilename, char *outfilename, FILE ** infp, FILE ** outfp)
{
  if (strcmp (infilename, "-") == 0)
    {
      *infp = stdin;
    }
  else
    {
      *infp = fopen (infilename, "r");
      if (*infp == NULL)
	{
	  fprintf (stderr, "fopen error[%s]\n", infilename);
	  return -1;
	}
    }

  if (outfilename == NULL)
    {
      *outfp = stdout;
    }
  else
    {
      *outfp = fopen (outfilename, "w");
      if (*outfp == NULL)
	{
	  fprintf (stderr, "fopen error[%s]\n", outfilename);
	  return -1;
	}
    }

  return 0;
}

static void
close_file (FILE * infp, FILE * outfp)
{
  if (infp != stdin)
    fclose (infp);

  fflush (outfp);
  if (outfp != stdout)
    fclose (outfp);
}

static int
log_bind_value (char *str, int bind_len, int lineno, FILE * outfp)
{
  char *p, *q, *r;
  char *value_p;
  int type;

  p = strchr (str, ':');
  if (p == NULL)
    {
      fprintf (stderr, "log error [line:%d]\n", lineno);
      return -1;
    }
  p += 2;
  q = strchr (p, ' ');
  if (q == NULL)
    {
      if (strcmp (p, "NULL") == 0)
	{
	  value_p = (char *) "";
	}
      else
	{
	  fprintf (stderr, "log error [line:%d]\n", lineno);
	  return -1;
	}
    }
  else
    {
      if (bind_len > 0)
	{
	  r = strchr (q, ')');
	  if (r == NULL)
	    {
	      fprintf (stderr, "log error [line:%d]\n", lineno);
	      return -1;
	    }
	  *q = '\0';
	  *r = '\0';
	  value_p = r + 1;
	}
      else
	{
	  *q = '\0';
	  value_p = q + 1;
	}
    }

  if (strcmp (p, "NULL") == 0)
    {
      type = CCI_U_TYPE_NULL;
    }
  else if (strcmp (p, "CHAR") == 0)
    {
      type = CCI_U_TYPE_CHAR;
    }
  else if (strcmp (p, "VARCHAR") == 0)
    {
      type = CCI_U_TYPE_STRING;
    }
  else if (strcmp (p, "NCHAR") == 0)
    {
      type = CCI_U_TYPE_NCHAR;
    }
  else if (strcmp (p, "VARNCHAR") == 0)
    {
      type = CCI_U_TYPE_VARNCHAR;
    }
  else if (strcmp (p, "BIT") == 0)
    {
      type = CCI_U_TYPE_BIT;
    }
  else if (strcmp (p, "VARBIT") == 0)
    {
      type = CCI_U_TYPE_VARBIT;
    }
  else if (strcmp (p, "NUMERIC") == 0)
    {
      type = CCI_U_TYPE_NUMERIC;
    }
  else if (strcmp (p, "UBIGINT") == 0)
    {
      type = CCI_U_TYPE_UBIGINT;
    }
  else if (strcmp (p, "BIGINT") == 0)
    {
      type = CCI_U_TYPE_BIGINT;
    }
  else if (strcmp (p, "UINT") == 0)
    {
      type = CCI_U_TYPE_UINT;
    }
  else if (strcmp (p, "INT") == 0)
    {
      type = CCI_U_TYPE_INT;
    }
  else if (strcmp (p, "USHORT") == 0)
    {
      type = CCI_U_TYPE_USHORT;
    }
  else if (strcmp (p, "SHORT") == 0)
    {
      type = CCI_U_TYPE_SHORT;
    }
  else if (strcmp (p, "MONETARY") == 0)
    {
      type = CCI_U_TYPE_MONETARY;
    }
  else if (strcmp (p, "FLOAT") == 0)
    {
      type = CCI_U_TYPE_FLOAT;
    }
  else if (strcmp (p, "DOUBLE") == 0)
    {
      type = CCI_U_TYPE_DOUBLE;
    }
  else if (strcmp (p, "DATE") == 0)
    {
      type = CCI_U_TYPE_DATE;
    }
  else if (strcmp (p, "TIME") == 0)
    {
      type = CCI_U_TYPE_TIME;
    }
  else if (strcmp (p, "TIMESTAMP") == 0)
    {
      type = CCI_U_TYPE_TIMESTAMP;
    }
  else if (strcmp (p, "DATETIME") == 0)
    {
      type = CCI_U_TYPE_DATETIME;
    }
  else if (strcmp (p, "TIMESTAMPTZ") == 0)
    {
      type = CCI_U_TYPE_TIMESTAMPTZ;
    }
  else if (strcmp (p, "DATETIMETZ") == 0)
    {
      type = CCI_U_TYPE_DATETIMETZ;
    }
  else if (strcmp (p, "OBJECT") == 0)
    {
      type = CCI_U_TYPE_OBJECT;
    }
  else if (strcmp (p, "BLOB") == 0)
    {
      type = CCI_U_TYPE_NULL;
      fprintf (stderr, "%s\nBLOB type is not implemented. Replaced with NULL\n", value_p);
      value_p = (char *) "";
      bind_len = 0;
      /* type = CCI_U_TYPE_BLOB; */
    }
  else if (strcmp (p, "CLOB") == 0)
    {
      type = CCI_U_TYPE_NULL;
      fprintf (stderr, "%s\nCLOB type is not implemented. Replaced with NULL\n", value_p);
      value_p = (char *) "";
      bind_len = 0;
      /* type = CCI_U_TYPE_CLOB; */
    }
  else if (strcmp (p, "ENUM") == 0)
    {
      type = CCI_U_TYPE_ENUM;
    }
  else if (strcmp (p, "JSON") == 0)
    {
      type = CCI_U_TYPE_JSON;
    }
  else
    {
      fprintf (stderr, "log error [line:%d]\n", lineno);
      return -1;
    }

  if (bind_len > 0)
    {
      fprintf (outfp, "B %d %d %s\n", type, bind_len, value_p);
    }
  else
    {
      fprintf (outfp, "B %d %s\n", type, value_p);
    }
  return 0;
}

static int
get_args (int argc, char *argv[])
{
  int c;

  while ((c = getopt (argc, argv, "iq")) != EOF)
    {
      switch (c)
	{
	case 'q':
	  add_query_info = 1;
	  break;
	case 'i':
	  add_query_id = 1;
	  break;
	default:
	  goto usage;
	}
    }

  if (optind + 1 >= argc)
    goto usage;

  return optind;

usage:
  fprintf (stderr,
	   "usage : %s [OPTION] infile outfile\n" "\n" "valid options:\n"
	   "  -i   add a unique id to each query as a comment.\n", argv[0]);
  return -1;
}
