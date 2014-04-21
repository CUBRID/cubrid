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
 * es.c - external storage API  (at client and server)
 */

#include "config.h"

#include <assert.h>

#include "error_code.h"
#include "system_parameter.h"
#include "error_manager.h"
#include "network_interface_cl.h"
#include "es.h"
#include "es_posix.h"
#include "es_owfs.h"

/*
 * es_storage_type - to be set by es_init() and to be reset by es_final()
 */
static ES_TYPE es_initialized_type = ES_NONE;

/*
 * es_init - initialization action of External Storage module
 *
 * return: error code
 */
int
es_init (const char *uri)
{
  int ret;
  ES_TYPE es_type;

  assert (uri != NULL);

  es_type = es_get_type (uri);
  if (es_type == ES_NONE)
    {
      ret = ER_ES_INVALID_PATH;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1, uri);
      return ret;
    }

  if (es_initialized_type == es_type)
    {
      return NO_ERROR;
    }
  else
    {
      es_final ();
    }

  es_initialized_type = es_type;
  if (es_initialized_type == ES_OWFS)
    {
#if defined(WINDOWS)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS",
	      "not supported");
      ret = ER_ES_GENERAL;
#else /* WINDOWS */
      ret = es_owfs_init (ES_POSIX_PATH_POS (uri));
#endif /* !WINDOWS */
    }
  else if (es_initialized_type == ES_POSIX)
    {
      ret = es_posix_init (ES_POSIX_PATH_POS (uri));
      if (ret == ER_ES_GENERAL)
	{
	  /*
	   * If es_posix_init() failed (eg.failed to open base dir), 
	   * ignore the error in order to start server normally. 
	   */
	  ret = NO_ERROR;
	}
    }
  else
    {
      ret = ER_ES_INVALID_PATH;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1, uri);
    }

  srand (time (NULL));

  return ret;
}

/*
 * es_final - cleanup action of External Storage module
 *
 * return: none
 */
void
es_final (void)
{
  if (es_initialized_type == ES_OWFS)
    {
#if defined(WINDOWS)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS",
	      "not supported");
#else /* WINDOWS */
      es_owfs_final ();
#endif /* !WINDOWS */
    }
  else if (es_initialized_type == ES_POSIX)
    {
      es_posix_final ();
    }
  es_initialized_type = ES_NONE;
}

/*
 * es_create_file -
 *
 * return: error code
 * out_uri(out):
 */
int
es_create_file (char *out_uri)
{
  int ret;

  assert (out_uri != NULL);

  if (es_initialized_type == ES_OWFS)
    {
#if defined(WINDOWS)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS",
	      "not supported");
      ret = ER_ES_GENERAL;
#else /* WINDOWS */
      strncpy (out_uri, ES_OWFS_PATH_PREFIX, sizeof (ES_OWFS_PATH_PREFIX));
      ret = es_owfs_create_file (ES_OWFS_PATH_POS (out_uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_create_file: es_owfs_create_file() -> %s: %d\n",
		    out_uri, ret);
#endif /* !WINDOWS */
    }
  else if (es_initialized_type == ES_POSIX)
    {
      strncpy (out_uri, ES_POSIX_PATH_PREFIX, sizeof (ES_POSIX_PATH_PREFIX));
#if defined (CS_MODE)
      ret = es_posix_create_file (ES_POSIX_PATH_POS (out_uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_create_file: es_posix_create_file() -> %s: %d\n",
		    out_uri, ret);
#else /* CS_MODE */
      ret = xes_posix_create_file (ES_POSIX_PATH_POS (out_uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_create_file: xes_posix_create_file() -> %s: %d\n",
		    out_uri, ret);
#endif /* SERVER_MODE  || SA_MODE */
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_NO_LOB_PATH, 0);
      return ER_ES_NO_LOB_PATH;
    }

  return ret;
}

/*
 * es_write_file -
 *
 * return: error code
 * uri(in)
 * buf(in)
 * count(in)
 * offset(in)
 */
ssize_t
es_write_file (const char *uri, const void *buf, size_t count, off_t offset)
{
  ssize_t ret;
  ES_TYPE es_type;

  assert (uri != NULL);
  assert (buf != NULL);
  assert (count > 0);
  assert (offset >= 0);

  if (es_initialized_type == ES_NONE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_NO_LOB_PATH, 0);
      return ER_ES_NO_LOB_PATH;
    }

  es_type = es_get_type (uri);
  if (es_type == ES_OWFS)
    {
#if defined(WINDOWS)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS",
	      "not supported");
      ret = ER_ES_GENERAL;
#else /* WINDOWS */
      ret = es_owfs_write_file (ES_OWFS_PATH_POS (uri), buf, count, offset);
      er_log_debug (ARG_FILE_LINE, "es_write_file: es_owfs_write_file(%s,"
		    " count %d, offset %ld) -> %d\n",
		    uri, count, offset, ret);
#endif /* !WINDOWS */
    }
  else if (es_type == ES_POSIX)
    {
#if defined (CS_MODE)
      ret = es_posix_write_file (ES_POSIX_PATH_POS (uri), buf, count, offset);
      er_log_debug (ARG_FILE_LINE, "es_write_file: es_posix_write_file(%s"
		    " count %d, offset %ld) -> %d\n",
		    uri, count, offset, ret);
#else /* CS_MODE */
      ret =
	xes_posix_write_file (ES_POSIX_PATH_POS (uri), buf, count, offset);
      er_log_debug (ARG_FILE_LINE, "es_write_file: xes_posix_write_file(%s"
		    " count %d, offset %ld) -> %d\n",
		    uri, count, offset, ret);
#endif /* SERVER_MODE  || SA_MODE */
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, uri);
      return ER_ES_INVALID_PATH;
    }

  return ret;
}

/*
 * es_read_file -
 *
 * return:
 * uri(in)
 * buf(out)
 * count(in)
 * offset(in)
 */
ssize_t
es_read_file (const char *uri, void *buf, size_t count, off_t offset)
{
  ssize_t ret;
  ES_TYPE es_type;

  assert (uri != NULL);
  assert (buf != NULL);
  assert (count > 0);
  assert (offset >= 0);

  if (es_initialized_type == ES_NONE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_NO_LOB_PATH, 0);
      return ER_ES_NO_LOB_PATH;
    }

  es_type = es_get_type (uri);
  if (es_type == ES_OWFS)
    {
#if defined(WINDOWS)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS",
	      "not supported");
      ret = ER_ES_GENERAL;
#else /* WINDOWS */
      ret = es_owfs_read_file (ES_OWFS_PATH_POS (uri), buf, count, offset);
      er_log_debug (ARG_FILE_LINE, "es_read_file: (es_owfs_read_file(%s,"
		    " count %d, offset %ld) -> %d\n",
		    uri, count, offset, ret);
#endif /* !WINDOWS */
    }
  else if (es_type == ES_POSIX)
    {
#if defined (CS_MODE)
      ret = es_posix_read_file (ES_POSIX_PATH_POS (uri), buf, count, offset);
      er_log_debug (ARG_FILE_LINE, "es_read_file: es_posix_read_file(%s,"
		    " count %d, offset %ld) -> %d\n",
		    uri, count, offset, ret);
#else /* CS_MODE */
      ret = xes_posix_read_file (ES_POSIX_PATH_POS (uri), buf, count, offset);
      er_log_debug (ARG_FILE_LINE, "es_read_file: xes_posix_read_file(%s,"
		    " count %d, offset %ld) -> %d\n",
		    uri, count, offset, ret);
#endif /* SERVER_MODE  || SA_MODE */
    }
  else if (es_type == ES_LOCAL)
    {
      ret = es_local_read_file (ES_LOCAL_PATH_POS (uri), buf, count, offset);
      er_log_debug (ARG_FILE_LINE, "es_read_file: es_local_read_file(%s,"
		    " count %d, offset %ld) -> %d\n",
		    uri, count, offset, ret);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, uri);
      return ER_ES_INVALID_PATH;
    }

  return ret;
}

/*
 * es_delete_file -
 *
 * return:
 * uri(in):
 */
int
es_delete_file (const char *uri)
{
  int ret;
  ES_TYPE es_type;

  assert (uri != NULL);

  if (es_initialized_type == ES_NONE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_NO_LOB_PATH, 0);
      return ER_ES_NO_LOB_PATH;
    }

  es_type = es_get_type (uri);
  if (es_type == ES_OWFS)
    {
#if defined(WINDOWS)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS",
	      "not supported");
      ret = ER_ES_GENERAL;
#else /* WINDOWS */
      ret = es_owfs_delete_file (ES_OWFS_PATH_POS (uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_delete_file: es_owfs_delete_file(%s) -> %d\n",
		    uri, ret);
#endif /* !WINDOWS */
    }
  else if (es_type == ES_POSIX)
    {
#if defined (CS_MODE)
      ret = es_posix_delete_file (ES_POSIX_PATH_POS (uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_delete_file: es_posix_delete_file(%s) -> %d\n",
		    uri, ret);
#else /* CS_MODE */
      ret = xes_posix_delete_file (ES_POSIX_PATH_POS (uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_delete_file: xes_posix_delete_file(%s) -> %d\n",
		    uri, ret);
#endif /* SERVER_MODE  || SA_MODE */
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, uri);
      return ER_ES_INVALID_PATH;
    }

  return ret;
}

/*
 * es_copy_file -
 *
 * return: error code
 * in_uri(in):
 * metapath(in) : meta name combined with in_uri
 * out_uri(out):
 */
int
es_copy_file (const char *in_uri, const char *metaname, char *out_uri)
{
  int ret;
  ES_TYPE es_type;

  assert (in_uri != NULL);
  assert (out_uri != NULL);
  assert (metaname != NULL);

  if (es_initialized_type == ES_NONE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_NO_LOB_PATH, 0);
      return ER_ES_NO_LOB_PATH;
    }

  es_type = es_get_type (in_uri);
  if (es_type != es_initialized_type)
    {
      /* copy file operation is allowed only between same types */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_COPY_TO_DIFFERENT_TYPE,
	      2, es_get_type_string (es_type),
	      es_get_type_string (es_initialized_type));
      return ER_ES_COPY_TO_DIFFERENT_TYPE;
    }

  if (es_type == ES_OWFS)
    {
#if defined(WINDOWS)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS",
	      "not supported");
      ret = ER_ES_GENERAL;
#else /* WINDOWS */
      strncpy (out_uri, ES_OWFS_PATH_PREFIX, sizeof (ES_OWFS_PATH_PREFIX));
      ret = es_owfs_copy_file (ES_OWFS_PATH_POS (in_uri), metaname,
			       ES_OWFS_PATH_POS (out_uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_copy_file: es_owfs_copy_file(%s) -> %s: %d\n",
		    in_uri, out_uri, ret);
#endif /* !WINDOWS */
    }
  else if (es_type == ES_POSIX)
    {
      strncpy (out_uri, ES_POSIX_PATH_PREFIX, sizeof (ES_POSIX_PATH_PREFIX));
#if defined (CS_MODE)
      ret = es_posix_copy_file (ES_POSIX_PATH_POS (in_uri), metaname,
				ES_POSIX_PATH_POS (out_uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_copy_file: es_posix_copy_file(%s) -> %s: %d\n",
		    in_uri, out_uri, ret);
#else /* CS_MODE */
      ret = xes_posix_copy_file (ES_POSIX_PATH_POS (in_uri), metaname,
				 ES_POSIX_PATH_POS (out_uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_copy_file: xes_posix_copy_file(%s) -> %s: %d\n",
		    in_uri, out_uri, ret);
#endif /* SERVER_MODE  || SA_MODE */
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1,
	      in_uri);
      return ER_ES_INVALID_PATH;
    }

  return ret;
}

/*
 * es_rename_file - rename a locator according to the metaname
 *
 * return: error code
 * in_uri(in):
 * metapath(in) : meta name combined with in_uri
 * out_uri(out):
 */
int
es_rename_file (const char *in_uri, const char *metaname, char *out_uri)
{
  int ret;
  ES_TYPE es_type;

  assert (in_uri != NULL);
  assert (out_uri != NULL);
  assert (metaname != NULL);

  if (es_initialized_type == ES_NONE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_NO_LOB_PATH, 0);
      return ER_ES_NO_LOB_PATH;
    }

  es_type = es_get_type (in_uri);
  if (es_type != es_initialized_type)
    {
      /* copy file operation is allowed only between same types */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_COPY_TO_DIFFERENT_TYPE,
	      2, es_get_type_string (es_type),
	      es_get_type_string (es_initialized_type));
      return ER_ES_COPY_TO_DIFFERENT_TYPE;
    }

  if (es_type == ES_OWFS)
    {
#if defined(WINDOWS)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS",
	      "not supported");
      ret = ER_ES_GENERAL;
#else /* WINDOWS */
      strncpy (out_uri, ES_OWFS_PATH_PREFIX, sizeof (ES_OWFS_PATH_PREFIX));
      ret = es_owfs_rename_file (ES_OWFS_PATH_POS (in_uri), metaname,
				 ES_OWFS_PATH_POS (out_uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_copy_file: es_owfs_copy_file(%s) -> %s: %d\n",
		    in_uri, out_uri, ret);
#endif /* !WINDOWS */
    }
  else if (es_type == ES_POSIX)
    {
      strncpy (out_uri, ES_POSIX_PATH_PREFIX, sizeof (ES_POSIX_PATH_PREFIX));
#if defined (CS_MODE)
      ret = es_posix_rename_file (ES_POSIX_PATH_POS (in_uri), metaname,
				  ES_POSIX_PATH_POS (out_uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_copy_file: es_posix_copy_file(%s) -> %s: %d\n",
		    in_uri, out_uri, ret);
#else /* CS_MODE */
      ret = xes_posix_rename_file (ES_POSIX_PATH_POS (in_uri), metaname,
				   ES_POSIX_PATH_POS (out_uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_copy_file: xes_posix_copy_file(%s) -> %s: %d\n",
		    in_uri, out_uri, ret);
#endif /* SERVER_MODE  || SA_MODE */
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1,
	      in_uri);
      return ER_ES_INVALID_PATH;
    }

  return ret;
}

/*
 * es_get_file_size
 *
 * return: file size or -1 on error
 * uri(in):
*/
off_t
es_get_file_size (const char *uri)
{
  off_t ret;
  ES_TYPE es_type;

  assert (uri != NULL);

  if (es_initialized_type == ES_NONE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_NO_LOB_PATH, 0);
      return ER_ES_NO_LOB_PATH;
    }

  es_type = es_get_type (uri);
  if (es_type == ES_OWFS)
    {
#if defined(WINDOWS)
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS",
	      "not supported");
      ret = ER_ES_GENERAL;
#else /* WINDOWS */
      ret = es_owfs_get_file_size (ES_OWFS_PATH_POS (uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_copy_file: es_owfs_get_file_size(%s) -> %d\n",
		    uri, ret);
#endif /* !WINDOWS */
    }
  else if (es_type == ES_POSIX)
    {
#if defined (CS_MODE)
      ret = es_posix_get_file_size (ES_POSIX_PATH_POS (uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_copy_file: es_posix_get_file_size(%s) -> %d\n",
		    uri, ret);
#else /* CS_MODE */
      ret = xes_posix_get_file_size (ES_POSIX_PATH_POS (uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_copy_file: xes_posix_get_file_size(%s) -> %d\n",
		    uri, ret);
#endif /* SERVER_MODE  || SA_MODE */
    }
  else if (es_type == ES_LOCAL)
    {
      ret = es_local_get_file_size (ES_LOCAL_PATH_POS (uri));
      er_log_debug (ARG_FILE_LINE,
		    "es_copy_file: es_local_get_file_size(%s) -> %d\n",
		    uri, ret);
    }
  else
    {
      ret = -1;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, uri);
    }

  return ret;
}
