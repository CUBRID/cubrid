/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * loadjava.c - loadjava utility
 */

#ident "$Id$"

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef HAVA_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include "error_code.h"
#include "message_catalog.h"
#include "utility.h"
#include "databases_file.h"
#if defined(WINDOWS)
#include "porting.h"
#endif /* WINDOWS */

#define JAVA_DIR                "java"
#define COPY_BUFFER_SIZE        1024
#if defined(WINDOWS)
#define SEPERATOR               '\\'
#else /* ! WINDOWS */
#define SEPERATOR               '/'
#endif /* !WINDOWS */

#define GETMSG(msgnum) util_get_message(Msg_catalog, MSG_SET_LOADJAVA, msgnum)

static int filecopy (const char *fn_src, const char *fn_dst);
static int check_dbname (const char *name);

static char *Program_name = NULL;
static char *Dbname = NULL;
static char *Src_class = NULL;
static int Force_overwrite = false;

/*
 * check_dbname() - test if the database name is valid
 *   return: 0 if all characters are valid for database name,
 *           otherwise bad character position.
 *   name(in): database name
 */
static int
check_dbname (const char *name)
{
  int badchar = 0;
#if 0
  const char *msg;

  badchar = util_check_dbname (name);
  if (badchar)
    {
      msg =
	msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GENERIC,
			MSGCAT_UTIL_GENERIC_BAD_DATABASE_NAME);
      if (msg != NULL)
	fprintf (stderr, msg, badchar, name);
    }

#endif
  return (badchar);
}

/*
 * filecopy() - copy a file
 *   return: 0 if success, otherwise errno
 *   fn_src(in): source filename
 *   fn_dst(in): destination filename
 */
static int
filecopy (const char *fn_src, const char *fn_dst)
{
  size_t bytesr;
  int retval = 0;
  FILE *fh_src = NULL;
  FILE *fh_dst = NULL;
  char buff[COPY_BUFFER_SIZE];
  char c;

  fh_src = fopen (fn_src, "rb");
  if (fh_src == NULL)
    {
      fprintf (stderr, "'%s' cannot open.\n", fn_src);
      retval = errno;
    }

  if (!retval)
    {
      if (!Force_overwrite)
	{
	  fh_dst = fopen (fn_dst, "r");
	  if (fh_dst)
	    {
	      fclose (fh_dst);
	      fprintf (stdout, "'%s' is exist. overwrite? (y/n): ", fn_dst);
	      c = getchar ();
	      if (c != 'Y' && c != 'y')
		{
		  fprintf (stdout, "loadjava is cancled\n");
		  return 0;
		}
	    }
	}
      fh_dst = fopen (fn_dst, "w+b");

      if (fh_dst == NULL)
	{
	  fprintf (stderr, "'%s' cannot open.\n", fn_dst);
	  retval = errno;
	}
      else
	{
	  while (!retval)
	    {
	      bytesr = fread (buff, 1, COPY_BUFFER_SIZE, fh_src);
	      if (bytesr < 1)
		{
		  if (feof (fh_src))
		    break;
		  else
		    retval = ferror (fh_src);
		}
	      else
		{
		  if (fwrite (buff, 1, bytesr, fh_dst) != bytesr)
		    {
		      retval = ferror (fh_dst);
		      break;
		    }
		}
	    }
	}

      if (fh_dst)
	fclose (fh_dst);

    }

  if (fh_src)
    fclose (fh_src);

  return retval;
}

static void
usage (void)
{
  fprintf (stderr,
	   "Usage: loadjava [OPTION] database-name java-class-file\n");
  fprintf (stderr, "Options:\n-y\t%s\n",
	   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADJAVA,
			   LOADJAVA_ARG_FORCE_OVERWRITE_HELP));
}

/*
 * main() - loadjava main function
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
main (int argc, char *argv[])
{
  int status = EXIT_FAILURE;
  int i;
  DB_INFO *db;
  int ret_val;
  char *java_dir = NULL;
  char *class_file_name = NULL;
  char *class_file_path = NULL;
  struct option loadjava_option[] = {
    {"overwrite", 0, 0, 'y'},
    {0, 0, 0, 0}
  };

  /* initialize message catalog for argument parsing and usage() */
  if (utility_initialize () != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, "y",
				loadjava_option, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case 'y':
	  Force_overwrite = true;
	  break;
	default:
	  usage ();
	  msgcat_final();
	  return EXIT_FAILURE;
	}
    }

  if (optind + 1 < argc)
    {
      Dbname = argv[optind];
      Src_class = argv[optind + 1];
    }
  else
    {
      usage ();
      msgcat_final();
      return EXIT_FAILURE;
    }

  Program_name = argv[0];

  if (check_dbname (Dbname))
    {
      goto error;
    }

  if ((db = cfg_find_db (Dbname)) == NULL)
    {
      fprintf (stderr, "database '%s' does not exist.\n", Dbname);
      goto error;
    }

  if ((java_dir =
       (char *) malloc (strlen (db->pathname) + strlen (JAVA_DIR) + 2)) ==
      NULL)
    {
      fprintf (stderr, "out of memory\n");
      goto error;
    }

  sprintf (java_dir, "%s%c%s", db->pathname, SEPERATOR, JAVA_DIR);
  if (mkdir (java_dir, 0744) != 0 && errno != EEXIST)
    {
      fprintf (stderr, "can't create directory: '%s'\n", java_dir);
      goto error;
    }

  for (i = strlen (Src_class); i >= 0; i--)
    {
      if (Src_class[i] == SEPERATOR)
	break;
    }

  class_file_name = &(Src_class[i + 1]);
  if ((class_file_path =
       (char *) malloc (strlen (java_dir) + strlen (class_file_name) + 2)) ==
      NULL)
    {
      fprintf (stderr, "out of memory\n");
      goto error;
    }

  sprintf (class_file_path, "%s%c%s", java_dir, SEPERATOR, class_file_name);
  if ((ret_val = filecopy (Src_class, class_file_path)) < 0)
    {
      fprintf (stderr, "loadjava fail: file operation error\n");
      goto error;
    }

  status = EXIT_SUCCESS;

error:
  if (java_dir)
    {
      free (java_dir);
    }
  if (class_file_path)
    {
      free (class_file_path);
    }
  msgcat_final();

  return (status);
}
