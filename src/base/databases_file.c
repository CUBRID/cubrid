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
 * databases_file.c - Parsing the database directory file
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(WINDOWS)
#include <io.h>
#else /* WINDOWS */
#include <unistd.h>
#endif /* WINDOWS */
#include <assert.h>

#include "porting.h"

#include "chartype.h"
#include "error_manager.h"
#include "databases_file.h"
#include "boot.h"
#include "connection_defs.h"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "system_parameter.h"

#if defined(WINDOWS)
#include "misc_string.h"
#include "wintcp.h"
#endif /* WINDOWS */
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"


/* conservative upper bound of a line in databases.txt */
#define CFG_MAX_LINE 4096

static char CFG_HOST_SEPARATOR = ':';

static char *cfg_next_char (char *str_p);
static char *cfg_next_line (char *str_p);
static char *cfg_pop_token (char *str_p, char **token_p);
static char *cfg_pop_linetoken (char *str_p, char **token_p);
static void cfg_get_directory_filename (char *buffer, int *local);

static int cfg_ensure_directory_write (void);
static FILE *cfg_open_directory_file (bool write_flag);

static char **cfg_copy_hosts (const char **host_array, int *num_hosts);
static const char *cfg_pop_host (const char *host_list, char *buffer, int *length);
static bool cfg_host_exists (char *host_list, char *hostname, int num_items);

/* PARSING UTILITIES */
/*
 * cfg_next_char() - Advances the given pointer until a non-whitespace character
 *               or the end of the string are encountered.
 *    return: char *
 *    str_p(in): buffer pointer
 */
static char *
cfg_next_char (char *str_p)
{
  char *p;

  p = str_p;
  while (char_isspace ((int) *p) && *p != '\0')
    {
      p++;
    }

  return (p);
}

/*
 * cfg_next_line()
 *    return: char *
 *    str_p(in): buffer pointer
 */
static char *
cfg_next_line (char *str_p)
{
  char *p;

  p = str_p;
  while (!char_iseol ((int) *p) && *p != '\0')
    {
      p++;
    }
  while (char_iseol ((int) *p) && *p != '\0')
    {
      p++;
    }

  return (p);
}

/*
 * cfg_pop_token() - This looks in the buffer for the next token which is define as
 *               a string of characters surrounded by whitespace
 *    return: char
 *    str_p(in): buffer with tokens
 *    token_p(in/out): returned next token string
 *
 *    Note : When found the token characters are copied into a new string
 *           and returned.
 *           The pointer to the first character following the new token in
 *           the buffer is returned.
 */
static char *
cfg_pop_token (char *str_p, char **token_p)
{
  char *p, *end, *token = NULL;
  int length;

  token = NULL;
  p = str_p;
  while (char_isspace ((int) *p) && *p != '\0')
    {
      p++;
    }
  end = p;
  while (!char_isspace ((int) *end) && *end != '\0')
    {
      end++;
    }

  length = (int) (end - p);
  if (length > 0)
    {
      token = (char *) malloc (length + 1);
      if (token != NULL)
	{
	  strncpy (token, p, length);
	  token[length] = '\0';
	}
    }

  *token_p = token;
  return (end);
}

/*
 * cfg_pop_linetoken()
 *    return: char *
 *    str_p(in):
 *    token_p(in/out):
 */
static char *
cfg_pop_linetoken (char *str_p, char **token_p)
{
  char *p, *end, *token = NULL;
  int length;

  if (str_p == NULL || char_iseol ((int) *str_p))
    {
      *token_p = NULL;
      return str_p;
    }
  token = NULL;
  p = str_p;
  while (char_isspace ((int) *p) && !char_iseol ((int) *p) && *p != '\0')
    {
      p++;
    }
  end = p;

  while (!char_isspace ((int) *end) && *end != '\0')
    {
      end++;
    }

  length = (int) (end - p);
  if (length > 0)
    {
      token = (char *) malloc (length + 1);
      if (token != NULL)
	{
	  strncpy (token, p, length);
	  token[length] = '\0';
	}
    }

  *token_p = token;
  return (end);
}

/*
 * cfg_get_directory_filename() - Finds the full pathname of the database
 *                                directory file.
 *    return: none
 *    buffer(in): character buffer to hold the full path name
 *    local(out): flag set if the file was assumed to be local
 */
static void
cfg_get_directory_filename (char *buffer, int *local)
{
#if !defined (DO_NOT_USE_CUBRIDENV)
  const char *env_name;

  *local = 0;
  env_name = envvar_get (DATABASES_ENVNAME);
  if (env_name == NULL || strlen (env_name) == 0)
    {
      sprintf (buffer, DATABASES_FILENAME);
      *local = 1;
    }
  else
    {
      if (env_name[strlen (env_name) - 1] == '/')
	{
	  sprintf (buffer, "%s%s", env_name, DATABASES_FILENAME);
	}
      else
	{
	  sprintf (buffer, "%s/%s", env_name, DATABASES_FILENAME);
	}
    }
#else
  *local = 0;
  envvar_vardir_file (buffer, PATH_MAX, DATABASES_FILENAME);
#endif
}

/*
 * cfg_os_working_directory() - Returns the current working directory
 *                              in a buffer.
 *                              Buffer returned is static and must be
 *                              copied immediately.
 *    return: char *
 */
char *
cfg_os_working_directory (void)
{
  char *return_str = NULL;

#if defined(WINDOWS)
  static char working_dir[PATH_MAX];
  return_str = _fullpath (working_dir, ".", PATH_MAX);
#else /* WINDOWS */
  return_str = getenv ("PWD");
#endif /* WINDOWS */

  return return_str;
}

/*
 * cfg_maycreate_get_directory_filename()
 *    return: char *
 *    buffer(in):
 */
char *
cfg_maycreate_get_directory_filename (char *buffer)
{
  int local_ignore;
  FILE *file_p = NULL;

  cfg_get_directory_filename (buffer, &local_ignore);
  if ((file_p = fopen (buffer, "a+")) == NULL)
    {
#if !defined(CS_MODE)
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_NO_WRITE_ACCESS, 1, buffer);
#else /* !CS_MODE */
      er_set_with_oserror (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_CFG_NO_WRITE_ACCESS, 1, buffer);
#endif /* !CS_MODE */
      return NULL;
    }
  fclose (file_p);
  return buffer;
}

/*
 * cfg_ensure_directory_write() - Make sure that we can get write access to
 *                                the directory file, if not abort
 *                                the operation.
 *    return: non-zero if directory file is writable
 */
static int
cfg_ensure_directory_write (void)
{
  char filename[PATH_MAX];
  FILE *file_p = NULL;
  int local, status;

  status = 0;

  cfg_get_directory_filename (filename, &local);
  file_p = fopen (filename, "a+");
  if (file_p == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_NO_WRITE_ACCESS, 1, filename);
    }
  else
    {
      status = 1;
      fclose (file_p);
    }

  return (status);
}

/*
 * cfg_open_directory_file()
 *    return: file pointer
 *    write_flag(in): set to open for write
 */
static FILE *
cfg_open_directory_file (bool write_flag)
{
  char filename[PATH_MAX];
  FILE *file_p = NULL;
  int local;

  cfg_get_directory_filename (filename, &local);

  if (write_flag)
    {
#if defined(WINDOWS)
      file_p = fopen (filename, "wbc");	/* write binary commit */
#else /* WINDOWS */
      file_p = fopen (filename, "w");
#endif /* WINDOWS */
    }
  else
    {
      file_p = fopen (filename, "r");
    }
  if (file_p == NULL)
    {
      if (write_flag)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_NO_WRITE_ACCESS, 1, filename);
	}
      else
	{
	  /* Only standalone and server will update the database location file */
#if !defined(CS_MODE)
	  /* no readable file, try to create one */
	  file_p = fopen (filename, "r+");
	  if (file_p == NULL)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_NO_WRITE_ACCESS, 1, filename);
	    }
#else /* !CS_MODE */
	  file_p = NULL;
#endif /* !CS_MODE */
	}
    }
  return (file_p);
}

/*
 * cfg_read_directory() - This reads the database directory file and returns
 *                        a list of descriptors
 *    return: non-zero for success
 *    info_p(out): pointer to returned list of descriptors
 *    write_flag(in): flag indicating write intention
 *
 * Note: The write_flag flag should be set if the file is to be
 *    modified.  This will ensure that write access is available before some
 *    potentially expensive operation like database creation is performed.
 *    A list of hosts where the database could exist is obtained from
 *    cfg_get_hosts() and attached to the DB_INFO structure.
 *
 *    However, cfg_read_directory() has a potential problem that
 *    the file lock is released. The file lock acquired through io_mount()
 *    is released when cfg_read_directory() opens and closes the file.
 */
int
cfg_read_directory (DB_INFO ** info_p, bool write_flag)
{
  char line[CFG_MAX_LINE];
  FILE *file_p = NULL;
  DB_INFO *databases, *last, *db;
  char *str = NULL;
  char *primary_host = NULL;
  int error_code = ER_FAILED;
  char *ha_node_list = NULL;

  databases = last = NULL;

#if defined(SERVER_MODE)
  if (!HA_DISABLED () && prm_get_string_value (PRM_ID_HA_NODE_LIST))
    {
      str = strchr (prm_get_string_value (PRM_ID_HA_NODE_LIST), '@');
      ha_node_list = (str) ? str + 1 : NULL;
    }
#endif

  if (!write_flag || cfg_ensure_directory_write ())
    {
      file_p = cfg_open_directory_file (false);
      if (file_p != NULL)
	{
	  while (fgets (line, CFG_MAX_LINE - 1, file_p) != NULL)
	    {
	      str = cfg_next_char (line);
	      if (*str != '\0' && *str != '#')
		{
		  db = (DB_INFO *) malloc (sizeof (DB_INFO));
		  if (db == NULL)
		    {
		      if (databases != NULL)
			{
			  cfg_free_directory (databases);
			}
		      *info_p = NULL;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_INFO));
		      return ER_OUT_OF_VIRTUAL_MEMORY;
		    }
		  db->next = NULL;
		  str = cfg_pop_token (str, &db->name);
		  str = cfg_pop_token (str, &db->pathname);
		  str = cfg_pop_token (str, &primary_host);
		  if (ha_node_list)
		    {
		      db->hosts = cfg_get_hosts (ha_node_list, &db->num_hosts, false);
		    }
		  else
		    {
		      db->hosts = cfg_get_hosts (primary_host, &db->num_hosts, false);
		    }
		  if (primary_host != NULL)
		    {
		      free_and_init (primary_host);
		    }

		  str = cfg_pop_token (str, &db->logpath);
		  str = cfg_pop_token (str, &db->lobpath);

		  if (databases == NULL)
		    {
		      databases = db;
		    }
		  else
		    {
		      last->next = db;
		    }
		  last = db;
		  if (db->name == NULL || db->pathname == NULL || db->hosts == NULL || db->logpath == NULL
		      /* skip to check above to support backward compatibility || db->lobpath == NULL */ )
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_INVALID_DATABASES, 1, DATABASES_FILENAME);
		      if (databases != NULL)
			{
			  cfg_free_directory (databases);
			}
		      *info_p = NULL;
		      return ER_CFG_INVALID_DATABASES;
		    }
		}
	    }
	  fclose (file_p);
	  error_code = NO_ERROR;
	}
    }
  *info_p = databases;
  return (error_code);
}

/*
 * cfg_read_directory_ex() - This provides same functionality of
 *                           cfg_read_directory().
 *    return: non-zero for success
 *    vdes(in): file descriptor
 *    info_p(out): pointer to returned list of descriptors
 *    write_flag(in): flag indicating write intention
 *
 *    Note: However it does not open/close the file, the file lock is
 *          preserved.
 */
int
cfg_read_directory_ex (int vdes, DB_INFO ** info_p, bool write_flag)
{
  char *line = NULL;
  DB_INFO *databases, *last, *db;
  char *str = NULL;
  char *primary_host = NULL;
  struct stat stat_buffer;
  int error_code = ER_FAILED;

#if defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
  return cfg_read_directory (info_p, write_flag);
#endif /* DONT_USE_MANDATORY_LOCK_IN_WINDOWS */

  databases = last = NULL;

  if (lseek (vdes, 0L, SEEK_SET) == 0L)
    {
      fstat (vdes, &stat_buffer);
      line = (char *) malloc (stat_buffer.st_size + 1);
      if (line == NULL)
	{
	  *info_p = NULL;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (stat_buffer.st_size + 1));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      read (vdes, line, (unsigned int) stat_buffer.st_size);
      line[stat_buffer.st_size] = '\0';
      str = cfg_next_char (line);
      while (*str != '\0')
	{
	  if (*str != '#')
	    {
	      if ((db = (DB_INFO *) malloc (sizeof (DB_INFO))) == NULL)
		{
		  if (databases != NULL)
		    {
		      cfg_free_directory (databases);
		    }
		  *info_p = NULL;
		  free_and_init (line);

		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_INFO));
		  return ER_OUT_OF_VIRTUAL_MEMORY;
		}

	      db->next = NULL;
	      str = cfg_pop_linetoken (str, &db->name);
	      str = cfg_pop_linetoken (str, &db->pathname);
	      str = cfg_pop_linetoken (str, &primary_host);
	      db->hosts = cfg_get_hosts (primary_host, &db->num_hosts, false);
	      if (primary_host != NULL)
		{
		  free_and_init (primary_host);
		}
	      str = cfg_pop_linetoken (str, &db->logpath);
	      str = cfg_pop_linetoken (str, &db->lobpath);

	      if (databases == NULL)
		{
		  databases = db;
		}
	      else
		{
		  last->next = db;
		}
	      last = db;
	      if (db->name == NULL || db->pathname == NULL || db->hosts == NULL || db->logpath == NULL
		  /* skip to check above to support backward compatibility || db->lobpath == NULL */ )

		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_INVALID_DATABASES, 1, DATABASES_FILENAME);
		  if (databases != NULL)
		    {
		      cfg_free_directory (databases);
		    }
		  *info_p = NULL;
		  free_and_init (line);
		  return ER_CFG_INVALID_DATABASES;
		}
	    }
	  str = cfg_next_line (str);
	  str = cfg_next_char (str);
	}
      error_code = NO_ERROR;
      free_and_init (line);
    }
  *info_p = databases;
  return (error_code);
}

/*
 * cfg_write_directory() - This writes a list of database descriptors to
 *                         the accessible config file. only the first host,
 *                         (primary host), is written to file.
 *    return: none
 *    databases(in): list of database descriptors
 *
 * Note: However, cfg_write_directory() has a potential problem that
 *       the file lock is released. The file lock acquired through io_mount()
 *       is released when cfg_write_directory() opens and closes the file.
 */
void
cfg_write_directory (const DB_INFO * databases)
{
  FILE *file_p;
  const DB_INFO *db_info_p;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

  file_p = cfg_open_directory_file (true);
  if (file_p != NULL)
    {

#if !defined(WINDOWS)
      sigfillset (&new_mask);
      sigdelset (&new_mask, SIGINT);
      sigdelset (&new_mask, SIGQUIT);
      sigdelset (&new_mask, SIGTERM);
      sigdelset (&new_mask, SIGHUP);
      sigdelset (&new_mask, SIGABRT);
      sigprocmask (SIG_SETMASK, &new_mask, &old_mask);
#endif /* !WINDOWS */

      fprintf (file_p, "#db-name\tvol-path\t\tdb-host\t\tlog-path\t\tlob-base-path\n");
      for (db_info_p = databases; db_info_p != NULL; db_info_p = db_info_p->next)
	{
	  bool t = (strlen (db_info_p->name) < 8);
#if defined(WINDOWS)
	  char short_path[256];
	  GetShortPathName (db_info_p->pathname, short_path, 256);
	  fprintf (file_p, "%s%s\t%s\t", db_info_p->name, (t ? "\t" : ""), short_path);
#else /* WINDOWS */
	  fprintf (file_p, "%s%s\t%s\t", db_info_p->name, (t ? "\t" : ""), db_info_p->pathname);
#endif /* WINDOWS */

	  if (db_info_p->hosts != NULL && *(db_info_p->hosts) != NULL)
	    {
	      char **array = db_info_p->hosts;
	      fprintf (file_p, "%s", *array++);
	      while (*array != NULL)
		{
		  fprintf (file_p, ":%s", *array++);
		}
	    }
	  else
	    {
	      fprintf (file_p, "localhost");
	    }

	  if (db_info_p->logpath != NULL)
	    {
#if defined(WINDOWS)
	      GetShortPathName (db_info_p->logpath, short_path, 256);
	      fprintf (file_p, "\t%s ", short_path);
#else /* WINDOWS */
	      fprintf (file_p, "\t%s ", db_info_p->logpath);
#endif /* WINDOWS */
	    }

	  if (db_info_p->lobpath != NULL)
	    {
	      fprintf (file_p, "\t%s ", db_info_p->lobpath);
	    }

	  fprintf (file_p, "\n");
	}
      fflush (file_p);
      fclose (file_p);

#if !defined(WINDOWS)
      sigprocmask (SIG_SETMASK, &old_mask, NULL);
#endif /* !WINDOWS */
    }
}

/*
 * cfg_write_directory_ex() - This writes a list of database descriptors
 *                            to the accessible config file.
 *                            only the first host, (primary host),
 *                            is written to file.
 *    return: none
 *    vdes(in): file descriptor
 *    databases(in): list of database descriptors
 *
 * Note : However, cfg_write_directory() has a potential problem that
 *        the file lock is released. The file lock acquired through io_mount()
 *        is released when cfg_write_directory() opens and closes the file.
 */
void
cfg_write_directory_ex (int vdes, const DB_INFO * databases)
{
  char line[LINE_MAX], *s;
  const DB_INFO *db_info_p;
  int n;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
  cfg_read_directory (info_p, true);
  return;
#endif /* DONT_USE_MANDATORY_LOCK_IN_WINDOWS */

#if !defined(WINDOWS)
  sigfillset (&new_mask);
  sigdelset (&new_mask, SIGINT);
  sigdelset (&new_mask, SIGQUIT);
  sigdelset (&new_mask, SIGTERM);
  sigdelset (&new_mask, SIGHUP);
  sigdelset (&new_mask, SIGABRT);
  sigprocmask (SIG_SETMASK, &new_mask, &old_mask);
#endif /* !WINDOWS */

  lseek (vdes, 0L, SEEK_SET);
  n = sprintf (line, "#db-name\tvol-path\t\tdb-host\t\tlog-path\t\tlob-base-path\n");
  write (vdes, line, n);
  for (db_info_p = databases; db_info_p != NULL; db_info_p = db_info_p->next)
    {
      bool t = (strlen (db_info_p->name) < 8);
      s = line;
      s += sprintf (s, "%s%s\t%s\t", db_info_p->name, (t ? "\t" : ""), db_info_p->pathname);

      if (db_info_p->hosts != NULL && *(db_info_p->hosts) != NULL)
	{
	  char **array = db_info_p->hosts;
	  s += sprintf (s, "%s", *array++);
	  while (*array != NULL)
	    {
	      s += sprintf (s, ":%s", *array++);
	    }
	}
      else
	{
	  s += sprintf (s, "localhost");
	}
      if (db_info_p->logpath)
	{
	  s += sprintf (s, "\t%s", db_info_p->logpath);
	}
      if (db_info_p->lobpath)
	{
	  s += sprintf (s, "\t%s", db_info_p->lobpath);
	}
      s += sprintf (s, "\n");
      n = (int) (s - line);
      write (vdes, line, n);
    }

  ftruncate (vdes, lseek (vdes, 0L, SEEK_CUR));

#if !defined(WINDOWS)
  sigprocmask (SIG_SETMASK, &old_mask, NULL);
#endif /* !WINDOWS */
}

/*
 * cfg_free_directory() - Frees a list of database descriptors.
 *    return: none
 *    databases(in): list of databases
 */
void
cfg_free_directory (DB_INFO * databases)
{
  DB_INFO *db_info_p, *next_info_p;

  for (db_info_p = databases, next_info_p = NULL; db_info_p != NULL; db_info_p = next_info_p)
    {

      next_info_p = db_info_p->next;

      if (db_info_p->name != NULL)
	{
	  free_and_init (db_info_p->name);
	}
      if (db_info_p->pathname != NULL)
	{
	  free_and_init (db_info_p->pathname);
	}
      if (db_info_p->hosts != NULL)
	{
	  cfg_free_hosts (db_info_p->hosts);
	}
      if (db_info_p->logpath != NULL)
	{
	  free_and_init (db_info_p->logpath);
	}
      if (db_info_p->lobpath != NULL)
	{
	  free_and_init (db_info_p->lobpath);
	}
      free_and_init (db_info_p);
    }
}

#if defined(CUBRID_DEBUG)
/*
 * cfg_dump_directory() - debug function.
 *    return: none
 *    databases(in): list of database descriptors
 */
void
cfg_dump_directory (const DB_INFO * databases)
{
  const DB_INFO *db_info_p;
  int i = 0;

  for (db_info_p = databases; db_info_p != NULL; db_info_p = db_info_p->next)
    {
      fprintf (stdout, "%s %s ", db_info_p->name, db_info_p->pathname);
      if (db_info_p->hosts != NULL)
	{
	  i = 0;
	  while ((*((db_info_p->hosts) + i)) != NULL)
	    {
	      fprintf (stdout, "%s", (*(db_info_p->hosts + i)));
	      i++;
	      if ((*((db_info_p->hosts) + i)) != NULL)
		{
		  fprintf (stdout, ",");
		}
	      else
		{
		  fprintf (stdout, " ");
		}
	    }
	}
      if (db_info_p->logpath != NULL)
	{
	  fprintf (stdout, "%s ", db_info_p->logpath);
	}
      if (db_info_p->lobpath != NULL)
	{
	  fprintf (stdout, "%s", db_info_p->lobpath);
	}
      fprintf (stdout, "\n");
    }
}
#endif

/*
 * cfg_update_db() - Updates pathname, logpath, and creates a new host list
 *                   with the hostname sent in used as the primary host.
 *    return: none
 *    db_info_p(in): database descriptor
 *    path(in): directory path name
 *    logpath(in): log path name
 *    host(in): server host name
 */
void
cfg_update_db (DB_INFO * db_info_p, const char *path, const char *logpath, const char *lobpath, const char *host)
{
  char **ptr_p;

  if (db_info_p != NULL)
    {
      if (path != NULL)
	{
	  if (db_info_p->pathname != NULL)
	    {
	      free_and_init (db_info_p->pathname);
	    }
	  db_info_p->pathname = strdup (path);
	}

      if (logpath != NULL)
	{
	  if (db_info_p->logpath != NULL)
	    {
	      free_and_init (db_info_p->logpath);
	    }
	  db_info_p->logpath = strdup (logpath);
	}

      if (lobpath != NULL)
	{
	  if (db_info_p->lobpath != NULL)
	    {
	      free_and_init (db_info_p->lobpath);
	    }
	  db_info_p->lobpath = strdup (lobpath);
	}

      if (host != NULL)
	{
	  ptr_p = cfg_get_hosts (host, &db_info_p->num_hosts, false);
	  if (db_info_p->hosts != NULL)
	    {
	      cfg_free_hosts (db_info_p->hosts);
	    }
	  db_info_p->hosts = ptr_p;
	}
    }
}

/*
 * cfg_new_db() - creates a new DB_INFO structure. If the hosts array sent
 *                in is NULL, an array with the local host as primary host
 *                is created.
 *    return: new database descriptor
 *    name(in): database name
 *    path(in):
 *    logpath(in): log path
 *    lobpath(in): lob path
 *    hosts(in):
 */
DB_INFO *
cfg_new_db (const char *name, const char *path, const char *logpath, const char *lobpath, const char **hosts)
{
  DB_INFO *db_info_p;

  db_info_p = (DB_INFO *) malloc (DB_SIZEOF (DB_INFO));
  if (db_info_p == NULL)
    {
      goto error;
    }

  db_info_p->pathname = NULL;
  db_info_p->logpath = NULL;
  db_info_p->lobpath = NULL;
  db_info_p->hosts = NULL;
  db_info_p->num_hosts = 0;

  db_info_p->name = strdup (name);
  if (db_info_p->name == NULL)
    goto error;

  if (path == NULL)
    {
      path = cfg_os_working_directory ();
    }

  /*
   * if NULL hosts is passed in, then create a new host list, with the
   * local host as the primary.
   */
  if (hosts == NULL)
    {
      db_info_p->hosts = cfg_get_hosts (NULL, &db_info_p->num_hosts, true);
    }
  else
    {
      db_info_p->hosts = cfg_copy_hosts (hosts, &db_info_p->num_hosts);
    }

  db_info_p->pathname = (path != NULL) ? strdup (path) : NULL;
  if (db_info_p->pathname == NULL)
    {
      goto error;
    }

  if (logpath == NULL)
    {
      db_info_p->logpath = strdup (db_info_p->pathname);
    }
  else
    {
      db_info_p->logpath = strdup (logpath);
    }

  if (db_info_p->logpath == NULL)
    {
      goto error;
    }

  if (lobpath != NULL)
    {
      db_info_p->lobpath = strdup (lobpath);
    }

  db_info_p->next = NULL;

  return (db_info_p);

error:
  if (db_info_p != NULL)
    {
      if (db_info_p->name != NULL)
	{
	  free_and_init (db_info_p->name);
	}
      if (db_info_p->pathname != NULL)
	{
	  free_and_init (db_info_p->pathname);
	}
      if (db_info_p->logpath != NULL)
	{
	  free_and_init (db_info_p->logpath);
	}
      if (db_info_p->lobpath != NULL)
	{
	  free_and_init (db_info_p->lobpath);
	}
      if (db_info_p->hosts != NULL)
	{
	  free_and_init (db_info_p->hosts);
	}

      free_and_init (db_info_p);
    }

  return NULL;
}

/*
 * cfg_find_db_list()
 *    return: database descriptor
 *    dir(in): descriptor list
 *    name(in): database name
 */
DB_INFO *
cfg_find_db_list (DB_INFO * db_info_list_p, const char *name)
{
  DB_INFO *db_info_p, *found_info_p;

  found_info_p = NULL;
  for (db_info_p = db_info_list_p; db_info_p != NULL && found_info_p == NULL; db_info_p = db_info_p->next)
    {
      if (strcmp (db_info_p->name, name) == 0)
	{
	  found_info_p = db_info_p;
	}
    }

  return (found_info_p);
}

/*
 * cfg_add_db() - Creates a new hosts array and DB_INFO structure and pops
 *                the structure into the dir linked-list.
 *    return: new database descriptor
 *    dir(in/out): pointer to directory list
 *    name(in): database name
 *    path(in): directory path
 *    logpath(in): log path
 *    lobpath(in): lob path
 */
DB_INFO *
cfg_add_db (DB_INFO ** dir, const char *name, const char *path, const char *logpath, const char *lobpath,
	    const char *host)
{
  DB_INFO *db_info_p;

  if (host != NULL)
    {
      const char *hosts[2];
      hosts[0] = host;
      hosts[1] = NULL;
      db_info_p = cfg_new_db (name, path, logpath, lobpath, hosts);
    }
  else
    {
      db_info_p = cfg_new_db (name, path, logpath, lobpath, NULL);
    }

  if (db_info_p != NULL)
    {
      db_info_p->next = *dir;
      *dir = db_info_p;
    }

  return (db_info_p);
}

/*
 * cfg_find_db()
 *    return: database descriptor
 *    db_name(in): database name
 */
DB_INFO *
cfg_find_db (const char *db_name)
{
  DB_INFO *dir_info_p, *db_info_p;

  db_info_p = NULL;

  if (cfg_read_directory (&dir_info_p, false) == NO_ERROR)
    {
      if (dir_info_p == NULL)
	{
#if !defined(CS_MODE)
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_FIND_DATABASE, 2, db_name, DATABASES_FILENAME);
#else /* !CS_MODE */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_CFG_FIND_DATABASE, 2, db_name, DATABASES_FILENAME);
#endif /* !CS_MODE */
	}
      else
	{
	  db_info_p = cfg_find_db_list (dir_info_p, db_name);
	  if (db_info_p == NULL)
	    {
#if !defined(CS_MODE)
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_FIND_DATABASE, 2, db_name, DATABASES_FILENAME);
#else /* !CS_MODE */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_CFG_FIND_DATABASE, 2, db_name, DATABASES_FILENAME);
#endif /* !CS_MODE */
	    }
	  else
	    {
	      if (db_info_p->hosts != NULL)
		{
		  db_info_p = cfg_new_db (db_info_p->name, db_info_p->pathname, db_info_p->logpath, db_info_p->lobpath,
					  (const char **) db_info_p->hosts);
		}
	      else
		{
		  db_info_p = cfg_new_db (db_info_p->name, db_info_p->pathname, db_info_p->logpath, db_info_p->lobpath,
					  NULL);
		}
	    }
	  cfg_free_directory (dir_info_p);
	}
    }
  else
    {
#if !defined(CS_MODE)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_READ_DATABASES, 1, DATABASES_FILENAME);
#else /* !CS_MODE */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_CFG_READ_DATABASES, 1, DATABASES_FILENAME);
#endif /* !CS_MODE */
    }
  return (db_info_p);
}

/*
 * cfg_delete_db() - Deletes a database entry from a directory list.
 *    return: if success is return true, otherwise return false
 *    dir_info_p(in): pointer to directory list
 *    name(in): database name
 */
bool
cfg_delete_db (DB_INFO ** dir_info_p, const char *name)
{
  DB_INFO *db_info_p, *prev_info_p, *found_info_p;
  bool success = false;

  for (db_info_p = *dir_info_p, found_info_p = NULL, prev_info_p = NULL; db_info_p != NULL && found_info_p == NULL;
       db_info_p = db_info_p->next)
    {
      if (strcmp (db_info_p->name, name) == 0)
	{
	  found_info_p = db_info_p;
	}
      else
	{
	  prev_info_p = db_info_p;
	}
    }
  if (found_info_p != NULL)
    {
      if (prev_info_p == NULL)
	{
	  *dir_info_p = found_info_p->next;
	}
      else
	{
	  prev_info_p->next = found_info_p->next;
	}
      found_info_p->next = NULL;
      cfg_free_directory (found_info_p);
      success = true;
    }
  return (success);
}

/*
 * cfg_get_hosts() - assigns value count, to the number of hosts in array.
 *                   cfg_free_hosts should be called to free up memory used
 *                   by the array
 *    return: pointer to an array containing the host list.
 *    dbname(in): database name to connect to. (this is for future use)
 *    prim_host(in): primary hostname for database.
 *    count(in): count will contain the number of host found after processing
 *    include_local_host(in): boolean indicating if the local host name should
 *                            be prepended to the list.
 */
char **
cfg_get_hosts (const char *prim_host, int *count, bool include_local_host)
{
  /* pointers to array of hosts, to return */
  char **host_array;
  char *hosts_data;
  int i;

  *count = 0;

  /*
   * get a clean host list, i.e., null fields and duplicate hosts removed.
   * prim_host will be prepended to the list, and the local host will
   * will be appended if include_local_host is true.
   */
  hosts_data = cfg_create_host_list (prim_host, include_local_host, count);
  if (*count == 0 || hosts_data == NULL)
    {
      return NULL;
    }

  /* create a list of pointers to point to the hosts in hosts_data */
  host_array = (char **) calloc (*count + 1, sizeof (char **));
  if (host_array == NULL)
    {
      free_and_init (hosts_data);
      return NULL;
    }
  for (i = 0; i < *count; i++)
    {
      host_array[i] = hosts_data;
      hosts_data = strchr (hosts_data, CFG_HOST_SEPARATOR);
      if (hosts_data == NULL)
	{
	  break;
	}

      *hosts_data++ = '\0';
    }

  return host_array;
}

/*
 * cfg_free_hosts() - free_and_init's host_array and *host_array if not NULL
 *    return: none
 *    host_array(in): array of pointers to buffer containing hostnames.
 */
void
cfg_free_hosts (char **host_array)
{
  if (host_array != NULL)
    {
      if (*host_array != NULL)
	{
	  free_and_init (*host_array);
	}
      free_and_init (host_array);
    }
}

/*
 * cfg_pop_host() - pointer to next character in string
 *    return: returns pointer to next character in string
 *    host_list(in): String containing list of hosts
 *    buffer(in): Buffer to pop in hostname.
 *    length(out): Returns the length of the hostname, popped.
 *             -1 indicates that the hostname was too long > MAXHOSTLEN,
 *             and buffer is empty
 *
 *    Note : Sending in a NULL buffer will mean the function will assign length
 *           to the length of the next host in the list only.
 */
static const char *
cfg_pop_host (const char *host_list, char *buffer, int *length)
{
  int current_host_length = 0;
  const char *start, *host;

  host = host_list;

  if (buffer != NULL)
    {
      *buffer = '\0';
    }

  /* Ignore initial spaces/field separators in list */

  while (((char_isspace (*host)) || (*host == CFG_HOST_SEPARATOR)) && (*host != '\0'))
    {
      ++host;
    }

  /* Read in next host, and make a note of its length */

  start = host;
  current_host_length = 0;

  while ((*host != CFG_HOST_SEPARATOR) && (!char_isspace (*host)) && (*host != '\0'))
    {
      host++;
      current_host_length++;
    }

  /*
   * Increment count if we have a valid hostname, and we have reached,
   * a field separator, a space or end of line.
   * Copy host into buffer supplied.
   */
  if (((*host == CFG_HOST_SEPARATOR) || (char_isspace (*host)) || (*host == '\0')) && (current_host_length != 0))
    {
      /* Note buffer is empty if length of host is greater than CUB_MAXHOSTNAMELEN) */
      if ((buffer != NULL) && (current_host_length <= CUB_MAXHOSTNAMELEN))
	{
	  strncpy (buffer, start, current_host_length);
	  *(buffer + current_host_length) = '\0';
	}
    }

  if (current_host_length >= CUB_MAXHOSTNAMELEN)
    {
      *length = (-1);
    }
  else
    {
      *length = current_host_length;
    }
  return (host);
}

/*
 * cfg_host_exists() - Traverses the host_list to locate hostname.
 *    return: true if item exists.
 *    host_list(in): Pointer to array holding host names
 *    hostname(in): host name to search for.
 *    num_items(in): The number of items currently in the list
 */
static bool
cfg_host_exists (char *host_list, char *hostname, int num_items)
{
  char *current_host;
  char *next_sep;
  int i = 0, len, hostname_len;

  hostname_len = (int) strlen (hostname);

  current_host = host_list;
  while ((current_host != NULL) && (i < num_items))
    {
      next_sep = strchr (current_host, CFG_HOST_SEPARATOR);
      if (next_sep == NULL)
	{
	  if (strcmp (current_host, hostname) == 0)
	    {
	      return true;
	    }
	  else
	    {
	      return false;
	    }
	}
      else
	{
	  len = CAST_STRLEN (next_sep - current_host);

	  if (len == hostname_len && strncmp (current_host, hostname, len) == 0)
	    {
	      return true;
	    }
	}

      i++;
      current_host = next_sep + 1;
    }
  return false;
}				/* cfg_host_exists() */

/*
 * cfg_copy_hosts() - a copy of the array holding hostnames.
 *    return: returns a pointer to a copy of the array holding hostnames.
 *    host_array(in): Pointer to array holding host names
 */
static char **
cfg_copy_hosts (const char **host_array, int *num_hosts)
{
  char **new_array;
  const char *host;
  char *buffer;
  int num;
  size_t buffer_size;

  assert (host_array != NULL);
  assert (num_hosts != NULL);

  *num_hosts = 0;
  buffer_size = 0;
  /* count the number of hosts array and calculate the size of buffer */
  for (num = 0, host = host_array[0]; host; num++, host = host_array[num])
    {
      buffer_size += strlen (host) + 1;
    }
  if (num == 0)
    {
      return NULL;
    }

  /* copy the hosts array into the buffer and make new pointer array */
  buffer = (char *) malloc (buffer_size);
  if (buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buffer_size);
      return NULL;
    }

  new_array = (char **) calloc (num + 1, sizeof (char **));
  if (new_array == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, ((num + 1) * sizeof (char **)));
      free_and_init (buffer);
      return NULL;
    }

  for (num = 0, host = host_array[0]; host; num++, host = host_array[num])
    {
      strcpy (buffer, host);
      new_array[num] = buffer;
      buffer += strlen (host) + 1;
    }

  if (host_array[0] == NULL)
    {
      free_and_init (buffer);
    }

  *num_hosts = num;

  return new_array;
}

/*
 * cfg_create_host_lsit()
 *    return: returns a pointer to a copy of the array holding hostnames
 *    primary_host_name(in): String containing primary host name.
 *    include_local_host(in): Flag indicating if the local hostname should be
 *                       included in the list.
 *    count(out): Pointer to integer which will be assigned the number of hosts
 *             in list.
 *
 *    Note : Null or empty hostnames are ignored, and duplicates are not
 *           included in the list.
 */
char *
cfg_create_host_list (const char *primary_host_name, bool include_local_host, int *count)
{
  int host_list_length, host_length, host_count;
  const char *str_ptr;
  char *full_host_list, *host_ptr;
  char local_host[CUB_MAXHOSTNAMELEN + 1];

  assert (count != NULL);

  host_list_length = 0;
  /* include local host to list if required */
  *local_host = '\0';
  if (include_local_host)
    {
#if 0				/* use Unix-domain socket for localhost */
      if (GETHOSTNAME (local_host, CUB_MAXHOSTNAMELEN) == 0)
	{
	  local_host[CUB_MAXHOSTNAMELEN] = '\0';
	  host_list_length += strlen (local_host) + 1;
	}
#else
      strcpy (local_host, "localhost");
      host_list_length += (int) strlen (local_host) + 1;
#endif
    }
  /* check the given primary hosts list */
  if (primary_host_name != NULL && *primary_host_name != '\0')
    {
      host_list_length += (int) strlen (primary_host_name) + 1;
    }

  /* get the hosts list from parameters */
  if (prm_get_string_value (PRM_ID_CFG_DB_HOSTS) != NULL && *prm_get_string_value (PRM_ID_CFG_DB_HOSTS) != '\0')
    {
      host_list_length += (int) strlen (prm_get_string_value (PRM_ID_CFG_DB_HOSTS)) + 1;
    }

  /*
   * concatenate host lists with separator
   * count the number of hosts in the list
   * ignore null and space
   * removing duplicates
   */
  if (host_list_length == 0)
    {
      return NULL;
    }
  full_host_list = (char *) malloc (host_list_length + 1);
  if (full_host_list == NULL)
    {
      return NULL;
    }
  host_count = 0;
  host_ptr = full_host_list;
  *host_ptr = '\0';
  /* add the given primary hosts to the list */
  if (primary_host_name != NULL && *primary_host_name != '\0')
    {
      str_ptr = primary_host_name;
      while (*str_ptr != '\0')
	{
	  str_ptr = cfg_pop_host (str_ptr, host_ptr, &host_length);
	  if (host_length > 0)
	    {
	      if (!cfg_host_exists (full_host_list, host_ptr, host_count))
		{
		  host_count++;
		  host_ptr += host_length;
		  *host_ptr++ = CFG_HOST_SEPARATOR;
		}
	      *host_ptr = '\0';
	    }
	}
    }
  /* append the hosts from the parameter to the list */
  if (prm_get_string_value (PRM_ID_CFG_DB_HOSTS) != NULL && *prm_get_string_value (PRM_ID_CFG_DB_HOSTS) != '\0')
    {
      str_ptr = prm_get_string_value (PRM_ID_CFG_DB_HOSTS);
      while (*str_ptr != '\0')
	{
	  str_ptr = cfg_pop_host (str_ptr, host_ptr, &host_length);
	  if (host_length > 0)
	    {
	      if (!cfg_host_exists (full_host_list, host_ptr, host_count))
		{
		  host_count++;
		  host_ptr += host_length;
		  *host_ptr++ = CFG_HOST_SEPARATOR;
		}
	      *host_ptr = '\0';
	    }
	}
    }
  /* append local host if exists */
  if (*local_host != '\0')
    {
      if (!cfg_host_exists (full_host_list, local_host, host_count))
	{
	  strcpy (host_ptr, local_host);
	  host_ptr += strlen (local_host);
	  host_count++;
	}
    }

  /* remove last separator */
  host_ptr--;
  if (*host_ptr == CFG_HOST_SEPARATOR)
    {
      *host_ptr = '\0';
    }

  /* return host list and counter */
  if (host_count != 0)
    {
      *count = host_count;
      return full_host_list;
    }

  /* no valid host name */
  free_and_init (full_host_list);
  return NULL;
}
