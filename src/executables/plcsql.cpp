/*
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
 * plcsql.cpp - utility for plcsql registration
 *
 */

#include <cstdlib>
#include <pwd.h>
#include <cstring>
#include <cwctype>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <iterator>

#include "environment_variable.h"
#include "utility.h"
#include "util_support.h"
#include "cubrid_getopt.h"
#include "error_manager.h"
#include "dbi.h"
#include "db_client_type.hpp"
#include "network_interface_cl.h"

#define DB_PLCSQL_AS_ARGS(arg) \
  arg.db_name, arg.user_name, arg.passwd, NULL, DB_CLIENT_TYPE_PLCSQL_HELPER

#define PLCSQL_LOG(str) \
  fprintf (stdout, "%s\n", str)

static std::string input_string;

struct plcsql_argument
{
  const char *db_name;
  const char *user_name;
  const char *passwd;
  const char *in_file_name;

  plcsql_argument ()
    : db_name (NULL)
    , user_name (NULL)
    , passwd (NULL)
    , in_file_name (NULL)
  {
    //
  }

  void print (void);
};

void
plcsql_argument::print (void)
{
  fprintf (stdout, "db name = %s, user name = %s, password = %s, input file = %s\n"
	   , db_name ? db_name : NULL
	   , user_name ? user_name : NULL
	   , passwd ? passwd : NULL
	   , in_file_name ? in_file_name : NULL);
}

static void
utility_plcsql_usage (void)
{
  fprintf (stdout, "%s\n", "invalid command");
}

static void
utility_plcsql_print (int message_num, ...)
{
  va_list ap;

  va_start (ap, message_num);
  vfprintf (stderr, utility_get_generic_message (message_num), ap);
  va_end (ap);
}

#if !defined(WINDOWS)
/*
 * csql_get_user_home() - get user home directory from /etc/passwd file
  *   return: 0 if success, -1 otherwise
 *   homedir(in/out) : user home directory
 *   homedir_size(in) : size of homedir buffer
 */
static int
plcsql_get_user_home (char *homedir, int homedir_size)
{
  struct passwd *ptr = NULL;
  uid_t userid = getuid ();

  setpwent ();

  while ((ptr = getpwent ()) != NULL)
    {
      if (userid == ptr->pw_uid)
	{
	  snprintf (homedir, homedir_size, "%s", ptr->pw_dir);
	  endpwent ();
	  return NO_ERROR;
	}
    }
  endpwent ();
  return ER_FAILED;
}
#endif /* !WINDOWS */

/*
 * plcsql_get_real_path() - get the real pathname (without wild/meta chars) using
 *                      the default shell
 *   return: the real path name
 *   pathname(in)
 *
 * Note:
 *   the real path name returned from this function is valid until next this
 *   function call. The return string will not have any leading/trailing
 *   characters other than the path name itself. If error occurred from O.S,
 *   give up the extension and just return the `pathname'.
 */
char *
plcsql_get_real_path (const char *pathname)
{
#if defined(WINDOWS)
  if (pathname == NULL)
    {
      return NULL;
    }

  while (isspace (pathname[0]))
    {
      pathname++;
    }

  if (pathname[0] == '\0')
    {
      return NULL;
    }

  return (char *) pathname;
#else /* ! WINDOWS */
  static char real_path[PATH_MAX];	/* real path name */
  char home[PATH_MAX];

  if (pathname == NULL)
    {
      return NULL;
    }

  while (isspace (pathname[0]))
    {
      pathname++;
    }

  if (pathname[0] == '\0')
    {
      return NULL;
    }

  /*
   * Do tilde-expansion here.
   */
#if !defined(WINDOWS)
  if (pathname[0] == '~')
    {
      if (plcsql_get_user_home (home, sizeof (home)) != NO_ERROR)
	{
	  return NULL;
	}
#endif

      snprintf (real_path, sizeof (real_path), "%s%s", home, &pathname[1]);
    }
  else
    {
      snprintf (real_path, sizeof (real_path), "%s", pathname);
    }

  return real_path;
#endif /* !WINDOWS */
}

/*
 * plcsql_read_file() - read a file into command editor
 *   return: none
 *   file_name(in): input file name
 */
static int
plcsql_read_file (const char *file_name)
{
  static char current_file[PATH_MAX] = "";
  char *p, *q;			/* pointer to string */

  {
    p = plcsql_get_real_path (file_name);	/* get real path name */
    if (p == NULL || p[0] == '\0')
      {
	/*
	 * No filename given; use the last one we were given.  If we've
	 * never received one before we have a genuine error.
	 */
	if (current_file[0] != '\0')
	  {
	    p = current_file;
	  }
	else
	  {
	    PLCSQL_LOG ("ERR_FILE_NAME_MISSED");
	    goto exit;
	  }
      }

    for (q = p; *q != '\0' && !iswspace ((wint_t) (*q)); q++)
      ;

    /* trim trailing blanks */
    for (; *q != '\0' && iswspace ((wint_t) (*q)); q++)
      {
	*q = '\0';
      }

    if (*q != '\0')
      {
	/* contains more than one file name */
	PLCSQL_LOG ("ERR_TOO_MANY_FILE_NAMES");
	goto exit;
      }

    std::ifstream infile (p, std::ios_base::binary);

    if (infile)
      {
	std::vector<char> buf (
		(std::istreambuf_iterator<char> (infile)),
		(std::istreambuf_iterator<char>()));

	input_string.assign (buf.data(), buf.size ());
	infile.close ();
      }
    else
      {
	PLCSQL_LOG ("RR_OS_ERROR");
	goto exit;
      }
    return NO_ERROR;
  }

exit:
  return ER_FAILED;
}

int
parse_options (int argc, char *argv[], plcsql_argument *pl_args, GETOPT_LONG *opts, char *option_string)
{
  utility_make_getopt_optstring (opts, option_string);

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, option_string, opts, &option_index);

      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case CSQL_USER_S:
	  if (pl_args->user_name != NULL)
	    {
	      free ((void *) pl_args->user_name);
	    }
	  pl_args->user_name = strdup (optarg);
	  break;

	case CSQL_PASSWORD_S:
	  if (pl_args->passwd != NULL)
	    {
	      free ((void *) pl_args->passwd);
	    }
	  pl_args->passwd = strdup (optarg);
	  break;

	case CSQL_INPUT_FILE_S:
	  if (pl_args->in_file_name != NULL)
	    {
	      free ((void *) pl_args->in_file_name);
	    }
	  pl_args->in_file_name = strdup (optarg);
	  break;
	default:
	  return ER_FAILED;
	}
    }

  if (argc - optind == 1)
    {
      pl_args->db_name = argv[optind];
    }
  else
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

int
main (int argc, char *argv[])
{
  int error = EXIT_FAILURE;
  char option_string[64];
  plcsql_argument plcsql_arg;

  GETOPT_LONG options[] =
  {
    {CSQL_USER_L, 1, 0, CSQL_USER_S},
    {CSQL_PASSWORD_L, 1, 0, CSQL_PASSWORD_S},
    {CSQL_INPUT_FILE_L, 1, 0, CSQL_INPUT_FILE_S},
    {0, 0, 0, 0}
  };

  {
    if (parse_options (argc, argv, &plcsql_arg, options, option_string) != NO_ERROR)
      {
	goto print_usage;
      }

    plcsql_arg.print ();

    if (db_restart_ex ("PLCSQL Helper", DB_PLCSQL_AS_ARGS (plcsql_arg)) != NO_ERROR)
      {
	PLCSQL_LOG ("connecting with cub_server is failed");
	goto exit_on_end;
      }

    if (plcsql_read_file (plcsql_arg.in_file_name) != NO_ERROR)
      {
	PLCSQL_LOG ("reading PL/CSQL program is failed");
	goto exit_on_end;
      }

    std::cout << "=============================================================" << std::endl;
    std::cout << input_string << std::endl;
    std::cout << "=============================================================" << std::endl;

    std::string output_string;
    if (plcsql_transfer_file (input_string, output_string) != NO_ERROR)
      {
	PLCSQL_LOG ("transferring PL/CSQL program is failed");
	goto exit_on_end;
      }

    std::cout << "*************************************************************" << std::endl;
    std::cout << output_string << std::endl;
    std::cout << "*************************************************************" << std::endl;

    error = NO_ERROR;
    goto exit_on_end;
  }

print_usage:
  utility_plcsql_usage ();
  error = EXIT_FAILURE;
  /* fall through */

exit_on_end:
  return error;
}
