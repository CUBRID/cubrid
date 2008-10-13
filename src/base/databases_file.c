/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
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

#include "porting.h"

#include "chartype.h"
#include "error_manager.h"
#include "databases_file.h"
#include "memory_manager_2.h"
#include "environment_variable.h"
#include "system_parameter.h"

#if defined(WINDOWS)
#include "ustring.h"
#include "wintcp.h"
#endif /* WINDOWS */


static char *cfg_next_char (char *str_p);
static char *cfg_next_line (char *str_p);
static char *cfg_pop_token (char *str_p, char **token_p);
static char *cfg_pop_linetoken (char *str_p, char **token_p);
static void cfg_get_directory_filename (char *buffer, int *local);

static int cfg_ensure_directory_write (void);
static FILE *cfg_open_directory_file (bool write_flag);

static char *cfg_pop_host (char *host_list, char *buffer, int *length);
static bool cfg_host_exists (char *host_list, char *hostname, int num_items);

static char *cfg_create_host_buffer (const char *primary_host_name,
				     bool append_local_host, int *cnt);
static char **cfg_create_host_array (char *hosts_data, int num_hosts,
				     int *count);
static char *cfg_get_prm_dbhosts (void);

#if defined(WINDOWS)
static DB_INFO *make_fake_db (const char *name, const char *hostname);
#endif /* WINDOWS */

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

  token = (char *) malloc (length + 1);

  if (length > 0 && token != NULL)
    {
      strncpy (token, p, length);
      token[length] = '\0';
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

  token = (char *) malloc (length + 1);

  if (length > 0 && token != NULL)
    {
      strncpy (token, p, length);
      token[length] = '\0';
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
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CFG_NO_WRITE_ACCESS, 1, buffer);
#else /* !CS_MODE */
      er_set_with_oserror (ER_WARNING_SEVERITY, ARG_FILE_LINE,
			   ER_CFG_NO_WRITE_ACCESS, 1, buffer);
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
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CFG_NO_WRITE_ACCESS, 1, filename);
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
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CFG_NO_WRITE_ACCESS, 1, filename);
	}
      else
	{
	  /* Only standalone and server will update the database location file */
#if !defined(CS_MODE)
	  /* no readable file, try to create one */
	  file_p = fopen (filename, "r+");
	  if (file_p == NULL)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ER_CFG_NO_WRITE_ACCESS, 1, filename);
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
bool
cfg_read_directory (DB_INFO ** info_p, bool write_flag)
{
  char line[PATH_MAX];
  FILE *file_p = NULL;
  DB_INFO *databases, *last, *db;
  char *str = NULL;
  int success = false;
  char *primary_host = NULL;

  databases = last = NULL;

  if (!write_flag || cfg_ensure_directory_write ())
    {
      file_p = cfg_open_directory_file (false);
      if (file_p != NULL)
	{
	  while (fgets (line, PATH_MAX - 1, file_p) != NULL)
	    {
	      str = cfg_next_char (line);
	      if (*str != '\0' && *str != '#')
		{
		  db = (DB_INFO *) malloc (DB_SIZEOF (DB_INFO));
		  if (db == NULL)
		    {
		      if (databases != NULL)
			{
			  cfg_free_directory (databases);
			}
		      *info_p = NULL;
		      return false;
		    }

		  db->next = NULL;
		  str = cfg_pop_token (str, &db->name);
		  str = cfg_pop_token (str, &db->pathname);
		  str = cfg_pop_token (str, &primary_host);
		  db->hosts =
		    cfg_get_hosts (db->name, primary_host, &db->num_hosts,
				   true);

		  if (primary_host != NULL)
		    {
		      free_and_init (primary_host);
		    }

		  str = cfg_pop_token (str, &db->logpath);

		  if (databases == NULL)
		    {
		      databases = db;
		    }
		  else
		    {
		      last->next = db;
		    }
		  last = db;
		  if (db->name == NULL ||
		      db->pathname == NULL ||
		      db->hosts == NULL || db->logpath == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_CFG_INVALID_DATABASES, 1,
			      DATABASES_FILENAME);
		      if (databases != NULL)
			{
			  cfg_free_directory (databases);
			}
		      *info_p = NULL;
		      return false;
		    }
		}
	    }
	  fclose (file_p);
	  success = true;
	}
    }
  *info_p = databases;
  return (success);
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
bool
cfg_read_directory_ex (int vdes, DB_INFO ** info_p, bool write_flag)
{
  char *line = NULL;
  DB_INFO *databases, *last, *db;
  char *str = NULL;
  int success = false;
  char *primary_host = NULL;
  struct stat stat_buffer;

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
	  return false;
	}
      read (vdes, line, stat_buffer.st_size);
      line[stat_buffer.st_size] = '\0';
      str = cfg_next_char (line);
      while (*str != '\0')
	{
	  if (*str != '#')
	    {
	      if ((db = (DB_INFO *) malloc (DB_SIZEOF (DB_INFO))) == NULL)
		{
		  if (databases != NULL)
		    {
		      cfg_free_directory (databases);
		    }
		  *info_p = NULL;
		  free_and_init (line);
		  return false;
		}

	      db->next = NULL;
	      str = cfg_pop_linetoken (str, &db->name);
	      str = cfg_pop_linetoken (str, &db->pathname);
	      str = cfg_pop_linetoken (str, &primary_host);
	      db->hosts =
		cfg_get_hosts (db->name, primary_host, &db->num_hosts, true);

	      if (primary_host != NULL)
		{
		  free_and_init (primary_host);
		}

	      str = cfg_pop_linetoken (str, &db->logpath);

	      if (databases == NULL)
		{
		  databases = db;
		}
	      else
		{
		  last->next = db;
		}
	      last = db;
	      if (db->name == NULL
		  || db->pathname == NULL
		  || db->hosts == NULL || db->logpath == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_CFG_INVALID_DATABASES, 1, DATABASES_FILENAME);
		  if (databases != NULL)
		    {
		      cfg_free_directory (databases);
		    }
		  *info_p = NULL;
		  free_and_init (line);
		  return false;
		}
	    }
	  str = cfg_next_line (str);
	  str = cfg_next_char (str);
	}
      success = true;
      free_and_init (line);
    }
  *info_p = databases;
  return (success);
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
      /*
       * This function writes the file from scratch. Its performance is quite
       * bad and it is prone to problems when a signal is raisen
       *
       * For example, what would have happen if ^C is hit in the middle of the
       * following loop, database.txt will be written completely. database.txt
       * may end up with fewer entries than when the operation started.
       *
       * The main problem is that database.txt is not part of the recovery
       * process of the database.. This is a bad design.. This file is shared
       * by different databases. We may need to think this a little more
       * careful. A release note may be needed.
       *
       * For now, I will block signals..Note that this is not enough since
       * the process may be killed (e.g., with SIGKILL) or the machine may
       * crash.. However, this fix will improve the problem.
       * It may be good idea to make a backup of the file just in case
       * a crash happens.
       */

#if !defined(WINDOWS)
      sigfillset (&new_mask);
      sigdelset (&new_mask, SIGINT);
      sigdelset (&new_mask, SIGQUIT);
      sigdelset (&new_mask, SIGTERM);
      sigdelset (&new_mask, SIGHUP);
      sigdelset (&new_mask, SIGABRT);
      sigprocmask (SIG_SETMASK, &new_mask, &old_mask);
#endif /* !WINDOWS */

      for (db_info_p = databases; db_info_p != NULL;
	   db_info_p = db_info_p->next)
	{
#if defined(WINDOWS)
	  char short_path[256];
	  GetShortPathName (db_info_p->pathname, short_path, 256);
	  fprintf (file_p, "%s %s ", db_info_p->name, short_path);
#else /* WINDOWS */
	  fprintf (file_p, "%s %s ", db_info_p->name, db_info_p->pathname);
#endif /* WINDOWS */

	  if (db_info_p->hosts != NULL)
	    {
	      fprintf (file_p, "%s ", (*(db_info_p->hosts)));
	    }

	  if (db_info_p->logpath != NULL)
	    {
#if defined(WINDOWS)
	      GetShortPathName (db_info_p->logpath, short_path, 256);
	      fprintf (file_p, "%s ", short_path);
#else /* WINDOWS */
	      fprintf (file_p, "%s ", db_info_p->logpath);
#endif /* WINDOWS */
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
  char line[PATH_MAX];
  const DB_INFO *db_info_p;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
  return cfg_read_directory (info_p, true);
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
  for (db_info_p = databases; db_info_p != NULL; db_info_p = db_info_p->next)
    {
      sprintf (line, "%s %s %s %s\n", db_info_p->name, db_info_p->pathname,
	       db_info_p->hosts ? (*(db_info_p->hosts)) : "",
	       db_info_p->logpath ? db_info_p->logpath : "");
      write (vdes, line, strlen (line));
    }
#if defined(WINDOWS)
  _chsize (vdes, lseek (vdes, 0L, SEEK_CUR));
#else /* WINDOWS */
  ftruncate (vdes, lseek (vdes, 0L, SEEK_CUR));
#endif /* WINDOWS */

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

  for (db_info_p = databases, next_info_p = NULL; db_info_p != NULL;
       db_info_p = next_info_p)
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
      free_and_init (db_info_p);
    }
}

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
      fprintf (stdout, "\n");
    }
}

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
cfg_update_db (DB_INFO * db_info_p, const char *path, const char *logpath,
	       const char *host)
{
  char *str;
  char **ptr_p;
  if (db_info_p != NULL)
    {
      str = strdup (path);
      if (db_info_p->pathname != NULL)
	{
	  free_and_init (db_info_p->pathname);
	}
      db_info_p->pathname = str;

      if (logpath != NULL)
	{
	  str = strdup (logpath);
	}
      else
	{
	  str = strdup (path);
	}

      if (db_info_p->logpath != NULL)
	{
	  free_and_init (db_info_p->logpath);
	}
      db_info_p->logpath = str;

      ptr_p =
	cfg_get_hosts (db_info_p->name, host, &db_info_p->num_hosts, true);
      if (db_info_p->hosts != NULL)
	{
	  cfg_free_hosts (db_info_p->hosts);
	}
      db_info_p->hosts = ptr_p;
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
 *    hosts(in):
 */
DB_INFO *
cfg_new_db (const char *name, const char *path,
	    const char *logpath, char **hosts)
{
  DB_INFO *db_info_p;
  char localhost[MAXHOSTNAMELEN];

  db_info_p = (DB_INFO *) malloc (DB_SIZEOF (DB_INFO));
  if (db_info_p == NULL)
    goto error;

  db_info_p->pathname = NULL;
  db_info_p->logpath = NULL;
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
      if (GETHOSTNAME (localhost, DB_SIZEOF (localhost)) != 0)
	{
	  /* unknown error */
	  strcpy (localhost, "???");
	}
      db_info_p->hosts =
	cfg_get_hosts (db_info_p->name, localhost, &db_info_p->num_hosts,
		       true);
    }
  else
    {
      db_info_p->hosts = cfg_copy_hosts (hosts);
    }
  db_info_p->pathname = strdup (path);
  if (db_info_p->pathname == NULL)
    goto error;

  if (logpath == NULL)
    {
      db_info_p->logpath = strdup (db_info_p->pathname);
    }
  else
    {
      db_info_p->logpath = strdup (logpath);
    }

  if (db_info_p->logpath == NULL)
    goto error;

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
  for (db_info_p = db_info_list_p; db_info_p != NULL && found_info_p == NULL;
       db_info_p = db_info_p->next)
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
 *    logpath(in):
 *    host(in): server host name
 */
DB_INFO *
cfg_add_db (DB_INFO ** dir, const char *name, const char *path,
	    const char *logpath, const char *host)
{
  DB_INFO *db_info_p;
  int num_hosts = 0;
  char **hosts = NULL;

  hosts = cfg_get_hosts (name, host, &num_hosts, true);

  db_info_p = cfg_new_db (name, path, logpath, hosts);

  cfg_free_hosts (hosts);

  if (db_info_p != NULL)
    {
      db_info_p->next = *dir;
      db_info_p->num_hosts = num_hosts;
      *dir = db_info_p;
    }

  return (db_info_p);
}

/*
 * cfg_find_db()
 *    return: database descriptor
 *    name(in): database name
 */
DB_INFO *
cfg_find_db (const char *name)
{
  DB_INFO *dir_info_p, *db_info_p;

  db_info_p = NULL;

  if (cfg_read_directory (&dir_info_p, false))
    {
      if (dir_info_p == NULL)
	{
#if !defined(CS_MODE)
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_FIND_DATABASE, 2,
		  name, DATABASES_FILENAME);
#else /* !CS_MODE */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_CFG_FIND_DATABASE, 2,
		  name, DATABASES_FILENAME);
#endif /* !CS_MODE */
	}
      else
	{
	  db_info_p = cfg_find_db_list (dir_info_p, name);
	  if (db_info_p == NULL)
	    {
#if !defined(CS_MODE)
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_FIND_DATABASE,
		      2, name, DATABASES_FILENAME);
#else /* !CS_MODE */
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_CFG_FIND_DATABASE, 2, name, DATABASES_FILENAME);
#endif /* !CS_MODE */
	    }
	  else
	    {
	      if (db_info_p->hosts != NULL)
		{
		  db_info_p =
		    cfg_new_db (db_info_p->name, db_info_p->pathname,
				db_info_p->logpath, db_info_p->hosts);
		}
	      else
		{
		  db_info_p =
		    cfg_new_db (db_info_p->name, db_info_p->pathname,
				db_info_p->logpath, NULL);
		}
	    }
	  cfg_free_directory (dir_info_p);
	}
    }
  else
    {
#if !defined(CS_MODE)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_READ_DATABASES, 1,
	      DATABASES_FILENAME);
#else /* !CS_MODE */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_CFG_READ_DATABASES, 1,
	      DATABASES_FILENAME);
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
  int success = false;

  for (db_info_p = *dir_info_p, found_info_p = NULL, prev_info_p = NULL;
       db_info_p != NULL && found_info_p == NULL; db_info_p = db_info_p->next)
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
 *    append_local_host(in): boolean indicating if the local host name should
 *                           be appended to the list.
 */
char **
cfg_get_hosts (const char *dbname, const char *prim_host,
	       int *count, bool append_local_host)
{
  /* pointers to array of hosts, to return */
  char **host_array = NULL;

  /* Buffer to hold host names */
  char *hosts_data = NULL;

  *count = 0;

  /*
   * get a clean host list, i.e., null fields and duplicate hosts removed.
   * prim_host will be prepended to the list, and the local host will
   * will be appended if append_local_host is true.
   */

  hosts_data = cfg_create_host_buffer (prim_host, append_local_host, count);

  /* Check if we have a list of hosts */

  if (*count == 0)
    {
      if (hosts_data != NULL)
	{
	  free_and_init (hosts_data);
	}
      return (NULL);
    }

  /* Create a list of pointers to point to the hosts in hosts_data */
  host_array = cfg_create_host_array (hosts_data, *count, count);

  if (*count == 0)
    {
      if (hosts_data != NULL)
	{
	  free_and_init (hosts_data);
	}
      if (host_array != NULL)
	{
	  free_and_init (host_array);
	}
      hosts_data = NULL;
      host_array = NULL;
    }
  /* send back host count, and new array */
  return (host_array);
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
      /* Free array databuffer */
      if (*host_array != NULL)
	{
	  free_and_init (*host_array);
	}
      /* Free the array pointers to buffer */
      free_and_init (host_array);
    }
}				/* cfg_free_hosts() */

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
static char *
cfg_pop_host (char *host_list, char *buffer, int *length)
{
  int current_host_length = 0;
  char *start = NULL;
  char *host;

  host = host_list;

  if (buffer != NULL)
    {
      *buffer = '\0';
    }

  /* Ignore initial spaces/field separators in list */

  while (((char_isspace (*host)) ||
	  (*host == CFG_HOST_SEPARATOR)) && (*host != '\0'))
    {
      ++host;
    }

  /* Read in next host, and make a note of it's length */

  start = host;
  current_host_length = 0;

  while ((*host != CFG_HOST_SEPARATOR) &&
	 (!char_isspace (*host)) && (*host != '\0'))
    {
      host++;
      current_host_length++;
    }

  /*
   * Increment count if we have a valid hostname, and we have reached,
   * a field separator, a space or end of line.
   * Copy host into buffer supplied.
   */
  if (((*host == CFG_HOST_SEPARATOR) || (char_isspace (*host))
       || (*host == '\0')) && (current_host_length != 0))
    {
      /* Note buffer is empty if length of host is greater than MAXHOSTNAMELEN) */
      if ((buffer != NULL) && (current_host_length <= MAXHOSTNAMELEN))
	{
	  strncpy (buffer, start, current_host_length);
	  *(buffer + current_host_length) = '\0';
	}
    }

  if (current_host_length >= MAXHOSTNAMELEN)
    {
      *length = (-1);
    }
  else
    {
      *length = current_host_length;
    }
  return (host);
}				/* cfg_pop_host() */

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
  int i = 0;

  current_host = host_list;
  while ((current_host != NULL) && (i < num_items))
    {
      next_sep = strchr (current_host, CFG_HOST_SEPARATOR);
      if (next_sep == NULL)
	{
	  if (strcmp (current_host, hostname) == 0)
	    return (true);
	}
      else
	{
	  if (strncmp (current_host, hostname, next_sep - current_host) == 0)
	    return (true);
	}
      i++;
      current_host = next_sep + 1;
    }
  return (false);
}				/* cfg_host_exists() */

/*
 * cfg_copy_hosts() - a copy of the array holding hostnames.
 *    return: returns a pointer to a copy of the array holding hostnames.
 *    host_array(in): Pointer to array holding host names
 */
char **
cfg_copy_hosts (char **host_array)
{
  char **new_array;
  char *new_buffer;
  const char *current_host;

  int buffer_size;
  int num_hosts = 0;
  int i;

  current_host = (*(host_array));
  num_hosts = 0;

  buffer_size = 0;
  /* find last element in array */
  while ((*(host_array + num_hosts)) != NULL)
    {
      current_host = (*(host_array + num_hosts));
      num_hosts++;
    }
  /* size of data is address of last byte - address of first byte */
  buffer_size = (current_host + strlen (current_host) + 1) - (*host_array);
  new_array = (char **) calloc (num_hosts + 1, DB_SIZEOF (char **));

  if (new_array == NULL)
    return (NULL);

  new_buffer = (char *) malloc (DB_SIZEOF (char) * buffer_size);
  if (new_buffer == NULL)
    {
      free_and_init (new_array);
      return (NULL);
    }

  if (memcpy (new_buffer, *host_array, buffer_size) != new_buffer)
    {
      free_and_init (new_buffer);
      free_and_init (new_array);
      return (NULL);
    }

  /* Assign the pointers, with same offsets */
  for (i = 0; i < num_hosts; i++)
    {
      *(new_array + i) = (new_buffer + ((*(host_array + i)) - (*host_array)));
    }
  return (new_array);
}				/* cfg_cpy_hosts() */

/*
 * cfg_create_host_buf()
 *    return: returns a pointer to a copy of the array holding hostnames
 *    primary_host_name(in): String containing primary host name.
 *    append_local_host(in): Flag indicating if the local hostname should be
 *                       included in the list.
 *    count(out): Pointer to integer which will be assigned the number of hosts
 *             in list.
 *
 *    Note : Null or empty hostnames are ignored, and duplicates are not
 *           included in the list.
 */
static char *
cfg_create_host_buffer (const char *primary_host_name, bool append_local_host,
			int *count)
{
  int hosts_str_length, current_host_length, local_host_length;
  int primary_host_length, new_buffer_length;

  char *prm_host_list = NULL;
  char *full_host_list = NULL;
  char *full_host_ptr = NULL;
  char *prm_host_ptr = NULL;

  char local_host[MAXHOSTNAMELEN];

  /* get the hosts list from parameters */

  prm_host_list = cfg_get_prm_dbhosts ();

  if ((prm_host_list == NULL) || (*prm_host_list == '\0'))
    {
      hosts_str_length = 0;
    }
  else
    {
      hosts_str_length = strlen (prm_host_list);
    }

  /* Append local host to list if required */
  if ((!append_local_host) || (GETHOSTNAME (local_host, MAXHOSTNAMELEN) != 0))
    {
      /* unknown error */
      strcpy (local_host, "???");
      local_host_length = 3;
    }
  else
    {
      local_host_length = (strlen (local_host) + 1);
    }

  /* Check we have a valid primary host name */
  if ((primary_host_name != NULL) && (*primary_host_name != '\0'))
    {
      primary_host_length = (strlen (primary_host_name) + 1);
    }
  else
    {
      primary_host_length = 0;
    }

  /* Create a buffer to hold the concatenated host list */
  new_buffer_length =
    hosts_str_length + primary_host_length + local_host_length;
  if (new_buffer_length > 0)
    {
      /* make space for data + separator + end of line terminator */
      full_host_list = (char *) malloc (new_buffer_length + 2);
      if (full_host_list == NULL)
	{
	  *count = 0;
	  return (NULL);
	}
      *full_host_list = '\0';
    }
  else
    {
      *count = 0;
      return (NULL);
    }

  *count = 0;

  /* Prepend primary host to list */
  if (primary_host_length != 0)
    {
      sprintf (full_host_list, "%s%c", primary_host_name, CFG_HOST_SEPARATOR);
      (*count)++;
    }

  /* set up host pointer, into parameter supplied db_hosts */
  prm_host_ptr = prm_host_list;

  /* set up pointer to the new of the new list */
  full_host_ptr = full_host_list + strlen (full_host_list);

  if (prm_host_ptr != NULL)
    {
      /* Create new list ignoring null fields and spaces, and removing duplicates */
      while ((*prm_host_ptr != '\0'))
	{
	  current_host_length = 0;
	  prm_host_ptr =
	    cfg_pop_host (prm_host_ptr, full_host_ptr, &current_host_length);
	  if ((current_host_length != 0) && (current_host_length != -1))
	    {
	      if (!cfg_host_exists (full_host_list, full_host_ptr, *count))
		{
		  *count = (*count) + 1;
		  /* Add separator and terminate string */
		  full_host_ptr += current_host_length;
		  *full_host_ptr = CFG_HOST_SEPARATOR;
		  *(full_host_ptr + 1) = '\0';
		  full_host_ptr++;
		}
	      else
		{
		  *full_host_ptr = '\0';
		}
	    }
	}
    }
  /* append local host */
  if (local_host_length > 0)
    {
      if (!cfg_host_exists (full_host_list, local_host, *count))
	{
	  strcat (full_host_list, local_host);
	  (*count)++;
	}
    }
  /* Remove last host separator */
  if (*(full_host_list + strlen (full_host_list) - 1) == CFG_HOST_SEPARATOR)
    *(full_host_ptr - 1) = '\0';

  /*
   * If we duplicates existed in the list, there will be some unused space
   * which we should give back.
   */
  if (full_host_list != NULL)
    {
      int length_parsed_list;
      char *parsed_list;

      /* if full_host_list has changed we have some unused space at the end */
      length_parsed_list = strlen (full_host_list);
      if (length_parsed_list != new_buffer_length)
	{
	  parsed_list =
	    (char *) malloc (DB_SIZEOF (char) * length_parsed_list + 1);
	  if (parsed_list == NULL)
	    {
	      free_and_init (full_host_list);
	      *count = 0;
	      return (NULL);
	    }
	  else
	    {
	      strcpy (parsed_list, full_host_list);
	      free_and_init (full_host_list);
	      return (parsed_list);
	    }
	}
      else
	{
	  return (full_host_list);
	}
    }
  else
    {
      return (NULL);
    }
}				/* cfg_create_host_buf() */

/*
 * cfg_create_host_array()
 *    return: returns a pointer to a copy of the array holding hostnames
 *    hosts_data(in): Buffer holding hostnames separated by CFG_HOST_SEPARATOR
 *    num_hosts(in): Number of hosts in buffer.
 *    count(out): Pointer to integer which will be assigned the number of hosts
 *              in list.
 *
 *    Note : Null or empty hostnames are ignored, and duplicates
 *           are not included in the list.
 */
static char **
cfg_create_host_array (char *hosts_data, int num_hosts, int *count)
{
  char **host_array = NULL;
  char *hosts_data_p = NULL;
  char *current_host_str = NULL;

  *count = 0;

  /*
   * Make an array of pointers with an
   * extra slot for the end of array null terminator
   * Use calloc to initialize array to NULLS.
   */
  host_array = (char **) calloc (num_hosts + 1, DB_SIZEOF (char **));
  if (host_array == NULL)
    {
      if (hosts_data != NULL)
	{
	  free_and_init (hosts_data);
	}
      return (NULL);
    }

  /* Assign pointers into hosts_data */

  hosts_data_p = hosts_data;

  while (hosts_data_p != NULL)
    {
      current_host_str = hosts_data_p;
      hosts_data_p = strchr (hosts_data_p, CFG_HOST_SEPARATOR);
      if (hosts_data_p != NULL)
	{
	  *hosts_data_p = '\0';
	  hosts_data_p++;
	}
      if (*current_host_str != '\0')
	{
	  *(host_array + (*count)) = current_host_str;
	  (*count)++;
	}
    }

  /* terminate the list for safety */
  *(host_array + (*count)) = NULL;
  return (host_array);
}				/* cfg_create_host_array() */

/*
 * cfg_get_prm_dbhosts() - Returns pointer to global parameter,
 *                         PRM_CFG_DB_HOSTS
 *    return: return PRM_CFG_DB_HOSTS *
 *
 */
static char *
cfg_get_prm_dbhosts (void)
{
  return ((char *) PRM_CFG_DB_HOSTS);
}				/* prm_get_dbhosts() */

#if defined(WINDOWS)
/*
 * make_fake_db() -  Build a DB_INFO structure for an ODBC database.
 *                   We make dummy entries for the pathname & logname which
 *                   we don't actually need on the client anyway.
 *                   Careful with the pathname though, we use it to format
 *                   the bo_Vlabel.
 *    return: DB_INFO *
 */
static DB_INFO *
make_fake_db (const char *name, const char *hostname)
{
  DB_INFO *db;
  db = (DB_INFO *) malloc (sizeof (DB_INFO));
  if (db == NULL)
    {
      goto memory_error;
    }
  db->next = NULL;
  db->hosts = NULL;

  db->name = (char *) malloc (strlen (name) + 1);
  if (db->name == NULL)
    {
      goto memory_error;
    }
  strcpy ((char *) (db->name), (char *) name);
  db->pathname = (char *) malloc (strlen (FAKE_PATHNAME) + 1);
  if (db->pathname == NULL)
    {
      goto memory_error;
    }
  strcpy ((char *) (db->pathname), (char *) FAKE_PATHNAME);
  db->logpath = (char *) malloc (strlen (FAKE_PATHNAME) + 1);
  if (db->logpath == NULL)
    {
      goto memory_error;
    }
  strcpy ((char *) (db->logpath), (char *) FAKE_PATHNAME);
  if (hostname != NULL)
    {
      /* Create hosts array, do not include local host name in list
       * This list is made up of hostname, hosts in parameter db_hosts
       */
      db->hosts = cfg_get_hosts (db->name, hostname, &db->num_hosts, false);
      if ((db->hosts == NULL) || (db->num_hosts == 0))
	{
	  goto memory_error;
	}
    }
  return db;

memory_error:
  cfg_free_directory (db);
  return NULL;
}
#endif /* WINDOWS */
