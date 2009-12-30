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
 * elo_recovery.c - This is the file that supports the interface to the
 *                  transaction mechanism
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#if defined(WINDOWS)
#include <io.h>
#else
#include <unistd.h>
#endif /* !WINDOWS */

#include "glo_class.h"
#include "elo_holder.h"
#include "db.h"
#include "network_interface_cl.h"
#include "recovery_cl.h"
#include "elo_class.h"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "message_catalog.h"
#include "elo_recovery.h"

#include "porting.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define COPY_BUFFER_SIZE 2048

typedef struct holder_ref REF;
struct holder_ref
{
  char *pathname;
  DB_OBJECT *holder;
  struct holder_ref *savepoint;
  struct holder_ref *next;
  int savepoint_detected;
};

/*different types of savepoints */

enum
{ NOT_DETECTED, USER_SPECIFIED_DETECTED, SYSTEM_SPECIFIED_DETECTED };

static REF *Shadow_file_anchor = NULL;

static REF *esm_make_ref (const char *savepoint_pathname);
static int esm_queue_shadow_file (const char *pathname, DB_OBJECT * holder);
static void esm_free_shadow_file_entry (REF * entry);
static char *esm_get_shadow_file_pathname (const DB_OBJECT * holder);
static int esm_drop_name_and_holder (DB_OBJECT * holder);
static void esm_free_entry_by_name (const char *name);
static char *esm_make_shadow_pathname (const char *pathname);
static int esm_log_shadow_file (const char *shadow_file,
				const char *pathname);
static int esm_copy_file (const char *source, const char *destination);
static char *esm_add_savepoint (REF * savepoint_temp_p);
static char *make_shadow_pathname (const DB_OBJECT * holder_p);

/*
 * esm_expand_pathname()
 *      return: none
 *  source(in) : source path
 *  destination(in) : destination buffer
 *  max_length(in) : length of destination buffer
 *
 * Note :
 *    Hack to allow environment variables to modify the names of the FBO
 *    files.  This is particularly usefull for the Windows client so we can
 *    add in volume prefixes for NFS mounted directory trees.
 *
 *    Two forms of environment variables are allowed, if CUBRID_FBO_PREFIX
 *    is defined, it is automatically prefixed to all FBO path names.
 *
 *    If the FBO path name begins with a '$' character, it is assumed to
 *    be a reference to an environment variable and is passed through
 *    the util_expand_function for expansion.
 *
 *    These two methods of environment variable expansion could work
 *    together but they probably would be mutually exclusive.
 */

void
esm_expand_pathname (const char *source, char *destination, int max_length)
{
  const char *env;
  int length;

  /* Let environment variables in the source pathname override
   * CUBRID_FBO_PREFIX.  May want to conditionlize this.
   */
  strcpy (destination, "");
  if (source[0] != '$')
    {
      /* need to be more careful about maxlen overflow */
      env = (char *) envvar_get ("FBO_PREFIX");
      if (env != NULL)
	{
	  strcpy (destination, env);
	  /* Kludge, make sure there is a separator here, should be doing
	   * this conditionally based on the results of the expansion.
	   */
	  length = strlen (destination);
	  if (length && destination[length - 1] != '/'
	      && destination[length - 1] != '\\' && source[0] != '/'
	      && source[0] != '\\')
#if defined(WINDOWS)
	    strcat (destination, "\\");
#else /* WINDOWS */
	    strcat (destination, "/");
#endif /* WINDOWS */
	}
    }

  if (source[0] != '$')
    {
      strcat (destination, source);
    }
  else
    {
      length = strlen (destination);
      if (envvar_expand (source, &destination[length], max_length - length)
	  != NO_ERROR)
	{
	  /* problems expanding the name, leave the unadorned name in the
	   * buffer, open() should choke on it and an error will be signaled.
	   */
	  strcpy (destination, source);
	}
    }
}

/*
 * esm_make_ref() -
 *      return:
 *  savepoint_pathname(in) :
 */

static REF *
esm_make_ref (const char *savepoint_pathname)
{
  REF *entry = NULL;
  entry = (REF *) malloc (sizeof (REF));
  if (entry != NULL)
    {
      entry->holder = NULL;
      entry->pathname = strdup ((char *) savepoint_pathname);
      entry->savepoint = NULL;
      entry->next = NULL;
      entry->savepoint_detected = NOT_DETECTED;
    }
  return (entry);
}

/*
 * esm_queue_shadow_file() -
 *      return:
 *  pathname(in) :
 *  holder(in) : The holder object that contains the glo primitive
 */

static int
esm_queue_shadow_file (const char *pathname, DB_OBJECT * holder_p)
{
  REF *entry;
  entry = esm_make_ref (pathname);
  if (entry != NULL)
    {
      entry->holder = holder_p;
      entry->next = Shadow_file_anchor;
      Shadow_file_anchor = entry;
      return (true);
    }
  return (false);
}

/*
 * esm_free_shadow_file_entry() -
 *      return: none
 *  entry_p(in) :
 */

static void
esm_free_shadow_file_entry (REF * entry_p)
{
  if (entry_p->savepoint)
    {
      esm_free_shadow_file_entry (entry_p->savepoint);
    }
  free_and_init (entry_p->pathname);
  free_and_init (entry_p);
}

/*
 * esm_get_shadow_file_pathname() -
 *      return:
 *  holder_p(in) : The holder object that contains the glo primitive
 */

static char *
esm_get_shadow_file_pathname (const DB_OBJECT * holder_p)
{
  REF *entry_p;

  for (entry_p = Shadow_file_anchor; entry_p; entry_p = entry_p->next)
    {
      if (entry_p->holder != holder_p)
	{
	  continue;
	}
      if (entry_p->savepoint_detected == NOT_DETECTED)
	{
	  return (entry_p->pathname);
	}
    }
  return (NULL);
}

/*
 * esm_drop_name_and_holder() -
 *      return:
 *  holder_p(in) : The holder object that contains the glo primitive
 */

static int
esm_drop_name_and_holder (DB_OBJECT * holder_p)
{
  DB_VALUE value;
  DB_OBJECT *name_object_p;
  int error = NO_ERROR;

  if (holder_p == NULL)
    {
      return error;
    }

  error = db_get (holder_p, GLO_HOLDER_NAME_PTR, &value);

  if (error != NO_ERROR)
    {
      return error;
    }

  if (DB_VALUE_TYPE (&value) != DB_TYPE_OBJECT)
    {
      return error;
    }
  name_object_p = DB_GET_OBJECT (&value);

  if (name_object_p != NULL)
    {
      error = db_drop (name_object_p);
    }
  /*
   * Delete this even if no name object exists, but not if we
   * got an unexpected error.
   */
  if (error == NO_ERROR || error == ER_HEAP_UNKNOWN_OBJECT)
    {
      error = db_drop (holder_p);
    }
  return (error);
}

/*
 * esm_free_entry_by_name() -
 *      return: none
 *  name(in) :
 * Note :
 *      Frees an entry from the list of currently modified fbos. Will also free
 *      all savepoints associated with the entry.
 *      Should only be called during transaction undo/redo.
 *
 */

static void
esm_free_entry_by_name (const char *name)
{
  REF *entry_p, *previous_p;

  for (entry_p = previous_p = Shadow_file_anchor; entry_p;
       previous_p = entry_p, entry_p = entry_p->next)
    {
      if (strcmp (entry_p->pathname, name) != 0)
	{
	  continue;
	}
      if (entry_p == Shadow_file_anchor)
	{
	  Shadow_file_anchor = entry_p->next;
	}
      else
	{
	  previous_p->next = entry_p->next;
	}
      esm_free_shadow_file_entry (entry_p);
      return;
    }
}

/*
 * esm_redo() - will transfer the shadow_file to the fbo
 *              to commit the transaction
 *      return: true or false if the operation suceeded or failed
 *  buffer_size(in) : the size of the data buffer
 *  buffer(in) : a buffer containing the fbo pathname and the shadow_file
 *                   pathname (in that order).
 * NOTE:
 *      this operation may be called multiple times during the recovery
 *      process. It will also be called during the normal commit process.
 *
 */

int
esm_redo (const int buffer_size, char *buffer)
{
  char *shadow_file, *source_file;
  char temp_path[COPY_BUFFER_SIZE];
  int length;
  int error = NO_ERROR;

  shadow_file = buffer;
  esm_free_entry_by_name (shadow_file);	/* ambiguity when "" is used? */
  length = strlen (buffer);

  if (length >= buffer_size)
    {
      return false;
    }
  source_file = buffer + length + 1;

  if (*shadow_file == '\0')
    {
      /* if shadow file name = "" then delete the source file */
      if (unlink (source_file) == 0)
	{
	  return (true);
	}
    }
  else
    {
      temp_path[0] = '\0';
      esm_expand_pathname (source_file, temp_path, COPY_BUFFER_SIZE);

      if (temp_path[0] == '\0')
	{
	  error = os_rename_file (shadow_file, source_file);
	}
      else
	{
	  error = os_rename_file (shadow_file, temp_path);
	}

      if (error == NO_ERROR)
	{
	  return true;
	}
    }
  return (false);
}

/*
 * esm_undo() - Deletes the shadow file, because the transaction
 *              has been aborted
 *      return: true or false if the operation suceeded or failed
 *  buffer_size(in) : the size of the data buffer
 *  buffer(in) : a buffer containing the shadow file pathname and the fbo
 *                   pathname (in that order).
 *
 * Note :
 *   If the shadow file name is "", then we ignore the entry, it was for a
 *   destroy operation.
 *
 */

int
esm_undo (const int buffer_size, char *buffer)
{
  char *shadow_file;

  shadow_file = buffer;
  esm_free_entry_by_name (shadow_file);
  if (*shadow_file != '\0')
    {
      unlink (shadow_file);
    }
  return (true);
}

/*
 * esm_dump() - Debugging function to print undo/redo buffer
 *      return: none
 *  buffer_size(in) : the size of the data buffer
 *  buffer(in) : a buffer containing the shadow file pathname and the fbo
 *               pathname (in that order).
 *
 */

void
esm_dump (FILE * fp, const int buffer_size, void *data)
{
  char *shadow_file, *source_file;
  char *buffer;
  int length;

  buffer = (char *) data;
  shadow_file = buffer;
  length = strlen (buffer);
  if (length < buffer_size)
    {
      source_file = buffer + length + 1;
      fprintf (fp, "esm_dump: shadow_file = %s,\n original file = %s\n",
	       shadow_file, source_file);
    }
  else
    {
      fprintf (fp,
	       "esm_dump error, shadow file length longer than buffer size\n");
    }
}

/*
 * esm_shadow_file_exists() - Returns true if the shadow file has been created,
 *                            false otherwise
 *      return:  true/false
 *  holder_p(in) : The holder object that contains the glo_primitive
 *
 */

int
esm_shadow_file_exists (const DB_OBJECT * holder_p)
{
  REF *entry_p;

  for (entry_p = Shadow_file_anchor; entry_p; entry_p = entry_p->next)
    {
      if (entry_p->holder != holder_p)
	{
	  continue;
	}

      if (entry_p->savepoint_detected == NOT_DETECTED)
	{
	  return (true);
	}
    }
  return (false);
}

/*
 * esm_make_shadow_pathname() -
 *      return:
 *  pathname(in) :
 *
 *
 */

static char *
esm_make_shadow_pathname (const char *pathname)
{
  static char shadow_pathname[PATH_MAX];
  char temp[PATH_MAX];
  int len;

  len = strlen (pathname);
  if (len + 6 >= PATH_MAX)
    {
      /* can't use full path, create it in the current working direcory */
      strcpy (temp, "t");
    }
  else
    {
      /* rather than do a straight copy, allow environment variable
       * expansion to select the directory.
       *   strcpy(temp, pathname);
       */
      esm_expand_pathname (pathname, temp, PATH_MAX);

#if defined(WINDOWS)
      /* We can't simply append the template to the end of the file,
         we must replace the entire leaf file name due to DOS restrictions.
         Try to keep the same path however so we don't force temp files
         into the current working directory.
       */
      len = strlen (temp);
      if (len)
	{
	  char *ptr;
	  for (ptr = &temp[len - 1];
	       ptr != temp && *ptr != '/' && *ptr != '\\' && *ptr != ':';
	       ptr--);
	  if (ptr == temp)
	    {
	      /* couldn't find a delimiter, waste te entire name */
	      strcpy (temp, "t");
	    }
	  else
	    {
	      /* found a delimeter, waste the following file name */
	      strcpy (ptr + 1, "t");
	    }
	}
#endif /* WINDOWS */
    }

  /* add template and make the temp file name */
  snprintf(shadow_pathname, PATH_MAX - 1, "%sXXXXXX", temp);
  mktemp (shadow_pathname);

  return (shadow_pathname);
}

/*
 * esm_log_shadow_file() -
 *      return:
 *  shadow_file(in) :
 *  pathname(in) :
 *
 */

static int
esm_log_shadow_file (const char *shadow_file, const char *pathname)
{
  char *buffer;
  int size;

  if (pathname == NULL)
    {
      return (false);
    }

  size = strlen (shadow_file) + 1;
  buffer = (char *) malloc (size + strlen (pathname) + 1);
  if (buffer != NULL)
    {
      strcpy (buffer, shadow_file);
      strcpy (buffer + size, pathname);
      size += strlen (pathname) + 1;
      log_append_client_undo (RVMM_INTERFACE, size, buffer);
      log_append_client_postpone (RVMM_INTERFACE, size, buffer);
      free_and_init (buffer);
      return (true);
    }

  return (false);
}

/* Note, environment variable expansion is expected to have been
 * done by now.
 */

/*
 * esm_copy_file() -
 *      return:
 *  source(in) : source path
 *  destination(in) : destination path
 */

static int
esm_copy_file (const char *source, const char *destination)
{
  int source_fd, destination_fd;
  char buffer[COPY_BUFFER_SIZE];
  int length;
  source_fd = open (source, O_RDONLY, 0);
  if (source_fd <= 0)
    {
      return true;
    }

  destination_fd = open (destination,
			 O_TRUNC | O_RDWR | O_CREAT,
			 S_IRWXU | S_IRWXG | S_IRWXO);
  if (destination_fd > 0)
    {
      while ((length = read (source_fd, buffer, COPY_BUFFER_SIZE)) > 0
	     && length <= COPY_BUFFER_SIZE)
	{
	  write (destination_fd, buffer, length);
	}
      close (destination_fd);
      close (source_fd);
      return (true);
    }
  close (source_fd);
  return (true);
}

/*
 * esm_add_savepoint() -
 *      return:
 *  savepoint_temp_p(in) :
 *
 */

static char *
esm_add_savepoint (REF * savepoint_temp_p)
{
  char *savepoint_pathname = NULL;

  savepoint_pathname = esm_make_shadow_pathname (savepoint_temp_p->pathname);
  if (savepoint_pathname)
    {
      /* need to be calling esm_expand_pathname ! */
      esm_copy_file (savepoint_temp_p->pathname, savepoint_pathname);
      savepoint_temp_p->savepoint = esm_make_ref (savepoint_pathname);
      return (savepoint_pathname);
    }
  return (NULL);
}

/* This will make an entry in the shadow queue if an new savepoint is needed */
/*
 * make_shadow_pathname() -
 *      return:
 *  holder_p(in) : The holder object that contains the glo primitive
 *
 *
 */

static char *
make_shadow_pathname (const DB_OBJECT * holder_p)
{
  REF *temp1, *temp2;
  char *savepoint_pathname = NULL;

  for (temp1 = Shadow_file_anchor; temp1; temp1 = temp1->next)
    {
      if (temp1->holder != holder_p)
	{
	  continue;
	}

      if (temp1->savepoint != NULL)
	{
	  for (temp2 = temp1; temp2->savepoint != NULL;
	       temp2 = temp2->savepoint);
	}
      else
	{
	  temp2 = temp1;
	}

      savepoint_pathname = temp2->pathname = esm_add_savepoint (temp2);

      if (temp2->pathname == NULL)
	{
	  continue;
	}
      esm_log_shadow_file (temp2->pathname, temp1->pathname);
      temp1->savepoint_detected = NOT_DETECTED;
      break;

    }
  return (savepoint_pathname);
}



/*
 * esm_make_shadow_file()
 *      return: none
 *  holder_p(in) : The holder object that contains the glo primitive
 *
 *   If a shadow file has been created for an FBO, add an entry to delete the
 *   shadow file during commit.
 */

char *
esm_make_shadow_file (DB_OBJECT * holder_p)
{
  char *pathname;
  char *shadow_pathname = NULL;
  DB_VALUE value1, value2;
  DB_OBJECT *name_object_p;
  DB_ELO *glo_p;
  int rc;

  shadow_pathname = make_shadow_pathname (holder_p);
  if (shadow_pathname != NULL)
    {
      return (shadow_pathname);
    }
  rc = db_get (holder_p, GLO_HOLDER_NAME_PTR, &value1);

  if (rc != 0)
    {
      goto end;
    }

  name_object_p = DB_GET_OBJECT (&value1);
  rc = db_get (name_object_p, GLO_NAME_PATHNAME, &value2);

  if (rc != 0)
    {
      goto end;
    }
  pathname = (char *) DB_GET_STRING (&value2);

  if (pathname == NULL)
    {
      pr_clear_value (&value2);
      goto end;
    }

  shadow_pathname = esm_make_shadow_pathname (pathname);
  if (shadow_pathname == NULL)
    {
      pr_clear_value (&value2);
      goto end;
    }

  rc = db_get (holder_p, GLO_HOLDER_GLO_NAME, &value1);

  if (rc != 0)
    {
      pr_clear_value (&value2);
      goto end;
    }

  glo_p = DB_GET_ELO (&value1);
  if (glo_p == NULL)
    {
      pr_clear_value (&value2);
      goto end;
    }

  if (esm_copy_file (pathname, shadow_pathname))
    {
      esm_queue_shadow_file (shadow_pathname, holder_p);
      esm_log_shadow_file (shadow_pathname, pathname);
    }

  pr_clear_value (&value2);

end:
  return (shadow_pathname);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * esm_process_savepoint() - creates new shadow files for all open FBOs
 *      return: none
 *
 * Note:
 *   Marks the shadow file structures so that any updates to FBOs will be
 *   done in context of a user specified savepoint.
 */

void
esm_process_savepoint (void)
{
  REF *temp;

  for (temp = Shadow_file_anchor; temp; temp = temp->next)
    {
      temp->savepoint_detected = USER_SPECIFIED_DETECTED;
    }
}

/*
 * esm_process_system_savepoint() - creates new shadow files for all open FBOs
 *      return: none
 * Note:
 *   Marks the shadow file structures so that any updates to FBOs will be
 *   done in context of an "internal" savepoint.
 *
 */

void
esm_process_system_savepoint (void)
{
  REF *temp;

  for (temp = Shadow_file_anchor; temp; temp = temp->next)
    {
      temp->savepoint_detected = SYSTEM_SPECIFIED_DETECTED;
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * esm_make_dropped_shadow_file() - Creates a shadow file for an FBO
 *      return: returns the db_error condition if a failure occurs
 *  holder_p(in) : The holder object that contains the glo primitive
 *
 */

int
esm_make_dropped_shadow_file (DB_OBJECT * holder_p)
{
  char *pathname = NULL;
  int ref_count;
  int error = NO_ERROR;

  pathname = esm_get_shadow_file_pathname (holder_p);
  if (pathname != NULL)
    {
      esm_log_shadow_file ("", pathname);
    }

  error = esm_find_glo_count (holder_p, &ref_count);

  if (error != NO_ERROR)
    {
      return error;
    }

  if (ref_count == 1)
    {
      error = esm_drop_name_and_holder (holder_p);
    }

  return (error);
}

/*
 * esm_get_shadow_file_name() - Returns the name of the shadow file
 *                              for the Glo (FBO)
 *      return: returns the db_error condition if a failure occurs
 *  glo_p(in) : The glo object that should have a shadow file
 *  path(in/out) : returns the shadow file pathname
 *
 * Note:
 *   If this is a LO, return NULL.
 */

int
esm_get_shadow_file_name (DB_OBJECT * glo_p, char **path)
{
  int error;
  DB_VALUE value;
  DB_OBJECT *holder_p;
  REF *entry = NULL, *entry2 = NULL;

  error = db_get (glo_p, GLO_CLASS_HOLDER_NAME, &value);

  if (error != NO_ERROR)
    {
      goto end;
    }

  holder_p = DB_GET_OBJECT (&value);

  if (holder_p == NULL)
    {
      goto end;
    }

  for (entry = Shadow_file_anchor; entry; entry = entry->next)
    {
      if (holder_p != entry->holder)
	{
	  continue;
	}
      for (entry2 = entry->savepoint; entry2 && entry2->savepoint;
	   entry2 = entry2->savepoint);

      if (entry2 != NULL)
	{
	  *path = entry2->pathname;
	}
      else
	{
	  *path = entry->pathname;
	}
      return (NO_ERROR);
    }

end:
  *path = NULL;
  return (error);
}
