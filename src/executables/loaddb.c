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
 * loaddb.c - Main for database loader
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>

#if !defined (WINDOWS)
#include <unistd.h>
#include <sys/param.h>
#endif

#include "db.h"
#include "utility.h"
#include "misc_string.h"
#if defined (LDR_OLD_LOADDB)
#include "loader_old.h"
#else /* !LDR_OLD_LOADDB */
#include "loader.h"
#include "load_object.h"
#include "environment_variable.h"
#endif /* LDR_OLD_LOADDB */
#include "message_catalog.h"
#include "log_manager.h"
#include "chartype.h"
#include "schema_manager.h"
#include "transform.h"
#include "server_interface.h"
#include "load_object.h"
#include "authenticate.h"
#include "dbi.h"
#include "network_interface_cl.h"
#include "util_func.h"

#if defined (SA_MODE)
extern bool locator_Dont_check_foreign_key;	/* from locator_sr.h */
#endif

extern void do_loader_parse (FILE * fp);

#define LOADDB_INIT_DEBUG()
#define LOADDB_DEBUG_PRINTF(x)

#define LOAD_INDEX_MIN_SORT_BUFFER_PAGES 8192
#define LOAD_INDEX_MIN_SORT_BUFFER_PAGES_STRING "8192"

static const char *Volume = "";
static const char *Input_file = "";
static const char *Schema_file = "";
static const char *Index_file = "";
static const char *Object_file = "";
static const char *Error_file = "";
static const char *User_name = NULL;
static const char *Password = NULL;
static const char *Ignore_class_file = NULL;
static bool Syntax_check = false;
/* No syntax checking performed */
static bool Load_only = false;
static bool Verbose = false;
#if 0
#if !defined(LDR_OLD_LOADDB)
static int No_optimization = 0;
#endif /* !LDR_OLD_LOADDB */
#endif
static int Verbose_commit = 0;
static int Estimated_size = 5000;
static bool Disable_statistics = false;
#if 0
static bool obsolete_Disable_statistics = false;
#endif
static int Periodic_commit = 0;
/* Don't ignore logging */
static int Ignore_logging = 0;
static int Interrupt_type = LDR_NO_INTERRUPT;
static int schema_file_start_line = 1;
static int index_file_start_line = 1;

#define LOADDB_LOG_FILENAME "loaddb.log"
static FILE *loaddb_log_file;

bool No_oid_hint = false;

/* The number of objects inserted if an interrupted occurred. */
int Total_objects_loaded = 0;

/* Jump buffer for loader to jump to if we have an interrupt */
static jmp_buf loaddb_jmp_buf;

int interrupt_query = false;
jmp_buf ldr_exec_query_status;

static int ldr_validate_object_file (FILE * outfp, const char *argv0);
static int ldr_check_file_name_and_line_no (void);
static void signal_handler (void);
static void loaddb_report_num_of_commits (int num_committed);
static void loaddb_get_num_of_inserted_objects (int num_objects);
#if defined (WINDOWS)
static int run_proc (char *path, char *cmd_line);
#endif /* WINDOWS */
static int loaddb_internal (UTIL_FUNCTION_ARG * arg, int dba_mode);
static void ldr_exec_query_interrupt_handler (void);
static int ldr_exec_query_from_file (const char *file_name, FILE * file,
				     int *start_line, int commit_period);
#if !defined (LDR_OLD_LOADDB)
static int get_ignore_class_list (const char *filename);
static void free_ignoreclasslist (void);
#endif

/*
 * print_log_msg - print log message
 *    return: void
 *    verbose(in): if set also print the message to standard output
 *    fmt(in): string format
 *    ...(in): string format arguments
 */
void
print_log_msg (int verbose, const char *fmt, ...)
{
  va_list ap;

  if (verbose)
    {
      va_start (ap, fmt);
      vprintf (fmt, ap);
      fflush (stdout);
      va_end (ap);
    }

  va_start (ap, fmt);
  vfprintf (loaddb_log_file, fmt, ap);
  fflush (loaddb_log_file);
  va_end (ap);
}

/*
 * load_usage() - print an usage of the load-utility
 *   return: void
 */
static void
load_usage (const char *argv0)
{
#ifdef LDR_OLD_LOADDB
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_USAGE + 1));
#else
  const char *exec_name;

  exec_name = basename ((char *) argv0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_USAGE),
	   exec_name);
#endif
}

/*
 * ldr_validate_object_file - check input file arguments
 *    return: 0 if successful, 1 if error
 *    outfp(out): error message destination
 */
static int
ldr_validate_object_file (FILE * outfp, const char *argv0)
{
  if (Volume == NULL)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_LOADDB,
				       LOADDB_MSG_MISSING_DBNAME));
      load_usage (argv0);
      return 1;
    }

  if (Input_file[0] == 0 && Object_file[0] == 0)
    {
      /* if schema/index file are specified, process them only */
      if (Schema_file[0] == 0 && Index_file[0] == 0)
	{
	  load_usage (argv0);
	  return 1;
	}
      else
	return 0;
    }
  else if (Input_file[0] != 0 && Object_file[0] != 0 &&
	   strcmp (Input_file, Object_file) != 0)
    {
      fprintf (outfp,
	       msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_GENERAL,
			       MSGCAT_GENERAL_ARG_DUPLICATE), "input-file");
      return 1;
    }
  else
    {
      if (Object_file[0] == 0)
	Object_file = Input_file;
      return 0;
    }
}


/*
 * ldr_check_file_name_and_line_no - parse schema file option
 *    return: void
 */
static int
ldr_check_file_name_and_line_no (void)
{
  char *p, *q;

  if (Schema_file[0] != 0)
    {
      if ((p = (char *) strchr (Schema_file, ':')) != NULL)
	{
	  for (q = p + 1; *q; q++)
	    if (!char_isdigit (*q))
	      break;
	  if (*q == 0)
	    {
	      schema_file_start_line = atoi (p + 1);
	      *p = 0;
	    }
	}
    }

  if (Index_file[0] != 0)
    {
      if ((p = (char *) strchr (Index_file, ':')) != NULL)
	{
	  for (q = p + 1; *q; q++)
	    if (!char_isdigit (*q))
	      break;
	  if (*q == 0)
	    {
	      index_file_start_line = atoi (p + 1);
	      *p = 0;
	    }
	}
    }

  return 0;
}


/*
 * signal_handler - signal handler registered via util_arm_signal_handlers
 *    return: void
 */
static void
signal_handler (void)
{
  LOADDB_DEBUG_PRINTF (("Signal caught : interrupt flag : %d\n",
			Interrupt_type));

  /* Flag the loader that that an interrupt has occurred. */
  ldr_interrupt_has_occurred (Interrupt_type);

  print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
				    MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_SIG1));
}

/*
 * loaddb_report_num_of_commits - report number of commits
 *    return: void
 *    num_committed(in): number of commits
 *
 * Note:
 *    registered as a callback function the loader will call this function
 *    to report the number of insertion that have taken place if the '-vc',
 *    verbose commit parameter was specified.
 */
static void
loaddb_report_num_of_commits (int num_committed)
{
  print_log_msg (Verbose_commit,
		 msgcat_message (MSGCAT_CATALOG_UTILS,
				 MSGCAT_UTIL_SET_LOADDB,
				 LOADDB_MSG_COMMITTED_INSTANCES),
		 num_committed);
}

/*
 * loaddb_get_num_of_inserted_objects - set of object inserted value to
 * Total_objects_loaded global variable
 *    return: void
 *    num_objects(in): number of inserted object to set
 */
static void
loaddb_get_num_of_inserted_objects (int num_objects)
{
  Total_objects_loaded = num_objects;
}

#if defined (WINDOWS)
/*
 * run_proc - run a process with a given command_line
 *    return: 0 for success, non-zero otherwise
 *    path(in): progarm path
 *    cmd_line(in): command line
 */
static int
run_proc (char *path, char *cmd_line)
{
  STARTUPINFO start_info;
  PROCESS_INFORMATION proc_info;
  BOOL status;

  GetStartupInfo (&start_info);
  start_info.wShowWindow = SW_HIDE;
  start_info.dwFlags = STARTF_USESTDHANDLES;
  start_info.hStdInput = GetStdHandle (STD_INPUT_HANDLE);
  start_info.hStdOutput = GetStdHandle (STD_OUTPUT_HANDLE);
  start_info.hStdError = GetStdHandle (STD_ERROR_HANDLE);

  status =
    CreateProcess (path, cmd_line, NULL, NULL, true, 0, NULL, NULL,
		   &start_info, &proc_info);
  if (status == false)
    {
      LPVOID lpMsgBuf;
      if (FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER |
			 FORMAT_MESSAGE_FROM_SYSTEM |
			 FORMAT_MESSAGE_IGNORE_INSERTS,
			 NULL,
			 GetLastError (),
			 MAKELANGID (LANG_NEUTRAL,
				     SUBLANG_DEFAULT),
			 (LPTSTR) & lpMsgBuf, 0, NULL))
	{
	  printf ("%s\n", lpMsgBuf);
	  LocalFree (lpMsgBuf);
	}

      return -1;
    }

  WaitForSingleObject (proc_info.hProcess, INFINITE);
  CloseHandle (proc_info.hProcess);
  CloseHandle (proc_info.hThread);
  return 0;
}
#endif /* WINDOWS */


/*
 * loaddb_internal - internal main loaddb function
 *    return: NO_ERROR if successful, error code otherwise
 *    argc(in): argc of main
 *    argv(in): argv of main
 *    dba_mode(in):
 */
static int
loaddb_internal (UTIL_FUNCTION_ARG * arg, int dba_mode)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  int error = NO_ERROR;
  /* set to static to avoid copiler warning (clobbered by longjump) */
  static FILE *schema_file = NULL;
  static FILE *index_file = NULL;
  static FILE *object_file = NULL;
  FILE *error_file = NULL;
  int status = 0;
  int errors, objects, defaults;
#if !defined (LDR_OLD_LOADDB)
  int lastcommit = 0;
#endif /* !LDR_OLD_LOADDB */
  char *passwd;
  /* set to static to avoid copiler warning (clobbered by longjump) */
  static int interrupted = false;
  int au_save = 0;
  extern bool obt_Enable_autoincrement;

  char log_file_name[PATH_MAX];

  LOADDB_INIT_DEBUG ();
  obt_Enable_autoincrement = false;

  Volume = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  Input_file = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE,
						1);
  User_name = utility_get_option_string_value (arg_map, LOAD_USER_S, 0);
  Password = utility_get_option_string_value (arg_map, LOAD_PASSWORD_S, 0);
  Syntax_check = utility_get_option_bool_value (arg_map, LOAD_CHECK_ONLY_S);
  Load_only = utility_get_option_bool_value (arg_map, LOAD_LOAD_ONLY_S);
  Estimated_size = utility_get_option_int_value (arg_map,
						 LOAD_ESTIMATED_SIZE_S);
  Verbose = utility_get_option_bool_value (arg_map, LOAD_VERBOSE_S);
  Disable_statistics =
    utility_get_option_bool_value (arg_map, LOAD_NO_STATISTICS_S);
  Periodic_commit = utility_get_option_int_value (arg_map,
						  LOAD_PERIODIC_COMMIT_S);
  Verbose_commit = Periodic_commit > 0 ? true : false;
  No_oid_hint = utility_get_option_bool_value (arg_map, LOAD_NO_OID_S);
  Schema_file = utility_get_option_string_value (arg_map, LOAD_SCHEMA_FILE_S,
						 0);
  Index_file = utility_get_option_string_value (arg_map, LOAD_INDEX_FILE_S,
						0);
  Object_file = utility_get_option_string_value (arg_map, LOAD_DATA_FILE_S,
						 0);
  Error_file = utility_get_option_string_value (arg_map,
						LOAD_ERROR_CONTROL_FILE_S, 0);
  Ignore_logging = utility_get_option_bool_value (arg_map,
						  LOAD_IGNORE_LOGGING_S);
#if !defined (LDR_OLD_LOADDB)
  Ignore_class_file = utility_get_option_string_value (arg_map,
						       LOAD_IGNORE_CLASS_S,
						       0);
#endif

  Input_file = Input_file ? Input_file : "";
  Schema_file = Schema_file ? Schema_file : "";
  Index_file = Index_file ? Index_file : "";
  Object_file = Object_file ? Object_file : "";
  Error_file = Error_file ? Error_file : "";

  if (ldr_validate_object_file (stderr, arg->argv0))
    {
      goto error_return;
    }

  /* error message log file */
  sprintf (log_file_name, "%s_%s.err", Volume, arg->command_name);
  er_init (log_file_name, ER_NEVER_EXIT);

  if (Index_file[0] != '\0'
      && PRM_SR_NBUFFERS < LOAD_INDEX_MIN_SORT_BUFFER_PAGES)
    {
      sysprm_set_force (PRM_NAME_SR_NBUFFERS,
			LOAD_INDEX_MIN_SORT_BUFFER_PAGES_STRING);
    }
  sysprm_set_force (PRM_NAME_JAVA_STORED_PROCEDURE, "no");

  /* login */
  if (User_name != NULL || !dba_mode)
    {
      (void) db_login (User_name, Password);
      if ((error = db_restart (arg->command_name, true, Volume)))
	{
	  if (error == ER_AU_INVALID_PASSWORD)
	    {
	      /* prompt for password and try again */
	      error = NO_ERROR;
	      passwd = getpass (msgcat_message (MSGCAT_CATALOG_UTILS,
						MSGCAT_UTIL_SET_LOADDB,
						LOADDB_MSG_PASSWORD_PROMPT));
	      if (!strlen (passwd))
		passwd = NULL;
	      (void) db_login (User_name, passwd);
	      error = db_restart (arg->command_name, true, Volume);
	    }
	}
    }
  else
    {
      /* if we're in the protected dba mode, just login without
         authorization */
      AU_DISABLE_PASSWORDS ();
      db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
      (void) db_login ("dba", NULL);
      error = db_restart (arg->command_name, true, Volume);
    }

  /* open loaddb log file */
  sprintf (log_file_name, "%s_loaddb.log", Volume);
  loaddb_log_file = fopen (log_file_name, "w+");
  if (loaddb_log_file == NULL)
    {
      printf ("Cannot open log file %s\n", log_file_name);
      status = 2;
      goto error_return;
    }

#if 0
#if !defined (LDR_OLD_LOADDB)

  /* Execute old loaddb if no optimization flag is set true
   * or LOADDB_NOPT is set, we must pass the argv except
   * -no option and invoke execvp() for no optimized loaddb
   */
  if (No_optimization || envvar_get ("LOADDB_NOPT"))
    {

      char **tmp;
      char *lastslash, path[PATH_MAX];
      int i = 1, j = 1;

      tmp = (char **) malloc (sizeof (char *) * (argc + 1));
      tmp[0] = (char *) "loaddb";
      while (j < argc)
	{
	  if (!strcmp (argv[j], "-no"))
	    j++;
	  else
	    tmp[i++] = argv[j++];
	};
      tmp[i] = 0;

      strcpy (path, argv[0]);
      lastslash = strrchr (path, (int) '/');
#if defined(WINDOWS)
      {
	char *p, exec_path[1024], cmd_line[1024 * 8];
	int cp_len = 0;

	db_shutdown ();

	p = envvar_root ();
	if (p == NULL)
	  {
	    printf ("The `CUBRID' environment variable is not set.\n");
	  }
	else
	  {
	    sprintf (exec_path, "%s/migdb_o.exe", p);
	    for (i = 0; tmp[i]; i++)
	      {
		cp_len += sprintf (cmd_line + cp_len, "\"%s\" ", tmp[i]);
	      }
	    if (envvar_get ("FRONT_DEBUG") != NULL)
	      {
		printf ("Executing:%s %s\n", exec_path, cmd_line);
	      }
	    run_proc (exec_path, cmd_line);
	  }
	exit (0);
      }
#else /* !WINDOWS */
      if (lastslash != NULL)
	strcpy (lastslash + 1, "migdb_o");
      else
	strcpy (path, "migdb_o");

      if (execvp (path, tmp) == -1)
	{
	  print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
					    MSGCAT_UTIL_SET_LOADDB,
					    LOADDB_MSG_NOPT_ERR));
	  exit (0);
	};
#endif /* WINDOWS */
    }
#endif /* !LDR_OLD_LOADDB */
#endif

  /* check if schema/index/object files exist */
  ldr_check_file_name_and_line_no ();

  if (Schema_file[0] != 0)
    {
      schema_file = fopen (Schema_file, "r");
      if (schema_file == NULL)
	{
	  print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
					    MSGCAT_UTIL_SET_LOADDB,
					    LOADDB_MSG_BAD_INFILE),
			 Schema_file);
	  status = 2;
	  goto error_return;
	}
    }
  if (Index_file[0] != 0)
    {
      index_file = fopen (Index_file, "r");
      if (index_file == NULL)
	{
	  print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
					    MSGCAT_UTIL_SET_LOADDB,
					    LOADDB_MSG_BAD_INFILE),
			 Index_file);
	  status = 2;
	  goto error_return;
	}
    }
  if (Object_file[0] != 0)
    {
      object_file = fopen_ex (Object_file, "rb");	/* keep out ^Z */

      if (object_file == NULL)
	{
	  print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
					    MSGCAT_UTIL_SET_LOADDB,
					    LOADDB_MSG_BAD_INFILE),
			 Object_file);
	  status = 2;
	  goto error_return;
	}
    }

#if !defined (LDR_OLD_LOADDB)
  if (Ignore_class_file)
    {
      int retval;
      retval = get_ignore_class_list (Ignore_class_file);

      if (retval < 0)
	{
	  status = 2;
	  goto error_return;
	}
    }
#endif

  /* Disallow syntax only and load only options together */
  if (Load_only && Syntax_check)
    {
      print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
					MSGCAT_UTIL_SET_LOADDB,
					LOADDB_MSG_INCOMPATIBLE_ARGS),
		     "--" LOAD_LOAD_ONLY_L, "--" LOAD_CHECK_ONLY_L);
      status = 1;		/* parsing error */
      goto error_return;
    }

  if (Error_file[0] != 0)
    {
      if (Syntax_check)
	{
	  print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
					    MSGCAT_UTIL_SET_LOADDB,
					    LOADDB_MSG_INCOMPATIBLE_ARGS),
			 "--" LOAD_ERROR_CONTROL_FILE_L,
			 "--" LOAD_CHECK_ONLY_L);
	  status = 1;		/* parsing error */
	  goto error_return;
	}
      error_file = fopen_ex (Error_file, "rt");
      if (error_file == NULL)
	{
	  print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
					    MSGCAT_UTIL_SET_LOADDB,
					    LOADDB_MSG_BAD_INFILE),
			 Error_file);
	  status = 2;
	  goto error_return;
	}
      er_filter_fileset (error_file);
      fclose (error_file);
    }

  /* check if no log option can be applied */
  if (error
      || (Ignore_logging != 0 && locator_log_force_nologging () != NO_ERROR))
    {
      /* couldn't log in */
      print_log_msg (1, "%s\n", db_error_string (3));
      status = 3;
      db_shutdown ();
      goto error_return;
    }

  /* change "print_key_value_on_unique_error" parameter */
  sysprm_change_server_parameters ("print_key_value_on_unique_error=1");

  /* if schema file is specified, do schema loading */
  if (schema_file != NULL)
    {
      print_log_msg (1, "\nStart schema loading.\n");

      /*
       * CUBRID 8.2 should be compatible with earlier versions of CUBRID.
       * Therefore, we do not perform user authentication when the loader
       * is executing by DBA group user.
       */
      if (au_is_dba_group_member (Au_user))
	{
	  AU_DISABLE (au_save);
	}

      if (ldr_exec_query_from_file (Schema_file, schema_file,
				    &schema_file_start_line,
				    Periodic_commit) != 0)
	{
	  print_log_msg (1, "\nError occurred during schema loading."
			 "\nAborting current transaction...");
	  status = 3;
	  db_shutdown ();
	  print_log_msg (1,
			 " done.\n\nRestart loaddb with '-%c %s:%d' option\n",
			 LOAD_SCHEMA_FILE_S, Schema_file,
			 schema_file_start_line);
	  goto error_return;
	}

      if (au_is_dba_group_member (Au_user))
	{
	  AU_ENABLE (au_save);
	}

      print_log_msg (1, "Schema loading from %s finished.\n", Schema_file);

      /* update catalog statistics */
      AU_DISABLE (au_save);
      sm_update_all_catalog_statistics ();
      AU_ENABLE (au_save);

      print_log_msg (1,
		     "Statistics for Catalog classes have been updated.\n\n");

      db_commit_transaction ();
      fclose (schema_file);
      schema_file = NULL;

    }


  /* if index file is specified, do index creation */

  if (object_file != NULL)
    {
#if defined (SA_MODE)
      locator_Dont_check_foreign_key = true;
#endif
      print_log_msg (1, "\nStart object loading.\n");
      ldr_init (Verbose);

      /* set the flag to indicate what type of interrupts to raise
       * If logging has been disabled set commit flag.
       * If logging is enabled set abort flag.
       */

      if (Ignore_logging)
	Interrupt_type = LDR_STOP_AND_COMMIT_INTERRUPT;
      else
	Interrupt_type = LDR_STOP_AND_ABORT_INTERRUPT;

      if (Periodic_commit)
	{
	  /* register the post commit function */
#if defined(LDR_OLD_LOADDB)
	  ldr_register_post_commit_handler (&loaddb_report_num_of_commits);
#else /* !LDR_OLD_LOADDB */
	  ldr_register_post_commit_handler (&loaddb_report_num_of_commits,
					    NULL);
#endif /* LDR_OLD_LOADDB */
	}

      /* Check if we need to perform syntax checking. */
      if (!Load_only)
	{
	  print_log_msg ((int) Verbose, msgcat_message (MSGCAT_CATALOG_UTILS,
							MSGCAT_UTIL_SET_LOADDB,
							LOADDB_MSG_CHECKING));
	  do_loader_parse (object_file);
#if defined(LDR_OLD_LOADDB)
	  ldr_stats (&errors, &objects, &defaults);
#else /* !LDR_OLD_LOADDB */
	  ldr_stats (&errors, &objects, &defaults, &lastcommit);
#endif /* LDR_OLD_LOADDB */
	}
      else
	errors = 0;

      if (errors)
	print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
					  MSGCAT_UTIL_SET_LOADDB,
					  LOADDB_MSG_ERROR_COUNT), errors);
      else if (!Syntax_check)
	{
	  /* now do it for real if there were no errors and we aren't
	     doing a simple syntax check */
	  ldr_start (Periodic_commit);
	  fclose (object_file);
	  object_file = fopen_ex (Object_file, "rb");	/* keep out ^Z */
	  if (object_file != NULL)
	    {
	      print_log_msg ((int) Verbose,
			     msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_LOADDB,
					     LOADDB_MSG_INSERTING));

	      /* make sure signals are caught */
	      util_arm_signal_handlers (signal_handler, signal_handler);

	      /* register function to call  and jmp environment to longjmp to
	       * after aborting or committing.
	       */
	      ldr_register_post_interrupt_handler
		(&loaddb_get_num_of_inserted_objects, &loaddb_jmp_buf);

	      if (setjmp (loaddb_jmp_buf) != 0)
		{

		  /* We have had an interrupt, the transaction should have
		   * been already been aborted or committed by the loader.
		   * If Total_objects_loaded is -1 an error occurred during
		   * rollback or commit.
		   */
		  if (Total_objects_loaded != -1)
		    print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
						      MSGCAT_UTIL_SET_LOADDB,
						      LOADDB_MSG_OBJECT_COUNT),
				   Total_objects_loaded);
#if !defined(LDR_OLD_LOADDB)
		  ldr_stats (&errors, &objects, &defaults, &lastcommit);
		  if (lastcommit > 0)
		    print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
						      MSGCAT_UTIL_SET_LOADDB,
						      LOADDB_MSG_LAST_COMMITTED_LINE),
				   lastcommit);
#endif /* !LDR_OLD_LOADDB */
		  interrupted = true;

		}
	      else
		{
		  do_loader_parse (object_file);
#if defined(LDR_OLD_LOADDB)
		  ldr_stats (&errors, &objects, &defaults);
#else /* !LDR_OLD_LOADDB */
		  ldr_stats (&errors, &objects, &defaults, &lastcommit);
#endif /* LDR_OLD_LOADDB */
		  if (errors)
		    {
#if defined(LDR_OLD_LOADDB)
		      print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
							MSGCAT_UTIL_SET_LOADDB,
							LOADDB_MSG_ERROR_COUNT),
				     errors);
#else /* !LDR_OLD_LOADDB */
		      if (lastcommit > 0)
			print_log_msg (1,
				       msgcat_message (MSGCAT_CATALOG_UTILS,
						       MSGCAT_UTIL_SET_LOADDB,
						       LOADDB_MSG_LAST_COMMITTED_LINE),
				       lastcommit);
#endif /* LDR_OLD_LOADDB */
		      /*
		       * don't allow the transaction to be committed at
		       * this point, note that if we ever move to a scheme
		       * where we write directly to the heap without the
		       * transaction context, we will have to unwind the
		       * changes made if errors are detected !
		       */
		      db_abort_transaction ();
		    }
		  else
		    {
		      if (objects)
			print_log_msg (1,
				       msgcat_message (MSGCAT_CATALOG_UTILS,
						       MSGCAT_UTIL_SET_LOADDB,
						       LOADDB_MSG_OBJECT_COUNT),
				       objects);
		      if (defaults)
			print_log_msg (1,
				       msgcat_message (MSGCAT_CATALOG_UTILS,
						       MSGCAT_UTIL_SET_LOADDB,
						       LOADDB_MSG_DEFAULT_COUNT),
				       defaults);
		      print_log_msg ((int) Verbose,
				     msgcat_message (MSGCAT_CATALOG_UTILS,
						     MSGCAT_UTIL_SET_LOADDB,
						     LOADDB_MSG_COMMITTING));

		      /* commit the transaction and then update statistics */
		      if (!db_commit_transaction ())
			{
			  if (!Disable_statistics)
			    {
			      if (Verbose)
				print_log_msg (1,
					       msgcat_message
					       (MSGCAT_CATALOG_UTILS,
						MSGCAT_UTIL_SET_LOADDB,
						LOADDB_MSG_UPDATING_STATISTICS));
			      if (!ldr_update_statistics ())
				{
				  /*
				   * would it be faster to update statistics
				   * before the first commit and just have a
				   * single commit ?
				   */
				  print_log_msg ((int) Verbose,
						 msgcat_message
						 (MSGCAT_CATALOG_UTILS,
						  MSGCAT_UTIL_SET_LOADDB,
						  LOADDB_MSG_COMMITTING));
				  (void) db_commit_transaction ();
				}
			    }
			}
		    }
		}
	    }
	}
      ldr_final ();
      if (object_file != NULL)
	{
	  fclose (object_file);
	  object_file = NULL;
	}
    }

  /* create index */
  if (!interrupted && index_file != NULL)
    {
      print_log_msg (1, "\nStart index loading.\n");
      if (ldr_exec_query_from_file (Index_file, index_file,
				    &index_file_start_line,
				    Periodic_commit) != 0)
	{
	  print_log_msg (1, "\nError occurred during index loading."
			 "\nAborting current transaction...");
	  status = 3;
	  db_shutdown ();
	  print_log_msg (1,
			 " done.\n\nRestart loaddb with '-%c %s:%d' option\n",
			 LOAD_INDEX_FILE_S, Index_file,
			 index_file_start_line);
	  goto error_return;
	}
      /* update catalog statistics */
      AU_DISABLE (au_save);
      sm_update_catalog_statistics (CT_INDEX_NAME);
      sm_update_catalog_statistics (CT_INDEXKEY_NAME);
      AU_ENABLE (au_save);

      print_log_msg (1, "Index loading from %s finished.\n", Index_file);
      db_commit_transaction ();
    }
  print_log_msg ((int) Verbose, msgcat_message (MSGCAT_CATALOG_UTILS,
						MSGCAT_UTIL_SET_LOADDB,
						LOADDB_MSG_CLOSING));
  (void) db_shutdown ();

#if !defined (LDR_OLD_LOADDB)
  free_ignoreclasslist ();
#endif
  return (status);
error_return:
  if (schema_file != NULL)
    fclose (schema_file);
  if (object_file != NULL)
    fclose (object_file);
  if (index_file != NULL)
    fclose (index_file);

#if !defined (LDR_OLD_LOADDB)
  free_ignoreclasslist ();
#endif

  return status;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * loaddb_dba - loaddb in dba mode
 *    return:  NO_ERROR if successful, error code otherwise
 *    argc(in): argc in main
 *    argv(in): argv in main
 */
int
loaddb_dba (UTIL_FUNCTION_ARG * arg)
{
  return loaddb_internal (arg, 1);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * loaddb_user - loaddb in user mode
 *    return:  NO_ERROR if successful, error code otherwise
 *    argc(in): argc in main
 *    argv(in): argv in main
 */
int
loaddb_user (UTIL_FUNCTION_ARG * arg)
{
  return loaddb_internal (arg, 0);
}


/*
 * ldr_exec_query_interrupt_handler - signal handler registered via
 * util_arm_signal_handlers
 *    return: void
 */
static void
ldr_exec_query_interrupt_handler (void)
{
  interrupt_query = true;

  log_set_interrupt (true);
}

/*
 * ldr_exec_query_from_file - execute queries from file
 *    return: 0 if successful, non-zero otherwise
 *    file_name(in): file path
 *    file(in): FILE *
 *    start_line(in): start line
 *    commit_period(in): commit period
 */
static int
ldr_exec_query_from_file (const char *file_name, FILE * input_stream,
			  int *start_line, int commit_period)
{
  DB_SESSION *session = NULL;
  DB_QUERY_RESULT *res = NULL;
  int error = 0;
  int stmt_cnt, stmt_id = 0, stmt_type;
  int executed_cnt = 0;
  int parser_start_line_no;
  int parser_end_line_no = 1;
  int check_line_no = true;

  if ((*start_line) > 1)
    {
      int line_count = *start_line - 1;

      do
	{
	  int c = fgetc (input_stream);
	  if (c == EOF)
	    {
	      print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS,
						MSGCAT_UTIL_SET_LOADDB,
						LOADDB_MSG_UNREACHABLE_LINE),
			     file_name, *start_line);
	      error = ER_GENERIC_ERROR;
	      goto end;
	    }
	  else if (c == '\n')
	    {
	      line_count--;
	    }
	}
      while (line_count > 0);
    }

  check_line_no = false;
  session = db_make_session_for_one_statement_execution (input_stream);
  if (session == NULL)
    {
      print_log_msg (1, "ERROR: %s\n", db_error_string (3));
      error = er_errid ();
      goto end;
    }

  util_arm_signal_handlers (&ldr_exec_query_interrupt_handler,
			    &ldr_exec_query_interrupt_handler);

  while (1)
    {
      if (interrupt_query)
	{
	  if (er_errid () != ER_INTERRUPTED)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	    }
	  error = er_errid ();
	  db_close_session (session);
	  goto end;
	}
      parser_start_line_no = parser_end_line_no;

      stmt_cnt = db_parse_one_statement (session);
      if (stmt_cnt > 0)
	{
	  db_get_parser_line_col (session, &parser_end_line_no, NULL);
	  stmt_id = db_compile_statement (session);
	}

      if (stmt_cnt <= 0 || stmt_id <= 0)
	{
	  DB_SESSION_ERROR *session_error;
	  int line, col;
	  if ((session_error = db_get_errors (session)) != NULL)
	    {
	      do
		{
		  session_error =
		    db_get_next_error (session_error, &line, &col);
		  if (line >= 0)
		    {
		      print_log_msg (1, "In %s line %d,\n", file_name,
				     line + (*start_line));
		      print_log_msg (1, "ERROR: %s \n", db_error_string (3));
		      error = er_errid ();
		    }
		}
	      while (session_error);
	    }
	  db_close_session (session);
	  break;
	}

      stmt_type = db_get_statement_type (session, stmt_id);

      res = (DB_QUERY_RESULT *) NULL;
      error = db_execute_statement (session, stmt_id, &res);

      if (error < 0)
	{
	  print_log_msg (1, "ERROR: %s\n", db_error_string (3));
	  db_close_session (session);
	  break;
	}
      executed_cnt++;
      error = db_query_end (res);
      if (error < 0)
	{
	  print_log_msg (1, "ERROR: %s\n", db_error_string (3));
	  db_close_session (session);
	  break;
	}

      if (stmt_type == CUBRID_STMT_COMMIT_WORK ||
	  (commit_period && (executed_cnt % commit_period == 0)))
	{
	  db_commit_transaction ();
	  print_log_msg (Verbose_commit,
			 "%8d statements executed. Commit transaction at line %d\n",
			 executed_cnt, parser_end_line_no);
	  *start_line = parser_end_line_no + 1;
	}
      print_log_msg ((int) Verbose, "Total %8d statements executed.\r",
		     executed_cnt);
      fflush (stdout);
    }

end:
  if (error < 0)
    {
      db_abort_transaction ();
    }
  else
    {
      *start_line = parser_end_line_no + 1;
      print_log_msg (1, "Total %8d statements executed.\n", executed_cnt);
      fflush (stdout);
      db_commit_transaction ();
    }
  return error;
}

#if !defined (LDR_OLD_LOADDB)
static int
get_ignore_class_list (const char *inputfile_name)
{
  int inc_unit = 128;
  int list_size;
  FILE *input_file = NULL;
  char buffer[DB_MAX_IDENTIFIER_LENGTH], buffer_scan_format[16];
  char class_name[DB_MAX_IDENTIFIER_LENGTH];

  if (ignoreClasslist != NULL)
    {
      free_ignoreclasslist ();
    }

  if (inputfile_name == NULL)
    {
      return 0;
    }

  input_file = fopen (inputfile_name, "r");
  if (input_file == NULL)
    {
      perror (inputfile_name);
      return 1;
    }

  ignoreClasslist = (char **) malloc (sizeof (char *) * inc_unit);
  if (ignoreClasslist == NULL)
    {
      goto error;
    }

  memset (ignoreClasslist, '\0', inc_unit);

  list_size = inc_unit;
  ignoreClassnum = 0;

  snprintf (buffer_scan_format, sizeof (buffer_scan_format), "%%%ds\n",
	    (int) (sizeof (buffer) - 1));

  while (fgets ((char *) buffer, DB_MAX_IDENTIFIER_LENGTH,
		input_file) != NULL)
    {
      if ((strchr (buffer, '\n') - buffer) >= 1)
	{
	  if (ignoreClassnum >= list_size)
	    {
	      ignoreClasslist =
		(char **) realloc (ignoreClasslist,
				   sizeof (char *) * (list_size + inc_unit));
	      if (ignoreClasslist == NULL)
		{
		  goto error;
		}

	      memset (ignoreClasslist + list_size, '\0', inc_unit);
	      list_size = list_size + inc_unit;
	    }

	  sscanf ((char *) buffer, buffer_scan_format, (char *) class_name);
	  ignoreClasslist[ignoreClassnum] = strdup (class_name);
	  if (ignoreClasslist[ignoreClassnum] == NULL)
	    {
	      goto error;
	    }

	  ignoreClassnum++;
	}
    }

  fclose (input_file);

  return 0;

error:
  free_ignoreclasslist ();
  fclose (input_file);

  return -1;
}

static void
free_ignoreclasslist (void)
{
  int i = 0;

  if (ignoreClasslist != NULL)
    {
      for (i = 0; i < ignoreClassnum; i++)
	{
	  if (*(ignoreClasslist + i) != NULL)
	    {
	      free (*(ignoreClasslist + i));
	    }
	}
      free (ignoreClasslist);
      ignoreClasslist = NULL;
    }
  ignoreClassnum = 0;
}
#endif
