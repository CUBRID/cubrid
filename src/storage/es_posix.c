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
 * es_posix.c - POSIX FS API for external storage supports (at server)
 */

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#if defined(WINDOWS)
#include <io.h>
#define S_ISDIR(m) ((m) & S_IFDIR)
#else
#include <unistd.h>
#include <sys/vfs.h>
#include <string.h>
#endif /* !WINDOWS */

#include "porting.h"
#include "thread_compat.hpp"
#include "error_manager.h"
#include "system_parameter.h"
#include "error_code.h"
#include "es_posix.h"
#if defined (SERVER_MODE)
#include "thread_entry.hpp"
#include "thread_manager.hpp"	// for thread_get_thread_entry_info
#endif // SERVER_MODE
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SA_MODE) || defined (SERVER_MODE)
/* es_posix_base_dir - */
static char es_base_dir[PATH_MAX];

static void es_get_unique_name (char *dirname1, char *dirname2, const char *metaname, char *filename);
static int es_make_dirs (const char *dirname1, const char *dirname2);
static void es_rename_path (char *src, char *tgt, char *metaname);

/*
 * es_posix_get_unique_name - make unique path string for external file
 *
 * return: none
 * dirname1(out): first level directory name which is generated
 * dirname2(out): second level directory name which is generated
 * filename(out): generated file name
 */
static void
es_get_unique_name (char *dirname1, char *dirname2, const char *metaname, char *filename)
{
  UINT64 unum;
  int hashval;
  int r;

#if defined(SERVER_MODE)
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  r = rand_r (&thread_p->rand_seed);
#else
  r = rand ();
#endif

  /* get unique numbers */
  unum = es_get_unique_num ();

  /* make a file name & a dir name */
  snprintf (filename, NAME_MAX, "%s.%020llu_%04d", metaname, (unsigned long long) unum, r % 10000);

  hashval = es_name_hash_func (ES_POSIX_HASH1, filename);
  snprintf (dirname1, NAME_MAX, "ces_%03d", hashval);

  hashval = es_name_hash_func (ES_POSIX_HASH2, filename);
  snprintf (dirname2, NAME_MAX, "ces_%03d", hashval);
}

/*
 * es_posix_make_dirs -
 *
 * return: error code, ER_ES_GENERAL or NO_ERROR
 * dirname1(in): first level directory name
 * dirname2(in): second level directory name
 */
static int
es_make_dirs (const char *dirname1, const char *dirname2)
{
  char dirbuf[PATH_MAX];
  int ret;

#if defined (CUBRID_OWFS_POSIX_TWO_DEPTH_DIRECTORY)
retry:
  if (snprintf (dirbuf, PATH_MAX - 1, "%s%c%s%c%s", es_base_dir, PATH_SEPARATOR, dirname1, PATH_SEPARATOR, dirname2)
      < 0)
    {
      assert (false);
      return ER_ES_INVALID_PATH;
    }
  ret = mkdir (dirbuf, 0755);
  if (ret < 0 && errno == ENOENT)
    {
      n = snprintf (dirbuf, PATH_MAX - 1, "%s%c%s", es_base_dir, PATH_SEPARATOR, dirname1);
      ret = mkdir (dirbuf, 0755);
      if (ret == 0 || errno == EEXIST)
	{
	  goto retry;
	}
    }
#else
  if (snprintf (dirbuf, PATH_MAX - 1, "%s%c%s", es_base_dir, PATH_SEPARATOR, dirname1) < 0)
    {
      assert (false);
      return ER_ES_INVALID_PATH;
    }
  ret = mkdir (dirbuf, 0744);
#endif

  if (ret < 0 && errno != EEXIST)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", dirbuf);
      return ER_ES_GENERAL;
    }
  return NO_ERROR;
}

/*
 * es_rename_path -
 *
 * return:
 * src(in): source path to be converted
 * metaname(in): new file name hint replacing a part of the src
 * tgt(out): target path
 */
static void
es_rename_path (char *src, char *tgt, char *metaname)
{
  char *s, *t;

  assert (metaname != NULL);
  /*
   * src: /.../ces_000/ces_tmp.123456789
   *                  ^
   *                  s
   */
  s = strrchr (src, PATH_SEPARATOR);
  assert (s != NULL);
  strcpy (tgt, src);
  if (s == NULL)
    {
      return;
    }
  /*
   * tgt: /.../ces_000/ces_tmp.123456789
   *                  ^
   *                  t
   */
  t = tgt + (s - src) + 1;

  /*
   * tgt: /.../ces_000/ces_tmp.123456789
   *                          ^
   *                          s
   */
  s = strchr (s, '.');
  assert (s != NULL);
  if (s == NULL)
    {
      return;
    }

  sprintf (t, "%s%s", metaname, s);
}
#endif /* SA_MODE || SERVER_MODE */


/*
 * es_posix_init - initialize posix module
 * 		   set the directory for external files
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * basedir(in): base directory path
 */
int
es_posix_init (const char *base_path)
{
#if defined(SA_MODE) || defined(SERVER_MODE)
  int ret;
  struct stat sbuf;

  ret = stat (base_path, &sbuf);
  if (ret != 0 || !S_ISDIR (sbuf.st_mode))
    {
      /* failed to open base dir */
      er_set_with_oserror (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", base_path);
      return ER_ES_GENERAL;
    }

  /* set base dir */
  strlcpy (es_base_dir, base_path, PATH_MAX);
  return NO_ERROR;
#else /* SA_MODE || SERVER_MODE */
  return NO_ERROR;
#endif /* CS_MODE */
}

/*
 * es_posix_final - finalize posix module
 *
 * return: none
 */
void
es_posix_final (void)
{
  return;
}

#if defined(SA_MODE) || defined(SERVER_MODE)
/*
 * xes_posix_create_file - create a new external file with auto generated name
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * new_path(out): file path newly created
 */
int
xes_posix_create_file (char *new_path)
{
  int fd;
  int ret, n;
  char dirname1[NAME_MAX], dirname2[NAME_MAX], filename[NAME_MAX];

retry:
  es_get_unique_name (dirname1, dirname2, "ces_temp", filename);
#if defined (CUBRID_OWFS_POSIX_TWO_DEPTH_DIRECTORY)
  n = snprintf (new_path, PATH_MAX - 1, "%s%c%s%c%s%c%s", es_base_dir, PATH_SEPARATOR, dirname1, PATH_SEPARATOR,
		dirname2, PATH_SEPARATOR, filename);
#else
  /* default */
  n = snprintf (new_path, PATH_MAX - 1, "%s%c%s%c%s", es_base_dir, PATH_SEPARATOR, dirname1, PATH_SEPARATOR, filename);
#endif
  if (n < 0)
    {
      assert (false);
      return ER_ES_INVALID_PATH;
    }

  es_log ("xes_posix_create_file(): %s\n", new_path);

#if defined (WINDOWS)
  fd = open (new_path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, S_IRWXU);
#else /* WINDOWS */
  fd = open (new_path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH | O_LARGEFILE);
#endif /* !WINDOWS */
  if (fd < 0)
    {
      if (errno == ENOENT)
	{
	  ret = es_make_dirs (dirname1, dirname2);
	  if (ret != NO_ERROR)
	    {
	      return ret;
	    }
#if defined (WINDOWS)
	  fd = open (new_path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, S_IRWXU);
#else /* WINDOWs */
	  fd = open (new_path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH | O_LARGEFILE);
#endif /* !WINDOWS */
	}
    }

  if (fd < 0)
    {
      if (errno == EEXIST)
	{
	  goto retry;
	}
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", new_path);
      return ER_ES_GENERAL;
    }
  close (fd);
  return NO_ERROR;
}

/*
 * xes_posix_write_file - write to the external file
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * path(in): file path
 */
ssize_t
xes_posix_write_file (const char *path, const void *buf, size_t count, off_t offset)
{
  struct stat pstat;
  int fd;
  ssize_t nbytes;
  size_t total = 0;

  es_log ("xes_posix_write_file(%s, count %d offset %ld)\n", path, count, offset);

  /*
   * TODO: This block of codes prevents partial update or writing at advanced
   * position or something like that.
   * This restriction is introduced due to OwFS's capability.
   * We need to reconsider about this specification.
   */
  if (stat (path, &pstat) < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", path);
      return ER_ES_GENERAL;
    }
  if (offset != pstat.st_size)
    {
      char buf[PATH_MAX];
      snprintf (buf, PATH_MAX, "offset error %s", path);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", buf);
      return ER_ES_GENERAL;
    }

#if defined (WINDOWS)
  fd = open (path, O_WRONLY | O_APPEND | O_BINARY, S_IRWXU);
#else /* WINDOWs */
  fd = open (path, O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH | O_LARGEFILE);
#endif /* !WINDOWS */
  if (fd < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", path);
      return ER_ES_GENERAL;
    }

  while (count > 0)
    {
      if (lseek (fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", path);
	  close (fd);
	  return ER_ES_GENERAL;
	}

      nbytes = write (fd, buf, (unsigned) count);
      if (nbytes <= 0)
	{
	  switch (errno)
	    {
	    case EINTR:
	    case EAGAIN:
	      continue;
	    default:
	      {
		er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", path);
		close (fd);
		return ER_ES_GENERAL;
	      }
	    }
	}

      offset += nbytes;
      count -= nbytes;
      buf = (char *) buf + nbytes;
      total += nbytes;
    }
  close (fd);

  return total;
}

/*
 * xes_posix_read_file - read from the external file
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * path(in): file path
 */
ssize_t
xes_posix_read_file (const char *path, void *buf, size_t count, off_t offset)
{
  int fd;
  ssize_t nbytes;
  size_t total = 0;

  es_log ("xes_posix_read_file(%s, count %d offset %ld)\n", path, count, offset);

#if defined (WINDOWS)
  fd = open (path, O_RDONLY | O_BINARY);
#else /* WINDOWS */
  fd = open (path, O_RDONLY | O_LARGEFILE);
#endif /* !WINDOWS */
  if (fd < 0)
    {
      if (errno == ENOENT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_FILE_NOT_FOUND, 1, path);
	  return ER_ES_FILE_NOT_FOUND;
	}
      else
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", path);
	  return ER_ES_GENERAL;
	}
    }

  while (count > 0)
    {
      if (lseek (fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", path);
	  close (fd);
	  return ER_ES_GENERAL;
	}

      nbytes = read (fd, buf, (unsigned) count);
      if (nbytes < 0)
	{
	  switch (errno)
	    {
	    case EINTR:
	    case EAGAIN:
	      continue;
	    default:
	      {
		er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", path);
		close (fd);
		return ER_ES_GENERAL;
	      }
	    }
	}
      if (nbytes == 0)
	{
	  break;
	}

      offset += nbytes;
      count -= nbytes;
      buf = (char *) buf + nbytes;
      total += nbytes;
    }
  close (fd);

  return total;
}

/*
 * xes_posix_delete_file - delete the external file
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * path(in): file path
 */
int
xes_posix_delete_file (const char *path)
{
  int ret;

  es_log ("xes_posix_delete_file(%s)\n", path);

  ret = unlink (path);
  if (ret < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", path);
      return ER_ES_GENERAL;
    }

  return NO_ERROR;
}

/*
 * xes_posix_copy_file - copy the external file to new one
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * src_path(in): file path to be copied
 * new_path(out): file path newly created
 */
int
xes_posix_copy_file (const char *src_path, char *metaname, char *new_path)
{
#define ES_POSIX_COPY_BUFSIZE		(4096 * 4)	/* 16K */

  int rd_fd, wr_fd, n;
  ssize_t ret;
  char dirname1[NAME_MAX], dirname2[NAME_MAX], filename[NAME_MAX];
  char buf[ES_POSIX_COPY_BUFSIZE];

  /* open a source file */
#if defined (WINDOWS)
  rd_fd = open (src_path, O_RDONLY | O_BINARY);
#else /* WINDOWS */
  rd_fd = open (src_path, O_RDONLY | O_LARGEFILE);
#endif /* !WINDOWS */
  if (rd_fd < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", src_path);
      return ER_ES_GENERAL;
    }

retry:
  /* create a target file */
  es_get_unique_name (dirname1, dirname2, metaname, filename);
#if defined (CUBRID_OWFS_POSIX_TWO_DEPTH_DIRECTORY)
  n = snprintf (new_path, PATH_MAX - 1, "%s%c%s%c%s%c%s", es_base_dir, PATH_SEPARATOR, dirname1, PATH_SEPARATOR,
		dirname2, PATH_SEPARATOR, filename);
#else
  /* default */
  n = snprintf (new_path, PATH_MAX - 1, "%s%c%s%c%s", es_base_dir, PATH_SEPARATOR, dirname1, PATH_SEPARATOR, filename);
#endif
  if (n < 0)
    {
      assert (false);
      return ER_ES_INVALID_PATH;
    }

  es_log ("xes_posix_copy_file(%s, %s): %s\n", src_path, metaname, new_path);

#if defined (WINDOWS)
  wr_fd = open (new_path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, S_IRWXU);
#else /* WINDOWS */
  wr_fd = open (new_path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH | O_LARGEFILE);
#endif /* !WINDOWS */
  if (wr_fd < 0)
    {
      if (errno == ENOENT)
	{
	  ret = es_make_dirs (dirname1, dirname2);
	  if (ret != NO_ERROR)
	    {
	      close (rd_fd);
	      return ER_ES_GENERAL;
	    }
#if defined (WINDOWS)
	  wr_fd = open (new_path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, S_IRWXU);
#else /* WINDOWS */
	  wr_fd = open (new_path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH | O_LARGEFILE);
#endif /* !WINDOWS */
	}
    }

  if (wr_fd < 0)
    {
      if (errno == EEXIST)
	{
	  goto retry;
	}
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", new_path);
      close (rd_fd);
      return ER_ES_GENERAL;
    }

  /* copy data */
  do
    {
      ret = read (rd_fd, buf, ES_POSIX_COPY_BUFSIZE);
      if (ret == 0)
	{
	  break;		/* end of file */
	}
      else if (ret < 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", src_path);
	  break;
	}

      ret = write (wr_fd, buf, (unsigned) ret);
      if (ret <= 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", new_path);
	  break;
	}
    }
  while (ret > 0);

  close (rd_fd);
  close (wr_fd);

  return (ret < 0) ? ER_ES_GENERAL : NO_ERROR;
}

/*
 * es_posix_rename_file - convert a locator & file path according to the metaname
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * src_path(in): file path to rename
 * metapath(in) : meta name combined with src_path
 * new_path(out): new file path
 */
int
xes_posix_rename_file (const char *src_path, const char *metaname, char *new_path)
{
  int ret;

  es_rename_path ((char *) src_path, new_path, (char *) metaname);

  es_log ("xes_posix_rename_file(%s, %s): %s\n", src_path, metaname, new_path);

  ret = os_rename_file (src_path, new_path);

  return (ret < 0) ? ER_ES_GENERAL : NO_ERROR;
}


/*
 * xes_posix_get_file_size - get the size of the external file
 *
 * return: file size, or ER_ES_GENERAL
 * path(in): file path
 */
off_t
xes_posix_get_file_size (const char *path)
{
  int ret;
  struct stat pstat;

  es_log ("xes_posix_get_file_size(%s)\n", path);

  ret = stat (path, &pstat);
  if (ret < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "POSIX", path);
      return -1;
    }

  return pstat.st_size;
}
#endif /* SA_MODE || SERVER_MODE */

/*
 * es_local_read_file - read from the local file
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * path(in): file path
 */
int
es_local_read_file (const char *path, void *buf, size_t count, off_t offset)
{
  int fd;
  ssize_t nbytes;
  size_t total = 0;

  es_log ("es_local_read_file(%s, count %d offset %ld)\n", path, count, offset);

#if defined (WINDOWS)
  fd = open (path, O_RDONLY | O_BINARY);
#else /* WINDOWS */
  fd = open (path, O_RDONLY | O_LARGEFILE);
#endif /* !WINDOWS */
  if (fd < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "LOCAL", path);
      return ER_ES_GENERAL;
    }

  while (count > 0)
    {
      if (lseek (fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "LOCAL", path);
	  close (fd);
	  return ER_ES_GENERAL;
	}

      nbytes = read (fd, buf, (unsigned) count);
      if (nbytes < 0)
	{
	  switch (errno)
	    {
	    case EINTR:
	    case EAGAIN:
	      continue;
	    default:
	      {
		er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "LOCAL", path);
		close (fd);
		return ER_ES_GENERAL;
	      }
	    }
	}
      if (nbytes == 0)
	{
	  break;
	}

      offset += nbytes;
      count -= nbytes;
      buf = (char *) buf + nbytes;
      total += nbytes;
    }
  close (fd);

  return (int) total;
}

/*
 * es_local_get_file_size - get the size of the external file
 *
 * return: file size, or ER_ES_GENERAL
 * path(in): file path
 */
off_t
es_local_get_file_size (const char *path)
{
  int ret;
  struct stat pstat;

  es_log ("es_local_get_file_size(%s)\n", path);

  ret = stat (path, &pstat);
  if (ret < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "LOCAL", path);
      return -1;
    }

  return pstat.st_size;
}
