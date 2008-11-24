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
 * esql_main.h - Main program header for ESQL
 *
 */

#ifndef _ESQL_MAIN_H_
#define _ESQL_MAIN_H_

#ident "$Id$"

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include "misc_string.h"
#include "esql_misc.h"
#include "environment_variable.h"
#include "message_catalog.h"
#include "memory_alloc.h"
#include "variable_string.h"

#define UCI_OPT_UNSAFE_NULL     0x0001
#define makestring1(x) #x
#define makestring(x) makestring1(x)
#if !defined(VERSION)
#define VERSION makestring(RELEASE_STRING)
#endif


#define ESQL_MSG_CATALOG        "esql.cat"

typedef struct pt_host_refs
{
  YY_CHAR *descr[2];		/* at most 2 descriptors: 1 in, 1 out */
  int descr_occupied;		/* occupied slots in descr            */
  HOST_REF **host_refs;		/* array of pointers to HOST_REF      */
  int occupied;			/* occupied slots in host_refs        */
  int allocated;		/* allocated slots in host_refs       */
  YY_CHAR *in_descriptor;	/* NULL or input descriptor name      */
  YY_CHAR *out_descriptor;	/* NULL or output descriptor name     */
  HOST_REF **in_refs;		/* array of input HOST_REF addresses  */
  int in_refs_occupied;
  int in_refs_allocated;
  HOST_REF **out_refs;		/* array of output HOST_REF addresses */
  int out_refs_occupied;
  int out_refs_allocated;
} PT_HOST_REFS;

enum esqlmain_msg
{
  MSG_REPEATED_IGNORED = 1,
  MSG_VERSION = 2
};

const char *VARCHAR_ARRAY_NAME = "array";
const char *VARCHAR_LENGTH_NAME = "length";
const char *pp_include_path;
const char *pp_include_file;
const char *prog_name;

unsigned int pp_uci_opt = 0;
int pp_emit_line_directives = 1;
int pp_dump_scope_info = 0;
int pp_dump_malloc_info = 0;
int pp_announce_version = 0;
int pp_enable_uci_trace = 0;
int pp_disable_varchar_length = 0;
int pp_varchar2 = 0;
int pp_unsafe_null = 0;
int pp_internal_ind = 0;
PARSER_CONTEXT *parser;
char *pt_buffer;
varstring pt_statement_buf;

static PTR_VEC id_list;
static HOST_LOD *input_refs, *output_refs;
static bool repeat_option;
static bool already_translated;
static bool structs_allowed;
static bool hvs_allowed;
static int i;
static int n_refs;
static char *outfile_name = NULL;

static void reset_globals (void);
static void ignore_repeat (void);
static char *ofile_name (char *fname);
static int validate_input_file (void *fname, FILE * outfp);

/*
 * check() -
 * return : void
 * cond(in) :
 */
static void
check (int cond)
{
  if (!cond)
    {
      perror (prog_name);
      exit (1);
    }
}

/*
 * check_w_file() -
 * return : void
 * cond(in) :
 * filename(in) :
 */
static void
check_w_file (int cond, const char *filename)
{
  if (!cond)
    {
      char buf[2 * PATH_MAX];
      sprintf (buf, "%s: %s", prog_name, filename);
      perror (buf);
      exit (1);
    }
}

/*
 * reset_globals() -
 * return : void
 */
static void
reset_globals (void)
{
  input_refs = NULL;
  output_refs = NULL;
  repeat_option = false;
  already_translated = false;
  structs_allowed = false;
  hvs_allowed = true;

  yy_set_buf (NULL);
  pp_clear_host_refs ();
  pp_gather_input_refs ();
}

/*
 * ignore_repeat() -
 * return : void
 */
static void
ignore_repeat (void)
{
  if (repeat_option)
    {
      yyvwarn (pp_get_msg (EX_ESQLMMAIN_SET, MSG_REPEATED_IGNORED));
    }
}


static int
validate_input_file (void *fname, FILE * outfp)
{
  return (fname == NULL) && isatty (fileno (stdin));
}


static char *
ofile_name (char *fname)
{
  static char buf[PATH_MAX];
  char *p;

  /* Get the last pathname component into buf. */
  p = strrchr (fname, '/');
  strcpy (buf, p ? (p + 1) : fname);

  /* Now add the .c suffix, copying over the .ec suffix if present. */
  p = strrchr (buf, '.');
  if (p && !STREQ (p, ".c"))
    {
      strcpy (p, ".c");
    }
  else
    {
      strcat (buf, ".c");
    }

  return buf;
}

/*
 * copy() -
 * return : void
 * fp(in) :
 * fname(in) :
 */
static void
copy (FILE * fp, const char *fname)
{
  int ifd, ofd;
  int rbytes, wbytes;
  char buf[BUFSIZ];

  rewind (fp);

  ifd = fileno (fp);
  ofd = open (fname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  check_w_file (ofd >= 0, fname);

  /* Now copy the contents; use read() and write() to try to cut down
     on excess buffering. */
  rbytes = 0;
  wbytes = 0;
  while ((rbytes = read (ifd, buf, sizeof (buf))) > 0)
    {
      wbytes = write (ofd, buf, rbytes);
      if (wbytes != rbytes)
	{
	  break;
	}
    }
  check (rbytes >= 0 && wbytes >= 0);

  close (ofd);
}


static const char *
pp_unsafe_get_msg (int msg_set, int msg_num)
{
  static int complained = 0;
  static MSG_CATD msg_cat = NULL;

  const char *msg;

  if (msg_cat == NULL)
    {
      if (complained)
	{
	  return NULL;
	}

      msg_cat = msgcat_open (ESQL_MSG_CATALOG);
      if (msg_cat == NULL)
	{
	  complained = 1;
	  fprintf (stderr, "%s: %s: %s\n",
		   prog_name, ESQL_MSG_CATALOG, strerror (ENOENT));
	  return NULL;
	}
    }

  msg = msgcat_gets (msg_cat, msg_set, msg_num, NULL);

  return (msg == NULL || msg[0] == '\0') ? NULL : msg;
}

/*
 * pp_get_msg() -
 * return :
 * msg_set(in) :
 * msg_num(in) :
 */
const char *
pp_get_msg (int msg_set, int msg_num)
{
  static const char no_msg[] = "No message available.";
  const char *msg;

  msg = pp_unsafe_get_msg (msg_set, msg_num);
  return msg ? msg : no_msg;
}


static void
usage (void)
{
  fprintf (stderr,
	   "usage: cubrid_esql [OPTION] input-file\n\n"
	   "valid option:\n"
	   "  -l, --suppress-line-directive  suppress #line directives in output file; default: off\n"
	   "  -o, --output-file              output file name; default: stdout\n"
	   "  -h, --include-file             include file containing uci_ prototypes; default: cubrid_esql.h\n"
	   "  -s, --dump-scope-info          dump scope debugging information; default: off\n"
	   "  -m, --dump-malloc-info         dump final malloc debugging information; default: off\n"
	   "  -t, --enable-uci-trace         enable UCI function trace; default: off\n"
	   "  -d, --disable-varchar-length   disable length initialization of VARCHAR host variable; default; off\n"
	   "  -2, --varchar2-style           use different VARCHAR style; default: off\n"
	   "  -u, --unsafe-null              ignore -462(null indicator needed) error: default; off\n"
	   "  -e, --internal-indicator       use internal NULL indicator to prevent -462(null indicator needed) error, default off\n");
}

static int
get_next ()
{
  return fgetc (yyin);
}

/*
 * main() -
 * return :
 * argc(in) :
 * argv(in) :
 */
int
main (int argc, char **argv)
{
  char *tmpfile_name;
  struct option esql_option[] = {
    {"suppress-line-directive", 0, 0, 'l'},
    {"output-file", 1, 0, 'o'},
    {"include-file", 1, 0, 'h'},
    {"dump-scope-info", 0, 0, 's'},
    {"dump-malloc-info", 0, 0, 'm'},
    {"version", 0, 0, 'v'},
    {"enable-uci-trace", 0, 0, 't'},
    {"disable-varchar-length", 0, 0, 'd'},
    {"varchar2-style", 0, 0, '2'},
    {"unsafe-null", 0, 0, 'u'},
    {"internal-indicator", 0, 0, 'e'},
    {0, 0, 0, 0}
  };


  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, "lo:h:smvtd2ue",
				esql_option, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case 'l':
	  pp_emit_line_directives = true;
	  break;
	case 'o':
	  outfile_name = strdup (optarg);
	  break;
	case 'h':
	  pp_include_file = strdup (optarg);
	  break;
	case 's':
	  pp_dump_scope_info = true;
	  break;
	case 'm':
	  pp_dump_malloc_info = true;
	  break;
	case 'v':
	  pp_announce_version = true;
	  break;
	case 't':
	  pp_enable_uci_trace = true;
	  break;
	case 'd':
	  pp_disable_varchar_length = true;
	  break;
	case '2':
	  pp_varchar2 = true;
	  break;
	case 'u':
	  pp_unsafe_null = true;
	  break;
	case 'e':
	  pp_internal_ind = true;
	  break;
	default:
	  usage ();
	  return errors;
	}
    }

  if (optind < argc)
    {
      yyfilename = argv[optind];
    }
  else
    {
      usage ();
      return errors;
    }

  tmpfile_name = NULL;

  prog_name = argv[0];
  if (pp_announce_version)
    {
      printf (pp_get_msg (EX_ESQLMMAIN_SET, MSG_VERSION), argv[0], VERSION);
    }

  if (pp_varchar2)
    {
      VARCHAR_ARRAY_NAME = "arr";
      VARCHAR_LENGTH_NAME = "len";
    }

  if (pp_unsafe_null)
    {
      pp_uci_opt |= UCI_OPT_UNSAFE_NULL;
    }

  pp_include_path = envvar_root ();
  if (pp_include_path == NULL)
    {
      exit (1);
    }

  if (yyfilename == NULL)
    {
      /* No input filename was supplied; use stdin for input and stdout
         for output. */
      yyin = stdin;
    }
  else
    {
      yyin = fopen (yyfilename, "r");
      check_w_file (yyin != NULL, yyfilename);
    }

  if (yyfilename && !outfile_name)
    {
      outfile_name = ofile_name (yyfilename);
    }

  if (outfile_name)
    {
      tmpfile_name = tempnam (".", "esql");
      check (tmpfile_name != NULL);
      yyout = fopen (tmpfile_name, "w+");
      check_w_file (yyout != NULL, tmpfile_name);
      /* Now unlink it so it will go away if we crash for some reason.
         We don't really care if the unlink fails. */
      (void) unlink (tmpfile_name);
    }
  else
    {
      yyout = stdout;
    }

  yyinit ();
  pp_startup ();
  vs_new (&pt_statement_buf);
  emit_line_directive ();

  if (utility_initialize () != NO_ERROR)
    {
      exit (1);
    }
  parser = parser_create_parser ();
  pt_buffer = pt_append_string (parser, NULL, NULL);

  ANTLRf (translation_unit (), get_next);

  if (tmpfile_name)
    {
      if (errors == 0)
	{
	  /*
	   * We want to keep the file, so rewind the temp file and copy
	   * it to the named file. It really would be nice if we could
	   * just associate the name with the file resources we've
	   * already created, but there doesn't seem to be a way to do
	   * that.
	   */
	  copy (yyout, outfile_name);
	}
      free (tmpfile_name);
    }

  fclose (yyin);
  fclose (yyout);

  msgcat_final ();
  exit (errors);
  return errors;
}

#endif /* _ESQL_MAIN_H_ */
