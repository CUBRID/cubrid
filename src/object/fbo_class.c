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
 * fbo_class.c - Implementation of the interface routines to the FBOs
 */

#ident "$Id$"

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(WINDOWS)
#include <io.h>
#else
#include <unistd.h>
#include <sys/param.h>
#include <sys/file.h>
#endif

#include "porting.h"
#include "util_func.h"
#include "memory_alloc.h"
#include "fbo_class.h"
#include "elo_recovery.h"

#define MAX_INSERT_BUFFER_SIZE 4096

static char *esm_open (char *name);
static int esm_insert_helper (const int fd, const INT64 loc,
			      char *string, const int len);
static INT64 esm_delete_helper (const int fd, const INT64 loc,
				const INT64 len);

/*
 * esm_open() -
 *      return: char *
 *  name(in) :
 *
 */

static char *
esm_open (char *name)
{
  static char pathname[PATH_MAX];
  char *return_path = NULL;

  /* avoid using realpath if we have an embedded environment varialbe, it gets
   * confused.
   */
  if (name != NULL)
    {
      if (name[0] == '$')
	{
	  return_path = name;
	}
      else
	{
	  return_path = realpath (name, pathname);
	  if (return_path == NULL)
	    {
	      if (errno == ENOENT)
		{
		  return_path = pathname;
		}
	      else
		{
		  return_path = name;
		}
	    }
	}
    }
  /* next two lines perform environment variable expansion */

  /* next two lines are blocked because return_path was already expanded. */
  return return_path;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * esm_create() - does nothing at this point
 *      return: always returns 0
 *  pathname(in) : fbo name
 *
 */

int
esm_create (char *pathname)
{
  return (0);
}

/*
 * esm_destroy() - this will delete the file associated with the fbo
 *      return: none
 *  pathname(in) : fbo name
 *
 */

void
esm_destroy (char *pathname)
{
  if (pathname)
    {
      unlink (pathname);
    }
}
#endif

/*
 * esm_get_size() - opens the fbo, seeks to the end of the file,
 *                  and returns the length
 *      return: returns the amount of data in the fbo
 *  pathname(in) : fbo name
 *
 */

INT64
esm_get_size (char *pathname)
{
  INT64 offset;
  int fd;

  if (pathname == NULL)
    {
      return (-1);
    }
  fd = open (esm_open (pathname), O_RDONLY, 0);
  if (fd < 0)
    {
      return (0);
    }
  offset = lseek (fd, (off_t) 0, SEEK_END);
  close (fd);
  return (offset);
}

/*
 * esm_read() - Opens the file, seeks to offset, and reads size bytes
 *              into buffer
 *      return: returns the number of bytes read from the fbo
 *  pathname(in) : fbo name
 *  offset(in) : position within file to start reading
 *  size(in) : amount of data to read
 *  buffer(out) : destination data buffer for the read
 *
 */

int
esm_read (char *pathname, const INT64 offset, const int size, char *buffer)
{
  int rc;
  int fd;

  if (pathname == NULL)
    {
      return (-1);
    }
  fd = open (esm_open (pathname), O_RDONLY, 0);
  if (fd < 0)
    {
      return (-1);
    }
  if (lseek (fd, offset, SEEK_SET) < 0)
    {
      close (fd);
      return (-1);
    }
  rc = (int) read (fd, buffer, size);
  close (fd);
  return (rc);
}

/*
 * esm_write() - Opens file, seeks to offset, writes size bytes
 *               from buffer to file
 *      return: The return value is the number of bytes written or -1 on error
 *  pathname(in) : pathname of object
 *  offset(in) : the starting offset to start writing into the object
 *  size(in) : the size of the buffer
 *  buffer(out) : the source data buffer containing the data to write (or
 *               overwrite) to the esm object.
 *
 */

int
esm_write (char *pathname, const INT64 offset, const int size, char *buffer)
{
  int rc;
  int fd;

  if (pathname == NULL)
    {
      return (-1);
    }

  fd = open (esm_open (pathname),
	     O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
  if (fd < 0)
    {
      return (-1);
    }
  if (lseek (fd, offset, SEEK_SET) < 0)
    {
      close (fd);
      return (-1);
    }
  rc = (int) write (fd, buffer, size);
  close (fd);
  return (rc);
}

/*
 * esm_insert_helper() - seeks to loc, moves any existing data "down" by len,
 *                       and writes the string to the file
 *      return: returns the length of data inserted, or < 0 on error
 *  fd(in) : file descriptor of the file
 *  loc(in) : location in the file to start inserting data
 *  string(out) : the data to insert into the file
 *  len(in) : length of data to insert into the file
 */

static int
esm_insert_helper (const int fd, const INT64 loc, char *string, const int len)
{
  char temp_buffer[MAX_INSERT_BUFFER_SIZE];
  off_t file_size, delta, seek_loc;
  int buf_size, rc;

  file_size = lseek (fd, (off_t) 0, SEEK_END);
  if (file_size > loc)
    {
      /*
       * Move any existing data down the file by len.
       */
      delta = file_size - loc;
      if (delta > MAX_INSERT_BUFFER_SIZE)
	{
	  buf_size = MAX_INSERT_BUFFER_SIZE;
	}
      else
	{
	  buf_size = (int) delta;
	}

      seek_loc = file_size - buf_size;
      while (delta > 0)
	{
	  /*
	   * Make sure we do not pass loc and that we do not copy more data
	   * than is needed
	   */
	  if (seek_loc < loc)
	    {
	      seek_loc = loc;
	    }

	  if (delta < buf_size)
	    {
	      buf_size = (int) delta;
	    }

	  lseek (fd, seek_loc, SEEK_SET);
	  buf_size = (int) read (fd, temp_buffer, buf_size);
	  if (buf_size <= 0)
	    {
	      break;
	    }

	  lseek (fd, seek_loc + len, SEEK_SET);
	  write (fd, temp_buffer, buf_size);
	  seek_loc -= buf_size;
	  delta -= buf_size;
	}
    }

  if (lseek (fd, loc, SEEK_SET) < 0)
    {
      close (fd);
      return (-1);
    }

  rc = write (fd, string, len);
  if (rc < 0)
    {
      close (fd);
      return (-1);
    }

  close (fd);
  return (len);
}

/*
 * esm_insert() - checks pathname and open operation, and returns value from
 *                the insert_helper routine
 *      return: returns the length of data inserted
 *  pathname(in) : fbo name
 *  offset(in) : starting point to insert data
 *  size(in) : amount of data to insert into the file
 *  buffer(out) : data to be inserted into the file
 *
 */

int
esm_insert (char *pathname, const INT64 offset, const int size, char *buffer)
{
  int fd;

  if (pathname == NULL)
    {
      return (-1);
    }
  fd = open (esm_open (pathname),
	     O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
  if (fd < 0)
    {
      return (-1);
    }
  return (esm_insert_helper (fd, offset, buffer, size));
}

/*
 * esm_delete_helper() - seeks to loc and moves all data "down" to loc
 *                       (over len). If the file is shorter than len,
 *                       we will simply truncate the file
 *      return: returns the size of data deleted
 *  fd(in) : file descriptor of the file
 *  loc(in) : length of data to delete from the file
 *  len(in) : the data to delete from the file
 */

static INT64
esm_delete_helper (const int fd, const INT64 loc, const INT64 len)
{
  char *temp_buffer;
  off_t file_size, read_loc, write_loc;
  int buf_size;

  file_size = lseek (fd, (off_t) 0, SEEK_END);
  lseek (fd, (off_t) loc, SEEK_SET);

  if (file_size - loc > len)
    {
      buf_size = MAX_INSERT_BUFFER_SIZE;
      temp_buffer = (char *) malloc (buf_size);
      if (temp_buffer == NULL)
	{
	  close (fd);
	  return (-1);
	}
      write_loc = loc;
      read_loc = loc + len;
      lseek (fd, read_loc, SEEK_SET);
      while (read_loc < file_size)
	{
	  buf_size = (int) read (fd, temp_buffer, buf_size);
	  lseek (fd, write_loc, SEEK_SET);
	  write (fd, temp_buffer, buf_size);
	  write_loc += buf_size;
	  read_loc += buf_size;
	  lseek (fd, read_loc, SEEK_SET);
	}
      free_and_init (temp_buffer);
      ftruncate (fd, write_loc);
      close (fd);
      return (len);
    }				/* Truncate file here */
  ftruncate (fd, loc);
  return ((INT64) (file_size - loc));
}

/*
 * esm_delete() - verifies pathname and open, and returns value
 *                from esm_delete_helper
 *      return: returns the size of data deleted if no errors, -1 otherwise
 *  pathname(in) : fbo name
 *  offset(in) : starting point to delete data
 *  size(in) : amount of data to delete from the file
 *
 */

INT64
esm_delete (char *pathname, INT64 offset, const INT64 size)
{
  int fd;

  if (pathname == NULL)
    {
      return (-1);
    }

  fd = open (esm_open (pathname),
	     O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
  if (fd < 0)
    {
      return (-1);
    }

  return (esm_delete_helper (fd, offset, size));
}

/*
 * esm_truncate() - This will truncate the data to the requested size
 *      return: returns size of data truncated, or -1 on error
 *  pathname(in) : fbo name
 *  offset(in) :
 *
 */

INT64
esm_truncate (char *pathname, const INT64 offset)
{
  int rc;
  int fd;
  INT64 lsize;

  if (pathname == NULL)
    {
      return (-1);
    }

  fd = open (esm_open (pathname),
	     O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
  if (fd < 0)
    {
      return (-1);
    }

  lsize = esm_get_size (pathname);
  if (lsize < 0)
    {
      return (-1);
    }

  rc = ftruncate (fd, offset);
  close (fd);

  if (rc != 0)
    {
      return rc;
    }

  return (lsize - offset);
}

/*
 * esm_append() - verifies pathname and open operation, seeks to
 *                the end of the file and then writes the new data
 *
 *      return: returns the amount of data append tot he end of the file
 *  pathname(in) : fbo name
 *  size(in) : amount of data to append to the end of the file
 *  buffer(out) : data to be appended to the end of the file
 *
 */

int
esm_append (char *pathname, const int size, char *buffer)
{
  int rc;
  int fd;

  if (pathname == NULL)
    {
      return (-1);
    }
  fd = open (esm_open (pathname),
	     O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
  if (fd < 0)
    {
      return (-1);
    }
  lseek (fd, (off_t) 0, SEEK_END);
  rc = (int) write (fd, buffer, size);
  close (fd);
  return (rc);
}
